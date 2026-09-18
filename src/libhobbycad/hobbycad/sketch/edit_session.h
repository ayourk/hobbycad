// =====================================================================
//  src/libhobbycad/hobbycad/sketch/edit_session.h — editing a sketch
//  with its own undo history
// =====================================================================
//
//  Capability tier of the front-end support layer. The edits every front
//  end makes to an open sketch, and the history that undoes them: applying
//  an undo command to a sketch's lists, recording what an edit changed,
//  and the checks a new constraint must pass. The sketch canvas and the
//  command line both use these, so a constraint one refuses the other
//  refuses too, and both can undo inside a sketch.
//
//  The lists are templates: the canvas keeps QVectors of its own entity
//  and constraint types (derived from Entity and Constraint), the command
//  line keeps std::vectors of the library's. Any container with begin(),
//  end(), erase() and push_back() of such elements will do.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_SKETCH_EDIT_SESSION_H
#define HOBBYCAD_SKETCH_EDIT_SESSION_H

#include "../core.h"
#include "constraint.h"
#include "entity.h"
#include "group.h"
#include "undo.h"

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

namespace hobbycad {
namespace sketch {

// ---- Equality ---------------------------------------------------------
//
// Model state only: selection, caches and solver feedback are left out, so
// an edit that changed nothing records nothing.

HOBBYCAD_EXPORT bool sameEntity(const Entity& a, const Entity& b);
HOBBYCAD_EXPORT bool sameConstraint(const Constraint& a, const Constraint& b);
HOBBYCAD_EXPORT bool sameGroup(const Group& a, const Group& b);

// ---- Applying a command -----------------------------------------------

/// Which way a command is applied.
enum class EditDirection {
    Undo,
    Redo,
};

/// Told when applying a command removes something a front end may be
/// holding on to (a selection, a hover).
class HOBBYCAD_EXPORT EditListener {
public:
    virtual ~EditListener();
    virtual void entityRemoved(int entityId);
    virtual void constraintRemoved(int constraintId);
};

/// An EditListener that calls functions; either may be empty.
class HOBBYCAD_EXPORT EditCallbacks : public EditListener {
public:
    EditCallbacks(std::function<void(int)> onEntityRemoved,
                  std::function<void(int)> onConstraintRemoved);
    void entityRemoved(int entityId) override;
    void constraintRemoved(int constraintId) override;

private:
    std::function<void(int)> m_entity;
    std::function<void(int)> m_constraint;
};

namespace detail {

template <class List>
auto findById(List& list, int id) -> decltype(&*list.begin())
{
    for (auto it = list.begin(); it != list.end(); ++it) {
        if (it->id == id) return &*it;
    }
    return nullptr;
}

template <class List>
void removeById(List& list, int id)
{
    list.erase(std::remove_if(list.begin(), list.end(),
                              [id](const typename List::value_type& v) { return v.id == id; }),
               list.end());
}

/// Point a group's members at it, or (-1) clear the members that point at
/// it. Entity::groupId is the back-pointer selection and group drags use;
/// Group::entityIds is the list; both have to agree.
template <class Entities>
void setMembership(Entities& entities, const Group& group, int groupIdOrMinusOne)
{
    for (int id : group.entityIds) {
        auto* e = findById(entities, id);
        if (!e) continue;
        if (groupIdOrMinusOne >= 0) {
            e->groupId = groupIdOrMinusOne;
        } else if (e->groupId == group.id) {
            e->groupId = -1;
        }
    }
}

}  // namespace detail

/// Undo or redo one command on a sketch's lists. A compound is undone in
/// reverse order. A modified element keeps whatever its front-end type adds
/// to the library's (selection state), because only the library part is
/// replaced.
template <class Entities, class Constraints, class Groups>
void applyUndoCommand(const UndoCommand& cmd, EditDirection direction, Entities& entities,
                      Constraints& constraints, Groups& groups,
                      EditListener* listener = nullptr)
{
    using EntityT = typename Entities::value_type;
    using ConstraintT = typename Constraints::value_type;
    using GroupT = typename Groups::value_type;
    const bool undo = direction == EditDirection::Undo;

    switch (cmd.type) {
    case CommandType::AddEntity:
    case CommandType::DeleteEntity:
        if ((cmd.type == CommandType::AddEntity) != undo) {
            entities.push_back(EntityT(cmd.entity));
        } else {
            detail::removeById(entities, cmd.entity.id);
            if (listener) listener->entityRemoved(cmd.entity.id);
        }
        break;

    case CommandType::ModifyEntity:
        if (auto* e = detail::findById(entities, cmd.entity.id)) {
            static_cast<Entity&>(*e) = undo ? cmd.previousEntity : cmd.entity;
        }
        break;

    case CommandType::AddConstraint:
    case CommandType::DeleteConstraint:
        if ((cmd.type == CommandType::AddConstraint) != undo) {
            constraints.push_back(ConstraintT(cmd.constraint));
        } else {
            detail::removeById(constraints, cmd.constraint.id);
            if (listener) listener->constraintRemoved(cmd.constraint.id);
        }
        break;

    case CommandType::ModifyConstraint:
        if (auto* c = detail::findById(constraints, cmd.constraint.id)) {
            static_cast<Constraint&>(*c) = undo ? cmd.previousConstraint : cmd.constraint;
        }
        break;

    case CommandType::AddGroup:
    case CommandType::DeleteGroup:
        if ((cmd.type == CommandType::AddGroup) != undo) {
            groups.push_back(GroupT(cmd.group));
            detail::setMembership(entities, cmd.group, cmd.group.id);
        } else {
            detail::setMembership(entities, cmd.group, -1);
            detail::removeById(groups, cmd.group.id);
        }
        break;

    case CommandType::ModifyGroup:
        if (auto* g = detail::findById(groups, cmd.group.id)) {
            const Group& from = undo ? cmd.group : cmd.previousGroup;
            const Group& to = undo ? cmd.previousGroup : cmd.group;
            detail::setMembership(entities, from, -1);
            static_cast<Group&>(*g) = to;
            detail::setMembership(entities, to, to.id);
        }
        break;

    case CommandType::Compound:
        if (undo) {
            for (auto it = cmd.subCommands.rbegin(); it != cmd.subCommands.rend(); ++it) {
                applyUndoCommand(*it, direction, entities, constraints, groups, listener);
            }
        } else {
            for (const UndoCommand& sub : cmd.subCommands) {
                applyUndoCommand(sub, direction, entities, constraints, groups, listener);
            }
        }
        break;
    }
}

// ---- Recording what changed -------------------------------------------

namespace detail {

template <class List, class Same, class Removed, class Added, class Modified>
void diffList(const List& before, const List& after, Same same, Removed removed, Added added,
              Modified modified, std::vector<UndoCommand>& out)
{
    for (const auto& b : before) {
        bool kept = false;
        for (const auto& a : after) {
            if (a.id != b.id) continue;
            kept = true;
            if (!same(b, a)) out.push_back(modified(b, a));
            break;
        }
        if (!kept) out.push_back(removed(b));
    }
    for (const auto& a : after) {
        bool existed = false;
        for (const auto& b : before) {
            if (b.id == a.id) {
                existed = true;
                break;
            }
        }
        if (!existed) out.push_back(added(a));
    }
}

}  // namespace detail

/// The command that turns the `before` lists into the `after` lists,
/// matched by id: removals, additions and modifications, entities first,
/// then constraints, then groups. Empty (a compound with no steps) when
/// nothing differs.
template <class Entities, class Constraints, class Groups>
UndoCommand diffCommand(const Entities& entitiesBefore, const Constraints& constraintsBefore,
                        const Groups& groupsBefore, const Entities& entitiesAfter,
                        const Constraints& constraintsAfter, const Groups& groupsAfter,
                        const std::string& description)
{
    std::vector<UndoCommand> steps;
    detail::diffList(
        entitiesBefore, entitiesAfter,
        [](const Entity& a, const Entity& b) { return sameEntity(a, b); },
        [](const Entity& e) { return UndoCommand::deleteEntity(e); },
        [](const Entity& e) { return UndoCommand::addEntity(e); },
        [](const Entity& b, const Entity& a) { return UndoCommand::modifyEntity(b, a); },
        steps);
    detail::diffList(
        constraintsBefore, constraintsAfter,
        [](const Constraint& a, const Constraint& b) { return sameConstraint(a, b); },
        [](const Constraint& c) { return UndoCommand::deleteConstraint(c); },
        [](const Constraint& c) { return UndoCommand::addConstraint(c); },
        [](const Constraint& b, const Constraint& a) {
            return UndoCommand::modifyConstraint(b, a);
        },
        steps);
    detail::diffList(
        groupsBefore, groupsAfter,
        [](const Group& a, const Group& b) { return sameGroup(a, b); },
        [](const Group& g) { return UndoCommand::deleteGroup(g); },
        [](const Group& g) { return UndoCommand::addGroup(g); },
        [](const Group& b, const Group& a) { return UndoCommand::modifyGroup(b, a); },
        steps);
    return UndoCommand::compound(steps, description);
}

// ---- Checking a new constraint ----------------------------------------

/// Why a new constraint is refused.
enum class ConstraintProblem {
    None,
    UnknownEntity,     ///< an operand names no entity (`entityId`)
    RepeatedOperand,   ///< the same point of the same entity twice (`entityId`)
    WrongOperands,     ///< the operands are the wrong kind (`reason`)
    BadValue,          ///< a dimension of zero or less
    Redundant,         ///< already implied by the constraints in place
    OverConstrains,    ///< contradicts them (`reason`, `conflictingIds`)
};

/// Which checks to run besides existence and repetition, which always run.
/// The solver checks only a driving constraint, and only when a solver is
/// available.
struct ConstraintCheckOptions {
    bool operands = true;   ///< the kind of each operand
    bool value = true;
    bool solver = true;
};

/// The outcome of checking a constraint.
struct ConstraintCheck {
    ConstraintProblem problem = ConstraintProblem::None;
    int entityId = -1;
    std::string reason;
    std::vector<int> conflictingIds;

