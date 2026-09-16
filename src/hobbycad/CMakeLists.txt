# =====================================================================
#  src/hobbycad/CMakeLists.txt — HobbyCAD Application
# =====================================================================
#
#  Builds the hobbycad executable with GUI (Full + Reduced modes)
#  and CLI mode.
#
# =====================================================================

# ---- Dependencies ---------------------------------------------------

find_package(OpenCASCADE REQUIRED)

# PNG must be found before Qt on Windows with vcpkg static builds.
# vcpkg's freetype cmake wrapper references PNG::PNG without calling
# find_dependency(PNG), causing "unknown target" errors.
find_package(PNG QUIET)

# Only the application needs Qt and OpenGL. This directory is also
# entered for a command-line-only build, which has neither, so asking
# for them unconditionally would fail that configure before it starts.
if(HOBBYCAD_BUILD_APP)
    find_package(Qt6 ${HOBBYCAD_MIN_QT_VERSION} REQUIRED COMPONENTS
        Core Gui Widgets OpenGL Svg)
    find_package(OpenGL REQUIRED)
endif()

# Note: libslvs is now linked via libhobbycad (see src/libhobbycad/CMakeLists.txt)
# The GUI uses sketch::Solver from the library, no direct SLVS dependency needed here.

# ---- Source files ----------------------------------------------------

# ---- The command layer ----------------------------------------------
#
#  Qt-free: these four files and the library are all the standalone
#  front end needs. The application compiles them too, and adds
#  cli_translator.cpp so its CLI output is translated.
#
set(CLI_SOURCES
    cli/climode.cpp
    cli/cliengine.cpp
    cli/clihistory.cpp
    cli/terminalinput.cpp
)

# ---- The application ------------------------------------------------
#
#  Everything from here to the command-line front end at the bottom is
#  the Qt application. It is skipped entirely when only the command line
#  is being built. The body is deliberately left at its original
#  indentation: re-indenting four hundred lines would hide a two-line
#  change inside a four-hundred-line diff.
#
if(HOBBYCAD_BUILD_APP)

set(APP_SOURCES
    main.cpp

    # Translation catalog loading and live language switching
    i18n/translations.cpp

    # Shared GUI components
    gui/mainwindow.cpp
    gui/formulaedit.cpp
    gui/objectsbrowserwidget.cpp
    softcrash.cpp
    gui/aboutdialog.cpp
    gui/clipanel.cpp
    gui/preferencesdialog.cpp
    gui/bindingsdialog.cpp
    gui/bindingeditrow.cpp
    gui/themevalidator.cpp
    gui/toolbarbutton.cpp
    gui/toolbardropdown.cpp
    gui/timelinewidget.cpp
    gui/formuladialog.cpp
    gui/formulafield.cpp
    gui/parametersdialog.cpp
    gui/sketchtoolbar.cpp
    gui/modeltoolbar.cpp
    gui/changelogpanel.cpp
    gui/constraintrenderer.cpp
    gui/dimensioninput.cpp
    gui/entityrenderer.cpp
    gui/colorpicker.cpp
    gui/sketchtheme.cpp
    gui/themeeditordialog.cpp
    gui/snapengine.cpp
    gui/sketchcanvas.cpp
    gui/sketchsolver.cpp

    gui/sketchplanedialog.cpp
    gui/constructionplanedialog.cpp
    gui/backgroundimagedialog.cpp
    gui/backgroundcalibrationdialog.cpp
    gui/tools/polygontoolhandler.cpp
    gui/tools/rectangletoolhandler.cpp
    gui/tools/constrainttoolhandler.cpp
    gui/tools/dimensiontoolhandler.cpp
    gui/tools/optoolhandlers.cpp
    gui/tools/texttoolhandler.cpp
    gui/tools/drawconstrainhandlers.cpp
    gui/constraintglyphs.cpp
    gui/tools/simpletoolhandlers.cpp
    gui/tools/slottoolhandler.cpp
    gui/tools/splinetoolhandler.cpp
    gui/tools/circletoolhandler.cpp
    gui/tools/ellipsetoolhandler.cpp
    gui/tools/pointtoolhandler.cpp
    gui/tools/arctoolhandler.cpp
    gui/tools/linetoolhandler.cpp
    gui/constraintexplorer.cpp
    gui/sketchpropertieswidget.cpp
    gui/sketchoptionswidget.cpp
    gui/planetransformpanel.cpp
    gui/projectbrowserwidget.cpp
    gui/extrudedialog.cpp
    gui/revolvedialog.cpp

    # Full mode (OpenGL viewport)
    gui/full/viewportwidget.cpp
    gui/full/fullmodewindow.cpp
    gui/full/scalebarwidget.cpp
    gui/full/aiscanvas2d.cpp
    gui/full/aisgrid.cpp
    gui/full/aissketchplane.cpp
    gui/full/navorbitring.cpp
    gui/full/navhomebutton.cpp

    # Reduced mode (no OpenGL)
    gui/reduced/reducedviewport.cpp
    gui/reduced/diagnosticdialog.cpp
    gui/reduced/reducedmodewindow.cpp

    # Command-line mode (the list is shared with the standalone front
    # end below, so the two never drift apart)
    ${CLI_SOURCES}

    # Qt translation for the Qt-free command layer
    cli_translator.cpp
)

