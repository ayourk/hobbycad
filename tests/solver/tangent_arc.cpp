// =====================================================================
//  tests/solver/tangent_arc.cpp — arcTangentToLine geometry
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  Locks the primitive a future "tangent arc from a line endpoint" gesture
//  (Fusion F-3) will build on: an arc tangent to a line at a given point and
//  passing through a given end point. The invariants that make it a tangent
//  arc (equal radii to both points, and the radius at the tangent point
//  perpendicular to the line) are what the interactive tool must preserve.
#include <cmath>
#include <cstdio>

#include "hobbycad/geometry/utils.h"

using namespace hobbycad;
using namespace hobbycad::geometry;

static int fails = 0;
static void ck(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++fails;
}
static bool near(double a, double b, double eps = 1e-6) { return std::fabs(a - b) < eps; }

int main() {
    // A horizontal line along X; tangent at its right end (10,0); the arc must
    // pass through (20,10), a quarter turn up around the center (10,10).
    const Point2D lineStart{0, 0}, lineEnd{10, 0};
    const Point2D tangentPt{10, 0}, endPt{20, 10};

    const TangentArcResult r = arcTangentToLine(lineStart, lineEnd, tangentPt, endPt);
    ck(r.valid, "a tangent arc is found");
    if (r.valid) {
        // Equal radii to both the tangent point and the end point.
        const double rt = std::hypot(tangentPt.x - r.center.x, tangentPt.y - r.center.y);
        const double re = std::hypot(endPt.x - r.center.x, endPt.y - r.center.y);
        ck(near(rt, r.radius, 1e-6), "the tangent point is at radius");
        ck(near(re, r.radius, 1e-6), "the end point is at radius too");

        // Tangency: the center-to-tangent-point vector is perpendicular to the
        // line direction, so their dot product is zero.
        const Point2D lineDir{lineEnd.x - lineStart.x, lineEnd.y - lineStart.y};
        const Point2D radial{tangentPt.x - r.center.x, tangentPt.y - r.center.y};
        const double dot = lineDir.x * radial.x + lineDir.y * radial.y;
        ck(near(dot, 0.0, 1e-6), "the radius at the tangent point is perpendicular to the line");

        // For this configuration the center sits directly above the tangent
        // point (the only way a radius there can be vertical).
        ck(near(r.center.x, 10.0, 1e-6), "the center is above the tangent point");
        ck(r.radius > 0.0, "the radius is positive");
    }

    std::printf("\n%s (%d failure(s))\n", fails ? "FAILURES" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
