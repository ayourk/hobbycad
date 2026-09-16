// =====================================================================
//  tests/project/projection.cpp
//  Projection groundwork: projectionSourceId round trip, plane bases,
//  and the cross-plane re-derive math (updateProjectionFromSource).
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#include <hobbycad/project.h>
#include <hobbycad/sketch/operations.h>
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
static bool near(double a, double b) { return std::fabs(a - b) < 1e-4; }

static sketch::Entity line2(int id, Point2D a, Point2D b) {
    sketch::Entity e; e.id = id; e.type = sketch::EntityType::Line; e.points = {a, b};
    return e;
}

int main(int argc, char** argv) {
    const std::string base = argc > 1 ? argv[1] : "/tmp/hobbycad_projection";
    fs::remove_all(base); fs::create_directories(base);
    std::printf("projection groundwork\n");

    // 1. projectionSourceId round trip
    const std::string dir = base + "/proj";
    {
        Project p; SketchData s; s.name = "proj";
        SketchEntityData src; src.type = sketch::EntityType::Line;
        src.points = { {0,0}, {10,0} };
        s.entities.push_back(src);
        SketchEntityData proj; proj.type = sketch::EntityType::Line;
        proj.points = { {0,0}, {10,0} };
        proj.projectionSourceId = 1;              // projection of entity 1
        s.entities.push_back(proj);
        p.addSketch(s);
        std::string err; check(p.save(dir, &err), "save");
    }
    {
        Project p; std::string err; check(p.load(dir, &err), "load");
        if (!p.sketches().empty() && p.sketches()[0].entities.size() == 2) {
            check(p.sketches()[0].entities[0].projectionSourceId == -1, "plain entity has no projection link");
            check(p.sketches()[0].entities[1].projectionSourceId == 1, "projectionSourceId survives the round trip");
        } else check(false, "two entities load");
    }

    // 2. plane bases (convention: XZ u=+X v=+Z n=+Y ; YZ u=+Y v=+Z n=+X)
    {
        PlaneBasis xz = planeBasisFor(SketchPlane::XZ);
        check(near(xz.uAxis.x,1) && near(xz.vAxis.z,1) && near(xz.normal.y,-1), "XZ basis: u=+X, v=+Z, n=-Y (right-handed)");
        PlaneBasis yz = planeBasisFor(SketchPlane::YZ);
        check(near(yz.uAxis.y,1) && near(yz.vAxis.z,1) && near(yz.normal.x,1), "YZ basis: u=+Y, v=+Z, n=+X");
    }

    // 3. cross-plane projection: an XZ line projected onto YZ (X collapses, Z kept)
    {
        sketch::Entity src = line2(1, {10,20}, {30,40});   // on XZ: u=X, v=Z
        sketch::Entity proj = line2(2, {0,0}, {0,0}); proj.projectionSourceId = 1;
        bool ok = sketch::updateProjectionFromSource(
            proj, src, planeBasisFor(SketchPlane::XZ), planeBasisFor(SketchPlane::YZ));
        check(ok, "XZ->YZ projection runs");
        check(proj.points.size()==2 && near(proj.points[0].x,0) && near(proj.points[0].y,20)
              && near(proj.points[1].x,0) && near(proj.points[1].y,40),
              "XZ->YZ: X collapses, Z preserved -> (0,20),(0,40)");
    }

    // 4. foreshortening onto a plane tilted 45 deg about X (custom basis)
    {
        const float c = 0.70710678f;
        PlaneBasis tilt;                       // XY basis rotated 45 deg about X
        tilt.origin = {0,0,0}; tilt.uAxis = {1,0,0}; tilt.vAxis = {0,c,c}; tilt.normal = {0,-c,c};
        sketch::Entity src = line2(1, {10,10}, {20,20});   // on XY
        sketch::Entity proj = line2(2, {0,0}, {0,0}); proj.projectionSourceId = 1;
        sketch::updateProjectionFromSource(proj, src, planeBasisFor(SketchPlane::XY), tilt);
        // XY point (u,v)->world (u,v,0); onto tilt: pu=u, pv=v*c
        check(near(proj.points[0].x,10) && near(proj.points[0].y,10*c)
              && near(proj.points[1].x,20) && near(proj.points[1].y,20*c),
              "45-deg plane foreshortens v by cos45 -> (10,7.07),(20,14.14)");
    }

    std::printf("\n%s (%d failure(s))\n", failures ? "FAILURES" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
