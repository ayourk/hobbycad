// =====================================================================
//  tests/solver/solve3d_wrapper.cpp
//  Solver::solve3D: 3D sketch solving THROUGH the HobbyCAD wrapper (not raw
//  libslvs). Proves solver.cpp can build/solve first-class 3D points, pin them
//  to a plane (the 2D-mode case), and report 3D free points. The 2D solve()
//  path is untouched (its own suites cover it).
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#include <hobbycad/sketch/solver.h>
#include <cmath>
#include <cstdio>

using namespace hobbycad;
using namespace hobbycad::sketch;

static int fails = 0;
static void ck(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++fails;
}

int main() {
    // A test that does not run cannot fail, so it must not pass.
    if (!Solver::isAvailable()) {
        std::printf("solve3d_wrapper: [FAIL] cannot run: libslvs not available\n");
        return 1;
    }
    Solver solver;
    const PlaneBasis xy;   // defaults: origin 0, u=X, v=Y, n=Z

    std::printf("Solver::solve3D\n");

    // ---- A. two free 3D points at distance 10 --------------------------
    {
        std::vector<Sketch3DPoint> pts = {
            {1, {0.0, 0.0, 0.0}, false, false},
            {2, {3.0, 0.0, 0.0}, false, false},
        };
        std::vector<Sketch3DConstraint> cons = {
            {Sketch3DConstraint::Distance, 1, 2, 10.0},
        };
        SolveResult r = solver.solve3D(pts, cons, xy);
        ck(r.success, "A: distance system solved");
        const double dx = pts[1].pos.x - pts[0].pos.x;
        const double dy = pts[1].pos.y - pts[0].pos.y;
        const double dz = pts[1].pos.z - pts[0].pos.z;
        ck(std::fabs(std::sqrt(dx*dx+dy*dy+dz*dz) - 10.0) < 1e-6,
           "A: the two 3D points end up 10 apart");
        ck(r.dof == 5, "A: one distance leaves 5 of 6 DOF");
    }

    // ---- B. a point pinned to the plane: 2 DOF, pulled onto z=0 --------
    {
        std::vector<Sketch3DPoint> pts = {
            {1, {4.0, 5.0, 6.0}, /*onPlane*/true, false},
        };
        SolveResult r = solver.solve3D(pts, {}, xy);
        ck(r.success, "B: on-plane point solved");
        ck(r.dof == 2, "B: an on-plane 3D point has 2 DOF (a 2D sketch point)");
        ck(std::fabs(pts[0].pos.z) < 1e-6, "B: it is pulled onto the plane (z=0)");
        ck(r.freePointsValid && r.freePoints.size() == 1 && r.freePoints[0].first == 1,
           "B: reported as a free point");
    }

    // ---- C. coincident with a fixed reference: 0 DOF, lands on it ------
    {
        std::vector<Sketch3DPoint> pts = {
            {1, {2.0, 3.0, 4.0}, false, /*fixed*/true},
            {2, {0.0, 0.0, 0.0}, false, false},
        };
        std::vector<Sketch3DConstraint> cons = {
            {Sketch3DConstraint::Coincident, 1, 2, 0.0},
        };
        SolveResult r = solver.solve3D(pts, cons, xy);
        ck(r.success, "C: coincidence solved");
        ck(r.dof == 0, "C: coincident to a fixed point removes all 3 DOF");
        ck(std::fabs(pts[1].pos.x-2.0) < 1e-6 &&
           std::fabs(pts[1].pos.y-3.0) < 1e-6 &&
           std::fabs(pts[1].pos.z-4.0) < 1e-6, "C: the point lands on the fixed reference");
        ck(r.state == SketchState::FullyConstrained, "C: state is FullyConstrained");
    }

    if (fails == 0) std::printf("solve3d_wrapper: ALL PASS\n");
    else            std::printf("solve3d_wrapper: %d FAILURE(S)\n", fails);
    return fails ? 1 : 0;
}
