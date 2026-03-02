# arm64-windows-static-md.cmake
#
# Static libraries linked against the dynamic MSVC runtime (/MD).
# Cross-compiled from x64 host to arm64 target.

set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

# Chainload toolchain for ARM64 cross-compilation
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/../vcpkg-toolchains/arm64-windows.cmake")

# Disable PNG support in freetype to work around vcpkg cmake wrapper bug
# (PNG::PNG used without find_dependency, breaks Qt builds)
set(VCPKG_CMAKE_CONFIGURE_OPTIONS_freetype "-DFT_DISABLE_PNG=ON")
