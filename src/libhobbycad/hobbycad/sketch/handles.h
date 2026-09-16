// =====================================================================
//  src/libhobbycad/hobbycad/sketch/handles.h — Entity handle dragging
// =====================================================================
//
//  Geometry for dragging an entity's edit handles.  Given an entity, a
//  handle index and an already-snapped target position, updates the
//  entity's points and derived values (radius, angles).
//
//  This is pure geometry: it performs no constraint lookup, no undo
//  bookkeeping, no solving and no rendering.  Callers resolve any
//  driving dimensional constraints themselves and pass the resulting
//  values in via HandleDragLocks; the GUI layer keeps ownership of
//  snapping, modifier keys, selection and label placement.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_SKETCH_HANDLES_H
#define HOBBYCAD_SKETCH_HANDLES_H

#include "entity.h"
#include "../core.h"
#include "../types.h"

namespace hobbycad {
namespace sketch {

// =====================================================================
//  Handle Drag Inputs
// =====================================================================

/// Dimensional locks already resolved from the sketch's driving
/// constraints by the caller.  A negative value means "not locked".
struct HOBBYCAD_EXPORT HandleDragLocks {
    /// Locked circle/arc radius.  Callers holding a Diameter constraint
    /// should halve it before setting this.
    double radius = -1.0;

    /// Locked arc sweep magnitude in degrees.  The sign of the entity's
    /// existing sweepAngle is preserved.
    double sweepAngle = -1.0;

    /// Angle snap increment in radians for handles that slide along a
    /// circular path (the arc-slot start handle, a slot end resized about
    /// a pinned handle).  Zero or negative disables snapping.
    double angleSnapIncrement = 0.0;

    /// How an arc slot's END handle (1 or 2) moves. SlideOnArc keeps the
    /// center and the arc radius (the default); ResizeAboutOther keeps the
    /// other end where it is and moves the center (the GUI's Shift);
    /// FreeResize moves the end freely and re-derives the center on the
    /// new chord's bisector at the radius the drag implies (Alt).
    enum class SlotEndMode { SlideOnArc, ResizeAboutOther, FreeResize };
    SlotEndMode slotEndMode = SlotEndMode::SlideOnArc;

    /// A slot end handle (1 or 2) the user pinned: a drag of the other end
    /// resizes the arc about it, with the angle snap when set. -1 for none.
    int fixedHandleIndex = -1;

    /// For a tangent arc (Entity::tangentEntityId >= 0), the entity it stays
    /// tangent to, resolved by the caller. Every handle then keeps the arc's
    /// start point on that entity. Null otherwise.
    const Entity* tangentHost = nullptr;
};

// =====================================================================
//  Handle Drag Result
// =====================================================================

/// Describes what the drag moved, so callers can update dependent
/// display state (such as dimension label positions) without having to
/// re-derive it.
struct HOBBYCAD_EXPORT HandleDragResult {
    /// True when the entity's geometry was modified.
    bool changed = false;

    /// Defining point (circle/arc/polygon/ellipse center) before and
    /// after the drag.  Equal when the entity has no such point.
    Point2D oldCenter{};
    Point2D newCenter{};

    /// True when oldCenter and newCenter differ.
    bool centerMoved = false;

    /// True when a 2-point (diameter) circle was rotated about its
    /// fixed endpoint, which moves labels along an arc rather than by a
    /// simple translation.
    bool diameterRotation = false;

    /// True when no type-specific rule applied and the handle was
    /// simply moved to the target.  Callers that propagate a moved
    /// point to coincident neighbors should do so only in this case.
    bool usedFallback = false;

