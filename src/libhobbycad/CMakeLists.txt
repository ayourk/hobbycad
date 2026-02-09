# =====================================================================
#  src/libhobbycad/CMakeLists.txt — HobbyCAD Core Library
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

# ---- Source files ----------------------------------------------------

set(LIBHOBBYCAD_SOURCES
    core.cpp
    document.cpp
    brep_io.cpp
    opengl_info.cpp
)

set(LIBHOBBYCAD_HEADERS
    hobbycad/core.h
    hobbycad/document.h
    hobbycad/brep_io.h
    hobbycad/opengl_info.h
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
    PRIVATE
        ${OpenCASCADE_INCLUDE_DIR}
)

# OCCT libraries needed by the core (no visualization)
#
# Note: OCCT 7.6.x uses TKXDESTEP / TKXDEIGES (all-caps suffix).
# OCCT 7.8+ renamed these to TKXDEStep / TKXDEIGES.  We try the
# 7.6 names first and fall back if needed.
#
if(TARGET TKXDESTEP)
    set(_OCCT_XDE_STEP TKXDESTEP)
elseif(TARGET TKXDEStep)
    set(_OCCT_XDE_STEP TKXDEStep)
else()
    # Bare library name fallback — let the linker figure it out
    set(_OCCT_XDE_STEP TKXDESTEP)
endif()

set(HOBBYCAD_OCCT_LIBS
    TKernel TKMath TKG2d TKG3d TKGeomBase TKGeomAlgo
    TKBRep TKTopAlgo TKPrim TKShHealing TKBO TKFillet
    TKSTEP TKSTL TKIGES TKXSBase TKXCAF TKLCAF TKCAF
    ${_OCCT_XDE_STEP} TKXDEIGES
)

target_link_libraries(hobbycad-lib
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::OpenGL
    PRIVATE
        ${HOBBYCAD_OCCT_LIBS}
)

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

