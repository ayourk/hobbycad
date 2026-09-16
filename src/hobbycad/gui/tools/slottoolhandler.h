// =====================================================================
//  src/hobbycad/gui/tools/slottoolhandler.h — Slot tool handler
// =====================================================================
//
//  Four modes. CenterToCenter/Overall are two-point; ArcRadius and ArcEnds
//  are staged over three points and are the flippable arc-slot cases (Shift
//  toggles the >180-degree "long way round").
//
//  ⚠ The SCROLL WHEEL adjusts slot width (radius) during placement, 1 mm per
//  step with a 1 mm floor, instead of zooming. Documented nowhere else.
//
//  ⚠ ArcEnds stores its points REORDERED: clicked as [start, end, center],
//  stored as [center, start, end], with the center forced onto the
//  perpendicular bisector. See finishEntity().
//
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#ifndef HOBBYCAD_SLOTTOOLHANDLER_H
#define HOBBYCAD_SLOTTOOLHANDLER_H

#include "sketchtoolhandler.h"
#include <QCoreApplication>

namespace hobbycad {

class SlotToolHandler : public SketchToolHandler {
    Q_DECLARE_TR_FUNCTIONS(hobbycad::SketchCanvas)
public:
    SketchTool tool() const override { return SketchTool::Slot; }
    bool applyCreationMode(SketchCanvas& canvas, int modeValue) override;
    bool initDimFields(SketchCanvas& canvas) override;
    bool wheel(SketchCanvas& canvas, QWheelEvent* event) override;
    QString hint(const SketchCanvas& canvas) const override;
    QString cursorHint(const SketchCanvas& canvas) const override;
    bool beginEntity(SketchCanvas& canvas, SketchEntity& entity) override;
    bool normalize(SketchCanvas& canvas, SketchEntity& entity, bool& valid) override;
    bool updateEntity(SketchCanvas& canvas, const QPointF& pos) override;
    bool drawPreview(SketchCanvas& canvas, QPainter& painter) override;

    bool mousePress(SketchCanvas& canvas, QMouseEvent* event, const QPointF& world) override;
    bool mouseRelease(SketchCanvas& canvas, QMouseEvent* event, const QPointF& world) override;

    /// Staged constraints for the two arc-slot modes. Called from BOTH press
    /// and release; both call sites are required (click vs drag-through).
    static void constrainArcSlot(const SketchCanvas& canvas, QPointF& snapped);

    /// Shift flips the sweep of an arc slot.
    bool keyPress(SketchCanvas& canvas, QKeyEvent* event) override;

    /// Only the linear slot modes snap; the arc modes follow their arc.
    bool supportsAngleSnap(const SketchCanvas& canvas) const override;

private:
    // drawPreview, arc and linear slot families.
    void drawArcSlotPreview(SketchCanvas& canvas, QPainter& painter,
                            const QPointF& p1World, const QPointF& p2World, double radius);
    void drawLinearSlotPreview(SketchCanvas& canvas, QPainter& painter,
                               const QPointF& p1World, const QPointF& p2World, double radius);
};

}  // namespace hobbycad
#endif
