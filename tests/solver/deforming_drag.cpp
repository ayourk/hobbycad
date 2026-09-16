// =====================================================================
//  tests/solver/deforming_drag.cpp — holding non-dragged points during a
//  drag makes the grabbed element deform instead of shoving distant geometry
//  (the "deforming body-drag" / real-DOF behavior). SPDX-License-Identifier: GPL-3.0-only
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

// An L: line1 (A-B) meets line2 (B-C) at B; line2's length is dimensioned.
// "Drag" line1 up by 5. C is the distant point.
static double runDrag(int mode, double* outAngleDeg = nullptr) {   // 0=none 1=soft-hold 2=hard-fix far
    std::vector<Entity> es{
        createLine(1, {0, 0}, {10, 0}),    // line1 A-B
        createLine(2, {10, 0}, {10, 10}),  // line2 B-C
    };
    Constraint coin; coin.id = 1; coin.type = ConstraintType::Coincident;
    coin.entityIds = {1, 2}; coin.pointIndices = {1, 0}; coin.enabled = true;  // B shared
    Constraint dist; dist.id = 2; dist.type = ConstraintType::Distance;
    dist.entityIds = {2, 2}; dist.pointIndices = {0, 1}; dist.value = 10.0; dist.enabled = true;
    std::vector<Constraint> cs{ coin, dist };
    if (mode == 2) {   // hard-fix the far point C during the drag
        Constraint fix; fix.id = 3; fix.type = ConstraintType::FixedPoint;
        fix.entityIds = {2}; fix.pointIndices = {1}; fix.enabled = true;
        cs.push_back(fix);
    }

    // Drag line1 up by (0,5): move its points, keep C where it is.
    es[0].points[0] = {0, 5};
    es[0].points[1] = {10, 5};
    std::vector<std::pair<int,int>> dragged{ {1,0}, {1,1} };
    if (mode == 1) dragged.push_back({2, 1});   // soft-hold C

    Solver s; s.setDraggedPoints(dragged); s.solve(es, cs);
    if (outAngleDeg) {
        // Angle between line1 (A-B) and line2 (B-C) after the solve.
        const double d1x = es[0].points[1].x - es[0].points[0].x;
        const double d1y = es[0].points[1].y - es[0].points[0].y;
        const double d2x = es[1].points[1].x - es[1].points[0].x;
        const double d2y = es[1].points[1].y - es[1].points[0].y;
        const double a1 = std::atan2(d1y, d1x), a2 = std::atan2(d2y, d2x);
        double d = (a2 - a1) * 180.0 / M_PI;
        while (d < 0) d += 360.0; while (d >= 360.0) d -= 360.0;
        *outAngleDeg = d;
    }
    // C is line2.pt1
    return std::hypot(es[1].points[1].x - 10.0, es[1].points[1].y - 10.0);
}

int main() {
    installSolverFatalHandler();
    std::printf("deforming drag: holding far points keeps them put\n");
    double softAngle = 90.0;
    const double none = runDrag(0);
    const double soft = runDrag(1, &softAngle);
    const double hard = runDrag(2);
    std::printf("  inter-segment angle after soft-hold drag: %.3f deg (started 90)\n", softAngle);
    std::printf("  distant point C moved: none=%.3f  soft-hold=%.3f  hard-fix=%.3f\n",
                none, soft, hard);
    ck(none > 1.0, "without any hold, the distant point C moves (old behavior)");
    ck(soft < none, "soft-holding the far point keeps it closer + lets the line deform");
    ck(std::fabs(softAngle - 90.0) > 1.0,
       "the angles either side of the dragged segment stay FREE (angle changed, not held)");
    ck(hard < 0.01, "hard-fix pins the far point exactly (SHIPPED interim; soft-weights to follow)");
    if (fails == 0) std::printf("deforming_drag: ALL PASS\n");
    else            std::printf("deforming_drag: %d FAILURE(S)\n", fails);
    return fails ? 1 : 0;
}
