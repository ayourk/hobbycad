=====================================================================
  tools/linux/ — Linux AppImage Packaging
=====================================================================

  BUILD AN APPIMAGE
  ------------------

  The build-appimage.sh script automates the full pipeline:

    ./tools/linux/build-appimage.sh

  It performs the following steps:

    1. Downloads linuxdeploy and its Qt plugin (if not present)
    2. Configures a Release build with /usr prefix
    3. Compiles HobbyCAD
    4. Validates build output (binary exists)
    5. Installs to a staging directory (AppDir)
    6. Validates AppDir (.desktop file, icon present)
    7. Runs linuxdeploy to bundle dependencies
    8. Produces HobbyCAD-x86_64.AppImage

  All output is logged to build-appimage.log in the project root,
  following the same convention as devtest.log: timestamp,
  environment info, then output from each phase.


  MANUAL BUILD STEPS
  -------------------

  The steps below can be run manually instead of using the script.

  1. Configure a Release build with /usr prefix (required for
     AppImage directory structure):

       cmake -B build-appimage -G Ninja \
         -DCMAKE_BUILD_TYPE=Release \
         -DCMAKE_INSTALL_PREFIX=/usr

  2. Build:

       cmake --build build-appimage -j$(nproc)

  3. Install into a staging directory (AppDir):

       DESTDIR=$(pwd)/AppDir cmake --install build-appimage

  4. Run linuxdeploy to bundle all dependencies and create the
     AppImage:

       export LDAI_UPDATE_INFORMATION="gh-releases-zsync|ayourk|HobbyCAD|latest|HobbyCAD-*x86_64.AppImage.zsync"
       ./tools/linux/linuxdeploy-x86_64.AppImage \
         --appdir AppDir \
         --plugin qt \
         --output appimage

  5. Test the AppImage:

       chmod +x HobbyCAD-x86_64.AppImage
       ./HobbyCAD-x86_64.AppImage

  6. Clean up build artifacts:

       rm -rf build-appimage AppDir


  APPDIR LAYOUT
  ---------------

  The cmake --install step (step 3) creates this layout, which
  linuxdeploy requires:

    AppDir/
      usr/
        bin/
          hobbycad
        lib/
          (shared libraries installed by the project)
        share/
          applications/
            hobbycad.desktop
          icons/
            hicolor/
              256x256/apps/hobbycad.png
              scalable/apps/hobbycad.svg

  The .desktop file and icon are required.  linuxdeploy will fail
  without them.  These are installed by the project's CMakeLists.txt
  via install() rules.


  KEY POINTS
  -----------

  - The Qt plugin automatically bundles Qt libraries, plugins,
    and platform themes
  - OCCT libraries (TK*.so) are bundled automatically via ldd
    resolution
  - PPA libraries (libslvs, libopenmesh, lib3mf, libmeshfix) are
    bundled automatically if they are installed system-wide or
    in the AppDir staging tree
  - The LDAI_UPDATE_INFORMATION variable enables delta updates
    via AppImageUpdate (optional but recommended for releases)
  - Test on a clean system or container to verify all dependencies
    are bundled:
      docker run --rm -v $(pwd):/app ubuntu:22.04 \
        /app/HobbyCAD-x86_64.AppImage --help


  TROUBLESHOOTING
  -----------------

  linuxdeploy fails:
    - Verify .desktop file and icon are in the AppDir
    - Check that the binary was built and installed correctly
    - If FUSE is not available, try:
        export APPIMAGE_EXTRACT_AND_RUN=1
    - Review linuxdeploy output in build-appimage.log

  Configure or build fails:
    - Run devtest to verify dependencies:
        cd devtest && cmake -B build && cmake --build build
        ./build/depcheck
    - See dev_environment_setup.txt Section 12 for general
      troubleshooting

  AppImage won't start on another system:
    - The target system's glibc may be too old
    - To find the minimum glibc version the AppImage requires,
      extract it and scan for the highest GLIBC symbol referenced:

        ./HobbyCAD-x86_64.AppImage --appimage-extract
        find squashfs-root -name '*.so' -o -name '*.so.*' \
          -o -type f -executable \
          | xargs strings 2>/dev/null \
          | grep '^GLIBC_' \
          | sed 's/GLIBC_//' \
          | sort --version-sort | uniq | tail -n 1

      The output (e.g., 2.35) is the minimum glibc the host needs.
      Compare with the target system:  ldd --version
    - Build on the oldest supported Ubuntu (22.04) for maximum
      compatibility
    - Test in a container:
        docker run --rm -v $(pwd):/app ubuntu:22.04 \
          /app/HobbyCAD-x86_64.AppImage --help


  LINUXDEPLOY TOOLS
  -------------------

  Downloaded automatically by build-appimage.sh into this
  directory.  To download manually:

  linuxdeploy-x86_64.AppImage

    AppDir creation and dependency bundling tool.

    wget -O tools/linux/linuxdeploy-x86_64.AppImage \
      https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
    chmod +x tools/linux/linuxdeploy-x86_64.AppImage

  linuxdeploy-plugin-qt-x86_64.AppImage

    Qt plugin for linuxdeploy.  Bundles Qt libraries, plugins,
    and platform themes into the AppImage.

    wget -O tools/linux/linuxdeploy-plugin-qt-x86_64.AppImage \
      https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage
    chmod +x tools/linux/linuxdeploy-plugin-qt-x86_64.AppImage

