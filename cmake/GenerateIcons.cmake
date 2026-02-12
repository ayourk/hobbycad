# =====================================================================
#  cmake/GenerateIcons.cmake — Build-time icon generation from SVG
# =====================================================================
#
#  Generates platform-specific icon files from the canonical SVG:
#
#    Linux:   PNG files at hicolor theme sizes (16–512)
#    Windows: Multi-resolution .ico (16–256)
#    macOS:   .icns via iconutil (16–1024)
#
#  Requires: rsvg-convert (from librsvg2-bin on Ubuntu)
#  Optional: icotool (from icoutils, for .ico generation)
#            iconutil (macOS only, ships with Xcode)
#            ImageMagick magick (fallback for .ico; avoid "convert"
#            which collides with Windows system utility)
#
#  Usage in CMakeLists.txt:
#    include(cmake/GenerateIcons.cmake)
#    generate_icons(
#        SVG    ${CMAKE_SOURCE_DIR}/resources/icons/hobbycad.svg
#        OUTPUT ${CMAKE_BINARY_DIR}/icons
#        NAME   hobbycad
#    )
#
# =====================================================================

find_program(RSVG_CONVERT rsvg-convert)
find_program(ICOTOOL icotool)
find_program(ICONUTIL iconutil)

# ImageMagick 7+ uses "magick" as the primary command.
# Avoid find_program(CONVERT convert) — on Windows this finds
# C:\Windows\System32\convert.exe (disk partition utility).
find_program(MAGICK magick)
if(NOT MAGICK)
    # ImageMagick 6 fallback — only search in typical install paths,
    # never in C:\Windows\System32.
    find_program(MAGICK convert
        PATHS /usr/bin /usr/local/bin /opt/homebrew/bin
        NO_DEFAULT_PATH
    )
endif()

