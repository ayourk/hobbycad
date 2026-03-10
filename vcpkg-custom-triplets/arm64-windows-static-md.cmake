# arm64-windows-static-md.cmake
#
# Static libraries linked against the dynamic MSVC runtime (/MD).
# Native ARM64 build on windows-11-arm runner.

set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_BUILD_TYPE release)

# Use lib.exe wrapper that adds /MACHINE:ARM64
# The wrapper path is passed via LIB_ARM64_WRAPPER environment variable
set(VCPKG_ENV_PASSTHROUGH LIB_ARM64_WRAPPER)
if(DEFINED ENV{LIB_ARM64_WRAPPER})
    set(VCPKG_CMAKE_CONFIGURE_OPTIONS
        "-DCMAKE_AR=$ENV{LIB_ARM64_WRAPPER}"
    )
endif()

