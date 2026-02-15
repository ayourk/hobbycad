// =====================================================================
//  devtest/depcheck.cpp — HobbyCAD Dependency Verification
// =====================================================================
//
//  Compiles and links against every HobbyCAD / HobbyMesh dependency,
//  then exercises each one at runtime to verify the installation.
//
//  Cross-platform: Linux, Windows (MSVC/MinGW), macOS (Apple Clang).
//
//  Phase 0 (Foundation) — required:
//    OCCT, Qt 6, libgit2, libzip, OpenGL, rsvg-convert
//    icotool (Windows only, optional — WARN if missing)
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
#include <fstream>
#include <string>
#include <vector>
#include <memory>
#include <ctime>
#include <cstdio>
#include <cstdlib>

// ---- Phase 0 (required) — OCCT ------------------------------------

#include <Standard_Version.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>
#include <STEPControl_Writer.hxx>
#include <StlAPI_Writer.hxx>
#include <IGESControl_Writer.hxx>

// ---- Phase 3 (optional) — pybind11 --------------------------------
//
// pybind11 MUST be included BEFORE Qt headers on all platforms.
// Qt defines a "slots" macro (via qobjectdefs.h) that collides with
// Python 3.12's use of "slots" as a C struct member name in object.h.
// Including pybind11 first ensures Python.h is parsed before Qt's
// macro definitions take effect.

#ifdef HAVE_PYBIND11
#include <pybind11/embed.h>
#endif

// ---- Phase 0 (required) — Qt 6 ------------------------------------

#include <QtCore/qconfig.h>
#include <QApplication>
#include <QOpenGLWidget>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QSurfaceFormat>

// ---- Phase 0 (required) — libgit2, libzip -------------------------

#include <git2.h>
#include <zip.h>

// ---- Phase 0 (required) — OpenGL ----------------------------------
//
// Platform-specific OpenGL header paths.

#if defined(__APPLE__)
    #include <OpenGL/gl.h>
#elif defined(_WIN32)
    #include <windows.h>
    #include <GL/gl.h>
#else
    #include <GL/gl.h>
#endif

// ---- Phase 1 (optional) — libslvs ---------------------------------

#ifdef HAVE_SLVS
#include <slvs.h>
#endif

// ---- Phase 5 (optional) -------------------------------------------

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

