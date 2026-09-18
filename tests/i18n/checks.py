#!/usr/bin/env python3
# =====================================================================
#  tests/i18n/checks.py — the catalogs, and the generator that reads them
# =====================================================================
#  SPDX-License-Identifier: GPL-3.0-only
#
#  Two jobs. First, the catalogs themselves: the key a front end looks a
#  message up by is (context, disambiguation, source), and 24 messages
#  differ only by the disambiguation, so a key that drops it silently
#  merges them. Second, scripts/ts2cpp.py, whose output is COMPILED: a
#  catalog is hostile input, and an escaping mistake there is not a crash
#  but arbitrary code in the build.
# =====================================================================

import os
import re
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

ROOT = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else ".")
CATALOGS = os.path.join(ROOT, "resources", "translations")
GEN = os.path.join(ROOT, "scripts", "ts2cpp.py")
CLI_CONTEXTS = "QObject,hobbycad::Commands"

failures = 0
skipped = 0


def check(ok, what):
    global failures
    print("  [%s] %s" % ("PASS" if ok else "FAIL", what))
    if not ok:
        failures += 1


def skip(what):
    global skipped
    skipped += 1
    print("  [SKIP] %s" % what)


def messages(path, contexts=None):
    """(context, disambiguation, source) -> translation, for one catalog."""
    out = {}
    tree = ET.parse(path)
    for context in tree.getroot().findall("context"):
        name = context.findtext("name") or ""
        if contexts and name not in contexts:
            continue
        for message in context.findall("message"):
            node = message.find("translation")
            text = "".join(node.itertext()) if node is not None else ""
            out[(name, message.findtext("comment") or "",
                 message.findtext("source") or "")] = text
    return out


def run_gen(args, expect_out=None):
    """The generator, with its exit code and whether it wrote."""
    result = subprocess.run([sys.executable, GEN] + args, capture_output=True, text=True)
    wrote = bool(expect_out) and os.path.exists(expect_out)
    return result, wrote


def write(path, text):
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)
    return path


def catalog(body, language="de_DE"):
    return ('<?xml version="1.0" encoding="utf-8"?>\n<!DOCTYPE TS>\n'
            '<TS version="2.1" language="%s">\n%s\n</TS>\n' % (language, body))


def one_message(source, translation, context="QObject", comment=None):
    return ("<context>\n  <name>%s</name>\n  <message>\n%s"
            "    <source>%s</source>\n    <translation>%s</translation>\n"
            "  </message>\n</context>" %
            (context,
             "    <comment>%s</comment>\n" % comment if comment else "",
             source, translation))


# ---- the catalogs ---------------------------------------------------

print("i18n")

de = messages(os.path.join(CATALOGS, "hobbycad_de.ts"))
full_keys = set(de)
without_dis = {(c, s) for c, _d, s in de}
check(len(full_keys) == len(de), "every message has its own (context, disambiguation, source)")
check(len(without_dis) < len(full_keys),
      "and dropping the disambiguation would merge messages, so the key keeps all three")
groups = len({(c, s) for c, d, s in de
               if len({dd for cc, dd, ss in de if (cc, ss) == (c, s)}) > 1})
check(groups == 24 and len(full_keys) - len(without_dis) == 30,
      "the 24 known groups merge, costing 30 keys (edit.copy vs sketch.transform.copy, ...)")

counts = {}
for name in sorted(os.listdir(CATALOGS)):
    if name.startswith("hobbycad_") and name.endswith(".ts"):
        counts[name] = len(messages(os.path.join(CATALOGS, name)))
check(len(set(counts.values())) == 1 and len(counts) == 15,
      "all 15 catalogs carry the same %d messages" % next(iter(counts.values())))

# A plural needs a count, which hobbycad::translate() cannot carry.
plural = subprocess.run(["grep", "-rn", "%n",
                         os.path.join(ROOT, "src", "libhobbycad"),
                         os.path.join(ROOT, "src", "hobbycad", "cli")],
                        capture_output=True, text=True)
check(plural.returncode != 0 and not plural.stdout.strip(),
      "no %n plural is reachable from the library or the command layer")

