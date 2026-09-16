# =====================================================================
#  cmake/GenerateIcons.cmake — Build-time icon generation from SVG
# =====================================================================
#
#  Generates platform-specific icon files from the canonical SVG:
#
#    Linux:   PNG files at hicolor theme sizes (16–512) and the 32x32
#             XPM the Debian menu wants (also on the BSDs)
#    Windows: Multi-resolution .ico (16–256)
#    macOS:   .icns via iconutil (16–1024)
#
#  Requires: nothing beyond the Qt the GUI already needs (Gui + Svg);
#  the icons are rendered by tools/render-icons.cpp, built here.
#  macOS .icns uses Apple's iconutil, present on every Mac.
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

# iconutil is Apple's own, present on every macOS; nothing to install.
find_program(ICONUTIL iconutil)

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

    # The scalable SVG comes straight from the source tree; every raster
    # form, the menu XPM included, is rendered from it below.
    install(FILES ${ICON_SVG}
        DESTINATION share/icons/hicolor/scalable/apps
        RENAME ${ICON_NAME}.svg
        OPTIONAL
    )

    # ------------------------------------------------------------------
    #  The renderer: tools/render-icons.cpp, built here with the Qt the
    #  GUI already requires (Gui + Svg). It writes the hicolor PNGs, the
    #  Windows .ico, the macOS iconset PNGs and the Debian menu XPM from
    #  the one SVG, so no rsvg-convert, icotool or ImageMagick is needed
    #  on any build host.
    # ------------------------------------------------------------------
    if(NOT TARGET Qt6::Svg)
        message(FATAL_ERROR "generate_icons: Qt6::Svg is required to render the icons")
    endif()
    add_executable(render_icons ${CMAKE_SOURCE_DIR}/tools/render-icons.cpp)
    target_link_libraries(render_icons PRIVATE Qt6::Gui Qt6::Svg)
    # A static Qt links its platform plugin into every executable, and on
    # macOS the Cocoa plugin refers to OpenGL (glGetString, glGetIntegerv)
    # without carrying the framework in its own link interface, so the
    # renderer, which asks Qt for no OpenGL of its own, failed to link.
    if(APPLE)
        target_link_libraries(render_icons PRIVATE "-framework OpenGL")
    endif()
    set_target_properties(render_icons PROPERTIES AUTOMOC OFF)

    file(MAKE_DIRECTORY ${ICON_OUTPUT})

    set(HICOLOR_SIZES 16 24 32 48 64 128 256 512)
    set(ICO_SIZES 16 32 48 64 128 256)
    set(_iconset_sizes 16 32 64 128 256 512 1024)

    set(ICON_PNG_FILES "")
    foreach(_size ${HICOLOR_SIZES})
        list(APPEND ICON_PNG_FILES "${ICON_OUTPUT}/${ICON_NAME}-${_size}.png")
    endforeach()
    set(_ico "${ICON_OUTPUT}/${ICON_NAME}.ico")
    set(_iconset_pngs "")
    foreach(_size ${_iconset_sizes})
        list(APPEND _iconset_pngs "${ICON_OUTPUT}/${ICON_NAME}-${_size}.png")
    endforeach()
    # One render of every size any platform needs (the lists overlap).
    set(_all_sizes ${HICOLOR_SIZES} ${_iconset_sizes})
    list(REMOVE_DUPLICATES _all_sizes)
    string(REPLACE ";" "," _ico_list "${ICO_SIZES}")

    # The Debian menu icon: XPM, 32x32 at most, on the platforms that have
    # a pixmaps directory. Windows and macOS never ask for it.
    set(_xpm "")
    set(_xpm_arg "")
    if(UNIX AND NOT APPLE)
        set(_xpm "${ICON_OUTPUT}/${ICON_NAME}.xpm")
        set(_xpm_arg "xpm=32")
    endif()

    # The hicolor and iconset lists overlap; one output list, no duplicates.
    set(_all_outputs ${ICON_PNG_FILES} ${_iconset_pngs} ${_ico} ${_xpm})
    list(REMOVE_DUPLICATES _all_outputs)

    # The renderer needs no display, but QGuiApplication needs a platform
    # plugin. A shared Qt ships the "offscreen" plugin, which is what a
    # build container without a display wants. A static Qt (the vcpkg
    # builds on Windows and macOS) links only the platform plugin it was
    # built with, "windows" or "cocoa"; asking it for "offscreen" aborts
    # with "Could not find the Qt platform plugin", so there the default
    # platform is used and no window is ever shown.
    get_target_property(_qt_core_type Qt6::Core TYPE)
    if(_qt_core_type STREQUAL "STATIC_LIBRARY")
        set(_render_env "")
    else()
        set(_render_env ${CMAKE_COMMAND} -E env QT_QPA_PLATFORM=offscreen)
    endif()
    add_custom_command(
        OUTPUT  ${_all_outputs}
        COMMAND ${_render_env}
                $<TARGET_FILE:render_icons> ${ICON_SVG} ${ICON_OUTPUT} ${ICON_NAME}
                ${_all_sizes} "ico=${_ico_list}" ${_xpm_arg}
        DEPENDS render_icons ${ICON_SVG}
        COMMENT "Rendering ${ICON_NAME} icons (PNG sizes, .ico, .xpm) from ${ICON_SVG}"
        VERBATIM
    )
    set(ICON_ICO_FILE ${_ico})

    # Windows: the .ico is installed beside the binary tree so an installer
    # can use it for its own icon (Inno Setup's SetupIconFile). Other
    # platforms have no use for it and Debian would have to list it.
    if(WIN32)
        install(FILES ${_ico} DESTINATION share/icons OPTIONAL)
    endif()
    if(_xpm)
        install(FILES ${_xpm} DESTINATION share/pixmaps OPTIONAL)
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
        # Copy to iconset with correct naming
        add_custom_command(
            OUTPUT  ${_icns}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${_iconset}
            COMMAND ${CMAKE_COMMAND} -E copy
                ${ICON_OUTPUT}/${ICON_NAME}-16.png
                ${_iconset}/icon_16x16.png
            COMMAND ${CMAKE_COMMAND} -E copy
                ${ICON_OUTPUT}/${ICON_NAME}-32.png
                ${_iconset}/icon_16x16@2x.png
            COMMAND ${CMAKE_COMMAND} -E copy
                ${ICON_OUTPUT}/${ICON_NAME}-32.png
                ${_iconset}/icon_32x32.png
            COMMAND ${CMAKE_COMMAND} -E copy
                ${ICON_OUTPUT}/${ICON_NAME}-64.png
                ${_iconset}/icon_32x32@2x.png
            COMMAND ${CMAKE_COMMAND} -E copy
                ${ICON_OUTPUT}/${ICON_NAME}-128.png
                ${_iconset}/icon_128x128.png
            COMMAND ${CMAKE_COMMAND} -E copy
                ${ICON_OUTPUT}/${ICON_NAME}-256.png
                ${_iconset}/icon_128x128@2x.png
            COMMAND ${CMAKE_COMMAND} -E copy
                ${ICON_OUTPUT}/${ICON_NAME}-256.png
                ${_iconset}/icon_256x256.png
            COMMAND ${CMAKE_COMMAND} -E copy
                ${ICON_OUTPUT}/${ICON_NAME}-512.png
                ${_iconset}/icon_256x256@2x.png
            COMMAND ${CMAKE_COMMAND} -E copy
                ${ICON_OUTPUT}/${ICON_NAME}-512.png
                ${_iconset}/icon_512x512.png
            COMMAND ${CMAKE_COMMAND} -E copy
                ${ICON_OUTPUT}/${ICON_NAME}-1024.png
                ${_iconset}/icon_512x512@2x.png
            COMMAND ${ICONUTIL} -c icns -o ${_icns} ${_iconset}
            DEPENDS ${_iconset_pngs}
            COMMENT "Generating ${ICON_NAME}.icns"
        )
        set(ICON_ICNS_FILE ${_icns})
    elseif(APPLE)
        message(STATUS "iconutil not found; .icns generation disabled")
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
    if(_xpm)
        list(APPEND _all_icons ${_xpm})
    endif()

    add_custom_target(icons ALL DEPENDS ${_all_icons})

    # ------------------------------------------------------------------
    #  Install rules
    # ------------------------------------------------------------------

    # Hicolor PNGs
    foreach(_size ${HICOLOR_SIZES})
        install(FILES ${ICON_OUTPUT}/${ICON_NAME}-${_size}.png
            DESTINATION share/icons/hicolor/${_size}x${_size}/apps
            RENAME ${ICON_NAME}.png
            OPTIONAL
        )
    endforeach()

    # Export variables to parent scope
    set(ICON_PNG_FILES  ${ICON_PNG_FILES}  PARENT_SCOPE)
    set(ICON_ICO_FILE   ${ICON_ICO_FILE}   PARENT_SCOPE)
    set(ICON_ICNS_FILE  ${ICON_ICNS_FILE}  PARENT_SCOPE)

endfunction()