    bool ok() const { return problem == ConstraintProblem::None; }
};

/// Check a constraint before it joins a sketch. Existence and repetition
/// are always checked; the rest as `options` say. The constraint should
/// carry the id it will have: the solver ignores id 0.
HOBBYCAD_EXPORT ConstraintCheck checkNewConstraint(const std::vector<Entity>& entities,
                                                   const std::vector<Constraint>& constraints,
                                                   const Constraint& constraint,
                                                   const ConstraintCheckOptions& options = {});

/// The same for any lists of the library's types or types derived from them.
template <class Entities, class Constraints>
ConstraintCheck checkNewConstraint(const Entities& entities, const Constraints& constraints,
                                   const Constraint& constraint,
                                   const ConstraintCheckOptions& options = {})
{
    const std::vector<Entity> es(entities.begin(), entities.end());
    const std::vector<Constraint> cs(constraints.begin(), constraints.end());
    return checkNewConstraint(es, cs, constraint, options);
}

// ---- The session ------------------------------------------------------

/// The undo history of one open sketch, and the edits that go through
/// checks before they are made. A front end holds one per sketch it has
/// open and passes its lists to each call.
class HOBBYCAD_EXPORT EditSession {
public:
    explicit EditSession(int depth = 100);

    const UndoStack& history() const { return m_history; }

