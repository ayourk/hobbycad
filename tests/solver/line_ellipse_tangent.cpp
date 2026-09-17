// =====================================================================
//  tests/solver/line_ellipse_tangent.cpp — a line tangent to an ELLIPSE
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  With the fork's ellipse entity (SLVS_HAS_ELLIPSE, libslvs patch 0028)
//  this tangency is exact; with an older libslvs it is APPROXIMATE: at the
//  point of the ellipse nearest the line, the ellipse is stood in for by
//  its osculating circle, and the line is made tangent to that, with the
//  constants re-derived from the ellipse on every solve.
//
//  What both routes must deliver is pinned here, at the tolerance the
//  approximate one can meet: the line ends up genuinely touching the
//  ellipse (min distance ~0, curve all on one side), the constraint removes
//  exactly one degree of freedom like the circle case, and what comes out
//  is still an ellipse. The approximate route can only move the line; the
//  exact one moves whichever side is free, so nothing here assumes the
//  ellipse stays put. ellipse_native.cpp pins what only the exact route can
//  do (a fixed line, a point on the ellipse).
// =====================================================================
#include <hobbycad/sketch/entity.h>
#include <hobbycad/sketch/constraint.h>
#include <hobbycad/sketch/solver.h>
#include <hobbycad/sketch/queries.h>
#include <cstdio>
#include <cmath>
#include <vector>
using namespace hobbycad;
using namespace hobbycad::sketch;

