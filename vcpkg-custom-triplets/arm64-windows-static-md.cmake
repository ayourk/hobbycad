# arm64-windows-static-md.cmake
#
# Static libraries linked against the dynamic MSVC runtime (/MD).
# Cross-compiled from x64 host to arm64 target.
#
# Note: Requires MSVC developer environment with arch=amd64_arm64
# and vcpkg --host-triplet=x64-windows for cross-compilation.

set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_BUILD_TYPE release)

# Force lib.exe to target ARM64 (even on native ARM64, lib.exe defaults to x64)
# The toolchain skips these flags during compiler detection to avoid breaking it
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/arm64-windows-toolchain.cmake")

