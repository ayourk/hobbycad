// =====================================================================
//  tests/project/ellipse.cpp — ellipse rotation survives save/load
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#include <hobbycad/project.h>
#include <cstdio>
#include <cmath>
#include <filesystem>
using namespace hobbycad;
namespace fs = std::filesystem;
static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what); if (!ok) ++failures;
}
static bool near(double a, double b, double e = 1e-6) { return std::fabs(a - b) < e; }

int main(int argc, char** argv) {
    const std::string base = argc > 1 ? argv[1] : "/tmp/hobbycad_ellipse";
    fs::remove_all(base); fs::create_directories(base);
    std::printf("ellipse rotation round trip\n");
    const std::string dir = base + "/proj";
    {
        Project p;
        SketchData s; s.name = "ell";
        SketchEntityData e; e.type = sketch::EntityType::Ellipse;
        e.points = { {0.0, 0.0} };
        e.majorRadius = 5.0; e.minorRadius = 2.0; e.ellipseRotation = 37.5;
        e.ellipseStart = 20.0; e.ellipseSweep = 250.0;
        s.entities.push_back(e);
        p.addSketch(s);
        std::string err; check(p.save(dir, &err), "save succeeds");
    }
    {
        Project p; std::string err;
        check(p.load(dir, &err), "load succeeds");
        if (!p.sketches().empty() && !p.sketches()[0].entities.empty()) {
            const auto& e = p.sketches()[0].entities[0];
            check(near(e.ellipseRotation, 37.5), "ellipse rotation survives the round trip");
            check(near(e.majorRadius, 5.0) && near(e.minorRadius, 2.0), "radii survive too");
            check(near(e.ellipseStart, 20.0) && near(e.ellipseSweep, 250.0),
                  "elliptical-arc range survives save/load");
        } else check(false, "the ellipse loads");
    }
    std::printf("\n%s (%d failure(s))\n", failures ? "FAILURES" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