    /// Record an edit the caller already made.
    void record(const UndoCommand& command);

    /// Record what an edit changed between two copies of the lists, or
    /// nothing when it changed nothing. True when something was recorded.
    template <class Entities, class Constraints, class Groups>
    bool recordChanges(const Entities& entitiesBefore, const Constraints& constraintsBefore,
                       const Groups& groupsBefore, const Entities& entitiesAfter,
                       const Constraints& constraintsAfter, const Groups& groupsAfter,
                       const std::string& description)
    {
        UndoCommand cmd = diffCommand(entitiesBefore, constraintsBefore, groupsBefore,
                                      entitiesAfter, constraintsAfter, groupsAfter,
                                      description);
        if (cmd.subCommands.empty()) return false;
        record(cmd.subCommands.size() == 1 ? withDescription(cmd.subCommands.front(),
                                                             description)
                                           : cmd);
        return true;
    }

    /// Undo up to `count` edits, newest first, and return them.
    template <class Entities, class Constraints, class Groups>
    std::vector<UndoCommand> undo(Entities& entities, Constraints& constraints, Groups& groups,
                                  int count = 1, EditListener* listener = nullptr)
    {
        std::vector<UndoCommand> done;
        for (int i = 0; i < count && m_history.canUndo(); ++i) {
            done.push_back(m_history.undo());
            applyUndoCommand(done.back(), EditDirection::Undo, entities, constraints, groups,
                             listener);
        }
        return done;
    }

