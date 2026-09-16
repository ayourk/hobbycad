// =====================================================================
//  src/libhobbycad/sketch/group.cpp — Entity grouping implementation
// =====================================================================

#include <hobbycad/sketch/group.h>
#include <hobbycad/format.h>

#include <cstdlib>


namespace hobbycad {
namespace sketch {

bool isGroupChainLocked(int groupId, const std::vector<Group>& groups)
{
    // Walk from the group up through its parents; a locked ancestor locks all
    // descendants. Cap the walk by the group count so a corrupt parent cycle
    // cannot spin forever.
    int gid = groupId;
    for (std::size_t guard = 0; gid >= 0 && guard <= groups.size(); ++guard) {
        const Group* g = nullptr;
        for (const Group& cand : groups) if (cand.id == gid) { g = &cand; break; }
        if (!g) return false;
        if (g->locked) return true;
        gid = g->parentGroupId;
    }
    return false;
}

int nextFreeGroupId(const std::vector<Group>& groups)
{
    int next = 1;
    for (const Group& g : groups) {
        if (g.id >= next) next = g.id + 1;
    }
    return next;
}

const Group* findGroupById(const std::vector<Group>& groups, int id)
{
    for (const Group& g : groups) if (g.id == id) return &g;
    return nullptr;
}

Group* findGroupById(std::vector<Group>& groups, int id)
{
    for (Group& g : groups) if (g.id == id) return &g;
    return nullptr;
}

const Group* findGroupByName(const std::vector<Group>& groups, const std::string& name)
{
    for (const Group& g : groups) if (g.name == name) return &g;
    return nullptr;
}

const Group* findGroupByRef(const std::vector<Group>& groups, const std::string& ref)
{
    if (ref.size() > 3 && equalsIgnoreCase(ref.substr(0, 3), "id=")) {
        char* end = nullptr;
        const long id = std::strtol(ref.c_str() + 3, &end, 10);
        if (end && *end == '\0') return findGroupById(groups, static_cast<int>(id));
        return nullptr;
    }
    return findGroupByName(groups, ref);
}

const char* groupKindToken(GroupKind kind)
{
    switch (kind) {
    case GroupKind::SweepAngle: return "sweep_angle";
    case GroupKind::Slot:       return "slot";
    case GroupKind::User: break;
    }
    return "";
}

bool parseGroupKindToken(const std::string& token, GroupKind& out)
{
    if (equalsIgnoreCase(token, "sweep_angle") || equalsIgnoreCase(token, "sweep")) {
        out = GroupKind::SweepAngle;
        return true;
    }
    if (equalsIgnoreCase(token, "slot")) { out = GroupKind::Slot; return true; }
    if (equalsIgnoreCase(token, "user")) { out = GroupKind::User; return true; }
    return false;
}

GroupKind inferLegacyGroupKind(const std::string& name)
{
    if (name.rfind(kSweepAngleGroupPrefix, 0) == 0) return GroupKind::SweepAngle;
    // Exactly "Slot <n>": the auto-group a slot made for itself.
    const std::string slotPrefix = "Slot ";
    if (name.size() > slotPrefix.size() && name.rfind(slotPrefix, 0) == 0) {
        bool digits = true;
        for (size_t i = slotPrefix.size(); i < name.size(); ++i)
            if (name[i] < '0' || name[i] > '9') { digits = false; break; }
        if (digits) return GroupKind::Slot;
    }
    return GroupKind::User;
}

bool isSweepAngleGroup(const Group& g)
{
    return g.kind == GroupKind::SweepAngle;
}

const Group* sweepAngleGroupForArc(const std::vector<Group>& groups, int arcId)
{
    for (const Group& g : groups) {
        if (isSweepAngleGroup(g) && g.containsEntity(arcId)) return &g;
    }
    return nullptr;
}

std::string sweepAngleGroupName(int ordinal)
{
    return std::string(kSweepAngleGroupPrefix) + " " + std::to_string(ordinal);
}

std::string slotGroupName(int slotId)
{
    return "Slot " + std::to_string(slotId);
}

Group makeSlotGroup(int groupId, int slotId, const std::vector<int>& pathIds)
{
    Group g;
    g.id = groupId;
    g.kind = GroupKind::Slot;
    g.name = slotGroupName(slotId);
    g.entityIds = pathIds;
    g.entityIds.push_back(slotId);
    g.locked = false;
    return g;
}

}  // namespace sketch
}  // namespace hobbycad
