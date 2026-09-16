// =====================================================================
//  src/libhobbycad/hobbycad/sketch/snap.h — Snap point detection
// =====================================================================
//
//  Functions for computing snap points on sketch entities.
//  Used for cursor snapping to geometric features during editing.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_SKETCH_SNAP_H
#define HOBBYCAD_SKETCH_SNAP_H

#include "constraint.h"
#include "entity.h"
#include "../core.h"
#include "../types.h"

#include <optional>
#include <vector>

namespace hobbycad {
namespace sketch {

// =====================================================================
//  Snap Types
// =====================================================================

/// Types of geometric snap points
enum class SnapType {
    Endpoint,       ///< Line/arc/spline endpoints, rectangle corners
    Point,          ///< Standalone sketch point (stronger pull than Endpoint)
    Midpoint,       ///< Midpoint of line/arc/slot centerline, edge midpoints
    Center,         ///< Circle/arc/ellipse/polygon/slot-arc center
    Quadrant,       ///< Circle/ellipse quadrant points (N/S/E/W)
    Intersection,   ///< Entity-entity or entity-axis intersections
    ArcEndCenter,   ///< Slot endpoint centers (semicircular end centers)
    Nearest,        ///< Nearest point on entity perimeter
    Origin,         ///< Origin point (0,0)
    AxisX,          ///< Point on X axis (Y=0)
    AxisY           ///< Point on Y axis (X=0)
};

/// A snap point with position, type, and source entity
struct HOBBYCAD_EXPORT SnapPoint {
    Point2D position;                       ///< World-space position
    SnapType type = SnapType::Endpoint;     ///< Type of snap point
    int entityId = -1;                      ///< Source entity ID (-1 for origin/axis)
};

// =====================================================================
//  Snap Weight
// =====================================================================

/// Default snap weight for a given snap type.
///
/// Higher weight = stronger pull.  Used for weighted distance
/// competition: effectiveDist = rawDist / weight.
///
/// Weights: Origin=4.0, Point=3.0, Endpoint/Center=2.5,
///   Intersection=2.2, Midpoint/Quadrant/ArcEndCenter=2.0,
///   AxisX/AxisY=1.25, Nearest=1.0
HOBBYCAD_EXPORT double defaultSnapWeight(SnapType type);

/// The constraint a snap of this kind implies, or nullopt for one that
/// implies none.
///
/// A draw-then-constrain interface turns the snap used to place a point
/// into a real constraint, so that the sketch is joined rather than merely
/// looking joined. Which snaps carry that meaning is a property of the
/// sketch model, not of any one interface, so it lives here:
///
///   Endpoint, Point, Center, ArcEndCenter   Coincident
///   Midpoint                                Midpoint
///   Nearest, on a line                      PointOnLine
///   Nearest, on a circle or arc              PointOnCircle
///   Nearest, on anything else                none: the solver has no
///                                           point-on-curve for splines,
///                                           ellipses or polygons
///   Quadrant, Intersection                   none: a position derived
///                                           from geometry rather than a
///                                           point either entity owns
///   Origin, AxisX, AxisY                    none: no entity to
///                                           reference (entityId is -1)
///
/// Nearest depends on WHAT was snapped to, which is why the target's type
/// is a parameter: a point on a line and a point on a circle are different
/// constraints, and on a spline there is no constraint to make at all.
HOBBYCAD_EXPORT std::optional<ConstraintType> constraintForSnap(
    SnapType type, EntityType targetType);

// =====================================================================
//  Snap Point Collection
// =====================================================================

/// Collect all explicit snap points from a single entity.
///
/// Extracts endpoints, midpoints, centers, quadrant points,
/// arc-end-centers, etc. depending on entity type.
/// Does not include intersections or nearest-on-perimeter.
///
/// @param entity The entity to extract snap points from
/// @return Vector of snap points for this entity
HOBBYCAD_EXPORT std::vector<SnapPoint> collectSnapPoints(const Entity& entity);

/// The perimeter anchor/snap points of a slot, each tagged with the snap TYPE
/// that gives it the right icon: cap centers (ArcEndCenter/circle), the
/// line-segment ends or radial cap junctions (Endpoint/square), and the side
/// and cap midpoints (Midpoint/triangle). Handles both a linear slot (2 cap
/// centers) and an arc slot ([center, start, end] + arcFlipped). Returns empty
/// for a non-slot or a zero half-width. Pure geometry: no interaction state.
HOBBYCAD_EXPORT std::vector<SnapPoint> slotAnchorPoints(const Entity& slot);

/// Collect all explicit snap points from all entities.
///
/// Includes per-entity snap points (endpoints, midpoints, centers,
/// quadrants, arc-end-centers) plus entity-entity intersection
/// points and axis-crossing intersection points.
///
/// @param entities All sketch entities
/// @param excludeEntityId Entity ID to skip (-1 for none)
/// @return Vector of all snap points
HOBBYCAD_EXPORT std::vector<SnapPoint> collectAllSnapPoints(
    const std::vector<Entity>& entities,
    int excludeEntityId = -1);

/// Collect intersection snap points between all entity pairs.
///
/// Handles Line, Circle, Arc, Rectangle, and Parallelogram
/// intersections.  Also includes axis-crossing points.
///
/// @param entities All sketch entities
/// @param excludeEntityId Entity ID to skip (-1 for none)
/// @return Vector of intersection snap points
HOBBYCAD_EXPORT std::vector<SnapPoint> collectIntersectionSnapPoints(
    const std::vector<Entity>& entities,
    int excludeEntityId = -1);

/// Collect points where entities cross the X axis (Y=0) and Y axis (X=0).
///
/// Handles Line, Rectangle, Parallelogram, Circle, and Arc.
/// Skips points at origin (0,0) since Origin snap covers that.
///
/// @param entities All sketch entities
/// @param excludeEntityId Entity ID to skip (-1 for none)
/// @return Vector of axis-crossing snap points
HOBBYCAD_EXPORT std::vector<SnapPoint> collectAxisCrossingSnapPoints(
    const std::vector<Entity>& entities,
    int excludeEntityId = -1);

// =====================================================================
//  Nearest Point on Entity
// =====================================================================

/// Find the nearest point on any entity perimeter to a given point.
///
/// Checks Line, Circle, Arc, Rectangle, Parallelogram, and Slot
/// entity types.
///
/// @param entities All sketch entities
/// @param point Query point in world coordinates
/// @param tolerance Maximum distance to consider
/// @param excludeEntityId Entity ID to skip (-1 for none)
/// @return Snap point with type Nearest, or entityId=-1 if none found
HOBBYCAD_EXPORT SnapPoint findNearestOnPerimeter(
    const std::vector<Entity>& entities,
    const Point2D& point,
    double tolerance,
    int excludeEntityId = -1);

// =====================================================================
//  Best Snap Evaluation
// =====================================================================

/// Result of snap evaluation
struct HOBBYCAD_EXPORT SnapResult {
    bool found = false;     ///< Whether a snap point was found
    SnapPoint snap;         ///< The winning snap point
};

/// Find the best snap point for a given cursor position.
///
/// Evaluates all snap candidates using weighted distance competition:
///   effectiveDist = rawDist / defaultSnapWeight(type)
///
/// Candidates checked:
/// 1. Origin (0,0)
/// 2. All explicit entity snap points (endpoints, midpoints, etc.)
///    plus entity-entity and axis-crossing intersections
/// 3. Nearest point on entity perimeter
/// 4. Axis proximity snaps (cursor near X or Y axis)
///
/// Does NOT handle grid snapping (GUI responsibility).
///
/// @param entities All sketch entities
/// @param worldPos Cursor position in world coordinates
/// @param worldTolerance Maximum snap distance in world units
/// @param excludeEntityId Entity ID to skip (-1 for none)
/// @return SnapResult with the winning snap point, or found=false
HOBBYCAD_EXPORT SnapResult findBestSnap(
    const std::vector<Entity>& entities,
    const Point2D& worldPos,
    double worldTolerance,
    int excludeEntityId = -1);

// =====================================================================
//  Entity Intersection Utility
// =====================================================================

/// Compute intersection points between two entities.
///
/// Handles all pairwise combinations of Line, Circle, Arc,
/// Rectangle, and Parallelogram.  Rectangles and parallelograms
/// are decomposed into their 4 edges.
///
/// @param e1 First entity
/// @param e2 Second entity
/// @return Vector of intersection points
HOBBYCAD_EXPORT std::vector<Point2D> computeEntityIntersectionPoints(
    const Entity& e1, const Entity& e2);

}  // namespace sketch
}  // namespace hobbycad

#endif  // HOBBYCAD_SKETCH_SNAP_H
