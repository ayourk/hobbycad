# x64-windows-static-md.cmake
#
# Static libraries linked against the dynamic MSVC runtime (/MD).
# Produces a single .exe with all dependencies statically linked,
# while using the standard Visual C++ runtime DLL (vcruntime140.dll
# etc.) which ships with Windows 10+.

set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_BUILD_TYPE release)

