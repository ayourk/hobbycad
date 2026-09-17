// =====================================================================
//  src/hobbycad/gui/tools/ellipsetoolhandler.h — Ellipse tool handler
// =====================================================================
//
//  Staged tool: every placement (whole ellipse or elliptical arc) is a
//  sequence of clicks placed on PRESS, previewed live, and committed when
//  the placement's click count is reached. Which clicks mean what lives in
//  the library (sketch::ellipseFromPlacement); this handler only routes
//  clicks, locks, hints and the preview through that one rule.
//
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#ifndef HOBBYCAD_ELLIPSETOOLHANDLER_H
#define HOBBYCAD_ELLIPSETOOLHANDLER_H

#include "sketchtoolhandler.h"

namespace hobbycad {

class EllipseToolHandler : public SketchToolHandler {
public:
    SketchTool tool() const override { return SketchTool::Ellipse; }
    bool initDimFields(SketchCanvas& canvas) override;
    QString hint(const SketchCanvas& canvas) const override;
    bool beginEntity(SketchCanvas& canvas, SketchEntity& entity) override;
    bool normalize(SketchCanvas& canvas, SketchEntity& entity, bool& valid) override;
    bool updateEntity(SketchCanvas& canvas, const QPointF& pos) override;
    bool drawPreview(SketchCanvas& canvas, QPainter& painter) override;

    /// Every placement takes three clicks or more, so the second must not commit.
    bool isMultiClick(const SketchCanvas&) const override { return true; }
    /// Clicks are placed on PRESS (mousePress); a drag through a stage
    /// places the next point on release. The release is consumed while
    /// drawing so the canvas's click-drag finish cannot end the entity early.
    bool mouseRelease(SketchCanvas& canvas, QMouseEvent* event,
                      const QPointF& world) override;
    /// Elliptical Arc, choosing the start and end: the cursor rides the
    /// perimeter of the ellipse the first three clicks fixed.
    bool constrainCursor(SketchCanvas& canvas, QPointF& world, bool altHeld) override;
    /// Shift while an arc's end is being chosen: the long way around (the
    /// canvas's arc-flip state, shared with the circular arcs and slots).
    bool keyPress(SketchCanvas& canvas, QKeyEvent* event) override;

    /// Stages every click after the first; the first still starts the entity
    /// through the canvas, as the staged arc and circle modes do.
    bool mousePress(SketchCanvas& canvas, QMouseEvent* event,
                    const QPointF& world) override;

    /// 0 Center + Axes, 1 3-Point, 2 Elliptical Arc, 3 Span + Rise arc,
    /// 4 Corner arc, 5 Endpoints arc (the toolbar's CreationMode order).
    bool applyCreationMode(SketchCanvas& canvas, int modeValue) override;
};

}  // namespace hobbycad
#endif
