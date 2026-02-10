=====================================================================
  devtest/README.txt — HobbyCAD Dependency Verification
=====================================================================

  This directory contains a standalone build test that verifies
  every HobbyCAD and HobbyMesh dependency across all development
  phases.  It compiles and links a small program against all
  libraries, then exercises each one at runtime.

  Cross-platform: Linux, Windows, and macOS.

  Phase 0 dependencies are required; later-phase dependencies
  report [WARN] with platform-specific corrective action if they
  are not installed.


  QUICK START — LINUX
  --------------------

    cd devtest
    cmake -B build
    cmake --build build -j$(nproc)
    ./build/depcheck


  QUICK START — WINDOWS (vcpkg)
  ------------------------------

  From an MSYS2 MinGW64 shell or Visual Studio Developer Command
  Prompt:

    cd devtest
    cmake -B build ^
      -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake
    cmake --build build
    .\build\depcheck.exe

  If Ninja is not installed, CMake uses the default generator and
  prints a [WARN].  Install Ninja for consistency with the main
  HobbyCAD build (see dev_environment_setup.txt Section 14).

  If using the Qt Online Installer instead of vcpkg for Qt, also
  pass -DQt6_DIR=C:\Qt\6.x.x\msvc2019_64\lib\cmake\Qt6.

  See dev_environment_setup.txt Sections 13-19 for full Windows
  environment setup.


  QUICK START — MACOS (Homebrew)
  --------------------------------

    cd devtest
    cmake -B build \
      -DQt6_DIR=$(brew --prefix qt@6)/lib/cmake/Qt6
    cmake --build build -j$(sysctl -n hw.ncpu)
    ./build/depcheck

  See dev_environment_setup.txt Sections 20-26 for full macOS
  environment setup.


  WHAT IT TESTS
  -------------

    Phase 0 — Foundation (required):
      OCCT            B-Rep kernel, STEP/STL/IGES writers
      Qt 6            GUI framework (Core, Widgets, OpenGL)
      libgit2         Version control library
      libzip          Archive support
      OpenGL          3D viewport symbol linkage
      rsvg-convert    SVG to PNG icon generation (build-time, required)
      icotool         Windows .ico generation (Windows only, WARN)

    Phase 1 — Basic Modeling (optional):
      libslvs         SolveSpace constraint solver

    Phase 3 — Python / Plugins (optional):
      pybind11        Embedded Python interpreter

    Phase 5 — HobbyMesh (optional):
      OpenMesh        Half-edge mesh kernel
      lib3mf          3MF format support
      MeshFix         Automatic mesh repair
      CGAL            Computational geometry algorithms
      OpenVDB         Voxelization / Make Solid
      Assimp          Multi-format mesh import
      Eigen           Linear algebra


  EXPECTED OUTPUT (all packages installed)
  ------------------------------------------

    ===== HobbyCAD Dependency Check =====
    Platform: linux

      -- Phase 0: Foundation --
      [PASS] OCCT 7.6.3 — BRep + STEP/STL/IGES writers OK
      [PASS] Qt 6 6.4.2 — QApplication + OpenGLWidgets OK
      [PASS] libgit2 1.7.2 — init + version query OK
      [PASS] libzip 1.10.1 — version query OK
      [PASS] OpenGL 4.6 (Mesa) — Mesa Intel(R) UHD Graphics 770 (ADL-S GT1)
      [PASS] rsvg-convert 2.56.1 — SVG to PNG conversion available

      -- Phase 1: Basic Modeling --
      [PASS] libslvs — solver invoked OK (result=0)

      -- Phase 3: Python / Plugins / Version Control --
      [PASS] pybind11 2.11.1 — embedded Python 3.12.3 OK

      -- Phase 5: HobbyMesh --
      [PASS] OpenMesh — created triangle mesh (3v, 1f)
      [PASS] lib3mf 2.4.1 — created 3MF model OK
      [PASS] MeshFix — library linked OK
      [PASS] CGAL — Surface_mesh created (3v, 1f)
      [PASS] OpenVDB 10.0.1 — initialized + created FloatGrid OK
      [PASS] Assimp 5.3.1 — Importer created OK
      [PASS] Eigen 3.4.0 — 3x3 identity matrix OK

    ===== Results: 15 passed, 0 warnings, 0 failed out of 15 =====

    All dependencies installed. Ready for all phases.

    Highest phase passed: 5 (Phase 5: HobbyMesh)

    DEVTEST_RESULT: [PASS] Success!  Good up to and including Phase 5

  Version numbers will differ on Windows (vcpkg) and macOS
  (Homebrew).  The Platform: line shows "windows" or "macos"
  accordingly.


  RESULT LINE
  ------------

  The final line of output (and of devtest.log) is one of:

    DEVTEST_RESULT: [PASS] Success!  Good up to and including Phase N
    DEVTEST_RESULT: [FAIL] Missing Phase 0 dependencies

  build-dev.sh/.bat parses lines starting with "DEVTEST_RESULT:"
  to skip devtest on subsequent runs.  If the result line is
  missing or shows [FAIL], devtest is rerun automatically.
  Delete devtest.log to force a rerun.


  EXPECTED OUTPUT (Phase 0 only)
  --------------------------------

  [WARN] lines show platform-appropriate install commands:

    Linux:    sudo apt install ...  /  ppa:ayourk/hobbycad
    Windows:  vcpkg install ...:x64-windows
    macOS:    brew install ...

  Libraries not in vcpkg or Homebrew show a "build from source"
  URL instead.


  EXIT CODES
  ----------

    0   All Phase 0 dependencies OK (warnings are informational)
    1   One or more Phase 0 dependencies FAILED


  LOG FILE
  --------

  devtest.log is written in the devtest/ source directory and
  captures the full pipeline from configure through runtime:

    1. CMake configure phase — written during cmake -B build:
       - Timestamp, generator, build type, source/build dirs
       - Compiler identity, version, and path
       - Platform detection
       - Each dependency: found (with version and method) or
         not found (with install hint)

    2. Runtime test phase — appended by ./build/depcheck:
       - Compiler, C++ standard, architecture, build type
       - Per-dependency [PASS]/[WARN]/[FAIL] with versions
       - OpenGL driver version and GPU renderer
       - Summary counts
       - Highest phase passed
       - Result line (Success/Failed)

  The log is overwritten on each cmake -B run (the configure
  phase starts fresh) and appended to by each depcheck run.

  Useful for bug reports, CI diagnostics, and verifying that
  the correct library versions are being picked up.  Listed in
  .gitignore — should not be committed.


  PLATFORM NOTES
  ---------------

  Linux:
    - pkg-config is used as fallback for libraries without CMake
      config files
    - libtbb-dev is required (OCCT links against TBB)
    - PPA packages available for libslvs, libopenmesh, lib3mf,
      meshfix
    - See dev_environment_setup.txt Sections 4-5 for package lists

  Windows:
    - vcpkg toolchain file is required for CMake to find packages
    - windeployqt runs automatically after build to copy Qt DLLs
    - MSYS2/MinGW-w64 and MSVC are both supported
    - See dev_environment_setup.txt Sections 15-16 for vcpkg setup

  macOS:
    - Qt is typically keg-only in Homebrew; pass Qt6_DIR explicitly
    - OpenGL is deprecated on macOS but still functional (4.1 max)
    - Works on both Apple Silicon (arm64) and Intel (x86_64)
    - See dev_environment_setup.txt Section 23 for architecture notes


  FILES
  -----

    CMakeLists.txt    Build system — cross-platform dependency detection
    depcheck.cpp      Test program — exercises each dependency
    README.txt        This file
    devtest.log       Generated at runtime (not checked in)


  CLEAN UP
  --------

    Linux / macOS:    rm -rf build
    Windows:          rmdir /s /q build

