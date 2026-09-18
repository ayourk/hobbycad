// =====================================================================
//  src/libhobbycad/sketch/placement_rectangle.cpp — placing a rectangle
//  or a parallelogram
// =====================================================================
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include "placement_detail.h"

#include <hobbycad/geometry/utils.h>

#include <cmath>

namespace hobbycad {
namespace sketch {
namespace placement_detail {

namespace {

/// A corner rectangle with both sides locked and a kept turn.
bool turned(const PlacementInput& in)
{
    return in.turn && lengthLock(in, 0) && lengthLock(in, 1);
}

/// The four corners of a turned corner rectangle: its sides keep the
/// directions they had at the lock, turned by how far the cursor has
/// turned about the first corner since.
std::vector<Point2D> turnedCorners(const PlacementInput& in)
{
    const Point2D origin = in.clicks[0];
    const Point2D toward = pointAt(in, 1);
    const double rotation =
        std::atan2(toward.y - origin.y, toward.x - origin.x) - in.turn->reference;
    const Point2D wDir = geometry::polarPoint(Point2D(), 1.0, in.turn->widthAngle + rotation);
    const Point2D hDir = geometry::polarPoint(Point2D(), 1.0, in.turn->heightAngle + rotation);
    const Point2D p1 = origin + wDir * *lockAt(in, 0);
    const Point2D p3 = origin + hDir * *lockAt(in, 1);
    return {origin, p1, p1 + hDir * *lockAt(in, 1), p3};
}

/// The two opposite corners of an axis-aligned rectangle.
void axisCorners(CreationMode m, const PlacementInput& in, Point2D& a, Point2D& b)
{
    const Point2D first = in.clicks[0];
    const Point2D second = pointAt(in, 1);
    if (m == CreationMode::RectCenter) {
        const Point2D delta = second - first;
        a = first - delta;
        b = first + delta;
    } else {
        a = first;
        b = second;
    }
}

/// The width of a 3-point rectangle: the cursor's signed distance across
/// the first edge.
double acrossFirstEdge(const Point2D& p1, const Point2D& p2, const Point2D& p)
{
    const Point2D dir = geometry::normalize(p2 - p1);
    return geometry::dot(p - p1, geometry::perpendicular(dir));
}

/// The first edge of the staged modes, with its length and angle fields.
void firstEdge(const Point2D& p1, const Point2D& p2, PlacementPreview& out)
{
    out.shapes.push_back(segment(p1, p2));
    const double len = geometry::lineLength(p1, p2);
    setValues(out, {len, angleDeg(p1, p2)});
    if (len > 0.1) {
        out.shapes.push_back(dimension(p1, p2, len, 0, LabelPlace::Along, IdleLabel::Line));
        out.shapes.push_back(dimension(p1, p2, out.fieldValues[1], 1, LabelPlace::Along,
                                       IdleLabel::None, 1));
    }
}

}  // namespace

Point2D rectangleCursor(CreationMode m, const PlacementInput& in)
{
    if (in.clicks.empty()) return in.cursor;
    const std::size_t placed = in.clicks.size();
    const Point2D p1 = in.clicks[0];
    const std::optional<double> len = lengthLock(in, 0);
    const std::optional<double> other = lockAt(in, 1);

    switch (m) {
    case CreationMode::RectThreePoint:
        if (placed == 1) {
            return (len || other) ? geometry::applyPolarLock(p1, in.cursor, len, other)
                                  : in.cursor;
        }
        if (len) {
            // The width, on the cursor's side of the first edge, keeping the
            // cursor's position along it.
            const Point2D p2 = in.clicks[1];
            if (geometry::lineLength(p1, p2) > 0.001) {
                const Point2D dir = geometry::normalize(p2 - p1);
                const Point2D perp = geometry::perpendicular(dir);
                const double side = acrossFirstEdge(p1, p2, in.cursor) >= 0 ? 1.0 : -1.0;
                return p1 + dir * geometry::dot(in.cursor - p1, dir) + perp * (side * *len);
            }
        }
        return in.cursor;

    case CreationMode::RectParallelogram:
        if (!len && !other) return in.cursor;
        if (placed == 1) return geometry::applyPolarLock(p1, in.cursor, len, other);
        return geometry::applyInsideAngleLock(p1, in.clicks[1], in.cursor, len, other);

    case CreationMode::RectCenter: {
        const std::optional<double> h = lengthLock(in, 1);
        Point2D delta = in.cursor - p1;
        if (len) delta.x = delta.x >= 0 ? *len / 2.0 : -*len / 2.0;
        if (h) delta.y = delta.y >= 0 ? *h / 2.0 : -*h / 2.0;
        return p1 + delta;
    }

    default: {
        // With both sides locked and a turn kept, the cursor only turns the
        // rectangle, so it stays where it is.
        if (turned(in)) return in.cursor;
        const std::optional<double> h = lengthLock(in, 1);
        Point2D delta = in.cursor - p1;
        if (len) delta.x = delta.x >= 0 ? *len : -*len;
        if (h) delta.y = delta.y >= 0 ? *h : -*h;
        return p1 + delta;
    }
    }
}

bool rectangleEntity(CreationMode m, const PlacementInput& in, Entity& out)
{
    if (in.clicks.empty()) return false;
    switch (m) {
    case CreationMode::RectThreePoint: {
        // Two clicks give the first edge, the third the width; stored as the
        // four corners of a turned rectangle.
        const Point2D p1 = in.clicks[0];
        const Point2D p2 = pointAt(in, 1);
        Point2D corners[4];
        if (!rectangleFromThreePoints(p1, p2, pointAt(in, 2), corners)) return false;
        out.points.assign(corners, corners + 4);
        return geometry::lineLength(p1, p2) > 0.1;
    }
    case CreationMode::RectParallelogram: {
        // Two edges, and the fourth corner that closes them.
        const Point2D p1 = in.clicks[0];
        const Point2D p2 = pointAt(in, 1);
        const Point2D p3 = pointAt(in, 2);
        out.points = {p1, p2, p3, parallelogramFourthCorner(p1, p2, p3)};
        return geometry::lineLength(p1, p2) > 0.1 && geometry::lineLength(p2, p3) > 0.1;
    }
    default:
        if (m == CreationMode::RectCorner && turned(in)) {
            const std::vector<Point2D> corners = turnedCorners(in);
            out.points.assign(corners.begin(), corners.end());
            return geometry::lineLength(corners[0], corners[1]) > 0.1;
        }
        // Two opposite corners; a center rectangle keeps no center.
        Point2D a, b;
        axisCorners(m, in, a, b);
        out.points = {a, b};
        return geometry::lineLength(a, b) > 0.1;
    }
}

PlacementPreview rectanglePreview(CreationMode m, const PlacementInput& in)
{
    PlacementPreview out;
    if (in.clicks.empty()) return out;
    const std::size_t placed = in.clicks.size();

    if (m == CreationMode::RectThreePoint || m == CreationMode::RectParallelogram) {
        const Point2D p1 = in.clicks[0];
        if (placed < 2) {
            firstEdge(p1, in.cursor, out);
        } else if (m == CreationMode::RectThreePoint) {
            const Point2D p2 = in.clicks[1];
            const double edgeLen = geometry::lineLength(p1, p2);
            if (edgeLen > 0.01) {
                const double across = acrossFirstEdge(p1, p2, in.cursor);
                const Point2D offset =
                    geometry::perpendicular(geometry::normalize(p2 - p1)) * across;
                const Point2D c3 = p2 + offset;
                out.shapes.push_back(path({p1, p2, c3, p1 + offset}, PreviewStroke::Pen, true));
                out.shapes.push_back(dimension(p1, p2, edgeLen, -1, LabelPlace::Along,
                                               IdleLabel::Line));
                const double width = std::abs(across);
                setValues(out, {width});
                if (width > 0.1) {
                    out.shapes.push_back(dimension(p2, c3, width, 0, LabelPlace::Along,
                                                   IdleLabel::Line));
                }
            }
        } else {
            const Point2D p2 = in.clicks[1];
            const Point2D p3 = in.cursor;
            const double edge1 = geometry::lineLength(p1, p2);
            const double edge2 = geometry::lineLength(p2, p3);
            // The INSIDE angle at p2, between the two edges.
            const double inside = (edge1 > 0.001 && edge2 > 0.001)
                ? geometry::angleBetween(p1 - p2, p3 - p2) : 0.0;
            out.shapes.push_back(path({p1, p2, p3, parallelogramFourthCorner(p1, p2, p3)},
                                      PreviewStroke::Pen, true));
            out.shapes.push_back(dimension(p1, p2, edge1, -1, LabelPlace::Along,
                                           IdleLabel::Line));
            setValues(out, {edge2, inside});
            if (edge2 > 0.1) {
                out.shapes.push_back(dimension(p2, p3, edge2, 0, LabelPlace::Along,
                                               IdleLabel::Line));
                if (edge1 > 0.1) {
                    PreviewShape angle = dimension(p2, p1, inside, 1, LabelPlace::Angle,
                                                   IdleLabel::AngleValue);
                    angle.points.push_back(p3);
                    out.shapes.push_back(angle);
                }
            }
        }
        for (const Point2D& p : in.clicks) out.shapes.push_back(mark(p, PreviewMark::Corner));
        return out;
    }

    if (m == CreationMode::RectCorner && turned(in)) {
        const std::vector<Point2D> c = turnedCorners(in);
        out.shapes.push_back(path(c, PreviewStroke::Pen, true));
        const double width = *lockAt(in, 0);
        const double height = *lockAt(in, 1);
        setValues(out, {width, height});
        out.shapes.push_back(dimension(c[0], c[1], width, 0, LabelPlace::Across,
                                       IdleLabel::None, 0, 18.0));
        out.shapes.push_back(dimension(c[0], c[3], height, 1, LabelPlace::Across,
                                       IdleLabel::None, 0, 40.0));
        return out;
    }

    Point2D a, b;
    axisCorners(m, in, a, b);
    const double minX = std::min(a.x, b.x), maxX = std::max(a.x, b.x);
    const double minY = std::min(a.y, b.y), maxY = std::max(a.y, b.y);
    out.shapes.push_back(path({{minX, minY}, {maxX, minY}, {maxX, maxY}, {minX, maxY}},
                              PreviewStroke::Pen, true));
    if (m == CreationMode::RectCenter) {
        out.shapes.push_back(mark(in.clicks[0], PreviewMark::Center));
    }
    const double width = maxX - minX;
    const double height = maxY - minY;
    setValues(out, {width, height});
    if (width > 0.1 || height > 0.1) {
        // The width under the bottom edge, the height right of the right one.
        out.shapes.push_back(dimension({minX, minY}, {maxX, minY}, width, 0, LabelPlace::Below,
                                       width > 0.1 ? IdleLabel::Line : IdleLabel::None));
        out.shapes.push_back(dimension({maxX, maxY}, {maxX, minY}, height, 1, LabelPlace::Right,
                                       height > 0.1 ? IdleLabel::Line : IdleLabel::None,
                                       0, 40.0));
    }
    return out;
}

}  // namespace placement_detail
}  // namespace sketch
}  // namespace hobbycad
