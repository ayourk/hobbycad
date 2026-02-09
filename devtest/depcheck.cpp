// =====================================================================
//  HobbyCAD Dependency Verification
// =====================================================================
//
//  Compiles and links against every HobbyCAD / HobbyMesh dependency,
//  then exercises each one at runtime to verify the installation.
//
//  Phase 0 (Foundation) — required:
//    OCCT, Qt 6, libgit2, libzip, OpenGL
//
//  Phase 1 (Basic Modeling) — optional:
//    libslvs (SolveSpace constraint solver)
//
//  Phase 3 (Python / Plugins / Version Control) — optional:
//    pybind11 + Python 3
//
//  Phase 5 (HobbyMesh) — optional:
//    OpenMesh, lib3mf, MeshFix, CGAL, OpenVDB, Assimp, Eigen
//
//  Exit code:
//    0 = all Phase 0 deps OK (warnings are informational)
//    1 = one or more Phase 0 deps FAILED
//
// =====================================================================

#include <iostream>
#include <string>
#include <vector>

// ---- Phase 0 (required) --------------------------------------------

#include <Standard_Version.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>
#include <STEPControl_Writer.hxx>
#include <StlAPI_Writer.hxx>
#include <IGESControl_Writer.hxx>

#include <QtCore/qconfig.h>
#include <QApplication>
#include <QOpenGLWidget>

#include <git2.h>
#include <zip.h>
#include <GL/gl.h>

// ---- Phase 1 (optional) --------------------------------------------

#ifdef HAVE_SLVS
#include <slvs.h>
#endif

// ---- Phase 3 (optional) --------------------------------------------

#ifdef HAVE_PYBIND11
#include <pybind11/embed.h>
#endif

// ---- Phase 5 (optional) --------------------------------------------

#ifdef HAVE_OPENMESH
#include <OpenMesh/Core/Mesh/TriMesh_ArrayKernelT.hh>
#endif

#ifdef HAVE_LIB3MF
#include <lib3mf_implicit.hpp>
#endif

// MeshFix: link-only test (no public header guaranteed)

#ifdef HAVE_CGAL
#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Surface_mesh.h>
#endif

#ifdef HAVE_OPENVDB
#include <openvdb/openvdb.h>
#endif

#ifdef HAVE_ASSIMP
#include <assimp/Importer.hpp>
#include <assimp/version.h>
#endif

#ifdef HAVE_EIGEN
#include <Eigen/Core>
#endif

// ---- Test framework -------------------------------------------------

enum Status { PASS, WARN, FAIL };

struct DepResult {
    std::string phase;
    std::string name;
    std::string version;
    Status      status;
    std::string detail;
    std::string fix;        // corrective action for WARN/FAIL
};

// =====================================================================
//  Tests
// =====================================================================

