// =====================================================================
//  src/libhobbycad/hobbycad/bindings.h — key and mouse bindings by command
// =====================================================================
//
//  Capability tier. A binding is text in Qt's portable key notation
//  ("Ctrl+Shift+Z") or a mouse gesture ("RightButton+Drag", "Wheel"); a
//  command has up to three. This file knows how to read those strings,
//  which ones a keyboard can produce, and when two commands may not share
//  one. It does not know the defaults: those are part of the arrangement
//  (hobbycad/layout/arrangement.h), and a front end that lays HobbyCAD out
//  its own way passes its own.
//
//  A default may name a platform key instead of spelling one out
//  ("std:Save"). Only the front end knows what that is on the running
//  platform, so it resolves those before building a Table
//  (standardKeyName()). Bindings are compared as text, so the front end
//  also writes each one in its toolkit's canonical form ("Ctrl+Shift+Z",
//  never "Shift+Ctrl+Z") before handing it over.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_BINDINGS_H
#define HOBBYCAD_BINDINGS_H

#include "core.h"

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

namespace hobbycad {
namespace bindings {

/// How many bindings one command can have.
constexpr int kSlotCount = 3;

/// A command's bindings; an empty string is an unused slot.
using Slots = std::array<std::string, kSlotCount>;

/// What produces a binding.
enum class InputKind {
    None,       ///< empty
    Keyboard,
    Mouse,
};

/// Whether `binding` is a key, a mouse gesture or empty.
HOBBYCAD_EXPORT InputKind classify(const std::string& binding);

/// A binding holding several alternatives ("Ctrl+Shift+Z,Ctrl+Y") split into
/// them, trimmed. A comma that is itself the key ("Ctrl+,") is kept.
HOBBYCAD_EXPORT std::vector<std::string> splitAlternatives(const std::string& binding);

/// Every keyboard sequence in `slots`, alternatives split, mouse gestures
/// left out, in slot order.
HOBBYCAD_EXPORT std::vector<std::string> keySequences(const Slots& slots);

/// For a default written "std:<Name>", the name ("Save"); otherwise "".
HOBBYCAD_EXPORT std::string standardKeyName(const std::string& binding);

/// True when a binding in context `a` and the same binding in context `b`
/// would both answer one key press (commands::BindingContext explains the
/// rule). An unknown context id is treated as application-wide.
HOBBYCAD_EXPORT bool contextsOverlap(const std::string& a, const std::string& b);

/// The current bindings of every command that has any, and the defaults
/// they started from.
class HOBBYCAD_EXPORT Table {
public:
    /// Add a command with its default bindings; its current bindings start
    /// as the defaults. Adding an id twice replaces the defaults and resets
    /// the current bindings.
    void addCommand(const std::string& commandId, const Slots& defaults);

    /// Whether the table has `commandId`.
    bool contains(const std::string& commandId) const;

    /// Command ids in the order they were added.
    const std::vector<std::string>& commandIds() const { return m_order; }

    /// Current bindings; all empty for an unknown id.
    const Slots& current(const std::string& commandId) const;

    /// Default bindings; all empty for an unknown id.
    const Slots& defaults(const std::string& commandId) const;

    /// Set one slot (0 to kSlotCount - 1). False for an unknown id or slot.
    bool set(const std::string& commandId, int slot, const std::string& binding);

    /// Whether a slot holds its default.
    bool isDefault(const std::string& commandId, int slot) const;

    /// Put one command, or every command, back to its defaults.
    void restore(const std::string& commandId);
    void restoreAll();

    /// Another command that `binding` would collide with if `commandId` had
    /// it, or "" when there is none. Each alternative in `binding` is
    /// checked against each alternative of the others.
    std::string findConflict(const std::string& commandId,
                             const std::string& binding) const;

    /// Every pair of commands whose current bindings collide, each pair once
    /// (first added first).
    std::vector<std::pair<std::string, std::string>> conflicts() const;

    /// The command a key sequence triggers among the commands heard where
    /// `contextId`'s are (the same surface, or the same context when it has
    /// none), or "" when none. The first added wins a tie.
    std::string commandForKey(const std::string& contextId,
                              const std::string& sequence) const;

private:
    struct Entry {
        Slots current;
        Slots defaults;
    };
    std::unordered_map<std::string, Entry> m_entries;
    std::vector<std::string> m_order;
};

}  // namespace bindings
}  // namespace hobbycad

#endif  // HOBBYCAD_BINDINGS_H
