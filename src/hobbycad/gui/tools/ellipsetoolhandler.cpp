// =====================================================================
//  src/hobbycad/gui/tools/ellipsetoolhandler.cpp — Ellipse tool handler
// =====================================================================
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#include "ellipsetoolhandler.h"
#include "../sketchcanvas.h"

namespace hobbycad {

using EllipseMode = SketchCanvas::EllipseMode;

sketch::PlacementKind EllipseToolHandler::kind(const SketchCanvas& canvas) const
{
    return placementKind(SketchTool::Ellipse, canvas.ellipseMode());
}

bool EllipseToolHandler::applyCreationMode(SketchCanvas& canvas, int modeValue)
{
    switch (modeValue) {
    case 1:  canvas.setEllipseMode(EllipseMode::ThreePoint); break;
    case 2:  canvas.setEllipseMode(EllipseMode::Arc);        break;
    case 3:  canvas.setEllipseMode(EllipseMode::SpanRise);   break;
    case 4:  canvas.setEllipseMode(EllipseMode::Corner);     break;
    case 5:  canvas.setEllipseMode(EllipseMode::Endpoints);  break;
    default: canvas.setEllipseMode(EllipseMode::CenterAxes); break;
    }
    return true;
}

bool EllipseToolHandler::mousePress(SketchCanvas& canvas, QMouseEvent* event,
                                    const QPointF& world)
{
    return stagedPress(canvas, event, world);
}

bool EllipseToolHandler::mouseRelease(SketchCanvas& canvas, QMouseEvent*, const QPointF& world)
{
    // A second click with a little travel used to commit a two-point
    // ellipse through the canvas's shared release path.
    return stagedRelease(canvas, world);
}

}  // namespace hobbycad
