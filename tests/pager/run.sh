#!/bin/sh
# =====================================================================
#  tests/pager/run.sh — pager navigation tests
#  SPDX-License-Identifier: GPL-3.0-only
# =====================================================================
#  Header-only arithmetic: no terminal, no Qt, no built tree needed.
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=$(mktemp -d); trap 'rm -rf "$OUT"' EXIT
STATUS=0
for src in "$(dirname "$0")"/*.cpp; do
    name=$(basename "$src" .cpp)
    g++ -std=c++17 -I"$ROOT/src/hobbycad/cli" "$src" -o "$OUT/$name" \
        || { STATUS=1; continue; }
    "$OUT/$name" || STATUS=1
done
if [ "$STATUS" -eq 0 ]; then echo "pager tests: ALL PASS"; else echo "pager tests: FAILURES"; fi
exit "$STATUS"
