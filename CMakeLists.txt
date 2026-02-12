# =====================================================================
#  CMakeLists.txt — HobbyCAD Top-Level Build
# =====================================================================
#
#  Top-level CMakeLists.txt
#
#  Build targets:
#    libhobbycad   — Reusable core library (geometry, file I/O)
#    hobbycad      — Application executable (GUI + CLI modes)
#
#  Usage:
#    cmake --preset linux-debug
#    cmake --build --preset linux-debug
#    ./build/src/hobbycad/hobbycad
#
#  Available presets: linux-debug, linux-release, linux-appimage,
#    macos-debug, macos-release, msys2-debug, msys2-release,
#    msvc-debug, msvc-release, linux-translations, macos-translations
#
#  See CMakePresets.json for full list. Manual configure:
#    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
#
#  Options:
#    -DHOBBYCAD_CORE_LINKAGE=STATIC|SHARED|OBJECT  (default: STATIC)
#
# =====================================================================

cmake_minimum_required(VERSION 3.20)

project(HobbyCAD
    VERSION   0.0.1
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for IDE integration
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Enable Qt's Meta-Object Compiler for signals/slots
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ---- Build options --------------------------------------------------

set(HOBBYCAD_CORE_LINKAGE "STATIC" CACHE STRING
    "Core library linkage type: STATIC, SHARED, or OBJECT")
set_property(CACHE HOBBYCAD_CORE_LINKAGE PROPERTY
    STRINGS "STATIC" "SHARED" "OBJECT")

# Validate the linkage option
if(NOT HOBBYCAD_CORE_LINKAGE MATCHES "^(STATIC|SHARED|OBJECT)$")
    message(FATAL_ERROR
        "Invalid HOBBYCAD_CORE_LINKAGE: '${HOBBYCAD_CORE_LINKAGE}'. "
        "Must be STATIC, SHARED, or OBJECT.")
endif()

message(STATUS "HobbyCAD ${PROJECT_VERSION}")
message(STATUS "Core library linkage: ${HOBBYCAD_CORE_LINKAGE}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# ---- Platform: silence macOS OpenGL deprecation warnings ------------

if(APPLE)
    add_definitions(-DGL_SILENCE_DEPRECATION)
endif()

# ---- Platform: expose POSIX math constants on Windows ---------------
#  OCCT headers use M_PI / M_PI_2 which are POSIX extensions, not
#  defined by default under strict C++ on MSVC or MinGW.

if(WIN32)
    add_compile_definitions(_USE_MATH_DEFINES)
endif()

# ---- Subdirectories -------------------------------------------------

add_subdirectory(src/libhobbycad)
add_subdirectory(src/hobbycad)

