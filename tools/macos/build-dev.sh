#!/bin/bash
# =====================================================================
#  tools/macos/build-dev.sh — HobbyCAD Developer Build Script (macOS)
# =====================================================================
#
#  Configures and builds HobbyCAD for day-to-day development.
#  All output (stdout + stderr) is logged to build-hobbycad.log
#  in the project root and also displayed on the terminal.
#
#  Parameters are processed left to right:
#
#    ./tools/macos/build-dev.sh                     # Debug build
#    ./tools/macos/build-dev.sh release             # Release build
#    ./tools/macos/build-dev.sh clean               # Clean only (no build)
#    ./tools/macos/build-dev.sh clean debug         # Clean + Debug build
#    ./tools/macos/build-dev.sh clean release       # Clean + Release build
#    ./tools/macos/build-dev.sh run                 # Run if built,
#                                                   #   else build + run
#    ./tools/macos/build-dev.sh run-reduced         # Run in Reduced Mode
#    ./tools/macos/build-dev.sh run-cli             # Run in CLI mode
#    ./tools/macos/build-dev.sh clean run           # Clean + build + run
#    ./tools/macos/build-dev.sh clean release run   # Clean + Release + run
#
#  "run" behavior:
#    - If build dir and binary exist: just launch (no build)
#    - If either is missing: devtest check → build → launch
#    - "clean" before "run" forces a full rebuild
#
#  Prerequisites:
#    - Xcode Command Line Tools (xcode-select --install)
#    - Homebrew with cmake, ninja, qt@6, opencascade installed
#      (auto-detected from /opt/homebrew or /usr/local)
#    - Can be run from any directory (repo root, tools/macos, etc.)
#
#  SPDX-License-Identifier: GPL-3.0-only
#
# =====================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
LOG="${PROJECT_ROOT}/build-hobbycad.log"
# The target is MACOSX_BUNDLE, so the executable lives inside the .app;
# the flat path is the fallback for a build configured without the bundle.
APP_BUNDLE="${BUILD_DIR}/src/hobbycad/HobbyCAD.app"
BINARY="${APP_BUNDLE}/Contents/MacOS/HobbyCAD"
[ -f "${BINARY}" ] || [ -d "${APP_BUNDLE}" ] || BINARY="${BUILD_DIR}/src/hobbycad/hobbycad"

DEVTEST_DIR="${PROJECT_ROOT}/devtest"
DEVTEST_LOG="${DEVTEST_DIR}/devtest.log"

# Track overall status (0 = ok, non-zero = failure)
EXIT_CODE=0

# ---- Locate Homebrew tools -------------------------------------------
#
#  If cmake isn't already on PATH, look for Homebrew in the standard
#  locations and prepend its bin/ so the build works from any shell.

if ! command -v cmake &>/dev/null; then
    BREW_BIN=""
    for dir in /opt/homebrew/bin /usr/local/bin; do
        if [ -x "${dir}/cmake" ]; then
            BREW_BIN="${dir}"
            break
        fi
    done
    if [ -n "${BREW_BIN}" ]; then
        echo "  [INFO] Adding ${BREW_BIN} to PATH"
        export PATH="${BREW_BIN}:${PATH}"
    else
        echo "  [FAIL] cmake not found.  Install via: brew install cmake"
        exit 1
    fi
fi

# ---- Parse arguments -------------------------------------------------

# macOS ships bash 3.2, which has no ${var,,}; lowercase through tr.
lower() { printf '%s' "$1" | tr '[:upper:]' '[:lower:]'; }

BUILD_TYPE="Debug"
ACTIONS=()
BUILD_REQUESTED=false

for arg in "$@"; do
    case "$(lower "$arg")" in
        release)     BUILD_TYPE="Release"; BUILD_REQUESTED=true ;;
        debug)       BUILD_TYPE="Debug"; BUILD_REQUESTED=true ;;
        clean)       ACTIONS+=("clean") ;;
        run)         ACTIONS+=("run") ;;
        run-reduced) ACTIONS+=("run-reduced") ;;
        run-cli)     ACTIONS+=("run-cli") ;;
        *)
            echo "Usage: $0 [debug|release] [clean] [run|run-reduced|run-cli]"
            echo ""
            echo "Log written to ${LOG}"
            exit 1
            ;;
    esac
done

