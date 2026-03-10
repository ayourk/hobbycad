# arm64-windows-static-md.cmake
#
# Static libraries linked against the dynamic MSVC runtime (/MD).
# Native ARM64 build on windows-11-arm runner.

set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_BUILD_TYPE release)

# Force lib.exe to target ARM64 using CMAKE_STATIC_LINKER_FLAGS_INIT
# The _INIT variant sets the initial cache value and is harder to override
set(VCPKG_CMAKE_CONFIGURE_OPTIONS
    "-DCMAKE_STATIC_LINKER_FLAGS_INIT=/MACHINE:ARM64"
)

