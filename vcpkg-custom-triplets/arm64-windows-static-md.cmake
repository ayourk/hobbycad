# arm64-windows-static-md.cmake
#
# Static libraries linked against the dynamic MSVC runtime (/MD).
# Cross-compiled from x64 host to arm64 target.

set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_BUILD_TYPE release)

# ARM64 cross-compilation linker flags for dynamic libraries and executables
set(VCPKG_LINKER_FLAGS "/MACHINE:ARM64")

# Use custom toolchain that adds /MACHINE:ARM64 to static linker flags
# This prevents LNK1112 errors where lib.exe defaults to x64 when cross-compiling
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/arm64-windows-toolchain.cmake")

