// =====================================================================
//  tests/solver/trim_extend.cpp — trim and extend geometry (C9)
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  The review flagged trim/extend as unverified. These lock the library
//  contract the GUI relies on: trim removes the clicked segment between
//  intersections; extend reaches the next boundary, and reports failure (the
//  fail-safe) when there is none.
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

static Entity line(int id, Point2D a, Point2D b) {
    Entity e; e.id = id; e.type = EntityType::Line; e.points = {a, b}; return e;
}

int main() {
    int nextId = 100;
    auto gen = [&nextId]() { return nextId++; };

    // ---- Trim the middle segment out of a line (two intersections) -----
    {
        Entity l = line(1, {0, 0}, {10, 0});
        std::vector<Point2D> inters = { {3, 0}, {7, 0} };
        const TrimResult r = trimEntity(l, inters, Point2D(5, 0), gen);  // click middle
        ck(r.success, "trim with two intersections succeeds");
        ck(r.newEntities.size() == 2, "the middle goes, two end pieces remain");
        if (r.newEntities.size() == 2) {
            // Pieces are (0,0)-(3,0) and (7,0)-(10,0), in order.
            ck(near(r.newEntities[0].points[1].x, 3.0), "first piece ends at the first cut");
            ck(near(r.newEntities[1].points[0].x, 7.0), "second piece starts at the second cut");
        }
    }

    // ---- Trim an end segment (one intersection) ------------------------
    {
        Entity l = line(2, {0, 0}, {10, 0});
        std::vector<Point2D> inters = { {5, 0} };
        const TrimResult r = trimEntity(l, inters, Point2D(8, 0), gen);  // click right of cut
        ck(r.success && r.newEntities.size() == 1, "trimming an end leaves one piece");
        if (r.newEntities.size() == 1)
            ck(near(r.newEntities[0].points[0].x, 0.0) && near(r.newEntities[0].points[1].x, 5.0),
               "the kept piece is the half the click was NOT on");
    }

    // ---- Trim with no intersection: the library reports no-op ----------
    // (The GUI turns this into a delete; the library itself just declines.)
    {
        Entity l = line(3, {0, 0}, {10, 0});
        const TrimResult r = trimEntity(l, {}, Point2D(5, 0), gen);
        ck(!r.success, "trim with no intersections does not fabricate pieces");
    }

    // ---- Extend a line to a boundary -----------------------------------
    {
        Entity l = line(4, {0, 0}, {5, 0});                 // short line
        std::vector<Entity> bounds = { line(5, {10, -5}, {10, 5}) };  // wall at x=10
        const ExtendResult r = extendEntity(l, bounds, /*extendEnd=*/-1, Point2D(5, 0));
        ck(r.success, "extend reaches the boundary");
        if (r.success)
            ck(near(r.entity.points[1].x, 10.0) && near(r.entity.points[1].y, 0.0),
               "the near end lands on the wall at (10,0)");
    }

    // ---- Extend fail-safe: no boundary -> no change --------------------
    {
        Entity l = line(6, {0, 0}, {5, 0});
        const ExtendResult r = extendEntity(l, {}, /*extendEnd=*/-1, Point2D(5, 0));
        ck(!r.success, "extend with no boundary is a reported no-op (F-64 fail-safe)");
    }

    // ---- Split: circle -> arcs summing to 360, not forced to equal halves ----
    {
        // A circle centered at origin, radius 5, cut at 0deg and 90deg.
        Entity circle = createCircle(200, Point2D(0, 0), 5.0);
        std::vector<Point2D> cuts = { Point2D(5, 0), Point2D(0, 5) };   // 0deg, 90deg
        const SplitResult r = splitEntityAtIntersections(circle, cuts, gen);
        ck(r.success && r.newEntities.size() == 2, "circle split at 2 points -> 2 arcs");
        double total = 0.0;
        bool allArcs = true;
        for (const Entity& e : r.newEntities) {
            if (e.type != EntityType::Arc) allArcs = false;
            total += std::fabs(e.sweepAngle);
        }
        ck(allArcs, "circle split pieces are arcs");
        ck(near(total, 360.0, 1e-6), "arc sweeps sum to 360");
        // The 0->90 gap is 90deg; the complement is 270deg, NOT two 180s.
        bool has90 = false, has270 = false;
        for (const Entity& e : r.newEntities) {
            if (near(std::fabs(e.sweepAngle), 90.0)) has90 = true;
            if (near(std::fabs(e.sweepAngle), 270.0)) has270 = true;
        }
        ck(has90 && has270, "sweeps follow the cut points (90 and 270), not forced 180/180");
    }

    // A single intersection point cannot cut a closed circle into two arcs.
    {
        Entity circle = createCircle(210, Point2D(0, 0), 5.0);
        const SplitResult r = splitEntityAtIntersections(circle, { Point2D(5, 0) }, gen);
        ck(!r.success, "circle split-at-intersections with one point is refused (needs two)");
    }

    // A single explicit point opens the circle into ONE 360-degree arc (not two
    // forced halves); its endpoints are welded by nobody (see cut_constraints).
    {
        Entity circle = createCircle(215, Point2D(0, 0), 5.0);
        const SplitResult r = splitEntityAt(circle, Point2D(5, 0), gen);
        ck(r.success && r.newEntities.size() == 1
           && r.newEntities[0].type == EntityType::Arc,
           "single-point splitEntityAt on a circle -> one arc");
        ck(r.success && r.newEntities.size() == 1
           && near(std::fabs(r.newEntities[0].sweepAngle), 360.0),
           "the opened arc sweeps a full 360 degrees");
    }

    // ---- Split: arc -> sub-arcs summing to the original sweep ----
    {
        // Arc centered at origin, radius 5, from 0deg sweeping 180deg; cut at 90deg.
        Entity arc = createArc(220, Point2D(0, 0), 5.0, 0.0, 180.0);
        const SplitResult r = splitEntityAtIntersections(arc, { Point2D(0, 5) }, gen);
        ck(r.success && r.newEntities.size() == 2, "arc split at 1 interior point -> 2 sub-arcs");
        double total = 0.0;
        for (const Entity& e : r.newEntities) total += std::fabs(e.sweepAngle);
        ck(near(total, 180.0, 1e-6), "sub-arc sweeps sum to the original 180");
    }

    std::printf("\n%s (%d failure(s))\n", fails ? "FAILURES" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
