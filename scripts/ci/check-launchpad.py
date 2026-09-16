#!/usr/bin/env python3
# =====================================================================
#  scripts/ci/check-launchpad.py — Launchpad URLs and PPA drift
#  SPDX-License-Identifier: GPL-3.0-only
#  Part of HobbyCAD (ayourk/hobbycad)
# =====================================================================
#
#  Usage: scripts/ci/check-launchpad.py [--offline] [--strict] [-v]
#
#  1. +sourcefiles URLs. Launchpad serves an upload's files under
#     .../+sourcefiles/<package>/<version>/<file> only while it keeps them:
#     once a newer upload supersedes that version the file eventually
#     returns 404, on no fixed schedule, and a Deleted upload is gone at
#     once. This scans the workflows, packaging, scripts, debian/ and
#     cmake/ for such URLs and asks Launchpad what became of each version:
#     still Published, Superseded (and by what), or Deleted, and whether
#     the file still downloads. An active URL fails the check; one inside
#     a comment (a recipe kept for later) is reported without failing.
#     The fix is always the same: copy the orig tarball to the "sources"
#     release of ayourk/hobbycad-vcpkg and pin it in versions.json.
#
#  2. PPA drift. For every versions.json dependency with a "ppa_source",
#     compares the pinned upstream version with what ppa:ayourk/hobbycad
#     currently publishes in each series, so a PPA upload that the pins
#     have not caught up with (or a pin ahead of its upload) is visible.
#     Reported as a warning; --strict makes it fail.
#
#  --offline skips every Launchpad request (the URL scan still fails on
#  active URLs). Requests are paced at two per second. Exit status: 0 clean,
#  1 findings, 2 Launchpad unreachable.
# =====================================================================
import json
import re
import subprocess
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
API = "https://api.launchpad.net/1.0"
OWNER, PPA = "ayourk", "hobbycad"
SCAN = [".github/workflows", "packaging", "scripts", "debian", "cmake", "tools", "vcpkg.json", "vcpkg-configuration.json"]
URL_RE = re.compile(r"https://launchpad\.net/~(?P<owner>[^/\s]+)/\+archive/ubuntu/(?P<ppa>[^/\s]+)/\+sourcefiles/"
                    r"(?P<pkg>[^/\s]+)/(?P<ver>[^/\s]+)/(?P<file>[^\s\"')]+)")
COMMENT_RE = re.compile(r"^\s*(#|//|\*|/\*|<!--|;)")

offline = "--offline" in sys.argv
strict = "--strict" in sys.argv
verbose = "-v" in sys.argv
failures, warnings, notes = [], [], []
_last = [0.0]


def fetch(url, method="GET"):
    """One Launchpad request, paced to about two per second."""
    wait = 0.5 - (time.monotonic() - _last[0])
    if wait > 0:
        time.sleep(wait)
    _last[0] = time.monotonic()
    req = urllib.request.Request(url, method=method, headers={"User-Agent": "hobbycad-check-launchpad"})
    # Launchpad's API answers slowly or times out now and then; a timeout
    # is retried with backoff before it counts as unreachable.
    for attempt in range(4):
        try:
            with urllib.request.urlopen(req, timeout=120) as r:
                body = r.read() if method == "GET" else b""
                return r.status, body
        except urllib.error.HTTPError as e:
            if e.code in (502, 503, 504) and attempt < 3:
                time.sleep(5 * (attempt + 1))
                continue
            return e.code, b""
        except (urllib.error.URLError, TimeoutError, OSError):
            if attempt == 3:
                raise
            time.sleep(5 * (attempt + 1))


_sources = {}


def publications(owner, ppa, pkg):
    """Every publication of a source package in a PPA, all statuses, cached."""
    key = (owner, ppa, pkg)
    if key not in _sources:
        url = f"{API}/~{owner}/+archive/ubuntu/{ppa}?ws.op=getPublishedSources&source_name={pkg}&exact_match=true"
        entries = []
        while url:
            status, body = fetch(url)
            if status != 200 or body[:15].lower().startswith(b"<!doctype") or body[:6].lower().startswith(b"<html"):
                raise ConnectionError(f"Launchpad API returned {status} for {pkg}")
            d = json.loads(body)
            entries += d.get("entries", [])
            url = d.get("next_collection_link")
        _sources[key] = entries
    return _sources[key]


