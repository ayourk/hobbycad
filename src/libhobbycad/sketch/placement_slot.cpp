// =====================================================================
//  src/libhobbycad/sketch/placement_slot.cpp — placing a slot
// =====================================================================
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include "placement_detail.h"

#include <hobbycad/geometry/intersections.h>
#include <hobbycad/geometry/utils.h>
#include <hobbycad/sketch/queries.h>
#include <hobbycad/units.h>

#include <cmath>

namespace hobbycad {
namespace sketch {
namespace placement_detail {

namespace {

bool isArcSlot(CreationMode m)
{
    return m == CreationMode::SlotArcRadius || m == CreationMode::SlotArcEnds;
}

/// The half width to draw and build with; a slot always has one.
double halfWidth(const PlacementInput& in)
{
    return in.slotRadius < 0.1 ? 5.0 : in.slotRadius;
}

/// The end of an arc slot placed about `center` from `start`, toward `c`:
/// on the arc, at the locked sweep, or held off the start by the floor
/// separation (one cap radius between the ends) and, the long way round,
/// short of where the caps would close from the other side.
Point2D arcSlotEnd(const PlacementInput& in, const Point2D& center, const Point2D& start,
                   const Point2D& c)
{
    const double arcRadius = geometry::lineLength(center, start);
    if (const std::optional<double> sweep = lockAt(in, 0)) {
        return geometry::pointAtLockedSweep(center, start, c, *sweep, in.flipped);
    }
    const double startAngle = std::atan2(start.y - center.y, start.x - center.x);
    double angle = std::atan2(c.y - center.y, c.x - center.x);
    const double radius = halfWidth(in);

    // The FLOOR, not the tangent separation: between the two the caps
    // overlap, which is how the middle of the ring is freed. The chord form,
    // not an arc length, or the caps overlap a little and cut the cusp off.
    const double floorDeg = arcSlotFloorSeparationDegrees(arcRadius, radius);
    const double minSep = floorDeg > 0.0 ? degreesToRadians(floorDeg) : 0.1;
    const double diff = geometry::wrapSweepRad(angle - startAngle);
    if (std::abs(diff) < minSep) angle = startAngle + (diff >= 0 ? minSep : -minSep);

    if (in.flipped) {
        // The same limit from the other side, which the short separation
        // above cannot see.
        const double maxDeg = absoluteMaxArcSlotSweepDegrees(arcRadius, radius);
        if (maxDeg > 0.0) {
            const double longSweep = diff + (diff > 0 ? -2.0 * M_PI : 2.0 * M_PI);
            const double maxSweep = degreesToRadians(maxDeg);
            if (std::abs(longSweep) > maxSweep) {
                angle = startAngle + (longSweep >= 0 ? maxSweep : -maxSweep);
            }
        }
    }
    return geometry::polarPoint(center, arcRadius, angle);
}

/// A straight slot's two cap centers: the clicks, or for Overall the clicks
/// moved inward by the half width when the slot is long enough.
void capCenters(CreationMode m, const Point2D& p1, const Point2D& p2, double radius,
                Point2D& c1, Point2D& c2)
{
    c1 = p1;
    c2 = p2;
    if (m != CreationMode::SlotOverall) return;
    const double len = geometry::lineLength(p1, p2);
    if (len <= radius * 2.0) return;   // too short for the width: the ends as they are
    const Point2D dir = (p2 - p1) / len;
    c1 = p1 + dir * radius;
    c2 = p2 - dir * radius;
}

/// An Ends slot's center: on the chord's bisector at the picked point,
/// held half the placing floor off the chord (Aaron), then pushed out so
/// the ends stay apart.
Point2D endsCenter(const Point2D& start, const Point2D& end, const Point2D& picked,
                   double radius)
{
    Point2D center = picked;
    const geometry::ArcCenterFromChord ac = geometry::arcCenterOnBisector(
        start, end, picked, false, false, geometry::ChordFloor::MinPerpDistance,
        geometry::kChordPerpFloor / 2.0);
    if (ac.valid) center = ac.center;
    return enforceSlotArcSeparation(start, end, center, radius).center;
}

}  // namespace

Point2D slotCursor(CreationMode m, const PlacementInput& in)
{
    if (in.clicks.empty()) return in.cursor;
    const std::size_t placed = in.clicks.size();
    const Point2D first = in.clicks[0];
    switch (m) {
    case CreationMode::SlotArcRadius:
        if (placed == 1) {
            const std::optional<double> r = lengthLock(in, 0);
            if (r && geometry::length(in.cursor - first) > geometry::kDegenerateLen) {
                return geometry::applyPolarLock(first, in.cursor, r, std::nullopt);
            }
            return in.cursor;
        }
        return arcSlotEnd(in, first, in.clicks[1], in.cursor);
    case CreationMode::SlotArcEnds:
        if (placed >= 2) {
            if (const std::optional<double> sweep = lockAt(in, 0)) {
                return geometry::arcCenterFromChordAndSweep(first, in.clicks[1], in.cursor,
                                                            *sweep);
            }
        }
        return in.cursor;
    default: {
        const std::optional<double> len = lengthLock(in, 0);
        if (len && geometry::length(in.cursor - first) > geometry::kDegenerateLen) {
            return geometry::applyPolarLock(first, in.cursor, len, std::nullopt);
        }
        return in.cursor;
    }
    }
}

bool slotEntity(CreationMode m, const PlacementInput& in, Entity& out)
{
    if (in.clicks.empty()) return false;
    out.type = EntityType::Slot;
    out.radius = in.slotRadius;
    if (isArcSlot(m)) {
        // Stored as [arc center, start, end].
        if (in.clicks.size() < 2) return false;
        Point2D center, start, end;
        if (m == CreationMode::SlotArcRadius) {
            center = in.clicks[0];
            start = in.clicks[1];
            // The end on the arc the start set.
            end = pointAt(in, 2);
            const double arcRadius = geometry::lineLength(center, start);
            if (geometry::lineLength(center, end) > 0.001 && arcRadius > 0.001) {
                end = geometry::closestPointOnCircle(end, center, arcRadius);
            }
        } else {
            start = in.clicks[0];
            end = in.clicks[1];
            center = endsCenter(start, end, pointAt(in, 2), in.slotRadius);
        }
        out.points = {center, start, end};
        out.arcFlipped = in.flipped;
        return true;
    }
    // Stored as the two cap centers.
    const Point2D p1 = in.clicks[0];
    const Point2D p2 = pointAt(in, 1);
    Point2D c1, c2;
    capCenters(m, p1, p2, in.slotRadius, c1, c2);
    out.points = {c1, c2};
    return geometry::lineLength(p1, p2) > 0.1;
}

PlacementPreview slotPreview(CreationMode m, const PlacementInput& in)
{
    PlacementPreview out;
    if (in.clicks.empty()) return out;
    const double radius = halfWidth(in);
    const Point2D first = in.clicks[0];

    if (!isArcSlot(m)) {
        const Point2D p2 = in.cursor;
        Point2D c1, c2;
        capCenters(m, first, p2, radius, c1, c2);
        if (geometry::lineLength(c1, c2) <= 0.001) return out;
        Entity slot = createSlot(0, c1, c2, radius);
        out.shapes.push_back(path(tessellate(slot, 48), PreviewStroke::Pen, true));
        out.shapes.push_back(segment(c1, c2, PreviewStroke::Guide));
        out.shapes.push_back(mark(c1, PreviewMark::Click));
        out.shapes.push_back(mark(c2, PreviewMark::Click));
        if (m == CreationMode::SlotOverall) {
            out.shapes.push_back(mark(first, PreviewMark::Tip));
            out.shapes.push_back(mark(p2, PreviewMark::Tip));
        }
        // Overall measures end to end, center-to-center between the centers.
        const double length = m == CreationMode::SlotOverall ? geometry::lineLength(first, p2)
                                                             : geometry::lineLength(c1, c2);
        setValues(out, {length});
        if (length > 0.1) {
            out.shapes.push_back(dimension(c1, c2, length, 0, LabelPlace::Below,
                                           IdleLabel::Line));
        }
        return out;
    }

    const bool byRadius = m == CreationMode::SlotArcRadius;
    if (in.clicks.size() == 1) {
        // The first leg: the radius, or the chord.
        out.shapes.push_back(segment(first, in.cursor));
        out.shapes.push_back(mark(first, PreviewMark::Click));
        const double distance = geometry::lineLength(first, in.cursor);
        if (byRadius) setValues(out, {distance});
        if (distance > 0.1) {
            out.shapes.push_back(dimension(first, in.cursor, distance, byRadius ? 0 : -1,
                                           LabelPlace::Along, IdleLabel::Line));
        }
        out.shapes.push_back(noteAlong(first, in.cursor, byRadius
            ? HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas", "Click to place START point")
            : HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas", "Click to place END point")));
        return out;
    }

    Point2D center, start, end;
    if (byRadius) {
        center = first;
        start = in.clicks[1];
        end = in.cursor;   // already on the arc
    } else {
        start = first;
        end = in.clicks[1];
        center = in.cursor;
        const double chord = geometry::lineLength(start, end);
        if (chord > 0.001) {
            // The center on the bisector at the cursor.
            const Point2D mid = geometry::lineMidpoint(start, end);
            const Point2D perp = geometry::perpendicular(geometry::normalize(end - start));
            center = mid + perp * geometry::dot(in.cursor - mid, perp);
        }
    }
    // The center pushed out so the ends stay apart, as the commit does.
    const SlotArcCenter sa = enforceSlotArcSeparation(start, end, center, radius);
    center = sa.center;
    const double arcRadius = sa.radius;

    Entity slot = createArcSlot(0, center, start, end, radius, in.flipped);
    const double startDeg = angleDeg(center, start);
    const double sweep = geometry::wrapSweepDeg(angleDeg(center, end) - startDeg);
    const double shown = in.flipped ? geometry::oppositeSweepDeg(sweep) : sweep;
    const std::vector<Point2D> outline = arcSlotOutline(slot);
    if (!outline.empty()) {
        out.shapes.push_back(path(outline, PreviewStroke::Pen, true));
    } else {
        // Too tight for a slot: the centerline and the two ends' widths.
        out.shapes.push_back(path(arcPoints(center, arcRadius, startDeg, shown)));
        out.shapes.push_back(path(circlePoints(start, radius), PreviewStroke::Pen, true));
        out.shapes.push_back(path(circlePoints(end, radius), PreviewStroke::Pen, true));
    }
    out.shapes.push_back(mark(start, PreviewMark::Click));
    out.shapes.push_back(mark(end, PreviewMark::Click));
    out.shapes.push_back(mark(center, PreviewMark::Click));

    const double arcLength = arcRadius * std::abs(degreesToRadians(shown));
    setValues(out, {std::abs(shown)});
    if (arcLength > 0.1) {
        const Point2D outer = atAngle(center, arcRadius + radius, startDeg + shown / 2.0);
        out.shapes.push_back(arcLabel(center, outer, arcLength, shown, 0, IdleLabel::ArcValue,
                                      0, 20.0));
    }
    out.shapes.push_back(note(byRadius ? end : center, {
        byRadius ? HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas", "Click to place END point")
                 : HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas", "Click to place ARC CENTER"),
        HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas", "(Shift to flip arc direction)"),
    }, NoteStyle::Boxed, 15.0));
    return out;
}

}  // namespace placement_detail
}  // namespace sketch
}  // namespace hobbycad
