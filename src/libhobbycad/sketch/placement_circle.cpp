// =====================================================================
//  src/libhobbycad/sketch/placement_circle.cpp — placing a circle
// =====================================================================
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include "placement_detail.h"

#include <hobbycad/geometry/intersections.h>
#include <hobbycad/geometry/utils.h>

#include <cmath>

namespace hobbycad {
namespace sketch {
namespace placement_detail {

namespace {

bool isLine(const Entity& e)
{
    return e.type == EntityType::Line && e.points.size() >= 2;
}

/// A circle tangent to the picked lines; for two lines its size comes
/// from how far `hint` is from where they cross.
geometry::TangentCircleResult tangentCircle(CreationMode m, const PlacementInput& in,
                                            const Point2D& hint)
{
    const auto& t = in.targets;
    if (m == CreationMode::CircleTwoTangent && t.size() >= 2 && isLine(t[0]) && isLine(t[1])) {
        const auto crossing = geometry::infiniteLineIntersection(t[0].points[0], t[0].points[1],
                                                                 t[1].points[0], t[1].points[1]);
        if (!crossing.intersects) return {};
        return geometry::circleTangentToTwoLines(t[0].points[0], t[0].points[1],
                                                 t[1].points[0], t[1].points[1],
                                                 geometry::length(hint - crossing.point), hint);
    }
    if (m == CreationMode::CircleThreeTangent && t.size() >= 3 && isLine(t[0]) && isLine(t[1])
        && isLine(t[2])) {
        return geometry::circleTangentToThreeLines(t[0].points[0], t[0].points[1],
                                                   t[1].points[0], t[1].points[1],
                                                   t[2].points[0], t[2].points[1]);
    }
    return {};
}

/// The circle through three points, or with a locked radius through the
/// first two, its center on the third point's side and the third point
/// moved onto it.
bool throughThree(const PlacementInput& in, Point2D& center, double& radius, Point2D& third)
{
    const Point2D p1 = pointAt(in, 0);
    const Point2D p2 = pointAt(in, 1);
    third = pointAt(in, 2);
    if (const std::optional<double> locked = lengthLock(in, 0)) {
        if (geometry::lockedRadiusCenterToward(p1, p2, *locked, third, center)) {
            radius = *locked;
            if (geometry::lineLength(center, third) > geometry::kDegenerateLen) {
                third = geometry::closestPointOnCircle(third, center, radius);
            }
            return true;
        }
        // The first two points are too far apart for that radius.
    }
    const auto arc = geometry::arcFromThreePoints(p1, p2, third);
    if (!arc) return false;
    center = arc->center;
    radius = arc->radius;
    return true;
}

}  // namespace

Point2D circleCursor(CreationMode m, const PlacementInput& in)
{
    if (in.clicks.empty()) return in.cursor;
    if (m != CreationMode::CircleCenterRadius && m != CreationMode::CircleTwoPoint) {
        return in.cursor;   // a 3-point radius picks its center at the commit
    }
    const std::optional<double> locked = lengthLock(in, 0);
    const Point2D from = in.clicks[0];
    if (!locked || geometry::length(in.cursor - from) <= geometry::kDegenerateLen) {
        return in.cursor;
    }
    return geometry::applyPolarLock(from, in.cursor, locked, std::nullopt);
}

bool circleEntity(CreationMode m, const PlacementInput& in, Entity& out)
{
    if (in.clicks.empty()) return false;
    out.type = EntityType::Circle;
    switch (m) {
    case CreationMode::CircleTwoPoint: {
        // Stored as [center, the two diameter ends].
        const Point2D p1 = in.clicks[0];
        const Point2D p2 = pointAt(in, 1);
        out.radius = geometry::lineLength(p1, p2) / 2.0;
        out.points = {geometry::lineMidpoint(p1, p2), p1, p2};
        return out.radius > 0.1;
    }
    case CreationMode::CircleThreePoint: {
        // Stored as [center, the three clicks].
        if (in.clicks.size() + 1 < 3) return false;
        Point2D center, third;
        double radius = 0.0;
        if (!throughThree(in, center, radius, third)) return false;
        out.radius = radius;
        out.points = {center, pointAt(in, 0), pointAt(in, 1), third};
        return radius > 0.1;
    }
    case CreationMode::CircleTwoTangent:
    case CreationMode::CircleThreeTangent: {
        const geometry::TangentCircleResult tc = tangentCircle(m, in, in.clicks[0]);
        if (!tc.valid) return false;
        out.radius = tc.radius;
        out.points = {tc.center, Point2D(tc.center.x + tc.radius, tc.center.y)};
        return true;
    }
    default: {
        // Stored as [center, the point that set the radius].
        const Point2D center = in.clicks[0];
        const Point2D perimeter = pointAt(in, 1);
        const std::optional<double> locked = lengthLock(in, 0);
        out.radius = locked ? *locked : geometry::lineLength(center, perimeter);
        out.points = {center, perimeter};
        return out.radius > 0.1;
    }
    }
}

PlacementPreview circlePreview(CreationMode m, const PlacementInput& in)
{
    PlacementPreview out;
    if (in.clicks.empty()) return out;

    switch (m) {
    case CreationMode::CircleTwoPoint: {
        const Point2D p1 = in.clicks[0];
        const Point2D p2 = in.cursor;
        const Point2D center = geometry::lineMidpoint(p1, p2);
        const double r = geometry::lineLength(p1, p2) / 2.0;
        out.shapes.push_back(path(circlePoints(center, r), PreviewStroke::Pen, true));
        out.shapes.push_back(mark(center, PreviewMark::Center));
        out.shapes.push_back(segment(p1, p2, PreviewStroke::Guide));
        out.shapes.push_back(mark(p1, PreviewMark::Center));
        out.shapes.push_back(mark(p2, PreviewMark::Center));
        setValues(out, {2.0 * r});
        if (r > 0.05) {
            out.shapes.push_back(dimension(p1, p2, 2.0 * r, 0, LabelPlace::Below,
                                           IdleLabel::Line));
        }
        return out;
    }
    case CreationMode::CircleThreePoint: {
        const std::optional<double> locked = lengthLock(in, 0);
        if (in.clicks.size() >= 2) {
            Point2D center, third;
            double r = 0.0;
            if (!throughThree(in, center, r, third)) return out;
            out.shapes.push_back(path(circlePoints(center, r), PreviewStroke::Pen, true));
            out.shapes.push_back(mark(center, PreviewMark::Center));
            // The three points on it; a 3-point circle has no quadrant marks.
            for (const Point2D& p : {in.clicks[0], in.clicks[1], third}) {
                out.shapes.push_back(mark(p, PreviewMark::Center));
            }
            setValues(out, {r});
            if (r > 0.1) {
                out.shapes.push_back(dimension(center, in.clicks[0], r, 0, LabelPlace::Below,
                                               IdleLabel::Line));
            }
            return out;
        }
        const Point2D p1 = in.clicks[0];
        out.shapes.push_back(segment(p1, in.cursor, PreviewStroke::Guide));
        out.shapes.push_back(mark(p1, PreviewMark::Center));
        out.shapes.push_back(mark(in.cursor, PreviewMark::Center));
        if (locked) {
            // Both circles the locked radius allows.
            const geometry::ChordCenters cc =
                geometry::circleCentersThroughPoints(p1, in.cursor, *locked);
            if (cc.valid) {
                out.shapes.push_back(path(circlePoints(cc.first, *locked),
                                          PreviewStroke::Faint, true));
                out.shapes.push_back(path(circlePoints(cc.second, *locked),
                                          PreviewStroke::Faint, true));
            }
        }
        return out;
    }
    case CreationMode::CircleTwoTangent:
    case CreationMode::CircleThreeTangent: {
        // The circle the picked curves give, where there is one.
        const geometry::TangentCircleResult tc = tangentCircle(m, in, in.cursor);
        if (tc.valid) {
            out.shapes.push_back(path(circlePoints(tc.center, tc.radius), PreviewStroke::Pen,
                                      true));
            out.shapes.push_back(mark(tc.center, PreviewMark::Center));
        }
        return out;
    }
    default: {
        const Point2D center = in.clicks[0];
        const Point2D perimeter = in.cursor;
        const double r = geometry::lineLength(center, perimeter);
        out.shapes.push_back(path(circlePoints(center, r), PreviewStroke::Pen, true));
        out.shapes.push_back(mark(center, PreviewMark::Center));
        out.shapes.push_back(mark(perimeter, PreviewMark::Center));
        setValues(out, {r});
        if (r > 0.1) {
            out.shapes.push_back(dimension(center, perimeter, r, 0, LabelPlace::Below,
                                           IdleLabel::Line));
        }
        return out;
    }
    }
}

}  // namespace placement_detail
}  // namespace sketch
}  // namespace hobbycad
