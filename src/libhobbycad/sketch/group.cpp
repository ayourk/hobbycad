// =====================================================================
//  src/libhobbycad/sketch/group.cpp — Entity grouping implementation
// =====================================================================

#include <hobbycad/sketch/group.h>

namespace hobbycad {
namespace sketch {

// =====================================================================
//  GroupManager Implementation
// =====================================================================

int GroupManager::createGroup(const QString& name)
{
    Group group;
    group.id = m_nextId++;
    group.name = name.isEmpty() ? QStringLiteral("Group %1").arg(group.id) : name;
    m_groups.append(group);
    return group.id;
}

void GroupManager::deleteGroup(int groupId, bool deleteChildren)
{
    Group* group = groupById(groupId);
    if (!group) return;

    int parentId = group->parentGroupId;

    if (deleteChildren) {
        // Recursively delete all child groups
        for (int childId : group->childGroupIds) {
            deleteGroup(childId, true);
        }
    } else {
        // Move child groups to parent (or make them top-level)
        for (int childId : group->childGroupIds) {
            Group* child = groupById(childId);
            if (child) {
                child->parentGroupId = parentId;
                if (parentId >= 0) {
                    Group* parent = groupById(parentId);
                    if (parent && !parent->childGroupIds.contains(childId)) {
                        parent->childGroupIds.append(childId);
                    }
                }
            }
        }
    }

    // Remove from parent's child list
    if (parentId >= 0) {
        Group* parent = groupById(parentId);
        if (parent) {
            parent->childGroupIds.removeAll(groupId);
        }
    }

    // Remove the group itself
    for (int i = 0; i < m_groups.size(); ++i) {
        if (m_groups[i].id == groupId) {
            m_groups.removeAt(i);
            break;
        }
    }
}

void GroupManager::addEntityToGroup(int entityId, int groupId)
{
    Group* group = groupById(groupId);
    if (group && !group->entityIds.contains(entityId)) {
        group->entityIds.append(entityId);
    }
}

void GroupManager::removeEntityFromGroup(int entityId, int groupId)
{
    Group* group = groupById(groupId);
    if (group) {
        group->entityIds.removeAll(entityId);
    }
}

bool GroupManager::addGroupToGroup(int childGroupId, int parentGroupId)
{
    if (childGroupId == parentGroupId) return false;
    if (wouldCreateCycle(childGroupId, parentGroupId)) return false;

    Group* child = groupById(childGroupId);
    Group* parent = groupById(parentGroupId);
    if (!child || !parent) return false;

    // Remove from old parent
    if (child->parentGroupId >= 0) {
        Group* oldParent = groupById(child->parentGroupId);
        if (oldParent) {
            oldParent->childGroupIds.removeAll(childGroupId);
        }
    }

    // Add to new parent
    child->parentGroupId = parentGroupId;
    if (!parent->childGroupIds.contains(childGroupId)) {
        parent->childGroupIds.append(childGroupId);
    }

    return true;
}

void GroupManager::ungroupFromParent(int groupId)
{
    Group* group = groupById(groupId);
    if (!group || group->parentGroupId < 0) return;

    Group* parent = groupById(group->parentGroupId);
    if (parent) {
        parent->childGroupIds.removeAll(groupId);
    }
    group->parentGroupId = -1;
}

Group* GroupManager::groupById(int id)
{
    for (auto& g : m_groups) {
        if (g.id == id) return &g;
    }
    return nullptr;
}

const Group* GroupManager::groupById(int id) const
{
    for (const auto& g : m_groups) {
        if (g.id == id) return &g;
    }
    return nullptr;
}

QVector<int> GroupManager::topLevelGroupIds() const
{
    QVector<int> result;
    for (const auto& g : m_groups) {
        if (g.parentGroupId < 0) {
            result.append(g.id);
        }
    }
    return result;
}

QSet<int> GroupManager::allEntityIds(int groupId) const
{
    QSet<int> result;
    const Group* group = groupById(groupId);
    if (!group) return result;

    // Add direct entities
    for (int entityId : group->entityIds) {
        result.insert(entityId);
    }

    // Recursively add entities from child groups
    for (int childId : group->childGroupIds) {
        result.unite(allEntityIds(childId));
    }

    return result;
}

QVector<int> GroupManager::groupsContainingEntity(int entityId) const
{
    QVector<int> result;
    for (const auto& g : m_groups) {
        if (g.entityIds.contains(entityId)) {
            result.append(g.id);
        }
    }
    return result;
}

bool GroupManager::wouldCreateCycle(int childId, int parentId) const
{
    // Check if parentId is a descendant of childId
    return isAncestorOf(childId, parentId);
}

void GroupManager::clear()
{
    m_groups.clear();
    m_nextId = 1;
}

bool GroupManager::isAncestorOf(int ancestorId, int descendantId) const
{
    const Group* descendant = groupById(descendantId);
    while (descendant && descendant->parentGroupId >= 0) {
        if (descendant->parentGroupId == ancestorId) {
            return true;
        }
        descendant = groupById(descendant->parentGroupId);
    }
    return false;
}

// =====================================================================
//  Group Utility Functions
// =====================================================================

int groupDepth(const GroupManager& manager, int groupId)
{
    int depth = 0;
    const Group* group = manager.groupById(groupId);
    while (group && group->parentGroupId >= 0) {
        depth++;
        group = manager.groupById(group->parentGroupId);
    }
    return depth;
}

QVector<int> groupPath(const GroupManager& manager, int groupId)
{
    QVector<int> path;
    const Group* group = manager.groupById(groupId);
    while (group) {
        path.prepend(group->id);
        if (group->parentGroupId < 0) break;
        group = manager.groupById(group->parentGroupId);
    }
    return path;
}

int commonAncestor(const GroupManager& manager, int groupId1, int groupId2)
{
    QVector<int> path1 = groupPath(manager, groupId1);
    QVector<int> path2 = groupPath(manager, groupId2);

    int common = -1;
    int minLen = qMin(path1.size(), path2.size());
    for (int i = 0; i < minLen; ++i) {
        if (path1[i] == path2[i]) {
            common = path1[i];
        } else {
            break;
        }
    }
    return common;
}

}  // namespace sketch
}  // namespace hobbycad
