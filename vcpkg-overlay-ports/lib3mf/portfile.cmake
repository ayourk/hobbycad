# lib3mf — 3D Manufacturing Format library
# Upstream: https://github.com/3MFConsortium/lib3mf

set(VERSION 2.4.1)
# Upstream tag: v2.4.1 (20c335489c69d15c64f3eaf1e15143b8176901f5)

vcpkg_download_distfile(ARCHIVE
    URLS
        "https://launchpad.net/~ayourk/+archive/ubuntu/hobbycad/+sourcefiles/lib3mf/2.4.1.git~20260208-1~ppa1~noble1/lib3mf_${VERSION}.git~20260208.orig.tar.gz"
    FILENAME "lib3mf_${VERSION}.git.20260208.orig.tar.gz"
    SHA512 8a840b9441c8f6f50b7a3b59d7606287ca4907d42781e2bba749f8eb2cee820db40f7c9e852c1d5dcc1275dcbae518d10648d0963caf81129b6fa90e1ddd72e6
)

vcpkg_extract_source_archive(
    SOURCE_PATH
    ARCHIVE "${ARCHIVE}"
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DLIB3MF_TESTS=OFF
        -DUSE_INCLUDED_ZLIB=ON
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/lib3mf)
vcpkg_fixup_pkgconfig()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