    /// Redo up to `count` edits, oldest first, and return them.
    template <class Entities, class Constraints, class Groups>
    std::vector<UndoCommand> redo(Entities& entities, Constraints& constraints, Groups& groups,
                                  int count = 1, EditListener* listener = nullptr)
    {
        std::vector<UndoCommand> done;
        for (int i = 0; i < count && m_history.canRedo(); ++i) {
            done.push_back(m_history.redo());
            applyUndoCommand(done.back(), EditDirection::Redo, entities, constraints, groups,
                             listener);
        }
        return done;
    }

    /// Check a constraint and, when it passes, add it with the next free id
    /// (unless it has one no constraint uses), mark a driving constraint's
    /// entities constrained, and record the addition. On refusal nothing
    /// changes. `added` receives the constraint as stored.
    template <class Entities, class Constraints>
    ConstraintCheck addConstraint(Entities& entities, Constraints& constraints,
                                  Constraint constraint, const ConstraintCheckOptions& options,
                                  const std::string& description, Constraint* added = nullptr)
    {
        using ConstraintT = typename Constraints::value_type;
        if (constraint.id <= 0 || detail::findById(constraints, constraint.id)) {
            int next = 1;
            for (const auto& c : constraints) next = std::max(next, c.id + 1);
            constraint.id = next;
        }
        ConstraintCheck check = checkNewConstraint(entities, constraints, constraint, options);
        if (!check.ok()) return check;

        std::vector<UndoCommand> steps;
        if (constraint.isDriving) {
            for (int id : constraint.entityIds) {
                auto* e = detail::findById(entities, id);
                if (!e || e->constrained) continue;
                const Entity before = *e;
                e->constrained = true;
                steps.push_back(UndoCommand::modifyEntity(before, *e));
            }
        }
        constraints.push_back(ConstraintT(constraint));
        steps.push_back(UndoCommand::addConstraint(constraint));
        record(steps.size() == 1 ? withDescription(steps.front(), description)
                                 : UndoCommand::compound(steps, description));
        if (added) *added = constraint;
        return check;
    }

    /// Record everything until endStep() as one undo step (an edit and what
    /// the solve after it moved). Steps do not nest.
    void beginStep(const std::string& description) { m_history.beginCompound(description); }
    void endStep() { m_history.endCompound(); }

    /// Forget the history, as when another sketch is opened.
    void clear() { m_history.clear(); }

private:
    static UndoCommand withDescription(UndoCommand cmd, const std::string& description);

    UndoStack m_history;
};

}  // namespace sketch
}  // namespace hobbycad

#endif  // HOBBYCAD_SKETCH_EDIT_SESSION_H
