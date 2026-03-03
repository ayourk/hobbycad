# arm64-toolchain.cmake
#
# Chainload toolchain for ARM64 Windows cross-compilation.
# Forces the static library linker to use ARM64 machine type.

# Set static linker flags for ARM64
# Without this, lib.exe defaults to x64 even when building ARM64 objects
set(CMAKE_STATIC_LINKER_FLAGS "/MACHINE:ARM64" CACHE STRING "Static linker flags" FORCE)
