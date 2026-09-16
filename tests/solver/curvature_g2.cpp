// =====================================================================
//  tests/solver/curvature_g2.cpp  (G2 piece C)
//  A Curvature (G2) constraint between two Bezier splines. Without the fork's
//  SLVS_HAS_CURVATURE it is skipped with a diagnostic (the solve must still
//  succeed; the wiring is safe); with the 0010 cut installed it drives the
//  two junction segments to equal curvature.
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#include <hobbycad/sketch/entity.h>
#include <hobbycad/sketch/constraint.h>
#include <hobbycad/sketch/solver.h>
#if defined(__has_include)
#  if __has_include(<slvs.h>)
#    include <slvs.h>   // for the SLVS_HAS_CURVATURE feature macro
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
    std::printf("curvature (G2) constraint between two Bezier splines\n");
    // A test that does not run cannot fail, so it must not pass.
    if (!Solver::isAvailable()) { std::printf("  [FAIL] cannot run: solver unavailable\n"); return 1; }

    // A finishes at (3,0); B starts at (3,0): a shared junction.
    Entity a = createBezierSpline(1, {{0,0},{1,1},{2,1},{3,0}});
    Entity b = createBezierSpline(2, {{3,0},{4,-1},{5,-1},{6,0}});
    std::vector<Entity> ents = { a, b };

    std::vector<Constraint> pins;   // fix A's 4 control points -> A is rigid, nonzero curvature
    for (int i = 0; i < 4; ++i) {
        Constraint fx; fx.id = 10 + i; fx.type = ConstraintType::FixedPoint;
        fx.entityIds = {1}; fx.pointIndices = {i}; fx.enabled = true; fx.isDriving = true;
        pins.push_back(fx);
    }
    Constraint coin; coin.id = 1; coin.type = ConstraintType::Coincident;
    coin.entityIds = {1, 2}; coin.pointIndices = {3, 0};
    coin.enabled = true; coin.isDriving = true;
    Constraint g2; g2.id = 2; g2.type = ConstraintType::Curvature;
    g2.entityIds = {1, 2}; g2.pointIndices = {1, 0};   // A finish, B start
    g2.enabled = true; g2.isDriving = true;
    std::vector<Constraint> cons = pins; cons.push_back(coin); cons.push_back(g2);

    Solver s; SolveResult r = s.solve(ents, cons);
    ck(r.success, "solve succeeds with a Curvature constraint present (wiring safe)");
#if defined(SLVS_HAS_CURVATURE)
    // solve() mutates the entities in place; read back and compare junction curvature.
    // signed curvature k = (d1 x d2).z / |d1|^3 (Bezier factors cancel A-vs-B).
    auto kap=[](double d1x,double d1y,double d2x,double d2y){
        double cz=d1x*d2y-d1y*d2x, len=std::hypot(d1x,d1y); return cz/(len*len*len); };
    const auto& pa = ents[0].points; const auto& pb = ents[1].points;
    const std::size_t na = pa.size();
    // A finish forward: P'=(P[n-1]-P[n-2]); P''=(P[n-1]-2P[n-2]+P[n-3])
    double kA = kap(pa[na-1].x-pa[na-2].x, pa[na-1].y-pa[na-2].y,
                    pa[na-1].x-2*pa[na-2].x+pa[na-3].x, pa[na-1].y-2*pa[na-2].y+pa[na-3].y);
    // B start forward: P'=(P[1]-P[0]); P''=(P[0]-2P[1]+P[2])
    double kB = kap(pb[1].x-pb[0].x, pb[1].y-pb[0].y,
                    pb[0].x-2*pb[1].x+pb[2].x, pb[0].y-2*pb[1].y+pb[2].y);
    std::printf("  kA(finish)=%+.6f  kB(start)=%+.6f  |diff|=%.2e\n", kA, kB, std::fabs(kA-kB));
    ck(std::fabs(kA-kB) < 1e-4, "G2: junction curvatures equal after solve");
    ck(std::fabs(kA) > 0.1, "A's curvature is a real nonzero value (B adopted it, not flattened)");
#else
    std::printf("  [note] SLVS_HAS_CURVATURE absent: G2 skipped by design (needs the 0010 cut)\n");
#endif
    std::printf("%s\n", fails ? "FAILED" : "OK");
    return fails ? 1 : 0;
}
