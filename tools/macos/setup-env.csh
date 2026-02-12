#!/bin/tcsh -f
# =====================================================================
#  tools/macos/setup-env.csh -- macOS development environment setup
# =====================================================================
#
#  csh/tcsh version of setup-env.sh.  Checks for required tools and
#  offers to install anything missing.  Handles Xcode Command Line
#  Tools, Homebrew, build tools, the HobbyCAD Homebrew tap, pinned
#  dependencies, repository clone, and CMAKE_PREFIX_PATH.
#
#  Can be run from inside a git clone or downloaded standalone.
#  When run standalone, it offers to clone the repository after
#  the toolchain is set up.
#
#  Run with:
#    tcsh tools/macos/setup-env.csh
#
#  AI Attribution: This script was primarily generated with the
#                  assistance of Claude AI (Anthropic).  Human review
#                  and editorial direction by the project author.
#
#  SPDX-License-Identifier: GPL-3.0-only
#
# =====================================================================

# --- Color codes ------------------------------------------------------

set RED    = "%{\033[0;31m%}"
set GREEN  = "%{\033[0;32m%}"
set YELLOW = "%{\033[0;33m%}"
set CYAN   = "%{\033[0;36m%}"
set GRAY   = "%{\033[0;37m%}"
set WHITE  = "%{\033[1;37m%}"
set NC     = "%{\033[0m%}"

# tcsh printf handles escape sequences reliably.
# Define output aliases.
alias header   'echo "" ; printf "\033[0;36m%s\033[0m\n" \
    "============================================================" ; \
    printf "\033[0;36m  %s\033[0m\n" "\!:1" ; \
    printf "\033[0;36m%s\033[0m\n" \
    "============================================================"'
alias ok       'printf "  \033[0;32m[OK]\033[0m   %s\n" "\!:*"'
alias warn_msg 'printf "  \033[0;33m[WARN]\033[0m %s\n" "\!:*"'
alias fail_msg 'printf "  \033[0;31m[FAIL]\033[0m %s\n" "\!:*"'
alias info_msg 'printf "  \033[0;37m[INFO]\033[0m %s\n" "\!:*"'

# --- Defaults ---------------------------------------------------------

set NON_INTERACTIVE = 0
set DO_UNINSTALL    = 0
set REPO_URL  = "https://github.com/ayourk/hobbycad.git"
set CLONE_DIR = ""
set ALL_OK    = 1
set CLONE_PATH = ""

# --- Parse arguments --------------------------------------------------

