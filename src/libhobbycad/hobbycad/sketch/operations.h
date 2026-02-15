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

#include <QVector>
#include <functional>

namespace hobbycad {
namespace sketch {

// =====================================================================
//  Intersection Detection
// =====================================================================

/// Information about an intersection between two entities
struct Intersection {
    int entityId1 = 0;
    int entityId2 = 0;
    QPointF point;
    double param1 = 0.0;  ///< Parameter on entity 1 (0-1 for lines)
    double param2 = 0.0;  ///< Parameter on entity 2
};

/// Find all intersections between entities in a sketch
/// @param entities List of entities to check
/// @return List of all intersection points with entity IDs
HOBBYCAD_EXPORT QVector<Intersection> findAllIntersections(
    const QVector<Entity>& entities);

/// Find intersections of a specific entity with all others
/// @param entity The entity to check
/// @param others Other entities to check against
/// @return List of intersections
HOBBYCAD_EXPORT QVector<Intersection> findIntersections(
    const Entity& entity,
    const QVector<Entity>& others);

/// Find intersection between two specific entities
HOBBYCAD_EXPORT QVector<Intersection> findIntersection(
    const Entity& e1, const Entity& e2);

// =====================================================================
//  Offset Operation
// =====================================================================

/// Result of an offset operation
struct OffsetResult {
    bool success = false;
    Entity entity;            ///< The new offset entity
    QString errorMessage;
};

/// Create an offset copy of an entity
/// @param entity The entity to offset
/// @param distance The offset distance (positive = outward/right)
/// @param side Which side to offset (use clickPos to determine, or +1/-1)
/// @param newId ID for the new entity
HOBBYCAD_EXPORT OffsetResult offsetEntity(
    const Entity& entity,
    double distance,
    const QPointF& clickPos,
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
    QString errorMessage;
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
HOBBYCAD_EXPORT std::optional<QPointF> findCornerPoint(
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
    QString errorMessage;
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
    QVector<Entity> newEntities;  ///< Entities after trimming (may be multiple)
    int removedEntityId = -1;     ///< ID of entity that was trimmed
    QString errorMessage;
};

/// Trim an entity at intersection points, removing the segment containing clickPos
/// @param entity The entity to trim
/// @param intersections Intersection points on this entity
/// @param clickPos The position clicked (determines which segment to remove)
/// @param nextId Function to get next entity ID
HOBBYCAD_EXPORT TrimResult trimEntity(
    const Entity& entity,
    const QVector<QPointF>& intersections,
    const QPointF& clickPos,
    std::function<int()> nextId);

// =====================================================================
//  Extend Operation
// =====================================================================

/// Result of an extend operation
struct ExtendResult {
    bool success = false;
    Entity entity;            ///< The extended entity
    QString errorMessage;
};

/// Extend an entity to the nearest intersection with boundary entities
/// @param entity The entity to extend
/// @param boundaries Entities to extend to
/// @param extendEnd Which end to extend (0 = start, 1 = end, -1 = nearest to clickPos)
/// @param clickPos Used to determine which end when extendEnd == -1
HOBBYCAD_EXPORT ExtendResult extendEntity(
    const Entity& entity,
    const QVector<Entity>& boundaries,
    int extendEnd,
    const QPointF& clickPos = QPointF());

// =====================================================================
//  Split Operation
// =====================================================================

/// Result of a split operation
struct SplitResult {
    bool success = false;
    QVector<Entity> newEntities;  ///< Entities after splitting
    int removedEntityId = -1;     ///< ID of original entity that was split
    QString errorMessage;
};

/// Split an entity at a specific point
/// @param entity The entity to split
/// @param splitPoint Point where to split
/// @param nextId Function to get next entity ID
HOBBYCAD_EXPORT SplitResult splitEntityAt(
    const Entity& entity,
    const QPointF& splitPoint,
    std::function<int()> nextId);

/// Split an entity at all intersection points with other entities
/// @param entity The entity to split
/// @param intersections All intersection points on this entity
/// @param nextId Function to get next entity ID
HOBBYCAD_EXPORT SplitResult splitEntityAtIntersections(
    const Entity& entity,
    const QVector<QPointF>& intersections,
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
HOBBYCAD_EXPORT QVector<int> findConnectedChain(
    int startId,
    const QVector<Entity>& entities,
    double tolerance = geometry::POINT_TOLERANCE);

/// Find a line connected to the given line at a corner near the click position
/// @param lineEntity The reference line entity
/// @param allEntities All entities in the sketch
/// @param cornerHint Position hint to determine which endpoint to check
/// @param tolerance Distance tolerance for endpoint matching
/// @return ID of connected line, or -1 if none found
HOBBYCAD_EXPORT int findConnectedLineAtCorner(
    const Entity& lineEntity,
    const QVector<Entity>& allEntities,
    const QPointF& cornerHint,
    double tolerance = geometry::POINT_TOLERANCE);

}  // namespace sketch
}  // namespace hobbycad

#endif  // HOBBYCAD_SKETCH_OPERATIONS_H
