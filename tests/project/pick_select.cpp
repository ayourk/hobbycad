// =====================================================================
//  tests/project/pick_select.cpp — picking, selecting and dragging
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  The sketch canvas decided what a click hit, how the selection changed
//  and how a handle drag moved; a second front end had to copy all of it.
//  These pin the library versions (sketch/view.h, pick.h, selection.h,
//  handle_drag.h), and the defects the move fixed:
//    - a window was a sketch-axis rectangle, so in a turned view it was not
//      the rectangle drawn on screen;
//    - a window's crossing mode followed the sketch's x, so in a flipped
//      view a right-to-left drag was a plain window;
//    - deselecting the primary entity left it primary;
//    - Shift, Ctrl or an axis key pressed mid-drag moved the handle without
//      entity snaps and without the guard that keeps an edge from
//      collapsing;
//    - the axis guide named the wrong model axis on the YZ plane.
// =====================================================================
#include <hobbycad/project.h>
#include <hobbycad/sketch/handle_drag.h>
#include <hobbycad/sketch/pick.h>
#include <hobbycad/sketch/selection.h>
#include <hobbycad/sketch/view.h>

#include <cmath>
#include <cstdio>

using namespace hobbycad;
using namespace hobbycad::sketch;

static int failures = 0;
static void check(bool ok, const char* what)
{
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

static bool near(const Point2D& a, const Point2D& b, double eps = 1e-9)
{
    return std::fabs(a.x - b.x) < eps && std::fabs(a.y - b.y) < eps;
}

static Entity inGroup(Entity e, int group)
{
    e.groupId = group;
    return e;
}

static Group group(int id, std::vector<int> members, GroupKind kind = GroupKind::User)
{
    Group g;
    g.id = id;
    g.kind = kind;
    g.entityIds = std::move(members);
    return g;
}

int main()
{
    std::printf("pick and select\n");

    // ---- the view ------------------------------------------------------------
    {
        SketchView v;
        v.center = {10, 5};
        v.zoom = 4.0;
        v.width = 800;
        v.height = 600;
        check(near(v.toScreen({10, 5}), {400, 300}), "the center is mid-view");
        check(near(v.toScreen({11, 5}), {404, 300}) && near(v.toScreen({10, 6}), {400, 296}),
              "sketch x runs right, y runs up");
        v.rotationDeg = 90.0;
        check(near(v.toScreen({11, 5}), {400, 296}), "a quarter turn turns x up");
        v.rotationDeg = 30.0;
        v.flipped = true;
        const Point2D p{3.5, -7.25};
        check(near(v.toSketch(v.toScreen(p)), p, 1e-9), "screen and sketch map back and forth");
        check(!v.axisAligned(), "a 30-degree view is turned");
        v.rotationDeg = -270.0;
        check(v.axisAligned(), "a three-quarter turn is not");
        check(std::fabs(v.sketchLength(8.0) - 2.0) < 1e-12, "pixels in sketch units");
    }

    // ---- picking ----------------------------------------------------------------
    {
        const std::vector<Entity> sketch = {
            createLine(1, {0, 0}, {10, 0}),
            createLine(2, {0, 0}, {10, 0}),                                   // on top
            inGroup(createLine(3, {20, 0}, {30, 0}), 7),                      // sweep rig
            inGroup(createArc(4, {25, 0}, 5, 0, 180), 7),
            createCircle(5, {50, 0}, 5),
        };
        const std::vector<Group> groups = {group(7, {3, 4}, GroupKind::SweepAngle)};
        check(pickEntity(sketch, groups, {5, 0.1}, 0.5) == 2, "the top-most entity wins");
        check(pickEntity(sketch, groups, {25, 0.1}, 0.5) == -1,
              "a sweep-angle rig's construction line is not picked");
        check(pickEntity(sketch, groups, {5, 3}, 0.5) == -1, "empty space");

        Entity text;
        text.id = 9;
        text.type = EntityType::Text;
        text.points = {Point3(0, 0, 0)};
        const std::vector<Entity> withText = {text};
        check(pickEntity(withText, groups, {1, 1}, 0.5) == -1, "no text test, no text hit");
        check(pickEntity(withText, groups, {1, 1}, 0.5,
                         [](const Entity&, const Point2D& at) { return at.x < 2; }) == 9,
              "the front end measures text");

        const PickResult pt = pickPoint(sketch, {9.8, 0.1}, 0.5);
        check(pt.kind == PickKind::Point && pt.index == 1, "the nearest point");

        // Handles: a grouped primary offers its group's; the nearest wins.
        const std::vector<Entity> rect = {inGroup(createLine(10, {0, 0}, {10, 0}), 3),
                                          inGroup(createLine(11, {10, 0}, {10, 5}), 3)};
        const std::vector<Group> rectGroup = {group(3, {10, 11})};
        const PickResult h = pickHandle(rect, rectGroup, 10, -1, {10.1, 5.1}, 0.5);
        check(h.kind == PickKind::Handle && h.id == 11 && h.index == 1,
              "a grouped primary offers every member's handles");
        check(!pickHandle(rect, rectGroup, 10, 3, {10.1, 5.1}, 0.5).hit(),
              "inside the group only the primary's own");
        check(!pickHandle(sketch, groups, 3, -1, {20, 0}, 0.5).hit(),
              "a sweep-angle construction line offers none");

        Entity bez = createBezierSpline(20, {{0, 10}, {3, 14}, {7, 14}, {10, 10}});
        const std::vector<Entity> splines = {bez};
        check(!pickBezierLeg(splines, -1, {}, {1.5, 12.1}, 0.5).hit(),
              "a leg of an unselected spline is hidden");
        const PickResult leg = pickBezierLeg(splines, 20, {}, {1.5, 12.1}, 0.5);
        check(leg.kind == PickKind::BezierLeg && leg.index == 0 && leg.index2 == 1,
              "a shown leg is picked by its two ends");
        check(pickBezierLeg(splines, -1, {20}, {1.5, 12.1}, 0.5).hit(),
              "a spline with a selected point shows its legs");

        check(pickMidpoint(sketch, {5, 0.2}, 0.5).id == 2,
              "a midpoint grip, the last drawn on a tie");

        // A line held tangent to a circle beyond its end: the contact dot.
        std::vector<Entity> tangent = {createLine(30, {-20, 5}, {-10, 5}),
                                       createCircle(31, {0, 0}, 5)};
        Constraint t;
        t.type = ConstraintType::Tangent;
        t.entityIds = {30, 31};
        const std::vector<Constraint> constraints = {t};
        const PickResult dot = pickTangentContact(tangent, constraints, {0, 5.2}, 0.5);
        check(dot.kind == PickKind::TangentContact && dot.id == 31, "a tangent contact dot");

        // The Select tool's order.
        SelectPick in;
        in.at = {10, 0.05};
        in.tolerances = PickTolerances{}.inSketchUnits(10.0);
        in.primaryId = 1;
        in.frontEnd.constraintLabel = 44;
        const std::vector<Constraint> none;
        check(pickForSelect(sketch, groups, none, in).kind == PickKind::Handle,
              "a handle of the selection comes first");
        in.primaryId = -1;
        check(pickForSelect(sketch, groups, none, in).kind == PickKind::ConstraintLabel,
              "then what the front end drew");
        in.frontEnd = FrontEndHits();
        check(pickForSelect(sketch, groups, none, in).kind == PickKind::Point,
              "then a point");
        in.at = {3, 0.05};
        check(pickForSelect(sketch, groups, none, in).kind == PickKind::Entity, "then the entity");
        in.filter = SelectFilter::PointsOnly;
        check(!pickForSelect(sketch, groups, none, in).hit(),
              "points only: a curve is empty space");
        in.filter = SelectFilter::CurvesOnly;
        in.at = {10, 0.05};
        const PickResult curve = pickForSelect(sketch, groups, none, in);
        check(curve.kind == PickKind::Entity && curve.id == 2, "curves only: no point");
        in.filter = SelectFilter::All;
        in.at = {5, 0.3};
        check(pickForSelect(sketch, groups, none, in).kind == PickKind::Midpoint,
              "a midpoint grip before its line");

        const std::vector<Entity> slotSketch = {createSlot(40, {0, 0}, {20, 0}, 2)};
        in.at = {10, 2.05};   // a side's midpoint, not one of the slot's own points
        const PickResult anchor = pickForSelect(slotSketch, groups, none, in);
        check(anchor.kind == PickKind::SlotAnchor && anchor.id == 40, "a slot's anchor point");
    }

    // ---- selection ------------------------------------------------------------------
    {
        const std::vector<Entity> sketch = {inGroup(createLine(1, {0, 0}, {1, 0}), 5),
                                            inGroup(createLine(2, {1, 0}, {1, 1}), 5),
                                            createLine(3, {5, 5}, {6, 6}),
                                            createCircle(4, {20, 20}, 3)};
        SelectionState s;
        selectEntity(s, sketch, 1, false, false);
        check(s.entities.size() == 2 && s.primary == 1, "a click takes the whole group");
        selectEntity(s, sketch, 3, true, false);
        check(s.entities.size() == 3 && s.primary == 3, "an extending click adds");
        selectEntity(s, sketch, 3, true, false);
        check(s.entities.size() == 2 && s.primary == 2,
              "deselecting the primary hands over to the last selected");
        selectEntity(s, sketch, 2, true, false);
        check(s.entities.empty() && s.primary == -1, "Ctrl on a member drops its group");
        selectEntity(s, sketch, 1, false, true);
        check(s.entities.size() == 1, "an individual click takes one member");

        s = SelectionState();
        clickEntity(s, sketch, 4, ClickSelect::Replace);
        clickEntity(s, sketch, 3, ClickSelect::Add);
        clickEntity(s, sketch, 3, ClickSelect::Add);
        check(s.has(3) && s.entities.size() == 2, "Shift never removes");

        s.constraint = 9;
        clickEntity(s, sketch, 4, ClickSelect::Replace);
        check(s.entities == std::vector<int>{4} && s.constraint == -1, "a plain click replaces");

        enterGroup(s, 5);
        check(s.entities.empty() && s.enteredGroup == 5, "entering a group clears");
        selectEntity(s, sketch, 2, false, false);
        check(s.entities == std::vector<int>{2}, "inside a group a click takes one member");
        const bool left = selectEntity(s, sketch, 3, false, false);
        check(left && s.enteredGroup == -1 && s.entities == std::vector<int>{3},
              "a click outside the group leaves it");
        enterGroup(s, 5);
        leaveGroup(s, sketch);
        check(s.entities.size() == 2 && s.enteredGroup == -1, "leaving selects the group");

        selectPoint(s, 3, 0, ClickSelect::Replace);
        selectPoint(s, 3, 1, ClickSelect::Add);
        selectPoint(s, 3, 0, ClickSelect::Toggle);
        check(s.entities.empty() && s.points.size() == 1 && s.points[0].second == 1,
              "points replace, add and toggle");
        selectConstraint(s, 12);
        check(s.constraint == 12 && s.entities.empty(), "a constraint deselects entities");

        check(windowIsCrossing({100, 0}, {50, 0}) && !windowIsCrossing({50, 0}, {100, 0}),
              "right to left on screen is crossing");

        // A diagonal line and a 45-degree window around it.
        const Entity diag = createLine(7, {0, 0}, {10, 10});
        const std::array<Point2D, 4> turned = {Point2D(-1, 0), Point2D(0, -1),
                                               Point2D(11, 10), Point2D(10, 11)};
        check(entityInWindow(diag, turned, false, false),
              "a turned window encloses what lies inside it");
        check(!entityInWindow(createLine(8, {0, 0}, {10, -5}), turned, false, false)
                  && entityInWindow(createLine(8, {0, 0}, {10, -5}), turned, false, true),
              "a crossing window takes what it touches");
        check(!entityInWindow(createLine(9, {20, 0}, {30, 0}), turned, false, true),
              "and not what it misses");
        const std::array<Point2D, 4> inside = {Point2D(19, 19), Point2D(21, 19),
                                               Point2D(21, 21), Point2D(19, 21)};
        check(entityInWindow(createCircle(10, {20, 20}, 5), inside, false, true),
              "a window inside a circle touches it");
        const std::array<Point2D, 4> box = {Point2D(-1, -1), Point2D(11, -1), Point2D(11, 11),
                                            Point2D(-1, 11)};
        check(entityInWindow(diag, box, true, false), "an upright window uses the exact test");

        SelectionState w;
        w.entities = {4};
        selectWindow(w, sketch, entitiesInWindow(sketch, box, true, false), true);
        check(w.entities.size() == 4 && w.has(4), "a kept window adds, groups whole");
        selectWindow(w, sketch, {3}, false);
        check(w.entities == std::vector<int>{3} && w.primary == 3, "a plain window replaces");
    }

    // ---- handle drags --------------------------------------------------------------------
    {
        check(dragAxisForKey(SketchPlane::XY, 'Y') == DragAxis::Vertical
                  && dragAxisForKey(SketchPlane::XZ, 'Z') == DragAxis::Vertical
                  && dragAxisForKey(SketchPlane::YZ, 'Y') == DragAxis::Horizontal
                  && dragAxisForKey(SketchPlane::XZ, 'Y') == DragAxis::None,
              "an axis key names the plane's own axes, not its normal");
        check(dragAxisLetter(SketchPlane::YZ, DragAxis::Horizontal) == 'Y'
                  && dragAxisLetter(SketchPlane::XZ, DragAxis::Vertical) == 'Z'
                  && dragAxisLetter(SketchPlane::XY, DragAxis::None) == 0,
              "and the guide names the key's axis");

        const Point2D raw{3, 4}, snapped{3.5, 4.5}, original{1, 1};
        check(near(handleDragTarget(raw, snapped, original, DragAxis::None, false), snapped),
              "without an axis the snapped cursor");
        check(near(handleDragTarget(raw, snapped, original, DragAxis::Horizontal, false), {3, 1}),
              "held horizontal from the raw cursor");
        check(near(handleDragTarget(raw, snapped, original, DragAxis::Vertical, true), {1, 4.5}),
              "held vertical from the snapped one when snapping");

        Entity projected = createLine(1, {0, 0}, {1, 0});
        projected.projectionSourceId = 4;
        std::vector<Group> groups = {group(2, {3})};
        groups[0].locked = true;
        check(handleDragRefusal(projected, groups) == DragRefusal::Projected
                  && handleDragRefusal(inGroup(createLine(3, {0, 0}, {1, 0}), 2), groups)
                         == DragRefusal::Locked
                  && handleDragRefusal(createLine(5, {0, 0}, {1, 0}), groups)
                         == DragRefusal::None,
              "projected and locked geometry is not dragged");

        std::vector<Entity> sketch = {createLine(1, {0, 0}, {10, 0})};
        const std::vector<Group> noGroups;
        std::vector<Constraint> constraints;
        HandleDrag drag;
        check(!drag.begin(sketch, noGroups, constraints, 1, 5) && !drag.active(),
              "a handle the entity lacks starts nothing");
        check(drag.begin(sketch, noGroups, constraints, 1, 1) && near(drag.original(), {10, 0}),
              "a drag keeps where it began");
        const Point2D kept = drag.target(sketch, {0, 0}, {0, 0}, DragAxis::None, false, 0.5);
        check(std::fabs(geometry::lineLength(kept, {0, 0}) - 0.5) < 1e-9,
              "the handle stays clear of the other end");
        check(!drag.record(sketch, constraints), "an unmoved drag records nothing");
        sketch[0].points[1] = Point3(12, 0, 0);
        const auto step = drag.record(sketch, constraints);
        check(step && step->type == CommandType::ModifyEntity
                  && step->previousEntity.points[1].x == 10,
              "a moved one records the entity as it was");
        drag.end();
        check(!drag.active() && !drag.record(sketch, constraints), "an ended drag records nothing");

        // A group drag records every member it moved, and the group's labels.
        std::vector<Entity> rect = {inGroup(createLine(1, {0, 0}, {10, 0}), 3),
                                    inGroup(createLine(2, {10, 0}, {10, 5}), 3)};
        std::vector<Group> rectGroup = {group(3, {1, 2})};
        Constraint label;
        label.id = 8;
        label.labelPosition = {5, -2};
        rectGroup[0].constraintIds = {8};
        std::vector<Constraint> labels = {label};
        drag.begin(rect, rectGroup, labels, 1, 1);
        rect[0].points[1] = Point3(11, 0, 0);
        rect[1].points[0] = Point3(11, 0, 0);
        labels[0].labelPosition = {5.5, -2};
        const auto group = drag.record(rect, labels);
        check(drag.coversGroup() && group && group->isCompound()
                  && group->subCommands.size() == 3,
              "a group drag is one step for every member and label it moved");

        // A circle opened at a cut.
        Entity arc = createArc(6, {0, 0}, 5, 0, 360);
        resyncArcEndpoints(arc);
        const std::vector<Entity> arcs = {arc};
        drag.begin(arcs, noGroups, constraints, 6, 2);
        check(drag.opensFullArc() && drag.openArcDraggedIndex() == 2
                  && std::fabs(drag.openArcSweep() - 360.0) < 1e-9,
              "grabbing a full arc's end opens it");
        drag.setOpenArc(1, 200.0);
        check(drag.openArcDraggedIndex() == 1 && drag.openArcSweep() == 200.0,
              "and follows the opening");
        drag.begin(arcs, noGroups, constraints, 6, 0);
        check(!drag.opensFullArc(), "its center does not open it");
    }

    std::printf("\n%s (%d failure(s))\n", failures ? "FAILURES" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
