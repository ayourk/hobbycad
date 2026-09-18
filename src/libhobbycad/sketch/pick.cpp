// =====================================================================
//  src/libhobbycad/sketch/pick.cpp — what a click in a sketch hits
// =====================================================================
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include <hobbycad/sketch/pick.h>

#include <hobbycad/sketch/snap.h>

namespace hobbycad {
namespace sketch {

bool entityHit(const Entity& e, const Point2D& at, double tolerance, const TextHitTest& textHit)
{
    // Text is as large as the front end draws it.
    if (e.type == EntityType::Text) return textHit && textHit(e, at);
    return e.containsPoint(at, tolerance);
}

bool midpointGrip(const Entity& e, Point2D& out)
{
    const bool line = e.type == EntityType::Line && e.points.size() >= 2;
    const bool arc = e.type == EntityType::Arc && e.points.size() >= 3;
    if (!line && !arc) return false;
    out = pointAtParameter(e, 0.5);
    return true;
}

int nearestSlotAnchor(const Entity& slot, const Point2D& at, double tolerance)
{
    int best = -1;
    double bestDistance = tolerance;
    const std::vector<SnapPoint> anchors = slotAnchorPoints(slot);
    for (int i = 0; i < static_cast<int>(anchors.size()); ++i) {
        const double d =
            geometry::lineLength(anchors[static_cast<std::size_t>(i)].position, at);
        if (d <= bestDistance) {
            bestDistance = d;
            best = i;
        }
    }
    return best;
}

bool nearestBezierLeg(const Entity& spline, const Point2D& at, double& distance, int& i0,
                      int& i1)
{
    bool found = false;
    const int n = static_cast<int>(spline.points.size());
    for (int i = 0; i + 1 < n; ++i) {
        const Point2D a = spline.points[static_cast<std::size_t>(i)];
        const Point2D b = spline.points[static_cast<std::size_t>(i) + 1];
        const double d = geometry::lineLength(geometry::closestPointOnSegment(at, a, b), at);
        if (d < distance) {
            distance = d;
            i0 = i;
            i1 = i + 1;
            found = true;
        }
    }
    return found;
}

bool tangentLineCircle(const Entity* a, const Entity* b, const Entity*& line,
                       const Entity*& circle)
{
    if (!a || !b) return false;
    line = a->type == EntityType::Line ? a : (b->type == EntityType::Line ? b : nullptr);
    circle = a->type == EntityType::Circle ? a : (b->type == EntityType::Circle ? b : nullptr);
    return line && circle;
}

}  // namespace sketch
}  // namespace hobbycad
