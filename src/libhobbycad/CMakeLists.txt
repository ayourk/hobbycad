# =====================================================================
#  HobbyCAD — src/libhobbycad/CMakeLists.txt — Core Library
# =====================================================================
#
#  Reusable library providing geometry operations, file I/O, and
#  document management.  No GUI dependencies (Qt Core only, no
#  Widgets or OpenGL).
#
#  Linkage type controlled by top-level HOBBYCAD_CORE_LINKAGE option.
#
# =====================================================================

# ---- Dependencies ---------------------------------------------------

find_package(OpenCASCADE REQUIRED)

# Qt is optional for the library.  When found (>= 6.4.2), it enables:
#   - JSON project serialization (QJsonDocument)
#   - Background image loading (QImage)
#   - OpenGL capability probing (QOpenGLContext)
# Without Qt, the library builds as a pure C++ library with these
# features compiled out via #if HOBBYCAD_HAS_QT guards.
find_package(Qt6 ${HOBBYCAD_MIN_QT_VERSION} QUIET COMPONENTS Core Gui OpenGL)
set(HOBBYCAD_LIB_HAS_QT ${Qt6_FOUND})

# nlohmann/json: header-only JSON library used as a Qt-free fallback
# for project serialization, parameter import/export, etc.
find_package(nlohmann_json CONFIG REQUIRED)
# Surface the version to the parent scope so the About dialog can name it.
set(nlohmann_json_VERSION "${nlohmann_json_VERSION}" PARENT_SCOPE)

# Optional EGL for headless OpenGL probing without Qt
set(HOBBYCAD_HAS_EGL FALSE)
if(NOT HOBBYCAD_LIB_HAS_QT)
    find_package(OpenGL COMPONENTS EGL QUIET)
    if(OpenGL_EGL_FOUND)
        set(HOBBYCAD_HAS_EGL TRUE)
        message(STATUS "libhobbycad: EGL found, OpenGL probing enabled without Qt")
    else()
        message(STATUS "libhobbycad: EGL not found, OpenGL probing will return stub data")
    endif()
endif()

# Required: libslvs for constraint solving (Phase 1)
# Try pkg-config first, then CMake config, then manual detection.
include(FindPkgConfig OPTIONAL)
set(HAVE_SLVS FALSE)
set(SLVS_TARGET "")

# Method 1: pkg-config (try libslvs first, then slvs). Both probes are QUIET:
# a miss on the first name is expected, and pkgconf would otherwise print
# "required by 'virtual:world', not found" for it.
if(PKG_CONFIG_FOUND)
    pkg_check_modules(SLVS QUIET IMPORTED_TARGET libslvs)
    if(NOT SLVS_FOUND)
        pkg_check_modules(SLVS QUIET IMPORTED_TARGET slvs)
    endif()
    if(SLVS_FOUND)
        set(HAVE_SLVS TRUE)
        set(SLVS_TARGET PkgConfig::SLVS)
        message(STATUS "libhobbycad: Found libslvs via pkg-config")
    endif()
endif()

# Method 2: CMake config
if(NOT HAVE_SLVS)
    find_package(slvs CONFIG QUIET)
    if(slvs_FOUND)
        set(HAVE_SLVS TRUE)
        if(TARGET slvs::slvs)
            set(SLVS_TARGET slvs::slvs)
        endif()
        message(STATUS "libhobbycad: Found libslvs via CMake config")
    endif()
endif()

# Method 3: Manual detection (header + library)
if(NOT HAVE_SLVS)
    find_path(SLVS_INCLUDE_DIR slvs.h
        HINTS /usr/include /usr/local/include)
    find_library(SLVS_LIBRARY slvs
        HINTS /usr/lib /usr/lib/x86_64-linux-gnu /usr/local/lib)
    if(SLVS_INCLUDE_DIR AND SLVS_LIBRARY)
        set(HAVE_SLVS TRUE)
        add_library(slvs_manual UNKNOWN IMPORTED)
        set_target_properties(slvs_manual PROPERTIES
            IMPORTED_LOCATION "${SLVS_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${SLVS_INCLUDE_DIR}")
        set(SLVS_TARGET slvs_manual)
        message(STATUS "libhobbycad: Found libslvs: ${SLVS_LIBRARY}")
    endif()
endif()

