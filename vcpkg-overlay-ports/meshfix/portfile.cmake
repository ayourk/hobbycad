# MeshFix — automatic mesh repair
# Upstream: https://github.com/MarcoAttene/MeshFix-V2.1

set(VERSION 2.1)
# Upstream commit: 6dd727b6d1ee04e7a5554aaf05fa6c7106c0dccb

vcpkg_download_distfile(ARCHIVE
    URLS
        "https://launchpad.net/~ayourk/+archive/ubuntu/hobbycad/+sourcefiles/meshfix/2.1.git~20260208-1~ppa1~noble1/meshfix_${VERSION}.git~20260208.orig.tar.gz"
    FILENAME "meshfix_${VERSION}.git.20260208.orig.tar.gz"
    SHA512 89c90a230cd4ea73289c5e2a95ebb0df943da34982edaa005333388ff9c2378cedafbaba6cafa375ecb64519b593f3557661a441fe3df104af697f811d7ec578
)

vcpkg_extract_source_archive(
    SOURCE_PATH
    ARCHIVE "${ARCHIVE}"
    PATCHES
        build-shared-library-cross-platform.patch
)

# Respect vcpkg triplet linkage (static vs shared)
string(COMPARE EQUAL "${VCPKG_LIBRARY_LINKAGE}" "dynamic" BUILD_SHARED)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DMESHFIX_BUILD_SHARED=${BUILD_SHARED}
        -DMESHFIX_BUILD_CLI=OFF
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/meshfix)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

# License
file(INSTALL "${SOURCE_PATH}/gpl-3.0.txt"
     DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"
     RENAME copyright)
