// =====================================================================
//  tests/project/ellipse_arc_variants.cpp — Span + Rise, Corner, Endpoints
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  Three elliptical-arc placements that no other CAD in the cache offers as
//  a tool (Aaron, 2026-09-16). Each is pinned by what the clicks MEAN:
//  the span's ends are on the curve and the apex too (half ellipse on the
//  apex's side); the corner is the center and the legs' points are axis
//  ends (quarter between them); the endpoints are on the curve and the
//  center and axis direction fix the rest, with the infeasible cases
//  refused, not guessed.
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
/// (x/a)^2 + (y/b)^2 for p in the ellipse's frame: 1 means p is ON it.
/// Indexes the center, so callers guard it with the placement's result: a
/// refused placement may leave the entity with no points at all.
static double implicitAt(const Entity& e, const Point2D& p)
{
    const double th = e.ellipseRotation * M_PI / 180.0;
    const double dx = p.x - e.points[0].x, dy = p.y - e.points[0].y;
    const double x =  dx * std::cos(th) + dy * std::sin(th);
    const double y = -dx * std::sin(th) + dy * std::cos(th);
    return (x * x) / (e.majorRadius * e.majorRadius)
           + (y * y) / (e.minorRadius * e.minorRadius);
}
/// Whether parameter `deg` lies within the stored arc range.
static bool inRange(const Entity& e, double deg)
{
    double d = std::fmod(deg - e.ellipseStart, 360.0);
    if (d < 0) d += 360.0;
    return d <= e.ellipseSweep + 1e-9;
}

