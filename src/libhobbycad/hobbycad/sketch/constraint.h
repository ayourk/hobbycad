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
#include "entity.h"
#include "../types.h"

#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <unordered_set>
#include <string>
#include <vector>

namespace hobbycad {
namespace sketch {

// =====================================================================
//  Constraint Types
// =====================================================================

/// Types of sketch constraints
/// Sentinel entity id for the sketch origin (0,0), so a constraint can
/// target it (e.g. a point Coincident to the origin, grounding a sketch).
/// Resolved by the solver to a fixed 2D origin point; not a real entity.
constexpr int kSketchOriginEntity = -1000;

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
    FixedAngle,    ///< Line has fixed angle

    // APPEND-ONLY below: ConstraintType is serialized by its integer value
    // (project.cpp), so new members must be added at the END (never inserted
    // mid-enum) or existing saved files decode to the wrong type.
    Curvature,          ///< G2 curvature continuity between two Bezier curve ends (or spline<->arc)
    PointOnSpline,      ///< Point lies on a Bezier spline (a segment of it)
    CurvatureDimension, ///< Radius of curvature at a Bezier spline end (a length; sign from geometry)
    TangentAngle        ///< Directed tangent angle (0-360 deg) at a Bezier spline anchor
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
    std::string expression;                ///< Source expression when the value came from
                                           ///< one (e.g. "width/2"); empty = a plain number.
                                           ///< Re-evaluated when parameters change.

    // State
    bool isDriving = true;                 ///< Driving vs reference (display only)
    bool enabled = true;                   ///< Whether constraint is active
    bool satisfied = true;                 ///< Whether constraint is currently satisfied (solver feedback)