set(APP_HEADERS
    i18n/retranslatable.h
    i18n/translations.h

    gui/mainwindow.h
    gui/formulaedit.h
    gui/objectsbrowserwidget.h
    gui/aboutdialog.h
    gui/clipanel.h
    gui/preferencesdialog.h
    gui/bindingsdialog.h
    gui/bindingeditrow.h
    gui/themevalidator.h
    gui/toolbarbutton.h
    gui/toolbardropdown.h
    gui/timelinewidget.h
    gui/formuladialog.h
    gui/formulafield.h
    gui/parametersdialog.h
    gui/sketchtoolbar.h
    gui/modeltoolbar.h
    gui/changelogpanel.h
    gui/sketchcanvas.h
    gui/sketchsolver.h

    gui/sketchplanedialog.h
    gui/constructionplanedialog.h
    gui/backgroundimagedialog.h
    gui/backgroundcalibrationdialog.h
    gui/tools/polygontoolhandler.h
    gui/tools/rectangletoolhandler.h
    gui/tools/constrainttoolhandler.h
    gui/tools/dimensiontoolhandler.h
    gui/tools/optoolhandlers.h
    gui/tools/texttoolhandler.h
    gui/tools/simpletoolhandlers.h
    gui/tools/slottoolhandler.h
    gui/tools/splinetoolhandler.h
    gui/tools/circletoolhandler.h
    gui/tools/ellipsetoolhandler.h
    gui/tools/pointtoolhandler.h
    gui/tools/arctoolhandler.h
    gui/tools/linetoolhandler.h
    gui/tools/sketchtoolhandler.h
    gui/constraintexplorer.h
    gui/sketchpropertieswidget.h
    gui/sketchoptionswidget.h
    gui/extrudedialog.h
    gui/revolvedialog.h
    gui/full/viewportwidget.h
    gui/full/fullmodewindow.h
    gui/full/scalebarwidget.h
    gui/full/aiscanvas2d.h
    gui/full/aisgrid.h
    gui/full/aissketchplane.h
    gui/full/navcontrols.h
    gui/full/navorbitring.h
    gui/full/navhomebutton.h
    gui/reduced/reducedviewport.h
    gui/reduced/diagnosticdialog.h
    gui/reduced/reducedmodewindow.h
    cli/climode.h
    cli/cliengine.h
    cli/clihistory.h
    cli/terminalinput.h
    cli_translator.h
)

