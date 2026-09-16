// =====================================================================
//  tests/solver/inference.cpp — drawing-time alignment inference
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#include <cmath>
#include <cstdio>

#include "hobbycad/sketch/inference.h"

using namespace hobbycad;
using namespace hobbycad::sketch;

static int fails = 0;
static void ck(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++fails;
}
static bool near(double a, double b, double eps = 1e-6) {
    return std::fabs(a - b) < eps;
}

static Entity lineAt(int id, Point2D a, Point2D b) {
    Entity e; e.id = id; e.type = EntityType::Line;
    e.points = {a, b};
    return e;
}

int main() {
    const double tol = 3.0;

    // ---- Near-horizontal snaps to horizontal --------------------------
    {
        auto r = inferSegment({}, {0, 0}, {100, 2}, tol, -1);  // ~1.15 deg
        ck(r.inferences.size() == 1, "a near-horizontal segment infers one thing");
        if (!r.empty()) {
            ck(r.inferences[0].kind == InferenceKind::Horizontal, "and it is horizontal");
            ck(near(r.adjusted.y, 0.0), "the end drops onto y = start y");
            ck(near(r.adjusted.x, 100.0), "keeping the extent drawn along x");
            ck(r.inferences[0].refEntityId == -1, "an axis inference has no reference entity");
        }
    }

    // ---- Outside the window: nothing inferred -------------------------
    {
        auto r = inferSegment({}, {0, 0}, {100, 20}, tol, -1);  // ~11 deg
        ck(r.empty(), "a clearly sloped segment infers nothing");
        ck(near(r.adjusted.x, 100.0) && near(r.adjusted.y, 20.0),
           "and the point is returned unchanged");
    }

    // ---- Near-vertical snaps to vertical ------------------------------
    {
        auto r = inferSegment({}, {5, 5}, {6, 105}, tol, -1);  // ~0.57 deg off vertical
        ck(!r.empty() && r.inferences[0].kind == InferenceKind::Vertical,
           "a near-vertical segment infers vertical");
        if (!r.empty()) ck(near(r.adjusted.x, 5.0), "the end drops onto x = start x");
    }

    // ---- Parallel to an existing line ---------------------------------
    {
        // Reference line at 30 degrees.
        Entity ref = lineAt(7, {0, 0}, {std::cos(30*M_PI/180)*100, std::sin(30*M_PI/180)*100});
        // A new segment near 31 degrees, well away from any axis.
        double a = 31 * M_PI / 180;
        auto r = inferSegment({ref}, {10, 10}, {10 + std::cos(a)*50, 10 + std::sin(a)*50}, tol, -1);
        ck(!r.empty(), "a segment near another line's angle infers something");
        if (!r.empty()) {
            ck(r.inferences[0].kind == InferenceKind::Parallel, "and it is parallel");
            ck(r.inferences[0].refEntityId == 7, "naming the reference line");
            // The adjusted direction must match the reference's 30 degrees.
            Point2D d = r.adjusted - Point2D{10, 10};
            double deg = std::atan2(d.y, d.x) * 180 / M_PI;
            ck(near(deg, 30.0, 1e-4), "the end lies exactly along the reference angle");
        }
    }

    // ---- Perpendicular to an existing line ----------------------------
    {
        Entity ref = lineAt(3, {0, 0}, {100, 0});  // horizontal reference
        // A new segment near 88 degrees -> perpendicular to horizontal.
        double a = 88 * M_PI / 180;
        auto r = inferSegment({ref}, {50, 0}, {50 + std::cos(a)*40, std::sin(a)*40}, tol, -1);
        ck(!r.empty(), "a near-perpendicular segment infers something");
        if (!r.empty()) {
            // Vertical (axis) and Perpendicular both apply here; the axis wins.
            ck(r.inferences[0].kind == InferenceKind::Vertical,
               "the axis beats the perpendicular when both apply");
        }
    }

    // ---- Perpendicular with no competing axis -------------------------
    {
        Entity ref = lineAt(4, {0, 0}, {std::cos(30*M_PI/180)*100, std::sin(30*M_PI/180)*100});
        double a = 121 * M_PI / 180;  // near 30+90 = 120, far from any axis
        auto r = inferSegment({ref}, {0, 0}, {std::cos(a)*50, std::sin(a)*50}, tol, -1);
        ck(!r.empty() && r.inferences[0].kind == InferenceKind::Perpendicular,
           "a segment near a line's normal infers perpendicular");
        if (!r.empty()) ck(r.inferences[0].refEntityId == 4, "naming the reference line");
    }

    // ---- Axis preference over parallel on a tie -----------------------
    {
        Entity ref = lineAt(9, {0, 0}, {100, 1});  // ~0.57 deg, nearly horizontal
        auto r = inferSegment({ref}, {0, 0}, {100, 0.5}, tol, -1);  // ~0.28 deg
        ck(!r.empty() && r.inferences[0].kind == InferenceKind::Horizontal,
           "horizontal is preferred over parallel-to-a-near-horizontal-line");
    }

    // ---- excludeId omits the segment's own line -----------------------
    {
        Entity self = lineAt(5, {0, 0}, {std::cos(30*M_PI/180)*100, std::sin(30*M_PI/180)*100});
        double a = 31 * M_PI / 180;
        auto r = inferSegment({self}, {0, 0}, {std::cos(a)*50, std::sin(a)*50}, tol, 5);
        ck(r.empty(), "a segment is never inferred parallel to itself");
    }

    std::printf("\n%s (%d failure(s))\n", fails ? "FAILURES" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
