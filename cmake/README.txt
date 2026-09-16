=====================================================================
  cmake/README.txt — Custom CMake Modules
=====================================================================

  Custom Find modules and utility scripts for the CMake build
  system.  Used when a dependency does not ship its own CMake
  config files.

  MODULES
  -------

  GenerateIcons.cmake
    Build-time icon generation from the canonical SVG source.
    Generates Linux hicolor PNGs (16-512px) and the 32x32 Debian menu
    XPM, the Windows .ico, and the macOS .icns (via Apple's iconutil).
    The images are rendered by tools/render-icons.cpp, built with the
    Qt the application already needs; no rsvg-convert, icotool or
    ImageMagick is involved.

    Usage:
      include(cmake/GenerateIcons.cmake)
      generate_icons(
          SVG    ${CMAKE_SOURCE_DIR}/resources/icons/hobbycad.svg
          OUTPUT ${CMAKE_BINARY_DIR}/icons
          NAME   hobbycad
      )

