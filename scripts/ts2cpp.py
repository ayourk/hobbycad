#!/usr/bin/env python3
# =====================================================================
#  scripts/ts2cpp.py — Qt catalogs to a compiled-in translation table
# =====================================================================
#  SPDX-License-Identifier: GPL-3.0-only
#
#  The Qt-free command line (hobbycad-cli) cannot call
#  QCoreApplication::translate(), so it has no way to reach the .ts
#  catalogs the application uses. This turns those catalogs into one C++
#  translation unit that is compiled into that binary: a key table shared
#  by every language, one array of string literals per language, and a
#  translator that installs itself into the library's seam
#  (hobbycad/translate.h). No catalog file is read at runtime, no
#  gettext, no libintl, and nothing new to install on Linux, Windows or
#  macOS.
#
#  The build calls this with explicit paths (see the hobbycad-cli block
#  in src/hobbycad/CMakeLists.txt). Run by hand, it finds the catalogs
#  next to itself:
#
#      scripts/ts2cpp.py --out /tmp/cli_catalogs.cpp
#      scripts/ts2cpp.py --contexts QObject --langs de,fr
#
#  THE CATALOGS ARE TREATED AS HOSTILE INPUT. This script's output is
#  compiled, so an escaping mistake is not a crash but arbitrary code in
#  the build, and a translation can arrive by pull request. Every string
#  leaves here octal-escaped, XML that can expand entities is refused
#  before parsing, and size, count and length are capped.
# =====================================================================

import argparse
import os
import re
import sys
import xml.etree.ElementTree as ET

# ---- caps -----------------------------------------------------------
#  Real catalogs are about 500 KB with 1,897 messages, the longest string
#  967 bytes. These leave room to grow and still refuse a file built to
#  exhaust the build.
MAX_FILE_BYTES = 8 * 1024 * 1024
MAX_MESSAGES = 20000
MAX_STRING_BYTES = 8 * 1024
MAX_OUTPUT_BYTES = 16 * 1024 * 1024

#  A locale token becomes part of a C++ identifier (kText_de), so it may
#  hold nothing else.
LOCALE_RE = re.compile(r"^[A-Za-z0-9_]{1,16}$")

#  Qt writes a bare "<!DOCTYPE TS>". An internal subset (the "[") is
#  where entity declarations live, and xml.etree does expand entities, so
#  a nested-entity catalog would exhaust the build. Refuse both spellings
#  rather than trusting a parser flag.
DOCTYPE_RE = re.compile(rb"<!DOCTYPE[^>\[]*\[")
ENTITY_RE = re.compile(rb"<!ENTITY")


class Refused(Exception):
    """Input this script will not process. Reported, never a traceback."""


def refuse(what):
    raise Refused(what)


# ---- reading --------------------------------------------------------

def locale_of(path):
    """The locale token from hobbycad_<loc>.ts, validated."""
    name = os.path.basename(path)
    if not name.startswith("hobbycad_") or not name.endswith(".ts"):
        refuse("%s: not a hobbycad_<locale>.ts catalog" % name)
    token = name[len("hobbycad_"):-len(".ts")]
    if not LOCALE_RE.match(token):
        refuse("%s: %r is not a locale a C++ identifier may carry" % (name, token))
    return token


def read_catalog(path, contexts):
    """(locale, {(ctx, dis, src): text}) for one .ts file."""
    locale = locale_of(path)
    size = os.path.getsize(path)
    if size > MAX_FILE_BYTES:
        refuse("%s: %d bytes, over the %d cap" % (path, size, MAX_FILE_BYTES))
    raw = open(path, "rb").read()
    if DOCTYPE_RE.search(raw):
        refuse("%s: a DOCTYPE with an internal subset is not read" % path)
    if ENTITY_RE.search(raw):
        refuse("%s: an entity declaration is not read" % path)
    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError as exc:
        refuse("%s: not UTF-8 (%s)" % (path, exc))
    try:
        root = ET.fromstring(text)
    except ET.ParseError as exc:
        refuse("%s: not readable XML (%s)" % (path, exc))

    out = {}
    count = 0
    for context in root.findall("context"):
        name = context.findtext("name") or ""
        if contexts and name not in contexts:
            continue
        for message in context.findall("message"):
            count += 1
            if count > MAX_MESSAGES:
                refuse("%s: more than %d messages" % (path, MAX_MESSAGES))
            # A plural needs a count, which the seam cannot carry, and
            # none is reachable from the library or the command layer.
            if message.get("numerus") == "yes":
                print("%s: skipping the plural %r (the seam takes no count)"
                      % (os.path.basename(path), (message.findtext("source") or "")[:40]),
                      file=sys.stderr)
                continue
            source = message.findtext("source") or ""
            node = message.find("translation")
            state = node.get("type") if node is not None else None
            if state in ("unfinished", "vanished", "obsolete"):
                continue
            value = node.text if node is not None and node.text else ""
            for field in (name, message.findtext("comment") or "", source, value):
                check_string(path, field)
            if not source or not value:
                continue
            out[(name, message.findtext("comment") or "", source)] = value
    return locale, out


