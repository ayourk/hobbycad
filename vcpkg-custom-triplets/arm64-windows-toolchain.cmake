# arm64-windows-toolchain.cmake
#
# Custom toolchain for ARM64 Windows builds.
# Adds /MACHINE:ARM64 to the static linker flags to prevent LNK1112 errors.
#
# This toolchain is chainloaded via VCPKG_CHAINLOAD_TOOLCHAIN_FILE.

# Skip linker flags during vcpkg's compiler detection phase
# Try multiple paths that might contain "detect_compiler"
set(_in_detection FALSE)
if(DEFINED CMAKE_SOURCE_DIR)
    string(FIND "${CMAKE_SOURCE_DIR}" "detect_compiler" _pos)
    if(NOT _pos EQUAL -1)
        set(_in_detection TRUE)
    endif()
endif()
if(DEFINED CMAKE_BINARY_DIR)
    string(FIND "${CMAKE_BINARY_DIR}" "detect_compiler" _pos)
    if(NOT _pos EQUAL -1)
        set(_in_detection TRUE)
    endif()
endif()
if(DEFINED VCPKG_ROOT_DIR)
    # During detection, source is inside vcpkg's scripts/detect_compiler
    string(FIND "${CMAKE_SOURCE_DIR}" "${VCPKG_ROOT_DIR}/scripts" _pos)
    if(NOT _pos EQUAL -1)
        set(_in_detection TRUE)
    endif()
endif()

if(NOT _in_detection)
    # Force static linker (lib.exe) to target ARM64
    set(CMAKE_STATIC_LINKER_FLAGS "/MACHINE:ARM64" CACHE STRING "" FORCE)
    set(CMAKE_STATIC_LINKER_FLAGS_RELEASE "/MACHINE:ARM64" CACHE STRING "" FORCE)
    set(CMAKE_STATIC_LINKER_FLAGS_DEBUG "/MACHINE:ARM64" CACHE STRING "" FORCE)
endif()
