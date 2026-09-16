// =====================================================================
//  src/hobbycad/gui/tools/rectangletoolhandler.h — Rectangle tool handler
// =====================================================================
//
//  Four modes. Corner/Center place with two clicks; ThreePoint and
//  Parallelogram are staged over three.
//
//  ⚠ Rectangle is one of the entity types that DECOMPOSES on commit: a
//  finished rectangle becomes 4 lines plus a constraint web plus a group, in
//  one compound undo step (libhobbycad/sketch/decomposition.cpp). Locked
//  dimensions are consumed by the decomposition rather than by
//  createLockedConstraints().
//
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#ifndef HOBBYCAD_RECTANGLETOOLHANDLER_H
#define HOBBYCAD_RECTANGLETOOLHANDLER_H

#include "sketchtoolhandler.h"
#include <QCoreApplication>

namespace hobbycad {

class RectangleToolHandler : public SketchToolHandler {
    Q_DECLARE_TR_FUNCTIONS(hobbycad::SketchCanvas)
public:
    SketchTool tool() const override { return SketchTool::Rectangle; }
    bool applyCreationMode(SketchCanvas& canvas, int modeValue) override;
    bool initDimFields(SketchCanvas& canvas) override;
    QString hint(const SketchCanvas& canvas) const override;
    bool beginEntity(SketchCanvas& canvas, SketchEntity& entity) override;
    bool normalize(SketchCanvas& canvas, SketchEntity& entity, bool& valid) override;
    bool updateEntity(SketchCanvas& canvas, const QPointF& pos) override;
    bool drawPreview(SketchCanvas& canvas, QPainter& painter) override;

    bool mousePress(SketchCanvas& canvas, QMouseEvent* event, const QPointF& world) override;
    bool mouseRelease(SketchCanvas& canvas, QMouseEvent* event, const QPointF& world) override;

    /// Staged constraints for the two three-point modes. Called from BOTH
    /// press and release; both call sites are required (click vs drag-through).
    static void constrainThreePoint(const SketchCanvas& canvas, QPointF& snapped);
    static void constrainParallelogram(const SketchCanvas& canvas, QPointF& snapped);

    /// Rotation reference for a rectangle whose width AND height are both
    /// locked: after that point the mouse rotates the rectangle instead of
    /// resizing it. This state lived on SketchCanvas purely because the
    /// key handler set it and the move handler read it; both go through this
    /// handler now, so it belongs here.
    void dimFieldsChanged(SketchCanvas& canvas) override;

private:
    // drawPreview, one function per rectangle mode family.
    void drawThreePointPreview(SketchCanvas& canvas, QPainter& painter);
    void drawParallelogramPreview(SketchCanvas& canvas, QPainter& painter);
    void drawCornerOrCenterPreview(SketchCanvas& canvas, QPainter& painter);
    bool   m_bothLocked = false;    ///< true once width and height are locked
    double m_lockRefAngle = 0.0;    ///< mouse angle at the moment both locked
    double m_lockWidthAngle = 0.0;  ///< initial width direction (0 or pi)
    double m_lockHeightAngle = 0.0; ///< initial height direction (+/- pi/2)

    /// Every rectangle mode snaps.
    bool supportsAngleSnap(const SketchCanvas& canvas) const override;
};

}  // namespace hobbycad
#endif
