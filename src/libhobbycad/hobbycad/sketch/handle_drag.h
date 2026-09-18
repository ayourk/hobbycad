// =====================================================================
//  src/libhobbycad/hobbycad/sketch/handle_drag.h — dragging a grab
//  handle of a sketch entity
// =====================================================================
//
//  Capability tier of the front-end support layer. One drag of one handle:
//  whether the entity may be dragged at all, where the handle goes for the
//  cursor (snapped, held to an axis, never collapsing an edge), the state an
//  opened full circle carries through the drag, and the undo record the
//  drag leaves behind. The geometry of moving a handle is
//  dragEntityHandle() (handles.h); the solve is the front end's.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_SKETCH_HANDLE_DRAG_H
#define HOBBYCAD_SKETCH_HANDLE_DRAG_H

#include "../core.h"
#include "../types.h"
#include "constraint.h"
#include "entity.h"
#include "group.h"
#include "handles.h"
#include "undo.h"

#include <optional>
#include <string>
#include <vector>

namespace hobbycad {

enum class SketchPlane;   // project.h

namespace sketch {

/// Why a handle may not be dragged.
enum class DragRefusal {
    None,
    Projected,   ///< projected geometry follows its source sketch
    Locked,      ///< the entity is in a locked group
};

/// Whether `e` may be dragged, among `groups`.
template <class Groups>
DragRefusal handleDragRefusal(const Entity& e, const Groups& groups)
{
    if (e.projectionSourceId >= 0) return DragRefusal::Projected;
    if (e.groupId < 0) return DragRefusal::None;
    const std::vector<Group> all(groups.begin(), groups.end());
    return isGroupChainLocked(e.groupId, all) ? DragRefusal::Locked : DragRefusal::None;
}

/// The sketch axis a dragged handle is held to.
enum class DragAxis {
    None,
    Horizontal,   ///< the sketch's u: the handle keeps its original v
    Vertical,     ///< the sketch's v: the handle keeps its original u
};

/// The sketch axis a model axis key ('X', 'Y' or 'Z') names on a plane, or
/// None when that axis is the plane's normal (or the plane is custom).
HOBBYCAD_EXPORT DragAxis dragAxisForKey(SketchPlane plane, char key);

/// The model axis letter a sketch axis is on a plane (dragAxisForKey()'s
/// inverse), or 0 for None.
HOBBYCAD_EXPORT char dragAxisLetter(SketchPlane plane, DragAxis axis);

/// Where a handle goes: the snapped cursor, or held to `axis` through
/// `original` (from the snapped cursor when `snapWithAxis`, else the raw
/// one).
HOBBYCAD_EXPORT Point2D handleDragTarget(const Point2D& raw, const Point2D& snapped,
                                         const Point2D& original, DragAxis axis,
                                         bool snapWithAxis);

/// One drag of one handle.
class HOBBYCAD_EXPORT HandleDrag {
public:
    /// Start dragging handle `handle` of `entityId`, keeping what the undo
    /// record needs: the entity, and when it is in a group every member and
    /// the group's constraints.
    template <class Entities, class Groups, class Constraints>
    bool begin(const Entities& entities, const Groups& groups, const Constraints& constraints,
               int entityId, int handle)
    {
        end();
        const Entity* e = find(entities, entityId);
        if (!e || handle < 0 || handle >= static_cast<int>(e->points.size())) return false;
        startWith(*e, handle);
        if (e->groupId >= 0) {
            for (const Entity& member : entities) {
                if (member.groupId == e->groupId) m_groupBefore.push_back(member);
            }
            for (const Group& g : groups) {
                if (g.id != e->groupId) continue;
                for (int cid : g.constraintIds) {
                    for (const Constraint& c : constraints) {
                        if (c.id == cid) m_groupConstraintsBefore.push_back(c);
                    }
                }
            }
        }
        return true;
    }

    void end();
    bool active() const { return m_active; }
    int entityId() const { return m_before.id; }
    int handle() const { return m_handle; }
    /// The handle where the drag began.
    Point2D original() const { return m_original; }
    /// The entity as it was when the drag began.
    const Entity& before() const { return m_before; }

