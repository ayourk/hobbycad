// =====================================================================
//  src/hobbycad/gui/tools/ellipsetoolhandler.h — Ellipse tool handler
// =====================================================================
//
//  Two-point tool riding the shared placement path, with a single Major
//  Radius dimension field from the first placed point onward.
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
};

}  // namespace hobbycad
#endif
