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

#include "entity.h"
#include "../core.h"
#include "../types.h"

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
