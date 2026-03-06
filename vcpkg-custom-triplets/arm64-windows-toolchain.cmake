# arm64-windows-toolchain.cmake
#
# Custom toolchain for ARM64 Windows cross-compilation.
# Adds /MACHINE:ARM64 to the static linker flags to prevent LNK1112 errors.
#
# This toolchain is chainloaded via VCPKG_CHAINLOAD_TOOLCHAIN_FILE, which means
# vcpkg's default Windows toolchain is already loaded. We only need to add our
# additional flags here.

# Force static linker (lib.exe) to target ARM64
# Without this, lib.exe defaults to x64 when cross-compiling on an x64 host
string(APPEND CMAKE_STATIC_LINKER_FLAGS_INIT " /MACHINE:ARM64 ")
string(APPEND CMAKE_STATIC_LINKER_FLAGS_RELEASE_INIT " /MACHINE:ARM64 ")
string(APPEND CMAKE_STATIC_LINKER_FLAGS_DEBUG_INIT " /MACHINE:ARM64 ")
