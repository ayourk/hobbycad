// =====================================================================
//  src/hobbycad/gui/tools/polygontoolhandler.h — Polygon tool handler
// =====================================================================
//
//  Three modes. Inscribed/Circumscribed place a regular N-gon from a center
//  and radius; Freeform accumulates vertices until the user clicks near the
//  first one.
//
//  ⚠ The SCROLL WHEEL adjusts the side count during placement (3..64) instead
//  of zooming. That control exists nowhere in the UI and is documented
//  nowhere else; see wheel().
//
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#ifndef HOBBYCAD_POLYGONTOOLHANDLER_H
#define HOBBYCAD_POLYGONTOOLHANDLER_H

#include "sketchtoolhandler.h"
#include <QCoreApplication>

namespace hobbycad {

class PolygonToolHandler : public SketchToolHandler {
    Q_DECLARE_TR_FUNCTIONS(hobbycad::SketchCanvas)
public:
    SketchTool tool() const override { return SketchTool::Polygon; }
    bool applyCreationMode(SketchCanvas& canvas, int modeValue) override;
    bool initDimFields(SketchCanvas& canvas) override;
    bool wheel(SketchCanvas& canvas, QWheelEvent* event) override;
    QString hint(const SketchCanvas& canvas) const override;
    bool beginEntity(SketchCanvas& canvas, SketchEntity& entity) override;
    bool normalize(SketchCanvas& canvas, SketchEntity& entity, bool& valid) override;
    bool updateEntity(SketchCanvas& canvas, const QPointF& pos) override;
    bool drawPreview(SketchCanvas& canvas, QPainter& painter) override;

    /// Freeform polygon: each release adds a vertex, and releasing near the
    /// first point closes the loop.
    bool mouseRelease(SketchCanvas& canvas, QMouseEvent* event,
                      const QPointF& world) override;

    /// Right-click ends the point run.
    bool finishesOnRightClick(const SketchCanvas& canvas) const override;

    /// The freeform polygon keeps collecting vertices, so the second click
    /// must not finish it. The old condition list in mousePressEvent left
    /// this out, which finished a freeform polygon after two clicks: too
    /// few points to be valid, so it was silently discarded and nothing was
    /// drawn at all.
    bool isMultiClick(const SketchCanvas& canvas) const override;
};

}  // namespace hobbycad
#endif
