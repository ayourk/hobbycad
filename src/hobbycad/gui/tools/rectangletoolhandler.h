// =====================================================================
//  src/hobbycad/gui/tools/rectangletoolhandler.h — Rectangle tool handler
// =====================================================================
//
//  Corner, Center, 3-Point and Parallelogram. The rules are the library's
//  (sketch::placement*); the two three-click modes are staged here, the
//  other two go through the canvas's two-click path.
//
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#ifndef HOBBYCAD_RECTANGLETOOLHANDLER_H
#define HOBBYCAD_RECTANGLETOOLHANDLER_H

#include "placementtoolhandler.h"

namespace hobbycad {

class RectangleToolHandler : public PlacementToolHandler {
public:
    SketchTool tool() const override { return SketchTool::Rectangle; }
    sketch::PlacementKind kind(const SketchCanvas& canvas) const override;
    bool applyCreationMode(SketchCanvas& canvas, int modeValue) override;
    bool mousePress(SketchCanvas& canvas, QMouseEvent* event, const QPointF& world) override;
    bool mouseRelease(SketchCanvas& canvas, QMouseEvent* event, const QPointF& world) override;
};

}  // namespace hobbycad

#endif
