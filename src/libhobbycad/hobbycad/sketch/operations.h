// =====================================================================
//  src/libhobbycad/hobbycad/sketch/operations.h — Sketch operations
// =====================================================================
//
//  Functions for modifying sketch geometry: offset, fillet, chamfer,
//  trim, extend, split, etc.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_SKETCH_OPERATIONS_H
#define HOBBYCAD_SKETCH_OPERATIONS_H

#include "entity.h"
#include "../geometry/types.h"

#include <functional>
#include <string>
#include <vector>

namespace hobbycad {
namespace sketch {

// =====================================================================
//  Intersection Detection
// =====================================================================

/// Information about an intersection between two entities
struct Intersection {
    int entityId1 = 0;
    int entityId2 = 0;
    Point2D point;
    double param1 = 0.0;  ///< Parameter on entity 1 (0-1 for lines)
    double param2 = 0.0;  ///< Parameter on entity 2
};

/// Find all intersections between entities in a sketch
/// @param entities List of entities to check
/// @return List of all intersection points with entity IDs
HOBBYCAD_EXPORT std::vector<Intersection> findAllIntersections(
    const std::vector<Entity>& entities);

/// Find intersections of a specific entity with all others
/// @param entity The entity to check
/// @param others Other entities to check against
/// @return List of intersections
HOBBYCAD_EXPORT std::vector<Intersection> findIntersections(
    const Entity& entity,
    const std::vector<Entity>& others);

/// Find intersection between two specific entities
HOBBYCAD_EXPORT std::vector<Intersection> findIntersection(
    const Entity& e1, const Entity& e2);

// =====================================================================
//  Offset Operation
// =====================================================================

/// Result of an offset operation
struct OffsetResult {
    bool success = false;
    Entity entity;            ///< The new offset entity
    std::string errorMessage;
};

/// Create an offset copy of an entity
/// @param entity The entity to offset
/// @param distance The offset distance (positive = outward/right)
/// @param side Which side to offset (use clickPos to determine, or +1/-1)
/// @param newId ID for the new entity
HOBBYCAD_EXPORT OffsetResult offsetEntity(
    const Entity& entity,
    double distance,
    const Point2D& clickPos,
    int newId);

/// Create an offset copy with explicit side selection
/// @param entity The entity to offset
/// @param distance The offset distance
/// @param side +1 for right/outward, -1 for left/inward
/// @param newId ID for the new entity
HOBBYCAD_EXPORT OffsetResult offsetEntity(
    const Entity& entity,
    double distance,
    int side,
    int newId);

// =====================================================================
//  Fillet Operation
// =====================================================================

/// Result of a fillet operation
struct FilletResult {
    bool success = false;
    Entity arc;               ///< The fillet arc
    Entity line1;             ///< Modified first line
    Entity line2;             ///< Modified second line
    std::string errorMessage;
};

/// Create a fillet (rounded corner) between two lines
/// @param line1 First line entity
/// @param line2 Second line entity (must share endpoint with line1)
/// @param radius Fillet radius
/// @param newArcId ID for the new arc entity
HOBBYCAD_EXPORT FilletResult createFillet(
    const Entity& line1,
    const Entity& line2,
    double radius,
    int newArcId);

/// Find the corner point between two lines (for fillet)
/// Returns the shared endpoint, or nullopt if lines don't connect
HOBBYCAD_EXPORT std::optional<Point2D> findCornerPoint(
    const Entity& line1,
    const Entity& line2,
    double tolerance = geometry::POINT_TOLERANCE);

// =====================================================================
//  Chamfer Operation
// =====================================================================

/// Result of a chamfer operation
struct ChamferResult {
    bool success = false;
    Entity chamferLine;       ///< The chamfer line
    Entity line1;             ///< Modified first line
    Entity line2;             ///< Modified second line
    std::string errorMessage;
};

/// Create a chamfer (beveled corner) between two lines
/// @param line1 First line entity
/// @param line2 Second line entity (must share endpoint with line1)
/// @param distance Chamfer distance from corner
/// @param newLineId ID for the new chamfer line
HOBBYCAD_EXPORT ChamferResult createChamfer(
    const Entity& line1,
    const Entity& line2,
    double distance,
    int newLineId);

/// Create a chamfer with different distances on each side
/// @param distance1 Distance from corner on line1
/// @param distance2 Distance from corner on line2
HOBBYCAD_EXPORT ChamferResult createChamfer(
    const Entity& line1,
    const Entity& line2,
    double distance1,
    double distance2,
    int newLineId);

// =====================================================================
//  Trim Operation
// =====================================================================

/// Result of a trim operation
struct TrimResult {
    bool success = false;
    std::vector<Entity> newEntities;  ///< Entities after trimming (may be multiple)
    int removedEntityId = -1;         ///< ID of entity that was trimmed
    std::string errorMessage;
};

/// Trim an entity at intersection points, removing the segment containing clickPos
/// @param entity The entity to trim
/// @param intersections Intersection points on this entity
/// @param clickPos The position clicked (determines which segment to remove)
/// @param nextId Function to get next entity ID
HOBBYCAD_EXPORT TrimResult trimEntity(
    const Entity& entity,
    const std::vector<Point2D>& intersections,
    const Point2D& clickPos,
    std::function<int()> nextId);

// =====================================================================
//  Extend Operation
// =====================================================================

/// Result of an extend operation
struct ExtendResult {
    bool success = false;
    Entity entity;            ///< The extended entity
    std::string errorMessage;
};

/// Extend an entity to the nearest intersection with boundary entities
/// @param entity The entity to extend
/// @param boundaries Entities to extend to
/// @param extendEnd Which end to extend (0 = start, 1 = end, -1 = nearest to clickPos)
/// @param clickPos Used to determine which end when extendEnd == -1
HOBBYCAD_EXPORT ExtendResult extendEntity(
    const Entity& entity,
    const std::vector<Entity>& boundaries,
    int extendEnd,
    const Point2D& clickPos = Point2D());

// =====================================================================
//  Split Operation
// =====================================================================

/// Result of a split operation
struct SplitResult {
    bool success = false;
    std::vector<Entity> newEntities;  ///< Entities after splitting
    int removedEntityId = -1;         ///< ID of original entity that was split
    std::string errorMessage;
};

/// Split an entity at a specific point
/// @param entity The entity to split
/// @param splitPoint Point where to split
/// @param nextId Function to get next entity ID
HOBBYCAD_EXPORT SplitResult splitEntityAt(
    const Entity& entity,
    const Point2D& splitPoint,
    std::function<int()> nextId);

/// Split an entity at all intersection points with other entities
/// @param entity The entity to split
/// @param intersections All intersection points on this entity
/// @param nextId Function to get next entity ID
HOBBYCAD_EXPORT SplitResult splitEntityAtIntersections(
    const Entity& entity,
    const std::vector<Point2D>& intersections,
    std::function<int()> nextId);

// =====================================================================
//  Chain Selection (connected entities)
// =====================================================================

/// Find all entities connected to a starting entity
/// Uses BFS to traverse connected entities
/// @param startId ID of starting entity
/// @param entities All entities in the sketch
/// @param tolerance Distance tolerance for endpoint matching
/// @return IDs of all connected entities (including startId)
HOBBYCAD_EXPORT std::vector<int> findConnectedChain(
    int startId,
    const std::vector<Entity>& entities,
    double tolerance = geometry::POINT_TOLERANCE);

/// Find a line connected to the given line at a corner near the click position
/// @param lineEntity The reference line entity
/// @param allEntities All entities in the sketch
/// @param cornerHint Position hint to determine which endpoint to check
/// @param tolerance Distance tolerance for endpoint matching
/// @return ID of connected line, or -1 if none found
HOBBYCAD_EXPORT int findConnectedLineAtCorner(
    const Entity& lineEntity,
    const std::vector<Entity>& allEntities,
    const Point2D& cornerHint,
    double tolerance = geometry::POINT_TOLERANCE);

// =====================================================================
//  Tangency Maintenance
// =====================================================================

/// Result of reestablishing tangency
struct ReestablishTangencyResult {
    bool success = false;
    Entity arc;               ///< The updated arc entity
    std::string errorMessage;
};

/// Reestablish tangency of an arc to a parent entity.
///
/// When an arc is tangent to a line or rectangle edge and its radius
/// or the parent entity has moved, this function repositions the arc's
/// center so that tangency is maintained.  The arc's radius and sweep
/// angle are preserved; only its center, start angle, and endpoint
/// positions are updated.
///
/// @param arc The arc entity (must have 3 points: center, tangentPoint, endPoint)
/// @param parentEntity The entity the arc is tangent to (Line or Rectangle)
/// @return Updated arc entity, or failure if types are incompatible
HOBBYCAD_EXPORT ReestablishTangencyResult reestablishTangency(
    const Entity& arc,
    const Entity& parentEntity);

// =====================================================================
//  Collinear Segment Rejoining
// =====================================================================

/// Result of validating and computing a rejoin of collinear segments
struct RejoinResult {
    bool success = false;
    Point2D mergedStart;                    ///< Start point of merged line
    Point2D mergedEnd;                      ///< End point of merged line
    std::vector<int> removedIds;            ///< IDs of entities to be removed
    std::vector<Point2D> junctionPoints;    ///< Interior junction points (for connectivity check)
    std::string errorMessage;
};

/// Validate and compute the merge of collinear line segments.
///
/// Checks that all provided entities are lines, are collinear (within
/// angular tolerance), and form a contiguous chain.  If valid, returns
/// the merged line endpoints and the junction points where segments met.
///
/// The caller is responsible for:
///  - Checking whether other entities attach at junction points
///  - Actually removing old entities and creating the merged line
///  - Updating constraints and selection state
///
/// @param entities The line entities to rejoin (must be at least 2)
/// @param angleTolerance Angular tolerance in radians for collinearity (default ~0.06°)
/// @param endpointTolerance Distance tolerance for endpoint matching
/// @return Merge result with endpoints and junction points
HOBBYCAD_EXPORT RejoinResult validateCollinearRejoin(
    const std::vector<Entity>& entities,
    double angleTolerance = 0.001,
    double endpointTolerance = 1e-4);

}  // namespace sketch
}  // namespace hobbycad

#endif  // HOBBYCAD_SKETCH_OPERATIONS_H
