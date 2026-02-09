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
find_package(Qt6 6.2 REQUIRED COMPONENTS
    Core Gui Widgets OpenGL)
find_package(OpenGL REQUIRED)

# ---- Source files ----------------------------------------------------

set(APP_SOURCES
    main.cpp

    # Shared GUI components
    gui/mainwindow.cpp
    gui/aboutdialog.cpp
    gui/clipanel.cpp

    # Full mode (OpenGL viewport)
    gui/full/viewportwidget.cpp
    gui/full/fullmodewindow.cpp

    # Reduced mode (no OpenGL)
    gui/reduced/reducedviewport.cpp
    gui/reduced/diagnosticdialog.cpp
    gui/reduced/reducedmodewindow.cpp

    # Command-line mode
    cli/climode.cpp
    cli/cliengine.cpp
    cli/clihistory.cpp
    cli/terminalinput.cpp
)

set(APP_HEADERS
    gui/mainwindow.h
    gui/aboutdialog.h
    gui/clipanel.h
    gui/full/viewportwidget.h
    gui/full/fullmodewindow.h
    gui/reduced/reducedviewport.h
    gui/reduced/diagnosticdialog.h
    gui/reduced/reducedmodewindow.h
    cli/climode.h
    cli/cliengine.h
    cli/clihistory.h
    cli/terminalinput.h
)

# ---- Resources -------------------------------------------------------

set(APP_RESOURCES
    resources/hobbycad.qrc
)

# ---- Executable ------------------------------------------------------

add_executable(hobbycad ${APP_SOURCES} ${APP_HEADERS} ${APP_RESOURCES})

target_include_directories(hobbycad PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
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
    OpenGL::GL
    ${APP_OCCT_LIBS}
)

# ---- Install ---------------------------------------------------------

install(TARGETS hobbycad RUNTIME DESTINATION bin)

install(FILES
    ${CMAKE_SOURCE_DIR}/resources/hobbycad.desktop
    DESTINATION share/applications
    OPTIONAL
)

install(FILES
    ${CMAKE_SOURCE_DIR}/resources/icons/hobbycad.svg
    DESTINATION share/icons/hicolor/scalable/apps
    OPTIONAL
)

