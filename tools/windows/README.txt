=====================================================================
  tools/windows/README.txt — Windows Build and Packaging Tools
=====================================================================

  ENVIRONMENT SETUP
  ------------------

  setup-env.ps1 checks for required tools and offers to install
  anything missing.  It handles MSYS2 download/install, UCRT64
  toolchain packages, PATH configuration, and vcpkg bootstrap.

    powershell -ExecutionPolicy Bypass -File tools\windows\setup-env.ps1

  Run this first on a fresh Windows machine.  See
  dev_environment_setup.txt Section 14 for manual setup details.


  DEVELOPER BUILD
  ----------------

  build-dev.bat configures and builds HobbyCAD for day-to-day
  development with logging.  See dev_environment_setup.txt
  Section 11.2 for usage and examples.

    tools\windows\build-dev.bat [debug|release] [clean] [run]


  INSTALLER PACKAGING
  --------------------

  Windows packaging tools and scripts will be added here as
  Windows support progresses.  See dev_environment_setup.txt
  Part B (Sections 13–19) for the current Windows build setup.

