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
    Generates Linux hicolor PNGs (16-512px), Windows .ico
    (via icotool or ImageMagick), and macOS .icns (via iconutil).
    Requires rsvg-convert (librsvg2-bin).

    Usage:
      include(cmake/GenerateIcons.cmake)
      generate_icons(
          SVG    ${CMAKE_SOURCE_DIR}/resources/icons/hobbycad.svg
          OUTPUT ${CMAKE_BINARY_DIR}/icons
          NAME   hobbycad
      )

