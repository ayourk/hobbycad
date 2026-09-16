// =====================================================================
//  tests/project/points3d.cpp
//  Entity points are 3D internally: a non-zero z round-trips through save/
//  load, and a planar (z==0) point stays 2D (back-compatible on disk).
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#include <hobbycad/project.h>
#include <hobbycad/sketch/entity.h>
#include <cmath>
#include <cstdio>
#include <filesystem>

using namespace hobbycad;
namespace fs = std::filesystem;
static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}
static bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

int main(int argc, char** argv) {
    const std::string base = argc > 1 ? argv[1] : "/tmp/hobbycad_points3d";
    fs::remove_all(base); fs::create_directories(base);
    const std::string dir = base + "/proj";
    std::printf("3D point round-trip\n");

    {
        Project p; SketchData s; s.name = "proj";
        // A genuinely 3D line (off-plane z), and a planar line (z == 0).
        SketchEntityData l3; l3.type = sketch::EntityType::Line;
        l3.points = { {1.0, 2.0, 3.0}, {4.0, 5.0, 6.0} };
        SketchEntityData l2; l2.type = sketch::EntityType::Line;
        l2.points = { {10.0, 20.0}, {30.0, 40.0} };   // z defaults to 0
        s.entities.push_back(l3);
        s.entities.push_back(l2);
        p.addSketch(s);
        std::string err; check(p.save(dir, &err), "save");
    }
    {
        Project p; std::string err; check(p.load(dir, &err), "load");
        if (!p.sketches().empty() && p.sketches()[0].entities.size() == 2) {
            const auto& e3 = p.sketches()[0].entities[0];
            const auto& e2 = p.sketches()[0].entities[1];
            check(e3.points.size() == 2 &&
                  near(e3.points[0].x,1) && near(e3.points[0].y,2) && near(e3.points[0].z,3) &&
                  near(e3.points[1].x,4) && near(e3.points[1].y,5) && near(e3.points[1].z,6),
                  "3D line: x, y AND z survive the round trip");
            check(e2.points.size() == 2 &&
                  near(e2.points[0].z, 0.0) && near(e2.points[1].z, 0.0),
                  "planar line: z stays 0 (2D is the z==0 case)");
            check(near(e2.points[0].x,10) && near(e2.points[0].y,20),
                  "planar line: x, y intact");
        } else check(false, "two entities load");
    }

    if (failures == 0) std::printf("points3d: ALL PASS\n");
    else               std::printf("points3d: %d FAILURE(S)\n", failures);
    return failures ? 1 : 0;
}
