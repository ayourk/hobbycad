# arm64-windows-toolchain.cmake
#
# Custom toolchain for ARM64 Windows builds.
# Adds /MACHINE:ARM64 to the static linker flags to prevent LNK1112 errors.
#
# This toolchain is chainloaded via VCPKG_CHAINLOAD_TOOLCHAIN_FILE.

# Only set linker flags if we're not in vcpkg's compiler detection phase
# Detection creates a minimal test project that fails with extra flags
if(NOT VCPKG_DETECTING_COMPILER)
    # Force static linker (lib.exe) to target ARM64
    set(CMAKE_STATIC_LINKER_FLAGS "/MACHINE:ARM64" CACHE STRING "" FORCE)
    set(CMAKE_STATIC_LINKER_FLAGS_RELEASE "/MACHINE:ARM64" CACHE STRING "" FORCE)
    set(CMAKE_STATIC_LINKER_FLAGS_DEBUG "/MACHINE:ARM64" CACHE STRING "" FORCE)
endif()
