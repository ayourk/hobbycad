#!/bin/bash
# =====================================================================
#  tools/macos/setup-env.sh — macOS development environment setup
# =====================================================================
#
#  Checks for required tools and offers to install anything missing.
#  Handles Xcode Command Line Tools, Homebrew, build tools, the
#  HobbyCAD Homebrew tap, pinned dependencies, repository clone,
#  and CMAKE_PREFIX_PATH.
#
#  Can be run from inside a git clone or downloaded standalone.
#  When run standalone, it offers to clone the repository after
#  the toolchain is set up.
#
#  Run with:
#    bash tools/macos/setup-env.sh
#
#  AI Attribution: This script was primarily generated with the
#                  assistance of Claude AI (Anthropic).  Human review
#                  and editorial direction by the project author.
#
#  SPDX-License-Identifier: GPL-3.0-only
#
# =====================================================================

set -euo pipefail

# --- Helpers -----------------------------------------------------------

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
CYAN='\033[0;36m'
GRAY='\033[0;37m'
WHITE='\033[1;37m'
NC='\033[0m'

header() {
    echo ""
    echo -e "${CYAN}$(printf '=%.0s' {1..60})${NC}"
    echo -e "${CYAN}  $1${NC}"
    echo -e "${CYAN}$(printf '=%.0s' {1..60})${NC}"
}

ok()   { echo -e "  ${GREEN}[OK]${NC}   $1"; }
warn() { echo -e "  ${YELLOW}[WARN]${NC} $1"; }
fail() { echo -e "  ${RED}[FAIL]${NC} $1"; }
info() { echo -e "  ${GRAY}[INFO]${NC} $1"; }

# Prompt for Y/n.  Returns 0 (true) on yes, 1 on no.
# With --yes flag, always returns 0.
confirm() {
    if [ "${NON_INTERACTIVE:-0}" = "1" ]; then
        return 0
    fi
    printf "  %s (Y/n) " "$1"
    read -r answer
    case "$answer" in
        ''|[Yy]) return 0 ;;
        *)       return 1 ;;
    esac
}

# --- Parse arguments ---------------------------------------------------

NON_INTERACTIVE=0
REPO_URL="https://github.com/ayourk/hobbycad.git"
CLONE_DIR=""

for arg in "$@"; do
    case "$arg" in
        --yes|-y)       NON_INTERACTIVE=1 ;;
        --repo-url=*)   REPO_URL="${arg#*=}" ;;
        --clone-dir=*)  CLONE_DIR="${arg#*=}" ;;
        --help|-h)
            echo "Usage: setup-env.sh [OPTIONS]"
            echo ""
            echo "  --yes, -y           Answer yes to all prompts"
            echo "  --repo-url=URL      Repository URL"
            echo "  --clone-dir=DIR     Clone target (parent dir)"
            echo "  --help, -h          Show this help"
            exit 0
            ;;
    esac
done

# --- Detect standalone vs. in-repo ------------------------------------
#
# If this script lives at tools/macos/setup-env.sh inside a
# HobbyCAD clone, the repo root is two directories up and will
# contain CMakeLists.txt.  If the script was downloaded on its
# own, that path won't exist.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
IS_IN_REPO=false
REPO_ROOT=""
CLONE_PATH=""

CANDIDATE_ROOT="$(cd "$SCRIPT_DIR/../.." 2>/dev/null && pwd)" \
    || true
if [ -n "$CANDIDATE_ROOT" ] &&
   [ -f "$CANDIDATE_ROOT/CMakeLists.txt" ]; then
    if grep -qi "hobbycad" "$CANDIDATE_ROOT/CMakeLists.txt"; then
        IS_IN_REPO=true
        REPO_ROOT="$CANDIDATE_ROOT"
    fi
fi

# --- Banner ------------------------------------------------------------

echo ""
echo -e "${WHITE}  HobbyCAD macOS Environment Setup${NC}"
echo -e "  ----------------------------------"

