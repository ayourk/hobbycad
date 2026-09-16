#!/bin/sh
# tests/theme/run.sh — SketchTheme palette (light == historical, dark sane)
# SPDX-License-Identifier: GPL-3.0-only
# SketchTheme is a plain header over QColor; compile against Qt6Gui only, no
# lib, no OCCT, no moc (it is not a QObject).
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
QT_CFLAGS=$(pkg-config --cflags Qt6Gui 2>/dev/null || true)
QT_LIBS=$(pkg-config --libs Qt6Gui 2>/dev/null || true)
OUT=$(mktemp -d); trap 'rm -rf "$OUT"' EXIT
STATUS=0
for src in "$(dirname "$0")"/*.cpp; do
    name=$(basename "$src" .cpp)
    # shellcheck disable=SC2086
    g++ -std=c++17 -fPIC -I"$ROOT/src/hobbycad/gui" $QT_CFLAGS \
        "$src" $QT_LIBS -o "$OUT/$name" || { STATUS=1; continue; }
    echo "-- $name"
    "$OUT/$name" || STATUS=1
done
if [ "$STATUS" -eq 0 ]; then echo "theme tests: ALL PASS"; else echo "theme tests: FAILURES"; fi
exit "$STATUS"
