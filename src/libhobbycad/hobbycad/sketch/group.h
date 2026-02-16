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

#include <QString>
#include <QVector>
#include <QSet>

namespace hobbycad {
namespace sketch {

// =====================================================================
//  Group Data Structure
// =====================================================================

/// A group of entities (can contain nested groups)
struct HOBBYCAD_EXPORT Group {
    int id = 0;                        ///< Unique group ID
    QString name;                      ///< Display name
    QVector<int> entityIds;            ///< Direct entity members
    QVector<int> childGroupIds;        ///< Nested group IDs
    int parentGroupId = -1;            ///< Parent group ID (-1 if top-level)
    bool locked = false;               ///< Prevent modification of members
    bool expanded = true;              ///< UI expansion state (for tree views)

    /// Check if this group directly contains an entity
    bool containsEntity(int entityId) const {
        return entityIds.contains(entityId);
    }

    /// Check if this group directly contains a child group
    bool containsGroup(int groupId) const {
        return childGroupIds.contains(groupId);
    }

    /// Check if this group is empty (no entities or child groups)
    bool isEmpty() const {
        return entityIds.isEmpty() && childGroupIds.isEmpty();
    }
};

// =====================================================================
//  Group Manager
// =====================================================================

/// Manages a collection of groups with hierarchy support
class HOBBYCAD_EXPORT GroupManager {
public:
    GroupManager() = default;

    /// Create a new group and return its ID
    int createGroup(const QString& name = QString());

    /// Delete a group by ID (entities are not deleted, just ungrouped)
    /// @param groupId Group to delete
    /// @param deleteChildren If true, delete child groups too; if false, move them to parent
    void deleteGroup(int groupId, bool deleteChildren = false);

    /// Add an entity to a group
    void addEntityToGroup(int entityId, int groupId);

    /// Remove an entity from a group
    void removeEntityFromGroup(int entityId, int groupId);

    /// Add a child group to a parent group
    /// @return false if this would create a cycle
    bool addGroupToGroup(int childGroupId, int parentGroupId);

    /// Remove a group from its parent (makes it top-level)
    void ungroupFromParent(int groupId);

    /// Get a group by ID (nullptr if not found)
    Group* groupById(int id);
    const Group* groupById(int id) const;

    /// Get all groups
    const QVector<Group>& groups() const { return m_groups; }

    /// Get all top-level groups (no parent)
    QVector<int> topLevelGroupIds() const;

    /// Get all entity IDs in a group (recursively includes nested groups)
    QSet<int> allEntityIds(int groupId) const;

    /// Get all groups that contain an entity (directly, not through nesting)
    QVector<int> groupsContainingEntity(int entityId) const;

    /// Check if adding childId as a child of parentId would create a cycle
    bool wouldCreateCycle(int childId, int parentId) const;

    /// Clear all groups
    void clear();

    /// Get next available group ID
    int nextGroupId() const { return m_nextId; }

private:
    QVector<Group> m_groups;
    int m_nextId = 1;

    /// Helper to check ancestry
    bool isAncestorOf(int ancestorId, int descendantId) const;
};

// =====================================================================
//  Group Utility Functions
// =====================================================================

/// Get the depth of a group in the hierarchy (0 = top-level)
HOBBYCAD_EXPORT int groupDepth(const GroupManager& manager, int groupId);

/// Get the path from root to a group (list of group IDs)
HOBBYCAD_EXPORT QVector<int> groupPath(const GroupManager& manager, int groupId);

/// Find the common ancestor of two groups (-1 if none)
HOBBYCAD_EXPORT int commonAncestor(const GroupManager& manager, int groupId1, int groupId2);

}  // namespace sketch
}  // namespace hobbycad

#endif  // HOBBYCAD_SKETCH_GROUP_H
