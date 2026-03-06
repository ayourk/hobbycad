# arm64-windows-toolchain.cmake
#
# Custom toolchain for ARM64 Windows cross-compilation.
# Chainloads the vcpkg Windows toolchain and adds /MACHINE:ARM64
# to the static linker flags to prevent LNK1112 errors.

# Include the standard vcpkg Windows toolchain
include("${VCPKG_ROOT}/scripts/toolchains/windows.cmake")

# Force static linker (lib.exe) to target ARM64
# Without this, lib.exe defaults to x64 when cross-compiling on an x64 host
string(APPEND CMAKE_STATIC_LINKER_FLAGS_INIT " /MACHINE:ARM64 ")
string(APPEND CMAKE_STATIC_LINKER_FLAGS_RELEASE_INIT " /MACHINE:ARM64 ")
string(APPEND CMAKE_STATIC_LINKER_FLAGS_DEBUG_INIT " /MACHINE:ARM64 ")
