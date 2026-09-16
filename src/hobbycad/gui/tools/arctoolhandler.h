// =====================================================================
//  src/hobbycad/gui/tools/arctoolhandler.h — Arc tool handler
// =====================================================================
//
//  Second tool migrated, and the first with substantial per-tool logic of its
//  own: four modes (ThreePoint, CenterStartEnd, StartEndRadius, Tangent),
//  three of them staged over three points, each with its own locked-dimension
//  behavior.
//
//  Unlike Line, Arc does NOT ride the shared two-point machinery: every mode
//  except Tangent has its own early-return branch in mousePressEvent, and the
//  same staged-constraint logic is invoked again from mouseReleaseEvent for
//  drag-through placement.
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================
#ifndef HOBBYCAD_ARCTOOLHANDLER_H
#define HOBBYCAD_ARCTOOLHANDLER_H

#include "sketchtoolhandler.h"
#include <QCoreApplication>

class QPointF;

namespace hobbycad {

class ArcToolHandler : public SketchToolHandler {
    Q_DECLARE_TR_FUNCTIONS(hobbycad::SketchCanvas)
public:
    SketchTool tool() const override { return SketchTool::Arc; }
    bool applyCreationMode(SketchCanvas& canvas, int modeValue) override;

    bool initDimFields(SketchCanvas& canvas) override;
    QString hint(const SketchCanvas& canvas) const override;
    bool beginEntity(SketchCanvas& canvas, SketchEntity& entity) override;
    bool normalize(SketchCanvas& canvas, SketchEntity& entity, bool& valid) override;
    bool updateEntity(SketchCanvas& canvas, const QPointF& pos) override;
    bool drawPreview(SketchCanvas& canvas, QPainter& painter) override;

    // Staged placement constraints, owned by this handler rather than the
    // canvas. Called from BOTH mousePressEvent (click placement) and
    // mouseReleaseEvent (drag-through placement); both call sites required.
    static void constrainCenterStartEnd(const SketchCanvas& canvas, QPointF& snapped);
    static void constrainStartEndRadius(const SketchCanvas& canvas, QPointF& snapped);

    /// Staged placement for the three point-based modes (ThreePoint,
    /// CenterStartEnd, StartEndRadius). Handles ONE click, whether it arrived
    /// as a press (click placement) or a release (drag-through placement).
    bool mousePress(SketchCanvas& canvas, QMouseEvent* event, const QPointF& world) override;

    /// The drag-through half of staged placement. On a click, mousePress has
    /// already placed the point and this only checks for completion; on a
    /// drag, THIS places the next point.
    bool mouseRelease(SketchCanvas& canvas, QMouseEvent* event, const QPointF& world) override;

    /// Every arc mode except Tangent keeps collecting points past the second
    /// click.
    bool isMultiClick(const SketchCanvas& canvas) const override;
    bool beginsOnFirstClick(const SketchCanvas& canvas) const override;

    /// Shift flips the sweep for Center+Start+End, Start+End+Radius and tangent arcs; Ctrl refreshes the Start+End+Radius preview for its 180-degree snap.
    bool keyPress(SketchCanvas& canvas, QKeyEvent* event) override;

    /// Tangent arc: the second point must lie on the arc path implied by the
    /// tangent point, so the cursor is projected onto that path.
    bool constrainCursor(SketchCanvas& canvas, QPointF& world, bool altHeld) override;

private:
    // drawPreview, one function per arc mode.
    void drawThreePointPreview(SketchCanvas& canvas, QPainter& painter);
    void drawCenterStartEndPreview(SketchCanvas& canvas, QPainter& painter);
    void drawStartEndRadiusPreview(SketchCanvas& canvas, QPainter& painter);
    void drawTangentPreview(SketchCanvas& canvas, QPainter& painter);
};

}  // namespace hobbycad

#endif  // HOBBYCAD_ARCTOOLHANDLER_H
