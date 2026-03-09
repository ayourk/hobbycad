# arm64-windows-toolchain.cmake
#
# Custom toolchain for ARM64 Windows builds.
# Adds /MACHINE:ARM64 to the static linker flags to prevent LNK1112 errors.
#
# This toolchain is chainloaded via VCPKG_CHAINLOAD_TOOLCHAIN_FILE.

# Skip linker flags during vcpkg's compiler detection phase
# Detection runs in buildtrees/detect_compiler/ directory
string(FIND "${CMAKE_CURRENT_BINARY_DIR}" "detect_compiler" _detect_pos)
if(_detect_pos EQUAL -1)
    # Force static linker (lib.exe) to target ARM64
    set(CMAKE_STATIC_LINKER_FLAGS "/MACHINE:ARM64" CACHE STRING "" FORCE)
    set(CMAKE_STATIC_LINKER_FLAGS_RELEASE "/MACHINE:ARM64" CACHE STRING "" FORCE)
    set(CMAKE_STATIC_LINKER_FLAGS_DEBUG "/MACHINE:ARM64" CACHE STRING "" FORCE)
endif()
