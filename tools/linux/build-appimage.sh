#!/bin/bash
# =====================================================================
#  tools/linux/build-appimage.sh — HobbyCAD AppImage Build Script
# =====================================================================
#
#  Builds a portable AppImage from a Release configuration.
#  All output is logged to build-appimage.log in the project root.
#
#  linuxdeploy and its Qt plugin are downloaded automatically into
#  the tools/ directory if they are not already present.
#
#  Usage (from the project root):
#    ./tools/linux/build-appimage.sh
#
# =====================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
LOG="${PROJECT_ROOT}/build-appimage.log"

BUILD_DIR="${PROJECT_ROOT}/build-appimage"
APP_DIR="${PROJECT_ROOT}/AppDir"
TOOLS_DIR="${SCRIPT_DIR}"

LINUXDEPLOY="${TOOLS_DIR}/linuxdeploy-x86_64.AppImage"
LINUXDEPLOY_QT="${TOOLS_DIR}/linuxdeploy-plugin-qt-x86_64.AppImage"

LINUXDEPLOY_URL="https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
LINUXDEPLOY_QT_URL="https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage"

# ---- Logging helpers ------------------------------------------------

log() {
    echo "$@" | tee -a "${LOG}"
}

run() {
    local hint="${STEP_HINT:-See ${LOG} for full output.}"
    log "  \$ $*"
    "$@" >> "${LOG}" 2>&1
    local rc=$?
    if [ $rc -ne 0 ]; then
        log "  FAILED (exit code ${rc})"
        log ""
        log "  ${hint}"
        log ""
        log "  Full output: ${LOG}"
        exit $rc
    fi
    return $rc
}

# ---- Start log ------------------------------------------------------

cat > "${LOG}" <<EOF
===== build-appimage.log =====
Started: $(date -u '+%Y-%m-%d %H:%M:%S') UTC

Host:     $(uname -srm)
User:     $(whoami)
Project:  ${PROJECT_ROOT}

EOF

# ---- Fetch tools if missing -----------------------------------------

log "--- Checking tools ---"
log ""

fetch_tool() {
    local path="$1"
    local url="$2"
    local name
    name="$(basename "${path}")"

    if [ -x "${path}" ]; then
        log "  ${name}: present"
    else
        log "  ${name}: not found — downloading..."
        log "    ${url}"
        if ! wget -q --show-progress -O "${path}" "${url}" 2>> "${LOG}"; then
            log "  ERROR: download failed for ${name}"
            log "  URL: ${url}"
            log ""
            log "  Check your network connection, or download manually:"
            log "    wget -O ${path} ${url}"
            log "    chmod +x ${path}"
            log ""
            log "  See tools/README.txt for details."
            rm -f "${path}"
            exit 1
        fi
        chmod +x "${path}"
        log "  ${name}: downloaded OK"
    fi
}

fetch_tool "${LINUXDEPLOY}" "${LINUXDEPLOY_URL}"
fetch_tool "${LINUXDEPLOY_QT}" "${LINUXDEPLOY_QT_URL}"

log ""
log "  linuxdeploy version:  $(${LINUXDEPLOY} --version 2>/dev/null || echo 'unknown')"
log "  cmake:                $(cmake --version | head -1)"
log "  ninja:                $(ninja --version 2>/dev/null || echo 'not found (using make)')"
log ""

# ---- Clean previous build -------------------------------------------

log "--- Cleaning previous build ---"
log ""

if [ -d "${BUILD_DIR}" ]; then
    log "  Removing ${BUILD_DIR}"
    rm -rf "${BUILD_DIR}"
fi

if [ -d "${APP_DIR}" ]; then
    log "  Removing ${APP_DIR}"
    rm -rf "${APP_DIR}"
fi

# Remove previous AppImage output
rm -f "${PROJECT_ROOT}"/HobbyCAD-*.AppImage
rm -f "${PROJECT_ROOT}"/HobbyCAD-*.AppImage.zsync

log ""

# ---- Configure ------------------------------------------------------

log "--- Configure (cmake) ---"
log ""

GENERATOR="Ninja"
if ! command -v ninja &>/dev/null; then
    GENERATOR="Unix Makefiles"
fi

STEP_HINT="Configure failed. Run devtest to check dependencies:
           cd devtest && cmake -B build && cmake --build build && ./build/depcheck
           See dev_environment_setup.txt Section 12 for troubleshooting."

run cmake -B "${BUILD_DIR}" -G "${GENERATOR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr

log "  Configure complete"
log ""

# ---- Build ----------------------------------------------------------

log "--- Build (cmake --build) ---"
log ""

NPROC=$(nproc 2>/dev/null || echo 4)

STEP_HINT="Build failed. Check compiler errors in ${LOG}.
           Run devtest to verify dependencies link correctly:
           cd devtest && cmake -B build && cmake --build build && ./build/depcheck
           See dev_environment_setup.txt Section 12 for troubleshooting."

run cmake --build "${BUILD_DIR}" -j"${NPROC}"

log "  Build complete"
log ""

# ---- Validate build output ------------------------------------------

log "--- Validating build output ---"
log ""

HOBBYCAD_BIN=$(find "${BUILD_DIR}" -name "hobbycad" -type f -executable -print -quit 2>/dev/null)
if [ -n "${HOBBYCAD_BIN}" ]; then
    log "  [OK] ${HOBBYCAD_BIN#${PROJECT_ROOT}/}"
