# OpenMesh polygon mesh data structure
# Upstream: https://gitlab.vci.rwth-aachen.de:9000/OpenMesh/OpenMesh

set(VERSION 11.0.0)
# Upstream tag: OpenMesh-11.0.0

vcpkg_download_distfile(ARCHIVE
    URLS
        "https://launchpad.net/~ayourk/+archive/ubuntu/hobbycad/+sourcefiles/libopenmesh/11.0.0.git~20260208-1~ppa1~noble1/libopenmesh_${VERSION}.git~20260208.orig.tar.gz"
    FILENAME "libopenmesh_${VERSION}.git.20260208.orig.tar.gz"
    SHA512 8c0cc273bac76ecceae624820cbe6430295c163018960e5bd648a207071ab3f81af63834a0efbb475ee4418d2e61f1c27db8e47f1d1d58d159b1a490f7ee5e31
)

vcpkg_extract_source_archive(
    SOURCE_PATH
    ARCHIVE "${ARCHIVE}"
)

string(COMPARE EQUAL "${VCPKG_LIBRARY_LINKAGE}" "dynamic" BUILD_SHARED)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DBUILD_APPS=OFF
        -DOPENMESH_BUILD_SHARED=${BUILD_SHARED}
        -DOPENMESH_DOCS=OFF
        -DOPENMESH_BUILD_UNIT_TESTS=OFF
)

vcpkg_cmake_install()

# OpenMesh may not install CMake config files for debug builds
if(NOT EXISTS "${CURRENT_PACKAGES_DIR}/debug/lib/cmake/OpenMesh")
    file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/debug/lib/cmake/OpenMesh")
endif()

vcpkg_cmake_config_fixup(PACKAGE_NAME OpenMesh CONFIG_PATH lib/cmake/OpenMesh)

# OpenMesh installs pkgconfig to libdata/pkgconfig/ on some platforms
if(EXISTS "${CURRENT_PACKAGES_DIR}/libdata/pkgconfig")
    file(RENAME "${CURRENT_PACKAGES_DIR}/libdata/pkgconfig" "${CURRENT_PACKAGES_DIR}/lib/pkgconfig")
    file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/libdata")
endif()
if(EXISTS "${CURRENT_PACKAGES_DIR}/debug/libdata/pkgconfig")
    file(RENAME "${CURRENT_PACKAGES_DIR}/debug/libdata/pkgconfig" "${CURRENT_PACKAGES_DIR}/debug/lib/pkgconfig")
    file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/libdata")
endif()
vcpkg_fixup_pkgconfig()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
