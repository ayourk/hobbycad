================================================================================
  tools/windows/ -- Windows Environment Setup and Build Scripts
================================================================================

  Scripts for setting up a Windows development environment and
  building HobbyCAD on Windows 10 version 1809 or later (64-bit).

  Environment Setup
  -------------------

  setup-env.ps1       Interactive PowerShell script that automates
                      the full MSYS2 + UCRT64 development environment.

      What it does (7 steps):
        1. Check administrator privileges
        2. Install MSYS2 (if not present)
        3. Install UCRT64 toolchain packages via pacman
        4. Configure PATH and environment variables
        5. Offer to clone the HobbyCAD repository
        6. Bootstrap vcpkg
        7. Print summary with verification commands

      Usage:
        .\setup-env.ps1                   # Install everything
        .\setup-env.ps1 -Uninstall        # Roll back all changes

      Parameters:
        -RepoUrl <url>    Override the default repository URL
        -CloneDir <path>  Override the default clone directory

      The script detects whether it is running from inside the
      HobbyCAD repository (standalone vs in-repo mode) and
      adjusts its behavior accordingly.

      Registry changes (HKCU\Environment):
        - PATH            Adds MSYS2 ucrt64/bin and usr/bin
        - VCPKG_ROOT      Points to the vcpkg installation
        - HOBBYCAD_REPO  Points to the cloned repository (if cloned)

      All registry values use REG_EXPAND_SZ to preserve
      references like %USERPROFILE%.

  Build Scripts
  ---------------

  build-dev.bat       Developer build with logging.
                      Uses CMake + Ninja. Output logged to
                      build-hobbycad.log in the project root.

  Continuous Integration
  -----------------------

  The project CI (.github/workflows/windows-build.yml) runs two
  parallel jobs:

    "HobbyCAD via MSYS"   MSYS2 UCRT64 / GCC / pacman packages.
                           Matches the local setup from setup-env.ps1.

    "HobbyCAD via MSVC"   Visual Studio / cl.exe / vcpkg manifest.
                           Tests MSVC compatibility with pinned
                           dependency versions from vcpkg.json.

  See docs/dev_environment_setup.txt Sections 13-19 for full
  Windows development details.
