#!/bin/sh
# Draw-then-constrain: hint derivation, and which constraint a snap implies.
#
# hints.cpp needs Qt only: the code under test is one pure QString
# function. snapmap.cpp tests library code and links libhobbycad like the
# other suites do.
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

# Two halves, two rules. hints.cpp links no library, so it uses this
# machine's Qt and stays meaningful even against a Qt-free build of the
# library. snapmap.cpp links libhobbycad, so its flags follow the build.
QTM_CFLAGS=$(pkg-config --cflags Qt6Widgets Qt6Gui Qt6Core 2>/dev/null || true)
QTM_LIBS=$(pkg-config --libs Qt6Core 2>/dev/null || true)
QT_CFLAGS=$(hc_qt_cflags Qt6Widgets Qt6Gui Qt6Core)
QT_LIBS=$(hc_qt_libs Qt6Core)
if [ -z "$QTM_CFLAGS" ]; then
    # A suite that does not run cannot fail, so it must not pass.
    echo "  [FAIL] drawconstrain: cannot run, Qt6 development files not found"
    exit 1
fi

OUT=$(mktemp -d); trap 'rm -rf "$OUT"' EXIT
STATUS=0

# --- hint derivation (no library needed) ---
# shellcheck disable=SC2086
g++ -std=c++17 -fPIC \
    -I"$ROOT/src/hobbycad/gui" -I"$ROOT/src/hobbycad" -I"$ROOT/src/libhobbycad" \
    -I/usr/include/opencascade $QTM_CFLAGS \
    "$HERE/hints.cpp" "$ROOT/src/hobbycad/gui/tools/drawconstrainhandlers.cpp" \
    $QTM_LIBS -o "$OUT/hints" || STATUS=1
[ "$STATUS" -eq 0 ] && { "$OUT/hints" || STATUS=1; }

# --- snap -> constraint mapping (library) ---
LIB=$BUILD/src/libhobbycad/libhobbycad.a
if [ -f "$LIB" ]; then
    OCC_LIBS=$(ls /usr/lib/*/libTK*.so 2>/dev/null | sed 's|.*/lib\(TK[^.]*\)\.so|-l\1|' | tr '\n' ' ')
    # shellcheck disable=SC2086
    g++ -std=c++17 -I"$ROOT/src/libhobbycad" -I/usr/include/opencascade $QT_CFLAGS \
        "$HERE/snapmap.cpp" "$LIB" $QT_LIBS $HC_IMG_LIBS $OCC_LIBS -o "$OUT/snapmap" || STATUS=1
    [ -x "$OUT/snapmap" ] && { "$OUT/snapmap" || STATUS=1; }
else
    echo "  [FAIL] snapmap: cannot run, $LIB not found; build the project first"
    STATUS=1
fi

if [ "$STATUS" -eq 0 ]; then echo "drawconstrain tests: ALL PASS"; else echo "drawconstrain tests: FAILURES"; fi
exit "$STATUS"