# Build when nothing was asked for, and also when a build type or linkage was
# named alongside clean. A bare ACTIONS-empty test never reached the second
# case: clean had already filled ACTIONS, so "clean release" cleaned and
# exited despite being documented as clean-then-build.
if [ ${#ACTIONS[@]} -eq 0 ]; then
    ACTIONS=("build")
elif [ "${BUILD_REQUESTED:-false}" = true ]; then
    has_buildish=false
    for a in "${ACTIONS[@]}"; do
        case "${a}" in
            build|run|run-reduced|run-cli|run-script) has_buildish=true ;;
        esac
    done
    if [ "${has_buildish}" = false ]; then
        ACTIONS+=("build")
    fi
fi

# ---- Detect generator ------------------------------------------------

if command -v ninja &>/dev/null; then
    GENERATOR="Ninja"
else
    GENERATOR="Unix Makefiles"
fi

# ---- Detect Homebrew paths -------------------------------------------

BREW_PREFIX="$(brew --prefix 2>/dev/null || echo "/opt/homebrew")"
CORES="$(sysctl -n hw.logicalcpu 2>/dev/null || echo 4)"

CMAKE_EXTRA_ARGS=()

# Qt: $HOBBYCAD_QT_DIR, else the prebuilt 6.4.2 the setup script documents
# (~/Qt/6.4.2/macos), else Homebrew's qt@6.
QT_PREFIX="${HOBBYCAD_QT_DIR:-}"
if [ -z "${QT_PREFIX}" ] && [ -d "${HOME}/Qt/6.4.2/macos/lib/cmake/Qt6" ]; then
    QT_PREFIX="${HOME}/Qt/6.4.2/macos"
fi
if [ -z "${QT_PREFIX}" ] && [ -d "${BREW_PREFIX}/opt/qt@6/lib/cmake" ]; then
    QT_PREFIX="${BREW_PREFIX}/opt/qt@6"
fi

# OCCT: the tap's keg-only pin first (setup-env.sh installs it), then the
# previous pin, then Homebrew core's rolling opencascade.
OCCT_DIR=""
OCCT_PREFIX=""
for occt in opencascade@8.0.1 opencascade; do
    if [ -d "${BREW_PREFIX}/opt/${occt}/lib/cmake/opencascade" ]; then
        OCCT_PREFIX="${BREW_PREFIX}/opt/${occt}"
        OCCT_DIR="${OCCT_PREFIX}/lib/cmake/opencascade"
        break
    fi
done

# CMAKE_PREFIX_PATH: the prebuilt Qt plus every keg-only formula the tap
# pins (keg-only means nothing under ${BREW_PREFIX}/{include,lib} points at
# them, so CMake and pkg-config only find them through this list). The
# caller's own CMAKE_PREFIX_PATH, if any, is kept behind ours.
PREFIXES=()
[ -n "${QT_PREFIX}" ] && PREFIXES+=("${QT_PREFIX}")
[ -n "${OCCT_PREFIX}" ] && PREFIXES+=("${OCCT_PREFIX}")
for keg in libzip@1.7.3 libgit2@1.9.2; do
    [ -d "${BREW_PREFIX}/opt/${keg}" ] && PREFIXES+=("${BREW_PREFIX}/opt/${keg}")
done
if [ ${#PREFIXES[@]} -gt 0 ]; then
    PREFIX_PATH="$(IFS=:; echo "${PREFIXES[*]}")"
    export CMAKE_PREFIX_PATH="${PREFIX_PATH}${CMAKE_PREFIX_PATH:+:${CMAKE_PREFIX_PATH}}"
    CMAKE_EXTRA_ARGS+=("-DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}")
fi
if [ -n "${OCCT_DIR}" ]; then
    export OpenCASCADE_DIR="${OCCT_DIR}"
    CMAKE_EXTRA_ARGS+=("-DOpenCASCADE_DIR=${OCCT_DIR}")
fi
# pkg-config for the keg-only libzip and libgit2 (devtest and the build
# both probe them through pkg-config first).
for pfx in ${PREFIXES[@]+"${PREFIXES[@]}"}; do
    [ -d "${pfx}/lib/pkgconfig" ] && export PKG_CONFIG_PATH="${pfx}/lib/pkgconfig${PKG_CONFIG_PATH:+:${PKG_CONFIG_PATH}}"
done

# ---- Logging setup ---------------------------------------------------

exec > >(tee "${LOG}") 2>&1

echo "====================================================================="
echo "  HobbyCAD Developer Build (macOS)"
echo "====================================================================="
echo ""
echo "  Project root : ${PROJECT_ROOT}"
echo "  Build dir    : ${BUILD_DIR}"
echo "  Build type   : ${BUILD_TYPE}"
echo "  Generator    : ${GENERATOR}"
echo "  Log file     : ${LOG}"
echo "  Date         : $(date)"
echo "  Cores        : ${CORES}"
echo "  Brew prefix  : ${BREW_PREFIX}"
echo "  Qt           : ${QT_PREFIX:-(not found)}"
echo "  OCCT         : ${OCCT_DIR:-(not found)}"
echo "  Prefix path  : ${CMAKE_PREFIX_PATH:-(none)}"
echo "  Actions      : ${ACTIONS[*]}"
echo ""

# ---- Helper functions ------------------------------------------------
# Each function returns 0 on success, non-zero on failure.
# The caller checks the return value and sets EXIT_CODE.

do_devtest() {
    local devtest_needed=true

    if [ -f "${DEVTEST_LOG}" ]; then
        local result_line
        result_line="$(grep "^DEVTEST_RESULT:.*\[PASS\]" "${DEVTEST_LOG}" | tail -1 || true)"
        if [ -n "${result_line}" ]; then
            local display
            display="$(echo "${result_line}" | sed 's/^DEVTEST_RESULT: //')"
            echo "--- Devtest: ${display} (skipping) ---"
            echo "  Log: ${DEVTEST_LOG}"
            echo ""
            devtest_needed=false
        else
            echo "--- Devtest: result line missing or failed, rerunning ---"
            echo ""
        fi
    fi

    if [ "${devtest_needed}" = true ]; then
        echo "--- Running devtest ---"
        echo ""

        if [ ! -d "${DEVTEST_DIR}" ]; then
            echo "  WARNING: devtest/ directory not found, skipping"
            echo ""
            return 0
        fi

        (
            cd "${DEVTEST_DIR}"
            # Same generator and the exported CMAKE_PREFIX_PATH /
            # OpenCASCADE_DIR as the real configure, so the check probes
            # the toolchain the build will use.
            cmake -B build -G "${GENERATOR}" 2>&1
            cmake --build build -j"${CORES}" 2>&1
            ./build/depcheck 2>&1 | tee "${DEVTEST_LOG}"
        )
        local devtest_exit=${PIPESTATUS[0]:-$?}
        if [ "${devtest_exit}" -ne 0 ]; then
            echo ""
            echo "  Devtest FAILED (exit code ${devtest_exit})"
            echo "  Fix dependency issues before building."
            echo "  See: ${DEVTEST_LOG}"
            echo ""
            return 1
        fi
        echo ""
        echo "  Devtest passed"
        echo ""
    fi

    return 0
}

do_clean() {
    echo "--- Cleaning build directory ---"
    if [ -d "${BUILD_DIR}" ]; then
        rm -rf "${BUILD_DIR}"
        echo "  Removed: ${BUILD_DIR}"
    else
        echo "  Nothing to clean"
    fi
    echo ""
    return 0
}

do_build() {
    if ! do_devtest; then
        return 1
    fi

    echo "--- Configuring (CMake) ---"
    echo ""

    local preset="macos-$(lower "$BUILD_TYPE")"

    # CMAKE_PREFIX_PATH and OpenCASCADE_DIR are exported above (the preset
    # has no cache entries for them, so the environment carries them).
    cmake --preset "${preset}" -S "${PROJECT_ROOT}"

    echo ""
    echo "--- Building (${GENERATOR}, ${CORES} jobs) ---"
    echo ""

    cmake --build --preset "${preset}" -j"${CORES}"

    echo ""

    if [ -f "${BINARY}" ]; then
        echo "====================================================================="
        echo "  Build successful"
        echo "====================================================================="
        echo ""
        echo "  Binary : ${BINARY}"
        echo "  Size   : $(du -h "${BINARY}" | cut -f1)"
        [ -d "${APP_BUNDLE}" ] && echo "  Bundle : ${APP_BUNDLE}"
        echo "  Type   : ${BUILD_TYPE}"
        # The icons are rendered by the build itself (tools/render-icons.cpp
        # through cmake/GenerateIcons.cmake); say so, the way the binary is
        # reported, so a missing icon set is visible here and not first in a
        # package build.
        local icon_dir="${BUILD_DIR}/icons"
        local png_count
        png_count=$(ls "${icon_dir}"/hobbycad-*.png 2>/dev/null | wc -l | tr -d ' ')
        if [ "${png_count}" -gt 0 ]; then
            echo "  Icons  : ${png_count} PNG sizes$( [ -f "${icon_dir}/hobbycad.ico" ] && echo ", hobbycad.ico" )$( [ -f "${icon_dir}/hobbycad.icns" ] && echo ", hobbycad.icns" ) in ${icon_dir}"
        else
            echo "  Icons  : [WARN] none rendered in ${icon_dir} (see the CMake output for render_icons)"
        fi
        echo ""
        return 0
    else
        echo "====================================================================="
        echo "  Build FAILED: binary not found"
        echo "====================================================================="
        echo ""
        return 1
    fi
}

do_run() {
    if [ -f "${BINARY}" ]; then
        echo "--- Launching HobbyCAD in background ---"
        echo ""
        "${BINARY}" &
        BGPID=$!
        echo "  PID    : ${BGPID}"
        echo "  Stop   : kill ${BGPID}"
        echo ""
        return 0
    fi

    echo "--- Binary not found, building first ---"
    echo ""
    if ! do_build; then
        return 1
    fi

    echo "--- Launching HobbyCAD in background ---"
    echo ""
    "${BINARY}" &
    BGPID=$!
    echo "  PID    : ${BGPID}"
    echo "  Stop   : kill ${BGPID}"
    echo ""
    return 0
}

do_run_reduced() {
    if [ -f "${BINARY}" ]; then
        echo "--- Launching HobbyCAD Reduced Mode in background ---"
        echo ""
        HOBBYCAD_REDUCED_MODE=1 "${BINARY}" &
        BGPID=$!
        echo "  PID    : ${BGPID}"
        echo "  Stop   : kill ${BGPID}"
        echo ""
        return 0
    fi

    echo "--- Binary not found, building first ---"
    echo ""
    if ! do_build; then
        return 1
    fi

    echo "--- Launching HobbyCAD Reduced Mode in background ---"
    echo ""
    HOBBYCAD_REDUCED_MODE=1 "${BINARY}" &
    BGPID=$!
    echo "  PID    : ${BGPID}"
    echo "  Stop   : kill ${BGPID}"
    echo ""
    return 0
}

do_run_cli() {
    if [ -f "${BINARY}" ]; then
        echo "--- Launching HobbyCAD CLI mode ---"
        echo ""
        "${BINARY}" --no-gui
        return $?
    fi

    echo "--- Binary not found, building first ---"
    echo ""
    if ! do_build; then
        return 1
    fi

    echo "--- Launching HobbyCAD CLI mode ---"
    echo ""
    "${BINARY}" --no-gui
    return $?
}

# ---- Execute actions left to right -----------------------------------

for action in "${ACTIONS[@]}"; do
    case "${action}" in
        clean)       do_clean       || EXIT_CODE=$? ;;
        build)       do_build       || EXIT_CODE=$? ;;
        run)         do_run         || EXIT_CODE=$? ;;
        run-reduced) do_run_reduced || EXIT_CODE=$? ;;
        run-cli)     do_run_cli     || EXIT_CODE=$? ;;
    esac

    # Stop processing further actions on failure
    if [ "${EXIT_CODE}" -ne 0 ]; then
        break
    fi
done

# ---- Final output (always reached) ----------------------------------

if [ "${EXIT_CODE}" -eq 0 ]; then
    has_run=false
    for action in "${ACTIONS[@]}"; do
        if [ "${action}" = "run" ] || [ "${action}" = "run-reduced" ] || [ "${action}" = "run-cli" ]; then has_run=true; fi
    done

    if [ "${has_run}" = false ] && [ -f "${BINARY}" ]; then
        echo "  Run:"
        echo "    ${BINARY}"
        echo ""
        echo "  Reduced Mode (testing):"
        echo "    HOBBYCAD_REDUCED_MODE=1 ${BINARY}"
        echo ""
        echo "  CLI Mode:"
        echo "    ${BINARY} --no-gui"
        echo ""
    fi
fi

echo "Log written to ${LOG}"
exit "${EXIT_CODE}"
