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
#include "../types.h"

#include <cmath>
#include <functional>
#include <limits>
#include <unordered_set>
#include <vector>

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
    Diameter,      ///< Circle/arc diameter (2 x radius)
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
    std::vector<int> entityIds;            ///< IDs of entities involved
    std::vector<int> pointIndices;         ///< Point indices within entities (for multi-point entities)

    // Value (for dimensional constraints)
    double value = 0.0;                    ///< Constraint value (mm or degrees)

    // State
    bool isDriving = true;                 ///< Driving vs reference (display only)
    bool enabled = true;                   ///< Whether constraint is active
    bool satisfied = true;                 ///< Whether constraint is currently satisfied (solver feedback)

    // Display properties
    Point2D labelPosition;                 ///< Where to display dimension label
    bool labelVisible = true;              ///< Show/hide dimension text

    // Angle constraint display
    //
    // For Angle and FixedAngle constraints, these fields control where and
    // how the angle arc + label are drawn.
    //
    // anchorPoint: the vertex where the two lines meet (or the computed ray
    //   intersection for non-adjacent lines).  When invalid (NaN), the
    //   drawing code computes it on the fly from the line geometry.
    //
    // supplementary: selects which of the two supplementary angles at the
    //   vertex to display.  false = the acute/obtuse angle between the
    //   direction vectors (SLVS_C_ANGLE value); true = 360 degrees minus that.
    //   This lets the UI unambiguously show the interior vs exterior angle.
    Point2D anchorPoint = Point2D(std::numeric_limits<double>::quiet_NaN(),
                                  std::numeric_limits<double>::quiet_NaN());
    bool supplementary = false;            ///< Show the supplementary (>180 degrees) angle

    /// True when anchorPoint has been explicitly set (not NaN)
    bool hasAnchorPoint() const {
        return !std::isnan(anchorPoint.x) && !std::isnan(anchorPoint.y);
    }
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
HOBBYCAD_EXPORT std::vector<ConstraintType> suggestConstraints(
    const struct Entity& e1, const struct Entity& e2);

/// Suggest applicable constraint types for a single entity
HOBBYCAD_EXPORT std::vector<ConstraintType> suggestConstraints(const struct Entity& entity);

/// Callable that resolves an entity ID to a (const) Entity pointer.
/// Used by overloads that avoid copying entity vectors.
using EntityFinder = std::function<const struct Entity*(int id)>;

/// Calculate the current value of a dimensional constraint
/// (what the dimension would be if applied now)
HOBBYCAD_EXPORT double calculateConstraintValue(
    ConstraintType type,
    const std::vector<const struct Entity*>& entities,
    const std::vector<int>& pointIndices = {});

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
    const std::vector<struct Entity>& entities,
    Point2D& p1, Point2D& p2);

/// Entity-finder overload — avoids requiring a full entity vector.
/// The caller provides a function that maps entity ID -> const Entity*.
HOBBYCAD_EXPORT bool getConstraintEndpoints(
    const Constraint& constraint,
    EntityFinder findEntity,
    Point2D& p1, Point2D& p2);

/// Find the entity with a given ID in a vector
/// @param entities Vector of entities to search
/// @param id Entity ID to find
/// @return Pointer to entity, or nullptr if not found
HOBBYCAD_EXPORT const struct Entity* findEntityById(
    const std::vector<struct Entity>& entities, int id);

// =====================================================================
//  Constraint Utility Functions
// =====================================================================

/// Get the set of entity IDs that have at least one enabled driving constraint
HOBBYCAD_EXPORT std::unordered_set<int> getConstrainedEntityIds(const std::vector<Constraint>& constraints);

/// Compute the current geometric value of a driven (non-driving) constraint
/// by inspecting the referenced entity geometry.
/// Returns the value (distance in mm, radius in mm, angle in degrees, etc.)
HOBBYCAD_EXPORT double computeDrivenValue(
    const Constraint& constraint,
    const std::vector<Entity>& entities);

/// Entity-finder overload — avoids copying the full entity vector.
HOBBYCAD_EXPORT double computeDrivenValue(
    const Constraint& constraint,
    EntityFinder findEntity);

}  // namespace sketch
}  // namespace hobbycad

#endif  // HOBBYCAD_SKETCH_CONSTRAINT_H