# Detect architecture
ARCH=$(uname -m)
if [ "$ARCH" = "arm64" ]; then
    BREW_PREFIX="/opt/homebrew"
    info "Architecture: Apple Silicon (arm64)"
else
    BREW_PREFIX="/usr/local"
    info "Architecture: Intel (x86_64)"
fi

if [ "$IS_IN_REPO" = true ]; then
    info "Repo root:    $REPO_ROOT"
else
    info "Mode:         standalone (not inside a clone)"
fi
echo ""

ALL_OK=true

# ===================================================================
#  1. XCODE COMMAND LINE TOOLS
# ===================================================================

header "1/7  Xcode Command Line Tools"

if xcode-select -p &>/dev/null; then
    ok "Xcode CLT installed."
    CLT_VER=$(clang++ --version 2>&1 | head -1)
    info "  $CLT_VER"
else
    fail "Xcode Command Line Tools not found."

    if confirm "Install Xcode Command Line Tools?"; then
        info "Running xcode-select --install..."
        info "(A dialog will appear — click 'Install'.)"
        xcode-select --install 2>/dev/null || true

        # Wait for installation to complete
        info "Waiting for installation to finish..."
        until xcode-select -p &>/dev/null; do
            sleep 5
        done
        ok "Xcode Command Line Tools installed."
    else
        info "Install manually: xcode-select --install"
        ALL_OK=false
    fi
fi

# ===================================================================
#  2. HOMEBREW
# ===================================================================

header "2/7  Homebrew"

if command -v brew &>/dev/null; then
    ok "Homebrew found: $(brew --version | head -1)"
    # Verify prefix matches architecture
    ACTUAL_PREFIX="$(brew --prefix)"
    if [ "$ACTUAL_PREFIX" != "$BREW_PREFIX" ]; then
        warn "Homebrew at $ACTUAL_PREFIX (expected $BREW_PREFIX)."
        info "Rosetta Homebrew? This may work but is untested."
        BREW_PREFIX="$ACTUAL_PREFIX"
    fi
else
    fail "Homebrew not found."

    if confirm "Install Homebrew?"; then
        info "Running the Homebrew installer..."
        /bin/bash -c "$(curl -fsSL \
            https://raw.githubusercontent.com/Homebrew/\
install/HEAD/install.sh)"

        # Activate for this session
        if [ -x "$BREW_PREFIX/bin/brew" ]; then
            eval "$("$BREW_PREFIX/bin/brew" shellenv)"
            ok "Homebrew installed."
        else
            fail "Homebrew install did not create $BREW_PREFIX/bin/brew."
            info "Install manually: https://brew.sh/"
            ALL_OK=false
        fi
    else
        info "Install manually: https://brew.sh/"
        ALL_OK=false
    fi
fi

# ===================================================================
#  3. BUILD TOOLS (cmake, ninja, python)
# ===================================================================

header "3/7  Build tools"

