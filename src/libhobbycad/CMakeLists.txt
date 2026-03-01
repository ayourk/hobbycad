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

# nlohmann/json — header-only JSON library used as a Qt-free fallback
# for project serialization, parameter import/export, etc.
find_package(nlohmann_json CONFIG REQUIRED)

# Optional EGL for headless OpenGL probing without Qt
set(HOBBYCAD_HAS_EGL FALSE)
if(NOT HOBBYCAD_LIB_HAS_QT)
    find_package(OpenGL COMPONENTS EGL QUIET)
    if(OpenGL_EGL_FOUND)
        set(HOBBYCAD_HAS_EGL TRUE)
        message(STATUS "libhobbycad: EGL found — OpenGL probing enabled without Qt")
    else()
        message(STATUS "libhobbycad: EGL not found — OpenGL probing will return stub data")
    endif()
endif()

# Required: libslvs for constraint solving (Phase 1)
# Try pkg-config first, then CMake config, then manual detection.
include(FindPkgConfig OPTIONAL)
set(HAVE_SLVS FALSE)
set(SLVS_TARGET "")

# Method 1: pkg-config (try libslvs first, then slvs)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(SLVS IMPORTED_TARGET libslvs)
    if(NOT SLVS_FOUND)
        pkg_check_modules(SLVS IMPORTED_TARGET slvs)
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
    project.cpp
    # Geometry module
    geometry/types.cpp
    geometry/intersections.cpp
    geometry/utils.cpp
    geometry/algorithms.cpp
    # Sketch module
    sketch/entity.cpp
    sketch/constraint.cpp
    sketch/operations.cpp
    sketch/patterns.cpp
    sketch/profiles.cpp
    sketch/solver.cpp
    sketch/queries.cpp
    sketch/export.cpp
    sketch/background.cpp
    sketch/parsing.cpp
    sketch/group.cpp
    sketch/undo.cpp
    sketch/snap.cpp
    sketch/decomposition.cpp
    # BREP module
    brep/operations.cpp
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
    hobbycad/project.h
    hobbycad/base64.h
    hobbycad/image_buffer.h
    # Geometry module
    hobbycad/geometry/types.h
    hobbycad/geometry/intersections.h
    hobbycad/geometry/utils.h
    hobbycad/geometry/algorithms.h
    # Sketch module
    hobbycad/sketch/entity.h
    hobbycad/sketch/constraint.h
    hobbycad/sketch/operations.h
    hobbycad/sketch/patterns.h
    hobbycad/sketch/profiles.h
    hobbycad/sketch/solver.h
    hobbycad/sketch/queries.h
    hobbycad/sketch/export.h
    hobbycad/sketch/background.h
    hobbycad/sketch/group.h
    hobbycad/sketch/undo.h
    hobbycad/sketch/snap.h
    hobbycad/sketch/decomposition.h
    # BREP module
    hobbycad/brep/operations.h
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
    SYSTEM PRIVATE
        ${OpenCASCADE_INCLUDE_DIR}
)

