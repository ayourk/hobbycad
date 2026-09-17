// =====================================================================
//  tests/solver/ellipse_native.cpp — the ellipse as a NATIVE solver entity
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  libslvs patch 0028 (SLVS_HAS_ELLIPSE) gives the fork an ellipse entity
//  with an exact point-on constraint and an exact line tangency. This pins
//  what only that route can do, which line_ellipse_tangent.cpp (written for
//  the osculating-circle approximation too) cannot ask for:
//    - a point put ON an ellipse (Point On Circle with an ellipse operand);
//    - a line that is fully FIXED and tangent: the ELLIPSE must move;
//    - the tangency to solver precision, not to 2e-3.
//  Against a libslvs without the entity the whole file is skipped with a
//  note; the solve paths it needs do not exist there.
// =====================================================================
#include <hobbycad/sketch/entity.h>
#include <hobbycad/sketch/constraint.h>
#include <hobbycad/sketch/solver.h>
#include <hobbycad/sketch/queries.h>
#include <cstdio>
#include <cmath>
#include <vector>
// The feature macro lives in slvs.h, which the test build sees through the
// system include path (the tests link -lslvs); same idiom as tangent_angle.
#if defined(__has_include)
#  if __has_include(<slvs.h>)
#    include <slvs.h>
#  endif
#endif
using namespace hobbycad;
using namespace hobbycad::sketch;

