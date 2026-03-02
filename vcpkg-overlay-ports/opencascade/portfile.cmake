# vcpkg-overlay-ports/opencascade/portfile.cmake
#
# Overlay port for OpenCASCADE 7.9.2.
#
# SETUP:
#   Pass --overlay-ports=vcpkg-overlay-ports to vcpkg install,
#   or set VCPKG_OVERLAY_PORTS in vcpkg-configuration.json.
#

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO Open-Cascade-SAS/OCCT
    REF V7_9_2
    SHA512 58f9ab91c5119e0a99fb7599bce574f17ce3e3a802a9c503fa0464228d5b2141e3f5557ef68355b4921b572bd10d99bec0f31836a103d5e5fa98cd0d685610a2
    HEAD_REF master
)

# macOS framework paths - OCCT uses non-standard CMake variable names
# that don't work with vcpkg's toolchain. Explicitly set them.
set(MACOS_OPTIONS "")
if(VCPKG_TARGET_IS_OSX)
    list(APPEND MACOS_OPTIONS
        "-DAppkit_LIB=-framework AppKit"
        "-DIOKit_LIB=-framework IOKit"
        "-DOpenGlLibs_LIB=-framework OpenGL"
    )
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DBUILD_LIBRARY_TYPE=Static
        ${MACOS_OPTIONS}
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

# Fix OpenCASCADE_INSTALL_PREFIX path calculation in the CMake config.
# The OpenCASCADEConfig.cmake calculates the install prefix but ends up at
# share/ instead of the vcpkg root. We append code to the config file that
# recalculates the correct paths after it runs.
file(APPEND "${CURRENT_PACKAGES_DIR}/share/opencascade/OpenCASCADEConfig.cmake" [[

# vcpkg overlay port fix: recalculate paths to point to vcpkg root
# The original config ends up with OpenCASCADE_INSTALL_PREFIX pointing to share/
# which causes include paths to be wrong. Fix by going up one more level.
get_filename_component(_VCPKG_OCCT_ROOT "${CMAKE_CURRENT_LIST_DIR}" PATH)
get_filename_component(_VCPKG_OCCT_ROOT "${_VCPKG_OCCT_ROOT}" PATH)
set(OpenCASCADE_INSTALL_PREFIX "${_VCPKG_OCCT_ROOT}")
set(OpenCASCADE_INCLUDE_DIR "${_VCPKG_OCCT_ROOT}/include/opencascade")
unset(_VCPKG_OCCT_ROOT)
]])

# Remove empty directories and debug includes
file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/debug/include"
    "${CURRENT_PACKAGES_DIR}/debug/share"
    "${CURRENT_PACKAGES_DIR}/share/doc"
    "${CURRENT_PACKAGES_DIR}/share/opencascade/data"
    "${CURRENT_PACKAGES_DIR}/share/opencascade/samples"
)

# Install license file
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE_LGPL_21.txt")
