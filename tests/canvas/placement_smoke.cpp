// =====================================================================
//  tests/canvas/placement_smoke.cpp — the drawing tools on the canvas
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  The tools' handlers now only turn events into calls on the library's
//  placement rules. This drives the canvas with real mouse and key events
//  and checks what was committed and what the preview tracked:
//    - the 3-point arc ends at its second click (it used to end at the
//      third);
//    - a locked value moves the point being placed while the preview is
//      drawn (for the 3-point rectangle it moved only at the click: the
//      handler assigned the locked point to a copy of the cursor);
//    - the ellipse's click honors its locked radius;
//    - a freeform polygon closes on its first vertex.
#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>

#include "sketchcanvas.h"

#include <hobbycad/geometry/utils.h>

#include <cmath>
#include <cstdio>

using namespace hobbycad;
static int fails = 0;
static void ck(bool ok, const char* what)
{
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++fails;
}

static bool near(double a, double b, double eps = 1e-6) { return std::fabs(a - b) < eps; }

static void mouse(SketchCanvas& canvas, QEvent::Type type, const QPointF& world)
{
    const QPointF at(canvas.toScreen(world));
    const Qt::MouseButtons held = type == QEvent::MouseButtonPress ? Qt::LeftButton
                                                                   : Qt::NoButton;
    QMouseEvent event(type, at, canvas.mapToGlobal(at), Qt::LeftButton, held, Qt::NoModifier);
    QCoreApplication::sendEvent(&canvas, &event);
}

static void move(SketchCanvas& canvas, const QPointF& world)
{
    mouse(canvas, QEvent::MouseMove, world);
}

static void click(SketchCanvas& canvas, const QPointF& world)
{
    move(canvas, world);
    mouse(canvas, QEvent::MouseButtonPress, world);
    mouse(canvas, QEvent::MouseButtonRelease, world);
}

static void type(SketchCanvas& canvas, const QString& text)
{
    for (const QChar c : text) {
        QKeyEvent key(QEvent::KeyPress, 0, Qt::NoModifier, QString(c));
        QCoreApplication::sendEvent(&canvas, &key);
    }
    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QCoreApplication::sendEvent(&canvas, &enter);
}

static const SketchEntity* last(const SketchCanvas& canvas)
{
    return canvas.entities().isEmpty() ? nullptr : &canvas.entities().last();
}

static void fresh(SketchCanvas& canvas, SketchTool tool, CreationMode mode)
{
    canvas.setEntities({});
    canvas.setActiveTool(tool);
    canvas.setCreationMode(mode);
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    std::printf("canvas placement\n");
    SketchCanvas canvas;
    canvas.resize(800, 600);
    canvas.show();
    canvas.setSnapToGrid(true);
    canvas.setGridSpacing(1.0);

    // ---- 3-point arc -------------------------------------------------------
    fresh(canvas, SketchTool::Arc, CreationMode::ArcThreePoint);
    click(canvas, {10, 0});
    click(canvas, {-10, 0});
    ck(canvas.isDrawing() && canvas.currentToolHint().contains("point on the arc"),
       "the arc asks for a point on it after its two ends");
    move(canvas, {0, 10});
    canvas.grab();   // the preview paints
    click(canvas, {0, 10});
    const SketchEntity* arc = last(canvas);
    ck(arc && arc->type == SketchEntityType::Arc && !canvas.isDrawing(),
       "three clicks commit an arc");
    ck(arc && near(arc->points[1].x, 10) && near(arc->points[2].x, -10)
           && near(std::fabs(arc->sweepAngle), 180.0),
       "it runs from the first click to the second, through the third");

    // ---- 3-point rectangle, width locked ------------------------------------
    fresh(canvas, SketchTool::Rectangle, CreationMode::RectThreePoint);
    click(canvas, {0, 0});
    click(canvas, {20, 7});
    move(canvas, {3, 17});
    ck(canvas.dimFieldCount() == 1, "the second edge asks for a width");
    type(canvas, QStringLiteral("4"));
    move(canvas, {4, 18});
    canvas.grab();
    const auto& pending = canvas.pendingEntity().points;
    const auto across = [](const Point2D& p) {
        const Point2D dir = geometry::normalize(Point2D(20, 7));
        return geometry::cross(dir, p);
    };
    ck(pending.size() == 3 && near(across(Point2D(pending[2])), 4.0, 1e-6),
       "the preview follows the locked width");
    click(canvas, {9, 30});
    const SketchEntity* rect = nullptr;
    for (const SketchEntity& e : canvas.entities()) {
        if (e.type == SketchEntityType::Line) rect = &e;
    }
    ck(rect != nullptr && !canvas.isDrawing(), "the rectangle is committed as lines");

    // ---- ellipse, radius locked --------------------------------------------
    fresh(canvas, SketchTool::Ellipse, CreationMode::EllipseCenterAxes);
    click(canvas, {0, 0});
    move(canvas, {30, 40});
    type(canvas, QStringLiteral("10"));
    click(canvas, {30, 40});
    click(canvas, {-20, 20});
    const SketchEntity* ellipse = nullptr;
    for (const SketchEntity& e : canvas.entities()) {
        if (e.type == SketchEntityType::Ellipse) ellipse = &e;
    }
    ck(ellipse && near(ellipse->majorRadius, 10.0, 1e-6),
       "an ellipse clicked with its radius locked has that radius");

    // ---- line --------------------------------------------------------------
    fresh(canvas, SketchTool::Line, CreationMode::LineTwoPoint);
    click(canvas, {0, 0});
    move(canvas, {6, 8});
    canvas.grab();
    ck(canvas.dimFieldCount() == 2 && near(canvas.dimInputState().field(0).liveValue, 10.0)
           && near(canvas.dimInputState().field(1).liveValue,
                   std::atan2(8.0, 6.0) * 180.0 / M_PI),
       "the painted preview hands its length and angle to the fields");
    click(canvas, {13, 21});
    const SketchEntity* line = nullptr;
    for (const SketchEntity& e : canvas.entities()) {
        if (e.type == SketchEntityType::Line) line = &e;
    }
    ck(line && near(line->points[1].x, 13) && near(line->points[1].y, 21),
       "two clicks make a line");

    // ---- freeform polygon ----------------------------------------------------
    fresh(canvas, SketchTool::Polygon, CreationMode::PolygonFreeform);
    click(canvas, {0, 0});
    click(canvas, {30, 0});
    click(canvas, {30, 30});
    ck(canvas.isDrawing() && canvas.currentToolHint().contains("close"),
       "a freeform polygon offers to close after three vertices");
    click(canvas, {0, 0});
    ck(!canvas.isDrawing() && canvas.entities().size() >= 3,
       "clicking the first vertex closes it");

    std::printf("\n%s (%d failure(s))\n", fails ? "FAILURES" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
