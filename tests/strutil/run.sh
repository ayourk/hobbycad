#!/bin/sh
# =====================================================================
#  tests/strutil/run.sh — the Qt-free string layer
#  SPDX-License-Identifier: GPL-3.0-only
# =====================================================================
#  strutil.h is header-only, so this suite links no library and needs
#  no build. It still takes a build argument for consistency with the
#  other suites, and ignores it.
#
#  Usage:  tests/strutil/run.sh [path/to/build]
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=$(mktemp -d); trap 'rm -rf "$OUT"' EXIT
STATUS=0
for src in "$(dirname "$0")"/*.cpp; do
    name=$(basename "$src" .cpp)
    g++ -std=c++17 -I"$ROOT/src/libhobbycad" "$src" -o "$OUT/$name" \
        || { echo "  [FAIL] $name: cannot run, it did not compile"; STATUS=1; continue; }
    "$OUT/$name" || STATUS=1
done
if [ "$STATUS" -eq 0 ]; then echo "strutil tests: ALL PASS"; else echo "strutil tests: FAILURES"; fi
exit "$STATUS"
