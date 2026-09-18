// =====================================================================
//  src/libhobbycad/sketch/placement_polygon.cpp — placing a polygon
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

/// A regular polygon's radius: locked, or to the cursor. For Circumscribed
/// it is the inscribed radius (the apothem).
double regularRadius(const PlacementInput& in)
{
    const std::optional<double> locked = lengthLock(in, 0);
    return locked ? *locked : geometry::lineLength(in.clicks[0], pointAt(in, 1));
}

std::vector<Point2D> regularVertices(CreationMode m, const PlacementInput& in, double radius)
{
    // The first vertex points at the cursor.
    const Point2D center = in.clicks[0];
    const Point2D toward = pointAt(in, 1);
    const double startAngle = std::atan2(toward.y - center.y, toward.x - center.x);
    return geometry::regularPolygonVertices(center, radius, in.sides > 0 ? in.sides : 6,
                                            startAngle,
                                            m == CreationMode::PolygonCircumscribed);
}

}  // namespace

Point2D polygonCursor(CreationMode m, const PlacementInput& in)
{
    if (m == CreationMode::PolygonFreeform || in.clicks.empty()) return in.cursor;
    const std::optional<double> locked = lengthLock(in, 0);
    const Point2D center = in.clicks[0];
    if (!locked || geometry::length(in.cursor - center) <= geometry::kDegenerateLen) {
        return in.cursor;
    }
    return geometry::applyPolarLock(center, in.cursor, locked, std::nullopt);
}

bool polygonEntity(CreationMode m, const PlacementInput& in, Entity& out)
{
    out.type = EntityType::Polygon;
    if (m == CreationMode::PolygonFreeform) {
        // The clicks are the vertices; at least a triangle.
        out.points.assign(in.clicks.begin(), in.clicks.end());
        out.sides = static_cast<int>(in.clicks.size());
        return in.clicks.size() >= 3;
    }
    // Stored as [center, the vertices].
    if (in.clicks.empty()) return false;
    const double radius = regularRadius(in);
    out.radius = radius;
    out.sides = in.sides > 0 ? in.sides : 6;
    out.points = {in.clicks[0]};
    if (radius <= 0.1) return false;
    for (const Point2D& v : regularVertices(m, in, radius)) out.points.push_back(v);
    return true;
}

PlacementPreview polygonPreview(CreationMode m, const PlacementInput& in)
{
    PlacementPreview out;
    if (in.clicks.empty()) return out;

    if (m == CreationMode::PolygonFreeform) {
        std::vector<Point2D> run = in.clicks;
        run.push_back(in.cursor);
        out.shapes.push_back(path(run));
        if (in.clicks.size() >= 2) {
            out.shapes.push_back(segment(in.cursor, in.clicks[0], PreviewStroke::Faint));
        }
        for (const Point2D& p : in.clicks) out.shapes.push_back(mark(p, PreviewMark::Click));
        if (placementClosesLoop({SketchTool::Polygon, m}, in, in.cursor)) {
            out.shapes.push_back(mark(in.clicks[0], PreviewMark::CloseLoop));
        }
        return out;
    }

    const Point2D center = in.clicks[0];
    const double radius = regularRadius(in);
    setValues(out, {radius});
    if (radius <= 0.1) return out;
    out.shapes.push_back(path(circlePoints(center, radius), PreviewStroke::Construction, true));
    out.shapes.push_back(path(regularVertices(m, in, radius), PreviewStroke::Pen, true));
    out.shapes.push_back(mark(center, PreviewMark::Click));
    out.shapes.push_back(segment(center, in.cursor, PreviewStroke::Guide));
    out.shapes.push_back(mark(in.cursor, PreviewMark::Center));
    // The radius beside its line, on the right of the way it runs.
    out.shapes.push_back(dimension(center, in.cursor, radius, 0, LabelPlace::Across,
                                   IdleLabel::Value, 0, -14.0));
    return out;
}

}  // namespace placement_detail
}  // namespace sketch
}  // namespace hobbycad
