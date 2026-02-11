================================================================================
  tools/linux/ -- Linux Build and Packaging Scripts
================================================================================

  Scripts for building HobbyCAD on Ubuntu 22.04 / 24.04 LTS.

  Prerequisites
  ---------------

  Install dependencies via apt (see docs/dev_environment_setup.txt
  Sections 4-5 for per-package details, or the one-line install):

    Ubuntu 24.04:  Section 4.10
    Ubuntu 22.04:  Section 5.6

  Scripts
  --------

  build-dev.sh        Developer build with logging.
                      Uses CMake + Ninja. Output logged to
                      build-hobbycad.log in the project root.

      Usage:
        ./tools/linux/build-dev.sh                   # Debug build
        ./tools/linux/build-dev.sh release           # Release build
        ./tools/linux/build-dev.sh clean             # Clean only
        ./tools/linux/build-dev.sh clean debug       # Clean + Debug
        ./tools/linux/build-dev.sh run               # Run (build if needed)
        ./tools/linux/build-dev.sh clean release run # Clean + Release + Run

  build-deb.sh        Build a Debian package (.deb).
                      Runs devtest, configures, builds, then
                      packages via dpkg-buildpackage.

  build-appimage.sh   Build an AppImage for portable distribution.
                      Produces a self-contained executable that
                      runs on most Linux distributions.

  See docs/dev_environment_setup.txt Section 11 for full build details.
