// =====================================================================
//  src/libhobbycad/hobbycad/sketch/constraint.h — Sketch constraint types
// =====================================================================
//
//  Unified constraint representation for parametric sketching.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_SKETCH_CONSTRAINT_H
#define HOBBYCAD_SKETCH_CONSTRAINT_H

#include "../core.h"

#include <QPointF>
#include <QVector>

namespace hobbycad {
namespace sketch {

// =====================================================================
//  Constraint Types
// =====================================================================

/// Types of sketch constraints
enum class ConstraintType {
    // Dimensional constraints
    Distance,      ///< Linear distance between two points or point-to-line
    Radius,        ///< Circle/arc radius
    Diameter,      ///< Circle/arc diameter (2 × radius)
    Angle,         ///< Angle between two lines

    // Geometric constraints
    Horizontal,    ///< Line is horizontal
    Vertical,      ///< Line is vertical
    Parallel,      ///< Two lines are parallel
    Perpendicular, ///< Two lines are perpendicular
    Coincident,    ///< Two points share the same position
    Tangent,       ///< Arc/circle tangent to line or arc/circle
    Equal,         ///< Two entities have equal length/radius
    Midpoint,      ///< Point lies at midpoint of a line
    Symmetric,     ///< Two points symmetric about a line
    Concentric,    ///< Two circles/arcs share the same center
    Collinear,     ///< Points or lines are collinear
    PointOnLine,   ///< Point lies on a line
    PointOnCircle, ///< Point lies on a circle
    FixedPoint,    ///< Point is fixed in position
    FixedAngle     ///< Line has fixed angle
};

// =====================================================================
//  Constraint Definition
// =====================================================================

/// A parametric constraint between sketch entities
struct HOBBYCAD_EXPORT Constraint {
    int id = 0;                            ///< Unique ID within the sketch
    ConstraintType type = ConstraintType::Distance;

    // Entity references
    QVector<int> entityIds;                ///< IDs of entities involved
    QVector<int> pointIndices;             ///< Point indices within entities (for multi-point entities)

    // Value (for dimensional constraints)
    double value = 0.0;                    ///< Constraint value (mm or degrees)

    // State
    bool isDriving = true;                 ///< Driving vs reference (display only)
    bool enabled = true;                   ///< Whether constraint is active

    // Display properties
    QPointF labelPosition;                 ///< Where to display dimension label
    bool labelVisible = true;              ///< Show/hide dimension text
};

// =====================================================================
//  Constraint Query Functions
// =====================================================================

/// Check if a constraint type is dimensional (has a value)
HOBBYCAD_EXPORT bool isDimensionalConstraint(ConstraintType type);

/// Check if a constraint type is geometric (no value)
HOBBYCAD_EXPORT bool isGeometricConstraint(ConstraintType type);

/// Get the number of entities required for a constraint type
HOBBYCAD_EXPORT int requiredEntityCount(ConstraintType type);

/// Get human-readable name for constraint type
HOBBYCAD_EXPORT const char* constraintTypeName(ConstraintType type);

/// Get unit string for constraint type ("mm", "°", or "")
HOBBYCAD_EXPORT const char* constraintUnit(ConstraintType type);

// =====================================================================
//  Constraint Detection
// =====================================================================

/// Suggest applicable constraint types between two entities
/// Returns a list of constraint types that could be applied
HOBBYCAD_EXPORT QVector<ConstraintType> suggestConstraints(
    const struct Entity& e1, const struct Entity& e2);

/// Suggest applicable constraint types for a single entity
HOBBYCAD_EXPORT QVector<ConstraintType> suggestConstraints(const struct Entity& entity);

/// Calculate the current value of a dimensional constraint
/// (what the dimension would be if applied now)
HOBBYCAD_EXPORT double calculateConstraintValue(
    ConstraintType type,
    const QVector<struct Entity*>& entities,
    const QVector<int>& pointIndices = {});

/// Suggest the most appropriate constraint type between two entities
/// @param e1 First entity
/// @param e2 Second entity
/// @return Most likely constraint type (Distance for points, Angle for lines, etc.)
HOBBYCAD_EXPORT ConstraintType suggestConstraintType(
    const struct Entity& e1, const struct Entity& e2);

/// Get the endpoint positions for a constraint visualization
/// @param constraint The constraint to get endpoints for
/// @param entities All entities in the sketch (for ID lookup)
/// @param p1 Output: first point
/// @param p2 Output: second point
/// @return True if endpoints were found, false otherwise
HOBBYCAD_EXPORT bool getConstraintEndpoints(
    const Constraint& constraint,
    const QVector<struct Entity>& entities,
    QPointF& p1, QPointF& p2);

/// Find the entity with a given ID in a vector
/// @param entities Vector of entities to search
/// @param id Entity ID to find
/// @return Pointer to entity, or nullptr if not found
HOBBYCAD_EXPORT const struct Entity* findEntityById(
    const QVector<struct Entity>& entities, int id);

}  // namespace sketch
}  // namespace hobbycad

#endif  // HOBBYCAD_SKETCH_CONSTRAINT_H
