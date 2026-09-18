// =====================================================================
//  src/hobbycad/gui/tools/splinetoolhandler.h — Spline tool handler
// =====================================================================
//
//  The Bezier pen (sketch::BezierPen), fit points, and the conic arc by
//  rho. The rules are the library's (sketch::placement*); this handler
//  turns presses, drags and releases into them.
//
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#ifndef HOBBYCAD_SPLINETOOLHANDLER_H
#define HOBBYCAD_SPLINETOOLHANDLER_H

#include "placementtoolhandler.h"

namespace hobbycad {

class SplineToolHandler : public PlacementToolHandler {
public:
    SketchTool tool() const override { return SketchTool::Spline; }
    sketch::PlacementKind kind(const SketchCanvas& canvas) const override;
    bool beginEntity(SketchCanvas& canvas, SketchEntity& entity) override;
    bool normalize(SketchCanvas& canvas, SketchEntity& entity, bool& valid) override;
    void cancel(SketchCanvas& canvas) override;
    /// "Control Points" (0) = Bezier pen; "Fit Points" (1) = Catmull-Rom;
    /// "Rational Bezier" (2) = the pen with weights; "Conic Arc (Rho)" (3).
    bool applyCreationMode(SketchCanvas& canvas, int modeValue) override;
    /// A spline collects points until the user right-clicks.
    bool isMultiClick(const SketchCanvas&) const override { return true; }
    /// Right-click ends the point run; a conic is bounded, so there it aborts.
    bool finishesOnRightClick(const SketchCanvas& canvas) const override;
    /// The pen owns press, drag and release to author anchors with handles;
    /// the conic stages four clicks.
    bool mousePress(SketchCanvas& canvas, QMouseEvent* event,
                    const QPointF& world) override;
    bool mouseMove(SketchCanvas& canvas, QMouseEvent* event,
                   const QPointF& world) override;
    /// Fit points add a point per release; the pen finishes the anchor's
    /// handles; a conic places its next click when the stage was dragged.
    bool mouseRelease(SketchCanvas& canvas, QMouseEvent* event,
                      const QPointF& world) override;
    /// Enter, Escape and right-click finish the spline, keeping its points.
    bool keyPress(SketchCanvas& canvas, QKeyEvent* event) override;

protected:
    sketch::PlacementStage stage(const SketchCanvas& canvas) const override;
    sketch::PlacementPreview preview(const SketchCanvas& canvas) const override;

private:
    bool isPen(const SketchCanvas& canvas) const;

    /// The pen's anchors while one is being authored; the mode lives on the
    /// canvas.
    sketch::BezierPen m_pen;
};

}  // namespace hobbycad

#endif
