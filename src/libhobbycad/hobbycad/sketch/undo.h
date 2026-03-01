// =====================================================================
//  src/libhobbycad/hobbycad/sketch/undo.h — Undo/redo system
// =====================================================================
//
//  Multi-level undo/redo system for sketch operations.
//  Supports compound commands and configurable stack depth.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_SKETCH_UNDO_H
#define HOBBYCAD_SKETCH_UNDO_H

#include "../core.h"
#include "entity.h"
#include "constraint.h"
#include "group.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace hobbycad {
namespace sketch {

// =====================================================================
//  Command Types
// =====================================================================

/// Types of undo commands
enum class CommandType {
    // Entity operations
    AddEntity,
    DeleteEntity,
    ModifyEntity,

    // Constraint operations
    AddConstraint,
    DeleteConstraint,
    ModifyConstraint,

    // Group operations
    AddGroup,
    DeleteGroup,
    ModifyGroup,

    // Compound operations (multiple sub-commands)
    Compound
};

// =====================================================================
//  Undo Command
// =====================================================================

/// A single undoable command
struct HOBBYCAD_EXPORT UndoCommand {
    CommandType type = CommandType::ModifyEntity;
    std::string description;              ///< Human-readable description

    // Entity data
    Entity entity;                    ///< Current/new entity state
    Entity previousEntity;            ///< Previous entity state (for modify)

    // Constraint data
    Constraint constraint;            ///< Current/new constraint state
    Constraint previousConstraint;    ///< Previous constraint state (for modify)

    // Group data
    Group group;                      ///< Current/new group state
    Group previousGroup;              ///< Previous group state (for modify)

    // For compound commands
    std::vector<UndoCommand> subCommands; ///< Child commands (for Compound type)

    /// Create an entity add command
    static UndoCommand addEntity(const Entity& entity, const std::string& desc = {});

    /// Create an entity delete command
    static UndoCommand deleteEntity(const Entity& entity, const std::string& desc = {});

    /// Create an entity modify command
    static UndoCommand modifyEntity(const Entity& before, const Entity& after,
                                     const std::string& desc = {});

    /// Create a constraint add command
    static UndoCommand addConstraint(const Constraint& constraint, const std::string& desc = {});

    /// Create a constraint delete command
    static UndoCommand deleteConstraint(const Constraint& constraint, const std::string& desc = {});

    /// Create a constraint modify command
    static UndoCommand modifyConstraint(const Constraint& before, const Constraint& after,
                                         const std::string& desc = {});

    /// Create a group add command
    static UndoCommand addGroup(const Group& group, const std::string& desc = {});

    /// Create a group delete command
    static UndoCommand deleteGroup(const Group& group, const std::string& desc = {});

    /// Create a group modify command
    static UndoCommand modifyGroup(const Group& before, const Group& after,
                                    const std::string& desc = {});

    /// Create a compound command from multiple sub-commands
    static UndoCommand compound(const std::vector<UndoCommand>& commands,
                                 const std::string& desc = {});

    /// Check if this is a compound command
    bool isCompound() const { return type == CommandType::Compound; }

    /// Get the number of atomic operations in this command
    int operationCount() const;
};

// =====================================================================
//  Undo Stack
// =====================================================================

/// Multi-level undo/redo stack
class HOBBYCAD_EXPORT UndoStack {
public:
    /// Constructor with optional maximum stack size
    explicit UndoStack(int maxSize = 100);

    /// Push a command onto the undo stack (clears redo stack)
    void push(const UndoCommand& command);

    /// Push multiple commands as a single compound operation
    void pushCompound(const std::vector<UndoCommand>& commands, const std::string& description = {});

    /// Check if undo is available
    bool canUndo() const { return !m_undoStack.empty(); }

    /// Check if redo is available
    bool canRedo() const { return !m_redoStack.empty(); }

    /// Get the number of available undo levels
    int undoLevels() const { return static_cast<int>(m_undoStack.size()); }

    /// Get the number of available redo levels
    int redoLevels() const { return static_cast<int>(m_redoStack.size()); }

    /// Pop the top command from undo stack and push to redo stack
    /// @return The command that was undone (empty if stack was empty)
    UndoCommand undo();

    /// Pop the top command from redo stack and push to undo stack
    /// @return The command that was redone (empty if stack was empty)
    UndoCommand redo();

    /// Undo multiple levels at once
    /// @param levels Number of undo operations to perform
    /// @return Commands that were undone
    std::vector<UndoCommand> undoMultiple(int levels);

    /// Redo multiple levels at once
    /// @param levels Number of redo operations to perform
    /// @return Commands that were redone
    std::vector<UndoCommand> redoMultiple(int levels);

    /// Get the description of the next undo operation
    std::string undoDescription() const;

    /// Get the description of the next redo operation
    std::string redoDescription() const;

    /// Get descriptions of all available undo operations
    std::vector<std::string> undoDescriptions() const;

    /// Get descriptions of all available redo operations
    std::vector<std::string> redoDescriptions() const;

    /// Clear both undo and redo stacks
    void clear();

    /// Clear only the redo stack
    void clearRedo();

    /// Get the maximum stack size
    int maxSize() const { return m_maxSize; }

    /// Set the maximum stack size
    void setMaxSize(int maxSize);

    /// Check if stack has been modified since last clear/construction
    bool isModified() const { return m_modified; }

    /// Mark the stack as unmodified (e.g., after save)
    void setUnmodified() { m_modified = false; }

    /// Begin a compound operation (all pushes until endCompound are grouped)
    void beginCompound(const std::string& description = {});

    /// End a compound operation
    void endCompound();

    /// Check if currently recording a compound operation
    bool isRecordingCompound() const { return m_recordingCompound; }

private:
    std::vector<UndoCommand> m_undoStack;
    std::vector<UndoCommand> m_redoStack;
    int m_maxSize = 100;
    bool m_modified = false;

    // Compound recording state
    bool m_recordingCompound = false;
    std::vector<UndoCommand> m_compoundCommands;
    std::string m_compoundDescription;

    void enforceMaxSize();
};

// =====================================================================
//  Transform Types (for move/copy/rotate/scale/mirror)
// =====================================================================

/// Transform operation types
enum class TransformType {
    Move,
    Copy,
    Rotate,
    Scale,
    Mirror
};

/// Alignment types for multi-selection
enum class AlignmentType {
    Left,
    Right,
    Top,
    Bottom,
    HorizontalCenter,
    VerticalCenter,
    DistributeHorizontal,
    DistributeVertical
};

// =====================================================================
//  Entity Type Names
// =====================================================================

/// Get human-readable name for an entity type
HOBBYCAD_EXPORT const char* entityTypeName(EntityType type);

/// Get localized/translated name for an entity type (uses QObject::tr)
HOBBYCAD_EXPORT std::string entityTypeDisplayName(EntityType type);

}  // namespace sketch
}  // namespace hobbycad

#endif  // HOBBYCAD_SKETCH_UNDO_H