int main()
{
    std::printf("elliptical arc placements: Span + Rise, Corner, Endpoints\n");

    check(ellipsePlacementClicks(EllipsePlacement::SpanRise) == 3
          && ellipsePlacementClicks(EllipsePlacement::Corner) == 3
          && ellipsePlacementClicks(EllipsePlacement::Endpoints) == 4
          && ellipsePlacementClicks(EllipsePlacement::Arc) == 5,
          "click counts: 3, 3, 4 (and the plain arc's 5)");

    // ---- Span + Rise ----------------------------------------------------
    {
        Entity e;
        const bool placed = ellipseFromPlacement(EllipsePlacement::SpanRise,
                                                 {{-10, 0}, {10, 0}, {0, 4}}, false, e);
        check(placed, "span 20 with a rise of 4 is accepted");
        const bool centered = placed && !e.points.empty();
        check(centered && near(e.points[0].x, 0) && near(e.points[0].y, 0),
              "center is the span's midpoint");
        check(near(e.majorRadius, 10) && near(e.minorRadius, 4),
              "half span and rise are the radii");
        check(near(e.ellipseSweep, 180.0), "the arc is a half ellipse");
        check(inRange(e, ellipseParamDeg(e, Point2D(0, 4))),
              "the half is the one on the apex's side");
        check(!inRange(e, ellipseParamDeg(e, Point2D(0, -4))), "and not the other one");
        check(centered && near(implicitAt(e, Point2D(0, 4)), 1.0), "the apex is on the curve");

        Entity below;
        const bool belowPlaced = ellipseFromPlacement(EllipsePlacement::SpanRise,
                                                      {{-10, 0}, {10, 0}, {0, -4}}, false,
                                                      below);
        check(belowPlaced && inRange(below, ellipseParamDeg(below, Point2D(0, -4)))
              && near(below.ellipseSweep, 180.0),
              "an apex below the span gives the lower half");

        Entity offCenter;
        const bool offPlaced = ellipseFromPlacement(EllipsePlacement::SpanRise,
                                                    {{-10, 0}, {10, 0}, {6, 3}}, false,
                                                    offCenter)
                               && !offCenter.points.empty();
        check(offPlaced && near(implicitAt(offCenter, Point2D(6, 3)), 1.0),
              "an off-center apex still lies on the curve");

        Entity tall;
        const bool tallPlaced = ellipseFromPlacement(EllipsePlacement::SpanRise,
                                                     {{-4, 0}, {4, 0}, {0, 10}}, false, tall)
                                && !tall.points.empty();
        check(tallPlaced && near(tall.ellipseSweep, 180.0)
              && inRange(tall, ellipseParamDeg(tall, Point2D(0, 10)))
              && near(implicitAt(tall, Point2D(0, 10)), 1.0)
              && near(implicitAt(tall, Point2D(4, 0)), 1.0),
              "a rise taller than the half span (the chord is the minor axis) "
              "still gives the right half");

        Entity two;
        check(ellipseFromPlacement(EllipsePlacement::SpanRise, {{-10, 0}, {10, 0}}, false, two)
              && near(two.ellipseSweep, 180.0),
              "two clicks (apex still being placed) preview a half");
        Entity bad;
        check(!ellipseFromPlacement(EllipsePlacement::SpanRise,
                                    {{1, 1}, {1, 1}, {0, 4}}, false, bad),
              "a zero span is refused");
    }

    // ---- Corner ---------------------------------------------------------
    {
        Entity e;
        const bool placed = ellipseFromPlacement(EllipsePlacement::Corner,
                                                 {{0, 0}, {10, 0}, {0, 4}}, false, e);
        check(placed, "corner at the origin, legs of 10 and 4");
        check(near(e.majorRadius, 10) && near(e.minorRadius, 4) && near(e.ellipseRotation, 0),
              "axes as clicked");
        check(near(e.ellipseStart, 0.0) && near(e.ellipseSweep, 90.0),
              "the quarter from the first leg to the second");
        check(placed && !e.points.empty()
              && near(implicitAt(e, Point2D(10, 0)), 1.0)
              && near(implicitAt(e, Point2D(0, 4)), 1.0),
              "both leg points are on the curve");

        Entity down;
        const bool downPlaced = ellipseFromPlacement(EllipsePlacement::Corner,
                                                     {{0, 0}, {10, 0}, {0, -4}}, false, down);
        check(downPlaced && near(down.ellipseStart, 270.0) && near(down.ellipseSweep, 90.0),
              "a leg on the other side: the quarter from 270 to 360");

        Entity slanted;
        const bool slantedPlaced = ellipseFromPlacement(EllipsePlacement::Corner,
                                                        {{0, 0}, {10, 0}, {3, 4}}, false,
                                                        slanted)
                                   && !slanted.points.empty();
        check(slantedPlaced && near(implicitAt(slanted, Point2D(3, 4)), 1.0),
              "a leg click off the perpendicular: the curve passes THROUGH the click");
        check(slantedPlaced && near(slanted.ellipseSweep, 90.0)
              && near(slanted.ellipseStart, 0.0),
              "but the arc is still the full quarter, ending at the second axis end "
              "(pass-through != end)");
        check(slantedPlaced && inRange(slanted, ellipseParamDeg(slanted, Point2D(3, 4))),
              "and the click lies within that quarter");
        check(slantedPlaced && near(slanted.majorRadius, 10.0)
              && near(slanted.ellipseRotation, 0.0),
              "the first leg still fixes the axis direction and the first radius");

        Entity tall;
        const bool tallPlaced = ellipseFromPlacement(EllipsePlacement::Corner,
                                                     {{0, 0}, {4, 0}, {0, 10}}, false, tall);
        check(tallPlaced && near(tall.majorRadius, 10) && near(tall.minorRadius, 4)
              && near(tall.ellipseRotation, 90.0)
              && near(tall.ellipseSweep, 90.0)
              && inRange(tall, ellipseParamDeg(tall, Point2D(4, 0)))
              && inRange(tall, ellipseParamDeg(tall, Point2D(0, 10))),
              "a longer second leg becomes the major axis "
              "and the quarter still spans both legs");
    }

    // ---- Endpoints ------------------------------------------------------
    {
        Entity e;
        // The +major and +minor vertices of a 10 x 4 ellipse as the endpoints,
        // center at the origin, axis along +x: a and b come back exactly.
        const bool placed = ellipseFromPlacement(EllipsePlacement::Endpoints,
                                                 {{10, 0}, {0, 4}, {0, 0}, {5, 0}}, false, e);
        check(placed, "two perimeter points + center + axis direction is accepted");
        check(near(e.majorRadius, 10) && near(e.minorRadius, 4) && near(e.ellipseRotation, 0),
              "both radii solved from the two points");
        check(near(e.ellipseStart, 0.0) && near(e.ellipseSweep, 90.0),
              "the arc runs from the first point to the second, the short way");
        check(placed && !e.points.empty()
              && near(implicitAt(e, Point2D(10, 0)), 1.0)
              && near(implicitAt(e, Point2D(0, 4)), 1.0),
              "both endpoints lie on the curve");

        Entity longWay;
        const bool longPlaced = ellipseFromPlacement(EllipsePlacement::Endpoints,
                                                     {{10, 0}, {0, 4}, {0, 0}, {5, 0}}, true,
                                                     longWay);
        check(longPlaced && near(longWay.ellipseStart, 90.0)
              && near(longWay.ellipseSweep, 270.0),
              "longWay: 270 the other way, stored from the end");

        Entity general;
        // Two general points on the 10 x 4 ellipse at parameters 30 and 120.
        const Point2D q1(10 * std::cos(M_PI / 6), 4 * std::sin(M_PI / 6));
        const Point2D q2(10 * std::cos(2 * M_PI / 3), 4 * std::sin(2 * M_PI / 3));
        check(ellipseFromPlacement(EllipsePlacement::Endpoints,
                                   {q1, q2, {0, 0}, {1, 0}}, false, general)
              && near(general.majorRadius, 10) && near(general.minorRadius, 4),
              "two general points recover the ellipse they came from");

        Entity rotated;
        // The same, on the ellipse rotated 30 degrees about (5, 5).
        const double c30 = std::cos(M_PI / 6), s30 = std::sin(M_PI / 6);
        auto rot = [&](double x, double y) {
            return Point2D(5 + x * c30 - y * s30, 5 + x * s30 + y * c30);
        };
        check(ellipseFromPlacement(EllipsePlacement::Endpoints,
                                   {rot(q1.x, q1.y), rot(q2.x, q2.y), {5, 5}, rot(3, 0)},
                                   false, rotated)
              && near(rotated.majorRadius, 10) && near(rotated.minorRadius, 4)
              && near(rotated.ellipseRotation, 30.0),
              "and so does a rotated one, with the rotation read from the axis click");

        Entity threeOnly;
        check(ellipseFromPlacement(EllipsePlacement::Endpoints,
                                   {{10, 0}, {0, 4}, {0, 0}}, false, threeOnly)
              && near(threeOnly.majorRadius, 10) && near(threeOnly.minorRadius, 4),
              "three clicks (axis still being chosen) preview with the axis along +x");

        Entity mirror, impossible, twoOnly;
        check(!ellipseFromPlacement(EllipsePlacement::Endpoints,
                                    {{-10, 0}, {10, 0}, {0, 0}, {1, 0}}, false, mirror),
              "two points mirror-symmetric about the axis leave a radius free: refused");
        check(!ellipseFromPlacement(EllipsePlacement::Endpoints,
                                    {{10, 0}, {12, 1}, {0, 0}, {1, 0}}, false, impossible),
              "no ellipse with that center and axis passes through both points: refused");
        check(!ellipseFromPlacement(EllipsePlacement::Endpoints,
                                    {{10, 0}, {0, 4}}, false, twoOnly),
              "two clicks alone describe nothing yet");
    }

    std::printf("\n%s (%d failure(s))\n", failures ? "FAILURES" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