static int failures = 0;
static void check(bool ok, const char* what)
{
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

/// Signed distance from `p` to the infinite line through a and b.
static double signedDist(const Point2D& p, const Point2D& a, const Point2D& b)
{
    const double dx = b.x - a.x, dy = b.y - a.y;
    const double len = std::sqrt(dx * dx + dy * dy);
    return (dy * (p.x - a.x) - dx * (p.y - a.y)) / len;
}

/// How the line sits against the ellipse: the smallest |distance| over a
/// dense sampling, and whether every sample is on one side (no crossing).
static void lineVsEllipse(const Entity& line, const Entity& ell,
                          double& minAbs, bool& oneSide)
{
    const std::vector<Point2D> pts = tessellate(ell, 720);
    minAbs = 1e300;
    double lo = 1e300, hi = -1e300;
    for (const Point2D& p : pts) {
        const double d = signedDist(p, line.points[0], line.points[1]);
        minAbs = std::min(minAbs, std::fabs(d));
        lo = std::min(lo, d);
        hi = std::max(hi, d);
    }
    // One side: the extremes do not straddle zero beyond tessellation slop.
    oneSide = (lo > -1e-3) || (hi < 1e-3);
}

static Constraint tangent(int id, int a, int b)
{
    Constraint c;
    c.id = id; c.type = ConstraintType::Tangent;
    c.entityIds = {a, b}; c.enabled = true;
    return c;
}

int main()
{
    installSolverFatalHandler();
    std::printf("line tangent to an ellipse (osculating-circle approximation)\n");

    // --- Exactly one degree of freedom, like the circle case ---------------
    {
        std::vector<Entity> es{ createLine(1, {0, 0}, {10, 3}),
                                createEllipse(2, {4, 9}, 5.0, 2.0, 20.0) };
        Solver s;
        std::vector<Constraint> none;
        const int before = s.degreesOfFreedom(es, none);
        std::vector<Constraint> one{ tangent(1, 1, 2) };
        const int after = s.degreesOfFreedom(es, one);
        std::printf("line+ellipse DOF: before=%d after=%d\n", before, after);
        check(before == 9, "line (4) + ellipse (5) starts at 9 DOF");
        check(after >= 0, "the solver survives a line-ellipse tangent");
        check(after == 8, "tangency removes exactly one DOF");
    }

    // --- And the line really ends up touching the ellipse -----------------
    // The line starts clear of the ellipse, below it, so a constraint that
    // merely registered would leave it there.
    {
        std::vector<Entity> es{ createLine(1, {-12, -8}, {12, -8}),
                                createEllipse(2, {0, 0}, 6.0, 3.0, 30.0) };
        double gap0; bool side0;
        lineVsEllipse(es[0], es[1], gap0, side0);
        std::vector<Constraint> cs{ tangent(1, 1, 2) };
        Solver s;
        SolveResult r = s.solve(es, cs);
        double gap; bool oneSide;
        lineVsEllipse(es[0], es[1], gap, oneSide);
        std::printf("start gap=%.4f  final min distance=%.6f  one side=%s  (%s)\n",
                    gap0, gap, oneSide ? "yes" : "no",
                    r.success ? "solved" : r.errorMessage.c_str());
        check(gap0 > 1.0, "the line really did start clear of the ellipse");
        check(r.success, "the sketch solves");
        check(gap < 2e-3, "the line touches the ellipse (min distance ~0)");
        check(oneSide, "and does not cut through it: tangent, not secant");
        // The approximate route cannot move the ellipse (its constants are
        // re-derived each solve); the exact route moves whichever side is
        // free, and Newton spreads the correction over both. What either
        // must leave behind is a real ellipse: perpendicular axes, radii.
        {
            const Entity& E = es[1];
            const double ax = E.points[1].x - E.points[0].x, ay = E.points[1].y - E.points[0].y;
            const double bx = E.points[2].x - E.points[0].x, by = E.points[2].y - E.points[0].y;
            const double cosang = (ax * bx + ay * by)
                / (std::sqrt(ax * ax + ay * ay) * std::sqrt(bx * bx + by * by));
            std::printf("ellipse after: a=%.4f b=%.4f rot=%.3f cos(axes)=%.1e\n",
                        E.majorRadius, E.minorRadius, E.ellipseRotation, cosang);
            check(E.majorRadius > 1.0 && E.minorRadius > 1.0 && std::fabs(cosang) < 1e-9,
                  "the ellipse is still an ellipse (perpendicular axes, positive radii)");
        }
    }

    // --- A circle-shaped ellipse agrees with the exact circle tangent -----
    // With a == b the osculating circle IS the ellipse, so even the
    // approximation is exact here. Both routes must leave the center at
    // exactly the ellipse's half-extent across the line's normal from the
    // line: for a round ellipse that is its radius, 3 if the ellipse did not
    // move. The exact route may move it, so the extent is read back from the
    // solved entity rather than assumed.
    {
        std::vector<Entity> es{ createLine(1, {-10, -7}, {10, -7}),
                                createEllipse(2, {0, 0}, 3.0, 3.0, 0.0) };
        std::vector<Constraint> cs{ tangent(1, 1, 2) };
        Solver s;
        SolveResult r = s.solve(es, cs);
        const Entity& E = es[1];
        const Point2D la = es[0].points[0], lb = es[0].points[1];
        const double d = std::fabs(signedDist(E.points[0], la, lb));
        const double dx = lb.x - la.x, dy = lb.y - la.y, len = std::sqrt(dx * dx + dy * dy);
        const double nx = -dy / len, ny = dx / len;
        const double th = E.ellipseRotation * M_PI / 180.0;
        const double Ax = E.majorRadius * std::cos(th), Ay = E.majorRadius * std::sin(th);
        const double Bx = -E.minorRadius * std::sin(th), By = E.minorRadius * std::cos(th);
        const double na = nx * Ax + ny * Ay, nb = nx * Bx + ny * By;
        const double want = std::sqrt(na * na + nb * nb);
        std::printf("round ellipse: solved=%s center-to-line=%.6f half-extent=%.6f "
                    "(3 if unmoved)\n",
                    r.success ? "yes" : "no", d, want);
        check(r.success, "a round ellipse solves");
        check(std::fabs(d - want) < 1e-6,
              "and matches the circle result: center-to-line == radius "
              "(the half-extent across the line)");
    }

    std::printf("\n%s (%d failure(s))\n", failures ? "FAILURES" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
