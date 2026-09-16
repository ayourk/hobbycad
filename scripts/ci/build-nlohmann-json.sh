#!/usr/bin/env bash
# =====================================================================
#  scripts/ci/build-nlohmann-json.sh — pinned nlohmann-json (headers)
#  SPDX-License-Identifier: GPL-3.0-only
#  Part of HobbyCAD (ayourk/hobbycad)
# =====================================================================
#
#  Usage: scripts/ci/build-nlohmann-json.sh [--prefix DIR] [--workdir DIR]
#
#  Installs the pinned nlohmann-json headers and CMake package config, for
#  distributions whose own copy is older than versions.json requires.
#  Defaults: --prefix /usr/local, --workdir $TMPDIR/nlohmann-json.
# =====================================================================
. "$(dirname "$0")/lib.sh"
PREFIX=/usr/local
WORK="${TMPDIR:-/tmp}/nlohmann-json"
while [ $# -gt 0 ]; do
    case $1 in
        --prefix) PREFIX=$2; shift ;;
        --workdir) WORK=$2; shift ;;
        *) die "unknown option $1 (see the header of $0)" ;;
    esac
    shift
done
require cmake ninja
fetch_source nlohmann-json "$WORK"
cmake -B "$WORK/build" -S "$WORK" -G Ninja -DCMAKE_INSTALL_PREFIX="$PREFIX" -DJSON_BuildTests=OFF
install_into "$PREFIX" cmake --install "$WORK/build"
log "nlohmann-json $(pin nlohmann-json VERSION) installed in $PREFIX"
