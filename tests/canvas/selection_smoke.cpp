// =====================================================================
//  tests/canvas/selection_smoke.cpp — selecting and dragging on the canvas
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  The canvas now asks the library what a click hit, how the selection
//  changes and where a dragged handle goes. Driven with real events:
//    - a window in a turned view catches what is inside the rectangle
//      drawn on screen (it used to be a sketch-axis rectangle);
//    - dragging right to left in a flipped view is a crossing window (it
//      used to follow the sketch's x);
//    - a click takes a whole group, Ctrl-click drops it;
//    - Ctrl+X holds a dragged handle to the X axis, and Shift pressed mid-
//      drag no longer lets the handle collapse the line onto a snap.
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

static void mouseAt(SketchCanvas& canvas, QEvent::Type type, const QPointF& screen,
                    Qt::KeyboardModifiers mods = Qt::NoModifier, bool held = false)
{
    const Qt::MouseButtons buttons =
        (type == QEvent::MouseButtonPress || held) ? Qt::LeftButton : Qt::NoButton;
    QMouseEvent event(type, screen, canvas.mapToGlobal(screen), Qt::LeftButton, buttons, mods);
    QCoreApplication::sendEvent(&canvas, &event);
}

static QPointF screenOf(const SketchCanvas& canvas, const QPointF& world)
{
    return canvas.toScreenF(world);
}

static void drag(SketchCanvas& canvas, const QPointF& from, const QPointF& to,
                 Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    mouseAt(canvas, QEvent::MouseMove, from, mods);
    mouseAt(canvas, QEvent::MouseButtonPress, from, mods);
    mouseAt(canvas, QEvent::MouseMove, (from + to) / 2.0, mods, true);
    mouseAt(canvas, QEvent::MouseMove, to, mods, true);
    mouseAt(canvas, QEvent::MouseButtonRelease, to, mods);
}

static void key(SketchCanvas& canvas, int k, Qt::KeyboardModifiers mods)
{
    QKeyEvent event(QEvent::KeyPress, k, mods);
    QCoreApplication::sendEvent(&canvas, &event);
}

