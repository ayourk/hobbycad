// =====================================================================
//  tests/handleguard/minedge.cpp — a drag must not collapse an edge
//  Repository: ayourk/hobbycad
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//
//  An edge driven to zero is unrecoverable, not merely ugly. A zero-length
//  line has no direction, so Horizontal and Vertical are satisfied
//  trivially while the coincident constraints hold the corners together.
//  Nothing left in the system can restore the shape: dragging a corner of
//  a collapsed rectangle afterwards just translates the point it became,
//  and the solver reports success throughout.
//
//  Verified before the guard existed: after collapsing a 40x25 rectangle
//  and dragging one corner to (30,20), all four lines read
//  (30,20)-(30,20).
//
// =====================================================================

#include <hobbycad/sketch/handles.h>
#include <hobbycad/sketch/constraint.h>
#include <hobbycad/sketch/entity.h>
#include <hobbycad/sketch/queries.h>

#include <cmath>
#include <cstdio>
#include <vector>

using namespace hobbycad;
using namespace hobbycad::sketch;

static int failures = 0;
static void check(bool ok, const char* what)
{
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}
static double dist(const Point2D& a, const Point2D& b)
{
    const double dx = a.x - b.x, dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

int main()
{
    // --- the separation follows the zoom -------------------------------
    const double atOne  = minHandleSeparation(1.0);    // 1 px per unit
    const double atTen  = minHandleSeparation(10.0);   // zoomed in
    std::printf("separation: zoom 1 -> %g, zoom 10 -> %g\n", atOne, atTen);
    check(atOne > atTen, "zooming in permits a finer edge");
    check(atTen > 0.0, "and it stays positive");
    check(minHandleSeparation(0.0) >= kMinEdgeLength,
          "a zero or broken zoom still yields the absolute floor");
    check(minHandleSeparation(1e12) >= kMinEdgeLength,
          "and no zoom can drive the floor to zero");

    const double minSep = minHandleSeparation(4.0);

    // --- a corner dragged onto its neighbor is pushed clear ------------
    {
        const Point2D corner{0.0, 0.0};
        std::vector<Point2D> others{ corner, {40.0, 0.0}, {40.0, 25.0} };
        // Drag the fourth corner exactly onto the first.
        const Point2D got = keepHandleApart(corner, others, {0.0, 25.0}, minSep);
        std::printf("dragged onto a corner -> (%.6f, %.6f), %.6f away\n",
                    got.x, got.y, dist(got, corner));
        check(dist(got, corner) >= minSep * 0.999,
              "a handle dropped exactly on another corner is pushed off it");
    }

    // --- clear of every obstacle, not just the last one -----------------
    {
        std::vector<Point2D> others{ {0.0, 0.0}, {0.05, 0.0}, {0.0, 0.05} };
        const Point2D got = keepHandleApart({0.02, 0.02}, others, {5.0, 5.0}, minSep);
        bool clear = true;
        for (const Point2D& o : others) if (dist(got, o) < minSep * 0.999) clear = false;
        std::printf("crowded by three -> (%.6f, %.6f)\n", got.x, got.y);
        check(clear, "settles clear of every obstacle, not merely the last");
    }

    // --- a legitimate drag is left alone --------------------------------
    {
        std::vector<Point2D> others{ {0.0, 0.0}, {40.0, 0.0} };
        const Point2D want{40.0, 25.0};
        const Point2D got = keepHandleApart(want, others, {40.0, 20.0}, minSep);
        check(got.x == want.x && got.y == want.y,
              "a drag that collapses nothing is passed through untouched");
    }

    // --- the same invariant, defended at the model ---------------------
    // The drag guard closes one door. Dimension values are another: the
    // properties panel checked for NaN but not for zero, so typing 0 into
    // a Distance field destroyed the geometry just as surely.
    check(!isValidConstraintValue(ConstraintType::Distance, 0.0),
          "a zero Distance is refused");
    check(!isValidConstraintValue(ConstraintType::Radius, 0.0),
          "a zero Radius is refused");
    check(!isValidConstraintValue(ConstraintType::Diameter, -5.0),
          "a negative Diameter is refused");
    check(isValidConstraintValue(ConstraintType::Distance, 0.001),
          "a small but positive Distance is accepted");
    check(isValidConstraintValue(ConstraintType::Angle, 0.0),
          "a zero Angle is accepted: that is parallel, and recoverable");
    check(!isValidConstraintValue(ConstraintType::Distance,
                                  std::nan("")),
          "NaN is refused");
    check(isValidConstraintValue(ConstraintType::Horizontal, 0.0),
          "a geometric constraint carries no value and is accepted");

    // --- a collapsed primitive is an ERROR, not a warning ---------------
    // A primitive reduced to zero has become a point: an entity whose
    // stored type no longer describes what it is. That is worse than being
    // out of tolerance, and validateSketch() used to call it a warning and
    // report the sketch valid.
    {
        std::vector<Entity> es{ createLine(1, {5.0, 5.0}, {5.0, 5.0}),
                                createCircle(2, {0.0, 0.0}, 0.0) };
        std::vector<Constraint> cs;
        const ValidationResult v = validateSketch(es, cs);
        std::printf("degenerate sketch: valid=%s errors=%zu warnings=%zu\n",
                    v.valid ? "yes" : "no", v.errors.size(), v.warnings.size());
        for (const auto& e : v.errors) std::printf("    %s\n", e.c_str());
        check(!v.valid, "a sketch holding a collapsed primitive is not valid");
        check(v.errors.size() == 2, "both the zero line and the zero circle are errors");
    }

    // --- and a healthy sketch is still clean ----------------------------
    {
        std::vector<Entity> es{ createLine(1, {0.0, 0.0}, {40.0, 0.0}),
                                createCircle(2, {0.0, 0.0}, 3.0) };
        std::vector<Constraint> cs;
        const ValidationResult v = validateSketch(es, cs);
        check(v.valid && v.errors.empty(),
              "ordinary geometry raises nothing");
    }

    if (failures == 0) { std::printf("handleguard: ALL PASS\n"); return 0; }
    std::printf("handleguard: %d FAILURE(S)\n", failures);
    return 1;
}
