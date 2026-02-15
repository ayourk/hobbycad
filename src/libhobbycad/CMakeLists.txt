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
find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui OpenGL)

# Required: libslvs for constraint solving (Phase 1)
# Try pkg-config first, then CMake config, then manual detection.
include(FindPkgConfig OPTIONAL)
set(HAVE_SLVS FALSE)
set(SLVS_TARGET "")

# Method 1: pkg-config
if(PKG_CONFIG_FOUND)
    pkg_check_modules(SLVS IMPORTED_TARGET slvs)
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
    # BREP module
    brep/operations.cpp
)

set(LIBHOBBYCAD_HEADERS
    hobbycad/core.h
    hobbycad/document.h
    hobbycad/brep_io.h
    hobbycad/opengl_info.h
    hobbycad/project.h
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
    TKBRep TKTopAlgo TKPrim TKShHealing TKBO TKFillet TKOffset
    ${_OCCT_STEP} ${_OCCT_STL} ${_OCCT_IGES}
    ${_OCCT_XSBASE} ${_OCCT_XCAF} TKLCAF TKCAF
    ${_OCCT_XDESTEP} ${_OCCT_XDEIGES}
)

target_link_libraries(hobbycad-lib
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::OpenGL
    PRIVATE
        ${HOBBYCAD_OCCT_LIBS}
)

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

