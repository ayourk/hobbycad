// =====================================================================
//  src/hobbycad/gui/tools/splinetoolhandler.cpp
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#include "splinetoolhandler.h"
#include "../sketchcanvas.h"

#include <QKeyEvent>
#include <QMouseEvent>

namespace hobbycad {

using SplineMode = SketchCanvas::SplineMode;

sketch::PlacementKind SplineToolHandler::kind(const SketchCanvas& canvas) const
{
    return placementKind(SketchTool::Spline, canvas.splineMode());
}

bool SplineToolHandler::isPen(const SketchCanvas& canvas) const
{
    return canvas.splineMode() == SplineMode::ControlPoints
        || canvas.splineMode() == SplineMode::Rational;
}

sketch::PlacementStage SplineToolHandler::stage(const SketchCanvas& canvas) const
{
    sketch::PlacementStage s = PlacementToolHandler::stage(canvas);
    s.anchors = !m_pen.empty();
    if (isPen(canvas)) s.canFinish = m_pen.size() >= 2;
    return s;
}

sketch::PlacementPreview SplineToolHandler::preview(const SketchCanvas& canvas) const
{
    if (isPen(canvas)) return m_pen.preview(canvas.currentMouseWorld());
    return PlacementToolHandler::preview(canvas);
}

bool SplineToolHandler::finishesOnRightClick(const SketchCanvas& canvas) const
{
    return canvas.splineMode() != SplineMode::Conic;
}

bool SplineToolHandler::applyCreationMode(SketchCanvas& canvas, int modeValue)
{
    // The toolbar's order: 0 Bezier pen, 1 Catmull-Rom, 2 Rational (weighted)
    // Bezier, 3 Conic Arc (Rho), a rational Bezier from four staged clicks.
    switch (modeValue) {
    case 1:  canvas.setSplineMode(SplineMode::FitPoints);     break;
    case 2:  canvas.setSplineMode(SplineMode::Rational);      break;
    case 3:  canvas.setSplineMode(SplineMode::Conic);         break;
    default: canvas.setSplineMode(SplineMode::ControlPoints); break;
    }
    return true;
}

bool SplineToolHandler::keyPress(SketchCanvas& canvas, QKeyEvent* event)
{
    if (!canvas.isDrawing()) return false;
    // Enter, Escape and right-click all FINISH, keeping the placed points,
    // like a line chain (Aaron: consistency). The commit keeps a valid
    // spline and drops an incomplete one.
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter
        || event->key() == Qt::Key_Escape) {
        canvas.commitEntity();
        return true;
    }
    return false;
}

void SplineToolHandler::cancel(SketchCanvas&)
{
    m_pen.clear();
}

bool SplineToolHandler::beginEntity(SketchCanvas& canvas, SketchEntity& entity)
{
    PlacementToolHandler::beginEntity(canvas, entity);
    entity.splineBezier = canvas.splineMode() != SplineMode::FitPoints;
    entity.splineRational = canvas.splineMode() == SplineMode::Rational
                         || canvas.splineMode() == SplineMode::Conic;
    if (entity.splineBezier) m_pen.clear();
    return true;
}

bool SplineToolHandler::normalize(SketchCanvas& canvas, SketchEntity& entity, bool& valid)
{
    if (entity.type != SketchEntityType::Spline) return false;
    if (!isPen(canvas)) return PlacementToolHandler::normalize(canvas, entity, valid);
    // The pen's anchors are the spline, as a cubic control polygon.
    valid = m_pen.entity(canvas.splineMode() == SplineMode::Rational,
                         static_cast<sketch::Entity&>(entity));
    return true;
}

bool SplineToolHandler::mousePress(SketchCanvas& canvas, QMouseEvent* event,
                                   const QPointF& world)
{
    if (event && event->button() != Qt::LeftButton) return false;
    if (canvas.splineMode() == SplineMode::Conic) {
        // Four clicks staged on PRESS, the ellipse tool's way; this tool also
        // starts the placement itself.
        if (canvas.isDrawing()) return stagedPress(canvas, event, world);
        if (event) canvas.beginDragDetection(event->pos());
        canvas.beginPlacement(canvas.snapToGeometry(world));
        canvas.update();
        return true;
    }
    if (!isPen(canvas)) return false;   // fit points use the release path

    const QPointF at = canvas.snapToGeometry(world);
    if (!canvas.isDrawing()) canvas.beginPlacement(at);   // beginEntity clears the pen
    m_pen.press(at);
    canvas.update();
    return true;
}

bool SplineToolHandler::mouseMove(SketchCanvas& canvas, QMouseEvent*, const QPointF& world)
{
    if (!isPen(canvas) || !m_pen.dragging() || m_pen.empty()) return false;
    m_pen.drag(world);
    canvas.update();
    return true;
}

bool SplineToolHandler::mouseRelease(SketchCanvas& canvas, QMouseEvent*, const QPointF& world)
{
    if (!canvas.isDrawing()) return false;
    if (canvas.splineMode() == SplineMode::Conic) return stagedRelease(canvas, world);
    if (isPen(canvas)) {
        m_pen.release();   // the anchor was placed on press; a drag set its handles
    } else {
        canvas.appendPlacementPoint(canvas.snapToGeometry(world));   // a fit point
    }
    canvas.update();
    return true;
}

}  // namespace hobbycad
