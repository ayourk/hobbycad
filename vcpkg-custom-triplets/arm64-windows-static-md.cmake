# arm64-windows-static-md.cmake
#
# Static libraries linked against the dynamic MSVC runtime (/MD).
# Native ARM64 build on windows-11-arm runner.

set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_BUILD_TYPE release)

# Force lib.exe to target ARM64 (even on native ARM64, lib.exe defaults to x64)
# Use VCPKG_CMAKE_CONFIGURE_OPTIONS instead of chainload to avoid breaking compiler detection
set(VCPKG_CMAKE_CONFIGURE_OPTIONS
    "-DCMAKE_STATIC_LINKER_FLAGS=/MACHINE:ARM64"
    "-DCMAKE_STATIC_LINKER_FLAGS_RELEASE=/MACHINE:ARM64"
    "-DCMAKE_STATIC_LINKER_FLAGS_DEBUG=/MACHINE:ARM64"
)

