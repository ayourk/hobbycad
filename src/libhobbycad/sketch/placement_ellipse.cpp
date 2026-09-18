// =====================================================================
//  src/libhobbycad/sketch/placement_ellipse.cpp — placing an ellipse or
//  an elliptical arc
// =====================================================================
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include "placement_detail.h"

#include <hobbycad/geometry/utils.h>
#include <hobbycad/sketch/queries.h>

#include <cmath>

namespace hobbycad {
namespace sketch {
namespace placement_detail {

namespace {

EllipsePlacement modeOf(CreationMode m)
{
    switch (m) {
    case CreationMode::EllipseThreePoint:   return EllipsePlacement::ThreePoint;
    case CreationMode::EllipseArc:          return EllipsePlacement::Arc;
    case CreationMode::EllipseSpanRiseArc:  return EllipsePlacement::SpanRise;
    case CreationMode::EllipseCornerArc:    return EllipsePlacement::Corner;
    case CreationMode::EllipseEndpointsArc: return EllipsePlacement::Endpoints;
    default:                                return EllipsePlacement::CenterAxes;
    }
}

/// The clicks with the cursor after them.
std::vector<Point2D> withCursor(const PlacementInput& in)
{
    std::vector<Point2D> pts = in.clicks;
    pts.push_back(in.cursor);
    return pts;
}

/// Elliptical Arc only: the ellipse the first three clicks fix.
bool fixedEllipse(EllipsePlacement mode, const PlacementInput& in, Entity& out)
{
    if (mode != EllipsePlacement::Arc || in.clicks.size() < 3) return false;
    const std::vector<Point2D> first(in.clicks.begin(), in.clicks.begin() + 3);
    return ellipseFromPlacement(EllipsePlacement::CenterAxes, first, false, out);
}

/// Where `p` lands on the perimeter of `e`: at the parameter a click there
/// is read at, so the constrained cursor and the click agree.
Point2D onPerimeter(const Entity& e, const Point2D& p)
{
    return ellipsePointAtParamDeg(e, ellipseParamDeg(e, p));
}

/// The point a radius-type lock measures from: the center for Center + Axes
/// and Corner, the first click for 3-Point's span, the span's midpoint for
/// the rise.
Point2D lockOrigin(EllipsePlacement mode, const PlacementInput& in, std::size_t slot)
{
    const bool midpointCenter =
        (mode == EllipsePlacement::ThreePoint || mode == EllipsePlacement::SpanRise);
    if (midpointCenter && slot >= 2 && in.clicks.size() >= 2) {
        return geometry::lineMidpoint(in.clicks[0], in.clicks[1]);
    }
    return in.clicks[0];
}

/// The radius of `e` that is not `known`, an axis the placement already
/// fixed. A second axis longer than the first becomes the major one, so a
/// stage's value can be either field.
double otherRadius(const Entity& e, double known)
{
    return std::fabs(e.majorRadius - known) < geometry::kZeroEps ? e.minorRadius
                                                                 : e.majorRadius;
}

}  // namespace

Point2D ellipseCursor(CreationMode m, const PlacementInput& in)
{
    if (in.clicks.empty()) return in.cursor;
    const EllipsePlacement mode = modeOf(m);
    const std::size_t slot = in.clicks.size();   // the point still being placed
    Point2D p = in.cursor;

    // Elliptical Arc, choosing the start and end: on the perimeter of the
    // ellipse the first three clicks fixed. A start or end anywhere else
    // means nothing, so nothing releases it.
    Entity fixed;
    const bool onFixed = fixedEllipse(mode, in, fixed);
    if (onFixed) p = onPerimeter(fixed, p);

    const std::optional<double> locked = lockAt(in, 0);
    if (!locked) return p;

    const bool radiusStage = slot <= 2 && mode != EllipsePlacement::Endpoints;
    const bool acrossFirstAxis =
        (mode == EllipsePlacement::SpanRise || mode == EllipsePlacement::Corner) && slot == 2;
    if (radiusStage && *locked > 0.0) {
        const Point2D o = lockOrigin(mode, in, slot);
        if (acrossFirstAxis) {
            // A rise, or a corner's second leg, is measured PERPENDICULAR to
            // the first axis: at that distance on the cursor's side of it.
            const Point2D axis = in.clicks[1] - in.clicks[0];
            if (geometry::isPositiveLength(geometry::length(axis))) {
                const Point2D n = geometry::perpendicular(geometry::normalize(axis));
                const double side = geometry::dot(in.cursor - o, n) < 0.0 ? -1.0 : 1.0;
                p = o + n * (*locked * side);
            }
        } else if (geometry::isPositiveLength(geometry::length(in.cursor - o))) {
            p = geometry::applyPolarLock(o, in.cursor, locked, std::nullopt);
        }
    } else if (onFixed) {
        // The start and end lock an ANGLE: the point at that parameter on
        // the fixed ellipse. The sweep is measured from the placed start.
        double paramDeg = *locked;
        if (slot == 4) paramDeg = ellipseParamDeg(fixed, in.clicks[3]) + *locked;
        p = ellipsePointAtParamDeg(fixed, paramDeg);
    }
    return p;
}

bool ellipseEntity(CreationMode m, const PlacementInput& in, Entity& out)
{
    // The click order -> stored layout translation is ellipseFromPlacement's,
    // shared with the ghost: a wrong layout renders identically and shows only
    // when the solver or a drag reads the points.
    const EllipsePlacement mode = modeOf(m);
    const int needed = ellipsePlacementClicks(mode);
    std::vector<Point2D> pts = in.clicks;
    if (static_cast<int>(pts.size()) < needed) pts.push_back(in.cursor);
    const int id = out.id;
    const bool construction = out.isConstruction;
    if (!ellipseFromPlacement(mode, pts, in.flipped, out)) return false;
    out.id = id;
    out.isConstruction = construction;
    return true;
}

PlacementPreview ellipsePreview(CreationMode m, const PlacementInput& in)
{
    PlacementPreview out;
    const int placed = placedCount(in);
    if (placed == 0) return out;
    const EllipsePlacement mode = modeOf(m);
    const bool arcMode = mode == EllipsePlacement::Arc;
    const bool hasField = !placementFields({SketchTool::Ellipse, m}, {placed}).empty();

    // The entity the clicks so far describe, with the cursor as the point
    // still being placed, by the rule the commit uses.
    Entity ghost;
    if (!ellipseFromPlacement(mode, withCursor(in), in.flipped, ghost)) {
        // Nothing to show yet, or for Endpoints no ellipse fits: the clicks
        // and a rubber line to the cursor.
        placedClicks(in, out.shapes);
        return out;
    }

    // Elliptical Arc, choosing the start: no curve, only the fixed points
    // and the cursor held on the perimeter (Aaron, 2026-09-16).
    const bool choosingStart = arcMode && placed == 3;
    // The arc ghost proper is solid; sizing ghosts stay dashed.
    bool solid = false;
    switch (mode) {
    case EllipsePlacement::Arc:       solid = placed >= 4; break;
    case EllipsePlacement::SpanRise:  solid = placed >= 2; break;
    case EllipsePlacement::Corner:    solid = placed >= 2; break;
    case EllipsePlacement::Endpoints: solid = placed >= 3; break;
    default: break;
    }
    if (!choosingStart) {
        if (mode == EllipsePlacement::Endpoints) {
            // The whole ellipse the axis click is choosing, under the arc.
            Entity whole = ghost;
            whole.ellipseStart = 0.0;
            whole.ellipseSweep = 360.0;
            out.shapes.push_back(path(tessellate(whole, 96)));
        }
        out.shapes.push_back(
            path(tessellate(ghost, 96), solid ? PreviewStroke::Solid : PreviewStroke::Pen));
    }
    if (arcMode && placed >= 3) out.shapes.push_back(mark(in.cursor, PreviewMark::Cursor));

    const Point2D c = ghost.points[0];

    if (mode == EllipsePlacement::CenterAxes || mode == EllipsePlacement::ThreePoint
        || arcMode) {
        // Center and the two axis ends. The stored +minor end is a quarter
        // turn counter-clockwise from the +major end, but the marker sits on
        // the cursor's side while the minor is chosen and on the third
        // click's side after, so it never jumps (Aaron, 2026-09-16).
        const Point2D majorEnd = ghost.points[1];
        Point2D minorMark = ghost.points[2];
        if (placed >= 2) {
            const Point2D ref = placed == 2 ? in.cursor : in.clicks[2];
            if (geometry::cross(majorEnd - c, ref - c) < 0.0) minorMark = c - (minorMark - c);
        }
        out.shapes.push_back(mark(c, PreviewMark::Click));
        out.shapes.push_back(mark(majorEnd, PreviewMark::Click));
        out.shapes.push_back(mark(minorMark, PreviewMark::Click));

        if (hasField) {
            double value = ghost.ellipseSweep;
            Point2D far = in.cursor;
            if (placed == 1) {
                value = ghost.majorRadius;
                far = majorEnd;
            } else if (placed == 2) {
                value = ghost.minorRadius;
                far = minorMark;
            } else if (placed == 3) {
                value = ghost.ellipseStart;
            }
            setValues(out, {value});
            out.shapes.push_back(dimension(c, far, value, 0, LabelPlace::Below,
                                           placed <= 2 ? IdleLabel::Line : IdleLabel::None,
                                           0, 0.0));
        }
        return out;
    }

    // The arc placements: their clicks are the arc's own points.
    placedClicks(in, out.shapes, false);
    out.shapes.push_back(mark(c, PreviewMark::Click));
    if (!hasField) return out;

    double value = 0.0;
    Point2D from = c;
    if (mode == EllipsePlacement::SpanRise) {
        if (placed == 1) {
            from = in.clicks[0];
            value = geometry::lineLength(in.cursor, from);   // the span
        } else {
            // The rise is the radius across the span, whichever axis it
            // became.
            value = otherRadius(ghost, geometry::lineLength(in.clicks[0], in.clicks[1]) / 2.0);
        }
    } else if (mode == EllipsePlacement::Corner) {
        if (placed == 1) {
            value = geometry::lineLength(in.cursor, c);   // the first leg
        } else {
            value = otherRadius(ghost, geometry::lineLength(in.clicks[1], c));
        }
    }
    setValues(out, {value});
    out.shapes.push_back(dimension(from, in.cursor, value, 0, LabelPlace::Below,
                                   IdleLabel::Line));
    return out;
}

}  // namespace placement_detail
}  // namespace sketch
}  // namespace hobbycad
