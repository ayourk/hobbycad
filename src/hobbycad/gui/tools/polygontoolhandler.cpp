// =====================================================================
//  src/hobbycad/gui/tools/polygontoolhandler.cpp
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#include "polygontoolhandler.h"
#include "../sketchcanvas.h"

#include <QWheelEvent>
#include <QtGlobal>

namespace hobbycad {

using PolygonMode = SketchCanvas::PolygonMode;

sketch::PlacementKind PolygonToolHandler::kind(const SketchCanvas& canvas) const
{
    return placementKind(SketchTool::Polygon, canvas.polygonMode());
}

bool PolygonToolHandler::wheel(SketchCanvas& canvas, QWheelEvent* event)
{
    if (!canvas.isDrawing() || canvas.polygonMode() == PolygonMode::Freeform) {
        return false;   // fall through to zoom
    }
    const int delta = event->angleDelta().y() > 0 ? 1 : -1;
    SketchEntity& e = canvas.pendingEntityRef();
    e.sides = qBound(3, e.sides + delta, 64);
    return true;
}

bool PolygonToolHandler::beginEntity(SketchCanvas& canvas, SketchEntity& entity)
{
    PlacementToolHandler::beginEntity(canvas, entity);
    // A regular polygon starts as a hexagon; the wheel adjusts it. A freeform
    // one counts its vertices at the commit.
    if (canvas.polygonMode() != PolygonMode::Freeform) entity.sides = 6;
    return true;
}

bool PolygonToolHandler::mouseRelease(SketchCanvas& canvas, QMouseEvent*, const QPointF& world)
{
    if (!canvas.isDrawing() || canvas.polygonMode() != PolygonMode::Freeform) return false;

    const QPointF snapped = canvas.snapToGeometry(world);
    // Releasing near the first vertex closes the polygon, and that click is
    // not another vertex.
    if (sketch::placementClosesLoop(kind(canvas), input(canvas, snapped), snapped)) {
        canvas.commitEntity();
        return true;
    }
    canvas.appendPlacementPoint(snapped);
    canvas.update();
    // Not finished: the user keeps clicking, or ends with a right-click or
    // Enter.
    return true;
}

bool PolygonToolHandler::finishesOnRightClick(const SketchCanvas& canvas) const
{
    return canvas.polygonMode() == PolygonMode::Freeform;
}

bool PolygonToolHandler::isMultiClick(const SketchCanvas& canvas) const
{
    return canvas.polygonMode() == PolygonMode::Freeform;
}

bool PolygonToolHandler::applyCreationMode(SketchCanvas& canvas, int modeValue)
{
    // Polygon modes: 0=Inscribed, 1=Circumscribed, 2=Freeform.
    switch (modeValue) {
    case 1:  canvas.setPolygonMode(PolygonMode::Circumscribed); break;
    case 2:  canvas.setPolygonMode(PolygonMode::Freeform); break;
    default: canvas.setPolygonMode(PolygonMode::Inscribed); break;
    }
    return true;
}

}  // namespace hobbycad
