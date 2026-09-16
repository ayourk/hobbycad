// tests/solver/arc_open.cpp
//   openFullArcByDrag: opening a full circle (one 360-degree arc) by dragging
//   one end. The arc must SHRINK from 360 for either drag direction, never
//   collapse to ~0 or exceed 360, keep its sign, cross 180 only when swung, and
//   swap the dragged endpoint at the inflection.
// SPDX-License-Identifier: GPL-3.0-only
#include <cmath>
#include <cstdio>
#include "hobbycad/sketch/handles.h"
using namespace hobbycad;
using namespace hobbycad::sketch;

static int fails = 0;
static void ck(bool ok, const char* w) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", w); if (!ok) ++fails;
}
static bool near(double a, double b, double eps = 1e-6) { return std::fabs(a - b) < eps; }

// Cursor on the radius-5 circle (center origin) at angle `deg`.
static Point2D at(double deg) {
    const double r = 5.0, a = deg * M_PI / 180.0;
    return { r * std::cos(a), r * std::sin(a) };
}

int main() {
    std::printf("openFullArcByDrag\n");
    const Point2D C{0, 0};
    const double R = 5.0;
    const double A = 0.0;          // fixed end sits at angle 0 (the cut point)

    // 1. First nudge CCW from the full circle -> ~359, shrinking (not ~0).
    {
        ArcOpenResult r = openFullArcByDrag(C, R, at(1.0), A, /*dragged=*/1, +360.0);
        ck(near(r.sweepAngle, 359.0, 1e-6), "CCW nudge -> sweep ~359 (shrinks from 360)");
        ck(r.sweepAngle > 0, "CCW nudge keeps + sign");
        ck(r.draggedIndex == 1, "CCW nudge drags the start (index 1)");
    }

    // 2. First nudge CW from the full circle -> also ~359 (not ~0), other branch.
    {
        ArcOpenResult r = openFullArcByDrag(C, R, at(-1.0), A, /*dragged=*/1, +360.0);
        ck(near(r.sweepAngle, 359.0, 1e-6), "CW nudge -> sweep ~359 (no collapse)");
        ck(r.draggedIndex == 2, "CW nudge swaps to drag the end (index 2)");
    }

    // 3. Progressive CCW opening shrinks monotonically and crosses 180.
    {
        double prev = 360.0; int idx = 1; double last = 360.0;
        bool monotonic = true, crossed180 = false;
        for (double d = 10.0; d <= 300.0; d += 10.0) {
            ArcOpenResult r = openFullArcByDrag(C, R, at(d), A, idx, prev);
            if (r.sweepAngle > last + 1e-9) monotonic = false;
            if (r.sweepAngle < 180.0) crossed180 = true;
            last = r.sweepAngle; prev = r.sweepAngle; idx = r.draggedIndex;
        }
        ck(monotonic, "CCW drag shrinks monotonically");
        ck(crossed180, "sweep crosses below 180 when swung past halfway");
        ck(last > 0.0 && last < 360.0, "sweep stays inside (0,360) throughout");
    }

    // 4. Sign is preserved for a CW (negative-sweep) arc.
    {
        ArcOpenResult r = openFullArcByDrag(C, R, at(20.0), A, /*dragged=*/1, -360.0);
        ck(r.sweepAngle < 0.0, "negative (CW) arc stays negative");
        ck(near(std::fabs(r.sweepAngle), 340.0, 1e-6), "CW arc opens to |sweep| ~340");
    }

    // 5. Re-closing toward full past 359 swaps the dragged endpoint (stays <360).
    {
        ArcOpenResult r = openFullArcByDrag(C, R, at(0.5), A, /*dragged=*/1, /*prev=*/358.0);
        ck(r.draggedIndex == 2, "sweep past 359 swaps the dragged endpoint");
        ck(std::fabs(r.sweepAngle) < 360.0, "swap keeps the arc under 360");
    }

    // 6. Radius is irrelevant to the angles (cursor angle is what matters).
    {
        ArcOpenResult r1 = openFullArcByDrag(C, R,     at(45.0), A, 1, 300.0);
        ArcOpenResult r2 = openFullArcByDrag(C, 999.0, at(45.0), A, 1, 300.0);
        ck(near(r1.sweepAngle, r2.sweepAngle), "sweep is independent of radius");
    }

    std::printf(fails ? "arc_open: %d FAILURE(S)\n" : "arc_open: ALL PASS\n", fails);
    return fails ? 1 : 0;
}
