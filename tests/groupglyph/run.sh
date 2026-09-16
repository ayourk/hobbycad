#!/bin/sh
# Group indicator + transform pivot glyph tests. Qt only, no library: the
# code under test is constraintglyphs.cpp, rendered offscreen and sampled.
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
HERE=$(dirname "$0")
QT_CFLAGS=$(pkg-config --cflags Qt6Gui Qt6Core 2>/dev/null || true)
QT_LIBS=$(pkg-config --libs Qt6Gui Qt6Core 2>/dev/null || true)
if [ -z "$QT_CFLAGS" ]; then
    # A suite that does not run cannot fail, so it must not pass.
    echo "  [FAIL] groupglyph: cannot run, Qt6 development files not found"
    exit 1
fi
OUT=$(mktemp -d); trap 'rm -rf "$OUT"' EXIT
STATUS=0
for t in occlusion pivot; do
    printf '\n=== %s ===\n' "$t"
    if g++ -std=c++17 -fPIC \
        -I"$ROOT/src/hobbycad/gui" -I"$ROOT/src/libhobbycad" \
        $QT_CFLAGS \
        "$HERE/$t.cpp" "$ROOT/src/hobbycad/gui/constraintglyphs.cpp" \
        $QT_LIBS -o "$OUT/$t"; then
        QT_QPA_PLATFORM=offscreen "$OUT/$t" || STATUS=1
    else
        STATUS=1
    fi
done
if [ "$STATUS" -eq 0 ]; then echo "groupglyph tests: ALL PASS"; else echo "groupglyph tests: FAILURES"; fi
exit "$STATUS"