set idx = 1
while ($idx <= $#argv)
    switch ("$argv[$idx]")
        case "--yes":
        case "-y":
            set NON_INTERACTIVE = 1
            breaksw
        case "--uninstall":
            set DO_UNINSTALL = 1
            breaksw
        case "--repo-url="*:
            set REPO_URL = `echo "$argv[$idx]" | sed 's/^--repo-url=//'`
            breaksw
        case "--clone-dir="*:
            set CLONE_DIR = `echo "$argv[$idx]" | sed 's/^--clone-dir=//'`
            breaksw
        case "--help":
        case "-h":
            echo "Usage: setup-env.csh [OPTIONS]"
            echo ""
            echo "  --yes, -y           Answer yes to all prompts"
            echo "  --uninstall         Roll back changes"
            echo "  --repo-url=URL      Repository URL"
            echo "  --clone-dir=DIR     Clone target (parent dir)"
            echo "  --help, -h          Show this help"
            exit 0
            breaksw
    endsw
    @ idx++
end

# --- Detect standalone vs. in-repo -----------------------------------

set SCRIPT_DIR = `dirname "$0"`
set SCRIPT_DIR = `cd "$SCRIPT_DIR" && pwd`
set IS_IN_REPO = 0
set REPO_ROOT  = ""

set CANDIDATE_ROOT = `cd "$SCRIPT_DIR/../.." && pwd` >& /dev/null
if ($status == 0 && "$CANDIDATE_ROOT" != "") then
    if (-f "$CANDIDATE_ROOT/CMakeLists.txt") then
        grep -qi "hobbycad" "$CANDIDATE_ROOT/CMakeLists.txt" \
            >& /dev/null
        if ($status == 0) then
            set IS_IN_REPO = 1
            set REPO_ROOT  = "$CANDIDATE_ROOT"
        endif
    endif
endif

# --- Locate Homebrew --------------------------------------------------

set ARCH = `uname -m`
if ("$ARCH" == "arm64") then
    set BREW_PREFIX = "/opt/homebrew"
else
    set BREW_PREFIX = "/usr/local"
endif
set BREW = "$BREW_PREFIX/bin/brew"

# Check PATH first
which brew >& /dev/null
if ($status == 0) then
    set BREW = `which brew`
    set BREW_PREFIX = `"$BREW" --prefix`
else if (! -x "$BREW") then
    set BREW = ""
endif

# --- Detect shell config file -----------------------------------------

set SHELL_RC = ""
if (-f "$HOME/.tcshrc") then
    set SHELL_RC = "$HOME/.tcshrc"
else if (-f "$HOME/.cshrc") then
    set SHELL_RC = "$HOME/.cshrc"
endif

# --- Resolve a tool to its full path ----------------------------------
# Usage:  set result = `resolve_tool cmake $BREW_PREFIX/bin/cmake`
# Cannot use a real function in csh, so this is an alias that
# writes to $resolved_path.  Call it, then read $resolved_path.
#
# Instead we'll inline the logic where needed using a pattern:
#   which $name >& /dev/null
#   if ($status == 0) then
#       set VARNAME = `which $name`
#   else if (-x "$known") then
#       set VARNAME = "$known"
#   else
#       set VARNAME = ""
#   endif

# --- Banner -----------------------------------------------------------

echo ""
printf "\033[1;37m  HobbyCAD macOS Environment Setup (csh)\033[0m\n"
echo "  ---------------------------------------"

if ("$BREW" != "") then
    info_msg "Homebrew:      $BREW"
else
    info_msg "Homebrew:      not found"
endif

if ("$ARCH" == "arm64") then
    info_msg "Architecture:  Apple Silicon (arm64)"
else
    info_msg "Architecture:  Intel (x86_64)"
endif

if ($IS_IN_REPO) then
    info_msg "Repo root:     $REPO_ROOT"
else
    info_msg "Mode:          standalone (not inside a clone)"
endif
echo ""

# ===================================================================
#  UNINSTALL MODE
# ===================================================================

if ($DO_UNINSTALL) then
    header "Uninstall -- rolling back changes"

    set CHANGED = 0

    # --- CMAKE_PREFIX_PATH ---------------------------------------------

    echo ""
    info_msg "Checking CMAKE_PREFIX_PATH in shell config..."

    if ("$SHELL_RC" != "") then
        grep -q "# HobbyCAD -- keg-only formula paths" \
            "$SHELL_RC" >& /dev/null
        if ($status == 0) then
            warn_msg "Found HobbyCAD CMAKE_PREFIX_PATH block in $SHELL_RC"

            if ($NON_INTERACTIVE) then
                set answer = "y"
            else
                printf "  Remove it? (Y/n) "
                set answer = "$<"
            endif
            if ("$answer" == "" || "$answer" == "y" || \
                "$answer" == "Y") then
                sed -i '' \
                    '/# HobbyCAD -- keg-only/,/^setenv CMAKE_PREFIX_PATH/d' \
                    "$SHELL_RC"
                ok "Removed from $SHELL_RC"
                set CHANGED = 1
            else
                info_msg "Skipped."
            endif
        else
            ok "No HobbyCAD CMAKE_PREFIX_PATH block found."
        endif
    else
        ok "No shell config file found."
    endif

    # --- HOBBYCAD_REPO ------------------------------------------------

    echo ""
    info_msg "Checking HOBBYCAD_REPO in shell config..."

    if ("$SHELL_RC" != "") then
        grep -q "# HobbyCAD -- clone path" \
            "$SHELL_RC" >& /dev/null
        if ($status == 0) then
            set SAVED_CLONE = `grep '^setenv HOBBYCAD_REPO' \
                "$SHELL_RC" | head -1 | \
                sed 's/^setenv HOBBYCAD_REPO //' | \
                sed 's/^"//' | sed 's/"$//'`

            warn_msg "Found HOBBYCAD_REPO in $SHELL_RC"

            if ("$SAVED_CLONE" != "" && -d "$SAVED_CLONE") then
                if ($NON_INTERACTIVE) then
                    set answer = "y"
                else
                    printf "  Delete %s? (Y/n) " "$SAVED_CLONE"
                    set answer = "$<"
                endif
                if ("$answer" == "" || "$answer" == "y" || \
                    "$answer" == "Y") then
                    rm -rf "$SAVED_CLONE"
                    if (! -d "$SAVED_CLONE") then
                        ok "Deleted $SAVED_CLONE"
                        set CHANGED = 1
                    else
                        fail_msg "Could not fully remove."
                        info_msg "Delete manually."
                    endif
                else
                    info_msg "Skipped folder deletion."
                endif
            else if ("$SAVED_CLONE" != "") then
                info_msg "Directory already gone."
            endif

            if ($NON_INTERACTIVE) then
                set answer = "y"
            else
                printf "  Remove HOBBYCAD_REPO from %s? (Y/n) " \
                    "$SHELL_RC"
                set answer = "$<"
            endif
            if ("$answer" == "" || "$answer" == "y" || \
                "$answer" == "Y") then
                sed -i '' \
                    '/# HobbyCAD -- clone path/,/^setenv HOBBYCAD_REPO/d' \
                    "$SHELL_RC"
                ok "Removed from $SHELL_RC"
                set CHANGED = 1
            else
                info_msg "Skipped."
            endif
        else
            ok "No HOBBYCAD_REPO block found."
        endif
    else
        ok "No shell config file found."
    endif

    # --- Homebrew tap + formulas ---------------------------------------

    echo ""
    info_msg "Checking for HobbyCAD Homebrew tap..."

    if ("$BREW" != "" && -x "$BREW") then
        "$BREW" tap |& grep -q '^ayourk/hobbycad$'
        if ($status == 0) then
            warn_msg "Tap ayourk/hobbycad is installed."

            if ($NON_INTERACTIVE) then
                set answer = "y"
            else
                printf "  Uninstall tap formulas and remove tap? (Y/n) "
                set answer = "$<"
            endif
            if ("$answer" == "" || "$answer" == "y" || \
                "$answer" == "Y") then
                info_msg "Removing tap formulas..."
                "$BREW" uninstall --force \
                    ayourk/hobbycad/opencascade@7.6.3 \
                    ayourk/hobbycad/libzip@1.7.3 \
                    ayourk/hobbycad/libgit2@1.7.2 \
                    >& /dev/null
                "$BREW" untap ayourk/hobbycad >& /dev/null
                ok "Tap and formulas removed."
                set CHANGED = 1
            else
                info_msg "Skipped."
            endif
        else
            ok "Tap ayourk/hobbycad not found."
        endif

        echo ""
        info_msg "Checking HobbyCAD core formulas..."

        set CORE_INSTALLED = ""
        foreach f (qt@6 librsvg)
            "$BREW" list "$f" >& /dev/null
            if ($status == 0) then
                if ("$CORE_INSTALLED" == "") then
                    set CORE_INSTALLED = "$f"
                else
                    set CORE_INSTALLED = "${CORE_INSTALLED} $f"
                endif
            endif
        end

        if ("$CORE_INSTALLED" != "") then
            warn_msg "Installed: $CORE_INSTALLED"

            if ($NON_INTERACTIVE) then
                set answer = "y"
            else
                printf "  Uninstall these formulas? (Y/n) "
                set answer = "$<"
            endif
            if ("$answer" == "" || "$answer" == "y" || \
                "$answer" == "Y") then
                "$BREW" uninstall --force $CORE_INSTALLED
                ok "Core formulas removed."
                set CHANGED = 1
            else
                info_msg "Skipped."
            endif
        else
            ok "No HobbyCAD core formulas installed."
        endif
    else
        ok "Homebrew not found -- nothing to do."
    endif

    # --- Summary -------------------------------------------------------

    echo ""
    if ($CHANGED) then
        ok "Uninstall complete."
        info_msg "Open a new terminal for changes to take effect."
    else
        info_msg "No changes were made."
    endif
    echo ""
    exit 0
endif

# ===================================================================
#  1. XCODE COMMAND LINE TOOLS
# ===================================================================

header "1/7  Xcode Command Line Tools"

xcode-select -p >& /dev/null
if ($status == 0) then
    ok "Xcode CLT installed."
    set CLT_VER = `clang++ --version |& head -1`
    info_msg "  $CLT_VER"
else
    fail_msg "Xcode Command Line Tools not found."

    if ($NON_INTERACTIVE) then
        set answer = "y"
    else
        printf "  Install Xcode Command Line Tools? (Y/n) "
        set answer = "$<"
    endif
    if ("$answer" == "" || "$answer" == "y" || \
        "$answer" == "Y") then
        info_msg "Running xcode-select --install..."
        info_msg "(A dialog will appear -- click 'Install'.)"
        xcode-select --install >& /dev/null

        info_msg "Waiting for installation to finish..."
        while (1)
            xcode-select -p >& /dev/null
            if ($status == 0) break
            sleep 5
        end
        ok "Xcode Command Line Tools installed."
    else
        info_msg "Install manually: xcode-select --install"
        set ALL_OK = 0
    endif
endif

# ===================================================================
#  2. HOMEBREW
# ===================================================================

header "2/7  Homebrew"

if ("$BREW" != "" && -x "$BREW") then
    set BREW_VER = `"$BREW" --version | head -1`
    ok "Homebrew found: $BREW_VER"

    set ACTUAL_PREFIX = `"$BREW" --prefix`
    if ("$ACTUAL_PREFIX" != "$BREW_PREFIX") then
        warn_msg "Homebrew at $ACTUAL_PREFIX (expected $BREW_PREFIX)."
        info_msg "Rosetta Homebrew? This may work but is untested."
        set BREW_PREFIX = "$ACTUAL_PREFIX"
    endif
else
    fail_msg "Homebrew not found."

    if ($NON_INTERACTIVE) then
        set answer = "y"
    else
        printf "  Install Homebrew? (Y/n) "
        set answer = "$<"
    endif
    if ("$answer" == "" || "$answer" == "y" || \
        "$answer" == "Y") then
        info_msg "Running the Homebrew installer..."
        /bin/bash -c "`curl -fsSL \
            https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh`"

        set BREW = "$BREW_PREFIX/bin/brew"
        if (-x "$BREW") then
            eval `"$BREW" shellenv`
            ok "Homebrew installed."
        else
            fail_msg "Homebrew install did not create $BREW."
            info_msg "Install manually: https://brew.sh/"
            set ALL_OK = 0
        endif
    else
        info_msg "Install manually: https://brew.sh/"
        set ALL_OK = 0
    endif
endif

# ===================================================================
#  3. BUILD TOOLS (cmake, ninja, python)
# ===================================================================

header "3/7  Build tools"

# Resolve tool paths
set CMAKE_EXE = ""
set NINJA_EXE = ""
set GIT_EXE   = ""
set PYTHON_EXE = ""

which cmake >& /dev/null
if ($status == 0) then
    set CMAKE_EXE = `which cmake`
else if (-x "$BREW_PREFIX/bin/cmake") then
    set CMAKE_EXE = "$BREW_PREFIX/bin/cmake"
endif

which ninja >& /dev/null
if ($status == 0) then
    set NINJA_EXE = `which ninja`
else if (-x "$BREW_PREFIX/bin/ninja") then
    set NINJA_EXE = "$BREW_PREFIX/bin/ninja"
endif

which git >& /dev/null
if ($status == 0) then
    set GIT_EXE = `which git`
else if (-x "/usr/bin/git") then
    set GIT_EXE = "/usr/bin/git"
endif

which python3 >& /dev/null
if ($status == 0) then
    set PYTHON_EXE = `which python3`
else if (-x "$BREW_PREFIX/bin/python3") then
    set PYTHON_EXE = "$BREW_PREFIX/bin/python3"
endif

if ("$BREW" != "" && -x "$BREW") then
    set TOOLS_MISSING = ""

    if ("$CMAKE_EXE" == "") then
        set TOOLS_MISSING = "cmake"
    endif
    if ("$NINJA_EXE" == "") then
        if ("$TOOLS_MISSING" == "") then
            set TOOLS_MISSING = "ninja"
        else
            set TOOLS_MISSING = "${TOOLS_MISSING} ninja"
        endif
    endif
    "$BREW" list python >& /dev/null
    if ($status != 0) then
        if ("$TOOLS_MISSING" == "") then
            set TOOLS_MISSING = "python"
        else
            set TOOLS_MISSING = "${TOOLS_MISSING} python"
        endif
    endif

    if ("$TOOLS_MISSING" == "") then
        ok "Build tools installed."
        set cmake_ver = `"$CMAKE_EXE" --version | head -1`
        set ninja_ver = `"$NINJA_EXE" --version`
        info_msg "  cmake : $cmake_ver"
        info_msg "  ninja : $ninja_ver"
        if ("$PYTHON_EXE" != "") then
            set python_ver = `"$PYTHON_EXE" --version`
            info_msg "  python: $python_ver"
        endif
    else
        warn_msg "Missing: $TOOLS_MISSING"

        if ($NON_INTERACTIVE) then
            set answer = "y"
        else
            printf "  Install missing build tools via Homebrew? (Y/n) "
            set answer = "$<"
        endif
        if ("$answer" == "" || "$answer" == "y" || \
            "$answer" == "Y") then
            info_msg "Running: brew install $TOOLS_MISSING"
            "$BREW" install $TOOLS_MISSING
            ok "Build tools installed."

            # Re-resolve after install
            which cmake >& /dev/null
            if ($status == 0) then
                set CMAKE_EXE = `which cmake`
            else if (-x "$BREW_PREFIX/bin/cmake") then
                set CMAKE_EXE = "$BREW_PREFIX/bin/cmake"
            endif
            which ninja >& /dev/null
            if ($status == 0) then
                set NINJA_EXE = `which ninja`
            else if (-x "$BREW_PREFIX/bin/ninja") then
                set NINJA_EXE = "$BREW_PREFIX/bin/ninja"
            endif
        else
            info_msg "Install manually:"
            info_msg "  brew install $TOOLS_MISSING"
            set ALL_OK = 0
        endif
    endif

    # cmake minimum version check
    if ("$CMAKE_EXE" != "") then
        set CMAKE_VER = `"$CMAKE_EXE" --version | head -1 | \
            grep -oE '[0-9]+\.[0-9]+'`
        set CMAKE_MAJOR = `echo "$CMAKE_VER" | cut -d. -f1`
        set CMAKE_MINOR = `echo "$CMAKE_VER" | cut -d. -f2`
        if ($CMAKE_MAJOR < 3 || \
            ($CMAKE_MAJOR == 3 && $CMAKE_MINOR < 20)) then
            warn_msg "CMake 3.20+ required (found $CMAKE_VER)."
            info_msg "  brew upgrade cmake"
            set ALL_OK = 0
        endif
    endif
else
    info_msg "Homebrew not available -- skipping build tools."
    set ALL_OK = 0
endif

# ===================================================================
#  4. HOBBYCAD HOMEBREW TAP + DEPENDENCIES
# ===================================================================

header "4/7  HobbyCAD dependencies"

if ("$BREW" != "" && -x "$BREW") then

    # Tap
    "$BREW" tap |& grep -q '^ayourk/hobbycad$'
    if ($status == 0) then
        ok "Tap ayourk/hobbycad already added."
    else
        if ($NON_INTERACTIVE) then
            set answer = "y"
        else
            printf "  Add the HobbyCAD Homebrew tap? (Y/n) "
            set answer = "$<"
        endif
        if ("$answer" == "" || "$answer" == "y" || \
            "$answer" == "Y") then
            info_msg "Running: brew tap ayourk/hobbycad"
            "$BREW" tap ayourk/hobbycad
            ok "Tap added."
        else
            info_msg "Add manually: brew tap ayourk/hobbycad"
            set ALL_OK = 0
        endif
    endif

    # Pinned keg-only formulas
    set PINNED_MISSING = ""

    foreach formula ( \
        ayourk/hobbycad/opencascade@7.6.3 \
        ayourk/hobbycad/libzip@1.7.3 \
        ayourk/hobbycad/libgit2@1.7.2 \
    )
        set short = `echo "$formula" | sed 's|.*/||'`
        "$BREW" list "$formula" >& /dev/null
        if ($status != 0) then
            if ("$PINNED_MISSING" == "") then
                set PINNED_MISSING = "$formula"
            else
                set PINNED_MISSING = \
                    "${PINNED_MISSING} $formula"
            endif
        else
            ok "$short installed."
        endif
    end

    # Core formulas
    set CORE_MISSING = ""

    foreach formula (qt@6 librsvg)
        "$BREW" list "$formula" >& /dev/null
        if ($status != 0) then
            if ("$CORE_MISSING" == "") then
                set CORE_MISSING = "$formula"
            else
                set CORE_MISSING = \
                    "${CORE_MISSING} $formula"
            endif
        else
            ok "$formula installed."
        endif
    end

    if ("$PINNED_MISSING" != "") then
        warn_msg "Missing pinned formulas: $PINNED_MISSING"

        if ($NON_INTERACTIVE) then
            set answer = "y"
        else
            printf "  Install pinned formulas? (Y/n) "
            set answer = "$<"
        endif
        if ("$answer" == "" || "$answer" == "y" || \
            "$answer" == "Y") then
            info_msg "Installing pinned formulas..."
            "$BREW" install $PINNED_MISSING
            ok "Pinned formulas installed."
        else
            info_msg "Install manually:"
            foreach f ($PINNED_MISSING)
                info_msg "  brew install $f"
            end
            set ALL_OK = 0
        endif
    endif

    if ("$CORE_MISSING" != "") then
        warn_msg "Missing core formulas: $CORE_MISSING"

        if ($NON_INTERACTIVE) then
            set answer = "y"
        else
            printf "  Install core formulas? (Y/n) "
            set answer = "$<"
        endif
        if ("$answer" == "" || "$answer" == "y" || \
            "$answer" == "Y") then
            info_msg "Installing core formulas..."
            "$BREW" install $CORE_MISSING
            ok "Core formulas installed."
        else
            info_msg "Install manually:"
            foreach f ($CORE_MISSING)
                info_msg "  brew install $f"
            end
            set ALL_OK = 0
        endif
    endif

    if ("$PINNED_MISSING" == "" && "$CORE_MISSING" == "") then
        ok "All dependencies installed."
    endif

else
    info_msg "Homebrew not available -- skipping dependencies."
    set ALL_OK = 0
endif

# ===================================================================
#  5. REPOSITORY
# ===================================================================

header "5/7  HobbyCAD Repository"

if ($IS_IN_REPO) then
    ok "Running from inside a clone at $REPO_ROOT"
    set CLONE_PATH = "$REPO_ROOT"
else
    info_msg "Script is running standalone (not inside a clone)."

    # Re-resolve git in case step 3 installed it
    if ("$GIT_EXE" == "") then
        which git >& /dev/null
        if ($status == 0) then
            set GIT_EXE = `which git`
        else if (-x "/usr/bin/git") then
            set GIT_EXE = "/usr/bin/git"
        endif
    endif

    if ("$GIT_EXE" != "") then
        if ($NON_INTERACTIVE) then
            set answer = "y"
        else
            printf "  Clone the HobbyCAD repository? (Y/n) "
            set answer = "$<"
        endif
        if ("$answer" == "" || "$answer" == "y" || \
            "$answer" == "Y") then
            # Determine target directory
            if ("$CLONE_DIR" != "") then
                set TARGET_PARENT = "$CLONE_DIR"
            else if ($NON_INTERACTIVE) then
                set TARGET_PARENT = `pwd`
            else
                set DEFAULT = `pwd`
                printf "  Clone to? [%s/hobbycad] " "$DEFAULT"
                set answer = "$<"
                if ("$answer" == "") then
                    set TARGET_PARENT = "$DEFAULT"
                else
                    set TARGET_PARENT = "$answer"
                endif
            endif

            set TARGET_DIR = "$TARGET_PARENT/hobbycad"

            # Verify the parent directory exists; offer to
            # create it (and any missing intermediate dirs).
            if (! -d "$TARGET_PARENT") then
                if ($NON_INTERACTIVE) then
                    set answer = "y"
                else
                    printf "  %s does not exist. Create it? (Y/n) " \
                        "$TARGET_PARENT"
                    set answer = "$<"
                endif
                if ("$answer" == "" || "$answer" == "y" || \
                    "$answer" == "Y") then
                    mkdir -p "$TARGET_PARENT"
                    if (-d "$TARGET_PARENT") then
                        ok "Created $TARGET_PARENT"
                    else
                        fail_msg "Could not create $TARGET_PARENT"
                        set ALL_OK = 0
                    endif
                else
                    info_msg "Skipped clone."
                    set ALL_OK = 0
                endif
            endif

            if (! -d "$TARGET_PARENT") then
                # Creation failed or was skipped -- skip clone
            else if (-d "$TARGET_DIR/.git") then
                ok "Repository already exists at $TARGET_DIR"
                set CLONE_PATH = "$TARGET_DIR"
            else
                info_msg "Cloning to $TARGET_DIR..."
                "$GIT_EXE" clone "$REPO_URL" "$TARGET_DIR"
                if ($status == 0) then
                    ok "Cloned to $TARGET_DIR"
                    set CLONE_PATH = "$TARGET_DIR"
                else
                    fail_msg "git clone failed."
                    info_msg "Clone manually:"
                    info_msg "  git clone $REPO_URL"
                    set ALL_OK = 0
                endif
            endif

            # Persist clone path so --uninstall can find it
            if ("$CLONE_PATH" != "" && "$SHELL_RC" != "") then
                # Remove any existing block first
                sed -i '' \
                    '/# HobbyCAD -- clone path/,/^setenv HOBBYCAD_REPO/d' \
                    "$SHELL_RC" >& /dev/null
                echo "" >> "$SHELL_RC"
                echo "# HobbyCAD -- clone path" >> "$SHELL_RC"
                echo "setenv HOBBYCAD_REPO $CLONE_PATH" \
                    >> "$SHELL_RC"
                setenv HOBBYCAD_REPO "$CLONE_PATH"
                info_msg "Saved clone path to $SHELL_RC"
                info_msg '($HOBBYCAD_REPO='"$CLONE_PATH"')'
            endif
        else
            info_msg "Clone manually when ready:"
            info_msg "  git clone $REPO_URL"
        endif
    else
        warn_msg "Git not available yet -- cannot clone."
        info_msg "Install Xcode CLT first (step 1), then re-run."
        set ALL_OK = 0
    endif
endif

# ===================================================================
#  6. CMAKE_PREFIX_PATH
# ===================================================================

header "6/7  CMAKE_PREFIX_PATH"

if ("$BREW" != "" && -x "$BREW") then
    set OCCT_PFX = `"$BREW" --prefix \
        ayourk/hobbycad/opencascade@7.6.3 >& /dev/null \
        && "$BREW" --prefix \
        ayourk/hobbycad/opencascade@7.6.3`
    set LZIP_PFX = `"$BREW" --prefix \
        ayourk/hobbycad/libzip@1.7.3 >& /dev/null \
        && "$BREW" --prefix \
        ayourk/hobbycad/libzip@1.7.3`
    set LGIT_PFX = `"$BREW" --prefix \
        ayourk/hobbycad/libgit2@1.7.2 >& /dev/null \
        && "$BREW" --prefix \
        ayourk/hobbycad/libgit2@1.7.2`
    set QT6_PFX  = `"$BREW" --prefix qt@6 >& /dev/null \
        && "$BREW" --prefix qt@6`

    set EXPECTED_PATH = ""
    foreach pfx ("$OCCT_PFX" "$LZIP_PFX" "$LGIT_PFX" "$QT6_PFX")
        if ("$pfx" != "" && -d "$pfx") then
            if ("$EXPECTED_PATH" == "") then
                set EXPECTED_PATH = "$pfx"
            else
                set EXPECTED_PATH = "${EXPECTED_PATH}:$pfx"
            endif
        endif
    end

    if ("$EXPECTED_PATH" == "") then
        warn_msg "Cannot determine CMAKE_PREFIX_PATH (deps not installed)."
        set ALL_OK = 0
    else if ($?CMAKE_PREFIX_PATH) then
        # Check if all prefixes are present
        set MISSING_PFX = 0
        foreach pfx ("$OCCT_PFX" "$LZIP_PFX" \
                      "$LGIT_PFX" "$QT6_PFX")
            if ("$pfx" != "" && -d "$pfx") then
                echo "$CMAKE_PREFIX_PATH" | \
                    grep -q "$pfx"
                if ($status != 0) then
                    set MISSING_PFX = 1
                endif
            endif
        end
        if ($MISSING_PFX) then
            warn_msg "CMAKE_PREFIX_PATH is set but incomplete."
            info_msg "Expected:"
            info_msg "  $EXPECTED_PATH"
        else
            ok "CMAKE_PREFIX_PATH is set and looks correct."
        endif
    else
        warn_msg "CMAKE_PREFIX_PATH is not set."
        info_msg "The pinned keg-only formulas require it."
        echo ""

        if ("$SHELL_RC" != "") then
            info_msg "Add to ${SHELL_RC}:"
        else
            info_msg "Add to your shell config:"
        endif
        echo ""
        info_msg "  setenv CMAKE_PREFIX_PATH \\"
        info_msg "    $EXPECTED_PATH"
        echo ""

        if ("$SHELL_RC" != "") then
            if ($NON_INTERACTIVE) then
                set answer = "y"
            else
                printf "  Append CMAKE_PREFIX_PATH to %s? (Y/n) " \
                    "$SHELL_RC"
                set answer = "$<"
            endif
            if ("$answer" == "" || "$answer" == "y" || \
                "$answer" == "Y") then
                echo "" >> "$SHELL_RC"
                echo "# HobbyCAD -- keg-only formula paths" \
                    >> "$SHELL_RC"
                echo "setenv CMAKE_PREFIX_PATH \\" \
                    >> "$SHELL_RC"
                echo "    $EXPECTED_PATH" >> "$SHELL_RC"
                setenv CMAKE_PREFIX_PATH "$EXPECTED_PATH"
                ok "Appended to $SHELL_RC and set for this session."
                info_msg "New terminal windows will inherit this."
            endif
        else
            info_msg "No shell config file found."
            info_msg "Create ~/.tcshrc and add the setenv line above."
        endif
    endif
else
    info_msg "Homebrew not available -- skipping CMAKE_PREFIX_PATH."
    set ALL_OK = 0
endif

# ===================================================================
#  7. SUMMARY
# ===================================================================

header "7/7  Summary"

# Final tool verification using resolved paths
which clang++ >& /dev/null
if ($status == 0) then
    set clangxx_ver = `clang++ --version |& head -1`
    ok "clang++ : $clangxx_ver"
else if (-x "/usr/bin/clang++") then
    set clangxx_ver = `/usr/bin/clang++ --version |& head -1`
    ok "clang++ : $clangxx_ver"
endif

if ("$CMAKE_EXE" != "") then
    set cmake_ver = `"$CMAKE_EXE" --version | head -1`
    ok "cmake   : $cmake_ver"
endif

if ("$NINJA_EXE" != "") then
    set ninja_ver = `"$NINJA_EXE" --version`
    ok "ninja   : $ninja_ver"
endif

if ("$GIT_EXE" != "") then
    set git_ver = `"$GIT_EXE" --version`
    ok "git     : $git_ver"
endif

if ("$PYTHON_EXE" == "") then
    which python3 >& /dev/null
    if ($status == 0) then
        set PYTHON_EXE = `which python3`
    else if (-x "$BREW_PREFIX/bin/python3") then
        set PYTHON_EXE = "$BREW_PREFIX/bin/python3"
    endif
endif
if ("$PYTHON_EXE" != "") then
    set python_ver = `"$PYTHON_EXE" --version`
    ok "python  : $python_ver"
endif

echo ""

set NCPU = `sysctl -n hw.ncpu`
if ($status != 0) set NCPU = 4

if ($ALL_OK) then
    ok "Environment is ready."
    echo ""
    info_msg "Next steps:"

    set STEP = 1

    if ("$CLONE_PATH" == "") then
        info_msg "  ${STEP}. Clone the repo:"
        info_msg "       git clone $REPO_URL hobbycad"
        info_msg "       cd hobbycad"
        @ STEP++
    else
        info_msg "  Repository: $CLONE_PATH"
    endif

    echo ""
    info_msg "  ${STEP}. Verify dependencies:"
    if ("$CLONE_PATH" != "") then
        info_msg "       cd $CLONE_PATH/devtest"
    else
        info_msg "       cd devtest"
    endif
    info_msg "       cmake -B build"
    info_msg "       cmake --build build -j$NCPU"
    info_msg "       ./build/depcheck"
    @ STEP++

    echo ""
    info_msg "  ${STEP}. Build HobbyCAD:"
    if ("$CLONE_PATH" != "") then
        info_msg "       cd $CLONE_PATH"
    else
        info_msg "       cd .."
    endif
    info_msg "       cmake --preset macos-debug"
    info_msg "       cmake --build --preset macos-debug -j$NCPU"
    echo ""
    info_msg "     Or use the build script:"
    info_msg "       ./tools/macos/build-dev.sh"
    @ STEP++

    echo ""
    info_msg "  ${STEP}. Run:"
    info_msg "       ./build/src/hobbycad/hobbycad"
else
    fail_msg "Some items need attention -- see above."
    echo ""
    info_msg "Fix the issues, then run this script again."
endif

echo ""
