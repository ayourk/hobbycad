// =====================================================================
//  src/hobbycad/gui/tools/arctoolhandler.cpp — Arc tool handler
// =====================================================================
//
//  Part of HobbyCAD.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include "arctoolhandler.h"

#include "../sketchcanvas.h"

#include <QKeyEvent>
#include <QMessageBox>
#include <QMouseEvent>

namespace hobbycad {

using ArcMode = SketchCanvas::ArcMode;

sketch::PlacementKind ArcToolHandler::kind(const SketchCanvas& canvas) const
{
    return placementKind(SketchTool::Arc, canvas.arcMode());
}

bool ArcToolHandler::mousePress(SketchCanvas& canvas, QMouseEvent* event, const QPointF& world)
{
    if (canvas.arcMode() != ArcMode::Tangent) return stagedPress(canvas, event, world);

    if (canvas.hasTangentTargets()) {
        // The second click: the end, where the arc and its locks put it.
        canvas.trackEntity(canvas.snapToGeometry(world));
        canvas.commitEntity();
        return true;
    }

    // The first click picks the line (or rectangle edge) to be tangent to.
    const int hitId = canvas.pick(world);
    const SketchEntity* target = hitId >= 0 ? canvas.findEntity(hitId) : nullptr;
    switch (sketch::tangentArcTarget(target, static_cast<std::size_t>(canvas.entities().size()))) {
    case sketch::PickRefusal::NoEntities:
        QMessageBox::information(&canvas, tr("Tangent Arc"),
            tr("There are no entities to create a tangent arc from.\n"
               "Please draw a line first."));
        return true;
    case sketch::PickRefusal::NothingHit:
        QMessageBox::information(&canvas, tr("Tangent Arc"),
            tr("Please click on a line or rectangle edge to create a tangent arc from."));
        return true;
    case sketch::PickRefusal::WrongKind:
    case sketch::PickRefusal::NoCorner:
        QMessageBox::information(&canvas, tr("Tangent Arc"),
            tr("Tangent arcs can currently only be created from lines or rectangles.\n"
               "Please click on a line or rectangle edge."));
        return true;
    case sketch::PickRefusal::None:
        break;
    }
    canvas.addTangentTarget(hitId);
    // The arc starts where the click meets the edge, pulled to an end or the
    // middle of it unless Alt is held.
    const bool snapEnds = !(event->modifiers() & Qt::AltModifier);
    canvas.beginPlacement(QPointF(sketch::tangentStart(
        *target, canvas.snapToGeometry(world), 10.0 / canvas.zoomFactor(), snapEnds)));
    canvas.update();
    return true;
}

bool ArcToolHandler::mouseRelease(SketchCanvas& canvas, QMouseEvent*, const QPointF& world)
{
    if (!canvas.isDrawing()) return false;
    // The tangent arc is click-click: a drag-release in between must not
    // finish it.
    if (canvas.arcMode() == ArcMode::Tangent) return true;
    return stagedRelease(canvas, world);
}

bool ArcToolHandler::normalize(SketchCanvas& canvas, SketchEntity& entity, bool& valid)
{
    if (!PlacementToolHandler::normalize(canvas, entity, valid)) return false;
    if (canvas.arcMode() == ArcMode::Tangent) {
        if (valid && canvas.hasTangentTargets()) {
            entity.tangentEntityId = canvas.tangentTargets()[0];
        }
        canvas.clearTangentTargets();
    }
    return true;
}

bool ArcToolHandler::isMultiClick(const SketchCanvas& canvas) const
{
    return canvas.arcMode() != ArcMode::Tangent;
}

bool ArcToolHandler::beginsOnFirstClick(const SketchCanvas& canvas) const
{
    return canvas.arcMode() != ArcMode::Tangent;
}

bool ArcToolHandler::keyPress(SketchCanvas& canvas, QKeyEvent* event)
{
    if (PlacementToolHandler::keyPress(canvas, event)) return true;
    if (canvas.isDrawing() && event->key() == Qt::Key_Control
        && canvas.arcMode() == ArcMode::StartEndRadius && canvas.previewPointCount() >= 2) {
        // The half-circle snap is read while drawing; this redraws the moment
        // Ctrl goes down.
        canvas.update();
        return true;
    }
    return false;
}

bool ArcToolHandler::applyCreationMode(SketchCanvas& canvas, int modeValue)
{
    // Arc modes: 0=ThreePoint, 1=CenterStartEnd, 2=StartEndRadius, 3=Tangent.
    switch (modeValue) {
    case 1:  canvas.setArcMode(ArcMode::CenterStartEnd); break;
    case 2:  canvas.setArcMode(ArcMode::StartEndRadius); break;
    case 3:  canvas.setArcMode(ArcMode::Tangent); break;
    default: canvas.setArcMode(ArcMode::ThreePoint); break;
    }
    return true;
}

}  // namespace hobbycad
