# =====================================================================
#  packaging/rpm/hobbycad.spec — HobbyCAD RPM (AlmaLinux 9, Fedora)
#  SPDX-License-Identifier: GPL-3.0-only
#  Part of HobbyCAD (ayourk/hobbycad)
# =====================================================================
#
#  Used by .github/workflows/linux-rpm.yml. BuildRequires is the job's only
#  list of distribution build dependencies: scripts/ci/install-build-deps.sh
#  feeds this file to dnf builddep. OpenCASCADE, libslvs, nlohmann-json and
#  libwebp are not listed because the job builds the versions.json pins of
#  those statically from source (scripts/ci/). The job configures, builds
#  and installs into rpm-root/ before rpmbuild runs, and passes the project
#  version as hc_version.
# =====================================================================
%{!?hc_version: %global hc_version 0.0.0}

Name:           hobbycad
Version:        %{hc_version}
Release:        1%{?dist}
Summary:        Parametric 3D CAD Application
License:        GPL-3.0-only
URL:            https://github.com/ayourk/hobbycad

BuildRequires:  gcc-c++
BuildRequires:  cmake
BuildRequires:  ninja-build
BuildRequires:  pkgconfig
BuildRequires:  qt6-qtbase-devel
BuildRequires:  qt6-qtsvg-devel
BuildRequires:  qt6-qttools-devel
BuildRequires:  qt6-qtimageformats
BuildRequires:  mesa-libGL-devel
BuildRequires:  mesa-libGLU-devel
%if 0%{?fedora}
BuildRequires:  mesa-libEGL-devel
%endif
BuildRequires:  libxkbcommon-devel
BuildRequires:  tbb-devel
BuildRequires:  libpng-devel
BuildRequires:  libjpeg-devel
# The static OpenCASCADE links FreeType, fontconfig (and its expat) and X11
# into the program, so their development files are needed here too.
BuildRequires:  freetype-devel
BuildRequires:  fontconfig-devel
BuildRequires:  expat-devel
BuildRequires:  libX11-devel
BuildRequires:  libXmu-devel
BuildRequires:  libXi-devel
BuildRequires:  libXext-devel

%description
HobbyCAD is a Linux-native open-source parametric 3D CAD application
built with Qt6 and OpenCASCADE.

%install
cp -a %{_builddir}/rpm-root/* %{buildroot}/

%files
/usr/bin/hobbycad
/usr/lib*/libhobbycad*
/usr/include/hobbycad/
/usr/share/applications/hobbycad.desktop
/usr/share/icons/hicolor/*/apps/hobbycad.*
/usr/share/pixmaps/hobbycad.xpm
/usr/share/hobbycad/
/usr/share/man/man1/hobbycad.1*

%changelog
* %(LC_ALL=C date '+%a %b %d %Y') HobbyCAD CI <ci@hobbycad.org> - %{hc_version}-1
- Automated build from CI
