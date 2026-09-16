// =====================================================================
//  tests/solver/solve_preserves_z.cpp
//  The main 2D solve must leave off-plane z untouched: the property the
//  whole 3D-sketch story rests on (z is edited/stored in 3D mode; the 2D
//  solve runs on the plane and must not clobber it). Guards against a future
//  solver change silently zeroing z. SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#include <hobbycad/sketch/entity.h>
#include <hobbycad/sketch/constraint.h>
#include <hobbycad/sketch/solver.h>
#include <cstdio>
#include <cmath>
#include <vector>
using namespace hobbycad;
using namespace hobbycad::sketch;

static int fails = 0;
static void ck(bool ok, const char* w) { std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", w); if (!ok) ++fails; }

int main() {
    installSolverFatalHandler();
    std::printf("main solve preserves off-plane z\n");

    // Two points carrying non-zero z (as a 3D-mode edit would). p1 fixed;
    // a distance drives p2 in-plane. z on both must survive the solve.
    std::vector<Entity> es{ createPoint(1, {0, 0}), createPoint(2, {10, 0}) };
    es[0].points[0].z = 3.0;
    es[1].points[0].z = 7.0;

    Constraint fix; fix.id = 1; fix.type = ConstraintType::FixedPoint;
    fix.entityIds = {1}; fix.pointIndices = {0}; fix.enabled = true;
    Constraint dist; dist.id = 2; dist.type = ConstraintType::Distance;
    dist.entityIds = {1, 2}; dist.pointIndices = {0, 0}; dist.value = 20.0; dist.enabled = true;
    std::vector<Constraint> cs{ fix, dist };

    Solver s;
    SolveResult r = s.solve(es, cs);
    std::printf("solved=%d  p1=(%.3f,%.3f,%.3f)  p2=(%.3f,%.3f,%.3f)\n",
                r.success, es[0].points[0].x, es[0].points[0].y, es[0].points[0].z,
                es[1].points[0].x, es[1].points[0].y, es[1].points[0].z);
    ck(r.success, "the 2D distance solve succeeds");
    ck(std::fabs(es[0].points[0].z - 3.0) < 1e-9, "fixed point keeps its off-plane z (3)");
    ck(std::fabs(es[1].points[0].z - 7.0) < 1e-9, "driven point keeps its off-plane z (7)");
    const double d = std::hypot(es[1].points[0].x - es[0].points[0].x,
                                es[1].points[0].y - es[0].points[0].y);
    ck(std::fabs(d - 20.0) < 1e-6, "and the in-plane distance solved to 20");

    if (fails == 0) std::printf("solve_preserves_z: ALL PASS\n");
    else            std::printf("solve_preserves_z: %d FAILURE(S)\n", fails);
    return fails ? 1 : 0;
}
