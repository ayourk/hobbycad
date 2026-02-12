#!/bin/bash
# =====================================================================
#  tools/macos/setup-env.sh -- macOS development environment setup
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

# Resolve a tool to its full path.  Checks PATH first, then
# falls back to a known filesystem path.  Prints the path or
# returns 1 if not found.
resolve_exe() {
    local name="$1"
    local known="${2:-}"
    local found
    found=$(command -v "$name" 2>/dev/null) && {
        echo "$found"; return 0
    }
    if [ -n "$known" ] && [ -x "$known" ]; then
        echo "$known"; return 0
    fi
    return 1
}

# Locate Homebrew by checking the architecture-appropriate
# prefix.  Sets BREW to the full path of the brew binary and
# BREW_PREFIX to the installation root.
find_brew() {
    local arch
    arch=$(uname -m)
    if [ "$arch" = "arm64" ]; then
        BREW_PREFIX="/opt/homebrew"
    else
        BREW_PREFIX="/usr/local"
    fi
    BREW="$BREW_PREFIX/bin/brew"

    # Check PATH first in case user has a non-standard location
    local found
    found=$(command -v brew 2>/dev/null) && {
        BREW="$found"
        BREW_PREFIX="$("$BREW" --prefix 2>/dev/null || true)"
        return 0
    }

    [ -x "$BREW" ] && return 0
    return 1
}

# Shell config file for persisting environment variables.
# Checks the user's actual login shell rather than assuming zsh.
# Returns the path in SHELL_RC or empty string if not found.
detect_shell_rc() {
    SHELL_RC=""
    local login_shell
    login_shell=$(basename "${SHELL:-/bin/zsh}")

    case "$login_shell" in
        zsh)
            if [ -f "$HOME/.zshrc" ]; then
                SHELL_RC="$HOME/.zshrc"
            elif [ -f "$HOME/.zprofile" ]; then
                SHELL_RC="$HOME/.zprofile"
            fi
            ;;
        bash)
            if [ -f "$HOME/.bash_profile" ]; then
                SHELL_RC="$HOME/.bash_profile"
            elif [ -f "$HOME/.bashrc" ]; then
                SHELL_RC="$HOME/.bashrc"
            elif [ -f "$HOME/.profile" ]; then
                SHELL_RC="$HOME/.profile"
            fi
            ;;
        fish)
            local fish_conf
            fish_conf="${XDG_CONFIG_HOME:-$HOME/.config}"
            fish_conf="$fish_conf/fish/config.fish"
            if [ -f "$fish_conf" ]; then
                SHELL_RC="$fish_conf"
            fi
            ;;
        ksh)
            if [ -f "$HOME/.kshrc" ]; then
                SHELL_RC="$HOME/.kshrc"
            elif [ -f "$HOME/.profile" ]; then
                SHELL_RC="$HOME/.profile"
            fi
            ;;
        *)
            # Generic fallback
            if [ -f "$HOME/.profile" ]; then
                SHELL_RC="$HOME/.profile"
            fi
            ;;
    esac
}

# Write a comment + export line to $SHELL_RC.
# Handles fish (set -gx) vs. POSIX (export) syntax.
#   $1 = comment text (marker for removal)
#   $2 = variable name
#   $3 = value
write_shell_export() {
    local comment="$1" varname="$2" value="$3"
    local login_shell
    login_shell=$(basename "${SHELL:-/bin/zsh}")

    {
        echo ""
        echo "# $comment"
        if [ "$login_shell" = "fish" ]; then
            echo "set -gx $varname \"$value\""
        else
            echo "export $varname=\"$value\""
        fi
    } >> "$SHELL_RC"
}

# Write a multi-line export block to $SHELL_RC.
# Used for CMAKE_PREFIX_PATH where the value is long.
#   $1 = comment text (marker for removal)
#   $2 = variable name
#   $3 = value
write_shell_export_block() {
    local comment="$1" varname="$2" value="$3"
    local login_shell
    login_shell=$(basename "${SHELL:-/bin/zsh}")

    {
        echo ""
        echo "# $comment"
        if [ "$login_shell" = "fish" ]; then
            echo "set -gx $varname \"$value\""
        else
            echo "export $varname=\"\\"
            echo "$value\""
        fi
    } >> "$SHELL_RC"
}

