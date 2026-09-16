// =====================================================================
//  tests/project/plane_basis.cpp — planeBasisFor frames are right-handed
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  The sketch point<->world mapping (plane-local storage) is only correct
//  if planeBasisFor returns a right-handed frame that matches the viewport.
//  Pins: every canonical plane is right-handed (u x v == normal), XZ is the
//  fixed normal=-Y with v=+Z (no geometry mirror), the SketchData overload
//  resolves inline rotation and construction planes, and world/project
//  round-trips on a rotated basis. Fails meaningfully if a sign regresses.
#include <hobbycad/project.h>
#include <hobbycad/plane_frame.h>
#include <cstdio>
#include <cmath>
using namespace hobbycad;

static int fails = 0;
static void ck(bool ok, const char* w) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", w);
    if (!ok) ++fails;
}
static bool feq(float a, float b, float e = 1e-4f) { return std::fabs(a - b) < e; }
static bool veq(const Vec3& a, float x, float y, float z) {
    return feq(a.x, x) && feq(a.y, y) && feq(a.z, z);
}
static Vec3 cross(const Vec3& a, const Vec3& b) {
    return Vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
static bool rightHanded(const PlaneBasis& b) {
    const Vec3 c = cross(b.uAxis, b.vAxis);
    return feq(c.x, b.normal.x) && feq(c.y, b.normal.y) && feq(c.z, b.normal.z);
}

int main() {
    std::printf("planeBasisFor frames\n");

    // 1. Every canonical plane is right-handed: n = u x v.
    ck(rightHanded(planeBasisFor(SketchPlane::XY)), "XY right-handed (u x v = n)");
    ck(rightHanded(planeBasisFor(SketchPlane::YZ)), "YZ right-handed");
    ck(rightHanded(planeBasisFor(SketchPlane::XZ)), "XZ right-handed (the fix)");

    // 2. XZ: v stays +Z (up, no geometry mirror), normal flipped to -Y.
    const PlaneBasis xz = planeBasisFor(SketchPlane::XZ);
    ck(veq(xz.uAxis, 1, 0, 0),  "XZ u = +X (right)");
    ck(veq(xz.vAxis, 0, 0, 1),  "XZ v = +Z (up, unchanged)");
    ck(veq(xz.normal, 0, -1, 0), "XZ normal = -Y");
    const PlaneBasis xzo = planeBasisFor(SketchPlane::XZ, 5.0);
    ck(veq(xzo.origin, 0, -5, 0), "XZ offset 5 -> origin (0,-5,0) along -Y normal");

    // 3. SketchData overload, canonical path == closed form.
    Project proj;
    SketchData s; s.plane = SketchPlane::XZ; s.planeOffset = 5.0; s.constructionPlaneId = -1;
    const PlaneBasis sb = planeBasisFor(s, proj);
    ck(veq(sb.normal, 0, -1, 0) && veq(sb.origin, 0, -5, 0),
       "SketchData XZ canonical matches closed form");

    // 4. Inline rotation: XY rotated +90 deg about X reproduces the XZ frame.
    SketchData r; r.plane = SketchPlane::XY; r.constructionPlaneId = -1;
    r.rotationAxis = PlaneRotationAxis::X; r.rotationAngle = 90.0;
    const PlaneBasis rb = planeBasisFor(r, proj);
    ck(rightHanded(rb), "inline-rotated basis right-handed");
    ck(veq(rb.uAxis, 1, 0, 0), "XY+90degX -> u = +X");
    ck(veq(rb.vAxis, 0, 0, 1), "XY+90degX -> v = +Z");
    ck(veq(rb.normal, 0, -1, 0), "XY+90degX -> normal = -Y (== XZ)");

    // 5. Construction-plane path resolved through the Project.
    ConstructionPlaneData cp;
    cp.type = ConstructionPlaneType::OffsetFromOrigin;
    cp.basePlane = SketchPlane::XY;
    cp.primaryAxis = PlaneRotationAxis::X;
    cp.primaryAngle = 90.0;
    proj.addConstructionPlane(cp);
    const int cid = proj.constructionPlanes().back().id;
    SketchData cs; cs.plane = SketchPlane::Custom; cs.constructionPlaneId = cid;
    const PlaneBasis cb = planeBasisFor(cs, proj);
    ck(rightHanded(cb), "construction-plane basis right-handed");
    ck(veq(cb.normal, 0, -1, 0), "construction XY+90degX -> normal = -Y");

    // 6. Plane-local -> world -> plane-local round-trips on a rotated basis.
    const Point3 p{3.0, 4.0, 0.0};
    const Point3 back = p.world(rb).project(rb);
    ck(feq((float)back.x, 3.0f) && feq((float)back.y, 4.0f) && feq((float)back.z, 0.0f),
       "world/project round-trips on rotated basis");

    // --- arbitraryAxisBasis: no-history plane frames from a bare normal ---
    ck(rightHanded(arbitraryAxisBasis(Vec3(0,0,1))), "AAA(+Z) right-handed");
    { const PlaneBasis b = arbitraryAxisBasis(Vec3(0,0,1));
      ck(veq(b.uAxis,1,0,0) && veq(b.vAxis,0,1,0) && veq(b.normal,0,0,1), "AAA(+Z) == identity XY frame"); }
    { const PlaneBasis b = arbitraryAxisBasis(Vec3(0,0,-1));   // classic DXF mirror
      ck(rightHanded(b) && veq(b.uAxis,-1,0,0) && veq(b.vAxis,0,1,0) && veq(b.normal,0,0,-1),
         "AAA(-Z): u=-X, v=+Y, n=-Z (right-handed mirror)"); }
    { const PlaneBasis b = arbitraryAxisBasis(Vec3(1,0,0));    // normal along +X
      ck(rightHanded(b) && veq(b.uAxis,0,1,0) && veq(b.vAxis,0,0,1) && veq(b.normal,1,0,0),
         "AAA(+X): u=+Y, v=+Z, n=+X (YZ-like)"); }
    { const float c = 0.57735026918962584f;                    // 1/sqrt(3)
      ck(rightHanded(arbitraryAxisBasis(Vec3(c,c,c))), "AAA(tilted 1,1,1) right-handed"); }

    if (fails == 0) std::printf("plane_basis: ALL PASS\n");
    else            std::printf("plane_basis: %d FAILURE(S)\n", fails);
    return fails ? 1 : 0;
}
