#!/usr/bin/env bash
# =====================================================================
#  scripts/ci/build-libslvs.sh — pinned libslvs (HobbyCAD series)
#  SPDX-License-Identifier: GPL-3.0-only
#  Part of HobbyCAD (ayourk/hobbycad)
# =====================================================================
#
#  Usage: scripts/ci/build-libslvs.sh [--static | --shared] [--prefix DIR]
#                                     [--eigen DIR] [--workdir DIR]
#
#  Builds libslvs from the orig tarball the PPA builds (the HobbyCAD patch
#  series is inside it). --static (the default) is for packages on
#  distributions that ship no libslvs: a dependency on libslvs.so.1 could
#  not be satisfied there, so the solver is linked in. Defaults: --prefix
#  /usr/local, --eigen /usr/include/eigen3, --workdir $TMPDIR/libslvs.
#  Needs cmake, ninja, a C++ compiler and the Eigen 3 headers.
# =====================================================================
. "$(dirname "$0")/lib.sh"
LINK=static
PREFIX=/usr/local
EIGEN=/usr/include/eigen3
WORK="${TMPDIR:-/tmp}/libslvs"
while [ $# -gt 0 ]; do
    case $1 in
        --static) LINK=static ;;
        --shared) LINK=shared ;;
        --prefix) PREFIX=$2; shift ;;
        --eigen) EIGEN=$2; shift ;;
        --workdir) WORK=$2; shift ;;
        *) die "unknown option $1 (see the header of $0)" ;;
    esac
    shift
done
require cmake ninja
[ -f "$EIGEN/Eigen/Core" ] || die "Eigen 3 headers not found in $EIGEN (install them or pass --eigen)"
fetch_source libslvs "$WORK"
cd "$WORK"

# The tarball has the extlib/ directories but not the submodules the solver
# does not use; empty stubs let the top-level CMakeLists add them.
for submod in zlib libpng freetype cairo pixman angle; do
    mkdir -p "extlib/$submod"
    echo "# stub" > "extlib/$submod/CMakeLists.txt"
done
mkdir -p extlib/eigen
cp -r "$EIGEN/." extlib/eigen/

# set(CMAKE_CXX_STANDARD 11) is a normal variable, which -D cannot override.
sed -i 's/set(CMAKE_CXX_STANDARD 11)/set(CMAKE_CXX_STANDARD 14)/' CMakeLists.txt

OPTS=()
if [ "$LINK" = static ]; then
    # The fork links mimalloc PRIVATE into the shared library; a static
    # archive would leave mi_heap_new and friends unresolved for every
    # consumer, so mimalloc's objects are compiled into it.
    sed -i 's/add_library(slvs SHARED)/add_library(slvs STATIC)\ntarget_compile_definitions(slvs PUBLIC STATIC_LIB)\ntarget_sources(slvs PRIVATE $<TARGET_OBJECTS:mimalloc-obj>)/' src/slvs/CMakeLists.txt
    grep -q 'add_library(slvs STATIC)' src/slvs/CMakeLists.txt || die "the static-library edit did not apply to src/slvs/CMakeLists.txt"
    OPTS+=(-DMI_BUILD_OBJECT=ON)
fi
# Eigen's NEON isfinite_impl miscompiles on aarch64.
if [ "$(uname -m)" = aarch64 ]; then
    OPTS+=(-DCMAKE_CXX_FLAGS=-DEIGEN_DONT_VECTORIZE)
fi

cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    "${OPTS[@]}" \
    -DBUILD_LIB=ON \
    -DENABLE_GUI=OFF \
    -DENABLE_CLI=OFF \
    -DENABLE_OPENMP=OFF \
    -DENABLE_TESTS=OFF
cmake --build build
install_into "$PREFIX" cmake --install build
# A shared library in /usr/local is not in the loader cache until ldconfig
# runs, and the package builds run their test suites against it.
if [ "$LINK" = shared ] && [ "$(uname -s)" = Linux ] && command -v ldconfig >/dev/null 2>&1; then
    as_root ldconfig
fi
log "libslvs $(pin libslvs VERSION) ($LINK) installed in $PREFIX"
