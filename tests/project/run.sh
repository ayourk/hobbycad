#!/bin/sh
# =====================================================================
#  tests/project/run.sh — project file format tests
#  SPDX-License-Identifier: GPL-3.0-only
# =====================================================================
#  Dependency-free, same approach as tests/solver/run.sh: link the built
#  static library directly, no test framework. Build the project first.
#
#  Usage:  tests/project/run.sh [path/to/build]
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

QT_CFLAGS=$(hc_qt_cflags Qt6Gui Qt6Core)
QT_LIBS=$(hc_qt_libs Qt6Gui Qt6Core)
OCC_INC=/usr/include/opencascade
OCC_LIBS=$(ls /usr/lib/*/libTK*.so 2>/dev/null | sed 's|.*/lib\(TK[^.]*\)\.so|-l\1|' | tr '\n' ' ')

OUT=$(mktemp -d); trap 'rm -rf "$OUT"' EXIT
STATUS=0

for src in "$(dirname "$0")"/*.cpp; do
    name=$(basename "$src" .cpp)
    # shellcheck disable=SC2086
    g++ -std=c++17 -I"$ROOT/src/libhobbycad" -I"$OCC_INC" $QT_CFLAGS \
        "$src" "$LIB" -lslvs $QT_LIBS $HC_IMG_LIBS $OCC_LIBS -o "$OUT/$name" || { STATUS=1; continue; }
    "$OUT/$name" "$OUT/$name.data" || STATUS=1
done

if [ "$STATUS" -eq 0 ]; then echo "project tests: ALL PASS"; else echo "project tests: FAILURES"; fi
exit "$STATUS"
