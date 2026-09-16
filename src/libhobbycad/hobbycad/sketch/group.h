// =====================================================================
//  src/libhobbycad/hobbycad/sketch/group.h — Entity grouping
// =====================================================================
//
//  Hierarchical entity grouping for sketch organization.
//  Groups can contain entities and other groups (nested).
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_SKETCH_GROUP_H
#define HOBBYCAD_SKETCH_GROUP_H

#include "../core.h"
#include "../types.h"

#include <string>
#include <vector>

namespace hobbycad {
namespace sketch {

// =====================================================================
//  Group Data Structure
// =====================================================================

/// A group of entities (can contain nested groups)
/// What a group is FOR. A User group is whatever the person gathered; the
/// other kinds are rigs the program builds and looks up again (the arc
/// sweep-angle dimension: two construction lines and an Angle constraint;
/// a slot with its construction centerline), and they must keep that
/// identity through a rename.
enum class GroupKind { User, SweepAngle, Slot };

struct HOBBYCAD_EXPORT Group {
    int id = 0;                        ///< Unique group ID
    std::string name;                  ///< Display name
    GroupKind kind = GroupKind::User;  ///< What the group is for (see GroupKind)
    std::vector<int> entityIds;        ///< Direct entity members
    std::vector<int> constraintIds;    ///< Direct constraint members
    std::vector<int> childGroupIds;    ///< Nested group IDs
    int parentGroupId = -1;            ///< Parent group ID (-1 if top-level)
    bool locked = false;               ///< Prevent modification of members
    bool hasPivot = false;             ///< A transform pivot has been set; unset means the geometric center
    Point2D pivot;                     ///< Transform pivot (rotate/scale/mirror about it) when hasPivot
    bool expanded = true;              ///< UI expansion state (for tree views)

    /// Check if this group directly contains an entity
    bool containsEntity(int entityId) const {
        return hobbycad::contains(entityIds, entityId);
    }

    /// Check if this group directly contains a constraint
    bool containsConstraint(int constraintId) const {
        return hobbycad::contains(constraintIds, constraintId);
    }

    /// Check if this group directly contains a child group
    bool containsGroup(int groupId) const {
        return hobbycad::contains(childGroupIds, groupId);
    }

    /// Check if this group is empty (no entities, constraints, or child groups)
    bool isEmpty() const {
        return entityIds.empty() && constraintIds.empty() && childGroupIds.empty();
    }
};

// =====================================================================
//  Group Utility Functions
// =====================================================================

/// True if `groupId`, or any of its ancestors, is locked, i.e. its members
/// must not be modified. Walks the parent chain in `groups`. A negative or
/// unknown groupId (an entity in no group) is not locked.
HOBBYCAD_EXPORT bool isGroupChainLocked(int groupId, const std::vector<Group>& groups);

/// Highest group id in the container plus one (1 when empty).
HOBBYCAD_EXPORT int nextFreeGroupId(const std::vector<Group>& groups);

/// Group lookups. By id, by exact name, or by a reference as typed at a
/// prompt: "id=<n>" (prefix case-insensitive) or the name.
HOBBYCAD_EXPORT const Group* findGroupById(const std::vector<Group>& groups, int id);
HOBBYCAD_EXPORT Group* findGroupById(std::vector<Group>& groups, int id);
HOBBYCAD_EXPORT const Group* findGroupByName(const std::vector<Group>& groups, const std::string& name);
HOBBYCAD_EXPORT const Group* findGroupByRef(const std::vector<Group>& groups, const std::string& ref);

// ---- Group kinds ----------------------------------------------------

/// The name prefix that marked sweep-angle rigs before `kind` existed. A
/// group read from an older file or script that has this prefix and no
/// kind is taken as a SweepAngle (inferLegacyGroupKind); new records carry
/// the kind explicitly, so a rename no longer breaks the rig.
constexpr const char* kSweepAngleGroupPrefix = "Sweep Angle";

/// Stored token for a kind ("sweep_angle"); "" for User, which is never
/// written. parseGroupKindToken also accepts the script's short "sweep".
HOBBYCAD_EXPORT const char* groupKindToken(GroupKind kind);
HOBBYCAD_EXPORT bool parseGroupKindToken(const std::string& token, GroupKind& out);
HOBBYCAD_EXPORT GroupKind inferLegacyGroupKind(const std::string& name);

HOBBYCAD_EXPORT bool isSweepAngleGroup(const Group& g);
/// The sweep-angle rig that holds `arcId`, or null.
HOBBYCAD_EXPORT const Group* sweepAngleGroupForArc(const std::vector<Group>& groups, int arcId);
/// Display name for the n-th sweep-angle rig ("Sweep Angle 3").
HOBBYCAD_EXPORT std::string sweepAngleGroupName(int ordinal);

/// The group that ties a slot to its centerline path(s): kind Slot, named
/// slotGroupName(slotId) ("Slot 7", by the SLOT's id, so the script
/// exporter and the CLI recognize it), members the path ids then the slot.
/// A group id of 0 lets addGroup() assign one.
HOBBYCAD_EXPORT std::string slotGroupName(int slotId);
HOBBYCAD_EXPORT Group makeSlotGroup(int groupId, int slotId, const std::vector<int>& pathIds);

}  // namespace sketch
}  // namespace hobbycad

#endif  // HOBBYCAD_SKETCH_GROUP_H
