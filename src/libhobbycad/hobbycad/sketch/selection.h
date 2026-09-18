// =====================================================================
//  src/libhobbycad/hobbycad/sketch/selection.h — what is selected in a
//  sketch, and how clicks and windows change it
// =====================================================================
//
//  Capability tier of the front-end support layer. The selection as data
//  (the entities in the order they were picked and the primary one, the
//  selected points, a constraint, the group being edited inside, a
//  midpoint grip, a slot anchor) and the rules that change it: a click
//  replaces, toggles or adds, and takes a whole group unless the click is
//  individual or the user is inside the group; a window takes what it
//  encloses, a crossing window what it touches.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_SKETCH_SELECTION_H
#define HOBBYCAD_SKETCH_SELECTION_H

#include "../core.h"
#include "../types.h"
#include "entity.h"

#include <algorithm>
#include <array>
#include <utility>
#include <vector>

namespace hobbycad {
namespace sketch {

/// What is selected.
struct HOBBYCAD_EXPORT SelectionState {
    std::vector<int> entities;                ///< in the order they were selected
    int primary = -1;                         ///< the one whose properties show
    std::vector<std::pair<int, int>> points;  ///< (entity, point index)
    int constraint = -1;
    int enteredGroup = -1;                    ///< the group being edited inside
    int midpoint = -1;                        ///< the entity whose midpoint grip is selected
    std::pair<int, int> slotAnchor{-1, -1};   ///< (slot, anchor index)

    bool has(int entityId) const;
    void add(int entityId);
    void remove(int entityId);
    /// Deselect every entity; the primary goes with them.
    void clearEntities();
};

/// How a click changes the selection.
enum class ClickSelect {
    Replace,   ///< a plain click
    Toggle,    ///< Ctrl: add, or remove when already selected
    Add,       ///< Shift: add, never remove
};

/// The click mode for the modifiers held.
inline ClickSelect clickSelectFor(bool ctrl, bool shift)
{
    return ctrl ? ClickSelect::Toggle : (shift ? ClickSelect::Add : ClickSelect::Replace);
}

/// Deselect everything, keeping the group being edited inside.
HOBBYCAD_EXPORT void clearSelection(SelectionState& s);

/// Select a constraint; entities are deselected.
HOBBYCAD_EXPORT void selectConstraint(SelectionState& s, int constraintId);

/// Select a point: a plain click replaces everything with it, Ctrl toggles
/// it, Shift adds it.
HOBBYCAD_EXPORT void selectPoint(SelectionState& s, int entityId, int pointIndex,
                                 ClickSelect mode);

/// Start editing inside a group; the selection is cleared.
HOBBYCAD_EXPORT void enterGroup(SelectionState& s, int groupId);

/// True when a window dragged from `screenStart` to `screenEnd` is a
/// crossing window: dragged right to left on screen.
inline bool windowIsCrossing(const Point2D& screenStart, const Point2D& screenEnd)
{
    return screenEnd.x < screenStart.x;
}

/// True when `e` is in the window with sketch-space corners `corners` (in
/// order round it): enclosed by it, or for a crossing window touching it.
/// `axisAligned` says the corners form an axis-aligned rectangle.
HOBBYCAD_EXPORT bool entityInWindow(const Entity& e, const std::array<Point2D, 4>& corners,
                                    bool axisAligned, bool crossing);

// ---- Over a sketch --------------------------------------------------------

/// Take in every member of a group any selected entity belongs to, unless
/// the user is editing inside a group.
template <class Entities>
void expandToGroups(SelectionState& s, const Entities& entities)
{
    if (s.enteredGroup >= 0) return;
    std::vector<int> touched;
    for (const Entity& e : entities) {
        if (e.groupId >= 0 && s.has(e.id)) touched.push_back(e.groupId);
    }
    if (touched.empty()) return;
    for (const Entity& e : entities) {
        if (e.groupId < 0 || s.has(e.id)) continue;
        if (std::find(touched.begin(), touched.end(), e.groupId) == touched.end()) continue;
        s.add(e.id);
        // The clicked entity stays primary; a member becomes it only when
        // there was none.
        if (s.primary < 0) s.primary = e.id;
    }
}

/// Stop editing inside a group and select the whole group again.
template <class Entities>
void leaveGroup(SelectionState& s, const Entities& entities)
{
    if (s.enteredGroup < 0) return;
    const int group = s.enteredGroup;
    s.enteredGroup = -1;
    for (const Entity& e : entities) {
        if (e.groupId != group) continue;
        s.add(e.id);
        s.primary = e.id;
    }
}

/// Select an entity. Without `extend` the entity replaces the selection;
/// with it the entity is added, or removed when already selected. The
/// entity's group comes with it unless `individual` or the user is editing
/// inside a group; a click outside that group leaves it first. Returns
/// true when it left a group.
template <class Entities>
bool selectEntity(SelectionState& s, const Entities& entities, int entityId, bool extend,
                  bool individual)
{
    if (!extend) {
        s.clearEntities();
        s.constraint = -1;
    }
    const Entity* entity = nullptr;
    for (const Entity& e : entities) {
        if (e.id == entityId) entity = &e;
    }
    if (!entity) return false;

    bool left = false;
    if (s.enteredGroup >= 0 && entity->groupId != s.enteredGroup) {
        s.enteredGroup = -1;
        s.clearEntities();
        left = true;
    }
    const bool single = individual || s.enteredGroup >= 0;
    if (extend && s.has(entityId)) {
        s.remove(entityId);
        if (!single && entity->groupId >= 0) {
            for (const Entity& e : entities) {
                if (e.groupId == entity->groupId) s.remove(e.id);
            }
        }
    } else {
        s.add(entityId);
        s.primary = entityId;   // the last clicked
        if (!single) expandToGroups(s, entities);
    }
    return left;
}

/// A click on an entity with the modifiers' mode: Shift on an entity already
/// selected keeps the selection as it is.
template <class Entities>
bool clickEntity(SelectionState& s, const Entities& entities, int entityId, ClickSelect mode,
                 bool individual = false)
{
    if (mode == ClickSelect::Add && s.has(entityId)) return false;
    return selectEntity(s, entities, entityId, mode != ClickSelect::Replace, individual);
}

/// The entities in a window, in sketch order.
template <class Entities>
std::vector<int> entitiesInWindow(const Entities& entities,
                                  const std::array<Point2D, 4>& corners, bool axisAligned,
                                  bool crossing)
{
    std::vector<int> ids;
    for (const Entity& e : entities) {
        if (entityInWindow(e, corners, axisAligned, crossing)) ids.push_back(e.id);
    }
    return ids;
}

/// Select what a window caught: added to the selection when `keep`, else
/// in place of it; groups come whole.
template <class Entities>
void selectWindow(SelectionState& s, const Entities& entities, const std::vector<int>& caught,
                  bool keep)
{
    if (!keep) {
        s.clearEntities();
        s.constraint = -1;
    }
    for (int id : caught) {
        s.add(id);
        s.primary = id;
    }
    expandToGroups(s, entities);
}

}  // namespace sketch
}  // namespace hobbycad

#endif  // HOBBYCAD_SKETCH_SELECTION_H
