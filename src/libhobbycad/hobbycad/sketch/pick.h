// =====================================================================
//  src/libhobbycad/hobbycad/sketch/pick.h — what a click in a sketch hits
// =====================================================================
//
//  Capability tier of the front-end support layer. The hit tests a sketch
//  view needs (an entity, a grab handle, a point, a Bezier leg, a tangent
//  contact, a slot anchor, a midpoint grip) and the order in which the
//  Select tool tries them. A front end supplies what only it can measure:
//  where it drew group glyphs and constraint labels, and how large its text
//  is.
//
//  The pickers are templates over the entity, group and constraint
//  containers, so a front end keeping its own element types (derived from
//  Entity, Group and Constraint) and containers need not copy them.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_SKETCH_PICK_H
#define HOBBYCAD_SKETCH_PICK_H

#include "../core.h"
#include "../types.h"
#include "constraint.h"
#include "entity.h"
#include "group.h"
#include "queries.h"
#include "../geometry/utils.h"

#include <functional>
#include <vector>

namespace hobbycad {
namespace sketch {

/// What the Select tool may pick.
enum class SelectFilter {
    All,
    PointsOnly,   ///< points only; curves are not selectable
    CurvesOnly,   ///< curves only; points are not selectable
};

/// How near a click must be, in pixels.
struct PickTolerances {
    double entity = 5.0;      ///< a curve or segment
    double point = 7.0;       ///< any point
    double handle = 6.0;      ///< a grab handle of the selection
    double bezierLeg = 6.0;   ///< a Bezier control-polygon leg
    double grip = 8.0;        ///< a midpoint grip, a slot anchor, a tangent contact

    /// The same distances in sketch units at `pixelsPerUnit`.
    PickTolerances inSketchUnits(double pixelsPerUnit) const
    {
        const double k = pixelsPerUnit > 0.0 ? 1.0 / pixelsPerUnit : 1.0;
        return {entity * k, point * k, handle * k, bezierLeg * k, grip * k};
    }
};

/// A front end's own test for text, whose extent depends on its fonts;
/// null means text is never hit.
using TextHitTest = std::function<bool(const Entity& text, const Point2D& at)>;

/// What a pick found.
enum class PickKind {
    None,             ///< empty space
    Handle,           ///< a grab handle: id, index
    GroupGlyph,       ///< a group's glyph (front end): id is the group
    ConstraintLabel,  ///< a constraint's label (front end): id is the constraint
    ConstraintGlyph,  ///< a constraint's glyph chip (front end): id
    Point,            ///< a point: id, index
    BezierLeg,        ///< a Bezier leg: id, index and index2 its two ends
    TangentContact,   ///< a tangent contact dot: id is the circle
    SlotAnchor,       ///< a slot's anchor point: id, index
    Midpoint,         ///< a midpoint grip: id
    Entity,           ///< an entity: id
};

struct PickResult {
    PickKind kind = PickKind::None;
    int id = -1;
    int index = -1;
    int index2 = -1;