# ---- Resources -------------------------------------------------------

set(APP_RESOURCES
    resources/hobbycad.qrc
)

# ---- Executable ------------------------------------------------------

if(APPLE)
    add_executable(hobbycad MACOSX_BUNDLE ${APP_SOURCES} ${APP_HEADERS} ${APP_RESOURCES})
else()
    add_executable(hobbycad ${APP_SOURCES} ${APP_HEADERS} ${APP_RESOURCES})
endif()

target_include_directories(hobbycad PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Treat OCCT headers as system headers to suppress deprecation warnings
# from NCollection_StlIterator.hxx (std::iterator is deprecated in C++17).
target_include_directories(hobbycad SYSTEM PRIVATE
    ${OpenCASCADE_INCLUDE_DIR}
)

# OCCT visualization libraries (needed by the app for AIS display)
set(APP_OCCT_LIBS
    TKernel TKMath TKBRep TKTopAlgo TKPrim
    TKV3d TKOpenGl TKService
)

target_link_libraries(hobbycad PRIVATE
    hobbycad-lib   # libhobbycad core
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::Svg
    OpenGL::GL
    ${APP_OCCT_LIBS}
)

# Note: libslvs is now linked via libhobbycad - no direct linking needed here

# Pass dependency versions as compile definitions for the About dialog.
# The About dialog is expected to name EVERY dependency, so anything added to
# the link line belongs here too. The solver is the exception: it reports
# itself through sketch::solverVersionString(), because its version has to
# come from the header actually compiled against, not from whatever
# pkg-config happened to find.
set(_hobbycad_about_defs
    HOBBYCAD_CMAKE_VERSION="${CMAKE_VERSION}"
    HOBBYCAD_OCCT_VERSION="${OpenCASCADE_VERSION}"
)

if(nlohmann_json_VERSION)
    list(APPEND _hobbycad_about_defs
         HOBBYCAD_JSON_VERSION="${nlohmann_json_VERSION}")
endif()

# Each of these is exported by the branch in libhobbycad that actually LINKS
# the dependency, so this list mirrors the link line exactly: linked means
# listed, not linked means absent. Do not set any of them from a "found"
# check: found is not linked.
if(HOBBYCAD_ABOUT_WEBP)
    list(APPEND _hobbycad_about_defs
         HOBBYCAD_WEBP_VERSION="${HOBBYCAD_ABOUT_WEBP}")
endif()

if(HOBBYCAD_ABOUT_STB)
    list(APPEND _hobbycad_about_defs
         HOBBYCAD_STB_VERSION="${HOBBYCAD_ABOUT_STB}")
endif()

if(HOBBYCAD_ABOUT_EGL)
    list(APPEND _hobbycad_about_defs
         HOBBYCAD_EGL_VERSION="${HOBBYCAD_ABOUT_EGL}")
endif()

target_compile_definitions(hobbycad PRIVATE ${_hobbycad_about_defs})

# ---- macOS App Bundle ------------------------------------------------

if(APPLE)
    # Get version from top-level project
    # The bundle is HobbyCAD.app with HobbyCAD inside it, the case users
    # see in Finder and the Dock; the target keeps its lowercase name.
    set_target_properties(hobbycad PROPERTIES
        OUTPUT_NAME "HobbyCAD"
        MACOSX_BUNDLE_BUNDLE_NAME "HobbyCAD"
        MACOSX_BUNDLE_EXECUTABLE_NAME "HobbyCAD"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.hobbycad.HobbyCAD"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/resources/macos/Info.plist.in"
    )
    # A dynamic Qt (the Homebrew and developer builds; the CI DMGs are static)
    # is referenced through @rpath. CMake strips the build rpaths on install,
    # which left the installed bundle unable to load QtWidgets; keep the
    # linked directories (the Qt lib dir, the OpenCASCADE keg) as install
    # rpaths. macOS only: the Linux packages find their libraries by the
    # system paths and lintian objects to embedded rpaths.
    set_target_properties(hobbycad PROPERTIES INSTALL_RPATH_USE_LINK_PATH TRUE)

endif()

# ---- Translations ----------------------------------------------------
#
#  hobbycad_en.ts is the extraction template: English is the source
#  language, so it holds no translations of its own and exists only so
#  lupdate has somewhere to write. The other catalogs are real.
#
#  Regenerate the templates after adding or changing any tr() string:
#      cmake --build build --target hobbycad_lupdate
#
#  That target is explicit and not part of "all", so ordinary builds are
#  not slowed down by it.
#
set(TS_FILES
    ${CMAKE_SOURCE_DIR}/resources/translations/hobbycad_en.ts
    ${CMAKE_SOURCE_DIR}/resources/translations/hobbycad_cs.ts
    ${CMAKE_SOURCE_DIR}/resources/translations/hobbycad_de.ts
    ${CMAKE_SOURCE_DIR}/resources/translations/hobbycad_es.ts
    ${CMAKE_SOURCE_DIR}/resources/translations/hobbycad_fr.ts
    ${CMAKE_SOURCE_DIR}/resources/translations/hobbycad_it.ts
    ${CMAKE_SOURCE_DIR}/resources/translations/hobbycad_ja.ts
    ${CMAKE_SOURCE_DIR}/resources/translations/hobbycad_ko.ts
    ${CMAKE_SOURCE_DIR}/resources/translations/hobbycad_nl.ts
    ${CMAKE_SOURCE_DIR}/resources/translations/hobbycad_pl.ts
    ${CMAKE_SOURCE_DIR}/resources/translations/hobbycad_pt_BR.ts
    ${CMAKE_SOURCE_DIR}/resources/translations/hobbycad_ru.ts
    ${CMAKE_SOURCE_DIR}/resources/translations/hobbycad_tr.ts
    ${CMAKE_SOURCE_DIR}/resources/translations/hobbycad_zh_CN.ts
    ${CMAKE_SOURCE_DIR}/resources/translations/hobbycad_zh_TW.ts
)

find_package(Qt6 COMPONENTS LinguistTools QUIET)
if(Qt6LinguistTools_FOUND)
    qt_add_lupdate(hobbycad
        TS_FILES ${TS_FILES}
        OPTIONS -no-obsolete
    )
endif()

# lrelease is located with find_program rather than required through
# LinguistTools, so Qt Linguist does not become a hard build dependency for
# everyone packaging HobbyCAD just to compile catalogs that are optional at
# runtime. When it is missing the application still builds and runs; it is
# simply English only.
#
# On Debian and Ubuntu /usr/bin/lrelease is a qtchooser symlink that resolves
# to Qt 5, and qtchooser has no Qt 6 concept at all (QT_SELECT=qt6 errors).
# Qt 6's copy lives in the Qt 6 bin directory and has to be named explicitly,
# which is why the default search path is consulted last and only for names
# that cannot be the qtchooser shim.
if(TARGET Qt6::lrelease)
    get_target_property(HOBBYCAD_LRELEASE Qt6::lrelease IMPORTED_LOCATION)
else()
    get_target_property(_qt_qmake Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin "${_qt_qmake}" DIRECTORY)
    find_program(HOBBYCAD_LRELEASE
        NAMES lrelease-qt6 lrelease6 lrelease
        HINTS "${_qt_bin}"
              /usr/lib/qt6/bin
              /usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}/qt6/bin
        NO_DEFAULT_PATH
    )
    if(NOT HOBBYCAD_LRELEASE)
        find_program(HOBBYCAD_LRELEASE NAMES lrelease-qt6 lrelease6)
    endif()