# Suppress deprecation warnings from OCCT headers (std::iterator in
# NCollection_StlIterator.hxx).  These are upstream issues we can't fix.
target_compile_options(hobbycad-lib PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wno-deprecated-declarations>
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

# TKXSBase was removed in 7.8+; only link if present.
set(_OCCT_XSBASE "")
if("TKXSBase" IN_LIST OpenCASCADE_DataExchange_LIBRARIES OR TARGET TKXSBase)
    set(_OCCT_XSBASE TKXSBase)
endif()

set(HOBBYCAD_OCCT_LIBS
    TKernel TKMath TKG2d TKG3d TKGeomBase TKGeomAlgo
    TKBRep TKTopAlgo TKPrim TKShHealing TKBO TKFillet TKOffset TKMesh
    ${_OCCT_STEP} ${_OCCT_STL} ${_OCCT_IGES} ${_OCCT_RWMESH}
    ${_OCCT_XSBASE} ${_OCCT_XCAF} TKLCAF TKCAF
    ${_OCCT_XDESTEP} ${_OCCT_XDEIGES}
)

target_link_libraries(hobbycad-lib
    PRIVATE
        ${HOBBYCAD_OCCT_LIBS}
        nlohmann_json::nlohmann_json
)

# Conditionally link Qt when available
if(HOBBYCAD_LIB_HAS_QT)
    target_link_libraries(hobbycad-lib
        PUBLIC
            Qt6::Core
            Qt6::Gui
            Qt6::OpenGL
    )
    # qtimageformats — runtime plugin for additional formats (WebP, TIFF, etc.)
    # No linking needed; Qt discovers imageformat plugins automatically at runtime.
    # We just check whether the WebP plugin is available and report status.
    find_file(_WEBP_PLUGIN libqwebp.so
        PATHS "${QT6_INSTALL_PREFIX}/plugins/imageformats"
              "/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}/qt6/plugins/imageformats"
        NO_DEFAULT_PATH)
    if(_WEBP_PLUGIN)
        message(STATUS "libhobbycad: Qt ${Qt6_VERSION} enabled (JSON, image, OpenGL support)")
        message(STATUS "libhobbycad: Qt WebP plugin found — WebP image support enabled")
    else()
        message(STATUS "libhobbycad: Qt ${Qt6_VERSION} enabled (JSON, image, OpenGL support)")
        message(STATUS "libhobbycad: Qt WebP plugin NOT found — install qtimageformats for WebP")
        message(STATUS "  Ubuntu: sudo apt install qt6-image-formats-plugins")
    endif()
    unset(_WEBP_PLUGIN CACHE)
else()
    message(STATUS "libhobbycad: Qt not found — building with nlohmann/json fallback")

    # stb_image — header-only image loader for background image support.
    # Only needed without Qt (the Qt path uses QImage/QImageReader).
    # Provides: image_buffer.cpp with stb_image decoding.
    find_path(STB_INCLUDE_DIR stb/stb_image.h
        HINTS /usr/include /usr/local/include)
    if(STB_INCLUDE_DIR)
        target_sources(hobbycad-lib PRIVATE image_buffer.cpp)
        target_include_directories(hobbycad-lib SYSTEM PRIVATE ${STB_INCLUDE_DIR})
        target_compile_definitions(hobbycad-lib PRIVATE HOBBYCAD_HAS_STB_IMAGE=1)
        message(STATUS "libhobbycad: stb_image found — background image support enabled")
    else()
        message(STATUS "libhobbycad: stb_image not found — background image loading disabled")
        message(STATUS "  Install: sudo apt install libstb-dev")
    endif()

    # libwebp — optional WebP image format support.
    # Only needed without Qt (the Qt path can use qtimageformats for WebP).
    if(PkgConfig_FOUND)
        pkg_check_modules(LIBWEBP IMPORTED_TARGET libwebp)
    endif()
    if(LIBWEBP_FOUND)
        target_link_libraries(hobbycad-lib PRIVATE PkgConfig::LIBWEBP)
        target_compile_definitions(hobbycad-lib PRIVATE HOBBYCAD_HAS_WEBP=1)
        message(STATUS "libhobbycad: libwebp found — WebP image support enabled")
    else()
        message(STATUS "libhobbycad: libwebp not found — WebP support disabled")
        message(STATUS "  Install: sudo apt install libwebp-dev")
    endif()

    # Link EGL for headless OpenGL probing without Qt
    if(HOBBYCAD_HAS_EGL)
        target_link_libraries(hobbycad-lib PRIVATE OpenGL::EGL OpenGL::GL)
        target_compile_definitions(hobbycad-lib PRIVATE HOBBYCAD_HAS_EGL=1)
    endif()
endif()

# Link libslvs (required for Phase 1)
if(SLVS_TARGET)
    target_link_libraries(hobbycad-lib PRIVATE ${SLVS_TARGET})
endif()
target_compile_definitions(hobbycad-lib PRIVATE HAVE_SLVS)

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