# Remove a comment + export block from $SHELL_RC.
#   $1 = exact comment text
#   $2 = variable name
remove_shell_export() {
    local comment="$1" varname="$2"
    local login_shell
    login_shell=$(basename "${SHELL:-/bin/zsh}")

    local end_pat
    if [ "$login_shell" = "fish" ]; then
        end_pat="^set -gx $varname "
    else
        end_pat="^export $varname="
    fi
    sed -i '' \
        "/# $comment/,/$end_pat/d" \
        "$SHELL_RC" 2>/dev/null || true
}

# Check whether a comment marker exists in $SHELL_RC.
#   $1 = comment text
has_shell_block() {
    [ -n "$SHELL_RC" ] &&
    grep -q "# $1" "$SHELL_RC" 2>/dev/null
}

# Read the value of a variable from $SHELL_RC.
#   $1 = variable name
# Prints the value (unquoted) or empty string.
read_shell_var() {
    local varname="$1"
    local login_shell
    login_shell=$(basename "${SHELL:-/bin/zsh}")

    local line
    if [ "$login_shell" = "fish" ]; then
        line=$(grep "^set -gx $varname " \
            "$SHELL_RC" 2>/dev/null | head -1)
        echo "$line" | sed "s/^set -gx $varname //" |
            sed 's/^"//' | sed 's/"$//'
    else
        line=$(grep "^export $varname=" \
            "$SHELL_RC" 2>/dev/null | head -1)
        echo "$line" | sed "s/^export $varname=//" |
            sed 's/^"//' | sed 's/"$//'
    fi
}

# --- Parse arguments ---------------------------------------------------

NON_INTERACTIVE=0
UNINSTALL=0
REPO_URL="https://github.com/ayourk/hobbycad.git"
CLONE_DIR=""

for arg in "$@"; do
    case "$arg" in
        --yes|-y)       NON_INTERACTIVE=1 ;;
        --uninstall)    UNINSTALL=1 ;;
        --repo-url=*)   REPO_URL="${arg#*=}" ;;
        --clone-dir=*)  CLONE_DIR="${arg#*=}" ;;
        --help|-h)
            echo "Usage: setup-env.sh [OPTIONS]"
            echo ""
            echo "  --yes, -y           Answer yes to all prompts"
            echo "  --uninstall         Roll back changes"
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

# Detect Homebrew location
if find_brew; then
    info "Homebrew:      $BREW"
else
    info "Homebrew:      not found"
fi

# Detect architecture
ARCH=$(uname -m)
if [ "$ARCH" = "arm64" ]; then
    info "Architecture:  Apple Silicon (arm64)"
else
    info "Architecture:  Intel (x86_64)"
fi

if [ "$IS_IN_REPO" = true ]; then
    info "Repo root:     $REPO_ROOT"
else
    info "Mode:          standalone (not inside a clone)"
fi
echo ""

ALL_OK=true

# ===================================================================
#  UNINSTALL MODE
# ===================================================================
#
# Rolls back changes this script can make:
#   1. Removes CMAKE_PREFIX_PATH from shell config
#   2. Removes HOBBYCAD_REPO from shell config
#   3. Offers to delete the HobbyCAD clone directory
#   4. Offers to remove HobbyCAD Homebrew tap + formulas
#   5. Offers to remove build tools

