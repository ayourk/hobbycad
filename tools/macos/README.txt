=====================================================================
  tools/macos/README.txt — macOS Build and Packaging Tools
=====================================================================

  ENVIRONMENT SETUP
  ------------------

  setup-env.sh checks for required tools and offers to install
  anything missing.  It handles Xcode Command Line Tools, Homebrew,
  build tools, the HobbyCAD tap, pinned dependencies, and
  CMAKE_PREFIX_PATH configuration.

    bash tools/macos/setup-env.sh

  Run this first on a fresh Mac.  See dev_environment_setup.txt
  Section 21 for manual setup details.


  DEVELOPER BUILD
  ----------------

  build-dev.sh configures and builds HobbyCAD for day-to-day
  development with logging.  See dev_environment_setup.txt
  Section 11.2 for usage and examples.

    ./tools/macos/build-dev.sh [debug|release] [clean] [run]


  .DMG PACKAGING
  ---------------

  macOS packaging tools and scripts will be added here as
  macOS support progresses.  See dev_environment_setup.txt
  Part C (Sections 20–26) for the current macOS build setup.

