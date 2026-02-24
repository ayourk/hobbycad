# libslvs - SolveSpace constraint solver library
#
# This port builds only the slvs library from SolveSpace, which provides
# a geometric constraint solver that can be embedded in other applications.
#
# Source tarball from Launchpad PPA includes all necessary submodules.

vcpkg_check_linkage(ONLY_DYNAMIC_LIBRARY)

set(VERSION 3.2)

# Eigen is provided by the eigen3 vcpkg dependency

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

# Copy Eigen headers from vcpkg installed dir into the solvespace extlib path
set(EIGEN_DIR "${SOURCE_PATH}/extlib/eigen")
if(NOT EXISTS "${EIGEN_DIR}/Eigen/Core")
    file(COPY "${CURRENT_INSTALLED_DIR}/include/eigen3/Eigen"
         DESTINATION "${EIGEN_DIR}")
    file(COPY "${CURRENT_INSTALLED_DIR}/include/eigen3/unsupported"
         DESTINATION "${EIGEN_DIR}")
endif()

# Verify Eigen is present before proceeding
if(NOT EXISTS "${EIGEN_DIR}/Eigen/Core")
    message(FATAL_ERROR "Eigen headers not found at ${EIGEN_DIR}/Eigen/Core")
endif()

# Override solvespace's set(CMAKE_CXX_STANDARD 11) — the -D flag cannot
# override a normal variable, so we must patch the source directly
file(READ "${SOURCE_PATH}/CMakeLists.txt" _cmakelists)
string(REPLACE "set(CMAKE_CXX_STANDARD 11)" "set(CMAKE_CXX_STANDARD 14)" _cmakelists "${_cmakelists}")
file(WRITE "${SOURCE_PATH}/CMakeLists.txt" "${_cmakelists}")

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