def series(e):
    return e["distro_series_link"].rstrip("/").rsplit("/", 1)[-1]


def upstream(ppa_version):
    """3.2.git~20260914+p1-1~ppa1~noble1 -> 3.2.git.20260914+p1; 1.9.2+ds-6~ppa2 -> 1.9.2."""
    v = ppa_version.split(":", 1)[-1]
    v = v.rsplit("-", 1)[0] if "-" in v else v
    v = re.sub(r"\+(dfsg|ds)\d*$", "", v)
    return v.replace("~", ".")


def newer(a, b):
    """True when version a sorts above b (dpkg rules when dpkg is present)."""
    try:
        return subprocess.run(["dpkg", "--compare-versions", a, "gt", b]).returncode == 0
    except FileNotFoundError:
        return a > b


# ---- 1. +sourcefiles URLs ---------------------------------------------------
hits = []
for entry in SCAN:
    base = ROOT / entry
    files = [base] if base.is_file() else sorted(p for p in base.rglob("*") if p.is_file()) if base.exists() else []
    for f in files:
        if "/build" in str(f) or f.suffix in (".png", ".ico", ".icns", ".xpm", ".zst", ".gz", ".xz"):
            continue
        try:
            lines = f.read_text(errors="replace").splitlines()
        except OSError:
            continue
        for n, line in enumerate(lines, 1):
            if f.name == Path(__file__).name:
                continue
            for m in URL_RE.finditer(line):
                hits.append((f.relative_to(ROOT), n, bool(COMMENT_RE.match(line)), m))

try:
    for path, n, commented, m in hits:
        where = f"{path}:{n}{' (comment)' if commented else ''}"
        owner, ppa, pkg, ver = m["owner"], m["ppa"], m["pkg"], m["ver"]
        detail = f"{pkg} {ver}"
        if not offline:
            pubs = publications(owner, ppa, pkg)
            mine = [e for e in pubs if e["source_package_version"] == ver]
            current = sorted({e["source_package_version"] for e in pubs if e["status"] == "Published"})
            state = ", ".join(sorted({e["status"] for e in mine})) or "never published"
            code, _ = fetch(m.group(0), method="HEAD")
            detail += f": {state}; file {'downloads' if code == 200 else f'returns {code}'}"
            if current and ver not in current:
                detail += f"; the PPA now publishes {', '.join(current)}"
        msg = f"{where}: Launchpad +sourcefiles URL for {detail}"
        (warnings if commented else failures).append(msg)

    # ---- 2. PPA drift -------------------------------------------------------
    if not offline:
        deps = json.load(open(ROOT / "versions.json"))["dependencies"]
        for name, d in deps.items():
            src = d.get("ppa_source")
            if not src:
                continue
            pin = d["version"] + (f"+{d['patch']}" if d.get("patch") else "")
            pubs = [e for e in publications(OWNER, PPA, src) if e["status"] == "Published"]
            if not pubs:
                warnings.append(f"PPA: {name} ({src}) has nothing published")
                continue
            for s in sorted({series(e) for e in pubs}):
                vers = sorted({upstream(e["source_package_version"]) for e in pubs if series(e) == s})
                for v in vers:
                    if v == pin:
                        notes.append(f"PPA: {name} {pin} matches {s}")
                    elif newer(v, pin):
                        (failures if strict else warnings).append(f"PPA: {name} pin {pin} is behind {s}, which publishes {v}")
                    else:
                        (failures if strict else warnings).append(f"PPA: {name} pin {pin} is ahead of {s}, which publishes {v}")
except (ConnectionError, urllib.error.URLError, TimeoutError) as e:
    print(f"check-launchpad: Launchpad unreachable ({e}); rerun, or use --offline for the URL scan only")
    sys.exit(2)

if verbose:
    for x in notes:
        print("  ok   ", x)
for x in warnings:
    print("  WARN ", x)
for x in failures:
    print("  FAIL ", x)
print(f"check-launchpad: {len(failures)} failure(s), {len(warnings)} warning(s), {len(notes)} match(es)"
      f"{' (offline: no Launchpad requests)' if offline else ''}")
sys.exit(1 if failures else 0)
