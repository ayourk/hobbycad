// =====================================================================
//  src/libhobbycad/sketch/undo.cpp — Undo/redo system implementation
// =====================================================================

#include <hobbycad/sketch/undo.h>

#include <algorithm>
#include <string>

namespace hobbycad {
namespace sketch {

// =====================================================================
//  UndoCommand Static Factories
// =====================================================================

UndoCommand UndoCommand::addEntity(const Entity& entity, const std::string& desc)
{
    UndoCommand cmd;
    cmd.type = CommandType::AddEntity;
    cmd.entity = entity;
    cmd.description = desc.empty() ? (std::string("Add ") + entityTypeName(entity.type)) : desc;
    return cmd;
}

UndoCommand UndoCommand::deleteEntity(const Entity& entity, const std::string& desc)
{
    UndoCommand cmd;
    cmd.type = CommandType::DeleteEntity;
    cmd.entity = entity;
    cmd.description = desc.empty() ? (std::string("Delete ") + entityTypeName(entity.type)) : desc;
    return cmd;
}

UndoCommand UndoCommand::modifyEntity(const Entity& before, const Entity& after, const std::string& desc)
{
    UndoCommand cmd;
    cmd.type = CommandType::ModifyEntity;
    cmd.previousEntity = before;
    cmd.entity = after;
    cmd.description = desc.empty() ? (std::string("Modify ") + entityTypeName(after.type)) : desc;
    return cmd;
}

UndoCommand UndoCommand::addConstraint(const Constraint& constraint, const std::string& desc)
{
    UndoCommand cmd;
    cmd.type = CommandType::AddConstraint;
    cmd.constraint = constraint;
    cmd.description = desc.empty() ? std::string("Add Constraint") : desc;
    return cmd;
}

UndoCommand UndoCommand::deleteConstraint(const Constraint& constraint, const std::string& desc)
{
    UndoCommand cmd;
    cmd.type = CommandType::DeleteConstraint;
    cmd.constraint = constraint;
    cmd.description = desc.empty() ? std::string("Delete Constraint") : desc;
    return cmd;
}

UndoCommand UndoCommand::modifyConstraint(const Constraint& before, const Constraint& after, const std::string& desc)
{
    UndoCommand cmd;
    cmd.type = CommandType::ModifyConstraint;
    cmd.previousConstraint = before;
    cmd.constraint = after;
    cmd.description = desc.empty() ? std::string("Modify Constraint") : desc;
    return cmd;
}

UndoCommand UndoCommand::addGroup(const Group& group, const std::string& desc)
{
    UndoCommand cmd;
    cmd.type = CommandType::AddGroup;
    cmd.group = group;
    cmd.description = desc.empty() ? std::string("Create Group") : desc;
    return cmd;
}

UndoCommand UndoCommand::deleteGroup(const Group& group, const std::string& desc)
{
    UndoCommand cmd;
    cmd.type = CommandType::DeleteGroup;
    cmd.group = group;
    cmd.description = desc.empty() ? std::string("Delete Group") : desc;
    return cmd;
}

UndoCommand UndoCommand::modifyGroup(const Group& before, const Group& after, const std::string& desc)
{
    UndoCommand cmd;
    cmd.type = CommandType::ModifyGroup;
    cmd.previousGroup = before;
    cmd.group = after;
    cmd.description = desc.empty() ? std::string("Modify Group") : desc;
    return cmd;
}

UndoCommand UndoCommand::compound(const std::vector<UndoCommand>& commands, const std::string& desc)
{
    UndoCommand cmd;
    cmd.type = CommandType::Compound;
    cmd.subCommands = commands;
    cmd.description = desc;
    return cmd;
}

int UndoCommand::operationCount() const
{
    if (type != CommandType::Compound) {
        return 1;
    }
    int count = 0;
    for (const auto& sub : subCommands) {
        count += sub.operationCount();
    }
    return count;
}

// =====================================================================
//  UndoStack Implementation
// =====================================================================

UndoStack::UndoStack(int maxSize)
    : m_maxSize(maxSize)
{
}

void UndoStack::push(const UndoCommand& command)
{
    if (m_recordingCompound) {
        m_compoundCommands.push_back(command);
        return;
    }

    m_undoStack.push_back(command);
    m_redoStack.clear();
    m_modified = true;
    enforceMaxSize();
}

void UndoStack::pushCompound(const std::vector<UndoCommand>& commands, const std::string& description)
{
    if (commands.empty()) return;

    if (commands.size() == 1) {
        push(commands.front());
    } else {
        push(UndoCommand::compound(commands, description));
    }
}

UndoCommand UndoStack::undo()
{
    if (m_undoStack.empty()) {
        return UndoCommand();
    }

    UndoCommand cmd = m_undoStack.back();
    m_undoStack.pop_back();
    m_redoStack.push_back(cmd);
    m_modified = true;
    return cmd;
}

UndoCommand UndoStack::redo()
{
    if (m_redoStack.empty()) {
        return UndoCommand();
    }

    UndoCommand cmd = m_redoStack.back();
    m_redoStack.pop_back();
    m_undoStack.push_back(cmd);
    m_modified = true;
    return cmd;
}

std::vector<UndoCommand> UndoStack::undoMultiple(int levels)
{
    std::vector<UndoCommand> result;
    for (int i = 0; i < levels && !m_undoStack.empty(); ++i) {
        result.push_back(undo());
    }
    return result;
}

std::vector<UndoCommand> UndoStack::redoMultiple(int levels)
{
    std::vector<UndoCommand> result;
    for (int i = 0; i < levels && !m_redoStack.empty(); ++i) {
        result.push_back(redo());
    }
    return result;
}

std::string UndoStack::undoDescription() const
{
    if (m_undoStack.empty()) {
        return std::string();
    }
    return m_undoStack.back().description;
}

std::string UndoStack::redoDescription() const
{
    if (m_redoStack.empty()) {
        return std::string();
    }
    return m_redoStack.back().description;
}

std::vector<std::string> UndoStack::undoDescriptions() const
{
    std::vector<std::string> result;
    // Return in reverse order (most recent first)
    for (int i = static_cast<int>(m_undoStack.size()) - 1; i >= 0; --i) {
        result.push_back(m_undoStack[i].description);
    }
    return result;
}

std::vector<std::string> UndoStack::redoDescriptions() const
{
    std::vector<std::string> result;
    // Return in reverse order (most recent first)
    for (int i = static_cast<int>(m_redoStack.size()) - 1; i >= 0; --i) {
        result.push_back(m_redoStack[i].description);
    }
    return result;
}

void UndoStack::clear()
{
    m_undoStack.clear();
    m_redoStack.clear();
    m_modified = false;
}

void UndoStack::clearRedo()
{
    m_redoStack.clear();
}

void UndoStack::setMaxSize(int maxSize)
{
    m_maxSize = std::max(1, maxSize);
    enforceMaxSize();
}

void UndoStack::beginCompound(const std::string& description)
{
    if (m_recordingCompound) {
        // Nested compound - just continue recording
        return;
    }
    m_recordingCompound = true;
    m_compoundCommands.clear();
    m_compoundDescription = description;
}

void UndoStack::endCompound()
{
    if (!m_recordingCompound) return;

    m_recordingCompound = false;

    if (!m_compoundCommands.empty()) {
        pushCompound(m_compoundCommands, m_compoundDescription);
    }

    m_compoundCommands.clear();
    m_compoundDescription.clear();
}

void UndoStack::enforceMaxSize()
{
    while (static_cast<int>(m_undoStack.size()) > m_maxSize) {
        m_undoStack.erase(m_undoStack.begin());
    }
}

// =====================================================================
//  Entity Type Names
// =====================================================================

const char* entityTypeName(EntityType type)
{
    switch (type) {
    case EntityType::Point:     return "Point";
    case EntityType::Line:      return "Line";
    case EntityType::Rectangle: return "Rectangle";
    case EntityType::Circle:    return "Circle";
    case EntityType::Arc:       return "Arc";
    case EntityType::Spline:    return "Spline";
    case EntityType::Polygon:   return "Polygon";
    case EntityType::Slot:      return "Slot";
    case EntityType::Ellipse:   return "Ellipse";
    case EntityType::Text:      return "Text";
    }
    return "Unknown";
}

std::string entityTypeDisplayName(EntityType type)
{
    // Without Qt translation support, just return the plain name
    return entityTypeName(type);
}

}  // namespace sketch
}  // namespace hobbycad