    // Display properties
    Point2D labelPosition;                 ///< Where to display dimension label
    bool labelVisible = true;              ///< Show/hide dimension text
    double labelAngle = std::numeric_limits<double>::quiet_NaN();  ///< Stored label angle (radians) for circle radius/diameter; NaN = follow perimeter point

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

/// Re-evaluate a constraint's expression (if it has one) against the given
/// parameter values, updating its value in place. Returns true when the value
/// actually changed. A constraint with no expression is left untouched.
HOBBYCAD_EXPORT bool reevaluateConstraint(
    Constraint& c, const std::map<std::string, double>& parameters);

/// Re-evaluate every expression-backed constraint in the list; returns how
/// many changed value. The caller re-solves afterward.
HOBBYCAD_EXPORT int reevaluateConstraints(
    std::vector<Constraint>& constraints,
    const std::map<std::string, double>& parameters);

/// Is `value` a legal magnitude for a constraint of this type?
///
/// Sizes must be positive. A Distance, Radius or Diameter of zero is not
/// a small shape, it is a DESTROYED one: a zero-length line has no
/// direction, so its horizontal/vertical constraints go slack while the
/// coincidents hold the corners together, and nothing remains that could
/// restore the geometry. The solver reports success on the collapsed
/// system, so nothing downstream notices either.
///
/// Angles are exempt: zero degrees means parallel, which is a legitimate
/// thing to ask for and is recoverable by asking for something else.
/// Non-dimensional constraints carry no value and are always accepted.
///
/// This lives beside the model rather than in a dialog because the
/// properties panel, the inline editor and the CLI are three separate
/// doors into the same field, and a rule enforced at one of them is not
/// enforced at all.
HOBBYCAD_EXPORT bool isValidConstraintValue(ConstraintType type, double value);

/// Check if a constraint type is geometric (no value)
HOBBYCAD_EXPORT bool isGeometricConstraint(ConstraintType type);

/// Get the number of entities required for a constraint type
HOBBYCAD_EXPORT int requiredEntityCount(ConstraintType type);

/// Whether a constraint's operands are the right KIND of entity. libslvs
/// asserts (and aborts) on some wrong pairings (Concentric of a line and a
/// circle, an Angle between two circles, PointOnLine whose "line" is a circle),
/// so both front ends check this before the solver ever sees them. The
/// GUI's suggestConstraints already gates its two-entity picks; this is the
/// same gate for the CLI and any other caller. `operandTypes` are the entity
/// types in the order the constraint lists them. Returns an empty string when
/// the kinds are acceptable, else a one-line reason.
HOBBYCAD_EXPORT std::string constraintOperandError(
    ConstraintType type, const std::vector<EntityType>& operandTypes);

/// Get human-readable name for constraint type
HOBBYCAD_EXPORT const char* constraintTypeName(ConstraintType type);

/// Parse a constraint type from a name, the inverse of
/// constraintTypeName().
///
/// Matching ignores case, spaces and underscores, so "PointOnLine",
/// "point on line" and "point_on_line" all resolve. A short alias is
/// accepted for the types whose full names are tedious to type
/// ("perp", "horiz", "vert", "dia", "coincide", "concentric").
///
/// In the library rather than the CLI because parsing a type name is not
/// a Qt or terminal concern: a second front end (a wxWidgets command
/// box, a script host, a file importer) needs exactly this.
///
/// @param name  The name to parse.
/// @param out   Set only on success.
/// @return false if the name matches no type, leaving `out` untouched.
HOBBYCAD_EXPORT bool parseConstraintTypeName(const std::string& name,
                                             ConstraintType* out);

/// Every constraint type, in the order constraintTypeName() lists them.
///
/// For offering the choices: a CLI usage message, a completion list, a
/// menu. Derived from the enum so it cannot fall behind it.
HOBBYCAD_EXPORT std::vector<ConstraintType> allConstraintTypes();

/// Get unit string for constraint type ("mm", "°", or "")
HOBBYCAD_EXPORT const char* constraintUnit(ConstraintType type);

/// True when a constraint's value is an ANGLE rather than a length.
///
/// Angle, FixedAngle and TangentAngle are angular, and forgetting one is an
/// easy mistake: FixedAngle was formatted as millimeters until this existed,
/// and TangentAngle was parsed as a length until 2026-09-15.
/// Callers should ask this rather than testing for ConstraintType::Angle.
HOBBYCAD_EXPORT bool isAngularConstraint(ConstraintType type);

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

/// The dimension a two-entity dimension tool should place: Distance for
/// point/point and point/line, Angle for line/line, Radius when either
/// entity is a circle or arc, Distance otherwise. Unlike
/// suggestConstraintType this never proposes a geometric constraint.
HOBBYCAD_EXPORT ConstraintType suggestDimensionType(
    const struct Entity& e1, const struct Entity& e2);

/// True when `c` is the Coincident constraint between (e1, i1) and (e2, i2),
/// in either order.
HOBBYCAD_EXPORT bool isCoincidentBetween(const Constraint& c, int e1, int i1, int e2, int i2);

/// What an angle dimension between two selected lines should carry.
struct AngleDimension {
    double value = 0.0;          ///< degrees
    bool supplementary = false;  ///< the interior angle at the shared vertex is the other one
    Point2D vertex;              ///< the shared endpoint, else the midpoint of the two starts
    bool sharedVertex = false;   ///< the lines meet at an endpoint
    Point2D bisector;            ///< unit direction from the vertex between the rays
    bool hasBisector = false;    ///< bisector is meaningful (sharedVertex and not opposite)
};
enum class AngleDimensionProblem { None, NotTwoLines, Degenerate, Parallel };

/// The angle dimension the dimension tool places between two lines: the
/// interior angle at their shared endpoint when they have one (with the
/// supplementary flag when that differs from the direction-vector angle),
/// else the direction-vector angle. Parallel lines have no angle to
/// dimension.
HOBBYCAD_EXPORT AngleDimensionProblem angleDimensionBetweenLines(const Entity& a, const Entity& b,
                                                                 AngleDimension& out);

/// Re-read an angle dimension from where its label sits: the value is the
/// sector (bounded by the two lines' rays through `vertex`) that contains
/// the label, and `supplementary` says whether that differs from the
/// direction-vector angle. False when either line collapses at the vertex.
HOBBYCAD_EXPORT bool angleFromLabelSide(const Entity& e1, const Entity& e2,
                                        const Point2D& vertex, const Point2D& labelPos,
                                        double& value, bool& supplementary);

/// True when `c` is the FixedPoint pinning point `pointIndex` of `entityId`.
HOBBYCAD_EXPORT bool isFixedPointOn(const Constraint& c, int entityId, int pointIndex);

/// True when `c` is a Distance dimension naming point `pointIndex` of
/// `entityId`, so that point is placed by the dimension. An ellipse's axis
/// point (1 major, 2 minor) dimensioned this way owns that axis's length.
HOBBYCAD_EXPORT bool dimensionDrivesPoint(const Constraint& c, int entityId, int pointIndex);

/// A FixedPoint constraint pinning one point (enabled, driving, no label).
HOBBYCAD_EXPORT Constraint makeFixedPoint(int id, int entityId, int pointIndex);

/// Assemble a constraint the way the dimension and constraint tools do:
/// enabled and satisfied, the given operands, value, label position,
/// driving flag and supplementary flag. For a Radius or Diameter on a
/// circle (`firstEntity`), labelAngle is set from the label's direction
/// from the center so the label keeps its bearing as the circle moves.
HOBBYCAD_EXPORT Constraint makeDimensionConstraint(int id, ConstraintType type,
                                                   const std::vector<int>& entityIds,
                                                   const std::vector<int>& pointIndices,
                                                   double value, const Point2D& labelPos,
                                                   bool driving, bool supplementary,
                                                   const Entity* firstEntity);

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

/// Entity-finder overload: avoids requiring a full entity vector.
/// The caller provides a function that maps entity ID -> const Entity*.
HOBBYCAD_EXPORT bool getConstraintEndpoints(
    const Constraint& constraint,
    EntityFinder findEntity,
    Point2D& p1, Point2D& p2);

// =====================================================================
//  Constraint Utility Functions
// =====================================================================

/// Find the constraint with a given ID in a vector.
/// @return Pointer to the constraint, or nullptr if not found.
HOBBYCAD_EXPORT const Constraint* findConstraintById(const std::vector<Constraint>& constraints, int id);
HOBBYCAD_EXPORT Constraint* findConstraintById(std::vector<Constraint>& constraints, int id);

/// Highest constraint id in the container plus one (1 when empty).
HOBBYCAD_EXPORT int nextFreeConstraintId(const std::vector<Constraint>& constraints);

/// Get the set of entity IDs that have at least one enabled driving constraint
HOBBYCAD_EXPORT std::unordered_set<int> getConstrainedEntityIds(const std::vector<Constraint>& constraints);

/// Compute the current geometric value of a driven (non-driving) constraint
/// by inspecting the referenced entity geometry.
/// Returns the value (distance in mm, radius in mm, angle in degrees, etc.)
HOBBYCAD_EXPORT double computeDrivenValue(
    const Constraint& constraint,
    const std::vector<Entity>& entities);

/// Entity-finder overload: avoids copying the full entity vector.
HOBBYCAD_EXPORT double computeDrivenValue(
    const Constraint& constraint,
    EntityFinder findEntity);

}  // namespace sketch
}  // namespace hobbycad

#endif  // HOBBYCAD_SKETCH_CONSTRAINT_H