endif()

if(HOBBYCAD_LRELEASE)
    set(_qm_dir ${CMAKE_CURRENT_BINARY_DIR}/translations)
    file(MAKE_DIRECTORY ${_qm_dir})

    set(_qm_files "")
    foreach(_ts ${TS_FILES})
        get_filename_component(_base ${_ts} NAME_WE)
        # Skip the extraction template. lrelease emits a valid but empty
        # catalog for it, which Translations::available() would then have to
        # recognize and discard at runtime. It does, but there is no reason
        # to ship the thing in the first place.
        if(_base STREQUAL "hobbycad_en")
            continue()
        endif()
        set(_qm ${_qm_dir}/${_base}.qm)
        add_custom_command(
            OUTPUT ${_qm}
            COMMAND ${HOBBYCAD_LRELEASE} -silent ${_ts} -qm ${_qm}
            DEPENDS ${_ts}
            COMMENT "Compiling translation ${_base}.qm"
            VERBATIM
        )
        list(APPEND _qm_files ${_qm})
    endforeach()

    # Embedded rather than installed-only. This removes a whole class of
    # failure (a catalog built but not installed, or installed to a prefix
    # the program was not told about) for a few hundred KB. The directory
    # search in Translations::catalogDirs() runs first regardless, so a
    # translator can still drop a .qm beside the binary and see it without
    # rebuilding.
    qt_add_resources(hobbycad "hobbycad_i18n"
        PREFIX "/i18n"
        BASE ${_qm_dir}
        FILES ${_qm_files}
    )

    # Packaged override location, searched between the binary directory and
    # the embedded copy.
    target_compile_definitions(hobbycad PRIVATE
        HOBBYCAD_TRANSLATIONS_DIR="${CMAKE_INSTALL_PREFIX}/share/hobbycad/translations"
    )

    install(FILES ${_qm_files}
            DESTINATION share/hobbycad/translations)
