# arm64-windows-static-md.cmake
#
# Static libraries linked against the dynamic MSVC runtime (/MD).
# Cross-compiled from x64 host to arm64 target.

set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

# ARM64 cross-compilation linker flags
# Without these, the linker defaults to x64 even when compiling for ARM64
set(VCPKG_LINKER_FLAGS "/MACHINE:ARM64")

# Chainload toolchain to force static linker to use ARM64
# VCPKG_CMAKE_CONFIGURE_OPTIONS doesn't reliably set CMAKE_STATIC_LINKER_FLAGS
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/arm64-toolchain.cmake")

