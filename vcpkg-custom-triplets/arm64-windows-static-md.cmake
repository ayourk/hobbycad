# arm64-windows-static-md.cmake
#
# Static libraries linked against the dynamic MSVC runtime (/MD).
# Native ARM64 build on windows-11-arm runner.

set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_BUILD_TYPE release)

# Pass CMAKE_STATIC_LINKER_FLAGS from environment to force lib.exe to target ARM64
# The workflow sets this environment variable before calling vcpkg
set(VCPKG_ENV_PASSTHROUGH CMAKE_STATIC_LINKER_FLAGS)

