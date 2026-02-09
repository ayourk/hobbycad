=====================================================================
  devtest — HobbyCAD Dependency Verification
=====================================================================

  This directory contains a standalone build test that verifies
  every HobbyCAD and HobbyMesh dependency is correctly installed.

  It compiles and links a small program against all libraries, then
  exercises each one at runtime.  Phase 0 dependencies are required;
  later-phase dependencies report [WARN] with corrective action if
  they are not installed.


  QUICK START
  -----------

    cd devtest
    cmake -B build
    cmake --build build -j$(nproc)
    ./build/depcheck


  WHAT IT TESTS
  -------------

    Phase 0 — Foundation (required):
      OCCT            B-Rep kernel, STEP/STL/IGES writers
      Qt 6            GUI framework (Core, Widgets, OpenGL)
      libgit2         Version control library
      libzip          Archive support
      OpenGL          3D viewport symbol linkage

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


  EXPECTED OUTPUT (Ubuntu 24.04, all packages installed)
  -------------------------------------------------------

    ===== HobbyCAD Dependency Check =====

      -- Phase 0: Foundation --
      [PASS] OCCT 7.6.3 — BRep + STEP/STL/IGES writers OK
      [PASS] Qt 6 6.4.2 — QApplication + OpenGLWidgets OK
      [PASS] libgit2 1.7.2 — init + version query OK
      [PASS] libzip 1.10.1 — version query OK
      [PASS] OpenGL — glGetString symbol OK

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

    ===== Results: 14 passed, 0 warnings, 0 failed out of 14 =====

    All dependencies installed. Ready for all phases.


  EXPECTED OUTPUT (Phase 0 only, no PPA packages)
  ------------------------------------------------

    ===== HobbyCAD Dependency Check =====

      -- Phase 0: Foundation --
      [PASS] OCCT 7.6.3 — BRep + STEP/STL/IGES writers OK
      [PASS] Qt 6 6.4.2 — QApplication + OpenGLWidgets OK
      [PASS] libgit2 1.7.2 — init + version query OK
      [PASS] libzip 1.10.1 — version query OK
      [PASS] OpenGL — glGetString symbol OK

      -- Phase 1: Basic Modeling --
      [WARN] libslvs not found — needed for sketch constraint solving
             -> sudo add-apt-repository ppa:ayourk/hobbycad && sudo apt install libslvs-dev

      -- Phase 3: Python / Plugins / Version Control --
      [WARN] pybind11 not found — needed for Python scripting
             -> sudo apt install pybind11-dev python3-dev python3-pybind11

      -- Phase 5: HobbyMesh --
      [WARN] OpenMesh not found — needed for mesh half-edge operations
             -> sudo add-apt-repository ppa:ayourk/hobbycad && sudo apt install libopenmesh-dev
      [WARN] lib3mf not found — needed for 3MF format support
             -> sudo add-apt-repository ppa:ayourk/hobbycad && sudo apt install lib3mf-dev
      [WARN] MeshFix not found — needed for automatic mesh repair
             -> sudo add-apt-repository ppa:ayourk/hobbycad && sudo apt install libmeshfix-dev
      [WARN] CGAL not found — needed for computational geometry algorithms
             -> sudo apt install libcgal-dev
      [WARN] OpenVDB not found — needed for voxelization / Make Solid
             -> sudo apt install libopenvdb-dev
      [WARN] Assimp not found — needed for multi-format mesh import
             -> sudo apt install libassimp-dev
      [WARN] Eigen not found — needed for linear algebra (used by CGAL/MeshFix)
             -> sudo apt install libeigen3-dev

    ===== Results: 5 passed, 9 warnings, 0 failed out of 14 =====

    Phase 0 OK. Optional dependencies above can be installed when needed.


  EXIT CODES
  ----------

    0   All Phase 0 dependencies OK (warnings are informational)
    1   One or more Phase 0 dependencies FAILED


  FILES
  -----

    CMakeLists.txt    Build system — finds all dependencies
    depcheck.cpp      Test program — exercises each dependency
    README            This file


  CLEAN UP
  --------

    rm -rf build