if(NOT HAVE_SLVS)
    message(FATAL_ERROR
        "libslvs not found (required for Phase 1 constraint solving).\n"
        "  Linux:   sudo add-apt-repository ppa:ayourk/hobbycad && sudo apt install libslvs-dev\n"
        "  Windows: pacman -S mingw-w64-ucrt-x86_64-solvespace\n"
        "  macOS:   build from source: https://github.com/solvespace/solvespace")
endif()

# Export HAVE_SLVS to parent scope so GUI knows solver is available
set(LIBHOBBYCAD_HAVE_SLVS TRUE PARENT_SCOPE)

# ---- Source files ----------------------------------------------------

set(LIBHOBBYCAD_SOURCES
    core.cpp
    document.cpp
    brep_io.cpp
    step_io.cpp
    stl_io.cpp
    obj_io.cpp
    parameters.cpp
    crashhandler.cpp
    opengl_info.cpp
    occt_viewer_probe.cpp
    project.cpp
    plane_frame.cpp
    file_format.cpp
    # Geometry module
    geometry/types.cpp
    geometry/intersections.cpp
    geometry/utils.cpp
    geometry/algorithms.cpp
    # Sketch module
    sketch/entity.cpp
    sketch/constraint.cpp
    sketch/operations.cpp
    sketch/slotpath.cpp
    sketch/patterns.cpp
    sketch/transform.cpp
    sketch/profiles.cpp
    sketch/solver.cpp
    sketch/queries.cpp
    sketch/export.cpp
    sketch/dxf_import.cpp
    sketch/background.cpp
    sketch/parsing.cpp
    sketch/group.cpp
    document_undo.cpp
    project_undo.cpp
    project_session.cpp
    browser.cpp
    naming.cpp
    sketch/undo.cpp
    sketch/snap.cpp
    sketch/inference.cpp
    sketch/handles.cpp
    sketch/decomposition.cpp
    sketch/bezier.cpp
    sketch/align.cpp
    sketch/properties.cpp
    # BREP module
    brep/operations.cpp
    brep/fit.cpp
    brep/slice.cpp
    brep/mesh_to_step.cpp
)

set(LIBHOBBYCAD_HEADERS
    hobbycad/core.h
    hobbycad/document.h
    hobbycad/brep_io.h
    hobbycad/step_io.h
    hobbycad/stl_io.h
    hobbycad/obj_io.h
    hobbycad/parameters.h
    hobbycad/crashhandler.h
    hobbycad/opengl_info.h
    hobbycad/occt_failure.h
    hobbycad/project.h
    hobbycad/project_undo.h
    hobbycad/project_session.h
    hobbycad/base64.h
    hobbycad/image_buffer.h
    # Geometry module
    hobbycad/geometry/types.h
    hobbycad/geometry/intersections.h
    hobbycad/geometry/utils.h
    hobbycad/geometry/algorithms.h
    # Sketch module
    hobbycad/sketch/entity.h
    hobbycad/sketch/slotpath.h
    hobbycad/sketch/constraint.h
    hobbycad/sketch/operations.h
    hobbycad/sketch/patterns.h
    hobbycad/sketch/transform.h
    hobbycad/sketch/profiles.h
    hobbycad/sketch/solver.h
    hobbycad/sketch/queries.h
    hobbycad/sketch/export.h
    hobbycad/sketch/background.h
    hobbycad/sketch/group.h
    hobbycad/sketch/undo.h
    hobbycad/sketch/snap.h
    hobbycad/sketch/handles.h
    hobbycad/sketch/decomposition.h
    # BREP module
    hobbycad/brep/operations.h
    hobbycad/brep/fit.h
    hobbycad/brep/slice.h
    hobbycad/brep/mesh_to_step.h
)

# ---- Library target --------------------------------------------------

add_library(hobbycad-lib ${HOBBYCAD_CORE_LINKAGE}
    ${LIBHOBBYCAD_SOURCES}
    ${LIBHOBBYCAD_HEADERS}
)

# Output file is named "libhobbycad" (not "libhobbycad-lib")
set_target_properties(hobbycad-lib PROPERTIES
    OUTPUT_NAME hobbycad
)

