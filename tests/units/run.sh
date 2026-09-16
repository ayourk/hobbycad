#!/bin/sh
# =====================================================================
#  tests/units/run.sh — display vs storage precision, 3D points
#  SPDX-License-Identifier: GPL-3.0-only
# =====================================================================
#  Each test here is compiled directly against the library headers
#  rather than linking the whole library. Keeps the tests runnable
#  without Qt, OCCT or a built tree.
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=$(mktemp -d); trap 'rm -rf "$OUT"' EXIT
STATUS=0

for src in "$(dirname "$0")"/*.cpp; do
    name=$(basename "$src" .cpp)
    g++ -std=c++17 -I"$ROOT/src/libhobbycad" \
        "$src" -o "$OUT/$name" \
        || { STATUS=1; continue; }
    "$OUT/$name" || STATUS=1
done

if [ "$STATUS" -eq 0 ]; then echo "units tests: ALL PASS"; else echo "units tests: FAILURES"; fi
exit "$STATUS"
