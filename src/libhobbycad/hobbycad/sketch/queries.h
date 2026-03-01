// =====================================================================
//  src/libhobbycad/hobbycad/sketch/queries.h — Sketch query functions
// =====================================================================
//
//  Functions for querying and analyzing sketch entities.
//  Useful for hit testing, selection, validation, and analysis.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_SKETCH_QUERIES_H
#define HOBBYCAD_SKETCH_QUERIES_H

#include "entity.h"
#include "constraint.h"
#include "../core.h"
#include "../geometry/types.h"
#include "../types.h"

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace hobbycad {
namespace sketch {

// =====================================================================
//  Hit Testing
// =====================================================================

/// Result of a hit test
struct HitTestResult {
    int entityId = -1;              ///< ID of hit entity (-1 if none)
    double distance = 0.0;          ///< Distance from query point to entity
    Point2D closestPoint;           ///< Closest point on entity
    int pointIndex = -1;            ///< Index of hit control point (-1 if not a point)
};

/// Find entities at or near a point
/// @param entities Entities to search
/// @param point Query point
/// @param tolerance Maximum distance to consider a hit
/// @return IDs of entities within tolerance, sorted by distance (nearest first)
HOBBYCAD_EXPORT std::vector<int> findEntitiesAtPoint(
    const std::vector<Entity>& entities,
    const Point2D& point,
    double tolerance = geometry::POINT_TOLERANCE);

/// Find the nearest entity to a point
/// @param entities Entities to search
/// @param point Query point
/// @return Hit test result with nearest entity info
HOBBYCAD_EXPORT HitTestResult findNearestEntity(
    const std::vector<Entity>& entities,
    const Point2D& point);

/// Find entities within a rectangular region
/// @param entities Entities to search
/// @param rect Selection rectangle
/// @param mustBeFullyInside If true, entity must be fully inside rect; if false, any intersection counts
/// @return IDs of entities in the region
HOBBYCAD_EXPORT std::vector<int> findEntitiesInRect(
    const std::vector<Entity>& entities,
    const Rect2D& rect,
    bool mustBeFullyInside = false);

/// Find control points at or near a point
/// @param entities Entities to search
/// @param point Query point
/// @param tolerance Maximum distance
/// @return List of (entityId, pointIndex) pairs for matching control points
HOBBYCAD_EXPORT std::vector<std::pair<int, int>> findControlPointsAtPoint(
    const std::vector<Entity>& entities,
    const Point2D& point,
    double tolerance = geometry::POINT_TOLERANCE);

// =====================================================================
//  Sketch Validation
// =====================================================================

/// Validation result
struct ValidationResult {
    bool valid = true;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
};

/// Validate a sketch for consistency
/// @param entities Sketch entities
/// @param constraints Sketch constraints
/// @return Validation result with any errors/warnings
HOBBYCAD_EXPORT ValidationResult validateSketch(
    const std::vector<Entity>& entities,
    const std::vector<Constraint>& constraints);

/// Check if sketch is fully constrained (DOF == 0)
/// @note Requires solver to be available; returns false if not
HOBBYCAD_EXPORT bool isSketchFullyConstrained(
    const std::vector<Entity>& entities,
    const std::vector<Constraint>& constraints);

/// Find entities that have no constraints applied
/// @param entities Sketch entities
/// @param constraints Sketch constraints
/// @return IDs of unconstrained entities
HOBBYCAD_EXPORT std::vector<int> findUnconstrainedEntities(
    const std::vector<Entity>& entities,
    const std::vector<Constraint>& constraints);

/// Find entities that are under-constrained (have some but not enough constraints)
/// @note This is a heuristic; full DOF analysis requires the solver
HOBBYCAD_EXPORT std::vector<int> findUnderconstrainedEntities(
    const std::vector<Entity>& entities,
    const std::vector<Constraint>& constraints);

// =====================================================================
//  Sketch Analysis
// =====================================================================

/// Calculate total area of all closed profiles in the sketch
/// @param entities Sketch entities
/// @return Total area (always positive)
HOBBYCAD_EXPORT double sketchArea(const std::vector<Entity>& entities);

/// Calculate total length of all entities in the sketch
/// @param entities Sketch entities
/// @return Total length
HOBBYCAD_EXPORT double sketchLength(const std::vector<Entity>& entities);

/// Get bounding box of entire sketch
/// @param entities Sketch entities
/// @return Bounding box enclosing all entities
HOBBYCAD_EXPORT geometry::BoundingBox sketchBounds(const std::vector<Entity>& entities);

/// Count entities by type
/// @param entities Sketch entities
/// @return Map of entity type to count
HOBBYCAD_EXPORT std::map<EntityType, int> countEntitiesByType(const std::vector<Entity>& entities);

// =====================================================================
//  Curve Utilities
// =====================================================================

/// Get the length of an entity (arc length for curves)
/// @param entity The entity
/// @return Length of the entity
HOBBYCAD_EXPORT double entityLength(const Entity& entity);

/// Get a point at parameter t along an entity
/// @param entity The entity
/// @param t Parameter in range [0, 1]
/// @return Point at parameter t
HOBBYCAD_EXPORT Point2D pointAtParameter(const Entity& entity, double t);

/// Get the parameter value for a point on an entity
/// @param entity The entity
/// @param point Point on or near the entity
/// @return Parameter value in [0, 1], or -1 if point is not on entity
HOBBYCAD_EXPORT double parameterAtPoint(const Entity& entity, const Point2D& point);

/// Get the tangent vector at parameter t
/// @param entity The entity
/// @param t Parameter in range [0, 1]
/// @return Unit tangent vector at parameter t
HOBBYCAD_EXPORT Point2D tangentAtParameter(const Entity& entity, double t);

/// Get the normal vector at parameter t (perpendicular to tangent)
/// @param entity The entity
/// @param t Parameter in range [0, 1]
/// @return Unit normal vector at parameter t
HOBBYCAD_EXPORT Point2D normalAtParameter(const Entity& entity, double t);

// =====================================================================
//  Tessellation
// =====================================================================

/// Tessellate an entity into a polyline
/// @param entity The entity to tessellate
/// @param tolerance Maximum deviation from true curve
/// @return Vector of points approximating the entity
HOBBYCAD_EXPORT std::vector<Point2D> tessellate(
    const Entity& entity,
    double tolerance = 0.1);

/// Tessellate multiple entities into line segments
/// @param entities Entities to tessellate
/// @param tolerance Maximum deviation from true curves
/// @return Vector of line segments
HOBBYCAD_EXPORT std::vector<std::pair<Point2D, Point2D>> tessellateToLines(
    const std::vector<Entity>& entities,
    double tolerance = 0.1);

}  // namespace sketch
}  // namespace hobbycad

#endif  // HOBBYCAD_SKETCH_QUERIES_H