if command -v brew &>/dev/null; then
    TOOLS_MISSING=()

    if ! command -v cmake &>/dev/null; then
        TOOLS_MISSING+=("cmake")
    fi
    if ! command -v ninja &>/dev/null; then
        TOOLS_MISSING+=("ninja")
    fi
    # python3 ships with macOS 12.3+ but Homebrew version preferred
    if ! brew list python &>/dev/null 2>&1; then
        TOOLS_MISSING+=("python")
    fi

    if [ ${#TOOLS_MISSING[@]} -eq 0 ]; then
        ok "Build tools installed."
        info "  cmake : $(cmake --version | head -1)"
        info "  ninja : $(ninja --version)"
        info "  python: $(python3 --version)"
    else
        LIST=$(IFS=', '; echo "${TOOLS_MISSING[*]}")
        warn "Missing: $LIST"

        if confirm "Install missing build tools via Homebrew?"; then
            info "Running: brew install ${TOOLS_MISSING[*]}"
            brew install "${TOOLS_MISSING[@]}"
            ok "Build tools installed."
        else
            info "Install manually:"
            info "  brew install ${TOOLS_MISSING[*]}"
            ALL_OK=false
        fi
    fi

    # cmake minimum version check
    if command -v cmake &>/dev/null; then
        CMAKE_VER=$(cmake --version | head -1 |
            grep -oE '[0-9]+\.[0-9]+')
        CMAKE_MAJOR=$(echo "$CMAKE_VER" | cut -d. -f1)
        CMAKE_MINOR=$(echo "$CMAKE_VER" | cut -d. -f2)
        if [ "$CMAKE_MAJOR" -lt 3 ] ||
           { [ "$CMAKE_MAJOR" -eq 3 ] &&
             [ "$CMAKE_MINOR" -lt 22 ]; }; then
            warn "CMake 3.22+ required (found $CMAKE_VER)."
            info "  brew upgrade cmake"
            ALL_OK=false
        fi
    fi
else
    info "Homebrew not available — skipping build tools."
    ALL_OK=false
fi

# ===================================================================
#  4. HOBBYCAD HOMEBREW TAP + DEPENDENCIES
# ===================================================================

header "4/7  HobbyCAD dependencies"

if command -v brew &>/dev/null; then

    # Tap
    if brew tap | grep -q "^ayourk/hobbycad$"; then
        ok "Tap ayourk/hobbycad already added."
    else
        if confirm "Add the HobbyCAD Homebrew tap?"; then
            info "Running: brew tap ayourk/hobbycad"
            brew tap ayourk/hobbycad
            ok "Tap added."
        else
            info "Add manually: brew tap ayourk/hobbycad"
            ALL_OK=false
        fi
    fi

    # Pinned keg-only formulas
    PINNED_FORMULAS=(
        "ayourk/hobbycad/opencascade@7.6.3"
        "ayourk/hobbycad/libzip@1.7.3"
        "ayourk/hobbycad/libgit2@1.7.2"
    )

    # Core formulas
    CORE_FORMULAS=(
        "qt@6"
        "pybind11"
        "libpng"
        "jpeg-turbo"
    )

    PINNED_MISSING=()
    for formula in "${PINNED_FORMULAS[@]}"; do
        short=$(echo "$formula" | sed 's|.*/||')
        if ! brew list "$formula" &>/dev/null 2>&1; then
            PINNED_MISSING+=("$formula")
        else
            ok "$short installed."
        fi
    done

    CORE_MISSING=()
    for formula in "${CORE_FORMULAS[@]}"; do
        if ! brew list "$formula" &>/dev/null 2>&1; then
            CORE_MISSING+=("$formula")
        else
            ok "$formula installed."
        fi
    done

    if [ ${#PINNED_MISSING[@]} -gt 0 ]; then
        LIST=$(IFS=', '; echo "${PINNED_MISSING[*]}")
        warn "Missing pinned formulas: $LIST"

        if confirm "Install pinned formulas?"; then
            info "Installing pinned formulas..."
            brew install "${PINNED_MISSING[@]}"
            ok "Pinned formulas installed."
        else
            info "Install manually:"
            for f in "${PINNED_MISSING[@]}"; do
                info "  brew install $f"
            done
            ALL_OK=false
        fi
    fi

    if [ ${#CORE_MISSING[@]} -gt 0 ]; then
        LIST=$(IFS=', '; echo "${CORE_MISSING[*]}")
        warn "Missing core formulas: $LIST"

        if confirm "Install core formulas?"; then
            info "Installing core formulas..."
            brew install "${CORE_MISSING[@]}"
            ok "Core formulas installed."
        else
            info "Install manually:"
            for f in "${CORE_MISSING[@]}"; do
                info "  brew install $f"
            done
            ALL_OK=false
        fi
    fi

    if [ ${#PINNED_MISSING[@]} -eq 0 ] &&
       [ ${#CORE_MISSING[@]} -eq 0 ]; then
        ok "All dependencies installed."
    fi

else
    info "Homebrew not available — skipping dependencies."
    ALL_OK=false
fi

# ===================================================================
#  5. REPOSITORY
# ===================================================================

header "5/7  HobbyCAD Repository"

if [ "$IS_IN_REPO" = true ]; then
    ok "Running from inside a clone at $REPO_ROOT"
    CLONE_PATH="$REPO_ROOT"
else
    info "Script is running standalone (not inside a clone)."

    if command -v git &>/dev/null; then
        if confirm "Clone the HobbyCAD repository?"; then
            # Determine target directory
            if [ -n "$CLONE_DIR" ]; then
                TARGET_PARENT="$CLONE_DIR"
            elif [ "$NON_INTERACTIVE" = "1" ]; then
                TARGET_PARENT="$(pwd)"
            else
                DEFAULT="$(pwd)"
                printf "  Clone to? [%s/hobbycad] " "$DEFAULT"
                read -r answer
                if [ -z "$answer" ]; then
                    TARGET_PARENT="$DEFAULT"
                else
                    TARGET_PARENT="$answer"
                fi
            fi

            TARGET_DIR="$TARGET_PARENT/hobbycad"

            if [ -d "$TARGET_DIR/.git" ]; then
                ok "Repository already exists at $TARGET_DIR"
                CLONE_PATH="$TARGET_DIR"
            else
                info "Cloning to $TARGET_DIR..."
                if git clone "$REPO_URL" "$TARGET_DIR"; then
                    ok "Cloned to $TARGET_DIR"
                    CLONE_PATH="$TARGET_DIR"
                else
                    fail "git clone failed."
                    info "Clone manually:"
                    info "  git clone $REPO_URL"
                    ALL_OK=false
                fi
            fi
        else
            info "Clone manually when ready:"
            info "  git clone $REPO_URL"
        fi
    else
        warn "Git not available yet — cannot clone."
        info "Install Xcode CLT first (step 1), then re-run."
        ALL_OK=false
    fi
fi

# ===================================================================
#  6. CMAKE_PREFIX_PATH
# ===================================================================

header "6/7  CMAKE_PREFIX_PATH"

if command -v brew &>/dev/null; then
    # Build the expected value
    OCCT_PFX="$(brew --prefix \
        ayourk/hobbycad/opencascade@7.6.3 2>/dev/null || true)"
    LZIP_PFX="$(brew --prefix \
        ayourk/hobbycad/libzip@1.7.3 2>/dev/null || true)"
    LGIT_PFX="$(brew --prefix \
        ayourk/hobbycad/libgit2@1.7.2 2>/dev/null || true)"
    QT6_PFX="$(brew --prefix qt@6 2>/dev/null || true)"

    EXPECTED_PATH=""
    for pfx in "$OCCT_PFX" "$LZIP_PFX" "$LGIT_PFX" "$QT6_PFX"; do
        if [ -n "$pfx" ] && [ -d "$pfx" ]; then
            if [ -n "$EXPECTED_PATH" ]; then
                EXPECTED_PATH="$EXPECTED_PATH:$pfx"
            else
                EXPECTED_PATH="$pfx"
            fi
        fi
    done

    if [ -z "$EXPECTED_PATH" ]; then
        warn "Cannot determine CMAKE_PREFIX_PATH (deps not installed)."
        ALL_OK=false
    elif [ -n "${CMAKE_PREFIX_PATH:-}" ]; then
        # Check if all four prefixes are present
        MISSING_PFX=false
        for pfx in "$OCCT_PFX" "$LZIP_PFX" "$LGIT_PFX" "$QT6_PFX"; do
            if [ -n "$pfx" ] && [ -d "$pfx" ]; then
                if ! echo "$CMAKE_PREFIX_PATH" |
                     grep -q "$pfx"; then
                    MISSING_PFX=true
                fi
            fi
        done
        if [ "$MISSING_PFX" = true ]; then
            warn "CMAKE_PREFIX_PATH is set but incomplete."
            info "Expected:"
            info "  $EXPECTED_PATH"
        else
            ok "CMAKE_PREFIX_PATH is set and looks correct."
        fi
    else
        warn "CMAKE_PREFIX_PATH is not set."
        info "The pinned keg-only formulas require it."
        info ""
        info "Add to ~/.zshrc (or ~/.zprofile):"
        info ""
        info "  export CMAKE_PREFIX_PATH=\"\\"
        for pfx in "$OCCT_PFX" "$LZIP_PFX" "$LGIT_PFX"; do
            if [ -n "$pfx" ] && [ -d "$pfx" ]; then
                info "  $pfx:\\"
            fi
        done
        if [ -n "$QT6_PFX" ] && [ -d "$QT6_PFX" ]; then
            info "  $QT6_PFX\""
        fi
        info ""

        # Detect shell config file
        SHELL_RC=""
        if [ -f "$HOME/.zshrc" ]; then
            SHELL_RC="$HOME/.zshrc"
        elif [ -f "$HOME/.zprofile" ]; then
            SHELL_RC="$HOME/.zprofile"
        fi

        if [ -n "$SHELL_RC" ] &&
           confirm "Append CMAKE_PREFIX_PATH to $SHELL_RC?"; then
            {
                echo ""
                echo "# HobbyCAD — keg-only formula paths"
                echo "export CMAKE_PREFIX_PATH=\"\\"
                echo "$EXPECTED_PATH\""
            } >> "$SHELL_RC"
            export CMAKE_PREFIX_PATH="$EXPECTED_PATH"
            ok "Appended to $SHELL_RC and set for this session."
            info "New terminal windows will inherit this."
        else
            if [ -z "$SHELL_RC" ]; then
                info "No ~/.zshrc or ~/.zprofile found."
                info "Create one and add the export line above."
            fi
        fi
    fi
else
    info "Homebrew not available — skipping CMAKE_PREFIX_PATH."
    ALL_OK=false
fi

# ===================================================================
#  7. SUMMARY
# ===================================================================

header "7/7  Summary"

# Final tool verification
if command -v clang++ &>/dev/null; then
    ok "clang++ : $(clang++ --version 2>&1 | head -1)"
fi
if command -v cmake &>/dev/null; then
    ok "cmake   : $(cmake --version | head -1)"
fi
if command -v ninja &>/dev/null; then
    ok "ninja   : $(ninja --version)"
fi
if command -v git &>/dev/null; then
    ok "git     : $(git --version)"
fi
if command -v python3 &>/dev/null; then
    ok "python  : $(python3 --version)"
fi

echo ""

NCPU=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)

if [ "$ALL_OK" = true ]; then
    ok "Environment is ready."
    echo ""
    info "Next steps:"

    # Step numbering adjusts based on whether clone happened
    STEP=1

    if [ -z "$CLONE_PATH" ]; then
        info "  $STEP. Clone the repo:"
        info "       git clone $REPO_URL hobbycad"
        info "       cd hobbycad"
        STEP=$((STEP + 1))
    else
        info "  Repository: $CLONE_PATH"
    fi

    info ""
    info "  $STEP. Verify dependencies:"
    if [ -n "$CLONE_PATH" ]; then
        info "       cd $CLONE_PATH/devtest"
    else
        info "       cd devtest"
    fi
    info "       cmake -B build"
    info "       cmake --build build -j$NCPU"
    info "       ./build/depcheck"
    STEP=$((STEP + 1))

    info ""
    info "  $STEP. Build HobbyCAD:"
    if [ -n "$CLONE_PATH" ]; then
        info "       cd $CLONE_PATH"
    else
        info "       cd .."
    fi
    info "       cmake -B build -G Ninja \\"
    info "         -DCMAKE_BUILD_TYPE=Debug"
    info "       cmake --build build -j$NCPU"
    STEP=$((STEP + 1))

    info ""
    info "  $STEP. Run:"
    info "       ./build/src/hobbycad/hobbycad"
else
    fail "Some items need attention — see above."
    echo ""
    info "Fix the issues, then run this script again."
fi

echo ""
