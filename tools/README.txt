================================================================================
  tools/ -- Build and Packaging Tools
================================================================================

  Platform-specific build scripts, environment setup scripts, and
  packaging helpers.

  Subdirectories
  ---------------

    linux/      Build scripts for Ubuntu 22.04 / 24.04 LTS
                  build-dev.sh        Developer build with logging
                  build-deb.sh        Debian package (.deb) build
                  build-appimage.sh   AppImage portable build

    windows/    Environment setup and build scripts for Windows 10+
                  setup-env.ps1       Automated MSYS2/vcpkg setup (PowerShell)
                  build-dev.bat       Developer build with logging

    macos/      Environment setup and build scripts for macOS 12+
                  setup-env.sh        Automated Homebrew setup (bash/zsh)
                  setup-env.csh       Automated Homebrew setup (tcsh/csh)
                  build-dev.sh        Developer build with logging

  See docs/dev_environment_setup.txt for full platform instructions.