// Platform name for install hints in runtime output
static const char* platform_name()
{
#if defined(__APPLE__)
    return "macos";
#elif defined(_WIN32)
    return "windows";
#else
    return "linux";
#endif
}

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
            r.fix    = "install OCCT dev packages";
        }
        add(r);
    }

    // Qt 6 — GUI framework
    //
    // QApplication is created here and kept alive so the OpenGL test
    // below can create a context and query the real GL version.
    std::unique_ptr<QApplication> qapp;
    {
        std::string ver = QT_VERSION_STR;
        DepResult r{"0", "Qt 6", ver, FAIL, "", ""};
        try {
            qapp = std::make_unique<QApplication>(argc, argv);
            r.status = PASS;
            r.detail = "QApplication + OpenGLWidgets OK";
        } catch (...) {
            r.detail = "exception during Qt test";
            r.fix    = "install Qt 6 dev packages";
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
    //
    // With QApplication alive, we can create a QOffscreenSurface and
    // QOpenGLContext to query the real GL version string from the
    // driver.  This mirrors the startup sequence in HobbyCAD (see
    // project_definition.txt Section 13.1).
    {
        DepResult r{"0", "OpenGL", "", FAIL, "", ""};
        if (qapp) {
            QSurfaceFormat fmt;
            fmt.setMajorVersion(3);
            fmt.setMinorVersion(3);
            fmt.setProfile(QSurfaceFormat::CoreProfile);

            QOffscreenSurface surface;
            surface.setFormat(fmt);
            surface.create();

            QOpenGLContext ctx;
            ctx.setFormat(fmt);
            if (ctx.create() && ctx.makeCurrent(&surface)) {
                const char* glVer =
                    reinterpret_cast<const char*>(glGetString(GL_VERSION));
                const char* glRenderer =
                    reinterpret_cast<const char*>(glGetString(GL_RENDERER));
                if (glVer) {
                    r.version = glVer;
                    r.status  = PASS;
                    r.detail  = glRenderer ? glRenderer : "context OK";
                } else {
                    r.status = PASS;
                    r.detail = "context created but glGetString returned null";
                }
                ctx.doneCurrent();
            } else {
                // No GPU / headless — still PASS if symbol linked
                auto fn = glGetString;
                if (fn != nullptr) {
                    r.status = PASS;
                    r.detail = "no GL context (headless?) — symbol linked OK";
                } else {
                    r.detail = "glGetString symbol missing";
                    r.fix    = "install OpenGL dev packages / GPU drivers";
                }
            }
        } else {
            // QApplication failed — can't create GL context
            auto fn = glGetString;
            if (fn != nullptr) {
                r.status = PASS;
                r.detail = "symbol linked OK (no Qt app for context)";
            } else {
                r.detail = "glGetString symbol missing";
                r.fix    = "install OpenGL dev packages / GPU drivers";
            }
        }
        add(r);
    }

    // rsvg-convert — SVG to PNG icon generation (build-time)
    {
        DepResult r{"0", "rsvg-convert", "", FAIL, "", ""};
#if defined(_WIN32)
        int rc = std::system("where rsvg-convert >nul 2>nul");
#else
        int rc = std::system("command -v rsvg-convert >/dev/null 2>&1");
#endif
        if (rc == 0) {
            // Try to get version
            FILE* fp = nullptr;
#if defined(_WIN32)
            fp = _popen("rsvg-convert --version 2>nul", "r");
#else
            fp = popen("rsvg-convert --version 2>/dev/null", "r");
#endif
            if (fp) {
                char buf[128] = {};
                if (fgets(buf, sizeof(buf), fp)) {
                    std::string ver(buf);
                    // Trim trailing newline
                    while (!ver.empty() && (ver.back() == '\n' || ver.back() == '\r'))
                        ver.pop_back();
                    // Extract version number (e.g., "rsvg-convert version 2.56.1")
                    auto pos = ver.rfind(' ');
                    if (pos != std::string::npos)
                        r.version = ver.substr(pos + 1);
                    else
                        r.version = ver;
                }
#if defined(_WIN32)
                _pclose(fp);
#else
                pclose(fp);
#endif
            }
            r.status = PASS;
            r.detail = "SVG to PNG conversion available";
        } else {
            r.detail = "rsvg-convert not found";
            const char* plat = platform_name();
            if      (std::string(plat) == "linux")
                r.fix = "sudo apt-get install -y librsvg2-bin";
            else if (std::string(plat) == "macos")
                r.fix = "brew install librsvg";
            else
                r.fix = "install librsvg / rsvg-convert";
        }
        add(r);
    }

    // icotool — .ico generation (Windows only, optional, build-time)
#if defined(_WIN32)
    {
        DepResult r{"0", "icotool", "", WARN, "", ""};
        int rc = std::system("where icotool >nul 2>nul");
        if (rc == 0) {
            FILE* fp = _popen("icotool --version 2>nul", "r");
            if (fp) {
                char buf[128] = {};
                if (fgets(buf, sizeof(buf), fp)) {
                    std::string ver(buf);
                    while (!ver.empty() && (ver.back() == '\n' || ver.back() == '\r'))
                        ver.pop_back();
                    auto pos = ver.rfind(' ');
                    if (pos != std::string::npos)
                        r.version = ver.substr(pos + 1);
                    else
                        r.version = ver;
                }
                _pclose(fp);
            }
            r.status = PASS;
            r.detail = "Windows .ico generation available";
        } else {
            r.detail = "icotool not found (optional — .ico generation disabled)";
            r.fix = "install icoutils";
        }
        add(r);
    }
#endif

    // =================================================================
    //  PHASE 1: Basic Modeling (optional)
    // =================================================================

#ifdef HAVE_SLVS
    {
        DepResult r{"1", "libslvs", "", PASS, "", ""};
        Slvs_System sys = {};
        Slvs_Param params[2];
        Slvs_Entity entities[1];

        params[0] = Slvs_MakeParam(1, 1, 10.0);
        params[1] = Slvs_MakeParam(2, 1, 20.0);
        sys.param  = params;
        sys.params = 2;

        // Args: entity_id, group, workplane, param_u, param_v
        // Use 0 for workplane (free in 3D space, but params define 2D position)
        entities[0] = Slvs_MakePoint2d(1, 1, 0, 1, 2);
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
        std::string fix;
        const char* plat = platform_name();
        if (std::string(plat) == "linux")
            fix = "sudo add-apt-repository ppa:ayourk/hobbycad && "
                  "sudo apt install libslvs-dev";
        else
            fix = "build from source: "
                  "https://github.com/solvespace/solvespace";
        DepResult r{"1", "libslvs", "not found", WARN,
            "needed for sketch constraint solving", fix};
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
            r.fix    = "install pybind11 + Python dev packages";
        }
        add(r);
    }
#else
    {
        std::string fix;
        const char* plat = platform_name();
        if (std::string(plat) == "linux")
            fix = "sudo apt install pybind11-dev python3-dev "
                  "python3-pybind11";
        else if (std::string(plat) == "windows")
            fix = "vcpkg install pybind11:x64-windows";
        else
            fix = "brew install pybind11 python";
        DepResult r{"3", "pybind11", "not found", WARN,
            "needed for Python scripting", fix};
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
        std::string fix;
        const char* plat = platform_name();
        if (std::string(plat) == "linux")
            fix = "sudo add-apt-repository ppa:ayourk/hobbycad && "
                  "sudo apt install libopenmesh-dev";
        else
            fix = "build from source: "
                  "https://www.graphics.rwth-aachen.de/software/openmesh/";
        DepResult r{"5", "OpenMesh", "not found", WARN,
            "needed for mesh half-edge operations", fix};
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
            r.fix    = "install lib3mf dev packages";
        }
        add(r);
    }
