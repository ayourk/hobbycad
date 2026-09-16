#!/bin/sh
# Group-transform tests: the rectangle acceptance matrix at library level.
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BUILD=${1:-$ROOT/build}
LIB=$BUILD/src/libhobbycad/libhobbycad.a
[ -f "$LIB" ] || { echo "error: $LIB not found; build the project first" >&2; exit 2; }
# ---- Qt flags follow the BUILD, not this machine ---------------------
# A build configured without Qt compiles libhobbycad with HOBBYCAD_HAS_QT
# 0. Handing the test Qt's cflags anyway would define QT_CORE_LIB, so the
# test would see a different Project and SketchData than the library it
# links against, and the link would fail. Ask the build what it is.
HC_QT=0
if [ -f "$BUILD/CMakeCache.txt" ] && grep -q '^Qt6_DIR:PATH=/' "$BUILD/CMakeCache.txt"; then
    HC_QT=1
fi
hc_qt_cflags() { [ "$HC_QT" = 1 ] || return 0; pkg-config --cflags "$@" 2>/dev/null || true; }
hc_qt_libs()   { [ "$HC_QT" = 1 ] || return 0; pkg-config --libs "$@" 2>/dev/null || true; }
# Without Qt the library decodes images through libwebp directly.
HC_IMG_LIBS=
[ "$HC_QT" = 1 ] || HC_IMG_LIBS=$(pkg-config --libs libwebp 2>/dev/null || echo -lwebp)

QT_CFLAGS=$(hc_qt_cflags Qt6Gui)
QT_LIBS=$(hc_qt_libs Qt6Gui Qt6Core)
OCCT_CFLAGS=""
for d in /usr/include/opencascade /usr/local/include/opencascade; do [ -d "$d" ] && OCCT_CFLAGS="-I$d" && break; done
OUT=$(mktemp -d); trap 'rm -rf "$OUT"' EXIT
STATUS=0
for t in rectangle; do
    printf '\n=== %s ===\n' "$t"
    g++ -std=c++17 -I"$ROOT/src/libhobbycad" $QT_CFLAGS $OCCT_CFLAGS -fPIC \
        -o "$OUT/$t" "$ROOT/tests/transform/$t.cpp" "$LIB" -lslvs $QT_LIBS $HC_IMG_LIBS || { STATUS=1; continue; }
    "$OUT/$t" || STATUS=1
done
if [ "$STATUS" -eq 0 ]; then echo "transform tests: ALL PASS"; else echo "transform tests: FAILURES"; fi
exit "$STATUS"
