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

# Force static linker to use ARM64 machine type
# lib.exe defaults to x64 when cross-compiling, causing LNK1112 errors
set(VCPKG_CMAKE_CONFIGURE_OPTIONS "-DCMAKE_STATIC_LINKER_FLAGS=/MACHINE:ARM64")

