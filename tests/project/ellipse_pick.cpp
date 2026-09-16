// =====================================================================
//  tests/project/ellipse_pick.cpp — picking a ROTATED ellipse
//  containsPoint/closestPoint/distanceTo honor ellipseRotation, so a rotated
//  ellipse is hit where it actually is, not where an axis-aligned one would be.
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#include <hobbycad/sketch/entity.h>
#include <cstdio>
#include <cmath>
using namespace hobbycad;
using namespace hobbycad::sketch;

static int fails = 0;
static void ck(bool ok, const char* w) { std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", w); if (!ok) ++fails; }
static bool near(double a, double b, double e = 0.3) { return std::fabs(a - b) < e; }

int main() {
    std::printf("rotated-ellipse picking\n");

    // major=10 along +X, minor=2; rotate 90 deg -> major now vertical.
    Entity e = createEllipse(1, {0, 0}, 10.0, 2.0, 90.0);

    // The rotated major-axis endpoint is at (0,10): must be ON the outline.
    ck(e.containsPoint({0.0, 10.0}, 0.5),
       "a point on the rotated major-axis end (0,10) is hit");

    // Where the UNrotated major end would be, (10,0), the ellipse only reaches
    // minor=2, so it must NOT be hit.
    ck(!e.containsPoint({10.0, 0.0}, 0.5),
       "the unrotated major end (10,0) is NOT hit (rotation honored)");

    // The rotated minor end is at (2,0): on the outline.
    ck(e.containsPoint({2.0, 0.0}, 0.5),
       "the rotated minor-axis end (2,0) is hit");

    // closestPoint of a far point straight above lands near (0,10).
    const Point2D cp = e.closestPoint({0.0, 20.0});
    ck(near(cp.x, 0.0) && near(cp.y, 10.0),
       "closestPoint above the ellipse lands near the rotated top (0,10)");

    // Sanity: an axis-aligned ellipse (no rotation) still hits its major end.
    Entity a = createEllipse(2, {0, 0}, 10.0, 2.0, 0.0);
    ck(a.containsPoint({10.0, 0.0}, 0.5) && !a.containsPoint({0.0, 10.0}, 0.5),
       "axis-aligned ellipse unchanged (hits (10,0), not (0,10))");

    if (fails == 0) std::printf("ellipse_pick: ALL PASS\n");
    else            std::printf("ellipse_pick: %d FAILURE(S)\n", fails);
    return fails ? 1 : 0;
}
