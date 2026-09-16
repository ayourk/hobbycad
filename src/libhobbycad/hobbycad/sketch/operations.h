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
#include "constraint.h"
#include "group.h"
#include "../geometry/types.h"

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

namespace hobbycad {
namespace sketch {

struct Constraint;   // full type in constraint.h; forward-declared for signatures

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
    int side = 0;             ///< The side actually used (+1/-1), for associativity
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

/// Re-derive an associative offset child from its parent, in place, using the
/// child's stored offsetDistance and offsetSide. Preserves the child's id and
/// its offset link fields; only the geometry (type, points, radius, angles)
/// is regenerated. Returns false if the parent type is not offsettable or the
/// result would be degenerate (e.g. a negative circle radius).
HOBBYCAD_EXPORT bool updateOffsetFromParent(Entity& child, const Entity& parent);

/// Re-derive a projected entity from its source, projecting the source's
/// points through the source plane into world space and onto the target plane.
/// Point-list entities only for now (Line/Point/Spline/Polygon); Circle/Arc
/// project to ellipses in general and are deferred. Returns false if the source
/// type is not yet projectable.
/// Geometric auto-constrain: scan existing sketch geometry and return the
/// constraints that obviously apply: coincident endpoints, horizontal/vertical
/// lines, parallel/perpendicular line pairs, and equal-length/-radius pairs.
/// Each candidate is added only if the solver says it will not over-constrain the
/// sketch, so the result is always safe to apply. Ids are assigned from
/// nextConstraintId (advanced as it goes). Not AI, unlike Fusion's: a plain
/// geometric pass.
HOBBYCAD_EXPORT std::vector<Constraint> autoConstrain(
    const std::vector<Entity>& entities,
    const std::vector<Constraint>& existing,
    int& nextConstraintId,
    double tolDist = 1e-3,
    double tolAngleDeg = 1.0);

HOBBYCAD_EXPORT bool updateProjectionFromSource(Entity& child, const Entity& source,
                                                const PlaneBasis& sourcePlane,
                                                const PlaneBasis& targetPlane);

/// Source lookup for a projection whose source lives in another sketch: fill
/// `out` and its plane basis for (sketchId, entityId); false when unknown.
using ProjectionSourceResolver =
    std::function<bool(int sketchId, int entityId, Entity& out, PlaneBasis& basis)>;

/// The post-solve pass both front ends need: every slot follows its path,
/// every associative offset its parent, every projection its source. A
/// same-sketch source uses `targetBasis` on both sides; a cross-sketch source
/// goes through `resolve` when one is supplied. Returns how many slots followed
/// their centerline.
HOBBYCAD_EXPORT int rederiveDependents(std::vector<Entity>& entities, const PlaneBasis& targetBasis,
                                       const ProjectionSourceResolver& resolve = {});

/// Build a projected child entity from `source` (which lives in the sketch
/// identified by `sourceSketchId`): a copy with a fresh id `newId`, its
/// projection-source link fields set, construction/centerline/selection reset,
/// and its points projected from the source plane onto the target plane.
/// Returns false and leaves `child` unusable if the source type is not yet
/// projectable (Circle/Arc project to ellipses and are deferred).
HOBBYCAD_EXPORT bool makeProjectionChild(Entity& child, const Entity& source,
                                         int sourceSketchId, int newId,
                                         const PlaneBasis& sourcePlane,
                                         const PlaneBasis& targetPlane);

/// Tessellate a spline's control points into a smooth polyline via Catmull-Rom
/// interpolation (the curve passes through every control point). segmentsPerSpan
/// samples per control-point interval. Fewer than 3 control points are returned
/// unchanged (a line or a single point has nothing to smooth).
HOBBYCAD_EXPORT std::vector<Point3> tessellateSpline(
    const std::vector<Point3>& controlPoints, int segmentsPerSpan = 12,
    bool bezier = false, bool closed = false);

/// Tessellate a RATIONAL piecewise-cubic Bezier: the control polygon plus one
/// weight per control point (3N+1 values). Each segment is C(t) =
/// sum w_i B_i(t) P_i / sum w_i B_i(t) (weighted de Casteljau). With all weights
/// equal this matches tessellateSpline(..., bezier=true). Weights must be > 0.
HOBBYCAD_EXPORT std::vector<Point3> tessellateRationalSpline(
    const std::vector<Point3>& controlPoints, const std::vector<double>& weights,
    int segmentsPerSpan = 12);

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

// =====================================================================
//  Adding a group
// =====================================================================

enum class AddGroupProblem {
    None,
    Empty,               ///< no entities, constraints or child groups
    MissingEntity,       ///< a member entity does not exist (id in AddGroupResult::id)
    MissingConstraint,   ///< a member constraint does not exist (id)
    MissingChildGroup,   ///< a child group does not exist yet (id)
    NameInUse,           ///< another group has the name (that group's id)
    IdInUse              ///< the requested id is taken (id)
};
struct AddGroupResult {
    AddGroupProblem problem = AddGroupProblem::None;
    int id = -1;   ///< the new group's id on success, else the id the problem is about
};

/// Validate and add a group: it needs at least one member; every member
/// must exist (a group naming things that are not there is what a browser
/// then looks up and does not find); the name must be unused, because names
/// are looked up; g.id > 0 asks for that id and must be free, g.id <= 0
/// takes the next free one. On success the children's parentGroupId and the
/// member entities' groupId point back at the group (nesting is recorded on
/// both sides; selection expansion and group drags key off Entity::groupId).
HOBBYCAD_EXPORT AddGroupResult addGroup(Group g, std::vector<Entity>& entities,
                                        const std::vector<Constraint>& constraints,
                                        std::vector<Group>& groups);

// =====================================================================
//  Deleting entities
// =====================================================================

/// What deleteEntities() removed besides the entities themselves.
struct DeleteReport {
    int constraintsDropped = 0;   ///< constraints that named a deleted entity
    int slotsUnlinked = 0;        ///< slots that followed a deleted path (shape kept)
    int groupsRemoved = 0;        ///< groups emptied by the delete
};

/// Remove the entities in `ids` with the cascade both front ends agree on:
/// a constraint naming any of them goes (the solver rejects a system that
/// names missing geometry, and leaving them is how a sketch becomes
/// unsolvable with no visible cause); a slot following one keeps its shape
/// but loses the link; the ids leave every group's member list, constraint
/// ids that no longer exist leave every group's constraint list, and a
/// group emptied by that is removed. Works on any containers of Entity,
/// Constraint and Group (or types derived from them), so the canvas and
/// the CLI call the same code on their own containers. Undo snapshots are
/// the caller's, taken before this.
template <class Entities, class Constraints, class Groups>
DeleteReport deleteEntities(Entities& entities, Constraints& constraints, Groups& groups,
                            const std::vector<int>& ids)
{
    DeleteReport report;
    auto gone = [&ids](int id) { return std::find(ids.begin(), ids.end(), id) != ids.end(); };

    for (auto& e : entities) {
        auto& paths = e.pathEntityIds;
        const auto before = paths.size();
        paths.erase(std::remove_if(paths.begin(), paths.end(), gone), paths.end());
        if (paths.size() != before) ++report.slotsUnlinked;
    }
    {
        const auto before = constraints.size();
        constraints.erase(std::remove_if(constraints.begin(), constraints.end(),
                              [&gone](const auto& c) {
                                  for (int id : c.entityIds) if (gone(id)) return true;
                                  return false;
                              }),
                          constraints.end());
        report.constraintsDropped = static_cast<int>(before - constraints.size());
    }
    entities.erase(std::remove_if(entities.begin(), entities.end(),
                       [&gone](const auto& e) { return gone(e.id); }),
                   entities.end());
    for (auto& g : groups) {
        g.entityIds.erase(std::remove_if(g.entityIds.begin(), g.entityIds.end(), gone),
                          g.entityIds.end());
        g.constraintIds.erase(std::remove_if(g.constraintIds.begin(), g.constraintIds.end(),
                                  [&constraints](int cid) {
                                      return std::none_of(constraints.begin(), constraints.end(),
                                          [cid](const auto& c) { return c.id == cid; });
                                  }),
                              g.constraintIds.end());
    }
    {
        const auto before = groups.size();
        groups.erase(std::remove_if(groups.begin(), groups.end(),
                         [](const auto& g) { return g.isEmpty(); }),
                     groups.end());
        report.groupsRemoved = static_cast<int>(before - groups.size());
    }
    return report;
}

/// The points of the intersections that involve `entityId`.
HOBBYCAD_EXPORT std::vector<Point2D> intersectionPointsTouching(const std::vector<Intersection>& all,
                                                                int entityId);

/// Where a click on a line asks it to be cut: the nearest intersection on
/// the line on either side of the click (one or two points, interior to the
/// line), so the piece under the cursor is what comes out. Empty for a
/// non-line or when nothing crosses it.
HOBBYCAD_EXPORT std::vector<Point2D> bracketingSplitPoints(const Entity& line,
                                                           const std::vector<Intersection>& all,
                                                           const Point2D& click);

/// A pair of entity endpoints to join with a Coincident constraint.
struct EndpointPair {
    int entityA = 0; int pointA = 0;
    int entityB = 0; int pointB = 0;
};

/// The corners a newly drawn line should be welded to: for each of its two
/// ends, the first endpoint of another line within `tolerance` that is not
/// already coincident with it (one weld per end; an end already welded to
/// the nearest candidate gets none). Empty when `newEntityId` is not a line.
HOBBYCAD_EXPORT std::vector<EndpointPair> proximityWelds(const std::vector<Entity>& entities,
                                                         const std::vector<Constraint>& constraints,
                                                         int newEntityId, double tolerance);

/// Find all entities connected to a starting entity
/// Uses BFS to traverse connected entities; two entities are connected when
/// any of their Entity::connectionPoints() coincide (so points and rectangle
/// corners join a chain, not only open-curve endpoints).
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
//  Cut/split constraint computation (shared by GUI and CLI)
// =====================================================================

/// Compute the constraints that keep a trim/extend/split result connected, so
/// the front ends behave identically:
///  - a Coincident between pieces that meet at a junction point (a split never
///    leaves the halves free to drift apart);
///  - a tie between ONE representative piece endpoint at a junction and any
///    OTHER entity that also meets it: a point-on-object (PointOnLine /
///    PointOnCircle / PointOnSpline, by the target's type) for a curve passing
///    through, or a Coincident for a point entity sitting there. Only one
///    endpoint is tied: joined pieces already carry the rest, and a lone
///    circle opened into a 360-degree arc must keep one end free.
///
/// @param pieces           New/modified entities to weld (their .id is used)
/// @param others           Candidate boundary entities (exclude the pieces and
///                         the original); pass empty for a plain join
/// @param junctionPoints   Where the cut/split happened; only endpoints there
///                         are welded
/// @param nextConstraintId Supplies fresh constraint ids
/// @param eps              Position tolerance
HOBBYCAD_EXPORT std::vector<Constraint> computeCutConstraints(
    const std::vector<Entity>& pieces,
    const std::vector<Entity>& others,
    const std::vector<Point2D>& junctionPoints,
    const std::function<int()>& nextConstraintId,
    double eps = 1e-6);

/// Carry an original entity's constraints onto the pieces that replaced it in a
/// trim or split, so a cut re-anchors what it can instead of dropping every
/// constraint that named the original:
///  - a point-anchored reference (Coincident, PointOnLine / PointOnCircle /
///    PointOnSpline, FixedPoint, and the like) moves to the piece that still
///    owns that point, and is dropped only when the cut removed the point;
///  - a whole-line orientation (Horizontal, Vertical, Parallel, Perpendicular,
///    Collinear, FixedAngle) is replicated onto every line piece, since each
///    collinear piece keeps the original's direction;
///  - any other constraint (a length or angular dimension, Equal, Tangent,
///    Midpoint, Symmetric, Concentric, curvature) is omitted, because the cut
///    changes what it would measure or how many pieces it would name.
///
/// @param constraints      Constraints that referenced original.id (others are
///                         ignored); each result carries a fresh id
/// @param original         The entity being replaced (its points resolve the
///                         point-anchored references)
/// @param pieces           The entities that replaced it (their .id is used)
/// @param nextConstraintId Supplies fresh constraint ids
/// @param eps              Position tolerance
HOBBYCAD_EXPORT std::vector<Constraint> remapCutConstraints(
    const std::vector<Constraint>& constraints,
    const Entity& original,
    const std::vector<Entity>& pieces,
    const std::function<int()>& nextConstraintId,
    double eps = 1e-6);

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

/// As above, and also refuses (success == false, errorMessage set,
/// `attachedEntityId` the culprit) when any entity in `all` that is not
/// among `entities` has a point at an interior junction: rejoining would
/// break that connection.
HOBBYCAD_EXPORT RejoinResult validateCollinearRejoin(
    const std::vector<Entity>& entities,
    const std::vector<Entity>& all,
    int* attachedEntityId,
    double angleTolerance = 0.001,
    double endpointTolerance = 1e-4);

}  // namespace sketch
}  // namespace hobbycad

#endif  // HOBBYCAD_SKETCH_OPERATIONS_H