def check_string(path, text):
    if len(text.encode("utf-8")) > MAX_STRING_BYTES:
        refuse("%s: a string over the %d byte cap" % (path, MAX_STRING_BYTES))
    if "\x00" in text:
        refuse("%s: a NUL inside a string" % path)
    for ch in text:
        if 0xD800 <= ord(ch) <= 0xDFFF:
            refuse("%s: a lone surrogate inside a string" % path)


def catalogs_from(paths):
    """Every hobbycad_*.ts named, or held by a named directory."""
    files = []
    for path in paths:
        if os.path.isdir(path):
            for name in sorted(os.listdir(path)):
                if name.startswith("hobbycad_") and name.endswith(".ts"):
                    files.append(os.path.join(path, name))
        else:
            files.append(path)
    return files


def default_catalog_dir():
    """resources/translations beside this script's checkout, or None."""
    here = os.path.dirname(os.path.abspath(__file__))
    guess = os.path.join(os.path.dirname(here), "resources", "translations")
    return guess if os.path.isdir(guess) else None


# ---- writing --------------------------------------------------------

def literal(text):
    """A C++ string literal that is pure ASCII whatever came in.

    Every byte outside printable ASCII leaves as a three-digit octal
    escape. Octal, not hex: a hex escape is greedy, so "\\xc3" followed
    by a letter would swallow it.
    """
    out = ['"']
    for byte in text.encode("utf-8"):
        ch = chr(byte)
        if ch == '"':
            out.append('\\"')
        elif ch == "\\":
            out.append("\\\\")
        elif 0x20 <= byte < 0x7F:
            out.append(ch)
        else:
            out.append("\\%03o" % byte)
    out.append('"')
    return "".join(out)


def generate(entries, langs, texts):
    """The translation unit. No timestamp and no path: a build that
    changes nothing produces the same bytes, which is what a
    reproducible build needs."""
    lines = [
        "// =====================================================================",
        "//  cli_catalogs.cpp - generated by scripts/ts2cpp.py, do not edit",
        "// =====================================================================",
        "//  SPDX-License-Identifier: GPL-3.0-only",
        "//",
        "//  The Qt catalogs, compiled in for the Qt-free command line. Every",
        "//  string is octal-escaped, so this file is ASCII whatever language it",
        "//  carries. Language 0 is English and holds no text of its own: its",
        "//  entries are the source strings in kKeys.",
        "// =====================================================================",
        "",
        "#include <hobbycad/translate.h>",
        "",
        "#include <cstddef>",
        "#include <cstring>",
        "",
        "namespace hobbycad {",
        "namespace cli_catalogs {",
        "namespace {",
        "",
        "struct Key {",
        "    const char* context;",
        "    const char* disambiguation;",
        "    const char* source;",
        "};",
        "",
        "// Sorted by (context, disambiguation, source) as bytes, so the",
        "// lookup below can bisect it.",
        "const Key kKeys[] = {",
    ]
    for ctx, dis, src in entries:
        lines.append("    {%s, %s, %s},"
                     % (literal(ctx), literal(dis), literal(src)))
    lines += [
        "};",
        "",
        "constexpr std::size_t kCount = sizeof(kKeys) / sizeof(kKeys[0]);",
        "",
    ]
    for lang in langs:
        row = texts.get(lang)
        if row is None:
            continue   # English with no table of its own
        lines.append("const char* const kText_%s[kCount] = {" % lang)
        for key in entries:
            value = row.get(key)
            lines.append("    %s," % (literal(value) if value else "nullptr"))
        lines += ["};", ""]

    lines += [
        "struct Language {",
        "    const char* name;",
        "    const char* const* text;   ///< nullptr: the source strings",
        "};",
        "",
        "const Language kLanguages[] = {",
    ]
    for lang in langs:
        table = "kText_%s" % lang if lang in texts else "nullptr"
        lines.append("    {%s, %s}," % (literal(lang), table))
    lines += [
        "};",
        "",
        "constexpr std::size_t kLanguageCount = sizeof(kLanguages) / sizeof(kLanguages[0]);",
        "",
        "/// Which language is installed. 0 is English, which needs no table.",
        "std::size_t g_language = 0;",
        "",
        "int compareKey(const Key& key, const char* context, const char* disambiguation,",
        "               const char* source)",
        "{",
        "    if (int c = std::strcmp(key.context, context ? context : \"\")) return c;",
        "    if (int c = std::strcmp(key.disambiguation, disambiguation ? disambiguation : \"\"))",
        "        return c;",
        "    return std::strcmp(key.source, source ? source : \"\");",
        "}",
        "",
        "std::string translateOne(const char* context, const char* source,",
        "                         const char* disambiguation)",
        "{",
        "    const char* const* row = kLanguages[g_language].text;",
        "    if (!row) return std::string();   // English: the source stands",
        "    std::size_t low = 0;",
        "    std::size_t high = kCount;",
        "    while (low < high) {",
        "        const std::size_t mid = low + (high - low) / 2;",
        "        const int order = compareKey(kKeys[mid], context, disambiguation, source);",
        "        if (order == 0) {",
        "            const char* text = row[mid];",
        "            return text ? std::string(text) : std::string();",
        "        }",
        "        if (order < 0) {",
        "            low = mid + 1;",
        "        } else {",
        "            high = mid;",
        "        }",
        "    }",
        "    return std::string();   // not in the catalog: English",
        "}",
        "",
        "}  // namespace",
        "",
        "bool install(const char* locale)",
        "{",
        "    if (!locale || !*locale) return false;",
        "    for (std::size_t i = 0; i < kLanguageCount; ++i) {",
        "        if (std::strcmp(kLanguages[i].name, locale) != 0) continue;",
        "        g_language = i;",
        "        hobbycad::setTranslator(&translateOne);",
        "        return true;",
        "    }",
        "    return false;",
        "}",
        "",
        "const char* language(std::size_t index)",
        "{",
        "    return index < kLanguageCount ? kLanguages[index].name : nullptr;",
        "}",
        "",
        "}  // namespace cli_catalogs",
        "}  // namespace hobbycad",
        "",
    ]
    return "\n".join(lines)