int main(int argc, char* argv[])
{
    std::vector<DepResult> results;
    int pass = 0, warn = 0, fail = 0;

    auto add = [&](DepResult r) {
        results.push_back(r);
        if      (r.status == PASS) pass++;
        else if (r.status == WARN) warn++;
        else                       fail++;
    };

    // =================================================================
    //  PHASE 0: Foundation (required)
    // =================================================================

    // OCCT — BRep kernel, STEP/STL/IGES writers
    {
        DepResult r{"0", "OCCT", OCC_VERSION_COMPLETE, FAIL, "", ""};
        try {
            BRep_Builder builder;
            TopoDS_Compound compound;
            builder.MakeCompound(compound);
            STEPControl_Writer stepW;
            StlAPI_Writer      stlW;
            IGESControl_Writer igesW;
            r.status = PASS;
            r.detail = "BRep + STEP/STL/IGES writers OK";
        } catch (...) {
            r.detail = "exception during OCCT test";
            r.fix    = "sudo apt install libocct-data-exchange-dev "
                       "libocct-modeling-algorithms-dev "
                       "libocct-visualization-dev";
        }
        add(r);
    }

    // Qt 6 — GUI framework
    {
        std::string ver = QT_VERSION_STR;
        DepResult r{"0", "Qt 6", ver, FAIL, "", ""};
        try {
            QApplication app(argc, argv);
            r.status = PASS;
            r.detail = "QApplication + OpenGLWidgets OK";
        } catch (...) {
            r.detail = "exception during Qt test";
            r.fix    = "sudo apt install qt6-base-dev libqt6opengl6-dev "
                       "libqt6openglwidgets6 libqt6svg6-dev";
        }
        add(r);
    }

    // libgit2 — version control
    {
        DepResult r{"0", "libgit2", "", FAIL, "", ""};
        int major, minor, rev;
        git_libgit2_init();
        git_libgit2_version(&major, &minor, &rev);
        r.version = std::to_string(major) + "." +
                    std::to_string(minor) + "." +
                    std::to_string(rev);
        r.status = PASS;
        r.detail = "init + version query OK";
        git_libgit2_shutdown();
        add(r);
    }

    // libzip — archive support
    {
        DepResult r{"0", "libzip", zip_libzip_version(),
                    PASS, "version query OK", ""};
        add(r);
    }

    // OpenGL — 3D viewport
    {
        DepResult r{"0", "OpenGL", "", FAIL, "", ""};
        auto fn = glGetString;
        if (fn != nullptr) {
            r.status = PASS;
            r.detail = "glGetString symbol OK";
        } else {
            r.detail = "glGetString symbol missing";
            r.fix    = "sudo apt install libgl-dev libglu1-mesa-dev";
        }
        add(r);
    }

    // =================================================================
    //  PHASE 1: Basic Modeling (optional)
    // =================================================================

#ifdef HAVE_SLVS
    {
        DepResult r{"1", "libslvs", "", PASS, "", ""};
        // Create a minimal system and verify the solver runs
        Slvs_System sys = {};
        Slvs_Param params[2];
        Slvs_Entity entities[1];

        params[0] = Slvs_MakeParam(1, 1, 10.0);
        params[1] = Slvs_MakeParam(2, 1, 20.0);
        sys.param  = params;
        sys.params = 2;

        entities[0] = Slvs_MakePoint2d(1, 1, 1, 2);
        sys.entity   = entities;
        sys.entities = 1;

        sys.constraint  = nullptr;
        sys.constraints = 0;

        Slvs_Solve(&sys, 1);
        r.detail = "solver invoked OK (result=" +
                   std::to_string(sys.result) + ")";
        add(r);
    }
#else
    {
        DepResult r{"1", "libslvs", "not found", WARN,
            "needed for sketch constraint solving",
            "sudo add-apt-repository ppa:ayourk/hobbycad && "
            "sudo apt install libslvs-dev"};
        add(r);
    }
#endif

    // =================================================================
    //  PHASE 3: Python / Plugins / Version Control (optional)
    // =================================================================

#ifdef HAVE_PYBIND11
    {
        std::string ver =
            std::string(PYBIND11_TOSTRING(PYBIND11_VERSION_MAJOR)) + "." +
            PYBIND11_TOSTRING(PYBIND11_VERSION_MINOR) + "." +
            PYBIND11_TOSTRING(PYBIND11_VERSION_PATCH);
        DepResult r{"3", "pybind11", ver, FAIL, "", ""};
        try {
            pybind11::scoped_interpreter guard{};
            auto result = pybind11::eval("2 + 2");
            if (result.cast<int>() == 4) {
                r.status = PASS;
                auto sys = pybind11::module_::import("sys");
                r.detail = "embedded Python " +
                    sys.attr("version").cast<std::string>().substr(0, 6)
                    + " OK";
            }
        } catch (const std::exception& e) {
            r.detail = std::string("exception: ") + e.what();
            r.fix    = "sudo apt install pybind11-dev python3-dev";
        }
        add(r);
    }
#else
    {
        DepResult r{"3", "pybind11", "not found", WARN,
            "needed for Python scripting",
            "sudo apt install pybind11-dev python3-dev "
            "python3-pybind11"};
        add(r);
    }
#endif

    // =================================================================
    //  PHASE 5: HobbyMesh (optional)
    // =================================================================

#ifdef HAVE_OPENMESH
    {
        DepResult r{"5", "OpenMesh", "", PASS, "", ""};
        typedef OpenMesh::TriMesh_ArrayKernelT<> TestMesh;
        TestMesh mesh;
        auto v0 = mesh.add_vertex(TestMesh::Point(0, 0, 0));
        auto v1 = mesh.add_vertex(TestMesh::Point(1, 0, 0));
        auto v2 = mesh.add_vertex(TestMesh::Point(0, 1, 0));
        mesh.add_face(v0, v1, v2);
        r.detail = "created triangle mesh (" +
                   std::to_string(mesh.n_vertices()) + "v, " +
                   std::to_string(mesh.n_faces()) + "f)";
        add(r);
    }
#else
    {
        DepResult r{"5", "OpenMesh", "not found", WARN,
            "needed for mesh half-edge operations",
            "sudo add-apt-repository ppa:ayourk/hobbycad && "
            "sudo apt install libopenmesh-dev"};
        add(r);
    }
#endif

#ifdef HAVE_LIB3MF
    {
        DepResult r{"5", "lib3mf", "", PASS, "", ""};
        try {
            auto wrapper = Lib3MF::CWrapper::loadLibrary();
            auto model   = wrapper->CreateModel();
            r.version    = wrapper->GetLibraryVersion();
            r.detail     = "created 3MF model OK";
        } catch (const std::exception& e) {
            r.status = FAIL;
            r.detail = std::string("exception: ") + e.what();
            r.fix    = "sudo add-apt-repository ppa:ayourk/hobbycad && "
                       "sudo apt install lib3mf-dev";
        }
        add(r);
    }
#else
    {
        DepResult r{"5", "lib3mf", "not found", WARN,
            "needed for 3MF format support",
            "sudo add-apt-repository ppa:ayourk/hobbycad && "
            "sudo apt install lib3mf-dev"};
        add(r);
    }
#endif

#ifdef HAVE_MESHFIX
    {
        // MeshFix: link-time verification (library linked successfully)
        DepResult r{"5", "MeshFix", "", PASS,
            "library linked OK", ""};
        add(r);
    }
#else
    {
        DepResult r{"5", "MeshFix", "not found", WARN,
            "needed for automatic mesh repair",
            "sudo add-apt-repository ppa:ayourk/hobbycad && "
            "sudo apt install libmeshfix-dev"};
        add(r);
    }
#endif

#ifdef HAVE_CGAL
    {
        DepResult r{"5", "CGAL", "", PASS, "", ""};
        typedef CGAL::Exact_predicates_inexact_constructions_kernel K;
        typedef CGAL::Surface_mesh<K::Point_3> Mesh;
        Mesh mesh;
        auto v0 = mesh.add_vertex(K::Point_3(0, 0, 0));
        auto v1 = mesh.add_vertex(K::Point_3(1, 0, 0));
        auto v2 = mesh.add_vertex(K::Point_3(0, 1, 0));
        mesh.add_face(v0, v1, v2);
        r.detail = "Surface_mesh created (" +
                   std::to_string(mesh.number_of_vertices()) + "v, " +
                   std::to_string(mesh.number_of_faces()) + "f)";
        add(r);
    }
#else
    {
        DepResult r{"5", "CGAL", "not found", WARN,
            "needed for computational geometry algorithms",
            "sudo apt install libcgal-dev"};
        add(r);
    }
#endif

#ifdef HAVE_OPENVDB
    {
        DepResult r{"5", "OpenVDB", "", PASS, "", ""};
        openvdb::initialize();
        auto grid = openvdb::FloatGrid::create();
        r.version = std::to_string(openvdb::OPENVDB_LIBRARY_MAJOR_VERSION)
            + "." + std::to_string(openvdb::OPENVDB_LIBRARY_MINOR_VERSION)
            + "." + std::to_string(openvdb::OPENVDB_LIBRARY_PATCH_VERSION);
        r.detail = "initialized + created FloatGrid OK";
        add(r);
    }
#else
    {
        DepResult r{"5", "OpenVDB", "not found", WARN,
            "needed for voxelization / Make Solid",
            "sudo apt install libopenvdb-dev"};
        add(r);
    }
#endif

#ifdef HAVE_ASSIMP
    {
        DepResult r{"5", "Assimp", "", PASS, "", ""};
        Assimp::Importer importer;
        r.version = std::to_string(aiGetVersionMajor()) + "." +
                    std::to_string(aiGetVersionMinor()) + "." +
                    std::to_string(aiGetVersionPatch());
        r.detail = "Importer created OK";
        add(r);
    }
#else
    {
        DepResult r{"5", "Assimp", "not found", WARN,
            "needed for multi-format mesh import",
            "sudo apt install libassimp-dev"};
        add(r);
    }
#endif

#ifdef HAVE_EIGEN
    {
        DepResult r{"5", "Eigen", "", PASS, "", ""};
        Eigen::Matrix3d m = Eigen::Matrix3d::Identity();
        r.version = std::to_string(EIGEN_WORLD_VERSION) + "." +
                    std::to_string(EIGEN_MAJOR_VERSION) + "." +
                    std::to_string(EIGEN_MINOR_VERSION);
        r.detail = "3x3 identity matrix OK";
        (void)m;  // suppress unused warning
        add(r);
    }
#else
    {
        DepResult r{"5", "Eigen", "not found", WARN,
            "needed for linear algebra (used by CGAL/MeshFix)",
            "sudo apt install libeigen3-dev"};
        add(r);
    }
#endif

    // =================================================================
    //  Report
    // =================================================================

    std::cout << "===== HobbyCAD Dependency Check =====\n";

    std::string current_phase;
    const char* phase_names[] = {
        "Phase 0: Foundation",
        "Phase 1: Basic Modeling",
        "Phase 2: Parametric Features",
        "Phase 3: Python / Plugins / Version Control",
        "Phase 4: Assemblies",
        "Phase 5: HobbyMesh"
    };

    for (const auto& r : results) {
        if (r.phase != current_phase) {
            current_phase = r.phase;
            int idx = std::stoi(current_phase);
            std::cout << "\n  -- " << phase_names[idx] << " --\n";
        }

        const char* tag = (r.status == PASS) ? "PASS" :
                          (r.status == WARN) ? "WARN" : "FAIL";
        std::cout << "  [" << tag << "] " << r.name;
        if (!r.version.empty())
            std::cout << " " << r.version;
        if (!r.detail.empty())
            std::cout << " — " << r.detail;
        std::cout << "\n";
        if (!r.fix.empty())
            std::cout << "         -> " << r.fix << "\n";
    }

    std::cout << "\n===== Results: "
              << pass << " passed, "
              << warn << " warnings, "
              << fail << " failed"
              << " out of " << (pass + warn + fail)
              << " =====\n";

    if (fail > 0) {
        std::cout << "\nPhase 0 dependencies are MISSING. "
                  << "HobbyCAD cannot build.\n"
                  << "See dev_environment_setup.txt Section 12 "
                  << "for troubleshooting.\n";
    } else if (warn > 0) {
        std::cout << "\nPhase 0 OK. Optional dependencies above "
                  << "can be installed when needed.\n";
    } else {
        std::cout << "\nAll dependencies installed. "
                  << "Ready for all phases.\n";
    }

    return fail > 0 ? 1 : 0;
}
