#!/usr/bin/env bash
# =====================================================================
#  scripts/ci/build-libwebp.sh — pinned libwebp, static by default
#  SPDX-License-Identifier: GPL-3.0-only
#  Part of HobbyCAD (ayourk/hobbycad)
# =====================================================================
#
#  Usage: scripts/ci/build-libwebp.sh [--static | --shared] [--prefix DIR]
#                                     [--workdir DIR]
#
#  Builds the pinned libwebp without its command-line tools, so the WebP
#  path is the same on every channel rather than the distribution's copy.
#  Defaults: --static, --prefix /usr/local, --workdir $TMPDIR/libwebp.
# =====================================================================
. "$(dirname "$0")/lib.sh"
SHARED=OFF
PREFIX=/usr/local
WORK="${TMPDIR:-/tmp}/libwebp"
while [ $# -gt 0 ]; do
    case $1 in
        --static) SHARED=OFF ;;
        --shared) SHARED=ON ;;
        --prefix) PREFIX=$2; shift ;;
        --workdir) WORK=$2; shift ;;
        *) die "unknown option $1 (see the header of $0)" ;;
    esac
    shift
done
require cmake ninja
fetch_source libwebp "$WORK"
cmake -B "$WORK/build" -S "$WORK" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DBUILD_SHARED_LIBS="$SHARED" \
    -DWEBP_BUILD_ANIM_UTILS=OFF -DWEBP_BUILD_CWEBP=OFF -DWEBP_BUILD_DWEBP=OFF \
    -DWEBP_BUILD_GIF2WEBP=OFF -DWEBP_BUILD_IMG2WEBP=OFF -DWEBP_BUILD_VWEBP=OFF \
    -DWEBP_BUILD_WEBPINFO=OFF -DWEBP_BUILD_WEBPMUX=OFF -DWEBP_BUILD_EXTRAS=OFF
cmake --build "$WORK/build"
install_into "$PREFIX" cmake --install "$WORK/build"
if [ "$SHARED" = ON ] && [ "$(uname -s)" = Linux ] && command -v ldconfig >/dev/null 2>&1; then
    as_root ldconfig
fi
log "libwebp $(pin libwebp VERSION) installed in $PREFIX"
