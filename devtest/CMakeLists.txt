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
#  Linux:
#    cd devtest
#    cmake -B build
#    cmake --build build -j$(nproc)
#    ./build/depcheck
#
#  Windows (vcpkg):
#    cd devtest
#    cmake -B build -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
#    cmake --build build
#    build\depcheck.exe
#
#  macOS (Homebrew):
#    cd devtest
#    cmake -B build -DQt6_DIR=$(brew --prefix qt@6)/lib/cmake/Qt6
#    cmake --build build -j$(sysctl -n hw.ncpu)
#    ./build/depcheck
#
# =====================================================================

cmake_minimum_required(VERSION 3.20)
project(HobbyCAD_DepCheck LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- Log file -------------------------------------------------------
#
# devtest.log is written in the source directory (where the user
# runs cmake from).  CMake writes the configure-time portion,
# then depcheck appends the runtime results.

set(DEPCHECK_LOG "${CMAKE_CURRENT_SOURCE_DIR}/devtest.log")

# Start the log fresh each configure run
string(TIMESTAMP _configure_time "%Y-%m-%d %H:%M:%S" UTC)
file(WRITE "${DEPCHECK_LOG}"
    "===== devtest.log =====\n"
    "Started: ${_configure_time} UTC\n"
    "\n"
    "Generator:  ${CMAKE_GENERATOR}\n"
    "Build type: ${CMAKE_BUILD_TYPE}\n"
    "Source dir: ${CMAKE_CURRENT_SOURCE_DIR}\n"
    "Build dir:  ${CMAKE_CURRENT_BINARY_DIR}\n"
    "\n"
    "--- CMake Configure ---\n\n")

# Helper macro: log a message and also print it to stdout
macro(depcheck_log msg)
    message(STATUS "${msg}")
    file(APPEND "${DEPCHECK_LOG}" "  ${msg}\n")
endmacro()

# ---- Platform detection ---------------------------------------------

if(WIN32)
    set(HOBBYCAD_PLATFORM "windows")
elseif(APPLE)
    set(HOBBYCAD_PLATFORM "macos")
else()
    set(HOBBYCAD_PLATFORM "linux")
endif()

depcheck_log("Platform: ${HOBBYCAD_PLATFORM}")
depcheck_log("CMake version: ${CMAKE_VERSION}")
depcheck_log("Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
depcheck_log("Compiler path: ${CMAKE_CXX_COMPILER}")

# ---- Generator check ---------------------------------------------------
#
# HobbyCAD uses Ninja for the main build.  devtest works with any
# generator but warns if Ninja is not being used.

depcheck_log("Generator: ${CMAKE_GENERATOR}")
if(NOT CMAKE_GENERATOR MATCHES "Ninja")
    depcheck_log("[WARN] Generator is '${CMAKE_GENERATOR}', not Ninja.")
    depcheck_log("       HobbyCAD uses Ninja for its build system.")
    depcheck_log("       Install Ninja and re-run with: cmake -B build -G Ninja")
    depcheck_log("       Continuing with ${CMAKE_GENERATOR}.")
endif()

# PkgConfig is optional — available on Linux and macOS (Homebrew),
# typically absent on Windows.  Used as a fallback when CMake config
# files are not shipped by a library.
find_package(PkgConfig QUIET)

# ---- Phase 0: Foundation (required) ---------------------------------

find_package(OpenCASCADE REQUIRED)
depcheck_log("Found OpenCASCADE: ${OpenCASCADE_INCLUDE_DIR}")

find_package(Qt6 6.2 REQUIRED COMPONENTS
    Core Gui Widgets OpenGL OpenGLWidgets Svg)
depcheck_log("Found Qt6: ${Qt6_VERSION}")

find_package(OpenGL REQUIRED)
depcheck_log("Found OpenGL: ${OPENGL_gl_LIBRARY}")

# libgit2 — try CMake config first (vcpkg ships one), fall back
# to pkg-config on Linux/macOS.
find_package(libgit2 QUIET)
if(NOT libgit2_FOUND)
    if(PkgConfig_FOUND)
        pkg_check_modules(LIBGIT2 REQUIRED IMPORTED_TARGET libgit2)
        set(LIBGIT2_TARGET PkgConfig::LIBGIT2)
        depcheck_log("Found libgit2: ${LIBGIT2_VERSION} (pkg-config)")
    else()
        message(FATAL_ERROR
            "libgit2 not found.  Install libgit2-dev (Linux), "
            "libgit2 via vcpkg (Windows), or libgit2 via Homebrew (macOS).")
    endif()
else()
    # vcpkg provides libgit2::libgit2 as the imported target
    set(LIBGIT2_TARGET libgit2::libgit2)
    depcheck_log("Found libgit2: (cmake config)")
endif()

# libzip — On Linux, the CMake config shipped by libzip-dev references
# companion tool binaries (zipcmp, zipmerge, ziptool) that live in a
# separate package.  If those tools are not installed, find_package()
# fails even though the library itself is present.  To avoid this,
# prefer pkg-config on Linux/macOS.  On Windows, vcpkg's CMake config
# works correctly.
set(LIBZIP_TARGET "")
if(PkgConfig_FOUND)
    pkg_check_modules(LIBZIP IMPORTED_TARGET libzip)
    if(LIBZIP_FOUND)
        set(LIBZIP_TARGET PkgConfig::LIBZIP)
        depcheck_log("Found libzip: ${LIBZIP_VERSION} (pkg-config)")
    endif()
endif()
if(NOT LIBZIP_TARGET)
    find_package(libzip QUIET)
    if(libzip_FOUND)
        set(LIBZIP_TARGET libzip::zip)
        depcheck_log("Found libzip: (cmake config)")
    endif()
endif()
if(NOT LIBZIP_TARGET)
    message(FATAL_ERROR
        "libzip not found.  Install libzip-dev (Linux), "
        "libzip via vcpkg (Windows), or libzip via Homebrew (macOS).")
endif()

# TBB — OCCT links against TBB for parallelism.  On Linux the system
# OCCT package requires libtbb-dev at link time.  On Windows/macOS
# vcpkg/Homebrew OCCT may bundle TBB or not need it explicitly.
find_package(TBB QUIET)
if(NOT TBB_FOUND AND PkgConfig_FOUND)
    pkg_check_modules(TBB IMPORTED_TARGET tbb)
endif()
if(TBB_FOUND)
    depcheck_log("Found TBB")
else()
    depcheck_log("TBB not found (may be bundled with OCCT)")
endif()

# ---- Phase 1: Basic Modeling (optional) ------------------------------

# libslvs — PPA package on Linux, must build from source on
# Windows/macOS.  Try pkg-config first, then CMake config.
set(HAVE_SLVS FALSE)
if(PkgConfig_FOUND)
    pkg_check_modules(SLVS IMPORTED_TARGET slvs)
    if(SLVS_FOUND)
        set(HAVE_SLVS TRUE)
    endif()
endif()
if(NOT HAVE_SLVS)
    find_package(slvs QUIET)
    if(slvs_FOUND)
        set(HAVE_SLVS TRUE)
    endif()
endif()
if(HAVE_SLVS)
    add_compile_definitions(HAVE_SLVS)
    depcheck_log("Found libslvs (Phase 1)")
else()
    if(HOBBYCAD_PLATFORM STREQUAL "linux")
        depcheck_log(
            "libslvs not found (Phase 1) — "
            "install libslvs-dev from ppa:ayourk/hobbycad")
    else()
        depcheck_log(
            "libslvs not found (Phase 1) — "
            "build from source: https://github.com/solvespace/solvespace")
    endif()
endif()

# ---- Phase 3: Python / Plugins / Version Control (optional) ----------

find_package(Python3 COMPONENTS Interpreter Development QUIET)
find_package(pybind11 QUIET)
if(pybind11_FOUND AND Python3_FOUND)
    set(HAVE_PYBIND11 TRUE)
    add_compile_definitions(HAVE_PYBIND11)
    depcheck_log("Found pybind11: ${pybind11_VERSION}, Python: ${Python3_VERSION} (Phase 3)")
else()
    if(HOBBYCAD_PLATFORM STREQUAL "linux")
        depcheck_log(
            "pybind11/Python3 not found (Phase 3) — "
            "install pybind11-dev python3-dev")
    elseif(HOBBYCAD_PLATFORM STREQUAL "windows")
        depcheck_log(
            "pybind11/Python3 not found (Phase 3) — "
            "vcpkg install pybind11:x64-windows")
    else()
        depcheck_log(
            "pybind11/Python3 not found (Phase 3) — "
            "brew install pybind11 python")
    endif()
endif()

# ---- Phase 5: HobbyMesh (optional) ----------------------------------

# OpenMesh
find_package(OpenMesh QUIET)
if(NOT OpenMesh_FOUND AND PkgConfig_FOUND)
    pkg_check_modules(OPENMESH IMPORTED_TARGET OpenMesh)
    if(OPENMESH_FOUND)
        set(OpenMesh_FOUND TRUE)
    endif()
endif()
if(OpenMesh_FOUND)
    set(HAVE_OPENMESH TRUE)
    add_compile_definitions(HAVE_OPENMESH)
    depcheck_log("Found OpenMesh (Phase 5)")
else()
    if(HOBBYCAD_PLATFORM STREQUAL "linux")
        depcheck_log(
            "OpenMesh not found (Phase 5) — "
            "install libopenmesh-dev from ppa:ayourk/hobbycad")
    else()
        depcheck_log(
            "OpenMesh not found (Phase 5) — "
            "build from source: https://www.graphics.rwth-aachen.de/software/openmesh/")
    endif()
endif()

# lib3mf
find_package(lib3mf QUIET)
if(NOT lib3mf_FOUND AND PkgConfig_FOUND)
    pkg_check_modules(LIB3MF IMPORTED_TARGET lib3mf)
    if(LIB3MF_FOUND)
        set(lib3mf_FOUND TRUE)
    endif()
endif()
if(lib3mf_FOUND)
    set(HAVE_LIB3MF TRUE)
    add_compile_definitions(HAVE_LIB3MF)
    depcheck_log("Found lib3mf (Phase 5)")
else()
    if(HOBBYCAD_PLATFORM STREQUAL "linux")
        depcheck_log(
            "lib3mf not found (Phase 5) — "
            "install lib3mf-dev from ppa:ayourk/hobbycad")
    else()
        depcheck_log(
            "lib3mf not found (Phase 5) — "
            "build from source: https://github.com/3MFConsortium/lib3mf")
    endif()
endif()

# MeshFix
set(HAVE_MESHFIX FALSE)
if(PkgConfig_FOUND)
    pkg_check_modules(MESHFIX IMPORTED_TARGET meshfix)
    if(MESHFIX_FOUND)
        set(HAVE_MESHFIX TRUE)
    endif()
endif()
if(NOT HAVE_MESHFIX)
    find_package(meshfix QUIET)
    if(meshfix_FOUND)
        set(HAVE_MESHFIX TRUE)
    endif()
endif()
if(HAVE_MESHFIX)
    add_compile_definitions(HAVE_MESHFIX)
    depcheck_log("Found MeshFix (Phase 5)")
else()
    if(HOBBYCAD_PLATFORM STREQUAL "linux")
        depcheck_log(
            "MeshFix not found (Phase 5) — "
            "install libmeshfix-dev from ppa:ayourk/hobbycad")
    else()
        depcheck_log(
            "MeshFix not found (Phase 5) — "
            "build from source: https://github.com/MarcoAttene/MeshFix-V2.1")
    endif()
endif()

# CGAL
find_package(CGAL QUIET)
if(CGAL_FOUND)
    set(HAVE_CGAL TRUE)
    add_compile_definitions(HAVE_CGAL)
    depcheck_log("Found CGAL (Phase 5)")
else()
    if(HOBBYCAD_PLATFORM STREQUAL "linux")
        depcheck_log("CGAL not found (Phase 5) — sudo apt install libcgal-dev")
    elseif(HOBBYCAD_PLATFORM STREQUAL "windows")
        depcheck_log("CGAL not found (Phase 5) — vcpkg install cgal:x64-windows")
    else()
        depcheck_log("CGAL not found (Phase 5) — brew install cgal")
    endif()
endif()

# OpenVDB
find_package(OpenVDB QUIET)
if(OpenVDB_FOUND)
    set(HAVE_OPENVDB TRUE)
    add_compile_definitions(HAVE_OPENVDB)
    depcheck_log("Found OpenVDB (Phase 5)")
else()
    if(HOBBYCAD_PLATFORM STREQUAL "linux")
        depcheck_log("OpenVDB not found (Phase 5) — sudo apt install libopenvdb-dev")
    elseif(HOBBYCAD_PLATFORM STREQUAL "windows")
        depcheck_log("OpenVDB not found (Phase 5) — vcpkg install openvdb:x64-windows")
    else()
        depcheck_log("OpenVDB not found (Phase 5) — brew install openvdb")
    endif()
endif()

# Assimp
find_package(assimp QUIET)
if(NOT assimp_FOUND AND PkgConfig_FOUND)
    pkg_check_modules(ASSIMP IMPORTED_TARGET assimp)
    if(ASSIMP_FOUND)
        set(assimp_FOUND TRUE)
    endif()
endif()
if(assimp_FOUND)
    set(HAVE_ASSIMP TRUE)
    add_compile_definitions(HAVE_ASSIMP)
    depcheck_log("Found Assimp (Phase 5)")
else()
    if(HOBBYCAD_PLATFORM STREQUAL "linux")
        depcheck_log("Assimp not found (Phase 5) — sudo apt install libassimp-dev")
    elseif(HOBBYCAD_PLATFORM STREQUAL "windows")
        depcheck_log("Assimp not found (Phase 5) — vcpkg install assimp:x64-windows")
    else()
        depcheck_log("Assimp not found (Phase 5) — brew install assimp")
    endif()
endif()

# Eigen
find_package(Eigen3 QUIET)
if(Eigen3_FOUND)
    set(HAVE_EIGEN TRUE)
    add_compile_definitions(HAVE_EIGEN)
    depcheck_log("Found Eigen: ${Eigen3_VERSION} (Phase 5)")
else()
    if(HOBBYCAD_PLATFORM STREQUAL "linux")
        depcheck_log("Eigen not found (Phase 5) — sudo apt install libeigen3-dev")
    elseif(HOBBYCAD_PLATFORM STREQUAL "windows")
        depcheck_log("Eigen not found (Phase 5) — vcpkg install eigen3:x64-windows")
    else()
        depcheck_log("Eigen not found (Phase 5) — brew install eigen")
    endif()
endif()

# ---- Build the test -------------------------------------------------

# Write configure-complete marker to log
file(APPEND "${DEPCHECK_LOG}" "\n--- Configure complete ---\n\n")

add_executable(depcheck depcheck.cpp)

# Pass the log file path to depcheck so it can append runtime results
target_compile_definitions(depcheck PRIVATE
    DEPCHECK_LOG_PATH="${DEPCHECK_LOG}")

target_include_directories(depcheck PRIVATE
    ${OpenCASCADE_INCLUDE_DIR})

# Phase 0 (required)
target_link_libraries(depcheck PRIVATE
    TKernel TKMath TKBRep TKSTEP TKXSBase TKSTL TKIGES
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets
    ${LIBGIT2_TARGET}
    ${LIBZIP_TARGET}
    OpenGL::GL)

# TBB (if found — required by OCCT on some platforms)
if(TBB_FOUND AND TARGET TBB::tbb)
    target_link_libraries(depcheck PRIVATE TBB::tbb)
elseif(TARGET PkgConfig::TBB)
    target_link_libraries(depcheck PRIVATE PkgConfig::TBB)
endif()

# Phase 1
if(HAVE_SLVS)
    if(TARGET PkgConfig::SLVS)
        target_link_libraries(depcheck PRIVATE PkgConfig::SLVS)
    endif()
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
    if(TARGET PkgConfig::MESHFIX)
        target_link_libraries(depcheck PRIVATE PkgConfig::MESHFIX)
    endif()
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

# ---- Post-build: append build results to log ------------------------
#
# This runs after cmake --build completes successfully, capturing
# the build phase in devtest.log between configure and runtime.

# Generate a small CMake script that appends build info to the log.
# Using a script avoids platform-specific shell differences.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/log_build.cmake" "
file(APPEND \"${DEPCHECK_LOG}\"
    \"--- Build complete ---\\n\"
    \"\\n\"
    \"  Binary: \${DEPCHECK_BINARY}\\n\"
    \"  Size:   \")
file(SIZE \"\${DEPCHECK_BINARY}\" _binsize)
math(EXPR _kb \"\${_binsize} / 1024\")
file(APPEND \"${DEPCHECK_LOG}\"
    \"\${_kb} KB\\n\"
    \"\\n\")
")

add_custom_command(TARGET depcheck POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        -DDEPCHECK_BINARY=$<TARGET_FILE:depcheck>
        -P "${CMAKE_CURRENT_BINARY_DIR}/log_build.cmake"
    COMMENT "Logging build results to devtest.log")

# ---- Windows: copy DLLs next to executable --------------------------

if(WIN32)
    get_target_property(_qt_qmake Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin "${_qt_qmake}" DIRECTORY)
    find_program(WINDEPLOYQT windeployqt HINTS "${_qt_bin}")
    if(WINDEPLOYQT)
        add_custom_command(TARGET depcheck POST_BUILD
            COMMAND "${WINDEPLOYQT}" --no-translations "$<TARGET_FILE:depcheck>"
            COMMENT "Running windeployqt...")
    endif()
endif()

