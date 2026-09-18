// =====================================================================
//  tests/project/placement.cpp — placing entities by clicks
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  The drawing tools' rules moved out of the Qt handlers into
//  sketch/placement.h. These pin what each tool does with its clicks, and
//  the defects the move fixed:
//    - the 3-point arc ended at the "point on the arc" click and ran
//      through the "end point" click, the reverse of its prompts;
//    - a locked value moved the preview in only some tools and the click
//      in others (the ellipse's click ignored a locked radius; the tangent
//      arc's ignored both its locks);
//    - an arc slot's preview clamped its end one way and the click another;
//    - a locked angle of exactly -1 degree was read as "not locked";
//    - an arc slot tessellated as a straight capsule from its center to its
//      start.
// =====================================================================
#include <hobbycad/geometry/utils.h>
#include <hobbycad/sketch/dimension_input.h>
#include <hobbycad/sketch/placement.h>
#include <hobbycad/sketch/queries.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

using namespace hobbycad;
using namespace hobbycad::sketch;

static int failures = 0;
static void check(bool ok, const char* what)
{
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

static bool near(double a, double b, double eps = 1e-6) { return std::fabs(a - b) < eps; }
static bool near(const Point2D& a, const Point2D& b, double eps = 1e-6)
{
    return near(a.x, b.x, eps) && near(a.y, b.y, eps);
}
static double dist(const Point2D& a, const Point2D& b) { return geometry::lineLength(a, b); }

static PlacementInput clicks(std::vector<Point2D> pts, Point2D cursor = {})
{
    PlacementInput in;
    in.clicks = std::move(pts);
    in.cursor = cursor;
    return in;
}

static PlacementInput locked(PlacementInput in, StageLocks locks)
{
    in.locks = std::move(locks);
    return in;
}

static bool build(PlacementKind k, const PlacementInput& in, Entity& e)
{
    e = Entity();
    return placementEntity(k, in, e);
}

static int countShapes(const PlacementPreview& p, PreviewShape::Kind kind, int field = -2)
{
    int n = 0;
    for (const PreviewShape& s : p.shapes) {
        if (s.kind == kind && (field == -2 || s.field == field)) ++n;
    }
    return n;
}

static constexpr auto none = std::nullopt;

int main()
{
    std::printf("placement\n");
    Entity e;

    // ---- stages ------------------------------------------------------------
    {
        const PlacementKind all[] = {
            {SketchTool::Line, CreationMode::LineTwoPoint},
            {SketchTool::Line, CreationMode::LineTangent},
            {SketchTool::Rectangle, CreationMode::RectParallelogram},
            {SketchTool::Circle, CreationMode::CircleThreeTangent},
            {SketchTool::Arc, CreationMode::ArcStartEndRadius},
            {SketchTool::Slot, CreationMode::SlotArcEnds},
            {SketchTool::Polygon, CreationMode::PolygonFreeform},
            {SketchTool::Ellipse, CreationMode::EllipseEndpointsArc},
            {SketchTool::Spline, CreationMode::SplineConic},
        };
        bool prompts = true;
        for (const PlacementKind& k : all) {
            for (int placed = 0; placed < 5; ++placed) {
                PlacementStage s;
                s.placed = placed;
                s.targets = placed > 0 ? 3 : 0;
                prompts = prompts && std::strlen(placementPrompt(k, s)) > 0;
            }
        }
        check(prompts, "every stage of every placement has a prompt");
        check(std::strcmp(placementContext(), "hobbycad::SketchCanvas") == 0,
              "prompts keep the context their translations are filed under");

        PlacementStage s;
        s.placed = 1;
        const auto f = placementFields({SketchTool::Rectangle, CreationMode::RectParallelogram}, s);
        s.placed = 2;
        const auto g = placementFields({SketchTool::Rectangle, CreationMode::RectParallelogram}, s);
        check(f == std::vector<DimField>{DimField::Edge1, DimField::Edge1Angle}
                  && g == std::vector<DimField>{DimField::Edge2, DimField::Edge2Angle},
              "a parallelogram asks for each edge in turn");
        s.placed = 0;
        s.targets = 1;
        check(placementFields({SketchTool::Arc, CreationMode::ArcTangent}, s).size() == 2,
              "a tangent arc's fields come with its target");
        check(placementClicks({SketchTool::Rectangle, CreationMode::RectThreePoint}) == 3
                  && placementClicks({SketchTool::Spline, CreationMode::SplineFitPoints}) == 0
                  && placementClicks({SketchTool::Ellipse, CreationMode::EllipseArc}) == 5
                  && placementTargets({SketchTool::Circle, CreationMode::CircleTwoTangent}) == 2,
              "click and target counts");
        s.placed = 2;
        s.targets = 0;
        check(placementFlippable({SketchTool::Slot, CreationMode::SlotArcRadius}, s)
                  && !placementFlippable({SketchTool::Slot, CreationMode::SlotOverall}, s),
              "only arcs flip");
        check(placementCanSwitch({SketchTool::Line, CreationMode::LineTwoPoint},
                                 CreationMode::LineConstruction)
                  && !placementCanSwitch({SketchTool::Line, CreationMode::LineTwoPoint},
                                         CreationMode::LineTangent),
              "a line becomes construction mid-placement, not tangent");
        check(placementEntityType({SketchTool::Rectangle, CreationMode::RectParallelogram})
                  == EntityType::Parallelogram,
              "a parallelogram is its own entity type");

        DimensionInput input;
        input.addField(DimField::Length, "Length");
        input.addField(DimField::Angle, "Angle");
        input.beginStates();
        input.setLiveValue(1, -1.0);
        DimKeyPress tab;
        tab.key = DimKey::Tab;
        input.handleKey(tab, false);
        DimKeyPress enter;
        enter.key = DimKey::Enter;
        input.handleKey(enter, false);
        const StageLocks locks = stageLocks(input);
        check(locks.size() == 2 && !locks[0] && locks[1] && near(*locks[1], -1.0),
              "an angle locked at -1 degree is a lock");
    }

    // ---- line ---------------------------------------------------------------
    {
        const PlacementKind line{SketchTool::Line, CreationMode::LineTwoPoint};
        const PlacementKind horizontal{SketchTool::Line, CreationMode::LineHorizontal};
        PlacementInput in = clicks({{0, 0}}, {10, 5});
        check(near(placementCursor(horizontal, in, in.cursor, false), {10, 0}),
              "a horizontal line holds the cursor on its axis");
        in.cursor = {10, 0.0005};
        check(near(placementCursor(horizontal, in, in.cursor, true), {10, 0.0005}),
              "a snap already on the axis is kept");
        in.cursor = {3, 4};
        check(near(placementCursor(line, locked(in, {10.0, none}), in.cursor, false), {6, 8}),
              "a locked length scales along the cursor's direction");
        check(near(placementCursor(line, locked(in, {none, -1.0}), in.cursor, false),
                   geometry::polarPoint({0, 0}, 5.0, -M_PI / 180.0)),
              "a line locked at -1 degree follows it");
        check(build(line, in, e) && e.type == EntityType::Line
                  && near(Point2D(e.points[1]), {3, 4}),
              "the cursor completes the line");
        check(!build(line, clicks({{0, 0}}, {0.05, 0}), e), "a line too short is refused");

        // Tangent: from (0,10) to a unit circle at the origin... radius 5.
        const PlacementKind tangent{SketchTool::Line, CreationMode::LineTangent};
        PlacementInput t = clicks({{0, 10}}, {6, -2});
        t.targets.push_back(createCircle(7, {0, 0}, 5.0));
        const Point2D foot = placementCursor(tangent, t, t.cursor, false);
        const Point2D dir = geometry::normalize(foot - Point2D(0, 10));
        const double reach = std::fabs(geometry::cross(dir, Point2D(0, 0) - Point2D(0, 10)));
        check(near(reach, 5.0, 1e-9), "a tangent line's cursor rides a tangent of its circle");
        std::vector<Point2D> moved;
        const Point2D end =
            placementCursor(tangent, locked(t, {none, 0.0}), t.cursor, false, &moved);
        check(moved.size() == 1 && near(dist(moved[0], {0, 0}), 5.0) && near(end.y, moved[0].y),
              "a locked angle slides the start round the circle to a tangent at that angle");
        check(placementPreview(tangent, clicks({})).shapes.empty(), "nothing to show yet");
        PlacementInput picked = clicks({});
        picked.targets = t.targets;
        const PlacementPreview shown = placementPreview(tangent, picked);
        check(shown.shapes.size() == 1 && shown.shapes[0].stroke == PreviewStroke::Target,
              "a picked circle is shown before the line starts");

        const PlacementPreview pv = placementPreview(line, clicks({{0, 0}}, {0, 10}));
        check(pv.fieldValues.size() == 2 && near(pv.fieldValues[0], 10.0)
                  && near(pv.fieldValues[1], 90.0)
                  && countShapes(pv, PreviewShape::Kind::Dimension, 0) == 1
                  && countShapes(pv, PreviewShape::Kind::Dimension, 1) == 1,
              "the preview carries the length and angle and where to type them");
    }

    // ---- rectangle ----------------------------------------------------------
    {
        const PlacementKind corner{SketchTool::Rectangle, CreationMode::RectCorner};
        const PlacementKind center{SketchTool::Rectangle, CreationMode::RectCenter};
        const PlacementKind three{SketchTool::Rectangle, CreationMode::RectThreePoint};
        const PlacementKind para{SketchTool::Rectangle, CreationMode::RectParallelogram};
        PlacementInput in = clicks({{0, 0}}, {-7, 3});
        check(near(placementCursor(corner, locked(in, {4.0, none}), in.cursor, false), {-4, 3}),
              "a locked width keeps the cursor's side");
        check(build(center, clicks({{1, 1}}, {3, 2}), e) && e.points.size() == 2
                  && near(Point2D(e.points[0]), {-1, 0}) && near(Point2D(e.points[1]), {3, 2}),
              "a center rectangle is stored by its corners");
        check(build(three, clicks({{0, 0}, {10, 0}}, {4, 3}), e) && e.points.size() == 4
                  && near(Point2D(e.points[2]), {10, 3}),
              "a 3-point rectangle stores four corners");
        PlacementInput w = clicks({{0, 0}, {10, 0}}, {4, -9});
        check(near(placementCursor(three, locked(w, {2.0}), w.cursor, false), {4, -2}),
              "its locked width is across the first edge, on the cursor's side");
        PlacementInput pg = clicks({{0, 0}, {10, 0}}, {12, 5});
        const Point2D p3 = placementCursor(para, locked(pg, {5.0, 60.0}), pg.cursor, false);
        check(near(p3, {7.5, 5.0 * std::sqrt(3.0) / 2.0}),
              "a parallelogram's inside angle is measured at the second corner");
        check(build(para, clicks({{0, 0}, {10, 0}, {12, 5}}), e) && e.points.size() == 4
                  && near(Point2D(e.points[3]), {2, 5}),
              "and its fourth corner closes it");

        PlacementInput turn = locked(clicks({{0, 0}}, {0, 10}), {4.0, 2.0});
        turn.turn = captureLockedTurn({0, 0}, {10, 0});
        check(build(corner, turn, e) && e.points.size() == 4
                  && near(Point2D(e.points[1]), {0, 4}) && near(Point2D(e.points[3]), {-2, 0}),
              "with both sides locked the cursor turns the rectangle");
        check(near(placementCursor(corner, turn, turn.cursor, false), turn.cursor),
              "and the cursor stays where it is");
        const PlacementPreview rp = placementPreview(corner, clicks({{0, 0}}, {4, 3}));
        check(rp.fieldValues.size() == 2 && near(rp.fieldValues[0], 4)
                  && near(rp.fieldValues[1], 3),
              "the preview measures width and height");
    }

    // ---- circle ------------------------------------------------------------------
    {
        const PlacementKind cr{SketchTool::Circle, CreationMode::CircleCenterRadius};
        const PlacementKind tp{SketchTool::Circle, CreationMode::CircleTwoPoint};
        const PlacementKind p3{SketchTool::Circle, CreationMode::CircleThreePoint};
        PlacementInput in = locked(clicks({{0, 0}}, {0, 3}), {7.0});
        const Point2D at = placementCursor(cr, in, in.cursor, false);
        in.cursor = at;
        check(near(at, {0, 7}) && build(cr, in, e) && near(e.radius, 7.0),
              "a locked radius places the perimeter point and sets the radius");
        check(build(tp, clicks({{-2, 0}}, {2, 0}), e) && e.points.size() == 3
                  && near(Point2D(e.points[0]), {0, 0}) && near(e.radius, 2.0),
              "a 2-point circle stores its center and both diameter ends");
        check(build(p3, clicks({{-5, 0}, {5, 0}}, {0, 5}), e) && e.points.size() == 4
                  && near(e.radius, 5.0) && near(Point2D(e.points[0]), {0, 0}),
              "a 3-point circle stores its center and the three clicks");
        check(build(p3, locked(clicks({{-5, 0}, {5, 0}}, {0, -1}), {13.0}), e)
                  && near(e.radius, 13.0) && near(Point2D(e.points[0]), {0, -12})
                  && near(dist(Point2D(e.points[3]), {0, -12}), 13.0),
              "with a locked radius the third click picks the center's side");

        const PlacementKind tan2{SketchTool::Circle, CreationMode::CircleTwoTangent};
        PlacementInput t = clicks({{3, 3}});
        t.targets = {createLine(1, {0, 0}, {10, 0}), createLine(2, {0, 0}, {0, 10})};
        check(build(tan2, t, e) && near(dist(Point2D(e.points[0]), {0, 0}) * std::sqrt(0.5),
                                        e.radius, 1e-6),
              "a circle tangent to two lines sits in their corner");
        check(near(e.radius, std::sqrt(18.0), 1e-6),
              "its size is how far the placing click is from their crossing");
        t.targets.pop_back();
        check(!build(tan2, t, e), "it needs both lines");
    }

    // ---- arc -------------------------------------------------------------------
    {
        const PlacementKind three{SketchTool::Arc, CreationMode::ArcThreePoint};
        check(build(three, clicks({{10, 0}, {-10, 0}, {0, 10}}), e)
                  && near(Point2D(e.points[1]), {10, 0}) && near(Point2D(e.points[2]), {-10, 0})
                  && near(e.sweepAngle, 180.0),
              "a 3-point arc runs from its start to its end through the third click");
        check(build(three, clicks({{10, 0}, {-10, 0}, {0, -10}}), e) && near(e.sweepAngle, -180.0),
              "the third click decides which way round");

        const PlacementKind cse{SketchTool::Arc, CreationMode::ArcCenterStartEnd};
        PlacementInput in = clicks({{0, 0}, {10, 0}}, {0, 3});
        check(near(placementCursor(cse, in, in.cursor, false), {0, 10}),
              "the end rides the circle the start set");
        check(build(cse, in, e) && near(e.sweepAngle, 90.0), "the short way round by default");
        in.flipped = true;
        check(build(cse, in, e) && near(e.sweepAngle, -270.0), "Shift takes the long way");
        in.flipped = false;
        check(near(placementCursor(cse, locked(in, {45.0}), in.cursor, false),
                   geometry::polarPoint({0, 0}, 10.0, M_PI / 4)),
              "a locked sweep places the end");

        const PlacementKind ser{SketchTool::Arc, CreationMode::ArcStartEndRadius};
        check(build(ser, clicks({{-10, 0}, {10, 0}}, {0, 30}), e)
                  && std::fabs(e.sweepAngle) < 180.0,
              "a center far from the chord makes the short arc");
        check(build(ser, clicks({{-10, 0}, {10, 0}}, {0, 2}), e)
                  && std::fabs(e.sweepAngle) > 180.0,
              "a center near it makes the long arc");
        PlacementInput semi = clicks({{-10, 0}, {10, 0}}, {0, 30});
        semi.semicircle = true;
        check(build(ser, semi, e) && near(std::fabs(e.sweepAngle), 180.0) && near(e.radius, 10.0),
              "Ctrl makes a half circle");

        const PlacementKind tangent{SketchTool::Arc, CreationMode::ArcTangent};
        PlacementInput t = clicks({{10, 0}}, {20, 10});
        t.targets.push_back(createLine(3, {0, 0}, {10, 0}));
        const Point2D end = placementCursor(tangent, locked(t, {4.0, none}), t.cursor, false);
        t.cursor = end;
        check(build(tangent, locked(t, {4.0, none}), e) && near(e.radius, 4.0, 1e-6),
              "a tangent arc's locked radius reaches the commit");
        PlacementInput s = clicks({{10, 0}}, {20, 10});
        s.targets = t.targets;
        s.cursor = placementCursor(tangent, locked(s, {none, 60.0}), s.cursor, false);
        check(build(tangent, s, e) && near(std::fabs(e.sweepAngle), 60.0, 1e-6),
              "and so does its locked sweep");
        check(tangentArcTarget(nullptr, 0) == PickRefusal::NoEntities
                  && tangentArcTarget(nullptr, 3) == PickRefusal::NothingHit
                  && tangentArcTarget(&t.targets[0], 3) == PickRefusal::None,
              "a tangent arc says why it cannot start");
        Entity circle = createCircle(4, {0, 0}, 1.0);
        check(tangentArcTarget(&circle, 3) == PickRefusal::WrongKind, "a circle is not a host");
        check(near(tangentStart(t.targets[0], {9.5, 2}, 1.0, true), {10, 0})
                  && near(tangentStart(t.targets[0], {9.5, 2}, 1.0, false), {9.5, 0}),
              "its start pulls to an end of the line unless told not to");
    }

    // ---- slot --------------------------------------------------------------------
    {
        const PlacementKind overall{SketchTool::Slot, CreationMode::SlotOverall};
        PlacementInput in = clicks({{0, 0}}, {20, 0});
        in.slotRadius = 3.0;
        check(build(overall, in, e) && near(Point2D(e.points[0]), {3, 0})
                  && near(Point2D(e.points[1]), {17, 0}) && near(e.radius, 3.0),
              "an overall slot is stored by its cap centers");

        const PlacementKind byRadius{SketchTool::Slot, CreationMode::SlotArcRadius};
        PlacementInput arc = clicks({{0, 0}, {20, 0}}, {20, 0.5});
        arc.slotRadius = 3.0;
        const Point2D end = placementCursor(byRadius, arc, arc.cursor, false);
        const Point2D clicked = placementClick(byRadius, arc, arc.cursor);
        const double sepDeg = geometry::angleBetween(Point2D(20, 0), end);
        check(near(dist(end, {0, 0}), 20.0) && near(sepDeg, arcSlotFloorSeparationDegrees(20, 3))
                  && near(clicked, end),
              "an arc slot's end stays a cap apart, in the preview and the click alike");
        arc.cursor = end;
        const PlacementPreview pv = placementPreview(byRadius, arc);
        check(countShapes(pv, PreviewShape::Kind::Note) == 1 && pv.fieldValues.size() == 1,
              "its preview shows the sweep and what to click");
        check(build(byRadius, arc, e) && e.points.size() == 3 && near(Point2D(e.points[0]), {0, 0}),
              "an arc slot is stored as center, start, end");

        const PlacementKind ends{SketchTool::Slot, CreationMode::SlotArcEnds};
        PlacementInput en = clicks({{-10, 0}, {10, 0}}, {3, 20});
        en.slotRadius = 2.0;
        check(build(ends, en, e) && near(Point2D(e.points[0]), {0, 20})
                  && near(Point2D(e.points[1]), {-10, 0}),
              "an ends slot moves its center onto the bisector and reorders");

        Entity slot = createArcSlot(1, {0, 0}, {10, 0}, {0, 10}, 2.0);
        const std::vector<Point2D> outline = arcSlotOutline(slot, 32, 8);
        bool within = !outline.empty() && near(outline.front(), outline.back());
        for (const Point2D& p : outline) {
            const double r = dist(p, {0, 0});
            within = within && r > 8.0 - 1e-9 && r < 12.0 + 1e-9 && p.x > -2.0 - 1e-9
                  && p.y > -2.0 - 1e-9;
        }
        check(within, "an arc slot's outline is closed and stays in its quarter ring");
        double area = 0.0;
        for (std::size_t i = 0; i + 1 < outline.size(); ++i) {
            area += geometry::cross(outline[i], outline[i + 1]) / 2.0;
        }
        // A quarter ring, 8 to 12, and two half-disk caps of radius 2 outside it.
        check(near(std::fabs(area), 24.0 * M_PI, 0.01 * 24.0 * M_PI),
              "its caps round the ends outward");
        const std::vector<Point2D> tess = tessellate(slot, 0.1);
        bool ring = tess.size() > 8;
        for (const Point2D& p : tess) {
            const double r = dist(p, {0, 0});
            ring = ring && r > 8.0 - 1e-9 && r < 12.0 + 1e-9;
        }
        check(ring, "an arc slot tessellates as its ring, not as a straight slot");
        slot.radius = 10.0;
        check(arcSlotOutline(slot).empty(), "a half width reaching the center has no outline");
    }

    // ---- polygon -----------------------------------------------------------------
    {
        const PlacementKind regular{SketchTool::Polygon, CreationMode::PolygonInscribed};
        PlacementInput in = clicks({{0, 0}}, {0, 5});
        in.sides = 5;
        check(build(regular, in, e) && e.points.size() == 6 && near(Point2D(e.points[1]), {0, 5})
                  && e.sides == 5,
              "a regular polygon: center, then vertices from the cursor");
        check(build(regular, locked(in, {2.0}), e) && near(Point2D(e.points[1]), {0, 2}),
              "a locked radius sizes it");

        const PlacementKind freeform{SketchTool::Polygon, CreationMode::PolygonFreeform};
        PlacementInput ff = clicks({{0, 0}, {10, 0}, {10, 10}}, {0.2, 0.1});
        ff.closeDistance = 0.5;
        check(placementClosesLoop(freeform, ff, ff.cursor)
                  && !placementClosesLoop(freeform, ff, {3, 3}),
              "a click by the first vertex closes the loop");
        check(build(freeform, ff, e) && e.points.size() == 3 && e.sides == 3,
              "the clicks are the vertices; the cursor is not one");
    }

    // ---- ellipse ---------------------------------------------------------------
    {
        const PlacementKind axes{SketchTool::Ellipse, CreationMode::EllipseCenterAxes};
        PlacementInput in = locked(clicks({{0, 0}}, {3, 4}), {10.0});
        const Point2D major = placementClick(axes, in, in.cursor);
        check(near(major, {6, 8}), "a click with the radius locked lands at that radius");

        const PlacementKind arc{SketchTool::Ellipse, CreationMode::EllipseArc};
        PlacementInput a = clicks({{0, 0}, {10, 0}, {0, 4}}, {20, 20});
        const Point2D onIt = placementCursor(arc, a, a.cursor, false);
        check(near(onIt.x * onIt.x / 100.0 + onIt.y * onIt.y / 16.0, 1.0, 1e-9),
              "choosing the arc's start, the cursor rides the ellipse");
        a.clicks.push_back(onIt);
        a.cursor = {-20, 20};
        check(build(arc, a, e) && e.type == EntityType::Ellipse && e.ellipseSweep < 360.0,
              "the cursor completes the arc");
        check(countShapes(placementPreview(arc, a), PreviewShape::Kind::Dimension, 0) == 1,
              "the sweep's field is shown");
    }

    // ---- spline ----------------------------------------------------------------
    {
        const PlacementKind fit{SketchTool::Spline, CreationMode::SplineFitPoints};
        check(build(fit, clicks({{0, 0}, {5, 5}, {10, 0}}, {20, 20}), e) && e.points.size() == 3
                  && !e.splineBezier,
              "fit points are the clicks, not the cursor");
        const PlacementKind conic{SketchTool::Spline, CreationMode::SplineConic};
        // The cursor beside the line from the chord's middle to the apex.
        PlacementInput c = clicks({{0, 0}, {10, 0}, {5, 10}}, {8, 2});
        const Point2D shoulder = placementCursor(conic, c, c.cursor, false);
        check(near(shoulder.x, 5.0) && shoulder.y > 0.0 && shoulder.y < 10.0,
              "choosing rho, the cursor rides the line to the apex");
        c.clicks.push_back(shoulder);
        check(build(conic, c, e) && near(e.conicRho, 0.2, 1e-6) && e.splineRational,
              "the conic stores its rho");

        BezierPen pen;
        pen.press({0, 0});
        pen.release();
        pen.press({10, 0});
        pen.drag({12, 3});
        check(pen.anchors()[1].hasOut && near(pen.anchors()[1].outHandle, {12, 3})
                  && near(pen.anchors()[1].inHandle, {8, -3}),
              "dragging from an anchor pulls both its handles");
        pen.release();
        check(pen.withCursor({20, 0}).size() == 3, "the cursor is the next anchor while moving");
        check(pen.entity(true, e) && e.points.size() == 4 && e.weights.size() == 4
                  && e.splineBezier && e.splineRational,
              "two anchors are one rational segment");
        check(countShapes(pen.preview({20, 0}), PreviewShape::Kind::Mark) >= 3,
              "the pen shows its anchors and handles");
        pen.clear();
        check(!pen.entity(false, e), "no anchors, no spline");
    }

    // ---- operations ---------------------------------------------------------------
    {
        std::vector<Entity> sketch = {createLine(1, {0, 0}, {10, 0}),
                                      createLine(2, {10, 0}, {10, 10}),
                                      createCircle(3, {50, 50}, 2)};
        const CornerPick pick = cornerPick(sketch, 1, {9, 0});
        check(pick.refusal == PickRefusal::None && pick.otherId == 2, "a corner is two lines");
        check(cornerPick(sketch, 3, {50, 52}).refusal == PickRefusal::WrongKind
                  && cornerPick(sketch, 9, {0, 0}).refusal == PickRefusal::NothingHit
                  && cornerPick(sketch, 1, {0, 0}).refusal == PickRefusal::NoCorner,
              "and says why when it is not");
        check(offsetTarget(&sketch[2]) == PickRefusal::None && offsetTarget(nullptr)
                  == PickRefusal::NothingHit,
              "circles offset");
        Entity text;
        text.type = EntityType::Text;
        check(offsetTarget(&text) == PickRefusal::WrongKind, "text does not");
    }

    std::printf("\n%s (%d failure(s))\n", failures ? "FAILURES" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
