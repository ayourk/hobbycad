// =====================================================================
//  src/hobbycad/gui/tools/circletoolhandler.h — Circle tool handler
// =====================================================================
//
//  The rules are the library's (sketch::placement*). The 3-point circle is
//  staged here; the tangent circles pick their curves here first; the rest
//  go through the canvas's two-click path.
//
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#ifndef HOBBYCAD_CIRCLETOOLHANDLER_H
#define HOBBYCAD_CIRCLETOOLHANDLER_H

#include "placementtoolhandler.h"

namespace hobbycad {

class CircleToolHandler : public PlacementToolHandler {
public:
    SketchTool tool() const override { return SketchTool::Circle; }
    sketch::PlacementKind kind(const SketchCanvas& canvas) const override;
    bool applyCreationMode(SketchCanvas& canvas, int modeValue) override;
    bool normalize(SketchCanvas& canvas, SketchEntity& entity, bool& valid) override;
    bool mousePress(SketchCanvas& canvas, QMouseEvent* event, const QPointF& world) override;
    bool mouseRelease(SketchCanvas& canvas, QMouseEvent* event, const QPointF& world) override;
    /// Only the 3-point circle is multi-click; the rest finish on click two.
    bool isMultiClick(const SketchCanvas& canvas) const override;
    bool beginsOnFirstClick(const SketchCanvas& canvas) const override;
};

}  // namespace hobbycad

#endif
