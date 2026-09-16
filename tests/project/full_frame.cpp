// tests/project/full_frame.cpp — a custom sketch resolves its construction
// plane's FULL frame (primary+secondary+roll), and it survives save/load.
// SPDX-License-Identifier: GPL-3.0-only
#include <hobbycad/project.h>
#include <hobbycad/plane_frame.h>
#include <cstdio>
#include <cmath>
#include <string>
using namespace hobbycad;
static int fails = 0;
static void ck(bool ok, const char* w) { std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", w); if (!ok) ++fails; }
static bool feq(float a, float b, float e = 1e-4f) { return std::fabs(a - b) < e; }
static bool vsame(const Vec3& a, const Vec3& b) { return feq(a.x,b.x) && feq(a.y,b.y) && feq(a.z,b.z); }
static Vec3 cross(const Vec3& a, const Vec3& b){ return Vec3(a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x); }
static bool rightHanded(const PlaneBasis& b){ Vec3 c=cross(b.uAxis,b.vAxis); return vsame(c,b.normal); }

// Add a construction plane (XY base) with the given rotations; return its id.
static int addCP(Project& p, double primary, double secondary, double roll) {
    ConstructionPlaneData cp;
    cp.type = ConstructionPlaneType::OffsetFromOrigin; cp.basePlane = SketchPlane::XY;
    cp.primaryAxis = PlaneRotationAxis::X; cp.primaryAngle = primary;
    cp.secondaryAxis = PlaneRotationAxis::Y; cp.secondaryAngle = secondary;
    cp.rollAngle = roll;
    p.addConstructionPlane(cp);
    return p.constructionPlanes().back().id;
}
static PlaneBasis basisOfCustomSketch(Project& p, int cid) {
    SketchData s; s.plane = SketchPlane::Custom; s.constructionPlaneId = cid;
    return planeBasisFor(s, p);
}

int main(int argc, char** argv) {
    const std::string dir = (argc > 1 ? std::string(argv[1]) : "/tmp/hobbycad_fullframe") + "_proj";
    std::printf("construction-plane full frame\n");

    Project full;  const int cFull = addCP(full, 30, 40, 25);
    Project prim;  const int cPrim = addCP(prim, 30,  0,  0);
    Project noRoll;const int cNR   = addCP(noRoll, 30, 40, 0);

    const PlaneBasis bFull = basisOfCustomSketch(full, cFull);
    const PlaneBasis bPrim = basisOfCustomSketch(prim, cPrim);
    const PlaneBasis bNR   = basisOfCustomSketch(noRoll, cNR);

    ck(rightHanded(bFull), "full-frame basis is right-handed");
    ck(!vsame(bFull.normal, bPrim.normal),
       "secondary rotation changes the normal (full frame, not first-rotation-only)");
    ck(vsame(bFull.normal, bNR.normal),
       "roll leaves the normal unchanged (it spins in-plane)");
    ck(!vsame(bFull.uAxis, bNR.uAxis),
       "roll rotates the in-plane u axis (roll is applied)");

    // Save/load: a custom sketch's construction-plane link + the plane survive,
    // so the full frame still resolves.
    { SketchData s; s.name = "on-cp"; s.plane = SketchPlane::Custom; s.constructionPlaneId = cFull;
      full.addSketch(s);
      std::string err; ck(full.save(dir, &err), "save project with a custom-plane sketch"); }
    { Project r; std::string err; ck(r.load(dir, &err), "reload project");
      // find the reloaded sketch
      const SketchData* rs = nullptr;
      for (const auto& sk : r.sketches()) if (sk.plane == SketchPlane::Custom) rs = &sk;
      ck(rs && rs->constructionPlaneId >= 0, "reloaded sketch kept its construction-plane id");
      if (rs) {
          const PlaneBasis rb = planeBasisFor(*rs, r);
          ck(vsame(rb.normal, bFull.normal) && vsame(rb.uAxis, bFull.uAxis),
             "full frame (normal + u) survives save/load");
      }
    }

    if (fails == 0) std::printf("full_frame: ALL PASS\n");
    else            std::printf("full_frame: %d FAILURE(S)\n", fails);
    return fails ? 1 : 0;
}
