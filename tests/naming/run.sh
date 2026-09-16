#!/bin/sh
# =====================================================================
#  tests/naming/run.sh — object name rules
#  SPDX-License-Identifier: GPL-3.0-only
# =====================================================================
#  naming.cpp depends on nothing but <string> and <cctype>, so it is
#  compiled in directly rather than linking the whole library. Keeps the
#  test runnable without Qt, OCCT or a built tree.
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=$(mktemp -d); trap 'rm -rf "$OUT"' EXIT
STATUS=0

for src in "$(dirname "$0")"/*.cpp; do
    name=$(basename "$src" .cpp)
    g++ -std=c++17 -I"$ROOT/src/libhobbycad" \
        "$src" "$ROOT/src/libhobbycad/naming.cpp" -o "$OUT/$name" \
        || { STATUS=1; continue; }
    "$OUT/$name" || STATUS=1
done

if [ "$STATUS" -eq 0 ]; then echo "naming tests: ALL PASS"; else echo "naming tests: FAILURES"; fi
exit "$STATUS"