# ---- the command line shows translated names ------------------------
#  constraintTypeName() is the keyword a person types and must stay
#  English; a message must carry the display name instead, or a German
#  sentence ends up with an English word in it.
engine = open(os.path.join(ROOT, "src", "hobbycad", "cli", "cliengine.cpp"),
              encoding="utf-8").read()
check("std::string(sketch::constraintTypeName(" not in engine
      and "std::string(sk::constraintTypeName(" not in engine,
      "no message interpolates the constraint keyword")
check(engine.count("constraintTypeName(") == 2,
      "the keyword survives only where input is parsed (and in the comment saying so)")
check("constraintDisplayContext()" in engine and "entityTypeDisplayName(" in engine,
      "and the display names come from the library's translated helpers")

# ---- the generator, on the real catalogs ----------------------------

tmp = tempfile.mkdtemp(prefix="hobbycad-i18n-")
try:
    out = os.path.join(tmp, "cli_catalogs.cpp")
    result, _ = run_gen(["--contexts", CLI_CONTEXTS, "--out", out, CATALOGS])
    check(result.returncode == 0 and os.path.exists(out), "the generator reads the real catalogs")
    generated = open(out, encoding="utf-8").read() if os.path.exists(out) else ""
    check("kLanguages[] = {\n    {\"en\", nullptr}," in generated,
          "English is language 0 and carries no table of its own")
    check(all(ord(c) < 0x80 for c in generated), "the generated unit is pure ASCII")
    check("__DATE__" not in generated and ROOT not in generated,
          "and holds no timestamp and no build path, so the build stays reproducible")

    # Writing again must not touch the file: an unchanged catalog should
    # not force a recompile.
    before = os.stat(out).st_mtime_ns
    run_gen(["--contexts", CLI_CONTEXTS, "--out", out, CATALOGS])
    check(os.stat(out).st_mtime_ns == before, "a second run with no change leaves the file alone")

    # English as language 0 proves the escaping: every emitted English
    # string must be its source, byte for byte.
    result, _ = run_gen(["--contexts", CLI_CONTEXTS, "--langs", "en", CATALOGS])
    keys = re.findall(r'^    \{"((?:[^"\\]|\\.)*)", "((?:[^"\\]|\\.)*)", "((?:[^"\\]|\\.)*)"\},$',
                      result.stdout, re.M)
    block = re.search(r"const char\* const kText_en\[kCount\] = \{\n(.*?)\n\};",
                      result.stdout, re.S)
    values = re.findall(r'^    (?:"((?:[^"\\]|\\.)*)"|nullptr),$',
                        block.group(1), re.M) if block else []

    def unescape(literal):
        raw = bytearray()
        i = 0
        while i < len(literal):
            if literal[i] != "\\":
                raw.append(ord(literal[i]))
                i += 1
            elif literal[i + 1] == '"':
                raw.append(0x22)
                i += 2
            elif literal[i + 1] == "\\":
                raw.append(0x5C)
                i += 2
            else:
                raw.append(int(literal[i + 1:i + 4], 8))
                i += 4
        return bytes(raw)

    check(len(keys) == len(values) and len(keys) > 400,
          "the English pass emits a real table (%d messages)" % len(keys))
    check(all(unescape(v) == unescape(k[2]) for k, v in zip(keys, values) if v),
          "every English string survives escaping byte for byte")
    check(keys == sorted(keys, key=lambda k: (unescape(k[0]), unescape(k[1]), unescape(k[2]))),
          "and the table is sorted the way the compiled lookup bisects it")

    # ---- hostile catalogs -------------------------------------------
    #  Each must be refused with a non-zero exit and no output written.
    bomb = ('<?xml version="1.0"?><!DOCTYPE TS [\n'
            '<!ENTITY a "aaaaaaaaaa"><!ENTITY b "&a;&a;&a;&a;&a;&a;&a;&a;&a;&a;">\n'
            ']><TS version="2.1" language="de_DE">' +
            one_message("x", "&b;") + "</TS>")
    hostile = [
        ("an entity bomb", write(os.path.join(tmp, "hobbycad_de.ts"), bomb)),
        ("a DOCTYPE with an internal subset",
         write(os.path.join(tmp, "hobbycad_es.ts"),
               '<?xml version="1.0"?><!DOCTYPE TS [ ]><TS version="2.1" language="es">'
               + one_message("x", "y") + "</TS>")),
        ("a string past the length cap",
         write(os.path.join(tmp, "hobbycad_fr.ts"),
               catalog(one_message("x", "y" * 9000)))),
        ("a locale a C++ identifier cannot carry",
         write(os.path.join(tmp, 'hobbycad_de; #include "x".ts'), catalog(one_message("a", "b")))),
    ]
    for what, path in hostile:
        target = os.path.join(tmp, "refused.cpp")
        if os.path.exists(target):
            os.remove(target)
        result, wrote = run_gen(["--out", target, path], target)
        check(result.returncode != 0 and not wrote, "refused: %s" % what)

    # A catalog over the size cap: valid XML, just far too much of it, so
    # only the cap can refuse it.
    padded = os.path.join(tmp, "hobbycad_pl.ts")
    with open(padded, "w", encoding="utf-8") as f:
        f.write('<?xml version="1.0" encoding="utf-8"?>\n<!DOCTYPE TS>\n'
                '<TS version="2.1" language="pl">\n<!-- ')
        f.write("padding " * (9 * 1024 * 1024 // 8))
        f.write(" -->\n" + one_message("x", "y") + "\n</TS>\n")
    target = os.path.join(tmp, "refused.cpp")
    if os.path.exists(target):
        os.remove(target)
    result, wrote = run_gen(["--out", target, padded], target)
    check(result.returncode != 0 and not wrote, "refused: a catalog past the size cap")

    binary = os.path.join(tmp, "hobbycad_it.ts")
    with open(binary, "wb") as f:
        f.write(b'<?xml version="1.0"?><!DOCTYPE TS><TS language="it"><context><name>QObject'
                b"</name><message><source>x</source><translation>\xff\xfe</translation>"
                b"</message></context></TS>")
    target = os.path.join(tmp, "refused.cpp")
    if os.path.exists(target):
        os.remove(target)
    result, wrote = run_gen(["--out", target, binary], target)
    check(result.returncode != 0 and not wrote, "refused: a catalog that is not UTF-8")

    # ---- an injection payload stays text ----------------------------
    payload = '"); system("touch /tmp/pwned"); //'
    inject = write(os.path.join(tmp, "hobbycad_nl.ts"),
                   catalog(one_message("safe", payload.replace("&", "&amp;")
                                       .replace("<", "&lt;").replace(">", "&gt;"))))
    out2 = os.path.join(tmp, "inject.cpp")
    result, _ = run_gen(["--out", out2, inject])
    text = open(out2, encoding="utf-8").read() if os.path.exists(out2) else ""
    emitted = re.findall(r'^    "((?:[^"\\]|\\.)*)",$', text, re.M)
    decoded = [unescape(e).decode("utf-8", "replace") for e in emitted]
    check(result.returncode == 0 and payload in decoded,
          "a translation carrying C++ comes back as text")
    check('\\"' in text and '") ;' not in text and not re.search(r'^\s*[^"/\s].*system\(',
                                                              text, re.M),
          "and its quotes are escaped, so it cannot close a literal or run")

    compiler = os.environ.get("CXX") or shutil.which("c++") or shutil.which("g++")
    if not compiler:
        skip("compiling the generated unit (no C++ compiler found)")
    else:
        for name, path in (("real catalogs", out), ("the injection payload", out2)):
            built = subprocess.run([compiler, "-std=c++17", "-fsyntax-only",
                                    "-I", os.path.join(ROOT, "src", "libhobbycad"), path],
                                   capture_output=True, text=True)
            check(built.returncode == 0, "the unit generated from %s compiles" % name)
finally:
    shutil.rmtree(tmp, ignore_errors=True)

print()
if failures:
    print("FAILURES (%d failure(s))" % failures)
    sys.exit(1)
print("ALL PASS (0 failure(s))%s" % (" [%d skipped]" % skipped if skipped else ""))
