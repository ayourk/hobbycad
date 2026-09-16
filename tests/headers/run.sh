#!/bin/sh
# =====================================================================
#  tests/headers/run.sh — header hygiene checks
#  SPDX-License-Identifier: GPL-3.0-only
# =====================================================================
#  Pure text checks, no compilation. They catch a class of bug that
#  compiles and links cleanly and then crashes at run time.
#
#  The one that motivated this: feature.h selected FeatureData's
#  `properties` member with #if HOBBYCAD_HAS_QT but never included
#  types.h, which is what DEFINES that macro. An undefined macro reads
#  as 0 to the preprocessor, so the struct's layout depended on whether
#  some earlier header happened to pull types.h in first. Two
#  translation units disagreed about what a FeatureData was: an ODR
#  violation that crashed as one, moving a QJsonObject that the other
#  side had built as an nlohmann::json.
#
#  Usage:  tests/headers/run.sh
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
LIB="$ROOT/src/libhobbycad"
fails=0

check() {
    if [ "$1" = "0" ]; then
        printf '  [PASS] %s\n' "$2"
    else
        printf '  [FAIL] %s\n' "$2"
        fails=$((fails + 1))
    fi
}

# ---- Every header that BRANCHES on HOBBYCAD_HAS_QT must include the
#      header that defines it. types.h is the definition itself.
offenders=""
for f in $(find "$LIB/hobbycad" -name '*.h'); do
    case "$f" in */types.h) continue ;; esac
    grep -q 'HOBBYCAD_HAS_QT' "$f" || continue
    # Any include of types.h, by whatever relative spelling.
    if ! grep -Eq '#include[[:space:]]*[<"].*types\.h[>"]' "$f"; then
        offenders="$offenders ${f#$ROOT/}"
    fi
done
if [ -n "$offenders" ]; then
    printf '         offenders:%s\n' "$offenders"
    check 1 "every header branching on HOBBYCAD_HAS_QT includes types.h"
else
    check 0 "every header branching on HOBBYCAD_HAS_QT includes types.h"
fi

# ---- document_undo.h must not reach OCCT.
#      It is included widely for plain undo bookkeeping; pulling in the
#      3D kernel made a test that needed neither OCCT headers nor OCCT
#      libraries suddenly need both. The project-object commands live in
#      project_undo.h precisely so only their users pay that cost.
if grep -Eq '#include[[:space:]]*[<"](project\.h|body\.h|TopoDS)' "$LIB/hobbycad/document_undo.h"; then
    check 1 "document_undo.h stays free of project.h and OCCT"
else
    check 0 "document_undo.h stays free of project.h and OCCT"
fi

printf '\n'
if [ $fails -eq 0 ]; then
    echo "ALL PASS (0 failure(s))"
    echo "headers tests: ALL PASS"
else
    echo "FAILURES ($fails failure(s))"
    echo "headers tests: FAILURES"
    exit 1
fi
