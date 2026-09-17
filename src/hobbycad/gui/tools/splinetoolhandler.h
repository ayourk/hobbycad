// =====================================================================
//  src/hobbycad/gui/tools/splinetoolhandler.h — Spline / Bezier tool handler
// =====================================================================
//
//  The only UNBOUNDED tool: clicks keep adding points until the user
//  right-clicks to finish.
//
//  Creation modes (the toolbar dropdown):
//   • "Control Points" (default): a Bezier PEN; click drops a corner anchor,
//     click-drag pulls out symmetric tangent handles (angle = tangent/G1,
//     length = per-side curvature). Produces a splineBezier entity whose points
//     are the cubic control polygon (via sketch::bezierControlPolygon).
//   • "Fit Points": the interpolating Catmull-Rom spline (points-only).
//   • "Rational Bezier": the pen, with a weight per anchor (all 1 until edited).
//   • "Conic Arc (Rho)": the one BOUNDED variant: start, end, apex (where
//     the end tangents meet), then rho by cursor along the apex line; commits
//     one rational cubic Bezier segment (sketch::conicFromRho) with the rho
//     stored on the entity, the Fusion route: rho stays an editable property
//     (Properties panel, `conic <id> rho`). Right-click aborts a half-placed one.
//
//  Finishes on Enter/Return, ESCAPE, or RIGHT-CLICK; all keep the placed
//  anchors (a valid spline commits, an incomplete one is dropped), so Escape is
//  consistent with ending a line chain rather than discarding work.
//
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#ifndef HOBBYCAD_SPLINETOOLHANDLER_H
#define HOBBYCAD_SPLINETOOLHANDLER_H

#include "sketchtoolhandler.h"
#include <hobbycad/sketch/entity.h>   // sketch::BezierAnchor
#include <vector>
#include <QPointF>
#include <QCoreApplication>

namespace hobbycad {

class SplineToolHandler : public SketchToolHandler {
    Q_DECLARE_TR_FUNCTIONS(hobbycad::SketchCanvas)
public:
    SketchTool tool() const override { return SketchTool::Spline; }
    bool initDimFields(SketchCanvas&) override { return true; }  // none
    QString hint(const SketchCanvas& canvas) const override;
    QString cursorHint(const SketchCanvas& canvas) const override;
    bool beginEntity(SketchCanvas& canvas, SketchEntity& entity) override;
    bool normalize(SketchCanvas& canvas, SketchEntity& entity, bool& valid) override;
    bool updateEntity(SketchCanvas& canvas, const QPointF& pos) override;
    bool drawPreview(SketchCanvas& canvas, QPainter& painter) override;
    void cancel(SketchCanvas& canvas) override;

    /// "Control Points" (0) = Bezier pen; "Fit Points" (1) = Catmull-Rom;
    /// "Rational Bezier" (2) = the pen with weights; "Conic Arc (Rho)" (3).
    bool applyCreationMode(SketchCanvas& canvas, int modeValue) override;

    /// A spline collects points until the user right-clicks.
    bool isMultiClick(const SketchCanvas&) const override { return true; }
    /// Right-click ends the point run; a conic is bounded, so there it aborts.
    bool finishesOnRightClick(const SketchCanvas& canvas) const override;
    /// Conic, choosing rho: the cursor rides the chord-midpoint-to-apex line.
    bool constrainCursor(SketchCanvas& canvas, QPointF& world, bool altHeld) override;

    /// Pen mode owns press+drag+release to author anchors with tangent handles.
    bool mousePress(SketchCanvas& canvas, QMouseEvent* event,
                    const QPointF& world) override;
    bool mouseMove(SketchCanvas& canvas, QMouseEvent* event,
                   const QPointF& world) override;
    /// Fit-points mode adds a control point per release; pen finalizes the
    /// current anchor's handles; a conic places its next click when the
    /// stage was dragged through.
    bool mouseRelease(SketchCanvas& canvas, QMouseEvent* event,
                      const QPointF& world) override;
    /// Enter/Return finishes the spline (Fusion parity); right-click also does.
    bool keyPress(SketchCanvas& canvas, QKeyEvent* event) override;

private:
    // The creation mode lives on the canvas (SketchCanvas::splineMode). What
    // stays here is the pen's anchor run while one is being authored.
    std::vector<sketch::BezierAnchor> m_anchors;  ///< pen-authored anchors
    std::vector<bool> m_manual;   ///< per anchor: handles set by dragging (keep)
    /// Give every non-dragged anchor smooth auto handles (Catmull-Rom -> Bezier),
    /// so clicking points yields an arced curve with visible, editable handles.
    void recomputeAutoHandles();
    bool m_dragging = false;   ///< currently pulling the back anchor's handles
    bool m_hasDrag = false;    ///< moved far enough to be a handle pull (else corner)
    QPointF m_anchorPos;       ///< world position of the anchor being placed
};

}  // namespace hobbycad
#endif
