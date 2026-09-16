// =====================================================================
//  src/hobbycad/gui/tools/linetoolhandler.h — Line tool handler
// =====================================================================
//
//  First tool migrated to the handler interface. Line was chosen because it
//  is the lowest-risk migration, NOT because it is the largest win: its
//  placement rides the shared two-point machinery in mousePressEvent (the
//  `isMultiClickTool` false branch), so only its Tangent mode has
//  tool-specific press handling. What Line does own is mode setup, the
//  Length/Angle dimension fields, construction-geometry flagging and its
//  preview.
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================
#ifndef HOBBYCAD_LINETOOLHANDLER_H
#define HOBBYCAD_LINETOOLHANDLER_H

#include "sketchtoolhandler.h"
#include <QCoreApplication>

namespace hobbycad {

class LineToolHandler : public SketchToolHandler {
    Q_DECLARE_TR_FUNCTIONS(hobbycad::SketchCanvas)
public:
    SketchTool tool() const override { return SketchTool::Line; }
    bool applyCreationMode(SketchCanvas& canvas, int modeValue) override;

    bool initDimFields(SketchCanvas& canvas) override;
    QString hint(const SketchCanvas& canvas) const override;
    QString cursorHint(const SketchCanvas& canvas) const override;
    bool beginEntity(SketchCanvas& canvas, SketchEntity& entity) override;
    bool normalize(SketchCanvas& canvas, SketchEntity& entity, bool& valid) override;
    bool updateEntity(SketchCanvas& canvas, const QPointF& pos) override;
    bool drawPreview(SketchCanvas& canvas, QPainter& painter) override;

    /// Tangent line placement. The first click picks the circle or arc to be
    /// tangent to; the second finishes the line. Both are entity-picking
    /// clicks, which is why they cannot ride the shared two-point machinery.
    bool mousePress(SketchCanvas& canvas, QMouseEvent* event,
                    const QPointF& world) override;

    /// Construction lines preview in the construction-geometry color.
    bool previewPen(const SketchCanvas& canvas, QPen& pen) const override;

    /// Horizontal, Vertical and Tangent already constrain the direction, so angle snap applies only to the free modes.
    bool supportsAngleSnap(const SketchCanvas& canvas) const override;

    /// Horizontal pins Y, Vertical pins X, Tangent projects onto the tangent
    /// ray of the picked circle or arc.
    bool constrainCursor(SketchCanvas& canvas, QPointF& world, bool altHeld) override;

    /// Two Point and Construction differ only by a flag on the entity, so a
    /// point already placed means the same under either and the switch can
    /// happen mid-line. Tangent cannot: it needs a curve picked first, so
    /// the click already made was not the click Tangent expects.
    bool canSwitchModeWhileDrawing(const SketchCanvas& canvas,
                                   int modeValue) const override;

    /// Lines chain, except in Tangent mode: that mode's first click picks
    /// the curve to be tangent to, so there is nothing to continue FROM.
    bool chainsFromLastPoint(const SketchCanvas& canvas) const override;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_LINETOOLHANDLER_H
