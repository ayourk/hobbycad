// =====================================================================
//  src/libhobbycad/sketch/placement_line.cpp — placing a line
// =====================================================================
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include "placement_detail.h"

#include <hobbycad/geometry/utils.h>
#include <hobbycad/units.h>

#include <algorithm>
#include <cmath>

namespace hobbycad {
namespace sketch {
namespace placement_detail {

namespace {

/// Essentially exact: is the snap point already ON the axis, not near it.
constexpr double kOnAxis = 0.001;

const Entity* circularTarget(const PlacementInput& in)
{
    if (in.targets.empty() || !tangentLineTarget(in.targets[0])) return nullptr;
    return &in.targets[0];
}

/// True when the angle `deg` about `arc`'s center lies within its sweep.
bool withinArc(const Entity& arc, double deg)
{
    const double start = normalizeAngle360(arc.startAngle);
    const double end = normalizeAngle360(arc.startAngle + arc.sweepAngle);
    const double a = normalizeAngle360(deg);
    if (arc.sweepAngle >= 0) {
        return end >= start ? (a >= start && a <= end) : (a >= start || a <= end);
    }
    return end <= start ? (a <= start && a >= end) : (a <= start || a >= end);
}

/// A tangent line at a locked angle touches its circle at one of two
/// points; the start slides to the one whose line runs toward the cursor,
/// and, on an arc, to one the arc actually has.
Point2D tangentStartAtAngle(const Entity& target, double angleDegrees, const Point2D& toward)
{
    const Point2D center = target.points[0];
    const double r = target.radius;
    const Point2D tp1 = atAngle(center, r, angleDegrees + 90.0);
    const Point2D tp2 = atAngle(center, r, angleDegrees - 90.0);
    const Point2D dir = atAngle(Point2D(), 1.0, angleDegrees);
    const bool firstAhead = geometry::dot(toward - tp1, dir) > geometry::dot(toward - tp2, dir);
    Point2D start = firstAhead ? tp1 : tp2;
    if (target.type == EntityType::Arc && !withinArc(target, angleDeg(center, start))) {
        start = firstAhead ? tp2 : tp1;
    }
    return start;
}

}  // namespace

Point2D lineCursor(CreationMode m, const PlacementInput& in, const Point2D& raw, bool keepSnap,
                   std::vector<Point2D>* clicks)
{
    if (in.clicks.empty()) return in.cursor;
    Point2D start = in.clicks[0];
    Point2D c = in.cursor;

    // The mode's path first.
    switch (m) {
    case CreationMode::LineHorizontal:
        if (!(std::abs(c.y - start.y) < kOnAxis && keepSnap)) c.y = start.y;
        break;
    case CreationMode::LineVertical:
        if (!(std::abs(c.x - start.x) < kOnAxis && keepSnap)) c.x = start.x;
        break;
    case CreationMode::LineTangent:
        if (const Entity* target = circularTarget(in)) {
            // From the free start P there are two tangent lines to the
            // circle: P->C turned by +/- asin(r/d). The cursor goes to the
            // nearer one, so the line snaps at two angles as it is drawn
            // (Aaron). No snap is kept: the tangent is the point.
            const Point2D toCenter = target->points[0] - start;
            const double d = geometry::length(toCenter);
            if (d >= geometry::kDegenerateLen) {
                const double turn = std::asin(std::min(1.0, std::abs(target->radius) / d));
                const Point2D u = toCenter / d;
                const auto foot = [&](double angle) {
                    const Point2D dir = geometry::rotatePoint(u, radiansToDegrees(angle));
                    return start + dir * geometry::dot(raw - start, dir);
                };
                const Point2D f1 = foot(turn);
                const Point2D f2 = foot(-turn);
                c = geometry::lineLength(raw, f1) <= geometry::lineLength(raw, f2) ? f1 : f2;
            }
        }
        break;
    default:
        break;
    }

    // Then the locks. A tangent line's locked angle moves its start round
    // the circle.
    const std::optional<double> length = lengthLock(in, 0);
    const std::optional<double> angle = lockAt(in, 1);
    if (m == CreationMode::LineTangent && angle) {
        if (const Entity* target = circularTarget(in)) {
            start = tangentStartAtAngle(*target, *angle, c);
            if (clicks && !clicks->empty()) (*clicks)[0] = start;
        }
    }
    if (length || angle) c = geometry::applyPolarLock(start, c, length, angle);
    return c;
}

bool lineEntity(CreationMode, const PlacementInput& in, Entity& out)
{
    if (in.clicks.empty()) return false;
    const Point2D a = in.clicks[0];
    const Point2D b = pointAt(in, 1);
    out.type = EntityType::Line;
    out.points = {a, b};
    return geometry::lineLength(a, b) > 0.1;
}

PlacementPreview linePreview(CreationMode m, const PlacementInput& in)
{
    PlacementPreview out;
    // A picked tangent target is shown so it is clear what the line will
    // touch (Aaron).
    if (m == CreationMode::LineTangent) {
        if (const Entity* target = circularTarget(in)) {
            out.shapes.push_back(path(circlePoints(target->points[0], std::abs(target->radius)),
                                      PreviewStroke::Target, true));
        }
    }
    if (in.clicks.empty()) return out;

    const Point2D start = in.clicks[0];
    const Point2D end = in.cursor;
    out.shapes.push_back(segment(start, end));
    const double length = geometry::lineLength(start, end);
    setValues(out, {length, angleDeg(start, end)});
    if (length > 0.1) {
        out.shapes.push_back(dimension(start, end, length, 0, LabelPlace::Along, IdleLabel::Line));
        out.shapes.push_back(dimension(start, end, out.fieldValues[1], 1, LabelPlace::Along,
                                       IdleLabel::None, 1));
    }
    return out;
}

}  // namespace placement_detail
}  // namespace sketch
}  // namespace hobbycad
