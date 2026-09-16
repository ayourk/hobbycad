// =====================================================================
//  src/hobbycad/gui/tools/constrainttoolhandler.h — Constraint tool
// =====================================================================
//
//  The Constraint tool is not a placement tool: its clicks build a
//  selection, and as soon as the selection supports a constraint the tool
//  applies it. Before this handler existed the tool set a cursor and a
//  status hint and did nothing else, which left every apply*Constraint()
//  function on SketchCanvas without a caller.
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================
#ifndef HOBBYCAD_CONSTRAINTTOOLHANDLER_H
#define HOBBYCAD_CONSTRAINTTOOLHANDLER_H

#include "sketchtoolhandler.h"

namespace hobbycad {

class ConstraintToolHandler : public SketchToolHandler {
public:
    SketchTool tool() const override { return SketchTool::Constraint; }

    QString hint(const SketchCanvas& canvas) const override;

    /// Plain click starts a fresh pair; Ctrl accumulates. Clicking empty
    /// space clears the selection.
    bool mousePress(SketchCanvas& canvas, QMouseEvent* event,
                    const QPointF& world) override;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_CONSTRAINTTOOLHANDLER_H
