// =====================================================================
//  src/libhobbycad/sketch/placement_arc.cpp — placing an arc
// =====================================================================
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include "placement_detail.h"

#include <hobbycad/geometry/utils.h>
#include <hobbycad/sketch/handles.h>
#include <hobbycad/units.h>

#include <cmath>

namespace hobbycad {
namespace sketch {
namespace placement_detail {

namespace {

/// A circle's radius, a locked sweep and the like below this are nothing.
constexpr double kOnPath = 0.001;

/// Store an arc: center, start and end points, and the angle fields.
bool storeArc(Entity& e, const geometry::Arc& arc)
{
    e.type = EntityType::Arc;
    e.points.assign(3, arc.center);
    e.radius = arc.radius;
    e.startAngle = arc.startAngle;
    e.sweepAngle = arc.sweepAngle;
    resyncArcEndpoints(e);   // points[1], points[2] from the angles
    return arc.radius > 0.1;
}

/// The sweep from `start` to `end` about `center`, the short way unless
/// `longWay`.
double sweepBetween(const Point2D& center, const Point2D& start, const Point2D& end,
                    bool longWay)
{
    double sweep = geometry::wrapSweepDeg(angleDeg(center, end) - angleDeg(center, start));
    if (longWay) sweep = geometry::oppositeSweepDeg(sweep);
    return sweep;
}

/// The start-end arc's center, radius and sweep: the center on the chord's
/// bisector at the picked point, the long way when the center is nearer the
/// chord than half its length.
struct ChordArc {
    bool valid = false;
    Point2D center;
    double radius = 0.0;
    double sweep = 0.0;
};

ChordArc chordArc(const Point2D& start, const Point2D& end, const Point2D& picked,
                  bool semicircle, bool flipped)
{
    ChordArc a;
    const double chord = geometry::lineLength(start, end);
    if (chord <= kOnPath) return a;
    const geometry::ArcCenterFromChord c =
        geometry::arcCenterOnBisector(start, end, picked, semicircle, flipped);
    a.valid = true;
    a.center = c.center;
    a.radius = c.radius;
    // The side of the chord decides the direction; how near the center is
    // decides short or long.
    a.sweep = sweepBetween(c.center, start, end, std::abs(c.projection) < chord / 2.0);
    if (semicircle) a.sweep = a.sweep > 0 ? 180.0 : -180.0;
    return a;
}

/// The edge of the tangent arc's host at its start.
geometry::TangentArcResult tangentArc(const Entity& host, const Point2D& start,
                                      const Point2D& end)
{
    Point2D a, b;
    if (!closestTangentHostEdge(host, start, a, b)) return {};
    return geometry::arcTangentToLine(a, b, start, end);
}

/// Center of a tangent arc of `radius`: off the host's edge at the tangent
/// point, on `toward`'s side.
Point2D lockedRadiusCenter(const Entity& host, const Point2D& tangentPoint,
                           const Point2D& toward, double radius)
{
    const Point2D normal =
        geometry::perpendicular(geometry::normalize(tangentHostEdgeDirAt(host, tangentPoint)));
    const Point2D c1 = tangentPoint + normal * radius;
    const Point2D c2 = tangentPoint - normal * radius;
    return geometry::lineLength(toward, c1) < geometry::lineLength(toward, c2) ? c1 : c2;
}

/// The two lines of the note under an arc being placed.
PreviewShape arcNote(const Point2D& center, double radius, std::vector<const char*> lines,
                     double gap)
{
    return note(center - Point2D(0.0, radius), std::move(lines), NoteStyle::Plain, gap);
}

}  // namespace

Point2D arcCursor(CreationMode m, const PlacementInput& in, const Point2D& raw, bool keepSnap)
{
    if (in.clicks.empty()) return in.cursor;
    const std::size_t placed = in.clicks.size();
    Point2D c = in.cursor;

    switch (m) {
    case CreationMode::ArcCenterStartEnd: {
        const Point2D center = in.clicks[0];
        if (placed == 1) {
            const std::optional<double> r = lengthLock(in, 0);
            if (r && geometry::length(c - center) > geometry::kDegenerateLen) {
                c = geometry::applyPolarLock(center, c, r, std::nullopt);
            }
            return c;
        }
        // The end rides the circle the start set, or sits at the locked sweep.
        const Point2D start = in.clicks[1];
        if (const std::optional<double> sweep = lockAt(in, 0)) {
            return geometry::pointAtLockedSweep(center, start, c, *sweep, in.flipped);
        }
        const double radius = geometry::lineLength(center, start);
        return atAngle(center, radius, angleDeg(center, c));
    }

    case CreationMode::ArcStartEndRadius: {
        const Point2D start = in.clicks[0];
        if (placed == 1) {
            const std::optional<double> len = lengthLock(in, 0);
            const std::optional<double> ang = lockAt(in, 1);
            return (len || ang) ? geometry::applyPolarLock(start, c, len, ang) : c;
        }
        // The center, at the locked sweep on the cursor's side of the chord.
        if (const std::optional<double> sweep = lockAt(in, 0)) {
            return geometry::arcCenterFromChordAndSweep(start, in.clicks[1], c, *sweep);
        }
        return c;
    }

    case CreationMode::ArcTangent: {
        if (in.targets.empty()) return c;
        const Entity& host = in.targets[0];
        const Point2D tangentPoint = projectOntoTangentHost(host, in.clicks[0]);

        // The end rides the arc the cursor implies. A snap already on that
        // arc is kept; otherwise the RAW cursor is projected, since solving
        // from the snapped one would compound the two.
        const geometry::TangentArcResult ta = tangentArc(host, tangentPoint, c);
        if (ta.valid && ta.radius > kOnPath) {
            const bool onArc = std::abs(geometry::lineLength(c, ta.center) - ta.radius) < kOnPath;
            if (!(onArc && keepSnap)) {
                const geometry::TangentArcResult rawTa = tangentArc(host, tangentPoint, raw);
                if (rawTa.valid && rawTa.radius > kOnPath) {
                    const double dist = geometry::lineLength(raw, rawTa.center);
                    if (dist > kOnPath) {
                        c = rawTa.center + (raw - rawTa.center) * (rawTa.radius / dist);
                    }
                }
            }
        }

        const std::optional<double> radius = lengthLock(in, 0);
        const std::optional<double> sweep = lockAt(in, 1);
        if (radius && sweep) {
            // Both: the center from the radius, the end at the sweep.
            const Point2D center = lockedRadiusCenter(host, tangentPoint, c, *radius);
            const double startDeg = angleDeg(center, tangentPoint);
            double sign = sweepBetween(center, tangentPoint, c, false) > 0 ? 1.0 : -1.0;
            if (in.flipped) sign = -sign;
            return atAngle(center, *radius, startDeg + sign * std::abs(*sweep));
        }
        if (radius) {
            const Point2D center = lockedRadiusCenter(host, tangentPoint, c, *radius);
            const double dist = geometry::lineLength(c, center);
            return dist > geometry::kDegenerateLen ? center + (c - center) * (*radius / dist) : c;
        }
        if (sweep) {
            const geometry::TangentArcResult cur = tangentArc(host, tangentPoint, c);
            if (cur.valid) {
                double sign = cur.sweepAngle >= 0 ? 1.0 : -1.0;
                if (in.flipped) sign = -sign;
                return atAngle(cur.center, cur.radius, cur.startAngle + sign * std::abs(*sweep));
            }
        }
        return c;
    }

    default:
        return c;   // the 3-point arc takes the cursor as it is
    }
}

bool arcEntity(CreationMode m, const PlacementInput& in, Entity& out)
{
    if (in.clicks.empty()) return false;
    switch (m) {
    case CreationMode::ArcCenterStartEnd: {
        if (in.clicks.size() < 2) return false;
        const Point2D center = in.clicks[0];
        const Point2D start = in.clicks[1];
        const Point2D end = pointAt(in, 2);
        const bool ccw = sweepBetween(center, start, end, in.flipped) > 0;
        return storeArc(out, geometry::arcFromCenterAndEndpoints(center, start, end, ccw));
    }
    case CreationMode::ArcStartEndRadius: {
        if (in.clicks.size() < 2) return false;
        const Point2D start = in.clicks[0];
        const Point2D end = in.clicks[1];
        const ChordArc a = chordArc(start, end, pointAt(in, 2), in.semicircle, in.flipped);
        if (!a.valid) return false;
        return storeArc(out, geometry::arcFromCenterAndEndpoints(a.center, start, end,
                                                                 a.sweep > 0));
    }
    case CreationMode::ArcTangent: {
        if (in.targets.empty()) return false;
        const Entity& host = in.targets[0];
        const Point2D tangentPoint = projectOntoTangentHost(host, in.clicks[0]);
        const geometry::TangentArcResult ta = tangentArc(host, tangentPoint, pointAt(in, 1));
        if (!ta.valid) return false;
        geometry::Arc arc;
        arc.center = ta.center;
        arc.radius = ta.radius;
        arc.startAngle = ta.startAngle;
        arc.sweepAngle = in.flipped ? geometry::oppositeSweepDeg(ta.sweepAngle) : ta.sweepAngle;
        storeArc(out, arc);   // the front end records the host as tangentEntityId
        return true;
    }
    default: {
        // Start, end, then a point it passes through, as the prompts ask.
        if (in.clicks.size() < 2) return false;
        const auto arc = geometry::arcFromThreePoints(in.clicks[0], pointAt(in, 2), in.clicks[1]);
        return arc && storeArc(out, *arc);
    }
    }
}

PlacementPreview arcPreview(CreationMode m, const PlacementInput& in)
{
    PlacementPreview out;
    if (in.clicks.empty()) return out;
    const std::size_t placed = in.clicks.size();

    switch (m) {
    case CreationMode::ArcCenterStartEnd: {
        const Point2D center = in.clicks[0];
        if (placed == 1) {
            out.shapes.push_back(segment(center, in.cursor));
            out.shapes.push_back(mark(center, PreviewMark::Center));
            const double r = geometry::lineLength(center, in.cursor);
            setValues(out, {r});
            if (r > 0.1) {
                out.shapes.push_back(dimension(center, in.cursor, r, 0, LabelPlace::Along,
                                               IdleLabel::ValueAside));
            }
            return out;
        }
        const Point2D start = in.clicks[1];
        const Point2D end = in.cursor;
        const double r = geometry::lineLength(center, start);
        const double startDeg = angleDeg(center, start);
        const double sweep = sweepBetween(center, start, end, in.flipped);
        out.shapes.push_back(path(arcPoints(center, r, startDeg, sweep)));
        out.shapes.push_back(mark(center, PreviewMark::Center));
        out.shapes.push_back(mark(start, PreviewMark::Click));
        out.shapes.push_back(mark(end, PreviewMark::Click));
        const double arcLength = r * std::abs(degreesToRadians(sweep));
        setValues(out, {std::abs(sweep)});
        if (arcLength > 0.1) {
            out.shapes.push_back(arcLabel(center, atAngle(center, r, startDeg + sweep / 2.0),
                                          arcLength, sweep, 0, IdleLabel::ArcValue, 0, 20.0));
        }
        out.shapes.push_back(arcNote(center, r, {
            HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas",
                                    "Arc: Center \xE2\x86\x92 Start \xE2\x86\x92 End"),
            HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas", "(Shift to flip arc direction)"),
        }, 30.0));
        return out;
    }

    case CreationMode::ArcStartEndRadius: {
        const Point2D start = in.clicks[0];
        if (placed == 1) {
            out.shapes.push_back(segment(start, in.cursor));
            out.shapes.push_back(mark(start, PreviewMark::Click));
            const double chord = geometry::lineLength(start, in.cursor);
            setValues(out, {chord, angleDeg(start, in.cursor)});
            if (chord > 0.1) {
                out.shapes.push_back(dimension(start, in.cursor, chord, 0, LabelPlace::Along,
                                               IdleLabel::ValueAside));
                out.shapes.push_back(dimension(start, in.cursor, out.fieldValues[1], 1,
                                               LabelPlace::Along, IdleLabel::None, 1));
            }
            return out;
        }
        const Point2D end = in.clicks[1];
        const ChordArc a = chordArc(start, end, in.cursor, in.semicircle, in.flipped);
        if (!a.valid) return out;
        const double startDeg = angleDeg(a.center, start);
        out.shapes.push_back(path(arcPoints(a.center, a.radius, startDeg, a.sweep)));
        // The center is where the cursor is held; Ctrl's exact half circle
        // shows larger.
        out.shapes.push_back(mark(a.center, in.semicircle ? PreviewMark::Snapped
                                                          : PreviewMark::Center));
        out.shapes.push_back(mark(start, PreviewMark::BigClick));
        out.shapes.push_back(mark(end, PreviewMark::BigClick));
        const double arcLength = a.radius * std::abs(degreesToRadians(a.sweep));
        setValues(out, {std::abs(a.sweep)});
        if (arcLength > 0.1) {
            out.shapes.push_back(arcLabel(a.center,
                                          atAngle(a.center, a.radius, startDeg + a.sweep / 2.0),
                                          arcLength, a.sweep, 0, IdleLabel::ArcValue, 0, 25.0));
        }
        out.shapes.push_back(arcNote(a.center, a.radius, {
            HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas",
                                    "Arc: Start \xE2\x86\x92 End \xE2\x86\x92 Center"),
            HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas",
                                    "(Shift to flip, Ctrl for 180\xC2\xB0)"),
        }, 60.0));
        return out;
    }

