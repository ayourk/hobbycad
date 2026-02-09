# =====================================================================
#  HobbyCAD Dependency Verification
# =====================================================================
#
#  Builds and runs a test program that verifies every HobbyCAD and
#  HobbyMesh dependency across all development phases.
#
#  Phase 0 dependencies are REQUIRED — cmake will fail if they are
#  missing.  All later-phase dependencies are optional and report
#  [WARN] at runtime if absent.
#
#  Usage:
#    cd devtest
#    cmake -B build
#    cmake --build build -j$(nproc)
#    ./build/depcheck
#
# =====================================================================

cmake_minimum_required(VERSION 3.20)
project(HobbyCAD_DepCheck LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- Phase 0: Foundation (required) ---------------------------------

find_package(OpenCASCADE REQUIRED)
find_package(Qt6 6.2 REQUIRED COMPONENTS
    Core Gui Widgets OpenGL OpenGLWidgets Svg)
find_package(OpenGL REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBGIT2 REQUIRED IMPORTED_TARGET libgit2)
pkg_check_modules(LIBZIP  REQUIRED IMPORTED_TARGET libzip)

# ---- Phase 1: Basic Modeling (optional) ------------------------------

pkg_check_modules(SLVS IMPORTED_TARGET slvs)
if(SLVS_FOUND)
    set(HAVE_SLVS TRUE)
    add_compile_definitions(HAVE_SLVS)
else()
    message(STATUS
        "libslvs not found (Phase 1) — "
        "install libslvs-dev from ppa:ayourk/hobbycad")
endif()

# ---- Phase 3: Python / Plugins / Version Control (optional) ----------

find_package(Python3 COMPONENTS Interpreter Development QUIET)
find_package(pybind11 QUIET)
if(pybind11_FOUND AND Python3_FOUND)
    set(HAVE_PYBIND11 TRUE)
    add_compile_definitions(HAVE_PYBIND11)
else()
    message(STATUS
        "pybind11/Python3 not found (Phase 3) — "
        "install pybind11-dev python3-dev")
endif()

# ---- Phase 5: HobbyMesh (optional) ----------------------------------

# OpenMesh
find_package(OpenMesh QUIET)
if(NOT OpenMesh_FOUND)
    pkg_check_modules(OPENMESH IMPORTED_TARGET OpenMesh)
    if(OPENMESH_FOUND)
        set(OpenMesh_FOUND TRUE)
    endif()
endif()
if(OpenMesh_FOUND)
    set(HAVE_OPENMESH TRUE)
    add_compile_definitions(HAVE_OPENMESH)
else()
    message(STATUS
        "OpenMesh not found (Phase 5) — "
        "install libopenmesh-dev from ppa:ayourk/hobbycad")
endif()

# lib3mf
find_package(lib3mf QUIET)
if(NOT lib3mf_FOUND)
    pkg_check_modules(LIB3MF IMPORTED_TARGET lib3mf)
    if(LIB3MF_FOUND)
        set(lib3mf_FOUND TRUE)
    endif()
endif()
if(lib3mf_FOUND)
    set(HAVE_LIB3MF TRUE)
    add_compile_definitions(HAVE_LIB3MF)
else()
    message(STATUS
        "lib3mf not found (Phase 5) — "
        "install lib3mf-dev from ppa:ayourk/hobbycad")
endif()

# MeshFix
pkg_check_modules(MESHFIX IMPORTED_TARGET meshfix)
if(MESHFIX_FOUND)
    set(HAVE_MESHFIX TRUE)
    add_compile_definitions(HAVE_MESHFIX)
else()
    message(STATUS
        "MeshFix not found (Phase 5) — "
        "install libmeshfix-dev from ppa:ayourk/hobbycad")
endif()

# CGAL
find_package(CGAL QUIET)
if(CGAL_FOUND)
    set(HAVE_CGAL TRUE)
    add_compile_definitions(HAVE_CGAL)
else()
    message(STATUS
        "CGAL not found (Phase 5) — "
        "sudo apt install libcgal-dev")
endif()

# OpenVDB
find_package(OpenVDB QUIET)
if(OpenVDB_FOUND)
    set(HAVE_OPENVDB TRUE)
    add_compile_definitions(HAVE_OPENVDB)
else()
    message(STATUS
        "OpenVDB not found (Phase 5) — "
        "sudo apt install libopenvdb-dev")
endif()

# Assimp
find_package(assimp QUIET)
if(NOT assimp_FOUND)
    pkg_check_modules(ASSIMP IMPORTED_TARGET assimp)
    if(ASSIMP_FOUND)
        set(assimp_FOUND TRUE)
    endif()
endif()
if(assimp_FOUND)
    set(HAVE_ASSIMP TRUE)
    add_compile_definitions(HAVE_ASSIMP)
else()
    message(STATUS
        "Assimp not found (Phase 5) — "
        "sudo apt install libassimp-dev")
endif()

# Eigen
find_package(Eigen3 QUIET)
if(Eigen3_FOUND)
    set(HAVE_EIGEN TRUE)
    add_compile_definitions(HAVE_EIGEN)
else()
    message(STATUS
        "Eigen not found (Phase 5) — "
        "sudo apt install libeigen3-dev")
endif()

# ---- Build the test -------------------------------------------------

add_executable(depcheck depcheck.cpp)

target_include_directories(depcheck PRIVATE
    ${OpenCASCADE_INCLUDE_DIR})

# Phase 0 (required)
target_link_libraries(depcheck PRIVATE
    TKernel TKMath TKBRep TKSTEP TKXSBase TKSTL TKIGES
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets
    PkgConfig::LIBGIT2
    PkgConfig::LIBZIP
    OpenGL::GL)

# Phase 1
if(HAVE_SLVS)
    target_link_libraries(depcheck PRIVATE PkgConfig::SLVS)
endif()

# Phase 3
if(HAVE_PYBIND11)
    target_link_libraries(depcheck PRIVATE pybind11::embed)
endif()

# Phase 5
if(HAVE_OPENMESH AND TARGET OpenMeshCore)
    target_link_libraries(depcheck PRIVATE OpenMeshCore)
elseif(HAVE_OPENMESH AND TARGET PkgConfig::OPENMESH)
    target_link_libraries(depcheck PRIVATE PkgConfig::OPENMESH)
endif()

if(HAVE_LIB3MF AND TARGET lib3mf::lib3mf)
    target_link_libraries(depcheck PRIVATE lib3mf::lib3mf)
elseif(HAVE_LIB3MF AND TARGET PkgConfig::LIB3MF)
    target_link_libraries(depcheck PRIVATE PkgConfig::LIB3MF)
endif()

if(HAVE_MESHFIX)
    target_link_libraries(depcheck PRIVATE PkgConfig::MESHFIX)
endif()

if(HAVE_CGAL)
    target_link_libraries(depcheck PRIVATE CGAL::CGAL)
endif()

if(HAVE_OPENVDB)
    target_link_libraries(depcheck PRIVATE OpenVDB::openvdb)
endif()

if(HAVE_ASSIMP AND TARGET assimp::assimp)
    target_link_libraries(depcheck PRIVATE assimp::assimp)
elseif(HAVE_ASSIMP AND TARGET PkgConfig::ASSIMP)
    target_link_libraries(depcheck PRIVATE PkgConfig::ASSIMP)
endif()

if(HAVE_EIGEN)
    target_link_libraries(depcheck PRIVATE Eigen3::Eigen)
endif()
