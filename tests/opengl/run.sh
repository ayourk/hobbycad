#!/bin/sh
# =====================================================================
#  tests/opengl/run.sh — viewport gate tests
#  SPDX-License-Identifier: GPL-3.0-only
# =====================================================================
#  Header-only logic: no OCCT, no Qt, no display needed.
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=$(mktemp -d); trap 'rm -rf "$OUT"' EXIT
STATUS=0

for src in "$(dirname "$0")"/*.cpp; do
    name=$(basename "$src" .cpp)
    g++ -std=c++17 -I"$ROOT/src/libhobbycad" "$src" -o "$OUT/$name" || { STATUS=1; continue; }
    "$OUT/$name" || STATUS=1
done

if [ "$STATUS" -eq 0 ]; then echo "opengl tests: ALL PASS"; else echo "opengl tests: FAILURES"; fi
exit "$STATUS"