static int failures = 0;
static void check(bool ok, const char* what)
{
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

#if defined(SLVS_HAS_ELLIPSE)
/// Signed distance from p to the infinite line through a and b.
static double signedDist(const Point2D& p, const Point2D& a, const Point2D& b)
{
    const double dx = b.x - a.x, dy = b.y - a.y;
    return (dy * (p.x - a.x) - dx * (p.y - a.y)) / std::sqrt(dx * dx + dy * dy);
}

/// (x/a)^2 + (y/b)^2 for p in the ellipse's own frame: 1 means on it.
static double implicitAt(const Entity& e, const Point2D& p)
{
    const double th = e.ellipseRotation * M_PI / 180.0;
    const double dx = p.x - e.points[0].x, dy = p.y - e.points[0].y;
    const double x =  dx * std::cos(th) + dy * std::sin(th);
    const double y = -dx * std::sin(th) + dy * std::cos(th);
    return (x * x) / (e.majorRadius * e.majorRadius) + (y * y) / (e.minorRadius * e.minorRadius);
}

/// The half-extent of the ellipse across a unit normal n: the exact
/// center-to-line distance of a tangent line with that normal.
static double halfExtent(const Entity& e, double nx, double ny)
{
    const double th = e.ellipseRotation * M_PI / 180.0;
    const double ax = e.majorRadius * std::cos(th), ay = e.majorRadius * std::sin(th);
    const double bx = -e.minorRadius * std::sin(th), by = e.minorRadius * std::cos(th);
    const double na = nx * ax + ny * ay, nb = nx * bx + ny * by;
    return std::sqrt(na * na + nb * nb);
}

static Constraint make(int id, ConstraintType t, std::vector<int> ents, std::vector<int> pts = {})
{
    Constraint c;
    c.id = id; c.type = t; c.entityIds = std::move(ents); c.pointIndices = std::move(pts);
    c.enabled = true;
    return c;
}
#endif

int main()
{
    installSolverFatalHandler();
    std::printf("ellipse as a native solver entity (libslvs 0028)\n");
#if !defined(SLVS_HAS_ELLIPSE)
    std::printf("    [note] SLVS_HAS_ELLIPSE absent: skipped by design (needs the 0028 cut)\n");
    // A skip is not a pass: say which it was. Exit 0 keeps a build against
    // an older libslvs green by design (the fallback paths are tested by
    // line_ellipse_tangent there).
    std::printf("\nSKIPPED (nothing checked)\n");
    return 0;
#else
    // --- a point put on the ellipse -------------------------------------
    {
        std::vector<Entity> es{ createEllipse(1, {0, 0}, 10.0, 4.0, 30.0),
                                createPoint(2, {14, 3}) };
        std::vector<Constraint> cs{ make(1, ConstraintType::PointOnCircle, {2, 1}, {0, 0}) };
        Solver s;
        SolveResult r = s.solve(es, cs);
        const double f = implicitAt(es[0], Point2D(es[1].points[0].x, es[1].points[0].y));
        std::printf("point on ellipse: %s  implicit=%.10f  dof=%d\n",
                    r.success ? "solved" : r.errorMessage.c_str(), f, r.dof);
        check(r.success, "Point On Circle accepts an ellipse and solves");
        check(std::fabs(f - 1.0) < 1e-8, "the point lands ON the ellipse ((x/a)^2 + (y/b)^2 == 1)");
        check(r.dof == 6, "ellipse 5 + point 2 - 1: dof 6");
    }

    // --- a FIXED line, tangent: the ellipse has to move -----------------
    // The approximate route could not do this at all (it only ever moved
    // the line). Here the line's ends are pinned to fixed points.
    {
        std::vector<Entity> es{ createEllipse(1, {0, 0}, 10.0, 4.0, 30.0),
                                createLine(2, {-15, -12}, {15, -12}),
                                createPoint(3, {-15, -12}), createPoint(4, {15, -12}) };
        std::vector<Constraint> cs{
            make(1, ConstraintType::FixedPoint, {3}),
            make(2, ConstraintType::FixedPoint, {4}),
            make(3, ConstraintType::Coincident, {2, 3}, {0, 0}),
            make(4, ConstraintType::Coincident, {2, 4}, {1, 0}),
            make(5, ConstraintType::Tangent, {1, 2}) };
        Solver s;
        SolveResult r = s.solve(es, cs);
        const Entity& E = es[0];
        const Point2D la(es[1].points[0].x, es[1].points[0].y),
                      lb(es[1].points[1].x, es[1].points[1].y);
        const double lineMoved = std::fabs(la.y + 12) + std::fabs(lb.y + 12);
        const double d = std::fabs(signedDist(Point2D(E.points[0].x, E.points[0].y), la, lb));
        const double want = halfExtent(E, 0.0, 1.0);
        // one-sidedness over the tessellated curve
        double lo = 1e300, hi = -1e300;
        for (const Point2D& p : tessellate(E, 720)) {
            const double sd = signedDist(p, la, lb);
            lo = std::min(lo, sd); hi = std::max(hi, sd);
        }
        std::printf("fixed line: %s  line moved=%.2e  center-to-line=%.9f want=%.9f  dof=%d\n",
                    r.success ? "solved" : r.errorMessage.c_str(), lineMoved, d, want, r.dof);
        check(r.success, "a fully fixed line + tangent solves (the ellipse gives)");
        check(lineMoved < 1e-9, "the line did not move");
        check(std::fabs(E.points[0].y) > 1e-3 || std::fabs(E.ellipseRotation - 30.0) > 1e-3
              || std::fabs(E.majorRadius - 10.0) > 1e-3,
              "the ELLIPSE moved to meet it");
        check(std::fabs(d - want) < 1e-6, "exact: center-to-line == half-extent across the normal");
        check((lo > -1e-3) || (hi < 1e-3), "tangent, not secant");
        check(r.dof == 4, "ellipse 5 - tangent 1: dof 4 (line fully pinned)");
    }

    // --- both free: exactly one DOF goes, and the touch is exact --------
    {
        std::vector<Entity> es{ createLine(1, {-12, -8}, {12, -8}),
                                createEllipse(2, {0, 0}, 6.0, 3.0, 30.0) };
        Solver s;
        std::vector<Constraint> none;
        const int before = s.degreesOfFreedom(es, none);
        std::vector<Constraint> cs{ make(1, ConstraintType::Tangent, {1, 2}) };
        const int after = s.degreesOfFreedom(es, cs);
        SolveResult r = s.solve(es, cs);
        const Entity& E = es[1];
        const Point2D la(es[0].points[0].x, es[0].points[0].y),
                      lb(es[0].points[1].x, es[0].points[1].y);
        const double dx = lb.x - la.x, dy = lb.y - la.y, len = std::sqrt(dx * dx + dy * dy);
        const double nx = -dy / len, ny = dx / len;
        const double d = std::fabs(signedDist(Point2D(E.points[0].x, E.points[0].y), la, lb));
        const double want = halfExtent(E, nx, ny);
        std::printf("both free: dof %d -> %d, %s, |d - want| = %.2e\n", before, after,
                    r.success ? "solved" : r.errorMessage.c_str(), std::fabs(d - want));
        check(before == 9 && after == 8, "tangency removes exactly one DOF");
        check(r.success && std::fabs(d - want) < 1e-6, "and the touch is exact to 1e-6");
    }

    std::printf("\n%s (%d failure(s))\n", failures ? "FAILURES" : "ALL PASS", failures);
    return failures ? 1 : 0;
#endif
}
