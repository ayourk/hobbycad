// =====================================================================
//  src/hobbycad/gui/tools/slottoolhandler.cpp
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#include "slottoolhandler.h"
#include "../sketchcanvas.h"

#include <QLineF>
#include <QWheelEvent>
#include <QtGlobal>

#include <limits>

namespace hobbycad {

using SlotMode = SketchCanvas::SlotMode;

namespace {
/// The two modes staged over three points.
bool isArcSlot(SlotMode m)
{
    return m == SlotMode::ArcRadius || m == SlotMode::ArcEnds;
}
}  // namespace

sketch::PlacementKind SlotToolHandler::kind(const SketchCanvas& canvas) const
{
    return placementKind(SketchTool::Slot, canvas.slotMode());
}

bool SlotToolHandler::mousePress(SketchCanvas& canvas, QMouseEvent* event, const QPointF& world)
{
    if (!isArcSlot(canvas.slotMode())) return false;
    return stagedPress(canvas, event, world);
}

bool SlotToolHandler::mouseRelease(SketchCanvas& canvas, QMouseEvent*, const QPointF& world)
{
    if (!isArcSlot(canvas.slotMode())) return false;
    return stagedRelease(canvas, world);
}

bool SlotToolHandler::beginEntity(SketchCanvas& canvas, SketchEntity& entity)
{
    PlacementToolHandler::beginEntity(canvas, entity);
    entity.radius = 5.0;   // default half-width; the wheel adjusts it
    return true;
}

bool SlotToolHandler::wheel(SketchCanvas& canvas, QWheelEvent* event)
{
    if (!canvas.isDrawing()) return false;   // fall through to zoom

    const int delta = event->angleDelta().y() > 0 ? 1 : -1;
    SketchEntity& e = canvas.pendingEntityRef();

    // A 1 mm floor, and for an arc slot a ceiling: its half width may not
    // pass its arc radius, or the inner edge crosses the arc center and the
    // outline turns inside out. Aaron's rule, 2026-08-28: "slot width/2 is
    // less than or equal to the arc radius."
    double ceiling = std::numeric_limits<double>::max();
    const auto& pts = e.points;
    switch (canvas.slotMode()) {
    case SlotMode::ArcRadius:
        // points[0] is the arc center, points[1] the first end.
        if (pts.size() >= 2) ceiling = QLineF(pts[0], pts[1]).length();
        break;
    case SlotMode::ArcEnds:
        // Placement order here is start, end, center; the reorder into
        // storage form happens at the commit.
        if (pts.size() >= 3) ceiling = QLineF(pts[2], pts[0]).length();
        break;
    default:
        break;   // a straight slot has no such limit
    }

    e.radius = qBound(1.0, e.radius + delta, qMax(1.0, ceiling));
    return true;
}

QString SlotToolHandler::cursorHint(const SketchCanvas& canvas) const
{
    // The wheel sets the width at every stage of every slot mode, so it is
    // its own line by the cursor (Aaron).
    const QString base = PlacementToolHandler::cursorHint(canvas);
    const QString scroll = tr("(scroll: thickness)");
    return base.isEmpty() ? scroll : base + QLatin1Char('\n') + scroll;
}

bool SlotToolHandler::applyCreationMode(SketchCanvas& canvas, int modeValue)
{
    // Slot modes: 0=CenterToCenter, 1=Overall, 2=ArcRadius, 3=ArcEnds.
    switch (modeValue) {
    case 1:  canvas.setSlotMode(SlotMode::Overall); break;
    case 2:  canvas.setSlotMode(SlotMode::ArcRadius); break;
    case 3:  canvas.setSlotMode(SlotMode::ArcEnds); break;
    default: canvas.setSlotMode(SlotMode::CenterToCenter); break;
    }
    return true;
}

}  // namespace hobbycad
