// =====================================================================
//  tests/units/point3.cpp — Point3::world(PlaneBasis) / project(PlaneBasis)
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  world() lifts a plane-local (u,v,w) point to world; project() is its
//  inverse (world -> plane-local). Proves the round trip and that on YZ the
//  hidden world-X equals the off-plane w (Aaron's "X hidden on YZ").
#include "hobbycad/types.h"
#include <cmath>
#include <cstdio>

using namespace hobbycad;
static int fails = 0;
static void ck(bool ok, const char* w) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", w);
    if (!ok) ++fails;
}
static bool near(double a, double b) { return std::fabs(a - b) < 1e-6; }
static bool eqp(const Point3& p, double x, double y, double z) {
    return near(p.x, x) && near(p.y, y) && near(p.z, z);
}

int main() {
    std::printf("Point3 world/project\n");

    // XY (identity basis): world == local
    {
        PlaneBasis xy;                       // defaults: origin 0, u=X, v=Y, n=Z
        Point3 p(3, 4, 5);
        ck(eqp(p.world(xy), 3, 4, 5), "XY: world == local (identity basis)");
        ck(eqp(p.world(xy).project(xy), 3, 4, 5), "XY: round-trip");
    }

    // YZ: u=Y, v=Z, w=X. A plane-local (u,v,w) maps to world (w, u, v).
    {
        PlaneBasis yz;
        yz.uAxis = Vec3(0, 1, 0); yz.vAxis = Vec3(0, 0, 1); yz.normal = Vec3(1, 0, 0);
        Point3 local(2, 3, 4);               // u=2, v=3, w=4
        Point3 w = local.world(yz);
        ck(eqp(w, 4, 2, 3), "YZ: local (u,v,w)=(2,3,4) -> world (x=w=4, y=u=2, z=v=3)");
        ck(eqp(w.project(yz), 2, 3, 4), "YZ: project(world) recovers (u,v,w)");
        ck(near(w.x, local.z), "YZ: the hidden world-X equals the off-plane w");
    }

    // Offset origin passes through.
    {
        PlaneBasis b; b.origin = Vec3(10, 20, 30);
        Point3 p(1, 2, 3);
        ck(eqp(p.world(b), 11, 22, 33), "offset origin: world adds the origin");
        ck(eqp(p.world(b).project(b), 1, 2, 3), "offset origin: round-trip");
    }

    // Arbitrary orthonormal (angled) basis round-trips.
    {
        PlaneBasis b;
        const float s = static_cast<float>(std::sqrt(0.5));
        b.uAxis = Vec3(s, s, 0); b.vAxis = Vec3(-s, s, 0); b.normal = Vec3(0, 0, 1);
        b.origin = Vec3(1, 1, 1);
        Point3 p(2.5, -1.5, 3.0);
        ck(eqp(p.world(b).project(b), 2.5, -1.5, 3.0), "angled basis: world->project round-trip");
    }

    if (fails == 0) std::printf("point3: ALL PASS\n");
    else            std::printf("point3: %d FAILURE(S)\n", fails);
    return fails ? 1 : 0;
}
