// =====================================================================
//  tests/project/ellipse_clicks.cpp — click order becomes stored layout
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  The ellipse tool takes three clicks and the two creation modes mean
//  different things by them. Getting that translation wrong is QUIET: the
//  ellipse still draws, and simply stores a different shape than the one
//  aimed at, which is invisible until the solver or a drag reads the
//  points. So the translation lives in the library and is pinned here.
//
//  The sharpest check is the last one: the two modes, given equivalent
//  clicks, must produce the SAME ellipse. If they diverge, one of the two
//  dropdown variants quietly draws something other than what was drawn.
// =====================================================================
#include <hobbycad/sketch/entity.h>
#include <cstdio>
#include <cmath>
#include <vector>
using namespace hobbycad;
using namespace hobbycad::sketch;

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what); if (!ok) ++failures;
}
static bool near(double a, double b, double e = 1e-9) { return std::fabs(a - b) < e; }
/// (x/a)^2 + (y/b)^2 for p in the ellipse's frame: 1 means p is ON the ellipse.
static double implicitAt(const Entity& e, const Point2D& p)
{
    const double th = e.ellipseRotation * M_PI / 180.0;
    const double dx = p.x - e.points[0].x, dy = p.y - e.points[0].y;
    const double x =  dx * std::cos(th) + dy * std::sin(th);
    const double y = -dx * std::sin(th) + dy * std::cos(th);
    return (x * x) / (e.majorRadius * e.majorRadius)
           + (y * y) / (e.minorRadius * e.minorRadius);
}

