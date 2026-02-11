# vcpkg-overlay-ports/opencascade/portfile.cmake
#
# Overlay port for OpenCASCADE 7.6.2 with MSVC compilation fix.
#
# This overlay port applies a source fix for a compilation error
# with MSVC 14.44+ where an implicit unsigned char* -> const char*
# conversion in StdPrs_BRepFont.cxx is rejected.  The fix uses
# vcpkg_replace_string() instead of a patch file for robustness
# across platforms.  Based on upstream commit 7236e83dcc1e.
#
# SETUP:
#   1. Compute the SHA512 of the OCCT V7_6_2 source tarball:
#
#        curl -sL https://github.com/Open-Cascade-SAS/OCCT/archive/refs/tags/V7_6_2.tar.gz \
#          | sha512sum
#
#   2. Replace the SHA512 placeholder below with the computed hash.
#
#   3. Pass --overlay-ports=vcpkg-overlay-ports to vcpkg install,
#      or set VCPKG_OVERLAY_PORTS in vcpkg-configuration.json.
#

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO Open-Cascade-SAS/OCCT
    REF V7_6_2
    SHA512 1339889bb721ff43af00ba0048f0ca41fd5e424339ca6ff147436f67629ce360cfb0a623896608554872a1cfab67e8bcb8df066f4d1458da914aef70ffed0960
    HEAD_REF master
)

# Fix MSVC compilation error: implicit unsigned char* -> const char*
# conversion in StdPrs_BRepFont.cxx rejected by MSVC 14.44+.
# Upstream fix: commit 7236e83dcc1e.
vcpkg_replace_string("${SOURCE_PATH}/src/StdPrs/StdPrs_BRepFont.cxx"
    "const char* aTags      = &anOutline->tags[aStartIndex];"
    "const auto* aTags      = &anOutline->tags[aStartIndex];"
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DBUILD_LIBRARY_TYPE=Static
        -DBUILD_MODULE_ApplicationFramework=ON
        -DBUILD_MODULE_DataExchange=ON
        -DBUILD_MODULE_Draw=OFF
        -DBUILD_MODULE_FoundationClasses=ON
        -DBUILD_MODULE_ModelingAlgorithms=ON
        -DBUILD_MODULE_ModelingData=ON
        -DBUILD_MODULE_Visualization=ON
        -DUSE_D3D=OFF
        -DUSE_DRACO=OFF
        -DUSE_FFMPEG=OFF
        -DUSE_FREEIMAGE=OFF
        -DUSE_GLES2=OFF
        -DUSE_OPENGL=ON
        -DUSE_OPENVR=OFF
        -DUSE_RAPIDJSON=ON
        -DUSE_TBB=OFF
        -DUSE_TK=OFF
        -DUSE_VTK=OFF
        -DINSTALL_DIR_LIB=lib
        -DINSTALL_DIR_BIN=bin
        -DINSTALL_DIR_INCLUDE=include/opencascade
        -DINSTALL_DIR_CMAKE=share/opencascade
        -DINSTALL_DIR_RESOURCE=share/opencascade/resources
        -DINSTALL_DIR_DATA=share/opencascade/data
        -DINSTALL_DIR_SAMPLES=share/opencascade/samples
        -DINSTALL_DIR_DOC=share/doc/opencascade
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(CONFIG_PATH share/opencascade)
vcpkg_copy_pdbs()

# Remove empty directories and debug includes
file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/debug/include"
    "${CURRENT_PACKAGES_DIR}/debug/share"
    "${CURRENT_PACKAGES_DIR}/share/doc"
    "${CURRENT_PACKAGES_DIR}/share/opencascade/data"
    "${CURRENT_PACKAGES_DIR}/share/opencascade/samples"
)

file(INSTALL "${SOURCE_PATH}/COPYING.md"
     DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"
     RENAME copyright)
