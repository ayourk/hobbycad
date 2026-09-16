// =====================================================================
//  src/hobbycad/gui/tools/dimensiontoolhandler.h — Dimension tool
// =====================================================================
//
//  Dimension is a click-sequence tool rather than a placement tool: its
//  clicks pick entities, and the dimension is created once enough targets
//  exist. Two sequences share the handler:
//
//    * single-entity shortcut: a line becomes a Distance dimension and a
//      circle or arc a Radius one, created on the FIRST click;
//    * two-entity sequence: pick entity 1, pick entity 2, then click once
//      more to place the label (angle between two lines, for example).
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================
#ifndef HOBBYCAD_DIMENSIONTOOLHANDLER_H
#define HOBBYCAD_DIMENSIONTOOLHANDLER_H

#include "sketchtoolhandler.h"

namespace hobbycad {

class DimensionToolHandler : public SketchToolHandler {
public:
    SketchTool tool() const override { return SketchTool::Dimension; }

    QString hint(const SketchCanvas& canvas) const override;

    bool mousePress(SketchCanvas& canvas, QMouseEvent* event,
                    const QPointF& world) override;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_DIMENSIONTOOLHANDLER_H