function(generate_icons)
    cmake_parse_arguments(ICON "" "SVG;OUTPUT;NAME" "" ${ARGN})

    if(NOT ICON_SVG)
        message(FATAL_ERROR "generate_icons: SVG argument required")
    endif()
    if(NOT ICON_OUTPUT)
        message(FATAL_ERROR "generate_icons: OUTPUT argument required")
    endif()
    if(NOT ICON_NAME)
        set(ICON_NAME "hobbycad")
    endif()

    if(NOT RSVG_CONVERT)
        message(WARNING
            "rsvg-convert not found — icon generation disabled.\n"
            "Install with: sudo apt-get install -y librsvg2-bin")
        return()
    endif()

    file(MAKE_DIRECTORY ${ICON_OUTPUT})

    # ------------------------------------------------------------------
    #  Linux: PNG files at freedesktop hicolor sizes
    # ------------------------------------------------------------------

    set(HICOLOR_SIZES 16 24 32 48 64 128 256 512)
    set(ICON_PNG_FILES "")

    foreach(_size ${HICOLOR_SIZES})
        set(_png "${ICON_OUTPUT}/${ICON_NAME}-${_size}.png")
        add_custom_command(
            OUTPUT  ${_png}
            COMMAND ${RSVG_CONVERT}
                    -w ${_size} -h ${_size}
                    -o ${_png}
                    ${ICON_SVG}
            DEPENDS ${ICON_SVG}
            COMMENT "Generating ${ICON_NAME}-${_size}.png"
        )
        list(APPEND ICON_PNG_FILES ${_png})
    endforeach()

    # ------------------------------------------------------------------
    #  Windows: .ico (multi-resolution)
    # ------------------------------------------------------------------

    set(ICO_SIZES 16 32 48 64 128 256)
    set(ICON_ICO_FILE "")

    if(ICOTOOL)
        # Preferred: icotool from icoutils
        set(_ico "${ICON_OUTPUT}/${ICON_NAME}.ico")
        set(_ico_pngs "")
        foreach(_size ${ICO_SIZES})
            list(APPEND _ico_pngs "${ICON_OUTPUT}/${ICON_NAME}-${_size}.png")
        endforeach()
        add_custom_command(
            OUTPUT  ${_ico}
            COMMAND ${ICOTOOL} -c -o ${_ico} ${_ico_pngs}
            DEPENDS ${_ico_pngs}
            COMMENT "Generating ${ICON_NAME}.ico (icotool)"
        )
        set(ICON_ICO_FILE ${_ico})
    elseif(MAGICK)
        # Fallback: ImageMagick (magick or convert)
        set(_ico "${ICON_OUTPUT}/${ICON_NAME}.ico")
        set(_ico_pngs "")
        foreach(_size ${ICO_SIZES})
            list(APPEND _ico_pngs "${ICON_OUTPUT}/${ICON_NAME}-${_size}.png")
        endforeach()
        add_custom_command(
            OUTPUT  ${_ico}
            COMMAND ${MAGICK} ${_ico_pngs} ${_ico}
            DEPENDS ${_ico_pngs}
            COMMENT "Generating ${ICON_NAME}.ico (ImageMagick)"
        )
        set(ICON_ICO_FILE ${_ico})
    else()
        message(STATUS "No .ico generator found (install icoutils or imagemagick)")
    endif()

    # ------------------------------------------------------------------
    #  macOS: .icns via iconutil
    # ------------------------------------------------------------------

    set(ICON_ICNS_FILE "")

    if(APPLE AND ICONUTIL)
        set(_iconset "${ICON_OUTPUT}/${ICON_NAME}.iconset")
        set(_icns "${ICON_OUTPUT}/${ICON_NAME}.icns")

        # macOS iconset requires specific filenames with @2x variants
        # icon_16x16.png, icon_16x16@2x.png (=32), icon_32x32.png, etc.
        set(_iconset_cmds
            COMMAND ${CMAKE_COMMAND} -E make_directory ${_iconset}
        )
        # Standard sizes and their @2x mappings:
        #   16 -> icon_16x16.png
        #   32 -> icon_16x16@2x.png AND icon_32x32.png
        #   64 -> icon_32x32@2x.png
        #  128 -> icon_128x128.png
        #  256 -> icon_128x128@2x.png AND icon_256x256.png
        #  512 -> icon_256x256@2x.png AND icon_512x512.png
        # 1024 -> icon_512x512@2x.png
        set(_iconset_sizes 16 32 64 128 256 512 1024)
        set(_iconset_pngs "")

        foreach(_size ${_iconset_sizes})
            set(_png "${ICON_OUTPUT}/${ICON_NAME}-iconset-${_size}.png")
            add_custom_command(
                OUTPUT  ${_png}
                COMMAND ${RSVG_CONVERT}
                        -w ${_size} -h ${_size}
                        -o ${_png}
                        ${ICON_SVG}
                DEPENDS ${ICON_SVG}
                COMMENT "Generating iconset ${_size}x${_size}"
            )
            list(APPEND _iconset_pngs ${_png})
        endforeach()

        # Copy to iconset with correct naming
        add_custom_command(
            OUTPUT  ${_icns}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${_iconset}
            COMMAND ${CMAKE_COMMAND} -E copy
                ${ICON_OUTPUT}/${ICON_NAME}-iconset-16.png
                ${_iconset}/icon_16x16.png
            COMMAND ${CMAKE_COMMAND} -E copy
                ${ICON_OUTPUT}/${ICON_NAME}-iconset-32.png
                ${_iconset}/icon_16x16@2x.png
            COMMAND ${CMAKE_COMMAND} -E copy
                ${ICON_OUTPUT}/${ICON_NAME}-iconset-32.png
                ${_iconset}/icon_32x32.png
            COMMAND ${CMAKE_COMMAND} -E copy
                ${ICON_OUTPUT}/${ICON_NAME}-iconset-64.png
                ${_iconset}/icon_32x32@2x.png
            COMMAND ${CMAKE_COMMAND} -E copy
                ${ICON_OUTPUT}/${ICON_NAME}-iconset-128.png
                ${_iconset}/icon_128x128.png
            COMMAND ${CMAKE_COMMAND} -E copy
                ${ICON_OUTPUT}/${ICON_NAME}-iconset-256.png
                ${_iconset}/icon_128x128@2x.png
            COMMAND ${CMAKE_COMMAND} -E copy
                ${ICON_OUTPUT}/${ICON_NAME}-iconset-256.png
                ${_iconset}/icon_256x256.png
            COMMAND ${CMAKE_COMMAND} -E copy
                ${ICON_OUTPUT}/${ICON_NAME}-iconset-512.png
                ${_iconset}/icon_256x256@2x.png
            COMMAND ${CMAKE_COMMAND} -E copy
                ${ICON_OUTPUT}/${ICON_NAME}-iconset-512.png
                ${_iconset}/icon_512x512.png
            COMMAND ${CMAKE_COMMAND} -E copy
                ${ICON_OUTPUT}/${ICON_NAME}-iconset-1024.png
                ${_iconset}/icon_512x512@2x.png
            COMMAND ${ICONUTIL} -c icns -o ${_icns} ${_iconset}
            DEPENDS ${_iconset_pngs}
            COMMENT "Generating ${ICON_NAME}.icns"
        )
        set(ICON_ICNS_FILE ${_icns})
    elseif(APPLE)
        message(STATUS "iconutil not found — .icns generation disabled")
    endif()

    # ------------------------------------------------------------------
    #  Collect all generated icons into a custom target
    # ------------------------------------------------------------------

    set(_all_icons ${ICON_PNG_FILES})
    if(ICON_ICO_FILE)
        list(APPEND _all_icons ${ICON_ICO_FILE})
    endif()
    if(ICON_ICNS_FILE)
        list(APPEND _all_icons ${ICON_ICNS_FILE})
    endif()

    add_custom_target(icons ALL DEPENDS ${_all_icons})

    # ------------------------------------------------------------------
    #  Install rules
    # ------------------------------------------------------------------

    # Scalable SVG
    install(FILES ${ICON_SVG}
        DESTINATION share/icons/hicolor/scalable/apps
        RENAME ${ICON_NAME}.svg
        OPTIONAL
    )

    # Hicolor PNGs
    foreach(_size ${HICOLOR_SIZES})
        install(FILES ${ICON_OUTPUT}/${ICON_NAME}-${_size}.png
            DESTINATION share/icons/hicolor/${_size}x${_size}/apps
            RENAME ${ICON_NAME}.png
            OPTIONAL
        )
    endforeach()

    # XPM (installed from source, not generated)
    install(FILES ${CMAKE_SOURCE_DIR}/resources/icons/${ICON_NAME}.xpm
        DESTINATION share/pixmaps
        OPTIONAL
    )

    # Export variables to parent scope
    set(ICON_PNG_FILES  ${ICON_PNG_FILES}  PARENT_SCOPE)
    set(ICON_ICO_FILE   ${ICON_ICO_FILE}   PARENT_SCOPE)
    set(ICON_ICNS_FILE  ${ICON_ICNS_FILE}  PARENT_SCOPE)

endfunction()