int main()
{
    std::printf("ellipse click order -> stored layout\n");

    // ---- Center + Axes: center, major end, then the minor point --------
    {
        Entity e;
        // Major axis along +X, length 10. Third click 4 above the center.
        const std::vector<Point2D> clicks = {{0, 0}, {10, 0}, {3, 4}};
        const bool placed = ellipseFromClicks(/*threePoint=*/false, clicks, e);
        check(placed, "Center+Axes placement is accepted");
        check(near(e.majorRadius, 10.0), "major radius is the center-to-click distance");
        // Fusion/Onshape rule: the third click is ON the ellipse. At x'=3 on a
        // major of 10, b = 4 / sqrt(1 - 0.09).
        check(near(e.minorRadius, 4.0 / std::sqrt(1.0 - 0.09)),
              "minor radius is solved so the ellipse passes THROUGH the third click");
        // implicitAt indexes the center, which a refused placement may lack.
        check(placed && !e.points.empty() && near(implicitAt(e, Point2D(3, 4)), 1.0),
              "the third click lies on the ellipse");
        check(near(e.ellipseRotation, 0.0), "rotation follows the major axis");
        check(e.points.size() == 3, "the canonical three points are written");
        if (e.points.size() == 3) {
            check(near(e.points[0].x, 0.0) && near(e.points[0].y, 0.0), "center is click one");
            check(near(e.points[1].x, 10.0) && near(e.points[1].y, 0.0), "+major end");
            check(near(e.points[2].x, 0.0) && near(e.points[2].y, e.minorRadius),
                  "+minor end on the minor axis");
        }
    }

    // ---- the curve follows the cursor: on the minor axis, off it, beyond --
    {
        Entity onAxis, offAxis, beyond;
        // Guarded: a refused placement must fail these checks, and implicitAt
        // indexes a center that a refused placement may not have written.
        const bool onPlaced = ellipseFromClicks(false, {{0, 0}, {10, 0}, {0, 4}}, onAxis);
        const bool offPlaced = ellipseFromClicks(false, {{0, 0}, {10, 0}, {9, 4}}, offAxis)
                               && !offAxis.points.empty();
        const bool beyondPlaced = ellipseFromClicks(false, {{0, 0}, {10, 0}, {12, 4}}, beyond);
        check(onPlaced && near(onAxis.minorRadius, 4.0),
              "a click on the minor axis IS the minor radius");
        check(offPlaced && near(implicitAt(offAxis, Point2D(9, 4)), 1.0),
              "sliding the click along the major axis keeps the curve under it "
              "(minor grows)");
        check(onPlaced && offPlaced && offAxis.minorRadius > onAxis.minorRadius,
              "so the minor radius is larger there, not equal");
        check(beyondPlaced && near(beyond.minorRadius, 4.0),
              "beyond the major extent no such ellipse exists: "
              "the perpendicular distance stands in");
    }

    // ---- a rotated placement -------------------------------------------
    {
        Entity e;
        // Major axis straight up: rotation 90, minor measured along -X/+X.
        const std::vector<Point2D> clicks = {{2, 2}, {2, 12}, {-1, 7}};
        const bool placed = ellipseFromClicks(false, clicks, e);
        check(placed, "rotated placement is accepted");
        check(near(e.majorRadius, 10.0), "rotated major radius");
        // click (-1,7) is x'=5 along the (upward) major, 3 across: b = 3/sqrt(1-0.25)
        check(near(e.minorRadius, 3.0 / std::sqrt(0.75)),
              "rotated minor radius solved through the click");
        check(placed && !e.points.empty() && near(implicitAt(e, Point2D(-1, 7)), 1.0),
              "the rotated ellipse passes through the click");
        check(near(e.ellipseRotation, 90.0), "rotation is the major axis angle, not zero");
    }

    // ---- 3-Point: two rim clicks, center is their midpoint -------------
    {
        Entity e;
        const std::vector<Point2D> clicks = {{-10, 0}, {10, 0}, {0, 4}};
        check(ellipseFromClicks(/*threePoint=*/true, clicks, e),
              "3-Point placement is accepted");
        check(near(e.points.empty() ? 1.0 : e.points[0].x, 0.0)
              && near(e.points.empty() ? 1.0 : e.points[0].y, 0.0),
              "center is the MIDPOINT of the two rim clicks");
        check(near(e.majorRadius, 10.0), "major radius is half the rim-to-rim span");
        check(near(e.minorRadius, 4.0),
              "minor radius: the click sits on the minor axis, so it is the radius");
    }

    // ---- an interrupted placement still commits ------------------------
    {
        Entity e;
        const std::vector<Point2D> clicks = {{0, 0}, {8, 0}};
        check(ellipseFromClicks(false, clicks, e), "two clicks are accepted");
        check(near(e.minorRadius, 4.0), "the minor axis falls back to half the major");
    }

    // ---- refusals -------------------------------------------------------
    {
        Entity e;
        const std::vector<Point2D> one = {{0, 0}};
        check(!ellipseFromClicks(false, one, e), "one click is refused");
        const std::vector<Point2D> degenerate = {{5, 5}, {5, 5}, {6, 6}};
        check(!ellipseFromClicks(false, degenerate, e),
              "a zero-length major axis is refused rather than stored");
    }

    // ---- the two modes agree on the same ellipse ------------------------
    {
        Entity byCenter, byRim;
        // Same ellipse described both ways: center (0,0), major 10 along +X.
        // Guarded: two refused placements would leave two equal defaults.
        const bool centerPlaced = ellipseFromClicks(false, {{0, 0}, {10, 0}, {0, 4}}, byCenter);
        const bool rimPlaced = ellipseFromClicks(true,  {{-10, 0}, {10, 0}, {0, 4}}, byRim);
        check(centerPlaced && rimPlaced
              && near(byCenter.majorRadius, byRim.majorRadius)
              && near(byCenter.minorRadius, byRim.minorRadius)
              && near(byCenter.ellipseRotation, byRim.ellipseRotation),
              "Center+Axes and 3-Point describe the SAME ellipse from "
              "equivalent clicks");
    }

    // ---- an ARC: two more clicks choose the range -----------------------
    {
        Entity e;
        // Ellipse: center (0,0), major 10 along +X, minor 4. Then start at
        // the +major end (parameter 0) and end at the +minor end (90).
        const std::vector<Point2D> clicks = {{0, 0}, {10, 0}, {0, 4}, {10, 0}, {0, 4}};
        check(ellipseFromClicks(false, clicks, e), "five clicks are accepted");
        check(near(e.ellipseStart, 0.0), "the start is the parameter of click four");
        check(near(e.ellipseSweep, 90.0), "the sweep runs counter-clockwise to click five");
        check(e.points.size() == 3, "the arc keeps the same three-point layout");
    }

    // ---- a click on the curve is a PARAMETER, not a polar angle ----------
    {
        Entity e;
        // 45 degrees polar on a 10 x 4 ellipse is NOT parameter 45: the point
        // (10cos45, 4sin45) is at parameter 45, but the point at polar 45,
        // (k, k), maps to atan2(k/4, k/10) = atan(2.5) = 68.2 degrees.
        const std::vector<Point2D> clicks = {{0, 0}, {10, 0}, {0, 4}, {5, 5}};
        check(ellipseFromClicks(false, clicks, e), "four clicks are accepted");
        check(near(e.ellipseStart, 68.19859051364818, 1e-6),
              "a click is read as the curve's parameter, not its polar angle");
        check(near(e.ellipseSweep, 360.0), "with no end yet the sweep is still a full turn");
    }

    // ---- a range that wraps through zero -------------------------------
    {
        Entity e;
        // Start at parameter 270 (the -minor end), end at 90 (+minor): the
        // counter-clockwise sweep passes through 0 and is 180, not -180.
        const std::vector<Point2D> clicks = {{0, 0}, {10, 0}, {0, 4}, {0, -4}, {0, 4}};
        check(ellipseFromClicks(false, clicks, e), "wrapping placement is accepted");
        check(near(e.ellipseStart, 270.0), "start at the -minor end is 270");
        check(near(e.ellipseSweep, 180.0), "a sweep through zero is positive, not negative");
    }

    // ---- the shorter way by default; longWay (Shift) for the long way ----
    {
        // Start at +major (parameter 0), end at -minor (270): counter-clockwise
        // that is 270, so the short way is the 90 clockwise, stored from the end.
        Entity shortArc, longArc;
        const bool shortPlaced = ellipseFromClicks(false,
                                                   {{0, 0}, {10, 0}, {0, 4}, {10, 0}, {0, -4}},
                                                   shortArc);
        check(shortPlaced && near(shortArc.ellipseStart, 270.0)
              && near(shortArc.ellipseSweep, 90.0),
              "a clockwise short arc is stored from the far end as a 90 sweep (not 270)");
        const bool longPlaced = ellipseFromClicks(false,
                                                  {{0, 0}, {10, 0}, {0, 4}, {10, 0}, {0, -4}},
                                                  longArc, /*longWay=*/true);
        check(longPlaced && near(longArc.ellipseStart, 0.0)
              && near(longArc.ellipseSweep, 270.0),
              "the long way keeps the start and sweeps 270");
        Entity ccwShort, ccwLong;
        const bool ccwShortPlaced = ellipseFromClicks(false,
                                                      {{0, 0}, {10, 0}, {0, 4}, {10, 0}, {0, 4}},
                                                      ccwShort);
        const bool ccwLongPlaced = ellipseFromClicks(false,
                                                     {{0, 0}, {10, 0}, {0, 4}, {10, 0}, {0, 4}},
                                                     ccwLong, /*longWay=*/true);
        check(ccwShortPlaced && near(ccwShort.ellipseStart, 0.0)
              && near(ccwShort.ellipseSweep, 90.0),
              "a counter-clockwise 90 stays as clicked");
        check(ccwLongPlaced && near(ccwLong.ellipseStart, 90.0)
              && near(ccwLong.ellipseSweep, 270.0),
              "and its long way is 270, stored from the end");
    }

    // ---- the same point twice is a full turn, not zero ------------------
    {
        Entity e;
        const std::vector<Point2D> clicks = {{0, 0}, {10, 0}, {0, 4}, {10, 0}, {10, 0}};
        // Guarded: a default entity is a whole ellipse too, so a refused
        // placement would otherwise pass.
        const bool placed = ellipseFromClicks(false, clicks, e);
        check(placed && near(e.ellipseSweep, 360.0),
              "start and end on the same point give a whole ellipse, not a zero arc");
    }

    // ---- the parameter helper on a rotated ellipse ----------------------
    {
        Entity e = createEllipse(9, Point2D(0, 0), 10.0, 4.0, 90.0);   // major up
        check(near(ellipseParamDeg(e, Point2D(0, 10)), 0.0), "the +major end is parameter 0");
        check(near(ellipseParamDeg(e, Point2D(-4, 0)), 90.0), "the +minor end is parameter 90");
        check(near(ellipseParamDeg(e, Point2D(0, -10)), 180.0), "the -major end is 180");
    }

    std::printf("\n%s (%d failure(s))\n", failures ? "FAILURES" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