    case CreationMode::ArcTangent: {
        if (in.targets.empty()) {
            out.shapes.push_back(segment(in.clicks[0], in.cursor));
            return out;
        }
        const Entity& host = in.targets[0];
        const Point2D tangentPoint = projectOntoTangentHost(host, in.clicks[0]);
        const geometry::TangentArcResult ta = tangentArc(host, tangentPoint, in.cursor);
        if (!ta.valid) {
            out.shapes.push_back(segment(tangentPoint, in.cursor));
            out.shapes.push_back(mark(tangentPoint, PreviewMark::Click));
            return out;
        }
        const double sweep =
            in.flipped ? geometry::oppositeSweepDeg(ta.sweepAngle) : ta.sweepAngle;
        out.shapes.push_back(path(arcPoints(ta.center, ta.radius, ta.startAngle, sweep)));
        out.shapes.push_back(mark(ta.center, PreviewMark::Center));
        out.shapes.push_back(mark(tangentPoint, PreviewMark::Click));
        out.shapes.push_back(mark(in.cursor, PreviewMark::Click));
        const double arcLength = ta.radius * std::abs(degreesToRadians(sweep));
        setValues(out, {ta.radius, std::abs(sweep)});
        if (arcLength > 0.1) {
            const Point2D middle = atAngle(ta.center, ta.radius, ta.startAngle + sweep / 2.0);
            out.shapes.push_back(arcLabel(ta.center, middle, arcLength, sweep, 0,
                                          IdleLabel::ArcValue, 0, 20.0));
            out.shapes.push_back(arcLabel(ta.center, middle, arcLength, sweep, 1,
                                          IdleLabel::None, 1, 20.0));
        }
        out.shapes.push_back(arcNote(ta.center, ta.radius, {
            HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas", "(Shift to flip arc direction)"),
        }, 30.0));
        return out;
    }