else
    log "  [MISSING] hobbycad binary not found in ${BUILD_DIR}"
    log ""
    log "  The build completed but did not produce an executable."
    log "  Run devtest to verify dependencies link correctly:"
    log "    cd devtest && cmake -B build && cmake --build build && ./build/depcheck"
    log ""
    log "  See dev_environment_setup.txt Section 12 for troubleshooting."
    log ""
    log "  Full output: ${LOG}"
    exit 1
fi

log ""

# ---- Install to staging (AppDir) ------------------------------------

log "--- Install to AppDir ---"
log ""

STEP_HINT="Install to AppDir failed. Verify the build completed
           successfully and that CMAKE_INSTALL_PREFIX is set to /usr.
           See dev_environment_setup.txt Section 11.4 for AppDir layout."

run env DESTDIR="${APP_DIR}" cmake --install "${BUILD_DIR}"

log "  AppDir layout:"
find "${APP_DIR}" -type f | sort | while read -r f; do
    log "    ${f#${APP_DIR}/}"
done
log ""

# ---- Validate AppDir ------------------------------------------------
#
# linuxdeploy requires the binary, a .desktop file, and an icon.
# Catch problems here with clear messages rather than letting
# linuxdeploy fail cryptically.

log "--- Validating AppDir ---"
log ""

APPDIR_OK=true

# Binary
if [ -x "${APP_DIR}/usr/bin/hobbycad" ]; then
    log "  [OK] usr/bin/hobbycad"
else
    log "  [MISSING] usr/bin/hobbycad"
    log "           The main HobbyCAD project must be built first."
    log "           Verify the project's CMakeLists.txt has:"
    log "             install(TARGETS hobbycad DESTINATION bin)"
    log "           Then rebuild: cmake --build build-appimage"
    APPDIR_OK=false
fi

# Desktop file
DESKTOP=$(find "${APP_DIR}" -name "hobbycad.desktop" -print -quit 2>/dev/null)
if [ -n "${DESKTOP}" ]; then
    log "  [OK] ${DESKTOP#${APP_DIR}/}"
else
    log "  [MISSING] usr/share/applications/hobbycad.desktop"
    log "           linuxdeploy requires a .desktop file."
    log "           Verify the project's CMakeLists.txt has:"
    log "             install(FILES hobbycad.desktop"
    log "               DESTINATION share/applications)"
    APPDIR_OK=false
fi

# Icon (any format — .png or .svg)
ICON=$(find "${APP_DIR}" -path "*/icons/*hobbycad*" -print -quit 2>/dev/null)
if [ -n "${ICON}" ]; then
    log "  [OK] ${ICON#${APP_DIR}/}"
else
    log "  [MISSING] usr/share/icons/hicolor/.../hobbycad.png (or .svg)"
    log "           linuxdeploy requires an icon in the hicolor theme."
    log "           Verify the project's CMakeLists.txt has:"
    log "             install(FILES icons/hobbycad.png"
    log "               DESTINATION share/icons/hicolor/256x256/apps)"
    APPDIR_OK=false
fi

log ""

if [ "${APPDIR_OK}" != "true" ]; then
    log "  AppDir validation FAILED."
    log ""
    log "  The AppDir is missing required files. This usually means"
    log "  the main HobbyCAD project has not been built yet, or its"
    log "  CMakeLists.txt is missing install() rules."
    log ""
    log "  See dev_environment_setup.txt Section 11.4 for the"
    log "  expected AppDir layout."
    log ""
    log "  Full output: ${LOG}"
    exit 1
fi

log "  AppDir validation passed."
log ""

# ---- Create AppImage ------------------------------------------------

log "--- Create AppImage (linuxdeploy) ---"
log ""

export LDAI_UPDATE_INFORMATION="gh-releases-zsync|ayourk|HobbyCAD|latest|HobbyCAD-*x86_64.AppImage.zsync"

log "  LDAI_UPDATE_INFORMATION=${LDAI_UPDATE_INFORMATION}"
log ""

cd "${PROJECT_ROOT}"

STEP_HINT="linuxdeploy failed. Common causes:
           - Missing .desktop file or icon in AppDir
             (see dev_environment_setup.txt Section 11.4 AppDir layout)
           - Missing shared libraries that ldd cannot resolve
           - FUSE not available (try: export APPIMAGE_EXTRACT_AND_RUN=1)
           Check linuxdeploy output in ${LOG}."

run "${LINUXDEPLOY}" \
    --appdir "${APP_DIR}" \
    --plugin qt \
    --output appimage

log ""

# ---- Verify output --------------------------------------------------

log "--- Output ---"
log ""

APPIMAGE=$(ls -1 "${PROJECT_ROOT}"/HobbyCAD-*.AppImage 2>/dev/null | head -1)
if [ -n "${APPIMAGE}" ] && [ -f "${APPIMAGE}" ]; then
    SIZE=$(du -h "${APPIMAGE}" | cut -f1)
    log "  AppImage: $(basename "${APPIMAGE}")"
    log "  Size:     ${SIZE}"
    log "  Path:     ${APPIMAGE}"
    chmod +x "${APPIMAGE}"
    log ""
    log "  To test:"
    log "    ./${APPIMAGE##*/}"
    log ""
    log "  To test in a clean container:"
    log "    docker run --rm -v $(pwd):/app ubuntu:22.04 /app/${APPIMAGE##*/} --help"
else
    log "  ERROR: AppImage not found after build"
    exit 1
fi

log ""
log "--- Done ---"
log "Finished: $(date -u '+%Y-%m-%d %H:%M:%S') UTC"
log ""

