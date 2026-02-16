// =====================================================================
//  src/libhobbycad/sketch/undo.cpp — Undo/redo system implementation
// =====================================================================

#include <hobbycad/sketch/undo.h>
#include <QObject>

namespace hobbycad {
namespace sketch {

// =====================================================================
//  UndoCommand Static Factories
// =====================================================================

UndoCommand UndoCommand::addEntity(const Entity& entity, const QString& desc)
{
    UndoCommand cmd;
    cmd.type = CommandType::AddEntity;
    cmd.entity = entity;
    cmd.description = desc.isEmpty() ? QObject::tr("Add %1").arg(entityTypeDisplayName(entity.type)) : desc;
    return cmd;
}

UndoCommand UndoCommand::deleteEntity(const Entity& entity, const QString& desc)
{
    UndoCommand cmd;
    cmd.type = CommandType::DeleteEntity;
    cmd.entity = entity;
    cmd.description = desc.isEmpty() ? QObject::tr("Delete %1").arg(entityTypeDisplayName(entity.type)) : desc;
    return cmd;
}

UndoCommand UndoCommand::modifyEntity(const Entity& before, const Entity& after, const QString& desc)
{
    UndoCommand cmd;
    cmd.type = CommandType::ModifyEntity;
    cmd.previousEntity = before;
    cmd.entity = after;
    cmd.description = desc.isEmpty() ? QObject::tr("Modify %1").arg(entityTypeDisplayName(after.type)) : desc;
    return cmd;
}

UndoCommand UndoCommand::addConstraint(const Constraint& constraint, const QString& desc)
{
    UndoCommand cmd;
    cmd.type = CommandType::AddConstraint;
    cmd.constraint = constraint;
    cmd.description = desc.isEmpty() ? QObject::tr("Add Constraint") : desc;
    return cmd;
}

UndoCommand UndoCommand::deleteConstraint(const Constraint& constraint, const QString& desc)
{
    UndoCommand cmd;
    cmd.type = CommandType::DeleteConstraint;
    cmd.constraint = constraint;
    cmd.description = desc.isEmpty() ? QObject::tr("Delete Constraint") : desc;
    return cmd;
}

UndoCommand UndoCommand::modifyConstraint(const Constraint& before, const Constraint& after, const QString& desc)
{
    UndoCommand cmd;
    cmd.type = CommandType::ModifyConstraint;
    cmd.previousConstraint = before;
    cmd.constraint = after;
    cmd.description = desc.isEmpty() ? QObject::tr("Modify Constraint") : desc;
    return cmd;
}

UndoCommand UndoCommand::addGroup(const Group& group, const QString& desc)
{
    UndoCommand cmd;
    cmd.type = CommandType::AddGroup;
    cmd.group = group;
    cmd.description = desc.isEmpty() ? QObject::tr("Create Group") : desc;
    return cmd;
}

UndoCommand UndoCommand::deleteGroup(const Group& group, const QString& desc)
{
    UndoCommand cmd;
    cmd.type = CommandType::DeleteGroup;
    cmd.group = group;
    cmd.description = desc.isEmpty() ? QObject::tr("Delete Group") : desc;
    return cmd;
}

UndoCommand UndoCommand::modifyGroup(const Group& before, const Group& after, const QString& desc)
{
    UndoCommand cmd;
    cmd.type = CommandType::ModifyGroup;
    cmd.previousGroup = before;
    cmd.group = after;
    cmd.description = desc.isEmpty() ? QObject::tr("Modify Group") : desc;
    return cmd;
}

UndoCommand UndoCommand::compound(const QVector<UndoCommand>& commands, const QString& desc)
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
        m_compoundCommands.append(command);
        return;
    }

    m_undoStack.append(command);
    m_redoStack.clear();
    m_modified = true;
    enforceMaxSize();
}

void UndoStack::pushCompound(const QVector<UndoCommand>& commands, const QString& description)
{
    if (commands.isEmpty()) return;

    if (commands.size() == 1) {
        push(commands.first());
    } else {
        push(UndoCommand::compound(commands, description));
    }
}

UndoCommand UndoStack::undo()
{
    if (m_undoStack.isEmpty()) {
        return UndoCommand();
    }

    UndoCommand cmd = m_undoStack.takeLast();
    m_redoStack.append(cmd);
    m_modified = true;
    return cmd;
}

UndoCommand UndoStack::redo()
{
    if (m_redoStack.isEmpty()) {
        return UndoCommand();
    }

    UndoCommand cmd = m_redoStack.takeLast();
    m_undoStack.append(cmd);
    m_modified = true;
    return cmd;
}

QVector<UndoCommand> UndoStack::undoMultiple(int levels)
{
    QVector<UndoCommand> result;
    for (int i = 0; i < levels && !m_undoStack.isEmpty(); ++i) {
        result.append(undo());
    }
    return result;
}

QVector<UndoCommand> UndoStack::redoMultiple(int levels)
{
    QVector<UndoCommand> result;
    for (int i = 0; i < levels && !m_redoStack.isEmpty(); ++i) {
        result.append(redo());
    }
    return result;
}

QString UndoStack::undoDescription() const
{
    if (m_undoStack.isEmpty()) {
        return QString();
    }
    return m_undoStack.last().description;
}

QString UndoStack::redoDescription() const
{
    if (m_redoStack.isEmpty()) {
        return QString();
    }
    return m_redoStack.last().description;
}

QStringList UndoStack::undoDescriptions() const
{
    QStringList result;
    // Return in reverse order (most recent first)
    for (int i = m_undoStack.size() - 1; i >= 0; --i) {
        result.append(m_undoStack[i].description);
    }
    return result;
}

QStringList UndoStack::redoDescriptions() const
{
    QStringList result;
    // Return in reverse order (most recent first)
    for (int i = m_redoStack.size() - 1; i >= 0; --i) {
        result.append(m_redoStack[i].description);
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
    m_maxSize = qMax(1, maxSize);
    enforceMaxSize();
}

void UndoStack::beginCompound(const QString& description)
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

    if (!m_compoundCommands.isEmpty()) {
        pushCompound(m_compoundCommands, m_compoundDescription);
    }

    m_compoundCommands.clear();
    m_compoundDescription.clear();
}

void UndoStack::enforceMaxSize()
{
    while (m_undoStack.size() > m_maxSize) {
        m_undoStack.removeFirst();
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

QString entityTypeDisplayName(EntityType type)
{
    switch (type) {
    case EntityType::Point:     return QObject::tr("Point");
    case EntityType::Line:      return QObject::tr("Line");
    case EntityType::Rectangle: return QObject::tr("Rectangle");
    case EntityType::Circle:    return QObject::tr("Circle");
    case EntityType::Arc:       return QObject::tr("Arc");
    case EntityType::Spline:    return QObject::tr("Spline");
    case EntityType::Polygon:   return QObject::tr("Polygon");
    case EntityType::Slot:      return QObject::tr("Slot");
    case EntityType::Ellipse:   return QObject::tr("Ellipse");
    case EntityType::Text:      return QObject::tr("Text");
    }
    return QObject::tr("Unknown");
}

}  // namespace sketch
}  // namespace hobbycad
