// =====================================================================
//  tests/solver/offset_associative.cpp — associative offset re-derivation
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  An offset copy is linked to its parent (offsetParentId/Distance/Side) and
//  re-derived by updateOffsetFromParent when the parent moves, the way a
//  slot follows its centerline. The copy must stay at the offset distance,
//  parallel for a line, and inward/outward as chosen for a circle.
#include <cmath>
#include <cstdio>

#include "hobbycad/sketch/operations.h"

using namespace hobbycad;
using namespace hobbycad::sketch;

static int fails = 0;
static void ck(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++fails;
}
static bool near(double a, double b, double eps = 1e-6) { return std::fabs(a - b) < eps; }
static double dist(const Point2D& a, const Point2D& b) { return std::hypot(a.x - b.x, a.y - b.y); }

// Perpendicular distance from point p to the infinite line through a,b.
static double perpDist(const Point2D& p, const Point2D& a, const Point2D& b) {
    const double dx = b.x - a.x, dy = b.y - a.y;
    const double len = std::hypot(dx, dy);
    return std::fabs((p.x - a.x) * dy - (p.y - a.y) * dx) / len;
}

int main() {
    // ---- Line offset follows a moved, rotated parent -------------------
    {
        Entity parent;
        parent.id = 1; parent.type = EntityType::Line;
        parent.points = { {0, 0}, {10, 0} };

        // Offset up by 2 (click above the line).
        const OffsetResult r = offsetEntity(parent, 2.0, Point2D(5, 5), 2);
        ck(r.success, "the line offset is created");
        Entity child = r.entity;
        child.offsetParentId = 1;
        child.offsetDistance = 2.0;
        child.offsetSide = r.side;
        ck(near(child.points[0].y, 2.0) && near(child.points[1].y, 2.0),
           "the initial offset is 2 above");

        // Now move + rotate the parent and re-derive.
        parent.points = { {1, 1}, {11, 11} };   // 45 degrees, shifted
        ck(updateOffsetFromParent(child, parent), "the offset re-derives");
        ck(near(perpDist(child.points[0], parent.points[0], parent.points[1]), 2.0, 1e-6),
           "the copy stays exactly 2 from the moved parent");
        ck(near(dist(child.points[0], child.points[1]),
                dist(parent.points[0], parent.points[1]), 1e-6),
           "and equal in length (parallel offset)");
    }

    // ---- Circle offset: outward and inward -----------------------------
    {
        Entity parent;
        parent.id = 3; parent.type = EntityType::Circle;
        parent.points = { {0, 0} }; parent.radius = 5.0;

        // Outward: click outside the circle.
        const OffsetResult out = offsetEntity(parent, 1.0, Point2D(20, 0), 4);
        Entity outer = out.entity;
        outer.offsetParentId = 3; outer.offsetDistance = 1.0; outer.offsetSide = out.side;
        ck(near(outer.radius, 6.0), "outward offset is radius + distance");

        // Inward: click inside the circle.
        const OffsetResult in = offsetEntity(parent, 1.0, Point2D(1, 0), 5);
        Entity inner = in.entity;
        inner.offsetParentId = 3; inner.offsetDistance = 1.0; inner.offsetSide = in.side;
        ck(near(inner.radius, 4.0), "inward offset is radius - distance");

        // Grow the parent and re-derive both.
        parent.radius = 10.0;
        ck(updateOffsetFromParent(outer, parent) && near(outer.radius, 11.0),
           "the outward copy follows the grown parent");
        ck(updateOffsetFromParent(inner, parent) && near(inner.radius, 9.0),
           "the inward copy stays inward after the parent grows");
    }

    std::printf("\n%s (%d failure(s))\n", fails ? "FAILURES" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
