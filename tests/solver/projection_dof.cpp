// =====================================================================
//  tests/solver/projection_dof.cpp
//  A projected entity is reference geometry: it must add ZERO degrees of
//  freedom (it is never registered with the solver).
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#include <hobbycad/sketch/solver.h>
#include <hobbycad/sketch/entity.h>
#include <cstdio>

using namespace hobbycad;
using namespace hobbycad::sketch;

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

static Entity line(int id, Point2D a, Point2D b) {
    Entity e; e.id = id; e.type = EntityType::Line; e.points = {a, b}; return e;
}

int main() {
    std::printf("projection adds zero DOF\n");
    // A test that does not run cannot fail, so it must not pass.
    if (!Solver::isAvailable()) { std::printf("  [FAIL] cannot run: solver unavailable\n"); return 1; }

    // One free line: 4 DOF.
    {
        Solver s; std::vector<Entity> ents = { line(1, {0,0}, {10,0}) };
        SolveResult r = s.solve(ents, {});
        check(r.success && r.dof == 4, "lone line = 4 DOF (baseline)");
    }
    // Same line plus a PROJECTED line: still 4 DOF (projection not solved).
    {
        Solver s;
        Entity proj = line(2, {0,5}, {10,5});
        proj.projectionSourceId = 1;              // reference geometry
        proj.projectionSourceSketchId = 99;       // from another sketch
        std::vector<Entity> ents = { line(1, {0,0}, {10,0}), proj };
        SolveResult r = s.solve(ents, {});
        check(r.success, "solve succeeds with a projection present");
        check(r.dof == 4, "projected line adds ZERO DOF (still 4)");
    }
    std::printf("\n%s (%d failure(s))\n", failures ? "FAILURES" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