#else
    {
        std::string fix;
        const char* plat = platform_name();
        if (std::string(plat) == "linux")
            fix = "sudo add-apt-repository ppa:ayourk/hobbycad && "
                  "sudo apt install lib3mf-dev";
        else
            fix = "build from source: "
                  "https://github.com/3MFConsortium/lib3mf";
        DepResult r{"5", "lib3mf", "not found", WARN,
            "needed for 3MF format support", fix};
        add(r);
    }
#endif

#ifdef HAVE_MESHFIX
    {
        DepResult r{"5", "MeshFix", "", PASS,
            "library linked OK", ""};
        add(r);
    }
#else
    {
        std::string fix;
        const char* plat = platform_name();
        if (std::string(plat) == "linux")
            fix = "sudo add-apt-repository ppa:ayourk/hobbycad && "
                  "sudo apt install libmeshfix-dev";
        else
            fix = "build from source: "
                  "https://github.com/MarcoAttene/MeshFix-V2.1";
        DepResult r{"5", "MeshFix", "not found", WARN,
            "needed for automatic mesh repair", fix};
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
        std::string fix;
        const char* plat = platform_name();
        if (std::string(plat) == "linux")
            fix = "sudo apt install libcgal-dev";
        else if (std::string(plat) == "windows")
            fix = "vcpkg install cgal:x64-windows";
        else
            fix = "brew install cgal";
        DepResult r{"5", "CGAL", "not found", WARN,
            "needed for computational geometry algorithms", fix};
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
        std::string fix;
        const char* plat = platform_name();
        if (std::string(plat) == "linux")
            fix = "sudo apt install libopenvdb-dev";
        else if (std::string(plat) == "windows")
            fix = "vcpkg install openvdb:x64-windows";
        else
            fix = "brew install openvdb";
        DepResult r{"5", "OpenVDB", "not found", WARN,
            "needed for voxelization / Make Solid", fix};
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
        std::string fix;
        const char* plat = platform_name();
        if (std::string(plat) == "linux")
            fix = "sudo apt install libassimp-dev";
        else if (std::string(plat) == "windows")
            fix = "vcpkg install assimp:x64-windows";
        else
            fix = "brew install assimp";
        DepResult r{"5", "Assimp", "not found", WARN,
            "needed for multi-format mesh import", fix};
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
        std::string fix;
        const char* plat = platform_name();
        if (std::string(plat) == "linux")
            fix = "sudo apt install libeigen3-dev";
        else if (std::string(plat) == "windows")
            fix = "vcpkg install eigen3:x64-windows";
        else
            fix = "brew install eigen";
        DepResult r{"5", "Eigen", "not found", WARN,
            "needed for linear algebra (used by CGAL/MeshFix)", fix};
        add(r);
    }
