# arm64-windows-toolchain.cmake
#
# Custom toolchain for ARM64 Windows builds.
# Adds /MACHINE:ARM64 to the static linker flags to prevent LNK1112 errors.
#
# This toolchain is chainloaded via VCPKG_CHAINLOAD_TOOLCHAIN_FILE.

# Force lib.exe to create ARM64 static libraries
# These flags should not affect compiler detection (which only compiles, not links)
set(CMAKE_STATIC_LINKER_FLAGS "/MACHINE:ARM64" CACHE STRING "" FORCE)
set(CMAKE_STATIC_LINKER_FLAGS_RELEASE "/MACHINE:ARM64" CACHE STRING "" FORCE)