if [ "$UNINSTALL" = "1" ]; then
    header "Uninstall -- rolling back changes"

    CHANGED=false
    detect_shell_rc

    # --- CMAKE_PREFIX_PATH ---------------------------------------------

    echo ""
    info "Checking CMAKE_PREFIX_PATH in shell config..."

    if has_shell_block "HobbyCAD -- keg-only formula paths"; then
        warn "Found HobbyCAD CMAKE_PREFIX_PATH block in $SHELL_RC"

        if confirm "Remove it?"; then
            remove_shell_export \
                "HobbyCAD -- keg-only formula paths" \
                "CMAKE_PREFIX_PATH"
            ok "Removed from $SHELL_RC"
            CHANGED=true
        else
            info "Skipped."
        fi
    else
        ok "No HobbyCAD CMAKE_PREFIX_PATH block found."
    fi

    # --- HOBBYCAD_REPO ------------------------------------------------

    echo ""
    info "Checking HOBBYCAD_REPO in shell config..."

    if has_shell_block "HobbyCAD -- clone path"; then
        SAVED_CLONE=$(read_shell_var "HOBBYCAD_REPO")

        warn "Found HOBBYCAD_REPO in $SHELL_RC"

        if [ -n "$SAVED_CLONE" ] && [ -d "$SAVED_CLONE" ]; then
            if confirm "Delete $SAVED_CLONE?"; then
                rm -rf "$SAVED_CLONE"
                if [ ! -d "$SAVED_CLONE" ]; then
                    ok "Deleted $SAVED_CLONE"
                    CHANGED=true
                else
                    fail "Could not fully remove."
                    info "Delete manually."
                fi
            else
                info "Skipped folder deletion."
            fi
        elif [ -n "$SAVED_CLONE" ]; then
            info "Directory already gone."
        fi

        if confirm "Remove HOBBYCAD_REPO from $SHELL_RC?"; then
            remove_shell_export \
                "HobbyCAD -- clone path" \
                "HOBBYCAD_REPO"
            ok "Removed from $SHELL_RC"
            CHANGED=true
        else
            info "Skipped."
        fi
    else
        ok "No HOBBYCAD_REPO block found."
    fi

    # --- Homebrew tap + formulas ---------------------------------------

    echo ""
    info "Checking for HobbyCAD Homebrew tap..."

    if [ -x "$BREW" ]; then
        if "$BREW" tap 2>/dev/null |
           grep -q "^ayourk/hobbycad$"; then
            warn "Tap ayourk/hobbycad is installed."

            if confirm "Uninstall tap formulas and remove tap?"; then
                info "Removing tap formulas..."
                "$BREW" uninstall --force \
                    ayourk/hobbycad/opencascade@7.6.3 \
                    ayourk/hobbycad/libzip@1.7.3 \
                    ayourk/hobbycad/libgit2@1.7.2 \
                    2>/dev/null || true
                "$BREW" untap ayourk/hobbycad 2>/dev/null || true
                ok "Tap and formulas removed."
                CHANGED=true
            else
                info "Skipped."
            fi
        else
            ok "Tap ayourk/hobbycad not found."
        fi

        # Core formulas
        echo ""
        info "Checking HobbyCAD core formulas..."

        CORE_INSTALLED=()
        for f in qt@6 librsvg; do
            if "$BREW" list "$f" &>/dev/null 2>&1; then
                CORE_INSTALLED+=("$f")
            fi
        done

        if [ ${#CORE_INSTALLED[@]} -gt 0 ]; then
            LIST=$(IFS=', '; echo "${CORE_INSTALLED[*]}")
            warn "Installed: $LIST"

            if confirm "Uninstall these formulas?"; then
                "$BREW" uninstall --force "${CORE_INSTALLED[@]}"
                ok "Core formulas removed."
                CHANGED=true
            else
                info "Skipped."
            fi
        else
            ok "No HobbyCAD core formulas installed."
        fi
    else
        ok "Homebrew not found -- nothing to do."
    fi

    # --- Summary -------------------------------------------------------

    echo ""
    if [ "$CHANGED" = true ]; then
        ok "Uninstall complete."
        info "Open a new terminal for changes to take effect."
    else
        info "No changes were made."
    fi
    echo ""
    exit 0
fi

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
        info "(A dialog will appear -- click 'Install'.)"
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

if [ -x "$BREW" ]; then
    ok "Homebrew found: $("$BREW" --version | head -1)"
    # Verify prefix matches architecture
    ACTUAL_PREFIX="$("$BREW" --prefix)"
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
        BREW="$BREW_PREFIX/bin/brew"
        if [ -x "$BREW" ]; then
            eval "$("$BREW" shellenv)"
            ok "Homebrew installed."
        else
            fail "Homebrew install did not create $BREW."
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

if [ -x "$BREW" ]; then
    TOOLS_MISSING=()

    CMAKE_EXE=$(resolve_exe "cmake" \
        "$BREW_PREFIX/bin/cmake") || true
    NINJA_EXE=$(resolve_exe "ninja" \
        "$BREW_PREFIX/bin/ninja") || true
    GIT_EXE=$(resolve_exe "git" "/usr/bin/git") || true

    if [ -z "$CMAKE_EXE" ]; then
        TOOLS_MISSING+=("cmake")
    fi
    if [ -z "$NINJA_EXE" ]; then
        TOOLS_MISSING+=("ninja")
    fi
    # python3 ships with macOS 12.3+ but Homebrew version preferred
    if ! "$BREW" list python &>/dev/null 2>&1; then
        TOOLS_MISSING+=("python")
    fi

    if [ ${#TOOLS_MISSING[@]} -eq 0 ]; then
        ok "Build tools installed."
        info "  cmake : $("$CMAKE_EXE" --version | head -1)"
        info "  ninja : $("$NINJA_EXE" --version)"
        PYTHON_EXE=$(resolve_exe "python3" \
            "$BREW_PREFIX/bin/python3") || true
        if [ -n "$PYTHON_EXE" ]; then
            info "  python: $("$PYTHON_EXE" --version)"
        fi
    else
        LIST=$(IFS=', '; echo "${TOOLS_MISSING[*]}")
        warn "Missing: $LIST"

        if confirm "Install missing build tools via Homebrew?"; then
            info "Running: brew install ${TOOLS_MISSING[*]}"
            "$BREW" install "${TOOLS_MISSING[@]}"
            ok "Build tools installed."
            # Re-resolve after install
            CMAKE_EXE=$(resolve_exe "cmake" \
                "$BREW_PREFIX/bin/cmake") || true
            NINJA_EXE=$(resolve_exe "ninja" \
                "$BREW_PREFIX/bin/ninja") || true
        else
            info "Install manually:"
            info "  brew install ${TOOLS_MISSING[*]}"
            ALL_OK=false
        fi
    fi

    # cmake minimum version check
    if [ -n "$CMAKE_EXE" ]; then
        CMAKE_VER=$("$CMAKE_EXE" --version | head -1 |
            grep -oE '[0-9]+\.[0-9]+')
        CMAKE_MAJOR=$(echo "$CMAKE_VER" | cut -d. -f1)
        CMAKE_MINOR=$(echo "$CMAKE_VER" | cut -d. -f2)
        if [ "$CMAKE_MAJOR" -lt 3 ] ||
           { [ "$CMAKE_MAJOR" -eq 3 ] &&
             [ "$CMAKE_MINOR" -lt 20 ]; }; then
            warn "CMake 3.20+ required (found $CMAKE_VER)."
            info "  brew upgrade cmake"
            ALL_OK=false
        fi
    fi
else
    info "Homebrew not available -- skipping build tools."
    ALL_OK=false
fi

# ===================================================================
#  4. HOBBYCAD HOMEBREW TAP + DEPENDENCIES
# ===================================================================

header "4/7  HobbyCAD dependencies"

if [ -x "$BREW" ]; then

    # Tap
    if "$BREW" tap | grep -q "^ayourk/hobbycad$"; then
        ok "Tap ayourk/hobbycad already added."
    else
        if confirm "Add the HobbyCAD Homebrew tap?"; then
            info "Running: brew tap ayourk/hobbycad"
            "$BREW" tap ayourk/hobbycad
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
        "librsvg"
    )

    PINNED_MISSING=()
    for formula in "${PINNED_FORMULAS[@]}"; do
        short=$(echo "$formula" | sed 's|.*/||')
        if ! "$BREW" list "$formula" &>/dev/null 2>&1; then
            PINNED_MISSING+=("$formula")
        else
            ok "$short installed."
        fi
    done

    CORE_MISSING=()
    for formula in "${CORE_FORMULAS[@]}"; do
        if ! "$BREW" list "$formula" &>/dev/null 2>&1; then
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
            "$BREW" install "${PINNED_MISSING[@]}"
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
            "$BREW" install "${CORE_MISSING[@]}"
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
    info "Homebrew not available -- skipping dependencies."
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

    # Re-resolve git in case step 3 installed it
    if [ -z "$GIT_EXE" ]; then
        GIT_EXE=$(resolve_exe "git" "/usr/bin/git") || true
    fi

    if [ -n "$GIT_EXE" ]; then
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

            # Verify the parent directory exists; offer to
            # create it (and any missing intermediate dirs).
            if [ ! -d "$TARGET_PARENT" ]; then
                if confirm "$TARGET_PARENT does not exist. Create it?"; then
                    mkdir -p "$TARGET_PARENT"
                    if [ -d "$TARGET_PARENT" ]; then
                        ok "Created $TARGET_PARENT"
                    else
                        fail "Could not create $TARGET_PARENT"
                        ALL_OK=false
                    fi
                else
                    info "Skipped clone."
                    ALL_OK=false
                fi
            fi

            if [ ! -d "$TARGET_PARENT" ]; then
                : # Creation failed or was skipped -- skip clone
            elif [ -d "$TARGET_DIR/.git" ]; then
                ok "Repository already exists at $TARGET_DIR"
                CLONE_PATH="$TARGET_DIR"
            else
                info "Cloning to $TARGET_DIR..."
                if "$GIT_EXE" clone "$REPO_URL" "$TARGET_DIR"; then
                    ok "Cloned to $TARGET_DIR"
                    CLONE_PATH="$TARGET_DIR"
                else
                    fail "git clone failed."
                    info "Clone manually:"
                    info "  git clone $REPO_URL"
                    ALL_OK=false
                fi
            fi

            # Persist the clone path so --uninstall can find it
            if [ -n "$CLONE_PATH" ]; then
                detect_shell_rc
                if [ -n "$SHELL_RC" ]; then
                    remove_shell_export \
                        "HobbyCAD -- clone path" \
                        "HOBBYCAD_REPO"
                    write_shell_export \
                        "HobbyCAD -- clone path" \
                        "HOBBYCAD_REPO" \
                        "$CLONE_PATH"
                    export HOBBYCAD_REPO="$CLONE_PATH"
                    info "Saved clone path to $SHELL_RC"
                    info "(\$HOBBYCAD_REPO=$CLONE_PATH)"
                fi
            fi
        else
            info "Clone manually when ready:"
            info "  git clone $REPO_URL"
        fi
    else
        warn "Git not available yet -- cannot clone."
        info "Install Xcode CLT first (step 1), then re-run."
        ALL_OK=false
    fi
fi

# ===================================================================
#  6. CMAKE_PREFIX_PATH
# ===================================================================

header "6/7  CMAKE_PREFIX_PATH"

if [ -x "$BREW" ]; then
    # Build the expected value
    OCCT_PFX="$("$BREW" --prefix \
        ayourk/hobbycad/opencascade@7.6.3 2>/dev/null || true)"
    LZIP_PFX="$("$BREW" --prefix \
        ayourk/hobbycad/libzip@1.7.3 2>/dev/null || true)"
    LGIT_PFX="$("$BREW" --prefix \
        ayourk/hobbycad/libgit2@1.7.2 2>/dev/null || true)"
    QT6_PFX="$("$BREW" --prefix qt@6 2>/dev/null || true)"

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

        # Detect shell config file
        detect_shell_rc
        LOGIN_SHELL=$(basename "${SHELL:-/bin/zsh}")

        if [ -n "$SHELL_RC" ]; then
            info "Add to $SHELL_RC:"
        else
            info "Add to your shell config:"
        fi
        info ""
        if [ "$LOGIN_SHELL" = "fish" ]; then
            info "  set -gx CMAKE_PREFIX_PATH \\"
            info "    \"$EXPECTED_PATH\""
        else
            info "  export CMAKE_PREFIX_PATH=\"\\"
            info "  $EXPECTED_PATH\""
        fi
        info ""

        if [ -n "$SHELL_RC" ] &&
           confirm "Append CMAKE_PREFIX_PATH to $SHELL_RC?"; then
            write_shell_export_block \
                "HobbyCAD -- keg-only formula paths" \
                "CMAKE_PREFIX_PATH" \
                "$EXPECTED_PATH"
            export CMAKE_PREFIX_PATH="$EXPECTED_PATH"
            ok "Appended to $SHELL_RC and set for this session."
            info "New terminal windows will inherit this."
        else
            if [ -z "$SHELL_RC" ]; then
                info "No shell config file found."
                info "Create one and add the export line above."
            fi
        fi
    fi
else
    info "Homebrew not available -- skipping CMAKE_PREFIX_PATH."
    ALL_OK=false
fi

# ===================================================================
#  7. SUMMARY
# ===================================================================

header "7/7  Summary"

# Final tool verification using resolved paths
CLANGXX_EXE=$(resolve_exe "clang++" "/usr/bin/clang++") || true
if [ -n "$CLANGXX_EXE" ]; then
    ok "clang++ : $("$CLANGXX_EXE" --version 2>&1 | head -1)"
fi
if [ -n "$CMAKE_EXE" ]; then
    ok "cmake   : $("$CMAKE_EXE" --version | head -1)"
fi
if [ -n "$NINJA_EXE" ]; then
    ok "ninja   : $("$NINJA_EXE" --version)"
fi
if [ -n "$GIT_EXE" ]; then
    ok "git     : $("$GIT_EXE" --version)"
fi
PYTHON_EXE=$(resolve_exe "python3" \
    "$BREW_PREFIX/bin/python3") || true
if [ -n "$PYTHON_EXE" ]; then
    ok "python  : $("$PYTHON_EXE" --version)"
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
    info "       cmake --preset macos-debug"
    info "       cmake --build --preset macos-debug -j$NCPU"
    info ""
    info "     Or use the build script:"
    info "       ./tools/macos/build-dev.sh"
    STEP=$((STEP + 1))

    info ""
    info "  $STEP. Run:"
    info "       ./build/src/hobbycad/hobbycad"
else
    fail "Some items need attention -- see above."
    echo ""
    info "Fix the issues, then run this script again."
fi

echo ""
