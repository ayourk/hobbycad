// =====================================================================
//  src/libhobbycad/sketch/handles.cpp — Entity handle dragging
// =====================================================================

#include "../hobbycad/sketch/handles.h"
#include <hobbycad/units.h>
#include "../hobbycad/geometry/utils.h"
#include "../hobbycad/geometry/intersections.h"

#include <algorithm>
#include <cmath>

#include <hobbycad/math_constants.h>

namespace hobbycad {
namespace sketch {

namespace {

constexpr double kEps      = geometry::kDegenerateLen;
constexpr double kEpsSmall = geometry::kZeroEps;


/// Point on a circle at the given angle (radians).
inline Point2D onCircle(const Point2D& c, double radius, double angle)
{
    return {c.x + radius * std::cos(angle), c.y + radius * std::sin(angle)};
}

/// Angle of (p - c) in radians.
inline double angleOf(const Point2D& c, const Point2D& p)
{
    return std::atan2(p.y - c.y, p.x - c.x);
}

/// Move `p` so it lies at `radius` from `c`, keeping its direction.
/// Returns false when p coincides with c and no direction exists.
bool projectToRadius(Point2D& p, const Point2D& c, double radius)
{
    const Point2D dir = p - c;
    const double len = geometry::length(dir);
    if (len <= kEps) return false;
    p = c + dir * (radius / len);
    return true;
}

/// Translate every point of the entity by `delta`.
void translateAll(Entity& e, const Point2D& delta)
{
    for (Point3& p : e.points) p += delta;
}

// -----------------------------------------------------------------
//  Circle
// -----------------------------------------------------------------
void dragCircle(Entity& e, int h, const Point2D& target,
                const HandleDragLocks& locks, HandleDragResult& out)
{
    if (h == 0) {
        translateAll(e, target - e.points[0]);
        return;
    }

    const std::size_t n = e.points.size();

    if (n == 3 && h >= 1) {
        // 2-point (diameter) circle: [center, p1, p2].
        // Keep the other diameter endpoint fixed, recompute the center.
        const int otherIdx = (h == 1) ? 2 : 1;
        const Point2D pOther = e.points[otherIdx];

        Point2D newDragPt = target;
        if (locks.radius > 0.0) {
            // Dimension locked: rotation only, diameter fixed.
            const Point2D dir = target - pOther;
            const double len = geometry::length(dir);
            if (len > kEps) {
                newDragPt = pOther + dir * (locks.radius * 2.0 / len);
            }
        }

        e.points[h] = newDragPt;
        e.points[0] = (newDragPt + pOther) / 2.0;   // center = midpoint
        e.radius    = geometry::length(newDragPt - pOther) / 2.0;
        out.diameterRotation = true;
        return;
    }

    if (n == 4 && h >= 1) {
        // 3-point circle: [center, p1, p2, p3].
        if (locks.radius > 0.0) {
            // Radius locked: the two untouched points stay put, so the
            // center must lie on their perpendicular bisector.
            int idx[2] = {0, 0};
            int k = 0;
            for (int i = 1; i <= 3; ++i) {
                if (i != h && k < 2) idx[k++] = i;
            }
            if (k < 2) return;

            const Point2D A = e.points[idx[0]];
            const Point2D B = e.points[idx[1]];
            const Point2D mid = (A + B) / 2.0;
            const Point2D ab = B - A;
            const double halfChord = geometry::length(ab) / 2.0;
            if (halfChord > locks.radius) {
                // Fixed points too far apart for the locked radius.
                return;
            }

            Point2D perp(-ab.y, ab.x);
            const double perpLen = geometry::length(perp);
            if (perpLen <= kEpsSmall) return;
            perp /= perpLen;

            const double d = std::sqrt(locks.radius * locks.radius
                                       - halfChord * halfChord);
            const Point2D c1 = mid + perp * d;
            const Point2D c2 = mid - perp * d;
            // Pick the center nearer the cursor.
            const Point2D center =
                (geometry::length(c1 - target) <= geometry::length(c2 - target))
                    ? c1 : c2;

            Point2D dragged = target;
            if (projectToRadius(dragged, center, locks.radius)) {
                e.points[h] = dragged;
            }
            e.points[0] = center;
            e.radius    = locks.radius;
            return;
        }

        // Unlocked: recompute the circumcircle through the three points.
        e.points[h] = target;
        const auto arc = geometry::arcFromThreePoints(
            e.points[1], e.points[2], e.points[3]);
        if (arc.has_value() && arc->radius > 0.1) {
            e.points[0] = arc->center;
            e.radius    = arc->radius;
        }
        return;
    }

    if (h >= 1) {
        // Center-radius circle: center fixed, radius follows the handle,
        // any other perimeter points ride along.
        e.points[h] = target;
        e.radius    = geometry::length(target - e.points[0]);
        const Point2D center = e.points[0];
        for (std::size_t i = 1; i < n; ++i) {
            if (static_cast<int>(i) == h) continue;
            Point2D p = e.points[i];
            if (projectToRadius(p, center, e.radius)) e.points[i] = p;
        }
    }
}

// -----------------------------------------------------------------
//  Arc — points: [center, start, end]
// -----------------------------------------------------------------
/// Re-impose a locked sweep after any arc handle move but the center's.
void enforceLockedSweep(Entity& e, int h, const HandleDragLocks& locks)
{
    if (h == 0 || locks.sweepAngle < 0.0) return;
    e.sweepAngle = (e.sweepAngle >= 0.0) ? locks.sweepAngle : -locks.sweepAngle;
    e.points[2] = onCircle(e.points[0], e.radius, degreesToRadians(e.startAngle + e.sweepAngle));
}

void dragArc(Entity& e, int h, const Point2D& target,
             const HandleDragLocks& locks)
{
    const Point2D center = e.points[0];

    if (h == 0) {
        const Point2D delta = target - center;
        e.points[0] = target;
        e.points[1] += delta;
        e.points[2] += delta;
    } else if (h == 1) {
        // Start endpoint: slide along the arc, adjusting the angle.
        Point2D p = target;
        if (projectToRadius(p, center, e.radius)) {
            e.points[1] = p;
            const double startAngle = radiansToDegrees(angleOf(center, e.points[1]));
            const double endAngle   = radiansToDegrees(angleOf(center, e.points[2]));
            double sweep = endAngle - startAngle;
            if (e.sweepAngle >= 0.0) { while (sweep < 0.0) sweep += 360.0; }
            else                     { while (sweep > 0.0) sweep -= 360.0; }
            e.startAngle = startAngle;
            e.sweepAngle = sweep;
        }
    } else if (h == 2) {
        // End endpoint: free drag, resizing the radius.
        e.points[2] = target;
        const double newRadius = geometry::length(target - center);
        if (newRadius > kEps) {
            e.points[1] = onCircle(center, newRadius, degreesToRadians(e.startAngle));
            e.radius    = newRadius;
            double sweep = radiansToDegrees(angleOf(center, target)) - e.startAngle;
            if (e.sweepAngle >= 0.0) { while (sweep < 0.0) sweep += 360.0; }
            else                     { while (sweep > 0.0) sweep -= 360.0; }
            e.sweepAngle = sweep;
        }
    }

    enforceLockedSweep(e, h, locks);
}

// -----------------------------------------------------------------
//  Arc slot — points: [arc center, start, end], radius = half width
// -----------------------------------------------------------------

/// Clamp the slot sweep to the furthest the slot may sweep at all
/// (absoluteMaxArcSlotSweepDegrees: the caps may overlap and free the
/// center piece; the same limit the slot tool and the CLI use), moving the
/// start point back onto the arc when the limit is exceeded.
void clampSlotSweep(Entity& e, const Point2D& center, double arcRadius)
{
    if (arcRadius <= kEps) return;
    const double maxSweep = degreesToRadians(absoluteMaxArcSlotSweepDegrees(arcRadius, e.radius));

    const double startAng = angleOf(center, e.points[1]);
    const double endAng   = angleOf(center, e.points[2]);
    double sweep = endAng - startAng;

    if (e.arcFlipped) {
        if (sweep > 0.0) sweep -= 2.0 * M_PI;
        else             sweep += 2.0 * M_PI;
    } else {
        while (sweep >  M_PI) sweep -= 2.0 * M_PI;
        while (sweep < -M_PI) sweep += 2.0 * M_PI;
    }

    if (std::abs(sweep) > maxSweep) {
        const double clamped = (sweep > 0.0) ? maxSweep : -maxSweep;
        e.points[1] = onCircle(center, arcRadius, endAng - clamped);
    }
}

void dragSlot(Entity& e, int h, const Point2D& target,
              const HandleDragLocks& locks)
{
    if (h == 0) {
        const Point2D delta = target - e.points[0];
        e.points[0] = target;
        e.points[1] += delta;
        e.points[2] += delta;
        return;
    }

    const Point2D center = e.points[0];
    const int otherIdx = (h == 1) ? 2 : 1;

    if (locks.slotEndMode == HandleDragLocks::SlotEndMode::ResizeAboutOther) {
        // Keep the other end fixed; the center goes to the old center's
        // projection on the new chord's bisector, at least 0.1 off the chord.
        const geometry::ArcCenterFromChord ac = geometry::arcCenterOnBisector(
            target, e.points[otherIdx], center, false, false,
            geometry::ChordFloor::MinPerpDistance, 0.1);
        e.points[h] = target;
        if (ac.valid) e.points[0] = ac.center;
        return;
    }

    if (locks.slotEndMode == HandleDragLocks::SlotEndMode::FreeResize) {
        // The end goes where the drag says; the arc radius is its distance
        // from the old center, and the center moves onto the new chord's
        // bisector to realize that radius, on the side it was.
        const Point2D otherPt = e.points[otherIdx];
        const Point2D chordMid = (target + otherPt) / 2.0;
        const Point2D chordDir = otherPt - target;
        const double chordLen = geometry::length(chordDir);
        e.points[h] = target;
        if (chordLen > kEps) {
            const Point2D perpDir = geometry::perpendicular(geometry::normalize(chordDir));
            const double newRadius = geometry::length(target - center);
            const double halfChord = chordLen / 2.0;
            const double perpDistSq = newRadius * newRadius - halfChord * halfChord;
            if (perpDistSq > 0.0) {
                double perpDist = std::sqrt(perpDistSq);
                if (geometry::dot(center - chordMid, perpDir) < 0.0) perpDist = -perpDist;
                e.points[0] = chordMid + perpDir * perpDist;
            }
        }
        return;
    }

    if ((locks.fixedHandleIndex == 1 || locks.fixedHandleIndex == 2) && locks.fixedHandleIndex != h) {
        // A pinned end: resize about it, snapping the dragged end's angle
        // about the new center when asked (and re-solving for that chord).
        const Point2D fixedPt = e.points[locks.fixedHandleIndex];
        Point2D dragged = target;
        geometry::ArcCenterFromChord ac = geometry::arcCenterOnBisector(
            dragged, fixedPt, center, false, false,
            geometry::ChordFloor::MinPerpDistance, 0.1);
        if (ac.valid) {
            if (locks.angleSnapIncrement > 0.0) {
                double angle = angleOf(ac.center, dragged);
                angle = std::round(angle / locks.angleSnapIncrement) * locks.angleSnapIncrement;
                dragged = onCircle(ac.center, geometry::length(fixedPt - ac.center), angle);
                const geometry::ArcCenterFromChord ac2 = geometry::arcCenterOnBisector(
                    dragged, fixedPt, center, false, false,
                    geometry::ChordFloor::MinPerpDistance, 0.1);
                if (ac2.valid) ac = ac2;
            }
            e.points[h] = dragged;
            e.points[0] = ac.center;
        } else {
            e.points[h] = target;
        }
        return;
    }

    if (h == 1) {
        // Start handle: slide along the arc to adjust the angle.
        const double arcRadius = geometry::length(e.points[2] - center);
        const Point2D dir = target - center;
        if (geometry::length(dir) > kEps && arcRadius > kEps) {
            double angle = std::atan2(dir.y, dir.x);
            if (locks.angleSnapIncrement > 0.0) {
                angle = std::round(angle / locks.angleSnapIncrement)
                        * locks.angleSnapIncrement;
            }
            e.points[1] = onCircle(center, arcRadius, angle);
            clampSlotSweep(e, center, arcRadius);
        } else {
            e.points[1] = target;
        }
        return;
    }

    if (h == 2) {
        // End handle: free drag, resizing the arc radius.  Both ends are
        // re-projected so they share the new radius.
        const double newArcRadius = geometry::length(target - center);
        if (newArcRadius <= kEps) return;

        Point2D pEnd = target;
        if (projectToRadius(pEnd, center, newArcRadius)) e.points[2] = pEnd;
        Point2D pStart = e.points[1];
        if (projectToRadius(pStart, center, newArcRadius)) e.points[1] = pStart;

        clampSlotSweep(e, center, newArcRadius);
    }
}

}  // namespace

// =====================================================================
//  dragEntityHandle
// =====================================================================


// -----------------------------------------------------------------
//  Tangent arc — points: [center, tangent point on host, end]
// -----------------------------------------------------------------

/// The host's unit normal at `tangentPt`, on the side the arc's center is
/// on today. False when the host has no edge direction there.
bool hostNormalTowardCenter(const Entity& host, const Point2D& tangentPt,
                            const Point2D& currentCenter, const Point2D& currentTangentPt,
                            Point2D& normal)
{
    const Point2D edgeDir = tangentHostEdgeDirAt(host, tangentPt);
    if (geometry::length(edgeDir) <= kEps) return false;
    normal = geometry::perpendicular(geometry::normalize(edgeDir));
    if (geometry::dot(currentCenter - currentTangentPt, normal) < 0.0) normal = -normal;
    return true;
}

/// Slide the arc along its host: the tangent point moves to `tangentPt`,
/// the center follows on the host normal at `radius`, sweep preserved.
void slideTangentArc(Entity& e, const Entity& host, const Point2D& tangentPt, double radius)
{
    Point2D normal;
    if (!hostNormalTowardCenter(host, tangentPt, e.points[0], e.points[1], normal)) return;
    const Point2D newCenter = tangentPt + normal * radius;
    const double startAngle = radiansToDegrees(angleOf(newCenter, tangentPt));
    e.points[0] = newCenter;
    e.points[1] = tangentPt;
    e.points[2] = onCircle(newCenter, radius, degreesToRadians(startAngle + e.sweepAngle));
    e.radius = radius;
    e.startAngle = startAngle;
}

void dragTangentArc(Entity& e, int h, const Point2D& target, const HandleDragLocks& locks)
{
    const Entity& host = *locks.tangentHost;
    const Point2D center = e.points[0];
    const Point2D currentTan = e.points[1];

    if (h == 0) {
        // Center: slide the tangent point along the host, keep radius and sweep.
        slideTangentArc(e, host, projectOntoTangentHost(host, target), e.radius);
        return;
    }

    if (h == 1) {
        const Point2D tangentPt = projectOntoTangentHost(host, target);
        if (locks.radius > 0.0) {
            // Locked radius: same slide as the center handle, at that radius.
            slideTangentArc(e, host, tangentPt, locks.radius);
            return;
        }
        // Free: keep the end where it is and re-derive the arc through it.
        const Point2D endPt = e.points[2];
        Point2D c; double r, startDeg, sweepDeg;
        if (solveTangentArcPreservingSide(host, tangentPt, endPt, center, currentTan, e.sweepAngle,
                                          c, r, startDeg, sweepDeg)) {
            e.points[0] = c;
            e.points[1] = tangentPt;
            e.points[2] = endPt;      // exact, no cos/sin drift
            e.radius = r; e.startAngle = startDeg; e.sweepAngle = sweepDeg;
        }
        return;
    }

    // End handle: re-project the tangent point, move the end.
    const Point2D tangentPt = projectOntoTangentHost(host, currentTan);
    if (locks.radius > 0.0) {
        // Locked radius: the center is fixed by the tangent point and the
        // host normal; the end is projected onto that circle, so only the
        // sweep changes.
        Point2D normal;
        if (!hostNormalTowardCenter(host, tangentPt, center, currentTan, normal)) return;
        const Point2D newCenter = tangentPt + normal * locks.radius;
        Point2D projEnd = target;
        if (!projectToRadius(projEnd, newCenter, locks.radius)) return;
        const double startDeg = radiansToDegrees(angleOf(newCenter, tangentPt));
        const double endDeg = radiansToDegrees(angleOf(newCenter, projEnd));
        double sweep = geometry::wrapSweepDeg(endDeg - startDeg);
        if (e.sweepAngle >= 0.0 && sweep < 0.0) sweep += 360.0;
        else if (e.sweepAngle < 0.0 && sweep > 0.0) sweep -= 360.0;
        e.points[0] = newCenter;
        e.points[1] = tangentPt;
        e.points[2] = projEnd;
        e.radius = locks.radius; e.startAngle = startDeg; e.sweepAngle = sweep;
        return;
    }
    Point2D c; double r, startDeg, sweepDeg;
    if (solveTangentArcPreservingSide(host, tangentPt, target, center, currentTan, e.sweepAngle,
                                      c, r, startDeg, sweepDeg)) {
        e.points[0] = c;
        e.points[1] = tangentPt;
        e.points[2] = onCircle(c, r, degreesToRadians(startDeg + sweepDeg));
        e.radius = r; e.startAngle = startDeg; e.sweepAngle = sweepDeg;
    }
}

// -----------------------------------------------------------------
//  Bezier spline — control polygon [P0, out0, in1, P1, ...]
// -----------------------------------------------------------------

/// An anchor (index 3k) drags with both its handles; a handle keeps the
/// node smooth by turning the opposite leg to point the other way, at the
/// length that leg had.
void dragBezier(Entity& e, int i, const Point2D& target)
{
    const int nn = static_cast<int>(e.points.size());
    const Point2D delta = target - e.points[i];
    if (i % 3 == 0) {
        e.points[i] = target;
        if (i - 1 >= 0) e.points[i - 1] += delta;
        if (i + 1 < nn) e.points[i + 1] += delta;
        return;
    }
    e.points[i] = target;
    const int anchorIdx = (i % 3 == 1) ? i - 1 : i + 1;
    const int oppIdx    = (i % 3 == 1) ? i - 2 : i + 2;
    if (anchorIdx < 0 || anchorIdx >= nn || oppIdx < 0 || oppIdx >= nn) return;
    const Point2D A = e.points[anchorIdx];
    const Point2D vThis = target - A;
    const double lThis = geometry::length(vThis);
    const double lOpp = geometry::length(Point2D(e.points[oppIdx]) - A);
    if (lThis > kEpsSmall) e.points[oppIdx] = A - (vThis / lThis) * lOpp;
}

HandleDragResult dragEntityHandle(Entity& entity, int handleIndex,
                                  const Point2D& target,
                                  const HandleDragLocks& locks)
{
    HandleDragResult out;

    if (handleIndex < 0
        || handleIndex >= static_cast<int>(entity.points.size())) {
        return out;
    }

    const bool hasCenter = !entity.points.empty();
    out.oldCenter = hasCenter ? entity.points[0] : Point3{};
    out.previousHandlePos = entity.points[handleIndex];

    // A type whose point count is too small for its own layout falls
    // back to the plain "move that point" behavior, matching the
    // original if/else chain where a failed size guard fell through.
    const std::size_t np = entity.points.size();
    bool handled = true;

    switch (entity.type) {
    case EntityType::Circle:
        dragCircle(entity, handleIndex, target, locks, out);
        break;

    case EntityType::Arc:
        if (np < 3) { handled = false; break; }
        if (locks.tangentHost && handleIndex <= 2) {
            dragTangentArc(entity, handleIndex, target, locks);
            enforceLockedSweep(entity, handleIndex, locks);
        } else {
            dragArc(entity, handleIndex, target, locks);
        }
        break;

    case EntityType::Slot:
        if (np < 3) { handled = false; break; }
        dragSlot(entity, handleIndex, target, locks);
        break;

    case EntityType::Polygon:
        if (np < 2) { handled = false; break; }
        if (handleIndex == 0) {
            const Point2D delta = target - entity.points[0];
            entity.points[0] = target;
            entity.points[1] += delta;
        } else if (handleIndex == 1) {
            entity.points[1] = target;
            entity.radius = geometry::length(entity.points[1] - entity.points[0]);
        }
        break;

    case EntityType::Ellipse:
        if (np < 2) { handled = false; break; }
        if (handleIndex == 0) {
            const Point2D delta = target - entity.points[0];
            entity.points[0] = target;
            entity.points[1] += delta;
        } else if (handleIndex == 1) {
            const double oldMajor = entity.majorRadius;
            entity.points[1] = target;
            entity.majorRadius = geometry::length(target - entity.points[0]);
            if (oldMajor > kEps) {
                entity.minorRadius *= entity.majorRadius / oldMajor;
            }
        }
        break;

    case EntityType::Parallelogram:
        if (np < 4) { handled = false; break; }
        entity.points[handleIndex] = target;
        // p4 is dependent: p1 + (p3 - p2)
        entity.points[3] = entity.points[0]
                         + (entity.points[2] - entity.points[1]);
        break;

    case EntityType::Spline:
        if (!entity.splineBezier) { handled = false; break; }
        dragBezier(entity, handleIndex, target);
        break;

    case EntityType::Text:
        if (np < 2) { handled = false; break; }
        if (handleIndex == 0) {
            const Point2D delta = target - entity.points[0];
            entity.points[0] += delta;
            entity.points[1] += delta;
        } else if (handleIndex == 1) {
            const Point2D anchor = entity.points[0];
            const double angle = radiansToDegrees(angleOf(anchor, target));
            entity.textRotation = angle;
            const double dist = std::max(
                entity.fontSize * 2.0,
                entity.fontSize
                    * static_cast<double>(entity.text.length()) * 0.6);
            entity.points[1] = onCircle(anchor, dist, degreesToRadians(angle));
        }
        break;

    default:
        // Lines, rectangles, splines, points: the handle simply moves.
        handled = false;
        break;
    }

    if (!handled) {
        entity.points[handleIndex] = target;
    }
    out.usedFallback = !handled;

    out.changed = true;
    out.newCenter = !entity.points.empty() ? entity.points[0] : Point3{};
    out.centerMoved = hasCenter
                      && (out.newCenter.x != out.oldCenter.x
                          || out.newCenter.y != out.oldCenter.y);
    if (!out.centerMoved) out.diameterRotation = false;

    return out;
}

// =====================================================================
//  Keeping a drag off zero
// =====================================================================

double minHandleSeparation(double pixelsPerWorldUnit, double pixels)
{
    if (!(pixelsPerWorldUnit > 0.0) || !std::isfinite(pixelsPerWorldUnit))
        return kMinEdgeLength;
    const double world = pixels / pixelsPerWorldUnit;
    return (world > kMinEdgeLength) ? world : kMinEdgeLength;
}

Point2D keepHandleApart(const Point2D& proposed,
                        const std::vector<Point2D>& others,
                        const Point2D& comingFrom,
                        double minSeparation)
{
    if (!(minSeparation > 0.0)) return proposed;

    Point2D out = proposed;
    // One pass per obstacle, repeated: pushing clear of one corner can
    // move the handle inside another. Two passes settle the rectangle
    // case (adjacent corner, then the opposite one); the cap is there so
    // a pathological arrangement cannot spin.
    for (int pass = 0; pass < 4; ++pass) {
        bool moved = false;
        for (const Point2D& o : others) {
            double dx = out.x - o.x, dy = out.y - o.y;
            double d = std::sqrt(dx * dx + dy * dy);
            if (d >= minSeparation) continue;

            if (d < geometry::kExactEps) {
                // Landed exactly on it, so there is no direction to push
                // along. Back toward wherever the handle came from; if
                // that is degenerate too, any axis will do.
                dx = comingFrom.x - o.x;
                dy = comingFrom.y - o.y;
                d = std::sqrt(dx * dx + dy * dy);
                if (d < geometry::kExactEps) { dx = 1.0; dy = 0.0; d = 1.0; }
            }
            out.x = o.x + dx / d * minSeparation;
            out.y = o.y + dy / d * minSeparation;
            moved = true;
        }
        if (!moved) break;
    }
    return out;
}

// -----------------------------------------------------------------
//  Opening a full (360-degree) arc by dragging one end
// -----------------------------------------------------------------
ArcOpenResult openFullArcByDrag(const Point2D& center, double radius,
                                const Point2D& cursor, double fixedEndAngleDeg,
                                int draggedIndex, double prevSweepDeg)
{
    (void)radius;   // radius is preserved; only the cursor's angle matters here
    ArcOpenResult r;
    const double s = (prevSweepDeg >= 0.0) ? 1.0 : -1.0;
    const double A = fixedEndAngleDeg;
    const double B = radiansToDegrees(angleOf(center, cursor));

    // CCW gap from the fixed end A to the dragged angle B, in [0,360). The two
    // arcs over these endpoints have magnitudes that sum to 360: one has the
    // dragged end as the start (index 1, magnitude 360-gap), the other has the
    // fixed end as the start (index 2, magnitude gap).
    double gap = std::fmod(B - A, 360.0);
    if (gap < 0.0) gap += 360.0;
    const double magDragged = 360.0 - gap;   // branch: dragged end is the start
    const double magFixed   = gap;           // branch: fixed end is the start

    // prevSweep == +/-360 seeds the drag: pick the branch nearest full so the arc
    // starts at ~360 and shrinks for EITHER drag direction (no collapse to ~0).
    const bool atStart = std::fabs(std::fabs(prevSweepDeg) - 360.0) < geometry::kAngleEpsDeg;

    int    idx;
    double mag;
    if (atStart) {
        if (magDragged >= magFixed) { idx = 1; mag = magDragged; }
        else                        { idx = 2; mag = magFixed;   }
    } else {
        idx = (draggedIndex == 2) ? 2 : 1;             // stay on the locked branch
        mag = (idx == 1) ? magDragged : magFixed;
        // Swap the dragged endpoint only at an inflection, so the sweep stays
        // strictly inside (0,360): magnitude climbing past ~359 (toward full) or
        // falling below ~1 (the dragged end crossing the fixed end) flips branch.
        if (mag > 359.0 || mag < 1.0) {
            idx = (idx == 1) ? 2 : 1;
            mag = (idx == 1) ? magDragged : magFixed;
        }
    }

    r.draggedIndex = idx;
    r.startAngle   = (idx == 1) ? B : A;
    r.sweepAngle   = s * mag;
    return r;
}


// ---- Tangent-arc host geometry --------------------------------------------

Point2D projectOntoTangentHost(const Entity& host, const Point2D& pt)
{
    // Entity::closestPoint already does exactly this for a Line (segment
    // projection) and a Rectangle (nearest of the four edges); reuse it.
    if ((host.type == EntityType::Line || host.type == EntityType::Rectangle)
            && host.points.size() >= 2)
        return host.closestPoint(pt);
    return pt;
}

bool closestTangentHostEdge(const Entity& host, const Point2D& pt, Point2D& a, Point2D& b)
{
    if (host.type == EntityType::Line && host.points.size() >= 2) {
        a = host.points[0];
        b = host.points[1];
        return true;
    }
    Point2D c[4];
    if (!rectangleCorners(host, c)) return false;
    double minDist = -1.0;
    for (int i = 0; i < 4; ++i) {
        const Point2D& p = c[i];
        const Point2D& q = c[(i + 1) % 4];
        const Point2D proj = geometry::closestPointOnLine(pt, p, q);
        const double d = std::hypot(pt.x - proj.x, pt.y - proj.y);
        if (minDist < 0.0 || d < minDist) {   // first strict minimum wins
            minDist = d;
            a = p;
            b = q;
        }
    }
    return true;
}

Point2D tangentHostEdgeDirAt(const Entity& host, const Point2D& pt)
{
    Point2D a, b;
    if (closestTangentHostEdge(host, pt, a, b)) return Point2D(b.x - a.x, b.y - a.y);
    return Point2D(1.0, 0.0);   // fallback
}

bool solveTangentArcPreservingSide(
    const Entity& host, const Point2D& tanPt, const Point2D& endPt,
    const Point2D& currentCenter, const Point2D& currentTanPt, double currentSweepDeg,
    Point2D& outCenter, double& outRadius, double& outStartDeg, double& outSweepDeg)
{
    const Point2D edgeDir = tangentHostEdgeDirAt(host, tanPt);
    if (geometry::length(edgeDir) < kEps) return false;

    Point2D normal = geometry::perpendicular(geometry::normalize(edgeDir));
    // Orient the normal toward the current center's side of the host.
    const double ox = currentCenter.x - currentTanPt.x;
    const double oy = currentCenter.y - currentTanPt.y;
    if (ox * normal.x + oy * normal.y < 0.0) normal = Point2D(-normal.x, -normal.y);

    // The center lies on the normal at tanPt (center = tanPt + t*normal) and
    // is equidistant from tanPt and endPt (radius = t). Solving
    // |tanPt + t*normal - endPt|^2 = t^2 gives t = -|d|^2 / (2 d.normal),
    // with d = tanPt - endPt.
    const double dx = tanPt.x - endPt.x, dy = tanPt.y - endPt.y;
    const double dDotN = dx * normal.x + dy * normal.y;
    if (std::abs(dDotN) < kEps) return false;            // degenerate
    const double t = -(dx * dx + dy * dy) / (2.0 * dDotN);
    if (t < kEps) return false;                            // center would flip sides

    outCenter = Point2D(tanPt.x + normal.x * t, tanPt.y + normal.y * t);
    outRadius = t;
    outStartDeg = radiansToDegrees(std::atan2(tanPt.y - outCenter.y, tanPt.x - outCenter.x));
    const double endDeg = radiansToDegrees(std::atan2(endPt.y - outCenter.y, endPt.x - outCenter.x));
    outSweepDeg = endDeg - outStartDeg;
    while (outSweepDeg > 180.0) outSweepDeg -= 360.0;     // normalize to [-180, 180]
    while (outSweepDeg < -180.0) outSweepDeg += 360.0;
    // Preserve the current CW/CCW sense.
    if (currentSweepDeg >= 0 && outSweepDeg < 0) outSweepDeg += 360.0;
    else if (currentSweepDeg < 0 && outSweepDeg > 0) outSweepDeg -= 360.0;
    return true;
}

}  // namespace sketch
}  // namespace hobbycad
