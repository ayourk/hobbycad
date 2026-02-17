# libslvs - SolveSpace constraint solver library
#
# This port builds only the slvs library from SolveSpace, which provides
# a geometric constraint solver that can be embedded in other applications.
#
# Source tarball from Launchpad PPA includes all necessary submodules.

vcpkg_check_linkage(ONLY_DYNAMIC_LIBRARY)

set(VERSION 3.2)

# Download Eigen (required for constraint solver)
vcpkg_download_distfile(EIGEN_ARCHIVE
    URLS "https://gitlab.com/libeigen/eigen/-/archive/3.4.0/eigen-3.4.0.tar.gz"
    FILENAME "eigen-3.4.0.tar.gz"
    SHA512 ba75ecb760e32acf4ceaf27115468e65d4f77c44f8d519b5765941e6be8238444f7395a4c1826e0492f5c631a3486e34085a91a8c7165e5c4ae5b9e2a84cc42b
)

vcpkg_download_distfile(ARCHIVE
    URLS
        "https://launchpad.net/~ayourk/+archive/ubuntu/hobbycad/+sourcefiles/libslvs/3.2.git~20260208-1~ppa2~noble1/libslvs_${VERSION}.git~20260208.orig.tar.gz"
    FILENAME "libslvs_${VERSION}.git.20260208.orig.tar.gz"
    SHA512 a95c2dbb7af60e1a172fdedf26a0e5de5ebfe8ebf16cc0e4933c3dcc28c537873620703a78ea0af3b2df14e96d406dfe2565f9fe9a078621c67a1e7e40debcbe
)

vcpkg_extract_source_archive(
    SOURCE_PATH
    ARCHIVE "${ARCHIVE}"
    PATCHES
        0001-handle-missing-git-directory.patch
)

# Stub empty git submodule directories — the constraint solver
# doesn't need any of these vendored libraries.
foreach(SUBMOD zlib libpng freetype cairo pixman angle)
    set(SUBMOD_DIR "${SOURCE_PATH}/extlib/${SUBMOD}")
    if(IS_DIRECTORY "${SUBMOD_DIR}" AND NOT EXISTS "${SUBMOD_DIR}/CMakeLists.txt")
        file(WRITE "${SUBMOD_DIR}/CMakeLists.txt" "# stub — submodule not needed for libslvs\n")
    endif()
endforeach()

# Extract Eigen into the source tree (required for constraint solver)
# NOTE: vcpkg_extract_source_archive returns the path to the .clean copy for
# incremental builds. We extract Eigen directly to this path so it's always present.
set(EIGEN_DIR "${SOURCE_PATH}/extlib/eigen")
if(NOT EXISTS "${EIGEN_DIR}/Eigen/Core")
    # Extract Eigen to extlib
    file(ARCHIVE_EXTRACT INPUT "${EIGEN_ARCHIVE}" DESTINATION "${SOURCE_PATH}/extlib")
    # Rename from eigen-3.4.0 to eigen
    if(EXISTS "${SOURCE_PATH}/extlib/eigen-3.4.0" AND NOT EXISTS "${EIGEN_DIR}")
        file(RENAME "${SOURCE_PATH}/extlib/eigen-3.4.0" "${EIGEN_DIR}")
    endif()
endif()

# Verify Eigen is present before proceeding
if(NOT EXISTS "${EIGEN_DIR}/Eigen/Core")
    message(FATAL_ERROR "Eigen headers not found at ${EIGEN_DIR}/Eigen/Core")
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DBUILD_LIB=ON
        -DBUILD_GUI=OFF
        -DENABLE_GUI=OFF
        -DENABLE_CLI=OFF
        -DENABLE_OPENMP=OFF
        -DENABLE_TESTS=OFF
)

vcpkg_cmake_install()

# Guard: debug cmake config may not be installed
if(NOT EXISTS "${CURRENT_PACKAGES_DIR}/debug/lib/cmake/slvs")
    file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/debug/lib/cmake/slvs")
endif()

vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/slvs)
vcpkg_fixup_pkgconfig()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/COPYING.txt")
