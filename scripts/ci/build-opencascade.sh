#!/usr/bin/env bash
# =====================================================================
#  scripts/ci/build-opencascade.sh — pinned OpenCASCADE, static
#  SPDX-License-Identifier: GPL-3.0-only
#  Part of HobbyCAD (ayourk/hobbycad)
# =====================================================================
#
#  Usage: scripts/ci/build-opencascade.sh [--prefix DIR] [--workdir DIR]
#
#  Builds the OpenCASCADE the PPA ships (versions.json) as static
#  libraries, so a package built against it needs no libTK*.so that the
#  distribution does not provide. Defaults: --prefix /usr/local,
#  --workdir $TMPDIR/opencascade. Needs cmake, ninja, a C++ compiler and
#  the FreeType, fontconfig, X11 and OpenGL development files.
# =====================================================================
. "$(dirname "$0")/lib.sh"
PREFIX=/usr/local
WORK="${TMPDIR:-/tmp}/opencascade"
while [ $# -gt 0 ]; do
    case $1 in
        --prefix) PREFIX=$2; shift ;;
        --workdir) WORK=$2; shift ;;
        *) die "unknown option $1 (see the header of $0)" ;;
    esac
    shift
done
require cmake ninja
fetch_source opencascade "$WORK"
cmake -B "$WORK/build" -S "$WORK" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DBUILD_LIBRARY_TYPE=Static \
    -DBUILD_MODULE_Draw=OFF \
    -DBUILD_MODULE_Visualization=ON \
    -DBUILD_MODULE_ApplicationFramework=ON \
    -DBUILD_MODULE_DataExchange=ON \
    -DUSE_TBB=OFF \
    -DUSE_VTK=OFF \
    -DUSE_FREETYPE=ON
cmake --build "$WORK/build"
install_into "$PREFIX" cmake --install "$WORK/build"
log "OpenCASCADE $(pin opencascade VERSION) installed in $PREFIX"