#endif

    // =================================================================
    //  Report
    // =================================================================

    // Determine the highest phase where all tests passed (no FAIL).
    // A phase is considered successful if every test in that phase
    // has status PASS or WARN (WARN = optional dependency missing).
    int highest_pass_phase = -1;
    {
        // Collect unique phases and check each
        std::vector<std::string> phases;
        for (const auto& r : results) {
            if (phases.empty() || phases.back() != r.phase)
                phases.push_back(r.phase);
        }
        for (const auto& ph : phases) {
            bool phase_ok = true;
            for (const auto& r : results) {
                if (r.phase == ph && r.status == FAIL) {
                    phase_ok = false;
                    break;
                }
            }
            if (phase_ok) {
                int idx = std::stoi(ph);
                if (idx > highest_pass_phase)
                    highest_pass_phase = idx;
            } else {
                // Stop at first failed phase — higher phases
                // depend on lower ones
                break;
            }
        }
    }

    const char* phase_names[] = {
        "Phase 0: Foundation",
        "Phase 1: Basic Modeling",
        "Phase 2: Parametric Features",
        "Phase 3: Python / Plugins / Version Control",
        "Phase 4: Assemblies",
        "Phase 5: HobbyMesh"
    };

    // Generate report into a lambda that writes to any ostream
    auto write_report = [&](std::ostream& out, bool verbose) {
        if (verbose) {
            // Timestamp
            std::time_t now = std::time(nullptr);
            char timebuf[64];
            std::strftime(timebuf, sizeof(timebuf),
                          "%Y-%m-%d %H:%M:%S %Z", std::localtime(&now));
            out << "Timestamp: " << timebuf << "\n";

            // Compiler info
#if defined(__clang__)
    #if defined(__apple_build_version__)
            out << "Compiler:  Apple Clang " << __clang_version__ << "\n";
    #else
            out << "Compiler:  Clang " << __clang_version__ << "\n";
    #endif
#elif defined(__GNUC__)
            out << "Compiler:  GCC " << __GNUC__ << "."
                << __GNUC_MINOR__ << "." << __GNUC_PATCHLEVEL__ << "\n";
#elif defined(_MSC_VER)
            out << "Compiler:  MSVC " << _MSC_FULL_VER << "\n";
#else
            out << "Compiler:  unknown\n";
#endif

            // C++ standard
            out << "C++ std:   " << __cplusplus << "\n";

            // Architecture
#if defined(__x86_64__) || defined(_M_X64)
            out << "Arch:      x86_64\n";
#elif defined(__aarch64__) || defined(_M_ARM64)
            out << "Arch:      arm64\n";
#elif defined(__i386__) || defined(_M_IX86)
            out << "Arch:      x86\n";
#else
            out << "Arch:      unknown\n";
#endif

            // Build type
#ifdef NDEBUG
            out << "Build:     Release\n";
#else
            out << "Build:     Debug\n";
#endif
            out << "\n";
        }

        out << "===== HobbyCAD Dependency Check =====\n"
            << "Platform: " << platform_name() << "\n";

        std::string current_phase;
        for (const auto& r : results) {
            if (r.phase != current_phase) {
                current_phase = r.phase;
                int idx = std::stoi(current_phase);
                out << "\n  -- " << phase_names[idx] << " --\n";
            }

            const char* tag = (r.status == PASS) ? "PASS" :
                              (r.status == WARN) ? "WARN" : "FAIL";
            out << "  [" << tag << "] " << r.name;
            if (!r.version.empty())
                out << " " << r.version;
            if (!r.detail.empty())
                out << " — " << r.detail;
            out << "\n";
            if (!r.fix.empty())
                out << "         -> " << r.fix << "\n";
        }

        out << "\n===== Results: "
            << pass << " passed, "
            << warn << " warnings, "
            << fail << " failed"
            << " out of " << (pass + warn + fail)
            << " =====\n";

        if (fail > 0) {
            out << "\nPhase 0 dependencies are MISSING. "
                << "HobbyCAD cannot build.\n"
                << "See dev_environment_setup.txt for "
                << "troubleshooting.\n";
        } else if (warn > 0) {
            out << "\nPhase 0 OK. Optional dependencies above "
                << "can be installed when needed.\n";
        } else {
            out << "\nAll dependencies installed. "
                << "Ready for all phases.\n";
        }

        if (highest_pass_phase >= 0) {
            out << "\nHighest phase passed: "
                << highest_pass_phase << " ("
                << phase_names[highest_pass_phase] << ")\n";
        }

        // Machine-readable final line for build-dev scripts.
        // build-dev.sh parses lines starting with "DEVTEST_RESULT:"
        if (fail > 0) {
            out << "\nDEVTEST_RESULT: [FAIL] Missing Phase 0 dependencies\n";
        } else {
            out << "\nDEVTEST_RESULT: [PASS] Success!  Good up to and "
                << "including Phase " << highest_pass_phase << "\n";
        }
    };

    // Write to stdout (compact)
    write_report(std::cout, false);

    // Append runtime results to devtest.log (started by CMake)
    const char* log_path =
#ifdef DEPCHECK_LOG_PATH
        DEPCHECK_LOG_PATH;
#else
        "devtest.log";
#endif

    std::ofstream logfile(log_path, std::ios::app);
    if (logfile.is_open()) {
        logfile << "--- Runtime Results ---\n\n";
        write_report(logfile, true);
        logfile.close();
        std::cout << "\nLog written to " << log_path << "\n";
    } else {
        std::cerr << "\nWarning: could not write " << log_path << "\n";
    }

    return fail > 0 ? 1 : 0;
}

