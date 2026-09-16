#!/bin/sh
# A handle drag must not be able to collapse an edge to zero length.
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BUILD=${1:-$ROOT/build}
HERE=$(dirname "$0")
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

QT_CFLAGS=$(hc_qt_cflags Qt6Gui Qt6Core)
QT_LIBS=$(hc_qt_libs Qt6Gui Qt6Core)
# minedge.cpp includes no Qt header: it needs Qt only to match a library
# that was built with it. A Qt-free build is a valid thing to test here, so
# demand Qt only when the build itself has Qt.
if [ "$HC_QT" = 1 ] && [ -z "$QT_CFLAGS" ]; then
    echo "  [FAIL] handleguard: cannot run, Qt6 development files not found"; exit 1
fi
LIB=$BUILD/src/libhobbycad/libhobbycad.a
if [ ! -f "$LIB" ]; then
    echo "  [FAIL] handleguard: cannot run, $LIB not found; build the project first"; exit 1
fi
OUT=$(mktemp -d); trap 'rm -rf "$OUT"' EXIT
STATUS=0
OCC_LIBS=$(ls /usr/lib/*/libTK*.so 2>/dev/null | sed 's|.*/lib\(TK[^.]*\)\.so|-l\1|' | tr '\n' ' ')
# shellcheck disable=SC2086
g++ -std=c++17 -I"$ROOT/src/libhobbycad" -I/usr/include/opencascade $QT_CFLAGS -fPIC \
    -o "$OUT/minedge" "$HERE/minedge.cpp" "$LIB" -lslvs $OCC_LIBS $QT_LIBS $HC_IMG_LIBS || STATUS=1
[ "$STATUS" -eq 0 ] && { "$OUT/minedge" || STATUS=1; }
if [ "$STATUS" -eq 0 ]; then echo "handleguard tests: ALL PASS"; else echo "handleguard tests: FAILURES"; fi
exit "$STATUS"
