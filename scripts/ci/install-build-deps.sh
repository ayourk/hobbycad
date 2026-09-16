#!/usr/bin/env bash
# =====================================================================
#  scripts/ci/install-build-deps.sh — build dependencies from the packaging
#  SPDX-License-Identifier: GPL-3.0-only
#  Part of HobbyCAD (ayourk/hobbycad)
# =====================================================================
#
#  Usage: scripts/ci/install-build-deps.sh deb  [--provided PKG,PKG] [--extra PKG...]
#         scripts/ci/install-build-deps.sh rpm  [--spec FILE]        [--extra PKG...]
#         scripts/ci/install-build-deps.sh arch [--pkgbuild FILE]    [--extra PKG...]
#
#  The packaging is the only list of build dependencies:
#    deb   debian/control Build-Depends, through apt-get build-dep. With
#          --provided, the named packages are left out (they are built from
#          source instead) and the rest of the unmet dependencies that
#          dpkg-checkbuilddeps reports are installed by name.
#    rpm   BuildRequires of packaging/rpm/hobbycad.spec, through dnf builddep.
#    arch  depends and makedepends of packaging/arch/PKGBUILD.
#  --extra names packages the job needs beyond the packaging, such as the
#  tools the pinned-dependency build scripts use. It must come last.
# =====================================================================
. "$(dirname "$0")/lib.sh"
KIND=${1:-}
[ -n "$KIND" ] || die "usage: $0 deb|rpm|arch [options] (see the header of $0)"
shift
PROVIDED=""
SPEC="$REPO_ROOT/packaging/rpm/hobbycad.spec"
PKGBUILD_FILE="$REPO_ROOT/packaging/arch/PKGBUILD"
EXTRA=()
while [ $# -gt 0 ]; do
    case $1 in
        --provided) PROVIDED=$2; shift ;;
        --spec) SPEC=$2; shift ;;
        --pkgbuild) PKGBUILD_FILE=$2; shift ;;
        --extra) shift; EXTRA=("$@"); break ;;
        *) die "unknown option $1 (see the header of $0)" ;;
    esac
    shift
done

case $KIND in
deb)
    export DEBIAN_FRONTEND=noninteractive
    as_root apt-get update
    as_root apt-get install -y build-essential fakeroot dpkg-dev debhelper
    if [ ${#EXTRA[@]} -gt 0 ]; then as_root apt-get install -y "${EXTRA[@]}"; fi
    cd "$REPO_ROOT"
    if [ -z "$PROVIDED" ]; then
        as_root apt-get build-dep -y ./
    else
        unmet=$(dpkg-checkbuilddeps 2>&1 >/dev/null | sed -n 's/.*Unmet build dependencies: //p' || true)
        # Drop version constraints, architecture and profile qualifiers, and
        # every alternative after the first; debhelper-compat is virtual.
        pkgs=$(printf '%s\n' "$unmet" \
            | sed -E 's/\([^)]*\)//g; s/\[[^]]*\]//g; s/<[^>]*>//g' \
            | awk '{ for (i = 1; i <= NF; i++) { if ($i == "|") { i++; continue } print $i } }' \
            | sed -E 's/:(any|native)$//; s/^debhelper-compat$/debhelper/' | sort -u)
        for p in ${PROVIDED//,/ }; do
            pkgs=$(printf '%s\n' "$pkgs" | grep -vx "$p" || true)
        done
        log "provided from source, not installed: $PROVIDED"
        if [ -n "$pkgs" ]; then
            # shellcheck disable=SC2086
            as_root apt-get install -y $pkgs
        fi
    fi
    ;;
rpm)
    [ -f "$SPEC" ] || die "no spec file at $SPEC"
    as_root dnf install -y dnf-plugins-core rpm-build
    if [ ${#EXTRA[@]} -gt 0 ]; then as_root dnf install -y "${EXTRA[@]}"; fi
    as_root dnf builddep -y "$SPEC"
    ;;
arch)
    [ -f "$PKGBUILD_FILE" ] || die "no PKGBUILD at $PKGBUILD_FILE"
    mapfile -t pkgs < <(bash -c 'set -e; . "$1"; printf "%s\n" "${depends[@]}" "${makedepends[@]}"' _ "$PKGBUILD_FILE")
    as_root pacman -S --noconfirm --needed "${pkgs[@]}" "${EXTRA[@]}"
    ;;
*)
    die "unknown packaging kind $KIND (deb, rpm or arch)"
    ;;
esac
log "build dependencies installed ($KIND)"
