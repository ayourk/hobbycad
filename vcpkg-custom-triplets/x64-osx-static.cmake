# x64-osx-static.cmake
#
# Static libraries for Intel (x86_64) macOS.
# Can be used on native x64 runners or cross-compiled from arm64
# (Apple Clang supports -arch x86_64 on arm64 hosts).

set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_OSX_ARCHITECTURES x86_64)
set(VCPKG_OSX_DEPLOYMENT_TARGET "11.0")
