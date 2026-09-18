// =====================================================================
//  tests/project/edit_session.cpp — a sketch's edits and their undo
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  The canvas applied undo commands in code of its own, the command line
//  had no undo inside a sketch at all, and the two checked a new
//  constraint differently (the canvas's Constraints menu let a second
//  Horizontal through, which the command line refuses). Both now use
//  sketch/edit_session.h. These checks cover applying commands to lists of
//  any element type, recording what an edit changed, the constraint gate,
//  and the session's history.
// =====================================================================
#include <hobbycad/sketch/edit_session.h>
#include <hobbycad/sketch/solver.h>

#include <cstdio>
#include <deque>
#include <string>
#include <vector>

using namespace hobbycad;
using namespace hobbycad::sketch;

static int failures = 0;
static void check(bool ok, const char* what)
{
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

/// A front end's entity type: the library's plus state of its own.
struct ViewEntity : Entity {
    bool selected = false;
    ViewEntity() = default;
    explicit ViewEntity(const Entity& e) : Entity(e) {}
};

struct ViewConstraint : Constraint {
    bool selected = false;
    ViewConstraint() = default;
    explicit ViewConstraint(const Constraint& c) : Constraint(c) {}
};

template <class List>
static bool hasId(const List& list, int id)
{
    for (const auto& v : list) {
        if (v.id == id) return true;
    }
    return false;
}

static const Entity* byId(const std::vector<Entity>& list, int id)
{
    return findEntityById(list, id);
}

/// A line and a point, pinned so the system is determined: libslvs reports
/// a redundant constraint only then (tests/cli/constraints.cpp).
static void pinned(std::vector<Entity>& es, std::vector<Constraint>& cs, bool horizontal)
{
    es = {createLine(1, Point2D(0, 0), Point2D(10, 0)), createPoint(2, Point2D(0, 0))};
    cs.clear();
    const auto add = [&cs](int id, ConstraintType t, std::vector<int> ids,
                           std::vector<int> points, double v) {
        Constraint c;
        c.id = id;
        c.type = t;
        c.entityIds = std::move(ids);
        c.pointIndices = std::move(points);
        c.value = v;
        cs.push_back(c);
    };
    add(1, ConstraintType::FixedPoint, {2}, {}, 0.0);
    add(2, ConstraintType::Coincident, {1, 2}, {0, 0}, 0.0);
    add(3, ConstraintType::Distance, {1, 2}, {1, 0}, 10.0);
    if (horizontal) add(4, ConstraintType::Horizontal, {1}, {}, 0.0);
}

struct Removed : EditListener {
    std::vector<int> entities, constraints;
    void entityRemoved(int id) override { entities.push_back(id); }
    void constraintRemoved(int id) override { constraints.push_back(id); }
};

int main()
{
    std::printf("sketch edit session\n");

    // ---- applying commands to any element type ---------------------------
    {
        std::deque<ViewEntity> es;
        std::deque<ViewConstraint> cs;
        std::vector<Group> gs;
        Entity line = createLine(1, Point2D(0, 0), Point2D(10, 0));

        applyUndoCommand(UndoCommand::addEntity(line), EditDirection::Redo, es, cs, gs);
        check(es.size() == 1 && es.front().id == 1, "a redone add puts the entity in");

        es.front().selected = true;
        Entity moved = line;
        moved.points[1] = Point3{20, 0, 0};
        const UndoCommand modify = UndoCommand::modifyEntity(line, moved);
        applyUndoCommand(modify, EditDirection::Redo, es, cs, gs);
        check(es.front().points[1].x == 20 && es.front().selected,
              "a modify replaces the geometry and keeps the front end's state");
        applyUndoCommand(modify, EditDirection::Undo, es, cs, gs);
        check(es.front().points[1].x == 10, "undoing it puts the old geometry back");

        Removed removed;
        applyUndoCommand(UndoCommand::addEntity(line), EditDirection::Undo, es, cs, gs, &removed);
        check(es.empty() && removed.entities == std::vector<int>{1},
              "an undone add removes the entity and says so");

        // A compound comes off in reverse: modify, then add.
        const UndoCommand both = UndoCommand::compound(
            {UndoCommand::addEntity(line), UndoCommand::modifyEntity(line, moved)}, "Both");
        applyUndoCommand(both, EditDirection::Redo, es, cs, gs);
        check(es.size() == 1 && es.front().points[1].x == 20, "a compound redoes in order");
        applyUndoCommand(both, EditDirection::Undo, es, cs, gs);
        check(es.empty(), "and undoes in reverse");

        // Two changes to one entity: only the reverse order ends where it began.
        applyUndoCommand(UndoCommand::addEntity(line), EditDirection::Redo, es, cs, gs);
        Entity further = moved;
        further.points[1] = Point3{30, 0, 0};
        const UndoCommand twice = UndoCommand::compound(
            {UndoCommand::modifyEntity(line, moved), UndoCommand::modifyEntity(moved, further)},
            "Twice");
        applyUndoCommand(twice, EditDirection::Redo, es, cs, gs);
        check(es.front().points[1].x == 30, "two changes redo to the last one");
        applyUndoCommand(twice, EditDirection::Undo, es, cs, gs);
        check(es.front().points[1].x == 10, "and undo back to the first state");
        es.clear();

        // A group and its members' back-pointers move together.
        applyUndoCommand(UndoCommand::addEntity(line), EditDirection::Redo, es, cs, gs);
        Group g;
        g.id = 7;
        g.entityIds = {1};
        applyUndoCommand(UndoCommand::addGroup(g), EditDirection::Redo, es, cs, gs);
        check(gs.size() == 1 && es.front().groupId == 7, "an added group claims its members");
        applyUndoCommand(UndoCommand::addGroup(g), EditDirection::Undo, es, cs, gs);
        check(gs.empty() && es.front().groupId == -1, "and lets them go when undone");

        Constraint h;
        h.id = 3;
        h.type = ConstraintType::Horizontal;
        h.entityIds = {1};
        applyUndoCommand(UndoCommand::addConstraint(h), EditDirection::Redo, es, cs, gs);
        applyUndoCommand(UndoCommand::deleteConstraint(h), EditDirection::Redo, es, cs, gs,
                         &removed);
        check(cs.empty() && removed.constraints == std::vector<int>{3},
              "a redone delete removes the constraint and says so");
    }

    // ---- recording what changed ---------------------------------------------
    {
        std::vector<Entity> es{createLine(1, Point2D(0, 0), Point2D(10, 0)),
                               createCircle(2, Point2D(5, 5), 2.0)};
        std::vector<Constraint> cs;
        std::vector<Group> gs;
        const UndoCommand none = diffCommand(es, cs, gs, es, cs, gs, "Nothing");
        check(none.subCommands.empty(), "no change records nothing");

        std::vector<Entity> after = es;
        after.erase(after.begin());                                   // line removed
        after[0].radius = 4.0;                                        // circle changed
        after.push_back(createPoint(3, Point2D(1, 1)));               // point added
        std::vector<Constraint> csAfter = cs;
        Constraint r;
        r.id = 1;
        r.type = ConstraintType::Radius;
        r.entityIds = {2};
        r.value = 4.0;
        csAfter.push_back(r);
        const UndoCommand diff = diffCommand(es, cs, gs, after, csAfter, gs, "Edit");
        check(diff.subCommands.size() == 4, "a removal, a change and two additions are found");

        std::vector<Entity> replay = after;
        std::vector<Constraint> replayCs = csAfter;
        applyUndoCommand(diff, EditDirection::Undo, replay, replayCs, gs);
        check(replay.size() == 2 && hasId(replay, 1) && !hasId(replay, 3)
                  && byId(replay, 2)->radius == 2.0 && replayCs.empty(),
              "undoing the difference gives back the lists it started from");
        applyUndoCommand(diff, EditDirection::Redo, replay, replayCs, gs);
        check(replay.size() == 2 && !hasId(replay, 1) && hasId(replay, 3)
                  && byId(replay, 2)->radius == 4.0 && replayCs.size() == 1,
              "and redoing it gives back the lists it ended with");
    }

    // ---- the constraint gate ----------------------------------------------
    {
        std::vector<Entity> es{createLine(1, Point2D(0, 0), Point2D(10, 0)),
                               createCircle(2, Point2D(5, 5), 2.0)};
        std::vector<Constraint> cs;
        const auto make = [](ConstraintType t, std::vector<int> ids, double v = 0.0) {
            Constraint c;
            c.id = 10;
            c.type = t;
            c.entityIds = std::move(ids);
            c.value = v;
            return c;
        };
        check(checkNewConstraint(es, cs, make(ConstraintType::Horizontal, {1})).ok(),
              "a horizontal line passes");
        const ConstraintCheck unknown =
            checkNewConstraint(es, cs, make(ConstraintType::Horizontal, {9}));
        check(unknown.problem == ConstraintProblem::UnknownEntity && unknown.entityId == 9,
              "an unknown entity is named");
        check(checkNewConstraint(es, cs, make(ConstraintType::Equal, {1, 1})).problem
                  == ConstraintProblem::RepeatedOperand,
              "the same entity twice is refused");
        Constraint closing = make(ConstraintType::Coincident, {1, 1});
        closing.pointIndices = {0, 1};
        check(checkNewConstraint(es, cs, closing, ConstraintCheckOptions{true, true, false}).ok(),
              "but two different points of it are a relation");
        check(checkNewConstraint(es, cs, make(ConstraintType::Horizontal, {2})).problem
                  == ConstraintProblem::WrongOperands,
              "horizontal on a circle is refused before the solver sees it");
        ConstraintCheckOptions noKinds;
        noKinds.operands = false;
        noKinds.solver = false;
        check(checkNewConstraint(es, cs, make(ConstraintType::Horizontal, {2}), noKinds).ok(),
              "unless the kinds are not asked about");
        check(checkNewConstraint(es, cs, make(ConstraintType::Radius, {2}, 0.0)).problem
                  == ConstraintProblem::BadValue,
              "a radius of zero is refused");
        if (Solver::isAvailable()) {
            pinned(es, cs, true);
            Constraint again = make(ConstraintType::Horizontal, {1});
            check(checkNewConstraint(es, cs, again).problem == ConstraintProblem::Redundant,
                  "a second horizontal on a determined line is redundant");
            again.isDriving = false;
            check(checkNewConstraint(es, cs, again).ok(),
                  "unless it is only a reference");
        }
    }

    // ---- the session ------------------------------------------------------
    {
        std::vector<Entity> es;
        std::vector<Constraint> cs;
        std::vector<Group> gs;
        pinned(es, cs, false);
        es[0].points[1] = Point3{10, 3, 0};   // not horizontal yet
        EditSession session;

        Constraint h;
        h.type = ConstraintType::Horizontal;
        h.entityIds = {1};
        Constraint stored;
        const ConstraintCheck added =
            session.addConstraint(es, cs, h, ConstraintCheckOptions{}, "Add Horizontal", &stored);
        check(added.ok() && cs.size() == 4 && stored.id == 4 && es[0].constrained,
              "an added constraint gets an id and marks its entity constrained");
        check(session.history().undoLevels() == 1
                  && session.history().undoDescription() == "Add Horizontal",
              "and is one undo step");

        if (Solver::isAvailable()) {
            const ConstraintCheck again =
                session.addConstraint(es, cs, h, ConstraintCheckOptions{}, "Add Horizontal");
            check(!again.ok() && cs.size() == 4 && session.history().undoLevels() == 1,
                  "a refused constraint changes nothing and records nothing");
        }

        std::vector<UndoCommand> done = session.undo(es, cs, gs);
        check(done.size() == 1 && cs.size() == 3 && !es[0].constrained,
              "undo takes the constraint and the constrained mark back");
        done = session.redo(es, cs, gs);
        check(done.size() == 1 && cs.size() == 4 && es[0].constrained, "redo puts both back");

        const std::vector<Entity> before = es;
        es[0].points[1] = Point3{20, 0, 0};
        check(session.recordChanges(before, cs, gs, es, cs, gs, "Move end"),
              "a recorded change is one step");
        check(!session.recordChanges(es, cs, gs, es, cs, gs, "Nothing"),
              "an unchanged edit is not");

        session.beginStep("Two edits");
        session.record(UndoCommand::addEntity(createPoint(5, Point2D(0, 0))));
        session.record(UndoCommand::addEntity(createPoint(6, Point2D(1, 0))));
        session.endStep();
        check(session.history().undoLevels() == 3
                  && session.history().undoDescription() == "Two edits",
              "a step groups what is recorded inside it");

        es.push_back(createPoint(5, Point2D(0, 0)));
        es.push_back(createPoint(6, Point2D(1, 0)));
        done = session.undo(es, cs, gs, 10);
        check(done.size() == 3 && es.size() == 2 && es[0].points[1].y == 3 && cs.size() == 3,
              "undo with a count stops when the history runs out");
        session.clear();
        check(!session.history().canUndo() && !session.history().canRedo(),
              "clear forgets everything");
    }

    std::printf("\n%s (%d failure(s))\n", failures ? "FAILURES" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
