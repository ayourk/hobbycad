// =====================================================================
//  tests/canvas/undo_smoke.cpp — the canvas's constraints and undo history
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  Three things the canvas got wrong before its edits went through
//  sketch/edit_session.h:
//    - a constraint from the Constraints menu or the Dimension tool could
//      not be undone at all;
//    - the Constraints menu let a second Horizontal onto a line, and a
//      Horizontal onto a circle, which the command line refuses (libslvs
//      aborts on some wrong pairings);
//    - the one canvas is reused for every sketch, and its history went
//      with it, so Undo in a newly opened sketch replayed the previous
//      sketch's steps onto entities that happened to share their ids.
#include <QApplication>

#include "sketchcanvas.h"

#include <hobbycad/sketch/entity.h>

#include <cmath>
#include <cstdio>

using namespace hobbycad;
static int fails = 0;
static void ck(bool ok, const char* what)
{
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++fails;
}

static SketchConstraint constraint(int id, ConstraintType type, std::vector<int> ids,
                                   std::vector<int> points = {}, double value = 0.0)
{
    SketchConstraint c;
    c.id = id;
    c.type = type;
    c.entityIds = std::move(ids);
    c.pointIndices = std::move(points);
    c.value = value;
    c.isDriving = true;
    c.enabled = true;
    return c;
}

static const SketchEntity* entity(const SketchCanvas& canvas, int id)
{
    for (const SketchEntity& e : canvas.entities()) {
        if (e.id == id) return &e;
    }
    return nullptr;
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    std::printf("canvas constraints and undo\n");
    SketchCanvas canvas;

    // ---- a constraint and what its solve moved come off together -----------
    // The line is already marked constrained, so no other recorded change
    // happens to carry its old position back.
    SketchEntity sloped(sketch::createLine(1, Point2D(0, 0), Point2D(10, 3)));
    sloped.constrained = true;
    canvas.setSketchContents(
        {sloped, SketchEntity(sketch::createCircle(2, Point2D(20, 20), 4.0))}, {}, {});
    ck(!canvas.canUndo(), "a sketch opens with nothing to undo");

    canvas.selectEntity(1);
    ck(canvas.applyTypedConstraint(ConstraintType::Horizontal) && canvas.constraints().size() == 1,
       "Horizontal applies to a line");
    const SketchEntity* line = entity(canvas, 1);
    ck(line && std::fabs(line->points[1].y - line->points[0].y) < 1e-6,
       "and the solve levels it");
    ck(canvas.canUndo(), "the constraint can be undone");

    canvas.undo();
    line = entity(canvas, 1);
    ck(canvas.constraints().isEmpty() && line
           && std::fabs((line->points[1].y - line->points[0].y) - 3.0) < 1e-6,
       "undo takes the constraint off and puts the line back where it was");
    canvas.redo();
    ck(canvas.constraints().size() == 1, "redo puts the constraint back");

    // ---- the Constraints menu checks what the command line checks ------------
    canvas.clearSelection();
    canvas.selectEntity(2);
    canvas.applyTypedConstraint(ConstraintType::Horizontal);
    ck(canvas.constraints().size() == 1, "Horizontal on a circle is refused");

    ck(canvas.canUndo(), "the sketch has history");
    canvas.clear();
    ck(!canvas.canUndo() && !canvas.canRedo(), "clearing the canvas clears its history");

    canvas.setSketchContents(
        {SketchEntity(sketch::createLine(1, Point2D(0, 0), Point2D(10, 3)))}, {}, {});
    canvas.selectEntity(1);
    canvas.applyTypedConstraint(ConstraintType::Horizontal);
    ck(canvas.canUndo(), "a new edit has history again");

    // Redundancy is reported only on a determined sketch.
    canvas.setSketchContents(
        {SketchEntity(sketch::createLine(1, Point2D(0, 0), Point2D(10, 0))),
         SketchEntity(sketch::createPoint(2, Point2D(0, 0)))},
        {constraint(1, ConstraintType::FixedPoint, {2}),
         constraint(2, ConstraintType::Coincident, {1, 2}, {0, 0}),
         constraint(3, ConstraintType::Distance, {1, 2}, {1, 0}, 10.0),
         constraint(4, ConstraintType::Horizontal, {1})},
        {});
    ck(!canvas.canUndo(), "opening another sketch forgets the last one's history");

    canvas.selectEntity(1);
    canvas.applyTypedConstraint(ConstraintType::Horizontal);
    ck(canvas.constraints().size() == 4 && !canvas.canUndo(),
       "a second Horizontal on a determined line is refused and records nothing");

    std::printf("\n%s (%d failure(s))\n", fails ? "FAILURES" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
