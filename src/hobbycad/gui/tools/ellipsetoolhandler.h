// =====================================================================
//  src/hobbycad/gui/tools/ellipsetoolhandler.h — Ellipse tool handler
// =====================================================================
//
//  The ellipse and the elliptical arcs. Their rules are the library's
//  (sketch::placement*, over sketch::ellipseFromPlacement); this handler
//  stages the clicks.
//
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#ifndef HOBBYCAD_ELLIPSETOOLHANDLER_H
#define HOBBYCAD_ELLIPSETOOLHANDLER_H

#include "placementtoolhandler.h"

namespace hobbycad {

class EllipseToolHandler : public PlacementToolHandler {
public:
    SketchTool tool() const override { return SketchTool::Ellipse; }
    sketch::PlacementKind kind(const SketchCanvas& canvas) const override;

    /// Every placement takes three clicks or more, so the second must not commit.
    bool isMultiClick(const SketchCanvas&) const override { return true; }
    /// Stages every click after the first; the first still starts the entity
    /// through the canvas, as the staged arc and circle modes do.
    bool mousePress(SketchCanvas& canvas, QMouseEvent* event,
                    const QPointF& world) override;
    /// A drag through a stage places the next point on release. The release
    /// is consumed while drawing so the canvas's click-drag finish cannot end
    /// the entity early.
    bool mouseRelease(SketchCanvas& canvas, QMouseEvent* event,
                      const QPointF& world) override;
    /// 0 Center + Axes, 1 3-Point, 2 Elliptical Arc, 3 Span + Rise arc,
    /// 4 Corner arc, 5 Endpoints arc (the toolbar's CreationMode order).
    bool applyCreationMode(SketchCanvas& canvas, int modeValue) override;
};

}  // namespace hobbycad

#endif
