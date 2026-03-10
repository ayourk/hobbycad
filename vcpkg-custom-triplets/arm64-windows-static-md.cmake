# arm64-windows-static-md.cmake
#
# Static libraries linked against the dynamic MSVC runtime (/MD).
# Native ARM64 build on windows-11-arm runner.

set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_BUILD_TYPE release)

# Force lib.exe to target ARM64 (even on native ARM64, lib.exe defaults to x64)
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/arm64-windows-toolchain.cmake")