static SketchEntity line(int id, QPointF a, QPointF b, int group = -1)
{
    SketchEntity e(sketch::createLine(id, a, b));
    e.groupId = group;
    return e;
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    std::printf("canvas selection\n");
    SketchCanvas canvas;
    canvas.resize(800, 600);
    canvas.show();
    canvas.setActiveTool(SketchTool::Select);

    // ---- a window in a turned view ----------------------------------------------
    canvas.setViewRotation(45.0);
    {
        canvas.setSketchContents({line(1, {-20, 0}, {20, 0})}, {}, {});
        const QPointF a = screenOf(canvas, {-20, 0});
        const QPointF b = screenOf(canvas, {20, 0});
        const QPointF topLeft(std::min(a.x(), b.x()) - 10, std::min(a.y(), b.y()) - 10);
        const QPointF bottomRight(std::max(a.x(), b.x()) + 10, std::max(a.y(), b.y()) + 10);
        // A short line in a corner of the window's sketch-axis bounds, well
        // outside the window drawn on screen.
        const auto corners = canvas.view().sketchCorners(topLeft, bottomRight);
        double minX = corners[0].x, maxY = corners[0].y;
        for (const auto& c : corners) {
            minX = std::min(minX, c.x);
            maxY = std::max(maxY, c.y);
        }
        const QPointF far(minX + 1, maxY - 1);
        canvas.setSketchContents({line(1, {-20, 0}, {20, 0}),
                                  line(2, far, far + QPointF(0.5, -0.5))}, {}, {});
        drag(canvas, topLeft, bottomRight);
        ck(canvas.isEntitySelected(1),
           "a window drawn round a line in a turned view selects it");
        ck(!canvas.isEntitySelected(2),
           "and not what lies outside the drawn window");
    }
    canvas.setViewRotation(0.0);

    // ---- a crossing window in a flipped view -------------------------------------
    canvas.clearSelection();
    canvas.setFlipView(true);
    {
        // From right to left on screen across the middle of the line only.
        const QPointF mid = screenOf(canvas, {0, 0});
        drag(canvas, mid + QPointF(30, -20), mid + QPointF(-30, 20));
        ck(canvas.isEntitySelected(1),
           "dragging right to left in a flipped view is a crossing window");
    }
    canvas.setFlipView(false);

    // ---- groups ------------------------------------------------------------------
    {
        SketchGroup g;
        g.id = 5;
        g.entityIds = {1, 2};
        canvas.setSketchContents({line(1, {0, 0}, {20, 0}, 5), line(2, {20, 0}, {20, 20}, 5)},
                                 {}, {g});
        const QPointF onFirst = screenOf(canvas, {6, 0});   // off the midpoint grip
        mouseAt(canvas, QEvent::MouseMove, onFirst);
        mouseAt(canvas, QEvent::MouseButtonPress, onFirst);
        mouseAt(canvas, QEvent::MouseButtonRelease, onFirst);
        ck(canvas.isEntitySelected(1) && canvas.isEntitySelected(2),
           "a click takes the whole group");
        ck(canvas.entities()[0].selected && canvas.entities()[1].selected,
           "and both are drawn selected");
        const QPointF onSecond = screenOf(canvas, {20, 6});
        mouseAt(canvas, QEvent::MouseButtonPress, onSecond, Qt::ControlModifier);
        mouseAt(canvas, QEvent::MouseButtonRelease, onSecond, Qt::ControlModifier);
        ck(canvas.selectionCount() == 0, "Ctrl-click on a member drops the group");
    }

    // ---- a handle drag held to an axis ----------------------------------------------
    {
        canvas.setSketchContents({line(1, {0, 0}, {20, 0})}, {}, {});
        canvas.selectEntity(1);
        const auto endOf = [&canvas]() { return QPointF(canvas.entities()[0].points[1]); };
        const QPointF grab = screenOf(canvas, {20, 0});
        mouseAt(canvas, QEvent::MouseMove, grab, Qt::ControlModifier);
        mouseAt(canvas, QEvent::MouseButtonPress, grab, Qt::ControlModifier);
        const QPointF away = screenOf(canvas, {25, 7});
        mouseAt(canvas, QEvent::MouseMove, (grab + away) / 2.0, Qt::ControlModifier, true);
        mouseAt(canvas, QEvent::MouseMove, away, Qt::ControlModifier, true);
        ck(std::fabs(endOf().y()) > 5.0, "the end follows the cursor");
        key(canvas, Qt::Key_X, Qt::ControlModifier);
        ck(std::fabs(endOf().y()) < 1e-9 && std::fabs(endOf().x() - 25.0) < 0.5,
           "Ctrl+X holds it to the X axis through where it started");

        // Onto the other end, then Shift: the handle keeps its distance.
        key(canvas, Qt::Key_Control, Qt::NoModifier);
        QKeyEvent ctrlUp(QEvent::KeyRelease, Qt::Key_Control, Qt::NoModifier);
        QCoreApplication::sendEvent(&canvas, &ctrlUp);
        const QPointF origin = screenOf(canvas, {0.05, 0.05});
        mouseAt(canvas, QEvent::MouseMove, origin, Qt::NoModifier, true);
        key(canvas, Qt::Key_Shift, Qt::ShiftModifier);
        const double length = geometry::lineLength(Point2D(canvas.entities()[0].points[0]),
                                                   Point2D(endOf()));
        ck(length > 1e-3, "Shift mid-drag does not collapse the line onto a snap");
        mouseAt(canvas, QEvent::MouseButtonRelease, origin);
        ck(canvas.canUndo(), "the drag is one undo step");
        canvas.undo();
        ck(std::fabs(endOf().x() - 20.0) < 1e-9 && std::fabs(endOf().y()) < 1e-9,
           "and undo puts the end back");
    }

    std::printf("\n%s (%d failure(s))\n", fails ? "FAILURES" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
