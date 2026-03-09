# arm64-windows-toolchain.cmake
#
# Custom toolchain for ARM64 Windows builds.
# Adds /MACHINE:ARM64 to the static linker flags to prevent LNK1112 errors.
#
# This toolchain is chainloaded via VCPKG_CHAINLOAD_TOOLCHAIN_FILE.

# Debug: print paths to understand what's happening
message(STATUS "ARM64 toolchain: CMAKE_SOURCE_DIR=${CMAKE_SOURCE_DIR}")
message(STATUS "ARM64 toolchain: CMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}")

# Skip linker flags during vcpkg's compiler detection phase
set(_in_detection FALSE)
if(CMAKE_SOURCE_DIR MATCHES "detect_compiler")
    set(_in_detection TRUE)
endif()
if(CMAKE_BINARY_DIR MATCHES "detect_compiler")
    set(_in_detection TRUE)
endif()

message(STATUS "ARM64 toolchain: _in_detection=${_in_detection}")

if(NOT _in_detection)
    message(STATUS "ARM64 toolchain: Setting /MACHINE:ARM64 flags")
    set(CMAKE_STATIC_LINKER_FLAGS "/MACHINE:ARM64" CACHE STRING "" FORCE)
    set(CMAKE_STATIC_LINKER_FLAGS_RELEASE "/MACHINE:ARM64" CACHE STRING "" FORCE)
    set(CMAKE_STATIC_LINKER_FLAGS_DEBUG "/MACHINE:ARM64" CACHE STRING "" FORCE)
else()
    message(STATUS "ARM64 toolchain: Skipping flags (in detection)")
endif()