else()
    # Loud rather than silent: the build still succeeds and the program still
    # runs, but every language is gone and nothing else would say so.
    message(WARNING
        "lrelease not found - building WITHOUT compiled translations. "
        "The result will be English only. Install qt6-l10n-tools to fix.")
endif()

# ---- Static Qt plugin imports (for vcpkg static builds) ----------------
#
# When Qt is built statically (vcpkg static triplets), plugins like
# QWindowsIntegrationPlugin must be linked directly into the executable.
# qt_finalize_target() handles this automatically; it discovers and
# links all required plugins that the target depends on.
# For shared Qt builds, this is a no-op.

get_target_property(_qt_type Qt6::Core TYPE)
if(_qt_type STREQUAL "STATIC_LIBRARY")
    qt_finalize_target(hobbycad)
endif()

# ---- Install ---------------------------------------------------------

if(APPLE)
    install(TARGETS hobbycad BUNDLE DESTINATION .)
else()
    install(TARGETS hobbycad RUNTIME DESTINATION bin)
endif()

install(FILES
    ${CMAKE_SOURCE_DIR}/resources/hobbycad.desktop
    DESTINATION share/applications
    OPTIONAL
)

# Man page. Installed here rather than only from debian/ so that vcpkg,
# Homebrew and the BSD ports get it as well.
install(FILES
    ${CMAKE_SOURCE_DIR}/docs/man/hobbycad.1
    DESTINATION share/man/man1
    OPTIONAL
)

# Icon generation and installation handled by cmake/GenerateIcons.cmake
include(${CMAKE_SOURCE_DIR}/cmake/GenerateIcons.cmake)
generate_icons(
    SVG    ${CMAKE_SOURCE_DIR}/resources/icons/hobbycad.svg
    OUTPUT ${CMAKE_BINARY_DIR}/icons
    NAME   hobbycad
)

# Copy .icns into macOS app bundle Resources
if(APPLE AND ICON_ICNS_FILE)
    set_source_files_properties(${ICON_ICNS_FILE} PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources"
    )
    target_sources(hobbycad PRIVATE ${ICON_ICNS_FILE})
endif()

