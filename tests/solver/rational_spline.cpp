// =====================================================================
//  tests/solver/rational_spline.cpp
//  A RATIONAL (weighted) Bezier spline registers each segment as
//  SLVS_E_RATIONAL_CUBIC (fork 0014), and a point constrained onto it
//  (PointOnSpline -> SLVS_C_PT_ON_RATIONAL_CUBIC) lands on the weighted
//  curve. Without SLVS_HAS_RATIONAL_CUBIC the wiring falls back and the
//  solve must still succeed.
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#include <hobbycad/sketch/entity.h>
#include <hobbycad/sketch/constraint.h>
#include <hobbycad/sketch/solver.h>
#if defined(__has_include)
#  if __has_include(<slvs.h>)
#    include <slvs.h>
#  endif
#endif
#include <cstdio>
#include <vector>
#include <cmath>
using namespace hobbycad;
using namespace hobbycad::sketch;
static int fails = 0;
static void ck(bool ok, const char* w){ std::printf("  [%s] %s\n", ok?"PASS":"FAIL", w); if(!ok)++fails; }

int main() {
    installSolverFatalHandler();
    std::printf("point on a rational (weighted) Bezier spline\n");
    // A test that does not run cannot fail, so it must not pass.
    if (!Solver::isAvailable()) { std::printf("  [FAIL] cannot run: solver unavailable\n"); return 1; }

    // rational bump: control (0,0)(0,2)(2,2)(2,0), weights 1,2,2,1 (genuinely rational)
    const std::vector<Point2D> cp = {{0,0},{0,2},{2,2},{2,0}};
    const std::vector<double>  w  = {1,2,2,1};
    Entity sp = createRationalBezierSpline(1, cp, w);
    Entity pt = createPoint(2, {1.0, 1.6});          // free point, near the curve
    std::vector<Entity> ents = { sp, pt };

    std::vector<Constraint> cons;
    for (int i = 0; i < 4; ++i) {                    // pin the spline's control points
        Constraint fx; fx.id = 10 + i; fx.type = ConstraintType::FixedPoint;
        fx.entityIds = {1}; fx.pointIndices = {i}; fx.enabled = true; fx.isDriving = true;
        cons.push_back(fx);
    }
    Constraint pos; pos.id = 1; pos.type = ConstraintType::PointOnSpline;
    pos.entityIds = {2, 1}; pos.enabled = true; pos.isDriving = true;   // point 2 on spline 1
    cons.push_back(pos);

    Solver s; SolveResult r = s.solve(ents, cons);
    ck(r.success, "solve succeeds with a point-on-rational-spline constraint");

#if defined(SLVS_HAS_RATIONAL_CUBIC)
    const Point2D P = ents[1].points[0];
    // nearest analytic rational curve point C(t) = sum w_i b_i P_i / sum w_i b_i
    double best = 1e30;
    for (int i = 0; i <= 200000; ++i) {
        double t = i / 200000.0, u = 1 - t;
        double b[4] = { u*u*u, 3*u*u*t, 3*u*t*t, t*t*t };
        double den = 0, nx = 0, ny = 0;
        for (int k = 0; k < 4; ++k) { double wb = w[k]*b[k]; den += wb; nx += wb*cp[k].x; ny += wb*cp[k].y; }
        best = std::min(best, std::hypot(P.x - nx/den, P.y - ny/den));
    }
    std::printf("    P=(%.5f,%.5f) dist-to-rational-curve=%.2e\n", P.x, P.y, best);
    ck(best < 1e-4, "the point lands on the RATIONAL curve (weighted, not the polynomial)");
#endif
    std::printf("%s\n", fails ? "FAILED" : "OK");
    return fails ? 1 : 0;
}
