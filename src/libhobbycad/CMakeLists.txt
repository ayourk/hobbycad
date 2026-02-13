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
    TKBRep TKTopAlgo TKPrim TKShHealing TKBO TKFillet
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

