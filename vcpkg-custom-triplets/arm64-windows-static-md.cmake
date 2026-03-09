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
set(VCPKG_CMAKE_CONFIGURE_OPTIONS "-DCMAKE_STATIC_LINKER_FLAGS=/MACHINE:ARM64")

