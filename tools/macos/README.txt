================================================================================
  tools/macos/ -- macOS Environment Setup and Build Scripts
================================================================================

  Scripts for setting up a macOS development environment and
  building HobbyCAD on macOS 12 (Monterey) or later, Intel or
  Apple Silicon.

  Environment Setup
  -------------------

  Two setup scripts are provided -- choose the one matching your
  shell:

  setup-env.sh        Interactive bash script for bash, zsh, and
                      other POSIX-compatible shells.

  setup-env.csh       Interactive csh/tcsh script for C shell users.

  Both scripts perform the same 7 steps:

      1. Check prerequisites (macOS version, disk space)
      2. Install Xcode Command Line Tools (if not present)
      3. Install Homebrew (if not present)
      4. Install build tools (cmake, ninja, pkg-config)
      5. Offer to clone the HobbyCAD repository
      6. Install dependencies from the HobbyCAD Homebrew tap
         (pinned opencascade, libzip, libgit2 formulas)
      7. Print summary with verification commands

      Usage:
        bash  tools/macos/setup-env.sh                # Install
        bash  tools/macos/setup-env.sh --uninstall    # Roll back

        tcsh  tools/macos/setup-env.csh               # Install
        tcsh  tools/macos/setup-env.csh --uninstall   # Roll back

      Parameters:
        --repo-url <url>    Override the default repository URL
        --clone-dir <path>  Override the default clone directory

      Shell environment changes:
        CMAKE_PREFIX_PATH   Set in shell rc file (~/.zshrc, etc.)
        HOBBYCAD_CLONE      Points to the cloned repository

      The setup-env.sh script auto-detects the active shell
      (bash, zsh, fish, ksh) and writes exports to the correct
      rc file.  The setup-env.csh script uses setenv syntax for
      tcsh/csh.

  Build Scripts
  ---------------

  build-dev.sh        Developer build with logging.
                      Uses CMake + Ninja. Output logged to
                      build-hobbycad.log in the project root.

      Usage:
        ./tools/macos/build-dev.sh                   # Debug build
        ./tools/macos/build-dev.sh release           # Release build
        ./tools/macos/build-dev.sh clean             # Clean only
        ./tools/macos/build-dev.sh run               # Run (build if needed)
        ./tools/macos/build-dev.sh clean release run # Clean + Release + Run

  See docs/dev_environment_setup.txt Sections 20-26 for full
  macOS development details.
