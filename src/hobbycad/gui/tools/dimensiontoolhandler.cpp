// =====================================================================
//  src/hobbycad/gui/tools/dimensiontoolhandler.cpp
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#include "dimensiontoolhandler.h"
#include "../sketchcanvas.h"

#include <QCoreApplication>
#include <QMouseEvent>

namespace hobbycad {

QString DimensionToolHandler::hint(const SketchCanvas& canvas) const
{
    const char* s = (canvas.constraintTargetCount() == 0)
        ? "Dimension: click an entity to dimension it"
        : canvas.dimensionReadyToPlace()
            ? "Dimension: click to place the label (right-click for options)"
            : "Dimension: click a second entity";
    return QCoreApplication::translate("hobbycad::SketchCanvas", s);
}

bool DimensionToolHandler::mousePress(SketchCanvas& canvas, QMouseEvent*,
                                      const QPointF& world)
{
    if (!canvas.isCreatingConstraint()) {
        return false;
    }

    // Once the target set is complete (a self-dimensioning line/circle/arc, or
    // two accumulated entities), the next click places the label. Arming first
    // and placing second, rather than placing on the very first click,
    // leaves room for the right-click radius/diameter and driven options, the
    // way Fusion's Dimension tool does.
    if (canvas.dimensionReadyToPlace()) {
        canvas.placeDimensionLabel(world);
        return true;
    }

    const int hitId = canvas.pick(world);
    if (hitId < 0) {
        return true;   // clicking empty space is a no-op, not a fall-through
    }

    // A line or a circle/arc dimensions itself: arm it and wait for the
    // placement click.
    if (canvas.constraintTargetCount() == 0
        && canvas.beginSingleEntityDimension(hitId)) {
        canvas.update();
        return true;
    }

    // Otherwise accumulate targets for the two-entity sequence.
    canvas.addConstraintTarget(hitId, world);
    canvas.update();
    return true;
}

}  // namespace hobbycad
