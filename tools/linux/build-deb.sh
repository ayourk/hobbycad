#!/bin/bash
# =====================================================================
#  tools/linux/build-deb.sh — HobbyCAD Debian Package Build Script
# =====================================================================
#
#  Builds a .deb package from the project source.  All output is
#  logged to build-deb.log in the project root.
#
#  Usage (from the project root):
#    ./tools/linux/build-deb.sh              # Build .deb
#    ./tools/linux/build-deb.sh install      # Build + install locally
#    ./tools/linux/build-deb.sh clean        # Remove build artifacts
#
#  Prerequisites:
#    sudo apt-get install -y \
#      debhelper-compat dh-cmake dpkg-dev fakeroot lintian \
#      devscripts ninja-build librsvg2-bin icoutils
#
#  Output:
#    ../<projectroot>/hobbycad_<version>_<arch>.deb
#
#  See dev_environment_setup.txt Section 11.3 for details.
#
#  SPDX-License-Identifier: GPL-3.0-only
#
# =====================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
LOG="${PROJECT_ROOT}/build-deb.log"

# ---- Parse arguments -------------------------------------------------

ACTION="build"

for arg in "$@"; do
    case "${arg,,}" in
        install)  ACTION="install" ;;
        clean)    ACTION="clean" ;;
        *)
            echo "Usage: $0 [install|clean]"
            exit 1
            ;;
    esac
done

# ---- Logging setup ---------------------------------------------------

exec > >(tee "${LOG}") 2>&1

# ---- Extract version from changelog ---------------------------------

VERSION="$(dpkg-parsechangelog -l "${PROJECT_ROOT}/debian/changelog" \
    -S Version 2>/dev/null || echo "unknown")"
ARCH="$(dpkg-architecture -qDEB_HOST_ARCH 2>/dev/null || echo "amd64")"
DEB_FILE="${PROJECT_ROOT}/../hobbycad_${VERSION}_${ARCH}.deb"

echo "====================================================================="
echo "  HobbyCAD Debian Package Build"
echo "====================================================================="
echo ""
echo "  Project root : ${PROJECT_ROOT}"
echo "  Version      : ${VERSION}"
echo "  Architecture : ${ARCH}"
echo "  Log file     : ${LOG}"
echo "  Date         : $(date)"
echo "  Action       : ${ACTION}"
echo ""

# ---- Clean -----------------------------------------------------------

if [ "${ACTION}" = "clean" ]; then
    echo "--- Cleaning debian build artifacts ---"
    cd "${PROJECT_ROOT}"
    if [ -d "obj-$(dpkg-architecture -qDEB_HOST_MULTIARCH 2>/dev/null)" ]; then
        fakeroot debian/rules clean 2>/dev/null || true
    fi
    rm -rf obj-* debian/.debhelper debian/hobbycad debian/tmp \
           debian/files debian/*.debhelper* debian/*.substvars
    echo "  Done"
    echo ""
    exit 0
fi

# ---- Prerequisite check ---------------------------------------------

echo "--- Checking build prerequisites ---"
echo ""

MISSING=()
for cmd in dpkg-buildpackage fakeroot lintian ninja rsvg-convert; do
    if ! command -v "${cmd}" &>/dev/null; then
        MISSING+=("${cmd}")
    fi
done

if [ ${#MISSING[@]} -gt 0 ]; then
    echo "  ERROR: Missing tools: ${MISSING[*]}"
    echo ""
    echo "  Install with:"
    echo "    sudo apt-get install -y \\"
    echo "      debhelper-compat dh-cmake dpkg-dev fakeroot \\"
    echo "      lintian devscripts ninja-build \\"
    echo "      librsvg2-bin icoutils"
    echo ""
    exit 1
fi

echo "  All prerequisites found"
echo ""

# ---- Verify debian/ directory ----------------------------------------

echo "--- Verifying debian/ packaging files ---"
echo ""

REQUIRED_FILES=(
    "debian/control"
    "debian/rules"
    "debian/changelog"
    "debian/copyright"
    "debian/compat"
    "debian/source/format"
)

for f in "${REQUIRED_FILES[@]}"; do
    if [ ! -f "${PROJECT_ROOT}/${f}" ]; then
        echo "  ERROR: Missing ${f}"
        exit 1
    fi
done

echo "  All packaging files present"
echo ""

# ---- Build the .deb -------------------------------------------------

echo "--- Building .deb package ---"
echo ""

cd "${PROJECT_ROOT}"
dpkg-buildpackage -us -uc -b

echo ""

# ---- Verify output ---------------------------------------------------

if [ -f "${DEB_FILE}" ]; then
    echo "====================================================================="
    echo "  Package built successfully"
    echo "====================================================================="
    echo ""
    echo "  File : ${DEB_FILE}"
    echo "  Size : $(du -h "${DEB_FILE}" | cut -f1)"
    echo ""

    echo "--- Running lintian ---"
    echo ""
    lintian "${DEB_FILE}" || true
    echo ""
else
    # dpkg-buildpackage may use slightly different naming
    FOUND_DEB="$(ls "${PROJECT_ROOT}"/../hobbycad_*.deb 2>/dev/null | head -1)"
    if [ -n "${FOUND_DEB}" ]; then
        DEB_FILE="${FOUND_DEB}"
        echo "====================================================================="
        echo "  Package built successfully"
        echo "====================================================================="
        echo ""
        echo "  File : ${DEB_FILE}"
        echo "  Size : $(du -h "${DEB_FILE}" | cut -f1)"
        echo ""

        echo "--- Running lintian ---"
        echo ""
        lintian "${DEB_FILE}" || true
        echo ""
    else
        echo "====================================================================="
        echo "  Build FAILED — .deb not found"
        echo "====================================================================="
        echo ""
        echo "  Check the log: ${LOG}"
        exit 1
    fi
fi

# ---- Install if requested --------------------------------------------

if [ "${ACTION}" = "install" ]; then
    echo "--- Installing package ---"
    echo ""
    sudo dpkg -i "${DEB_FILE}"
    sudo apt-get install -f -y
    echo ""
    echo "  Installed. Run with: hobbycad"
    echo "  Remove with: sudo apt-get remove hobbycad"
    echo ""
fi
