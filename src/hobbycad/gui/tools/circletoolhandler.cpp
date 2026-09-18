// =====================================================================
//  src/hobbycad/gui/tools/circletoolhandler.cpp — Circle tool handler
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#include "circletoolhandler.h"
#include "../sketchcanvas.h"

namespace hobbycad {

using CircleMode = SketchCanvas::CircleMode;

namespace {
bool isTangent(CircleMode m)
{
    return m == CircleMode::TwoTangent || m == CircleMode::ThreeTangent;
}
}  // namespace

sketch::PlacementKind CircleToolHandler::kind(const SketchCanvas& canvas) const
{
    return placementKind(SketchTool::Circle, canvas.circleMode());
}

bool CircleToolHandler::mousePress(SketchCanvas& canvas, QMouseEvent* event, const QPointF& world)
{
    const CircleMode mode = canvas.circleMode();
    if (isTangent(mode)) {
        // Clicks pick the curves to be tangent to. Once there are enough,
        // the next click places the circle itself.
        const int hitId = canvas.pick(world);
        if (hitId >= 0 && !canvas.tangentTargets().contains(hitId)) {
            canvas.addTangentTarget(hitId);
            if (canvas.tangentTargetCount() >= sketch::placementTargets(kind(canvas))) {
                canvas.beginPlacement(canvas.snapToGeometry(world));
            }
            canvas.update();
        }
        return true;
    }
    if (mode != CircleMode::ThreePoint) return false;   // the canvas's two-click path
    return stagedPress(canvas, event, world);
}

bool CircleToolHandler::mouseRelease(SketchCanvas& canvas, QMouseEvent*, const QPointF& world)
{
    if (canvas.circleMode() != CircleMode::ThreePoint) return false;
    return stagedRelease(canvas, world);
}

bool CircleToolHandler::normalize(SketchCanvas& canvas, SketchEntity& entity, bool& valid)
{
    if (!PlacementToolHandler::normalize(canvas, entity, valid)) return false;
    // The picked curves belong to this circle only.
    if (isTangent(canvas.circleMode())) canvas.clearTangentTargets();
    return true;
}

bool CircleToolHandler::isMultiClick(const SketchCanvas& canvas) const
{
    return canvas.circleMode() == CircleMode::ThreePoint;
}

bool CircleToolHandler::beginsOnFirstClick(const SketchCanvas& canvas) const
{
    return canvas.circleMode() == CircleMode::ThreePoint;
}

bool CircleToolHandler::applyCreationMode(SketchCanvas& canvas, int modeValue)
{
    // Circle modes: 0=CenterRadius, 1=TwoPoint (diameter), 2=ThreePoint,
    // 3=TwoTangent, 4=ThreeTangent.
    switch (modeValue) {
    case 1:  canvas.setCircleMode(CircleMode::TwoPoint);     break;
    case 2:  canvas.setCircleMode(CircleMode::ThreePoint);   break;
    case 3:  canvas.setCircleMode(CircleMode::TwoTangent);   break;
    case 4:  canvas.setCircleMode(CircleMode::ThreeTangent); break;
    default: canvas.setCircleMode(CircleMode::CenterRadius); break;
    }
    return true;
}

}  // namespace hobbycad