    /// Position the dragged handle occupied before the drag.  Callers
    /// use this to find coincident neighboring points to propagate to.
    Point2D previousHandlePos{};
};

// =====================================================================
//  Handle Dragging
// =====================================================================

/// Apply a handle drag to an entity.
///
/// @param entity       Entity to modify in place.
/// @param handleIndex  Index into entity.points of the handle dragged.
/// @param target       Target position, already snapped by the caller.
/// @param locks        Dimensional locks resolved from constraints.
/// @return             What moved; see HandleDragResult.
///
/// Out-of-range handle indices and entities with too few points for
/// their type are ignored, returning a result with changed == false.
///
/// Covered per type: Circle (center, radius, diameter and three-point
/// forms, radius lock), Arc (center, start slides, end resizes, sweep lock;
/// a tangent arc when locks.tangentHost is set), Slot (arc slots: center,
/// slide, the end modes and the pinned handle in HandleDragLocks, sweep
/// clamped to the slot's absolute limit), Polygon, Ellipse, Parallelogram,
/// Text, and a Bezier spline (an anchor drags with its handles, a handle
/// keeps its node smooth). Lines, rectangles, points and other splines
/// simply move the point (usedFallback).
/// Smallest edge a drag is allowed to produce, in world units.
///
/// An edge driven to exactly zero is not merely ugly, it is UNRECOVERABLE.
/// A zero-length line has no direction, so Horizontal and Vertical are both
/// satisfied trivially and the coincident constraints hold the corners
/// together; nothing remains in the system that could restore the shape.
/// Dragging a corner of a collapsed rectangle afterwards just translates
/// the point it has become. The solver reports success throughout, so
/// nothing tells the user their rectangle is gone.
///
/// The floor is therefore a hard invariant rather than a nicety, and this
/// absolute value exists so it holds at any zoom. In practice the caller
/// passes something larger; see minHandleSeparation().
constexpr double kMinEdgeLength = geometry::kDegenerateLen;

/// The separation a handle drag should honor at a given zoom.
///
/// Scaled to the viewport rather than fixed, because a fixed world-unit
/// floor is the wrong shape for the problem: 1e-6 mm is invisible at any
/// normal zoom, so the user still sees a collapsed rectangle and still
/// believes it is broken. A few pixels' worth of world units keeps the
/// edge grabbable at whatever zoom the user is actually working at, and
/// zooming in is what earns finer control, which is the same bargain
/// every other snap in the canvas makes.
///
/// @param pixelsPerWorldUnit  The viewport scale (zoom).
/// @param pixels              How many screen pixels the floor should be.
/// @return                    Never less than kMinEdgeLength.
HOBBYCAD_EXPORT double minHandleSeparation(double pixelsPerWorldUnit,
                                           double pixels = 3.0);

/// Push `proposed` away from anything it would collapse onto.
///
/// @param proposed   Where the drag wants to put the handle.
/// @param others     Points it must not reach: the other corners of the
///                   entity, and of its group when it has one.
/// @param comingFrom The handle's position before this drag, used as the
///                   push direction when `proposed` lands exactly on one
///                   of `others` and the direction would otherwise be
///                   undefined.
/// @param minSeparation  Usually from minHandleSeparation().
/// @return           `proposed` unchanged when it is already clear.
HOBBYCAD_EXPORT Point2D keepHandleApart(const Point2D& proposed,
                                        const std::vector<Point2D>& others,
                                        const Point2D& comingFrom,
                                        double minSeparation);

HOBBYCAD_EXPORT HandleDragResult dragEntityHandle(
        Entity& entity,
        int handleIndex,
        const Point2D& target,
        const HandleDragLocks& locks = HandleDragLocks{});

/// Result of opening a full (360-degree) arc by dragging one end.
struct HOBBYCAD_EXPORT ArcOpenResult {
    double startAngle   = 0.0;   ///< new arc start angle (degrees)
    double sweepAngle   = 0.0;   ///< signed sweep; magnitude kept inside (0,360)
    int    draggedIndex = 1;     ///< which endpoint the drag now controls (1 or 2)
};

/// Reposition a full circle that was opened into one 360-degree arc so it
/// SHRINKS from 360 as the cursor drags one end away, tracking the mouse
/// continuously and never exceeding 360. The endpoint NOT being dragged stays at
/// @a fixedEndAngleDeg. The gesture is branch-locked: on the first move (signaled
/// by @a prevSweepDeg == +/-360) it picks the branch that starts near 360 for
/// either drag direction; thereafter it stays on @a draggedIndex so the sweep
/// shrinks monotonically and crosses 180 only when the mouse is swung that far,
/// swapping the dragged endpoint only at the inflection (|sweep| > 359 or < 1).
/// The arc sign is taken from @a prevSweepDeg. @a radius is unused (angle only).
///
/// @param center           arc center
/// @param radius           arc radius (unchanged; accepted for API symmetry)
/// @param cursor           current cursor position (its angle drives the drag)
/// @param fixedEndAngleDeg angle (deg) of the endpoint left in place
/// @param draggedIndex     the endpoint currently dragged (1=start, 2=end)
/// @param prevSweepDeg     the previous frame's sweep (+/-360 seeds the drag)
HOBBYCAD_EXPORT ArcOpenResult openFullArcByDrag(
        const Point2D& center, double radius,
        const Point2D& cursor, double fixedEndAngleDeg,
        int draggedIndex, double prevSweepDeg);


// ---- Tangent-arc host geometry --------------------------------------------
// The pure geometry behind the canvas's tangent-arc handle drag, moved down
// from SketchCanvas per the layering rule. The "host" is the entity an arc is
// tangent to: a Line, or any edge of a Rectangle.

/// Closest point on the tangent host to `pt`: on a Line, or on the nearest
/// edge of a Rectangle (2-point axis-aligned or 4-point rotated). Any other
/// type, or a host with fewer than two points, returns `pt` unchanged.
HOBBYCAD_EXPORT Point2D projectOntoTangentHost(const Entity& host, const Point2D& pt);

/// Direction (unnormalized) of the host edge nearest to `pt`: a Line's own
/// direction, or that of the nearest Rectangle edge. (1, 0) otherwise.
/// The edge of a tangent host nearest to `pt`: a Line's own endpoints, or the
/// nearest of a Rectangle's four edges (segment distance, first minimum wins).
/// Returns false for any other host.
HOBBYCAD_EXPORT bool closestTangentHostEdge(const Entity& host, const Point2D& pt,
                                            Point2D& a, Point2D& b);

HOBBYCAD_EXPORT Point2D tangentHostEdgeDirAt(const Entity& host, const Point2D& pt);

/// Solve the arc tangent to `host` at `tanPt` and through `endPt`, its
/// center on the host's normal at `tanPt`. The normal is oriented toward
/// `currentCenter` (seen from `currentTanPt`) so the center stays on the side
/// it is on now, and the sweep keeps the sense of `currentSweepDeg`; that is
/// what stops the center flipping across the host mid-drag. Degrees in and
/// out. Returns false when degenerate (host edge too short, chord
/// perpendicular to the normal, or the center would cross the host).
HOBBYCAD_EXPORT bool solveTangentArcPreservingSide(
    const Entity& host, const Point2D& tanPt, const Point2D& endPt,
    const Point2D& currentCenter, const Point2D& currentTanPt, double currentSweepDeg,
    Point2D& outCenter, double& outRadius, double& outStartDeg, double& outSweepDeg);

}  // namespace sketch
}  // namespace hobbycad

#endif  // HOBBYCAD_SKETCH_HANDLES_H
