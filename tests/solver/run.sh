#!/bin/sh
# =====================================================================
#  tests/solver/run.sh — build and run the solver tests
#  SPDX-License-Identifier: GPL-3.0-only
# =====================================================================
#
#  Deliberately dependency-free: no gtest, no Catch2, no CMake target.
#  The project has not chosen a test framework yet (see ../README.txt), and
#  these tests are worth having NOW rather than after that decision. They
#  link the already-built static library, so build the project first.
#
#  Usage:  tests/solver/run.sh [path/to/build]
#
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BUILD=${1:-$ROOT/build}
LIB=$BUILD/src/libhobbycad/libhobbycad.a

if [ ! -f "$LIB" ]; then
    echo "error: $LIB not found; build the project first" >&2
    exit 2
fi

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
# project.h includes OCCT headers, so anything touching the project model
# needs them on the include path even though these tests link none of it.
OCCT_CFLAGS=""
for d in /usr/include/opencascade /usr/local/include/opencascade; do
    [ -d "$d" ] && OCCT_CFLAGS="-I$d" && break
done
QT_LIBS=$(hc_qt_libs Qt6Gui Qt6Core)
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

STATUS=0
TESTS="entity_types regression arc_endpoints slvs_capabilities sketch_state
       driven_dimensions over_constraint redundancy_finder redundant_scale
       overconstrained_file line_circle_tangent line_ellipse_tangent
       ellipse_native fault_recovery curve_curve_tangent inference tangent_arc
       offset_associative trim_extend projection_dof sketch3d_solve
       solve3d_wrapper solve_preserves_z validate_groups ground_origin
       deforming_drag autoconstrain bezier_spline curvature_g2 rational_spline
       tangent_angle cut_constraints arc_open"
for t in $TESTS; do
    printf '\n=== %s ===\n' "$t"

    # Only a test that touches the PROJECT model needs OCCT at link time:
    # project.cpp lives in the same static library as brep_io.cpp, so pulling
    # it in drags OCCT symbols with it. Keyed off the include so the rule is
    # visible in the test source rather than hidden in a list here.
    EXTRA=""
    if grep -q "hobbycad/project.h" "$ROOT/tests/solver/$t.cpp"; then
        for l in TKernel TKMath TKBRep TKG2d TKG3d TKGeomBase TKTopAlgo \
                 TKPrim TKBO TKShHealing TKMesh TKService TKV3d TKDE \
                 TKXSBase TKDESTEP TKDESTL TKLCAF TKCAF TKCDF TKDECascade; do
            [ -n "$(ls /usr/lib/*/lib$l.so* 2>/dev/null | head -1)" ] && EXTRA="$EXTRA -l$l"
        done
    fi

    g++ -std=c++17 -I"$ROOT/src/libhobbycad" $QT_CFLAGS $OCCT_CFLAGS -fPIC \
        -o "$OUT/$t" "$ROOT/tests/solver/$t.cpp" "$LIB" -lslvs $EXTRA $QT_LIBS $HC_IMG_LIBS
    "$OUT/$t" || STATUS=1
done

printf '\n'
[ $STATUS -eq 0 ] && echo "solver tests: ALL PASS" || echo "solver tests: FAILURES"
exit $STATUS
