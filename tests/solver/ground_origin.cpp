// =====================================================================
//  tests/solver/ground_origin.cpp — Coincident to the sketch origin grounds
//  a point (removes 2 DOF, pins it to (0,0)). This is the "grounding" a sketch
//  needs so it stops floating/orbiting when dragged.
//  SPDX-License-Identifier: GPL-3.0-only
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
    std::printf("Coincident to the sketch origin grounds a point\n");

    // A single free point away from the origin: 2 DOF on its own.
    {
        std::vector<Entity> es{ createPoint(1, {5, 5}) };
        Constraint g; g.id = 1; g.type = ConstraintType::Coincident;
        g.entityIds = {1, kSketchOriginEntity}; g.pointIndices = {0, 0}; g.enabled = true;
        std::vector<Constraint> cs{ g };
        Solver s; SolveResult r = s.solve(es, cs);
        std::printf("  solved=%d dof=%d  p=(%.4f,%.4f)\n",
                    r.success, r.dof, es[0].points[0].x, es[0].points[0].y);
        ck(r.success, "solve succeeds with a coincident-to-origin");
        ck(std::fabs(es[0].points[0].x) < 1e-6 && std::fabs(es[0].points[0].y) < 1e-6,
           "the point is pulled onto the origin (0,0)");
        ck(r.dof == 0, "grounding a lone point removes both DOF (dof==0)");
    }

    // A free line (4 DOF): ground one endpoint -> that end sits at the origin
    // and the DOF drops by 2 (to 2: the other end is still free).
    {
        std::vector<Entity> es{ createLine(1, {3, 4}, {10, 2}) };
        Constraint g; g.id = 1; g.type = ConstraintType::Coincident;
        g.entityIds = {1, kSketchOriginEntity}; g.pointIndices = {0, 0}; g.enabled = true;
        std::vector<Constraint> cs{ g };
        Solver s; SolveResult r = s.solve(es, cs);
        std::printf("  solved=%d dof=%d  end0=(%.4f,%.4f)\n",
                    r.success, r.dof, es[0].points[0].x, es[0].points[0].y);
        ck(r.success, "solve succeeds grounding a line endpoint");
        ck(std::fabs(es[0].points[0].x) < 1e-6 && std::fabs(es[0].points[0].y) < 1e-6,
           "the grounded endpoint sits at the origin");
        ck(r.dof == 2, "grounding one endpoint of a free line removes 2 DOF (4 -> 2)");
    }

    if (fails == 0) std::printf("ground_origin: ALL PASS\n");
    else            std::printf("ground_origin: %d FAILURE(S)\n", fails);
    return fails ? 1 : 0;
}