target_include_directories(hobbycad-lib
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

# SYSTEM has to come before the first visibility keyword. Written after
# one, CMake took it for a directory name and every compile line carried
# a bogus -I<source>/SYSTEM, while OCCT was included non-system and its
# warnings were ours to read.
target_include_directories(hobbycad-lib SYSTEM PRIVATE
    ${OpenCASCADE_INCLUDE_DIR}
)

# OCCT libraries needed by the core (no visualization)
#
# OCCT 7.8+ reorganized data exchange libraries under the TKDE prefix:
#   TKSTEP    → TKDESTEP       TKXDESTEP → merged into TKDESTEP
#   TKIGES    → TKDEIGES       TKXDEIGES → merged into TKDEIGES
#   TKSTL     → TKDESTL        TKXSBase  → removed
#   TKXCAF    → TKDEXCAF
#
# We use OpenCASCADE_DataExchange_LIBRARIES (set by find_package) to
# detect which libraries are available.  This is more reliable than
# checking CMake targets, which may not be defined on all platforms.
#
# Checked against OCCT 8.0.1: the toolkit names this file links are IDENTICAL
# in 7.9.x and 8.0.x, so nothing here needs a version branch.  Note that
# TKDEXCAF and TKDERWMESH (the "new" names in the table above) exist in
# NEITHER version; occt_pick_lib falls back to TKXCAF/TKRWMesh, which is why
# it must keep tolerating a missing NEW name rather than assuming the rename
# happened.  find_package(OpenCASCADE) is deliberately left unversioned, as
# are the libocct-*-dev build-deps in debian/control, so both majors resolve.
# Source-level compatibility with 7.x is exercised by the Debian 12 CI leg,
# which builds against Debian's OpenCASCADE 7.6.3, and by the MSYS2 build on
# MSYS2's packaged OpenCASCADE; every other Linux package builds the pinned
# 8.0.1+p1. The message accessor that differs between the majors lives in
# hobbycad/occt_failure.h.
#
macro(occt_pick_lib VAR OLD NEW)
    if(OLD IN_LIST OpenCASCADE_DataExchange_LIBRARIES)
        set(${VAR} ${OLD})
    elseif(NEW IN_LIST OpenCASCADE_DataExchange_LIBRARIES)
        set(${VAR} ${NEW})
    elseif(TARGET ${OLD})
        set(${VAR} ${OLD})
    elseif(TARGET ${NEW})
        set(${VAR} ${NEW})
    else()
        # Last resort: assume 7.8+ naming
        set(${VAR} ${NEW})
    endif()
endmacro()

occt_pick_lib(_OCCT_STEP     TKSTEP     TKDESTEP)
occt_pick_lib(_OCCT_IGES     TKIGES     TKDEIGES)
occt_pick_lib(_OCCT_STL      TKSTL      TKDESTL)
occt_pick_lib(_OCCT_XCAF     TKXCAF     TKDEXCAF)
occt_pick_lib(_OCCT_RWMESH   TKRWMesh   TKDERWMESH)

# TKXDESTEP/TKXDEIGES were merged into TKDESTEP/TKDEIGES in 7.8+.
# Only link them if they exist as separate libraries.
set(_OCCT_XDESTEP "")
set(_OCCT_XDEIGES "")
if("TKXDESTEP" IN_LIST OpenCASCADE_DataExchange_LIBRARIES OR TARGET TKXDESTEP)
    set(_OCCT_XDESTEP TKXDESTEP)
endif()
if("TKXDEIGES" IN_LIST OpenCASCADE_DataExchange_LIBRARIES OR TARGET TKXDEIGES)
    set(_OCCT_XDEIGES TKXDEIGES)
endif()

# OBJ reading/writing (RWObj_CafReader) moved from TKRWMesh (7.6) to
# TKDEOBJ (7.8+). TKRWMesh still exists in 7.8+ for other formats, so this
# cannot go through occt_pick_lib (which prefers the old name): link TKDEOBJ
# whenever it exists. Without it the SHARED library build fails to link
# (the static build hid it, since the executable links every OCCT library).
set(_OCCT_OBJ "")
if("TKDEOBJ" IN_LIST OpenCASCADE_DataExchange_LIBRARIES OR TARGET TKDEOBJ)
    set(_OCCT_OBJ TKDEOBJ)
endif()

# TKXSBase was removed in 7.8+; only link if present.
set(_OCCT_XSBASE "")
if("TKXSBase" IN_LIST OpenCASCADE_DataExchange_LIBRARIES OR TARGET TKXSBase)
    set(_OCCT_XSBASE TKXSBase)
endif()

set(HOBBYCAD_OCCT_LIBS
    TKernel TKMath TKG2d TKG3d TKGeomBase TKGeomAlgo
    TKBRep TKTopAlgo TKPrim TKShHealing TKBO TKFillet TKOffset TKMesh
    ${_OCCT_STEP} ${_OCCT_STL} ${_OCCT_IGES} ${_OCCT_RWMESH} ${_OCCT_OBJ}
    ${_OCCT_XSBASE} ${_OCCT_XCAF} TKLCAF TKCAF
    ${_OCCT_XDESTEP} ${_OCCT_XDEIGES}
)

target_link_libraries(hobbycad-lib
    PRIVATE
        ${HOBBYCAD_OCCT_LIBS}
        nlohmann_json::nlohmann_json
)

# backtrace()/backtrace_symbols() are in libc on glibc, but on the BSDs they
# live in a separate libexecinfo. crashhandler.cpp guards the include with
# __has_include(<execinfo.h>), which SUCCEEDS on FreeBSD because the header is
# installed, so the code compiles and then fails at link with
# "undefined symbol: backtrace". Testing for the header answers the wrong
# question; test for the symbol and link the library when it is separate.
include(CheckCXXSymbolExists)
check_cxx_symbol_exists(backtrace "execinfo.h" HOBBYCAD_BACKTRACE_IN_LIBC)
if(NOT HOBBYCAD_BACKTRACE_IN_LIBC)
    find_library(HOBBYCAD_EXECINFO_LIBRARY execinfo)
    if(HOBBYCAD_EXECINFO_LIBRARY)
        target_link_libraries(hobbycad-lib PRIVATE ${HOBBYCAD_EXECINFO_LIBRARY})
        message(STATUS "backtrace(): linking ${HOBBYCAD_EXECINFO_LIBRARY}")
    else()
        message(STATUS "backtrace(): not in libc and no libexecinfo; "
                       "crash reports will omit the stack")
    endif()
endif()

# Conditionally link Qt when available
if(HOBBYCAD_LIB_HAS_QT)
    target_link_libraries(hobbycad-lib
        PUBLIC
            Qt6::Core
            Qt6::Gui
            Qt6::OpenGL
    )
    # qtimageformats: runtime plugin for additional formats (WebP, TIFF, etc.)
    # No linking needed; Qt discovers imageformat plugins automatically at runtime.
    # We just check whether the WebP plugin is available and report status.
    # The plugin is libqwebp.so on Linux, qwebp.dll on Windows (MSYS2 keeps
    # plugins under share/qt6) and libqwebp.dylib on macOS.
    find_file(_WEBP_PLUGIN NAMES libqwebp.so qwebp.dll libqwebp.dylib
        PATHS "${QT6_INSTALL_PREFIX}/plugins/imageformats"
              "${QT6_INSTALL_PREFIX}/share/qt6/plugins/imageformats"
              "/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}/qt6/plugins/imageformats"
        NO_DEFAULT_PATH)
    if(_WEBP_PLUGIN)
        message(STATUS "libhobbycad: Qt ${Qt6_VERSION} enabled (JSON, image, OpenGL support)")
        message(STATUS "libhobbycad: Qt WebP plugin found, WebP image support enabled")
    else()
        message(STATUS "libhobbycad: Qt ${Qt6_VERSION} enabled (JSON, image, OpenGL support)")
        message(STATUS "libhobbycad: Qt WebP plugin NOT found; install qtimageformats for WebP")
        message(STATUS "  Ubuntu: sudo apt install qt6-image-formats-plugins")
    endif()
    unset(_WEBP_PLUGIN CACHE)
else()
    message(STATUS "libhobbycad: Qt not found, building with nlohmann/json fallback")

    # stb_image: header-only image loader for background image support.
    # Only needed without Qt (the Qt path uses QImage/QImageReader).
    # Provides: image_buffer.cpp with stb_image decoding.
    find_path(STB_INCLUDE_DIR stb/stb_image.h
        HINTS /usr/include /usr/local/include)
    if(STB_INCLUDE_DIR)
        target_sources(hobbycad-lib PRIVATE image_buffer.cpp)
        target_include_directories(hobbycad-lib SYSTEM PRIVATE ${STB_INCLUDE_DIR})
        target_compile_definitions(hobbycad-lib PRIVATE HOBBYCAD_HAS_STB_IMAGE=1)
        # stb_image is header-only and carries no version macro, so the fact
        # of its presence is all that can honestly be reported.
        set(HOBBYCAD_ABOUT_STB "present" PARENT_SCOPE)
        message(STATUS "libhobbycad: stb_image found, background image support enabled")
    else()
        message(STATUS "libhobbycad: stb_image not found, background image loading disabled")
        message(STATUS "  Install: sudo apt install libstb-dev")
    endif()

    # libwebp: optional WebP image format support.
    # Only needed without Qt (the Qt path can use qtimageformats for WebP).
    if(PkgConfig_FOUND)
        pkg_check_modules(LIBWEBP IMPORTED_TARGET libwebp)
    endif()
    if(LIBWEBP_FOUND)
        target_link_libraries(hobbycad-lib PRIVATE PkgConfig::LIBWEBP)
        target_compile_definitions(hobbycad-lib PRIVATE HOBBYCAD_HAS_WEBP=1)
        set(HOBBYCAD_ABOUT_WEBP "${LIBWEBP_VERSION}" PARENT_SCOPE)
        message(STATUS "libhobbycad: libwebp found, WebP image support enabled")
    else()
        message(STATUS "libhobbycad: libwebp not found, WebP support disabled")
        message(STATUS "  Install: sudo apt install libwebp-dev")
    endif()

    # Link EGL for headless OpenGL probing without Qt
    if(HOBBYCAD_HAS_EGL)
        target_link_libraries(hobbycad-lib PRIVATE OpenGL::EGL OpenGL::GL)
        target_compile_definitions(hobbycad-lib PRIVATE HOBBYCAD_HAS_EGL=1)
        set(HOBBYCAD_ABOUT_EGL "present" PARENT_SCOPE)
    endif()
endif()

# Link libslvs (required for Phase 1)
if(SLVS_TARGET)
    target_link_libraries(hobbycad-lib PRIVATE ${SLVS_TARGET})
endif()
target_compile_definitions(hobbycad-lib PRIVATE HAVE_SLVS)
# The libslvs fatal-error extension advertises itself in slvs.h
# (SLVS_HAS_FATAL_ERROR_HANDLER, SLVS_RESULT_INTERNAL_ERROR), so no
# build-time probe is needed; see sketch/solver.cpp.  Report what we got:
if(EXISTS "${SLVS_INCLUDE_DIR}/slvs.h")
    file(READ "${SLVS_INCLUDE_DIR}/slvs.h" _slvs_hdr)
elseif(SLVS_INCLUDE_DIRS)
    file(READ "${SLVS_INCLUDE_DIRS}/slvs.h" _slvs_hdr)
else()
    set(_slvs_hdr "")
endif()
if(_slvs_hdr MATCHES "SLVS_RESULT_INTERNAL_ERROR")
    message(STATUS "libhobbycad: libslvs can recover from solver faults")
elseif(_slvs_hdr MATCHES "SLVS_HAS_FATAL_ERROR_HANDLER")
    message(STATUS "libhobbycad: libslvs reports faults but cannot recover")
else()
    message(STATUS "libhobbycad: stock libslvs; a solver fault ends the process")
endif()
if(_slvs_hdr MATCHES "SLVS_HAS_DRAG_WEIGHTS")
    # The fork's per-point drag-weight extension advertises itself in slvs.h.
    # solver.cpp already sees it via the header; propagate the macro PUBLIC so the
    # GUI TUs (which never include slvs.h) also compile their weight paths.
    target_compile_definitions(hobbycad-lib PUBLIC SLVS_HAS_DRAG_WEIGHTS=1)
    message(STATUS "libhobbycad: libslvs supports per-point drag weights")
endif()
unset(_slvs_hdr)

# Shared library: export symbols
if(HOBBYCAD_CORE_LINKAGE STREQUAL "SHARED")
    target_compile_definitions(hobbycad-lib
        PUBLIC  HOBBYCAD_SHARED
        PRIVATE HOBBYCAD_BUILDING
    )
    set_target_properties(hobbycad-lib PROPERTIES
        VERSION   ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
    )
endif()

# ---- Install rules ---------------------------------------------------

install(TARGETS hobbycad-lib
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY hobbycad/
    DESTINATION include/hobbycad
    FILES_MATCHING PATTERN "*.h"
)