def write_out(path, text):
    """Atomically, and only when the content differs: an unchanged
    catalog must not re-touch the file and force a recompile."""
    data = text.encode("utf-8")
    if len(data) > MAX_OUTPUT_BYTES:
        refuse("the generated table would be %d bytes, over the %d cap"
               % (len(data), MAX_OUTPUT_BYTES))
    if os.path.exists(path) and open(path, "rb").read() == data:
        return False
    tmp = path + ".part"
    with open(tmp, "wb") as f:
        f.write(data)
        f.flush()
        os.fsync(f.fileno())
    os.replace(tmp, path)
    return True


# ---- main -----------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="Turn Qt .ts catalogs into a compiled-in C++ translation table.")
    parser.add_argument("paths", nargs="*",
                        help=".ts files, or a directory holding hobbycad_*.ts "
                             "(default: resources/translations beside this script)")
    parser.add_argument("--out", help="write here (default: stdout)")
    parser.add_argument("--contexts",
                        help="comma separated translation contexts to keep (default: all)")
    parser.add_argument("--langs",
                        help="comma separated locales to emit (default: all but English, "
                             "which is language 0 and needs no table; name 'en' to emit it "
                             "as a real array, which the round-trip test uses)")
    args = parser.parse_args()

    paths = args.paths
    if not paths:
        found = default_catalog_dir()
        if not found:
            parser.print_usage(sys.stderr)
            print("%s: no catalogs given and none found beside this script"
                  % parser.prog, file=sys.stderr)
            return 2
        paths = [found]

    contexts = set(c for c in (args.contexts or "").split(",") if c) or None
    wanted = [c for c in (args.langs or "").split(",") if c] or None

    files = catalogs_from(paths)
    if not files:
        print("%s: no hobbycad_*.ts catalogs in %s"
              % (parser.prog, ", ".join(paths)), file=sys.stderr)
        return 2

    texts = {}
    keys = set()
    for path in files:
        locale, row = read_catalog(path, contexts)
        if wanted is not None and locale not in wanted:
            continue
        # English is the extraction template: its "translations" are the
        # source repeated, so it carries no text unless asked for.
        if locale == "en" and (wanted is None or "en" not in wanted):
            keys.update(row.keys())
            continue
        texts[locale] = row
        keys.update(row.keys())

    entries = sorted(keys, key=lambda k: (k[0].encode("utf-8"),
                                          k[1].encode("utf-8"),
                                          k[2].encode("utf-8")))
    # English is language 0 whether or not it has a table of its own.
    langs = ["en"] + sorted(l for l in texts if l != "en")

    out = generate(entries, langs, texts)
    if args.out:
        changed = write_out(args.out, out)
        print("%s: %d messages, %d languages%s"
              % (os.path.basename(args.out), len(entries), len(langs),
                 "" if changed else " (unchanged)"), file=sys.stderr)
    else:
        sys.stdout.write(out)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Refused as exc:
        print("ts2cpp.py: %s" % exc, file=sys.stderr)
        sys.exit(1)
