// =====================================================================
//  src/libhobbycad/hobbycad/sketch/transform.h — transforming entities as one unit
//  SPDX-License-Identifier: GPL-3.0-only
//  Part of HobbyCAD (ayourk/hobbycad)
//
//  Transform a set of sketch entities (a group's members, or any
//  selection) as one unit, with Onshape-style repair of the constraints
//  that do not survive the transform.
//
//  The model, decided 2026-09-03 (see HobbyCAD-outside/group-transforms-plan.md):
//    - Translate is rigid: every constraint type is translation-invariant,
//      so nothing is repaired and no solve is needed for the members.
//    - Rotate, Scale and Mirror are passive with repair:
//        rotate by 90/270  -> Horizontal and Vertical swap
//        rotate by 0/180   -> nothing
//        other angles      -> Horizontal/Vertical become Parallel/Perpendicular
//                             to a construction line that rotated with the set
//                             and is pinned by two FixedPoints, so the set's
//                             degrees of freedom are unchanged (Onshape: the
//                             construction geometry "maintains degrees of freedom");
//                             FixedAngle values turn with the set
//        scale             -> Distance/Radius/Diameter values inside the set
//                             scale with the geometry (angles do not)
//        mirror            -> Horizontal/Vertical survive an axis-aligned
//                             mirror; Symmetric/Midpoint whose axis lies
//                             outside the set make the mirror refuse
//    - Constraints that cross the set boundary are never edited here: the
//      caller re-solves, and the solver moves a free outsider or reports
//      the constraint it cannot keep.
//
//  Nothing here solves. The caller owns the solve, the undo record and the
//  group bookkeeping (adding the construction line to the group, if any).
// =====================================================================
#pragma once

#include <hobbycad/types.h>
#include <hobbycad/types.h>
#include <hobbycad/sketch/entity.h>
#include <hobbycad/sketch/constraint.h>
#include <hobbycad/sketch/group.h>
#include <hobbycad/sketch/solver.h>
#include <functional>
#include <utility>

#include <string>
#include <vector>

