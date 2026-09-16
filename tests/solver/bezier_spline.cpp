// =====================================================================
//  tests/solver/bezier_spline.cpp
//  A Bezier spline (piece B) registers each cubic segment as a solver
//  entity (SLVS_E_CUBIC) and solves. The cubic is a passive curve entity
//  built from the control points, so it adds no DOF and must not break the
//  solve, the foundation the G2 (curvature) constraint sits on.
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#include <hobbycad/sketch/entity.h>
#include <hobbycad/sketch/constraint.h>
#include <hobbycad/sketch/solver.h>
#include <hobbycad/sketch/operations.h>
#include <cmath>
#include <cstdio>
#include <vector>
#include <algorithm>
using namespace hobbycad;
using namespace hobbycad::sketch;

static int fails = 0;
static void ck(bool ok, const char* w) { std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", w); if (!ok) ++fails; }

int main() {
    installSolverFatalHandler();
    std::printf("bezier spline registers as a solver cubic\n");
    // A test that does not run cannot fail, so it must not pass.
    if (!Solver::isAvailable()) { std::printf("  [FAIL] cannot run: solver unavailable\n"); return 1; }

    const std::vector<Point2D> cp = {{0,0},{1,3},{4,3},{5,0}};   // 1 cubic segment
    {
        std::vector<Entity> ents = { createBezierSpline(1, cp) };
        std::vector<Constraint> cons;
        Solver s; SolveResult r = s.solve(ents, cons);
        ck(r.success, "bezier spline (1 seg) registers + solves");
        ck(r.dof == 8, "4 free control points -> dof 8 (cubic entity adds none)");
    }
    {   // parity: same points as Catmull-Rom -> same DOF, also solves
        std::vector<Entity> ents = { createSpline(1, cp) };
        std::vector<Constraint> cons;
        Solver s; SolveResult r = s.solve(ents, cons);
        ck(r.success && r.dof == 8, "catmull-rom parity (solves, dof 8)");
    }
    {   // 7 control points = 2 shared-endpoint cubic segments
        const std::vector<Point2D> cp2 = {{0,0},{1,2},{2,2},{3,0},{4,-2},{5,-2},{6,0}};
        std::vector<Entity> ents = { createBezierSpline(1, cp2) };
        std::vector<Constraint> cons;
        Solver s; SolveResult r = s.solve(ents, cons);
        ck(r.success, "bezier spline (2 segs) registers + solves");
        ck(r.dof == 14, "7 free control points -> dof 14");
    }
    {   // De Casteljau tessellation of a Bezier segment: (0,0)(0,1)(1,1)(1,0)
        std::vector<Point3> bp = {{0,0,0},{0,1,0},{1,1,0},{1,0,0}};
        std::vector<Point3> t = tessellateSpline(bp, 12, /*bezier=*/true);
        ck(!t.empty() && std::hypot(t.front().x-0, t.front().y-0) < 1e-9, "tess start endpoint exact");
        ck(!t.empty() && std::hypot(t.back().x-1, t.back().y-0) < 1e-9, "tess end endpoint exact");
        Point3 m = t[t.size()/2];
        ck(std::fabs(m.x-0.5) < 0.05 && std::fabs(m.y-0.75) < 0.05, "tess midpoint ~ (0.5,0.75)");
    }
    {   // Picking uses the Bezier control polygon, not Catmull-Rom.
        // Cubic (0,0)(0,1)(1,1)(1,0) peaks at (0.5,0.75); the same 4 points as
        // Catmull-Rom interpolate and bulge to ~(0.5,1.125) instead.
        const std::vector<Point2D> pk = {{0,0},{0,1},{1,1},{1,0}};
        Entity bez = createBezierSpline(1, pk);
        ck(bez.distanceTo({0.5, 0.75}) < 0.02, "bezier pick: point on the curve peak is ~0 away");
        Point2D cp = bez.closestPoint({0.5, 5.0});
        ck(std::fabs(cp.x-0.5) < 0.05 && std::fabs(cp.y-0.75) < 0.05, "bezier pick: closest point to (0.5,5) is the peak");
        Entity cr = createSpline(1, pk);
        ck(cr.distanceTo({0.5, 0.75}) > 0.1, "catmull-rom reads the same points differently (branch is real)");
    }
    {   // boundingBox samples the curve, covering Catmull-Rom overshoot.
        // (0,0)(1,0)(2,0)(3,-5): the P1->P2 span bulges to y~+0.37, above every
        // control point's y (max 0); an endpoint-only bound would clip it at 0.
        Entity cr = createSpline(1, {{0,0},{1,0},{2,0},{3,-5}});
        auto bb = cr.boundingBox();
        ck(bb.maxY > 0.3, "spline bbox covers Catmull-Rom overshoot above the control points");
    }
    {   // bezierControlPolygon assembles [P0,out0,in1,P1,...] from anchors.
        std::vector<BezierAnchor> a(2);
        a[0].pos={0,0}; a[0].hasOut=true; a[0].outHandle={1,0};
        a[1].pos={3,0}; a[1].hasIn=true;  a[1].inHandle={2,0};
        auto poly = bezierControlPolygon(a);
        ck(poly.size()==4, "bezier polygon: 2 anchors -> 4 control points (one segment)");
        ck(poly.size()==4 && poly[0].x==0 && poly[1].x==1 && poly[2].x==2 && poly[3].x==3,
           "bezier polygon: [P0,out0,in1,P1] ordering");
        std::vector<BezierAnchor> c(2);      // no handles -> corners
        c[0].pos={0,0}; c[1].pos={5,5};
        auto p2 = bezierControlPolygon(c);
        ck(p2.size()==4 && p2[1].x==0 && p2[1].y==0 && p2[2].x==5 && p2[2].y==5,
           "bezier polygon: missing handles collapse to their anchors");
        // three anchors -> 7 control points (two segments), feeds createBezierSpline
        std::vector<BezierAnchor> d(3);
        d[0].pos={0,0}; d[1].pos={1,1}; d[2].pos={2,0};
        ck(bezierControlPolygon(d).size()==7, "bezier polygon: 3 anchors -> 7 control points");
    }
    {   // bezierAnchorsFromControlPolygon inverts bezierControlPolygon.
        std::vector<BezierAnchor> a(3);
        a[0].pos={0,0}; a[0].hasOut=true; a[0].outHandle={1,0};
        a[1].pos={3,3}; a[1].hasIn=true; a[1].inHandle={2,3}; a[1].hasOut=true; a[1].outHandle={4,3};
        a[2].pos={6,0}; a[2].hasIn=true; a[2].inHandle={5,0};
        auto poly = bezierControlPolygon(a);
        auto back = bezierAnchorsFromControlPolygon(poly);
        ck(back.size()==3, "anchors round-trip: 3 anchors recovered");
        auto poly2 = bezierControlPolygon(back);
        bool same = poly.size()==poly2.size();
        for (size_t i=0; same && i<poly.size(); ++i)
            same = std::fabs(poly[i].x-poly2[i].x)<1e-12 && std::fabs(poly[i].y-poly2[i].y)<1e-12;
        ck(same, "anchors round-trip: polygon reconstructs identically");
        ck(back.size()==3 && back[1].hasIn && back[1].hasOut && std::fabs(back[1].outHandle.x-4)<1e-12,
           "interior anchor recovers both handles");
    }
    {   // suggestConstraints offers the spline pairings the GUI needs
        Entity s1 = createBezierSpline(1, {{0,0},{1,0},{2,0},{3,0}});
        Entity s2 = createBezierSpline(2, {{3,0},{4,0},{5,0},{6,0}});
        auto sug = suggestConstraints(s1, s2);
        ck(std::find(sug.begin(), sug.end(), ConstraintType::Curvature) != sug.end(),
           "two splines suggest Curvature (G2)");
        Entity pt; pt.id = 3; pt.type = EntityType::Point; pt.points = {{1,1}};
        auto sug2 = suggestConstraints(pt, s1);
        ck(std::find(sug2.begin(), sug2.end(), ConstraintType::PointOnSpline) != sug2.end(),
           "point + spline suggests PointOnSpline");
        Entity arc; arc.id = 4; arc.type = EntityType::Arc; arc.points = {{0,0},{2,0},{0,2}};
        auto sug3 = suggestConstraints(s1, arc);
        ck(std::find(sug3.begin(), sug3.end(), ConstraintType::Curvature) != sug3.end(),
           "spline + arc suggests Curvature (cubic<->arc G2)");
    }
    std::printf("%s\n", fails ? "FAILED" : "OK");
    return fails ? 1 : 0;
}
