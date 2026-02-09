=====================================================================
  tools/ — Build and Packaging Tools
=====================================================================

  Platform-specific build scripts and packaging tools are
  organized into subdirectories:

    linux/      AppImage packaging (linuxdeploy)
    windows/    Windows installer packaging (future)
    macos/      macOS .dmg packaging (future)

  Each subdirectory contains its own README.txt with usage
  instructions, manual build steps, and troubleshooting notes.

  Downloaded third-party binaries (linuxdeploy, etc.) are stored
  alongside the scripts that use them and are not checked into
  version control (see .gitignore).

