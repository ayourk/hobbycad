# arm64-osx-static.cmake
#
# Static libraries for Apple Silicon (arm64) macOS.
# All third-party dependencies are statically linked; only macOS
# system frameworks (AppKit, CoreFoundation, OpenGL, etc.) remain
# dynamic.

set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_OSX_ARCHITECTURES arm64)
set(VCPKG_OSX_DEPLOYMENT_TARGET "11.0")
