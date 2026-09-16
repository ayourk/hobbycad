// A line tangent to a full CIRCLE.
//
// ARC_LINE_TANGENT is endpoint tangency: it dereferences the arc's
// point[1]/point[2]. A circle registers only a center, so feeding one to
// that constraint made libslvs abort with "Cannot find handle" and the
// whole sketch stopped solving, not just the tangent.
//
// The constraint is now built from the definition instead: a touch point on
// the circle, on the line, with the radius to it perpendicular to the line.
// This test asserts the GEOMETRY afterwards, not merely that nothing
// crashed: the distance from the center to the line must equal the radius.
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

/// Perpendicular distance from `c` to the infinite line through a and b.
static double distPointLine(const Point2D& c, const Point2D& a, const Point2D& b)
{
    const double dx = b.x - a.x, dy = b.y - a.y;
    const double len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-12) return std::sqrt((c.x - a.x) * (c.x - a.x)
                                    + (c.y - a.y) * (c.y - a.y));
    return std::fabs(dy * (c.x - a.x) - dx * (c.y - a.y)) / len;
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

    // --- The constraint must remove exactly one degree of freedom --------
    {
        std::vector<Entity> es{ createLine(1, {0, 0}, {10, 3}),
                                createCircle(2, {4, 9}, 4.0) };
        Solver s;
        std::vector<Constraint> none;
        const int before = s.degreesOfFreedom(es, none);
        std::vector<Constraint> one{ tangent(1, 1, 2) };
        const int after = s.degreesOfFreedom(es, one);
        std::printf("line+circle DOF: before=%d after=%d\n", before, after);
        check(before == 7, "line (4) + circle (3) starts at 7 DOF");
        check(after >= 0, "the solver survives a line-circle tangent");
        check(after == 6, "tangency removes exactly one DOF, as it does for an arc");
    }

    // --- And the geometry must actually be tangent ------------------------
    // The line starts well clear of the circle, so a constraint that merely
    // registered without doing anything would leave it there.
    {
        std::vector<Entity> es{ createLine(1, {-10, -6}, {10, -6}),
                                createCircle(2, {0, 0}, 3.0) };
        const double gapBefore = distPointLine(es[1].points[0],
                                               es[0].points[0], es[0].points[1]);
        std::vector<Constraint> cs{ tangent(1, 1, 2) };
        Solver s;
        SolveResult r = s.solve(es, cs);
        const double d = distPointLine(es[1].points[0],
                                       es[0].points[0], es[0].points[1]);
        std::printf("start gap=%.6f  radius=%.6f  final distance=%.6f  (%s)\n",
                    gapBefore, es[1].radius, d,
                    r.success ? "solved" : r.errorMessage.c_str());
        check(gapBefore > es[1].radius + 1.0, "the line really did start clear of the circle");
        check(r.success, "the sketch solves");
        check(std::fabs(d - es[1].radius) < 1e-6,
              "center-to-line distance equals the radius: the line touches");
        check(!offSegmentTangentPoint(es[0], es[1]).has_value(),
              "a tangency that touches ON the segment is not flagged");
    }

    // --- The line running THROUGH the center ------------------------------
    // No closest-approach direction exists, so the touch point has to be
    // seeded along the line's NORMAL. Seeded along the line instead, the
    // radius starts COLLINEAR with the line, a collinear radius can never be
    // made perpendicular to it, and the whole sketch fails to solve.
    {
        std::vector<Entity> es{ createLine(1, {-10, 0}, {10, 0}),
                                createCircle(2, {0, 0}, 3.0) };
        std::vector<Constraint> cs{ tangent(1, 1, 2) };
        Solver s;
        SolveResult r = s.solve(es, cs);
        const double d = distPointLine(es[1].points[0],
                                       es[0].points[0], es[0].points[1]);
        std::printf("center on the line: solved=%s radius=%.6f distance=%.6f\n",
                    r.success ? "yes" : "no", es[1].radius, d);
        check(r.success, "a line through the center still solves");
        check(es[1].radius > 1e-6, "the radius does not collapse to zero");
        check(std::fabs(d - es[1].radius) < 1e-6,
              "and the result is genuinely tangent");
    }

    // --- A redundant second tangency must be absorbed, not counted --------
    // Two lines held collinear cannot both add a tangency to one circle: the
    // second says nothing the first did not.
    {
        std::vector<Entity> es{ createLine(1, {-10, -6}, {-2, -6}),
                                createLine(2, {2, -6}, {10, -6}),
                                createCircle(3, {0, 0}, 3.0) };
        Constraint col;
        col.id = 1; col.type = ConstraintType::Collinear;
        col.entityIds = {1, 2}; col.enabled = true;
        Solver s;
        std::vector<Constraint> one{ col, tangent(2, 1, 3) };
        std::vector<Constraint> two{ col, tangent(2, 1, 3), tangent(3, 2, 3) };
        const int d1 = s.degreesOfFreedom(es, one);
        const int d2 = s.degreesOfFreedom(es, two);
        std::printf("collinear pair: one tangency=%d, both=%d\n", d1, d2);
        check(d1 == d2, "the redundant second tangency removes no further freedom");
    }

    // --- Tangent to the PERIMETER, of the INFINITE line -------------------
    // The circle side is exact: the touch point is on the circumference, so
    // the center-to-line distance equals the radius and the radius never
    // collapses. The line side is the infinite line, not the drawn segment,
    // so the touch point can land past an end; the segment itself then
    // never reaches the circle. That is standard CAD behavior and cannot be
    // constrained away in any case: "between the endpoints" is an
    // inequality, and the solver takes equations. Asserted here so nobody
    // later reads it as a bug and clamps it.
    {
        std::vector<Entity> es{ createLine(1, {-10, -6}, {-6, -6}),
                                createCircle(2, {14, 0}, 3.0) };
        std::vector<Constraint> cs{ tangent(1, 1, 2) };
        Solver s;
        SolveResult r = s.solve(es, cs);
        const Point2D a = es[0].points[0], b = es[0].points[1];
        const Point2D c = es[1].points[0];
        const double dx = b.x - a.x, dy = b.y - a.y;
        const double len2 = dx * dx + dy * dy;
        const double t = ((c.x - a.x) * dx + (c.y - a.y) * dy) / len2;
        const double d = distPointLine(c, a, b);
        std::printf("off-segment: t=%.3f radius=%.6f distance=%.6f\n",
                    t, es[1].radius, d);
        check(r.success, "a circle beyond the segment's end still solves");
        check(es[1].radius > 1e-6, "the radius stays positive");
        check(std::fabs(d - es[1].radius) < 1e-6,
              "tangent to the PERIMETER: distance equals the radius");
        check(t > 1.0, "and the touch point is past the segment's end, as expected");

        // The canvas marker: the query must report this one, and put the dot
        // on the perimeter where the tangency actually happens.
        auto marker = offSegmentTangentPoint(es[0], es[1]);
        check(marker.has_value(), "an off-segment tangency is reported for marking");
        if (marker) {
            const double mr = std::sqrt((marker->x - c.x) * (marker->x - c.x)
                                      + (marker->y - c.y) * (marker->y - c.y));
            std::printf("marker at (%.4f, %.4f), %.6f from the center\n",
                        marker->x, marker->y, mr);
            check(std::fabs(mr - es[1].radius) < 1e-6,
                  "the marker sits ON the perimeter, not inside the circle");
        }

        // A tangent line only ever meets the EDGE: because the perpendicular
        // distance equals the radius, no point of the infinite line is inside
        // the circle. Sampled well past both ends of the drawn segment.
        bool everInside = false;
        for (int i = -50; i <= 50; ++i) {
            const double s2 = i / 10.0;
            const double px = a.x + s2 * dx, py = a.y + s2 * dy;
            const double dd = std::sqrt((px - c.x) * (px - c.x)
                                      + (py - c.y) * (py - c.y));
            if (dd < es[1].radius - 1e-6) everInside = true;
        }
        check(!everInside, "the line never passes through the circle, only grazes it");
    }

    // --- Tangency to an ARC must keep working -----------------------------
    {
        std::vector<Entity> es{ createLine(1, {0, 0}, {10, 3}),
                                createArc(2, {4, 9}, 4.0, 0.0, 90.0) };
        Solver s;
        std::vector<Constraint> none;
        const int before = s.degreesOfFreedom(es, none);
        std::vector<Constraint> one{ tangent(1, 1, 2) };
        const int after = s.degreesOfFreedom(es, one);
        std::printf("line+arc DOF: before=%d after=%d\n", before, after);
        check(before == 9 && after == 8, "arc tangency still removes one DOF");
    }

    if (failures == 0) { std::printf("line_circle_tangent: ALL PASS\n"); return 0; }
    std::printf("line_circle_tangent: %d FAILURE(S)\n", failures);
    return 1;
}
