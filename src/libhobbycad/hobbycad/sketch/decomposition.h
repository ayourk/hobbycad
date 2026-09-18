// =====================================================================
//  src/libhobbycad/hobbycad/sketch/decomposition.h — Entity decomposition
// =====================================================================
//
//  Decomposes compound entities (Rectangle, Parallelogram, Polygon)
//  into primitive Lines + geometric Constraints + a named Group.
//
//  This is a pure domain operation: no GUI dependencies.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_SKETCH_DECOMPOSITION_H
#define HOBBYCAD_SKETCH_DECOMPOSITION_H

#include "../core.h"
#include "entity.h"
#include "constraint.h"
#include "dimension_field.h"
#include "group.h"
#include "solver.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace hobbycad {
namespace sketch {

// =====================================================================
//  Decomposition Result
// =====================================================================

/// Result of decomposing a compound entity
struct DecompositionResult {
    bool success = false;
    std::vector<Entity> entities;          ///< Lines + optional construction Circle
    std::vector<Constraint> constraints;   ///< Coincident, Equal, Parallel, etc.
    Group group;                           ///< Named group containing all produced IDs
};

// =====================================================================
//  Decomposition Function
// =====================================================================

/// Decompose a compound entity into primitive Lines + Constraints + Group.
///
/// Handles Rectangle, Parallelogram, and Polygon (both regular and freeform).
/// Returns a DecompositionResult; the caller is responsible for inserting
/// the entities/constraints/group into its own data structures.
///
/// @param compound       The compound entity to decompose
/// @param lockedDims     Locked dimension fields (field -> value) for constraint creation
/// @param nextEntityId   Callable returning the next unique entity ID
/// @param nextConstraintId Callable returning the next unique constraint ID
/// @param groupId        The group ID to assign
/// @param existingGroups Existing groups (for serial numbering the group name)
/// @param typeName       Human-readable type name (e.g., "Rectangle", "Polygon")
/// @param isFreeform     For Polygon: true = freeform (no construction circle, no Equal)
HOBBYCAD_EXPORT DecompositionResult decomposeEntity(
    const Entity& compound,
    const LockedDims& lockedDims,
    std::function<int()> nextEntityId,
    std::function<int()> nextConstraintId,
    int groupId,
    const std::vector<Group>& existingGroups,
    const std::string& typeName,
    bool isFreeform = false);

/// A solve read-out that counts compound entities honestly. The solver does
/// not model rectangles, parallelograms or polygons, so a floating rectangle
/// reports "dof 0 (fully constrained)" from a plain solve. This decomposes
/// each compound into the edges and constraints the GUI would store, solves
/// that system and returns its report; a system with no compounds returns
/// `solved` unchanged. Non-compound entities pass through.
HOBBYCAD_EXPORT SolveResult solveReportWithDecomposedCompounds(
    const std::vector<Entity>& entities,
    const std::vector<Constraint>& constraints,
    const SolveResult& solved);

// =====================================================================
//  2D Sweep (what used to be called a slot)
// =====================================================================

/// How a sweep finishes at a free end.
enum class SweepEndStyle {
    Round,   ///< A semicircle, the usual case
    Flat,    ///< A straight line across, square to the path
};

/// Decompose a 2D sweep into ordinary geometry, constraints and a group.
///
/// Width rule (Aaron, 2026-08-28: "slot width/2 is less than or equal to
/// the arc radius"): on an arc path a half-width up to and INCLUDING the
/// path radius is built; at equality the inner side is a Point at the
/// center and the two caps meet there (the 180-degree slot). Past it the
/// inner edge would turn inside out and nothing is built (success false).
///
/// Aaron, 2026-08-28: *"I wouldn't call it a slot object anymore; more like
/// a 2D sweep... I am expecting the slot to be decomposed, but I see it
/// could still be grouped. I also like the idea of adding appropriate
/// constraints to make the decompose possible."*
///
/// This is what both mainstream parametric CADs already do. FreeCAD's own
/// documentation calls a slot *"a closed polyline consisting of two
/// semicircles connected by two parallel straight lines"*, and Fusion's
/// slot tools produce arcs, lines, construction geometry and constraints.
/// Neither keeps a slot object.
///
/// Decomposing has a further advantage that only showed up after trying the
/// alternative: there is no OUTLINE to compute, so no polygon union is
/// needed. The geometry is the outline. The union is what defeated
/// `slotOutline()`.
///
/// What it produces, for a path of one line or one arc:
///
///   * two offset elements, one either side of the path
///   * two end caps: arcs when Round, lines when Flat
///   * Coincident at each of the four junctions, so the outline closes
///   * Parallel and Equal (a line path) or Concentric (an arc path),
///     so the two sides stay a matched pair
///   * Tangent where a round cap meets a side, so the join stays smooth
///   * Equal between the two caps, so both ends keep one width
///
/// The path itself is NOT included; the caller decides whether to keep it
/// as construction geometry. It usually should.
///
/// @param path            One Line or one Arc. Longer paths need the
///                        offsets trimmed at each joint and are not
///                        handled yet.
/// @param halfWidth       Half the sweep's width; the cap radius.
/// @param ends            Round or Flat.
/// @param nextEntityId    Callable returning the next unique entity ID.
/// @param nextConstraintId Callable returning the next unique constraint ID.
/// @param groupId         The group ID to assign.
/// @param existingGroups  For serial-numbering the group's name.
HOBBYCAD_EXPORT DecompositionResult decomposeSweep(
    const Entity& path,
    double halfWidth,
    SweepEndStyle ends,
    std::function<int()> nextEntityId,
    std::function<int()> nextConstraintId,
    int groupId,
    const std::vector<Group>& existingGroups);

/// What applySweep() added, for the caller's undo record and report.
struct SweepApplied {
    bool success = false;
    std::vector<Entity> entities;        ///< the pieces (sides, caps, construction)
    std::vector<Constraint> constraints; ///< the ties between them
    Group group;                         ///< holds the pieces and the path
    bool pathConverted = false;          ///< the path was real geometry, now construction
    Entity pathBefore;                   ///< the path as it was, when pathConverted
};

/// Commit a sweep of the path `pathId` into the caller's containers: the
/// decomposition's pieces and constraints are appended, the path becomes
/// construction (it is the centerline, a guide rather than an edge), the
/// group holds the pieces and the path, and every member's groupId points
/// at it. A template over the containers so the canvas (QVector of derived
/// types) and the CLI (std::vector) commit identically; the ids come from
/// the caller's sequences. Nothing is touched when the sweep cannot be built.
template <class Entities, class Constraints, class Groups>
SweepApplied applySweep(Entities& entities, Constraints& constraints, Groups& groups,
                        int pathId, double halfWidth, SweepEndStyle ends,
                        std::function<int()> nextEntityId,
                        std::function<int()> nextConstraintId,
                        int groupId)
{
    SweepApplied out;
    const Entity* src = nullptr;
    for (const auto& e : entities) if (e.id == pathId) { src = &e; break; }
    if (!src) return out;
    const Entity path = *src;   // copy: the container grows below
    const std::vector<Group> existing(groups.begin(), groups.end());
    const DecompositionResult d = decomposeSweep(path, halfWidth, ends,
                                                 std::move(nextEntityId), std::move(nextConstraintId),
                                                 groupId, existing);
    if (!d.success) return out;

    for (const Entity& e : d.entities) entities.push_back(typename Entities::value_type(e));
    for (const Constraint& c : d.constraints) constraints.push_back(typename Constraints::value_type(c));
    for (auto& e : entities) {
        if (e.id != pathId) continue;
        if (!e.isConstruction) {
            out.pathBefore = e;
            e.isConstruction = true;
            out.pathConverted = true;
        }
        break;
    }
    Group g = d.group;
    g.entityIds.push_back(pathId);
    groups.push_back(typename Groups::value_type(g));
    for (auto& e : entities) if (g.containsEntity(e.id)) e.groupId = g.id;

    out.entities = d.entities;
    out.constraints = d.constraints;
    out.group = g;
    out.success = true;
    return out;
}


}  // namespace sketch
}  // namespace hobbycad

#endif  // HOBBYCAD_SKETCH_DECOMPOSITION_H
