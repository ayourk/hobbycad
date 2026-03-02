# vcpkg-toolchains/arm64-windows.cmake
#
# Toolchain file for ARM64 Windows cross-compilation from x64 host.
# Ensures the correct ARM64 linker is used.

# Set target system
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR ARM64)

# Find MSVC tools directory
if(DEFINED ENV{VCToolsInstallDir})
    set(_VCTOOLS "$ENV{VCToolsInstallDir}")
else()
    # Fallback: try to find VS 2022 installation
    file(GLOB _VCTOOLS "C:/Program Files/Microsoft Visual Studio/2022/*/VC/Tools/MSVC/*")
    list(GET _VCTOOLS -1 _VCTOOLS)  # Use latest version
endif()

if(_VCTOOLS)
    # Use the ARM64 cross-linker (runs on x64, produces ARM64)
    set(_ARM64_LINKER "${_VCTOOLS}/bin/Hostx64/arm64/link.exe")
    if(EXISTS "${_ARM64_LINKER}")
        set(CMAKE_LINKER "${_ARM64_LINKER}" CACHE FILEPATH "ARM64 linker" FORCE)
        set(CMAKE_AR "${_VCTOOLS}/bin/Hostx64/arm64/lib.exe" CACHE FILEPATH "ARM64 archiver" FORCE)
    endif()
endif()

# Ensure linker produces ARM64 output
set(CMAKE_EXE_LINKER_FLAGS_INIT "/MACHINE:ARM64")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "/MACHINE:ARM64")
set(CMAKE_STATIC_LINKER_FLAGS_INIT "/MACHINE:ARM64")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "/MACHINE:ARM64")