namespace hobbycad {
namespace sketch {

enum class GroupTransformKind { Translate, Rotate, Scale, Mirror };

struct GroupTransformParams {
    GroupTransformKind kind = GroupTransformKind::Translate;
    Point2D delta;                 ///< Translate: the offset
    double angleDeg = 0.0;         ///< Rotate: counter-clockwise degrees
    double factor = 1.0;           ///< Scale: multiplier (> 0)
    bool mirrorAcrossHorizontal = true; ///< Mirror: across the horizontal line through the center (flip y); false = vertical line (flip x)
    bool mirrorLineGiven = false;  ///< Mirror: across the line through mirrorA and mirrorB instead of an axis through the center
    Point2D mirrorA, mirrorB;
    ///< `delta` is also honored as a translation applied AFTER rotate/scale/mirror (default zero), so a
    ///< free-move gesture (turn, then drag) is one transform with one undo record.
    bool centerGiven = false;      ///< Rotate/Scale/Mirror: use `center`; otherwise the members' geometric center (memberCenter)
    Point2D center;
};

struct GroupTransformResult {
    bool applied = false;
    std::string refusal;                     ///< Non-empty when nothing was changed and why
    std::vector<int> changedEntityIds;       ///< Members whose geometry moved
    std::vector<int> changedConstraintIds;   ///< Constraints edited (type, value or label)
    std::vector<Entity> addedEntities;       ///< A construction reference line, when a non-right-angle rotation needed one; already appended to `entities`
    std::vector<Constraint> addedConstraints; ///< The two FixedPoints that pin that line (its orientation is the rotated frame); already appended to `constraints`
    std::vector<std::string> notes;          ///< One line per repair, for the report
    Point2D centerUsed;
};

/// Transform `memberIds` inside `entities`/`constraints` in place.
/// `nextEntityId` and `nextConstraintId` are consumed only if a construction line (and its two pins) is added.
/// Refusals leave both vectors untouched.
HOBBYCAD_EXPORT GroupTransformResult transformEntities(
    std::vector<Entity>& entities,
    std::vector<Constraint>& constraints,
    const std::vector<int>& memberIds,
    const GroupTransformParams& params,
    int nextEntityId,
    int nextConstraintId);

/// Apply the same transform to one point (a stored group pivot, a label): the
/// geometry a transform did to the set, done to a point that was not in it.
HOBBYCAD_EXPORT Point2D transformPoint(const Point2D& p, const GroupTransformParams& params, const Point2D& centerUsed);

/// The pivot a transform of this group uses when the caller gives none:
/// the group's stored pivot, else its geometric center. One rule for GUI and CLI.
HOBBYCAD_EXPORT Point2D effectivePivot(const Group& group, const std::vector<Entity>& entities);

/// Copies of a set: the member entities cloned with fresh ids (groupId -1;
/// a pathEntityIds entry inside the set follows the map, one outside is dropped),
/// and the constraints wholly inside the set cloned with remapped ids. A
/// constraint that crosses the set boundary is not copied: a copy never
/// half-belongs to anything.
struct CloneSetResult {
    std::vector<Entity> entities;
    std::vector<Constraint> constraints;
    std::vector<std::pair<int, int>> entityIdMap;      ///< old -> new
    std::vector<std::pair<int, int>> constraintIdMap;  ///< old -> new
    int nextEntityId = 0;                              ///< after the clones
    int nextConstraintId = 0;
};
HOBBYCAD_EXPORT CloneSetResult cloneSet(const std::vector<Entity>& entities,
                                        const std::vector<Constraint>& constraints,
                                        const std::vector<int>& memberIds,
                                        int nextEntityId, int nextConstraintId);

// ---- The scratch run both front ends make ------------------------------

/// A transform worked out on COPIES, kept only if the caller accepts it:
/// the entities and constraints after the move, the clones when it was a
/// copy, the ids the transform targeted (members, or their clones), and the
/// transform's own report.
struct TransformScratch {
    std::vector<Entity> entities;
    std::vector<Constraint> constraints;
    CloneSetResult clones;            ///< empty unless createCopy
    std::vector<int> targetIds;       ///< the members, or the clones' ids
    GroupTransformResult result;
    int nextEntityId = 0;             ///< after any clones and additions
    int nextConstraintId = 0;
};

/// Copy the sketch's entities and constraints, clone the members when
/// `createCopy`, and transform the targets. Nothing of the caller's is
/// touched; commit `entities` / `constraints` when `result.applied` and the
/// caller's own checks (a solve, an undo record) pass.
HOBBYCAD_EXPORT TransformScratch transformSet(const std::vector<Entity>& entities,
                                              const std::vector<Constraint>& constraints,
                                              const std::vector<int>& memberIds,
                                              const GroupTransformParams& params,
                                              bool createCopy,
                                              int nextEntityId, int nextConstraintId);

/// The group a copied whole group becomes: the source's record with a new
/// id, "<name> copy", the clones as members, no parent or children, and the
/// stored pivot carried through the transform.
HOBBYCAD_EXPORT Group makeCopyGroup(const Group& source, int newId, const CloneSetResult& clones,
                                    const GroupTransformParams& params, const Point2D& centerUsed);

// ---- Committing a transform: one backend for the CLI and the canvas ----

/// What a front end wants done with a transformed whole group's stored
/// pivot: Follow it through the transform (the default; a turn about itself
/// leaves it be), Clear it, or Set it to `pivotPoint` (then moved along).
enum class PivotUpdate { Follow, Clear, Set };

struct TransformCommitOptions {
    int homeGroupId = -1;       ///< the group the set belongs to, or -1: reference geometry and clones go here
    bool wholeGroup = false;    ///< homeGroupId is exactly the set: a copy gets its own "<name> copy" group, and the pivot rule applies
    PivotUpdate pivot = PivotUpdate::Follow;
    Point2D pivotPoint;         ///< for PivotUpdate::Set
};

/// Everything commitTransform() changed, as before/after pairs, so a front
/// end records undo and reports from this and never re-reads the model to
/// find out what happened.
struct TransformCommit {
    bool applied = false;
    std::string refusal;                 ///< why nothing changed (the solver named)
    SolveResult solve;                   ///< the solve of the committed state
    std::vector<std::pair<Entity, Entity>> modifiedEntities;        ///< before, after
    std::vector<std::pair<Constraint, Constraint>> modifiedConstraints;
    std::vector<Entity> addedEntities;   ///< clones and reference geometry, as committed
    std::vector<Constraint> addedConstraints;
    std::vector<int> cloneEntityIds;     ///< the clones among addedEntities
    bool copyGroupMade = false;
    Group copyGroup;
    bool pivotChanged = false;
    Group groupBefore, groupAfter;       ///< the whole group's record around a pivot change
};

/// True when two records of one entity carry the same geometry.
inline bool sameGeometry(const Entity& a, const Entity& b)
{
    if (a.points.size() != b.points.size()) return false;
    for (size_t i = 0; i < a.points.size(); ++i)
        if (a.points[i].x != b.points[i].x || a.points[i].y != b.points[i].y) return false;
    return a.radius == b.radius && a.startAngle == b.startAngle && a.sweepAngle == b.sweepAngle
        && a.majorRadius == b.majorRadius && a.minorRadius == b.minorRadius
        && a.ellipseRotation == b.ellipseRotation && a.textRotation == b.textRotation;
}

/// True when two records of one constraint agree on what a transform edits.
inline bool sameConstraintEdit(const Constraint& a, const Constraint& b)
{
    return a.type == b.type && a.entityIds == b.entityIds && a.pointIndices == b.pointIndices
        && a.value == b.value
        && a.labelPosition.x == b.labelPosition.x && a.labelPosition.y == b.labelPosition.y
        && a.anchorPoint.x == b.anchorPoint.x && a.anchorPoint.y == b.anchorPoint.y;
}

/// Commit a transform worked out by transformSet(): the ONE path for the
/// canvas and the CLI (Aaron: the two front ends have one backend to
/// contend with). It solves the scratch first and refuses the whole
/// transform, changing nothing, when a constraint cannot be kept; otherwise
/// every entity takes the solved geometry (outsiders the solve moved
/// included, which is intended), constraints follow, clones and any
/// reference geometry the transform added are appended and homed, a copied
/// whole group becomes "<name> copy", and the stored pivot is updated per
/// the options. A template over the callers' containers (QVector of derived
/// types, std::vector), like deleteEntities and applySweep. New group ids
/// come from `nextGroupId`, asked only when a group is made.
template <class Entities, class Constraints, class Groups>
TransformCommit commitTransform(Entities& entities, Constraints& constraints, Groups& groups,
                                const TransformScratch& scratch, bool createCopy,
                                const GroupTransformParams& params,
                                const TransformCommitOptions& opts,
                                std::function<int()> nextGroupId)
{
    TransformCommit out;
    const GroupTransformResult& res = scratch.result;
    if (!res.applied) { out.refusal = res.refusal; return out; }

    // Solve first: a transform the constraints cannot absorb is refused
    // whole rather than committed and left unsolvable.
    std::vector<Entity> ents = scratch.entities;
    const std::vector<Constraint>& cons = scratch.constraints;
    {
        Solver solver;
        out.solve = solver.solve(ents, cons);
    }
    if (!out.solve.success) {
        std::string which;
        for (int cid : out.solve.failedConstraintIds) which += (which.empty() ? "" : ", ") + std::to_string(cid);
        out.refusal = "the solver cannot keep constraint" +
                      std::string(out.solve.failedConstraintIds.size() == 1 ? " " : "s ") +
                      (which.empty() ? std::string("(unknown)") : which) +
                      " with the set moved" +
                      (out.solve.errorMessage.empty() ? "" : " (" + out.solve.errorMessage + ")");
        return out;
    }

    // Where clones and reference geometry go.
    const Group* source = nullptr;
    if (opts.wholeGroup && opts.homeGroupId >= 0)
        for (const auto& g : groups) if (g.id == opts.homeGroupId) { source = &g; break; }
    int home = opts.homeGroupId;
    if (createCopy) {
        if (source) {
            out.copyGroup = makeCopyGroup(*source, nextGroupId(), scratch.clones, params, res.centerUsed);
            out.copyGroupMade = true;
            home = out.copyGroup.id;
        } else {
            home = -1;
        }
    }
    auto isClone = [&](int id) {
        for (const Entity& c : scratch.clones.entities) if (c.id == id) return true;
        return false;
    };
    auto isAdded = [&](int id) {
        for (const Entity& a : res.addedEntities) if (a.id == id) return true;
        return false;
    };

    for (const Entity& after : ents) {
        bool found = false;
        for (auto& live : entities) {
            if (live.id != after.id) continue;
            found = true;
            Entity& base = live;
            if (!sameGeometry(base, after)) {
                out.modifiedEntities.emplace_back(base, after);
                base = after;
            }
            break;
        }
        if (found) continue;
        Entity added = after;
        const bool clone = isClone(added.id);
        if (clone || isAdded(added.id)) added.groupId = home;
        entities.push_back(typename Entities::value_type(added));
        out.addedEntities.push_back(added);
        if (clone) out.cloneEntityIds.push_back(added.id);
    }
    for (const Constraint& after : cons) {
        bool found = false;
        for (auto& live : constraints) {
            if (live.id != after.id) continue;
            found = true;
            Constraint& base = live;
            if (!sameConstraintEdit(base, after)) {
                out.modifiedConstraints.emplace_back(base, after);
                base = after;
            }
            break;
        }
        if (found) continue;
        constraints.push_back(typename Constraints::value_type(after));
        out.addedConstraints.push_back(after);
    }

    // Group bookkeeping for the reference geometry the transform added.
    if (out.copyGroupMade) {
        for (const Entity& a : res.addedEntities)
            if (!hobbycad::contains(out.copyGroup.entityIds, a.id)) out.copyGroup.entityIds.push_back(a.id);
        for (const Constraint& c : res.addedConstraints)
            if (!hobbycad::contains(out.copyGroup.constraintIds, c.id)) out.copyGroup.constraintIds.push_back(c.id);
        groups.push_back(typename Groups::value_type(out.copyGroup));
    } else if (home >= 0) {
        for (auto& g : groups) {
            if (g.id != home) continue;
            for (const Entity& a : res.addedEntities)
                if (!hobbycad::contains(g.entityIds, a.id)) g.entityIds.push_back(a.id);
            for (const Constraint& c : res.addedConstraints)
                if (!hobbycad::contains(g.constraintIds, c.id)) g.constraintIds.push_back(c.id);
            break;
        }
    }

    // The stored pivot of a transformed whole group.
    if (!createCopy && opts.wholeGroup && opts.homeGroupId >= 0) {
        for (auto& g : groups) {
            if (g.id != opts.homeGroupId) continue;
            const Group before = g;
            switch (opts.pivot) {
            case PivotUpdate::Clear:
                g.hasPivot = false;
                break;
            case PivotUpdate::Set:
                g.hasPivot = true;
                g.pivot = transformPoint(opts.pivotPoint, params, res.centerUsed);
                break;
            case PivotUpdate::Follow:
                if (g.hasPivot) g.pivot = transformPoint(g.pivot, params, res.centerUsed);
                break;
            }
            if (g.hasPivot != before.hasPivot || g.pivot.x != before.pivot.x || g.pivot.y != before.pivot.y) {
                out.pivotChanged = true;
                out.groupBefore = before;
                out.groupAfter = g;
            }
            break;
        }
    }

    out.applied = true;
    return out;
}

/// True when every entity the constraint names is in `memberIds`.
HOBBYCAD_EXPORT bool constraintInsideSet(const Constraint& c, const std::vector<int>& memberIds);

/// Geometric center of the members: the length-weighted center of their
/// curves (lines by length, circles by circumference, arcs by arc length),
/// which is the rectangle's center and, unlike a bounding-box center, does not
/// drift toward a lone far corner in an asymmetric set. Points count only when
/// the set has no curves at all.
HOBBYCAD_EXPORT Point2D memberCenter(const std::vector<Entity>& entities, const std::vector<int>& memberIds);

} // namespace sketch
} // namespace hobbycad