    bool hit() const { return kind != PickKind::None; }
};

/// Hits only the front end can measure, found before the pick.
struct FrontEndHits {
    int groupGlyph = -1;
    int constraintLabel = -1;
    int constraintGlyph = -1;
};

/// Everything the Select tool's pick reads besides the sketch.
struct SelectPick {
    Point2D at;
    PickTolerances tolerances;          ///< in sketch units
    SelectFilter filter = SelectFilter::All;
    int primaryId = -1;                 ///< the primary selected entity
    int enteredGroupId = -1;            ///< the group being edited inside, or -1
    std::vector<int> pointOwners;       ///< entities with a selected point
    FrontEndHits frontEnd;
    TextHitTest textHit;
};

// ---- Per entity ---------------------------------------------------------

/// True when `at` is on `e` within `tolerance`.
HOBBYCAD_EXPORT bool entityHit(const Entity& e, const Point2D& at, double tolerance,
                               const TextHitTest& textHit);

/// The midpoint grip of a line or an arc.
HOBBYCAD_EXPORT bool midpointGrip(const Entity& e, Point2D& out);

/// The nearest of a slot's anchor points within `tolerance`, or -1.
HOBBYCAD_EXPORT int nearestSlotAnchor(const Entity& slot, const Point2D& at, double tolerance);

/// The nearest control-polygon leg of a Bezier spline within `tolerance`;
/// `distance` is updated when a nearer one is found.
HOBBYCAD_EXPORT bool nearestBezierLeg(const Entity& spline, const Point2D& at, double& distance,
                                      int& i0, int& i1);

/// The line and circle of a tangency, when the constraint is one between
/// them; the returned pointers index `line` and `circle`.
HOBBYCAD_EXPORT bool tangentLineCircle(const Entity* a, const Entity* b, const Entity*& line,
                                       const Entity*& circle);

// ---- Over a sketch --------------------------------------------------------

template <class Entities>
const Entity* findEntity(const Entities& entities, int id)
{
    for (const auto& e : entities) {
        if (e.id == id) return &e;
    }
    return nullptr;
}

/// True for the construction lines of a sweep-angle rig, which are not
/// picked on their own.
template <class Groups>
bool inSweepAngleRig(const Groups& groups, const Entity& e)
{
    if (e.type != EntityType::Line) return false;
    for (const Group& g : groups) {
        if (isSweepAngleGroup(g) && g.containsEntity(e.id)) return true;
    }
    return false;
}

/// The top-most entity at `at` (the last drawn wins), or -1.
template <class Entities, class Groups>
int pickEntity(const Entities& entities, const Groups& groups, const Point2D& at,
               double tolerance, const TextHitTest& textHit = {})
{
    for (auto it = entities.rbegin(); it != entities.rend(); ++it) {
        const Entity& e = *it;
        if (inSweepAngleRig(groups, e)) continue;
        if (entityHit(e, at, tolerance, textHit)) return e.id;
    }
    return -1;
}

/// The nearest point of any entity (text and dimensions have none to pick).
template <class Entities>
PickResult pickPoint(const Entities& entities, const Point2D& at, double tolerance)
{
    PickResult r;
    double best = tolerance;
    for (const Entity& e : entities) {
        if (e.type == EntityType::Text || e.type == EntityType::Dimension) continue;
        for (int i = 0; i < static_cast<int>(e.points.size()); ++i) {
            const Point2D p = e.points[static_cast<std::size_t>(i)];
            const double d = geometry::lineLength(p, at);
            if (d < best) {
                best = d;
                r = {PickKind::Point, e.id, i, -1};
            }
        }
    }
    return r;
}

/// A grab handle of the primary selection. A primary in a group (not being
/// edited inside) offers every member's handles, the nearest winning;
/// otherwise the primary's first handle within reach. A sweep-angle rig's
/// construction lines offer none.
template <class Entities, class Groups>
PickResult pickHandle(const Entities& entities, const Groups& groups, int primaryId,
                      int enteredGroupId, const Point2D& at, double tolerance)
{
    PickResult r;
    const Entity* primary = findEntity(entities, primaryId);
    if (!primary) return r;
    if (primary->groupId >= 0 && enteredGroupId < 0) {
        double best = tolerance;
        for (const Entity& e : entities) {
            if (e.groupId != primary->groupId) continue;
            for (int i = 0; i < static_cast<int>(e.points.size()); ++i) {
                const Point2D p = e.points[static_cast<std::size_t>(i)];
                const double d = geometry::lineLength(p, at);
                if (d < best) {
                    best = d;
                    r = {PickKind::Handle, e.id, i, -1};
                }
            }
        }
    } else {
        for (int i = 0; i < static_cast<int>(primary->points.size()); ++i) {
            const Point2D p = primary->points[static_cast<std::size_t>(i)];
            if (geometry::lineLength(p, at) < tolerance) {
                r = {PickKind::Handle, primary->id, i, -1};
                break;
            }
        }
    }
    if (r.hit()) {
        const Entity* owner = findEntity(entities, r.id);
        if (owner && inSweepAngleRig(groups, *owner)) r = PickResult();
    }
    return r;
}

/// A leg of a Bezier spline whose control points are showing (the primary,
/// or one with a selected point).
template <class Entities>
PickResult pickBezierLeg(const Entities& entities, int primaryId,
                         const std::vector<int>& pointOwners, const Point2D& at,
                         double tolerance)
{
    PickResult r;
    double best = tolerance;
    for (const Entity& e : entities) {
        if (e.type != EntityType::Spline || !e.splineBezier) continue;
        bool showing = e.id == primaryId;
        for (int owner : pointOwners) showing = showing || owner == e.id;
        if (!showing) continue;
        int i0 = -1, i1 = -1;
        if (nearestBezierLeg(e, at, best, i0, i1)) r = {PickKind::BezierLeg, e.id, i0, i1};
    }
    return r;
}

/// The dot where a line held tangent to a circle would touch it, when the
/// touch lies past the line's ends; it picks the circle.
template <class Entities, class Constraints>
PickResult pickTangentContact(const Entities& entities, const Constraints& constraints,
                              const Point2D& at, double tolerance)
{
    for (const Constraint& c : constraints) {
        if (!c.enabled || c.type != ConstraintType::Tangent || c.entityIds.size() < 2) continue;
        const Entity* line = nullptr;
        const Entity* circle = nullptr;
        if (!tangentLineCircle(findEntity(entities, c.entityIds[0]),
                               findEntity(entities, c.entityIds[1]), line, circle)) {
            continue;
        }
        const auto dot = offSegmentTangentPoint(*line, *circle);
        if (dot && geometry::lineLength(*dot, at) <= tolerance) {
            return {PickKind::TangentContact, circle->id};
        }
    }
    return {};
}

/// The nearest midpoint grip.
template <class Entities>
PickResult pickMidpoint(const Entities& entities, const Point2D& at, double tolerance)
{
    PickResult r;
    double best = tolerance;
    for (const Entity& e : entities) {
        Point2D mid;
        if (!midpointGrip(e, mid)) continue;
        const double d = geometry::lineLength(mid, at);
        if (d <= best) {
            best = d;
            r = {PickKind::Midpoint, e.id};
        }
    }
    return r;
}

/// What a Select-tool click hits, in the order it is tried: a handle of the
/// selection, a group glyph, a constraint label or glyph, a point, a Bezier
/// leg, a tangent contact, a slot anchor, a midpoint grip, an entity. The
/// filter keeps points or curves out.
template <class Entities, class Groups, class Constraints>
PickResult pickForSelect(const Entities& entities, const Groups& groups,
                         const Constraints& constraints, const SelectPick& in)
{
    const PickTolerances& tol = in.tolerances;
    const bool points = in.filter != SelectFilter::CurvesOnly;
    const bool curves = in.filter != SelectFilter::PointsOnly;

    PickResult r = pickHandle(entities, groups, in.primaryId, in.enteredGroupId, in.at,
                              tol.handle);
    if (r.hit()) return r;
    if (in.frontEnd.groupGlyph >= 0) return {PickKind::GroupGlyph, in.frontEnd.groupGlyph};
    if (in.frontEnd.constraintLabel >= 0) {
        return {PickKind::ConstraintLabel, in.frontEnd.constraintLabel};
    }
    if (in.frontEnd.constraintGlyph >= 0) {
        return {PickKind::ConstraintGlyph, in.frontEnd.constraintGlyph};
    }
    if (points) {
        r = pickPoint(entities, in.at, tol.point);
        if (r.hit()) return r;
        r = pickBezierLeg(entities, in.primaryId, in.pointOwners, in.at, tol.bezierLeg);
        if (r.hit()) return r;
    }
    if (curves) {
        r = pickTangentContact(entities, constraints, in.at, tol.grip);
        if (r.hit()) return r;
    }
    const int under = pickEntity(entities, groups, in.at, tol.entity, in.textHit);
    if (const Entity* e = findEntity(entities, under)) {
        if (e->type == EntityType::Slot) {
            const int anchor = nearestSlotAnchor(*e, in.at, tol.grip);
            if (anchor >= 0) return {PickKind::SlotAnchor, e->id, anchor};
        }
    }
    if (points) {
        r = pickMidpoint(entities, in.at, tol.grip);
        if (r.hit()) return r;
    }
    if (curves && under >= 0) return {PickKind::Entity, under};
    return {};
}

}  // namespace sketch
}  // namespace hobbycad

#endif  // HOBBYCAD_SKETCH_PICK_H
