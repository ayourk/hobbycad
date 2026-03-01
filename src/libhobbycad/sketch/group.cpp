// =====================================================================
//  src/libhobbycad/sketch/group.cpp — Entity grouping implementation
// =====================================================================

#include <hobbycad/sketch/group.h>

#include <algorithm>

namespace hobbycad {
namespace sketch {

// =====================================================================
//  GroupManager Implementation
// =====================================================================

int GroupManager::createGroup(const std::string& name)
{
    Group group;
    group.id = m_nextId++;
    group.name = name.empty() ? ("Group " + std::to_string(group.id)) : name;
    m_groups.push_back(group);
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
                    if (parent && !hobbycad::contains(parent->childGroupIds, childId)) {
                        parent->childGroupIds.push_back(childId);
                    }
                }
            }
        }
    }

    // Remove from parent's child list
    if (parentId >= 0) {
        Group* parent = groupById(parentId);
        if (parent) {
            hobbycad::removeAll(parent->childGroupIds, groupId);
        }
    }

    // Remove the group itself
    for (int i = 0; i < static_cast<int>(m_groups.size()); ++i) {
        if (m_groups[i].id == groupId) {
            m_groups.erase(m_groups.begin() + i);
            break;
        }
    }
}

void GroupManager::addEntityToGroup(int entityId, int groupId)
{
    Group* group = groupById(groupId);
    if (group && !hobbycad::contains(group->entityIds, entityId)) {
        group->entityIds.push_back(entityId);
    }
}

void GroupManager::removeEntityFromGroup(int entityId, int groupId)
{
    Group* group = groupById(groupId);
    if (group) {
        hobbycad::removeAll(group->entityIds, entityId);
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
            hobbycad::removeAll(oldParent->childGroupIds, childGroupId);
        }
    }

    // Add to new parent
    child->parentGroupId = parentGroupId;
    if (!hobbycad::contains(parent->childGroupIds, childGroupId)) {
        parent->childGroupIds.push_back(childGroupId);
    }

    return true;
}

void GroupManager::ungroupFromParent(int groupId)
{
    Group* group = groupById(groupId);
    if (!group || group->parentGroupId < 0) return;

    Group* parent = groupById(group->parentGroupId);
    if (parent) {
        hobbycad::removeAll(parent->childGroupIds, groupId);
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

std::vector<int> GroupManager::topLevelGroupIds() const
{
    std::vector<int> result;
    for (const auto& g : m_groups) {
        if (g.parentGroupId < 0) {
            result.push_back(g.id);
        }
    }
    return result;
}

std::unordered_set<int> GroupManager::allEntityIds(int groupId) const
{
    std::unordered_set<int> result;
    const Group* group = groupById(groupId);
    if (!group) return result;

    // Add direct entities
    for (int entityId : group->entityIds) {
        result.insert(entityId);
    }

    // Recursively add entities from child groups
    for (int childId : group->childGroupIds) {
        auto childIds = allEntityIds(childId);
        result.insert(childIds.begin(), childIds.end());
    }

    return result;
}

std::vector<int> GroupManager::groupsContainingEntity(int entityId) const
{
    std::vector<int> result;
    for (const auto& g : m_groups) {
        if (hobbycad::contains(g.entityIds, entityId)) {
            result.push_back(g.id);
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

std::vector<int> groupPath(const GroupManager& manager, int groupId)
{
    std::vector<int> path;
    const Group* group = manager.groupById(groupId);
    while (group) {
        path.insert(path.begin(), group->id);
        if (group->parentGroupId < 0) break;
        group = manager.groupById(group->parentGroupId);
    }
    return path;
}

int commonAncestor(const GroupManager& manager, int groupId1, int groupId2)
{
    std::vector<int> path1 = groupPath(manager, groupId1);
    std::vector<int> path2 = groupPath(manager, groupId2);

    int common = -1;
    int minLen = std::min(static_cast<int>(path1.size()), static_cast<int>(path2.size()));
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
