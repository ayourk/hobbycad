#!/bin/sh
# =====================================================================
#  tests/catalog/run.sh — the command line's compiled-in translations
#  SPDX-License-Identifier: GPL-3.0-only
# =====================================================================
#  Generates a table from fixture catalogs with scripts/ts2cpp.py,
#  compiles it beside tests/catalog/lookup.cpp and runs the result. The
#  library is not linked: hobbycad/translate.h is header-only, so this
#  needs no build of the project, no Qt and no gettext, and runs the same
#  against a library-only Qt-free build.
#
#  Usage:  tests/catalog/run.sh [build dir, ignored]
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
GEN=$ROOT/scripts/ts2cpp.py

if ! command -v python3 > /dev/null 2>&1; then
    echo "  [FAIL] catalog: python3 is needed to generate the table"
    echo "catalog tests: FAILURES"
    exit 1
fi
CXX=${CXX:-c++}
if ! command -v "$CXX" > /dev/null 2>&1; then
    echo "  [FAIL] catalog: no C++ compiler found"
    echo "catalog tests: FAILURES"
    exit 1
fi

OUT=$(mktemp -d); trap 'rm -rf "$OUT"' EXIT
TS=$OUT/ts
mkdir -p "$TS"

# ---- fixture catalogs -----------------------------------------------
#  German carries everything, French only one message (so its other
#  entries are null), Dutch the text that has to survive being written
#  as a C++ literal.
cat > "$TS/hobbycad_de.ts" <<'EOF'
<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="de_DE">
<context>
    <name>QObject</name>
    <message>
        <source>Design rejected</source>
        <translation>Design abgelehnt</translation>
    </message>
    <message>
        <source>Copy</source>
        <comment>edit.copy</comment>
        <translation>Kopieren (Bearbeiten)</translation>
    </message>
    <message>
        <source>Copy</source>
        <comment>sketch.transform.copy</comment>
        <translation>Kopieren (Skizze)</translation>
    </message>
    <message>
        <source>Aardvark</source>
        <translation>Erdferkel</translation>
    </message>
    <message>
        <source>Zulu</source>
        <translation>Zulu-Zeit</translation>
    </message>
    <message>
        <source>Quoted</source>
        <translation>nicht benutzt</translation>
    </message>
</context>
</TS>
EOF

cat > "$TS/hobbycad_fr.ts" <<'EOF'
<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="fr">
<context>
    <name>QObject</name>
    <message>
        <source>Copy</source>
        <comment>edit.copy</comment>
        <translation>Copier</translation>
    </message>
</context>
</TS>
EOF

cat > "$TS/hobbycad_nl.ts" <<'EOF'
<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="nl">
<context>
    <name>QObject</name>
    <message>
        <source>Quoted</source>
        <translation>Hij zei "ga" en 5° en \ terug</translation>
    </message>
</context>
</TS>
EOF

python3 "$GEN" --out "$OUT/fixture_catalogs.cpp" "$TS" > /dev/null 2>&1 \
    || { echo "  [FAIL] catalog: the generator refused the fixtures"
         echo "catalog tests: FAILURES"; exit 1; }

"$CXX" -std=c++17 -Wall -Wextra -I"$ROOT/src/libhobbycad" \
    "$ROOT/tests/catalog/lookup.cpp" "$OUT/fixture_catalogs.cpp" -o "$OUT/lookup" \
    || { echo "  [FAIL] catalog: the generated table did not compile"
         echo "catalog tests: FAILURES"; exit 1; }

if "$OUT/lookup"; then
    echo "catalog tests: ALL PASS"
else
    echo "catalog tests: FAILURES"
    exit 1
fi