# Windows: a GUI-subsystem program, so a double-click opens no console
# window; main() attaches to the parent console when there is one (see
# main.cpp), and Qt supplies the WinMain shim for a plain main().
if(WIN32)
    set_target_properties(hobbycad PROPERTIES WIN32_EXECUTABLE ON)

    # hobbycad.com: the console-subsystem launcher beside hobbycad.exe
    # (see win/hobbycad-com.c). Shells resolve "hobbycad" to it first, so
    # scripts and pipes keep console semantics while the .exe stays free
    # of a console window. It shares the icon resource.
    enable_language(C)
    add_executable(hobbycad_com win/hobbycad-com.c)
    set_target_properties(hobbycad_com PROPERTIES OUTPUT_NAME hobbycad SUFFIX .com)
    install(TARGETS hobbycad_com RUNTIME DESTINATION bin)
endif()

# Windows: the executable carries the icon as a resource, which is what
# Explorer, the Start menu, the taskbar, the uninstall entry and the .hcad
# association (DefaultIcon ...,0) display. The resource script is written
# beside the rendered .ico and compiled with the rest of the target; the
# absolute path keeps rc.exe and windres from resolving it against their
# own working directory.
if(WIN32 AND ICON_ICO_FILE)
    enable_language(RC)
    set(_hobbycad_rc "${CMAKE_BINARY_DIR}/icons/hobbycad.rc")
    file(TO_CMAKE_PATH "${ICON_ICO_FILE}" _hobbycad_ico_path)
    file(WRITE "${_hobbycad_rc}" "IDI_ICON1 ICON \"${_hobbycad_ico_path}\"\n")
    set_source_files_properties("${_hobbycad_rc}" PROPERTIES
        OBJECT_DEPENDS "${ICON_ICO_FILE}"
    )
    target_sources(hobbycad PRIVATE "${_hobbycad_rc}")
    target_sources(hobbycad_com PRIVATE "${_hobbycad_rc}")
endif()

endif()  # HOBBYCAD_BUILD_APP: end of the application

# =====================================================================
#  hobbycad-cli — the command line as its own front end
# =====================================================================
#
#  libhobbycad plus the command layer: no Qt, no GUI, no display.
#
#  This is a TEST configuration and is deliberately not installed or
#  packaged: the shipped application already reaches these commands.
#  What it proves is the layering, that the command layer builds and
#  runs without the front end it usually sits behind. Do not add an
#  install rule here.
#
if(HOBBYCAD_BUILD_CLI)
    add_executable(hobbycad-cli ${CLI_SOURCES} cli/main_cli.cpp)

    # AUTOMOC would otherwise run over these files looking for Qt macros
    # that are deliberately not there, and would need Qt to do it.
    set_target_properties(hobbycad-cli PROPERTIES
        AUTOMOC OFF
        AUTOUIC OFF
        AUTORCC OFF)

    target_include_directories(hobbycad-cli PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/cli
    )

    # The library's public headers include OCCT headers, but libhobbycad
    # keeps the OCCT include path PRIVATE, so a target that links it does
    # not inherit one. The application adds the path itself; so must this.
    target_include_directories(hobbycad-cli SYSTEM PRIVATE
        ${OpenCASCADE_INCLUDE_DIR}
    )

    # Same story for nlohmann/json: without Qt the library's own
    # feature.h includes <nlohmann/json.hpp>, and libhobbycad links that
    # package PRIVATE. On a system where the headers sit in /usr/include
    # this is invisible; where they come from vcpkg it is a hard failure,
    # which is exactly how the Windows run found it.
    find_package(nlohmann_json QUIET)
    if(TARGET nlohmann_json::nlohmann_json)
        target_link_libraries(hobbycad-cli PRIVATE nlohmann_json::nlohmann_json)
    endif()

    target_link_libraries(hobbycad-cli PRIVATE hobbycad-lib)
endif()
