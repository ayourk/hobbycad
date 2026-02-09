=====================================================================
  tools/ — Build and Packaging Tools
=====================================================================

  This directory holds third-party tools used for building and
  packaging HobbyCAD.  These files are not checked into version
  control (see .gitignore).

  Download the following tools into this directory:


  linuxdeploy-x86_64.AppImage
  --------------------------------

    AppDir creation and dependency bundling tool.

    Download:
      https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage

    After downloading:
      chmod +x linuxdeploy-x86_64.AppImage


  linuxdeploy-plugin-qt-x86_64.AppImage
  ----------------------------------------

    Qt plugin for linuxdeploy.  Automatically bundles Qt libraries,
    plugins, and platform themes into the AppImage.

    Download:
      https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage

    After downloading:
      chmod +x linuxdeploy-plugin-qt-x86_64.AppImage


  Usage
  -----

  See dev_environment_setup.txt Section 11.4 for full AppImage
  build instructions.