    DragAxis axis() const { return m_axis; }
    void setAxis(DragAxis axis) { m_axis = axis; }

    /// Opening a full circle: a 360-degree arc whose two ends sit together
    /// at the cut, grabbed by one of them, shrinks from a full turn.
    bool opensFullArc() const { return m_opensFullArc; }
    double openArcFixedAngle() const { return m_openArcFixedAngle; }
    int openArcDraggedIndex() const { return m_openArcDraggedIndex; }
    double openArcSweep() const { return m_openArcSweep; }
    /// The opening's progress after a move (openFullArcByDrag's result).
    void setOpenArc(int draggedIndex, double sweep)
    {
        m_openArcDraggedIndex = draggedIndex;
        m_openArcSweep = sweep;
    }

    /// Where the handle goes for the cursor: handleDragTarget() held to
    /// `axis` (the drag's axis while the front end's axis key is held), then
    /// kept `minSeparation` from the entity's other points (and its
    /// group's), so no edge collapses.
    template <class Entities>
    Point2D target(const Entities& entities, const Point2D& raw, const Point2D& snapped,
                   DragAxis axis, bool snapWithAxis, double minSeparation) const
    {
        const Point2D proposed = handleDragTarget(raw, snapped, m_original, axis, snapWithAxis);
        std::vector<Point2D> others;
        for (const Entity& e : entities) {
            const bool self = e.id == m_before.id;
            if (!self && (m_before.groupId < 0 || e.groupId != m_before.groupId)) continue;
            for (int i = 0; i < static_cast<int>(e.points.size()); ++i) {
                if (self && i == m_handle) continue;
                others.push_back(e.points[static_cast<std::size_t>(i)]);
            }
        }
        return keepHandleApart(proposed, others, m_original, minSeparation);
    }

    /// The undo record for the drag so far: every group member (and group
    /// label) it changed as one step, or the dragged entity alone; nothing
    /// when nothing changed.
    template <class Entities, class Constraints>
    std::optional<UndoCommand> record(const Entities& entities,
                                      const Constraints& constraints) const
    {
        if (!m_active) return std::nullopt;
        if (m_groupBefore.empty()) {
            const Entity* now = find(entities, m_before.id);
            if (!now || !changed(m_before, *now)) return std::nullopt;
            return UndoCommand::modifyEntity(m_before, *now, "Resize");
        }
        std::vector<UndoCommand> steps;
        for (const Entity& was : m_groupBefore) {
            const Entity* now = find(entities, was.id);
            if (now && changed(was, *now)) {
                steps.push_back(UndoCommand::modifyEntity(was, *now, "Move group member"));
            }
        }
        for (const Constraint& was : m_groupConstraintsBefore) {
            for (const Constraint& now : constraints) {
                if (now.id == was.id && labelMoved(was, now)) {
                    steps.push_back(UndoCommand::modifyConstraint(was, now, "Move group label"));
                }
            }
        }
        if (steps.empty()) return std::nullopt;
        return UndoCommand::compound(steps, "Move group");
    }

    /// True when a drag changed what the undo record keeps of an entity.
    static bool changed(const Entity& before, const Entity& after);
    /// True when the drag is recorded for a whole group.
    bool coversGroup() const { return !m_groupBefore.empty(); }
    static bool labelMoved(const Constraint& before, const Constraint& after);

private:
    template <class Entities>
    static const Entity* find(const Entities& entities, int id)
    {
        for (const Entity& e : entities) {
            if (e.id == id) return &e;
        }
        return nullptr;
    }
    void startWith(const Entity& e, int handle);

    bool m_active = false;
    int m_handle = -1;
    Point2D m_original;
    Entity m_before;
    std::vector<Entity> m_groupBefore;
    std::vector<Constraint> m_groupConstraintsBefore;
    DragAxis m_axis = DragAxis::None;
    bool m_opensFullArc = false;
    double m_openArcFixedAngle = 0.0;
    int m_openArcDraggedIndex = -1;
    double m_openArcSweep = 0.0;
};

}  // namespace sketch
}  // namespace hobbycad

#endif  // HOBBYCAD_SKETCH_HANDLE_DRAG_H