    default: {
        for (const Point2D& p : in.clicks) out.shapes.push_back(mark(p, PreviewMark::Click));
        if (placed == 1) {
            out.shapes.push_back(segment(in.clicks[0], in.cursor, PreviewStroke::Guide));
            return out;
        }
        const Point2D p0 = in.clicks[0];
        const Point2D p1 = in.clicks[1];
        const auto arc = geometry::arcFromThreePoints(p0, in.cursor, p1);
        if (!arc) {
            // In a line: no arc, the three points joined.
            out.shapes.push_back(path({p0, in.cursor, p1}, PreviewStroke::Guide));
            return out;
        }
        out.shapes.push_back(path(arcPoints(arc->center, arc->radius, arc->startAngle,
                                            arc->sweepAngle)));
        out.shapes.push_back(mark(arc->center, PreviewMark::Center));
        out.shapes.push_back(mark(in.cursor, PreviewMark::Cursor));
        const double arcLength = geometry::arcLength(*arc);
        if (arcLength > 0.1) {
            const Point2D middle = atAngle(arc->center, arc->radius,
                                           arc->startAngle + arc->sweepAngle / 2.0);
            out.shapes.push_back(arcLabel(arc->center, middle, arcLength, arc->sweepAngle, -1,
                                          IdleLabel::ArcValue, 0, 20.0));
        }
        return out;
    }
    }
}

}  // namespace placement_detail
}  // namespace sketch
}  // namespace hobbycad
