// =====================================================================
//  tests/solver/tangent_angle.cpp
//  A TangentAngle dimension on a Bezier spline anchor. Without the fork's
//  SLVS_HAS_TANGENT_ANGLE it is skipped with a diagnostic (the solve must still
//  succeed); with the 0015 cut it drives the anchor's tangent to the directed
//  target angle, including across the 0/360 seam.
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
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
using namespace hobbycad;
using namespace hobbycad::sketch;
static int fails = 0;
static void ck(bool ok, const char* w){ std::printf("  [%s] %s\n", ok?"PASS":"FAIL", w); if(!ok)++fails; }

static void one(double target){
    // P0 anchor at origin; P1 the free out-handle SEEDED near the target (so the
    // directed +/-180 side is fixed); P2,P3 fixed to shape the rest.
    const double rad = target * M_PI / 180.0;
    Entity e = createBezierSpline(1, {{0,0},{0.4*std::cos(rad),0.4*std::sin(rad)},{2,1},{3,0}});
    std::vector<Entity> ents = { e };
    std::vector<Constraint> cons;
    for (int i : {0,2,3}) {   // fix everything except P1
        Constraint fx; fx.id=10+i; fx.type=ConstraintType::FixedPoint;
        fx.entityIds={1}; fx.pointIndices={i}; fx.enabled=true; fx.isDriving=true; cons.push_back(fx);
    }
    Constraint ta; ta.id=1; ta.type=ConstraintType::TangentAngle;
    ta.entityIds={1}; ta.pointIndices={0}; ta.value=target; ta.enabled=true; ta.isDriving=true;
    cons.push_back(ta);
    Solver s; SolveResult r = s.solve(ents, cons);
    char msg[96]; std::snprintf(msg,sizeof msg,"solve succeeds with TangentAngle=%.0f present",target);
    ck(r.success, msg);
#if defined(SLVS_HAS_TANGENT_ANGLE)
    const auto& p = ents[0].points;
    double got = std::atan2(p[1].y-p[0].y, p[1].x-p[0].x) * 180.0/M_PI; if(got<0) got+=360;
    double diff = std::fabs(got-target); if(diff>180) diff=360-diff;
    std::printf("    target=%.1f  solved tangent=%.2f  diff=%.3f\n", target, got, diff);
    std::snprintf(msg,sizeof msg,"tangent solved to %.0f deg (directed)",target);
    ck(diff < 1e-2, msg);
#else
    std::printf("    [note] SLVS_HAS_TANGENT_ANGLE absent: skipped by design (needs the 0015 cut)\n");
#endif
}

int main(){
    installSolverFatalHandler();
    std::printf("TangentAngle dimension on a Bezier spline anchor\n");
    // A test that does not run cannot fail, so it must not pass.
    if (!Solver::isAvailable()) { std::printf("  [FAIL] cannot run: solver unavailable\n"); return 1; }
    one(60.0);    // mid range
    one(200.0);   // other half (directed)
    one(358.0);   // near the 0/360 seam

    // Handle-length via a Distance dimension on a control-polygon leg (anchor->
    // handle). Proves leg distance-dimensioning solves through getPointHandle.
    {
        Entity e = createBezierSpline(2, {{0,0},{0.5,0},{2,1},{3,0}});
        std::vector<Entity> ents = { e };
        std::vector<Constraint> cons;
        Constraint fx; fx.id=20; fx.type=ConstraintType::FixedPoint;
        fx.entityIds={2}; fx.pointIndices={0}; fx.enabled=true; fx.isDriving=true; cons.push_back(fx);
        Constraint d; d.id=21; d.type=ConstraintType::Distance;
        d.entityIds={2,2}; d.pointIndices={0,1}; d.value=1.5; d.enabled=true; d.isDriving=true; cons.push_back(d);
        Solver s2; SolveResult r2 = s2.solve(ents, cons);
        ck(r2.success, "leg (anchor->handle) Distance dimension solves");
        double len = std::hypot(ents[0].points[1].x-ents[0].points[0].x,
                                ents[0].points[1].y-ents[0].points[0].y);
        std::printf("    handle leg length = %.4f (target 1.5)\n", len);
        ck(std::fabs(len-1.5) < 1e-4, "handle leg dimensioned to 1.5");
    }
    std::printf("%s\n", fails ? "FAILED" : "OK");
    return fails ? 1 : 0;
}
