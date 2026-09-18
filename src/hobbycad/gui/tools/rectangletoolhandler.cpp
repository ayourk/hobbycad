// =====================================================================
//  src/hobbycad/gui/tools/rectangletoolhandler.cpp
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#include "rectangletoolhandler.h"
#include "../sketchcanvas.h"

namespace hobbycad {

using RectMode = SketchCanvas::RectMode;

namespace {
/// The two modes staged over three points; the others place with two
/// clicks through the canvas.
bool isStaged(RectMode m)
{
    return m == RectMode::ThreePoint || m == RectMode::Parallelogram;
}
}  // namespace

sketch::PlacementKind RectangleToolHandler::kind(const SketchCanvas& canvas) const
{
    return placementKind(SketchTool::Rectangle, canvas.rectMode());
}

bool RectangleToolHandler::mousePress(SketchCanvas& canvas, QMouseEvent* event,
                                      const QPointF& world)
{
    if (!isStaged(canvas.rectMode())) return false;
    return stagedPress(canvas, event, world);
}

bool RectangleToolHandler::mouseRelease(SketchCanvas& canvas, QMouseEvent*, const QPointF& world)
{
    if (!isStaged(canvas.rectMode())) return false;
    return stagedRelease(canvas, world);
}

bool RectangleToolHandler::applyCreationMode(SketchCanvas& canvas, int modeValue)
{
    // Rectangle modes: 0=Corner, 1=Center, 2=ThreePoint, 3=Parallelogram.
    switch (modeValue) {
    case 1:  canvas.setRectMode(RectMode::Center); break;
    case 2:  canvas.setRectMode(RectMode::ThreePoint); break;
    case 3:  canvas.setRectMode(RectMode::Parallelogram); break;
    default: canvas.setRectMode(RectMode::Corner); break;
    }
    return true;
}

}  // namespace hobbycad
