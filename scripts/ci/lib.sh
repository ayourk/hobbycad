# shellcheck shell=bash
# =====================================================================
#  scripts/ci/lib.sh — shared helpers for the CI dependency scripts
#  SPDX-License-Identifier: GPL-3.0-only
#  Part of HobbyCAD (ayourk/hobbycad)
# =====================================================================
#
#  Sourced by the scripts beside it, never run. Versions, URLs and
#  checksums come from pins.sh, which tools/check-versions.py generates
#  from versions.json; nothing here names a version.
# =====================================================================
set -euo pipefail

CI_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$CI_DIR/../.." && pwd)"
# shellcheck source=pins.sh
. "$CI_DIR/pins.sh"

die() { echo "error: $*" >&2; exit 1; }
log() { echo "==> $*"; }

# pin DEPENDENCY FIELD: the pinned VERSION, URL or SHA256 of a dependency.
pin() {
    local var
    var="PIN_$(printf '%s' "$1" | tr 'a-z-' 'A-Z_')_$2"
    [ -n "${!var:-}" ] || die "nothing pinned for $1 $2 in scripts/ci/pins.sh (see versions.json)"
    printf '%s' "${!var}"
}

# Run a command as root: directly when we are root (containers), through
# sudo otherwise (hosted runners).
as_root() {
    if [ "$(id -u)" -eq 0 ] || ! command -v sudo >/dev/null 2>&1; then "$@"; else sudo "$@"; fi
}

# Install into PREFIX: through sudo only when PREFIX is not writable.
install_into() {
    local prefix=$1; shift
    if [ -w "$prefix" ] || { [ ! -e "$prefix" ] && [ -w "$(dirname "$prefix")" ]; }; then "$@"; else as_root "$@"; fi
}

sha256_of() {
    if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1
    else shasum -a 256 "$1" | cut -d' ' -f1; fi
}

require() {
    local tool
    for tool in "$@"; do command -v "$tool" >/dev/null 2>&1 || die "$tool is not installed"; done
}

# fetch_source DEPENDENCY DESTINATION: download the pinned archive, refuse
# an HTML page or a checksum mismatch, and unpack it into DESTINATION (which
# is replaced). A verified archive already in TMPDIR is reused.
fetch_source() {
    local dep=$1 dest=$2 url sha file got
    require curl tar
    url=$(pin "$dep" URL); sha=$(pin "$dep" SHA256)
    file="${TMPDIR:-/tmp}/ci-src-${url##*/}"
    if [ ! -f "$file" ] || [ "$(sha256_of "$file")" != "$sha" ]; then
        log "fetching $dep $(pin "$dep" VERSION): $url"
        curl -fL --retry 3 --retry-all-errors --connect-timeout 30 -o "$file.part" "$url" || die "download failed: $url"
        if head -c 512 "$file.part" | grep -qiE '<!doctype|<html'; then
            rm -f "$file.part"; die "$url returned an HTML page instead of the archive"
        fi
        mv -f "$file.part" "$file"
    fi
    got=$(sha256_of "$file")
    if [ "$got" != "$sha" ]; then
        rm -f "$file"; die "checksum mismatch for $dep: got $got, versions.json pins $sha"
    fi
    rm -rf "$dest"; mkdir -p "$dest"
    tar -xf "$file" -C "$dest" --strip-components=1
    log "$dep $(pin "$dep" VERSION) unpacked in $dest (sha256 verified)"
}
