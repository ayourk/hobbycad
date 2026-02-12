#!/bin/bash
# =====================================================================
#  tools/linux/build-dev.sh — HobbyCAD Developer Build Script
# =====================================================================
#
#  Configures and builds HobbyCAD for day-to-day development.
#  All output (stdout + stderr) is logged to build-hobbycad.log
#  in the project root and also displayed on the terminal.
#
#  Parameters are processed left to right:
#
#    ./tools/linux/build-dev.sh                     # Debug build
#    ./tools/linux/build-dev.sh release             # Release build
#    ./tools/linux/build-dev.sh clean               # Clean only (no build)
#    ./tools/linux/build-dev.sh clean debug         # Clean + Debug build
#    ./tools/linux/build-dev.sh clean release       # Clean + Release build
#    ./tools/linux/build-dev.sh run                 # Run if built,
#                                                   #   else build + run
#    ./tools/linux/build-dev.sh run-reduced         # Run in Reduced Mode
#    ./tools/linux/build-dev.sh run-cli             # Run in CLI mode
#    ./tools/linux/build-dev.sh clean run           # Clean + build + run
#    ./tools/linux/build-dev.sh clean release run   # Clean + Release + run
#
#  "run" behavior:
#    - If build dir and binary exist: just launch (no build)
#    - If either is missing: devtest check → build → launch
#    - "clean" before "run" forces a full rebuild
#
#  SPDX-License-Identifier: GPL-3.0-only
#
# =====================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
LOG="${PROJECT_ROOT}/build-hobbycad.log"
BINARY="${BUILD_DIR}/src/hobbycad/hobbycad"

DEVTEST_DIR="${PROJECT_ROOT}/devtest"
DEVTEST_LOG="${DEVTEST_DIR}/devtest.log"

# Track overall status (0 = ok, non-zero = failure)
EXIT_CODE=0

# ---- Verify cmake is available ---------------------------------------

if ! command -v cmake &>/dev/null; then
    echo "  [FAIL] cmake not found.  Install via:"
    echo "         sudo apt install cmake ninja-build"
    exit 1
fi
# ---- Parse arguments -------------------------------------------------

BUILD_TYPE="Debug"
ACTIONS=()

for arg in "$@"; do
    case "${arg,,}" in
        release)  BUILD_TYPE="Release" ;;
        debug)    BUILD_TYPE="Debug" ;;
        clean)    ACTIONS+=("clean") ;;
        run)      ACTIONS+=("run") ;;
        run-cli)  ACTIONS+=("run-cli") ;;
        run-reduced) ACTIONS+=("run-reduced") ;;
        *)
            echo "Usage: $0 [debug|release] [clean] [run|run-reduced|run-cli]"
            echo ""
            echo "Log written to ${LOG}"
            exit 1
            ;;
    esac
done

# Default action: build
if [ ${#ACTIONS[@]} -eq 0 ]; then
    ACTIONS=("build")
fi

# ---- Logging setup ---------------------------------------------------

exec > >(tee "${LOG}") 2>&1

echo "====================================================================="
echo "  HobbyCAD Developer Build"
echo "====================================================================="
echo ""
echo "  Project root : ${PROJECT_ROOT}"
echo "  Build dir    : ${BUILD_DIR}"
echo "  Build type   : ${BUILD_TYPE}"
echo "  Log file     : ${LOG}"
echo "  Date         : $(date)"
echo "  Cores        : $(nproc)"
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
            echo "--- Devtest: result line missing or failed — rerunning ---"
            echo ""
        fi
    fi

    if [ "${devtest_needed}" = true ]; then
        echo "--- Running devtest ---"
        echo ""

        if [ ! -d "${DEVTEST_DIR}" ]; then
            echo "  WARNING: devtest/ directory not found — skipping"
            echo ""
            return 0
        fi

        (
            cd "${DEVTEST_DIR}"
            cmake -B build 2>&1
            cmake --build build -j"$(nproc)" 2>&1
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

    local preset="linux-${BUILD_TYPE,,}"
    cmake --preset "${preset}" -S "${PROJECT_ROOT}"

    echo ""
    echo "--- Building (Ninja, $(nproc) jobs) ---"
    echo ""

    cmake --build --preset "${preset}" -j"$(nproc)"

    echo ""

    if [ -f "${BINARY}" ]; then
        echo "====================================================================="
        echo "  Build successful"
        echo "====================================================================="
        echo ""
        echo "  Binary : ${BINARY}"
        echo "  Size   : $(du -h "${BINARY}" | cut -f1)"
        echo "  Type   : ${BUILD_TYPE}"
        echo ""
        return 0
    else
        echo "====================================================================="
        echo "  Build FAILED — binary not found"
        echo "====================================================================="
        echo ""
        return 1
    fi
}

do_run() {
    # If the binary already exists, just launch it
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

    # Binary missing — need to build first
    echo "--- Binary not found — building first ---"
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
    # If the binary already exists, just launch it in reduced mode
    if [ -f "${BINARY}" ]; then
        echo "--- Launching HobbyCAD (Reduced Mode) in background ---"
        echo ""
        HOBBYCAD_REDUCED_MODE=1 "${BINARY}" &
        BGPID=$!
        echo "  PID    : ${BGPID}"
        echo "  Stop   : kill ${BGPID}"
        echo ""
        return 0
    fi

    # Binary missing — need to build first
    echo "--- Binary not found — building first ---"
    echo ""
    if ! do_build; then
        return 1
    fi

    echo "--- Launching HobbyCAD (Reduced Mode) in background ---"
    echo ""
    HOBBYCAD_REDUCED_MODE=1 "${BINARY}" &
    BGPID=$!
    echo "  PID    : ${BGPID}"
    echo "  Stop   : kill ${BGPID}"
    echo ""
    return 0
}

do_run_cli() {
    # If the binary already exists, just launch it in CLI mode
    if [ -f "${BINARY}" ]; then
        echo "--- Launching HobbyCAD CLI mode ---"
        echo ""
        "${BINARY}" --no-gui
        return $?
    fi

    # Binary missing — need to build first
    echo "--- Binary not found — building first ---"
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
    # If no run was requested, show usage hints
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
