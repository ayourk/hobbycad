# vcpkg-toolchains/arm64-windows.cmake
#
# Toolchain file for ARM64 Windows cross-compilation from x64 host.
# Only sets linker flags - let vcpkg handle compiler detection.

# Don't set CMAKE_SYSTEM_NAME or CMAKE_SYSTEM_PROCESSOR here as it
# interferes with vcpkg's compiler detection. vcpkg already knows the
# target architecture from the triplet.

# Ensure linker produces ARM64 output. The /MACHINE flag tells the linker
# what architecture to target even if called from an x64 environment.
set(CMAKE_EXE_LINKER_FLAGS_INIT "/MACHINE:ARM64")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "/MACHINE:ARM64")
set(CMAKE_STATIC_LINKER_FLAGS_INIT "/MACHINE:ARM64")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "/MACHINE:ARM64")
