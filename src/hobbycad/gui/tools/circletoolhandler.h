// =====================================================================
//  src/hobbycad/gui/tools/circletoolhandler.h — Circle tool handler
// =====================================================================
//
//  Five modes, and the widest spread of click semantics of any tool:
//
//    CenterRadius  two-point placement; Radius field
//    TwoPoint      two-point placement across a diameter; Diameter field
//    ThreePoint    STAGED over three points; Radius lockable from stage 2
//    TwoTangent    the first two clicks pick ENTITIES, not coordinates
//    ThreeTangent  the first three clicks pick ENTITIES, not coordinates
//
//  The tangent modes are the only place in the sketcher where a click during
//  placement selects an existing entity rather than contributing a point.
//
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#ifndef HOBBYCAD_CIRCLETOOLHANDLER_H
#define HOBBYCAD_CIRCLETOOLHANDLER_H

#include "sketchtoolhandler.h"
#include <QCoreApplication>

namespace hobbycad {

class CircleToolHandler : public SketchToolHandler {
    Q_DECLARE_TR_FUNCTIONS(hobbycad::SketchCanvas)
public:
    SketchTool tool() const override { return SketchTool::Circle; }
    bool applyCreationMode(SketchCanvas& canvas, int modeValue) override;
    bool initDimFields(SketchCanvas& canvas) override;
    QString hint(const SketchCanvas& canvas) const override;
    bool beginEntity(SketchCanvas& canvas, SketchEntity& entity) override;
    bool normalize(SketchCanvas& canvas, SketchEntity& entity, bool& valid) override;
    bool updateEntity(SketchCanvas& canvas, const QPointF& pos) override;

    bool mousePress(SketchCanvas& canvas, QMouseEvent* event, const QPointF& world) override;
    bool mouseRelease(SketchCanvas& canvas, QMouseEvent* event, const QPointF& world) override;
    bool drawPreview(SketchCanvas& canvas, QPainter& painter) override;

    /// Only the 3-point circle is multi-click; the rest finish on click two.
    bool isMultiClick(const SketchCanvas& canvas) const override;
    bool beginsOnFirstClick(const SketchCanvas& canvas) const override;
};

}  // namespace hobbycad
#endif
