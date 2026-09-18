// =====================================================================
//  src/hobbycad/gui/tools/polygontoolhandler.h — Polygon tool handler
// =====================================================================
//
//  The rules are the library's (sketch::placement*). A regular polygon
//  goes through the canvas's two-click path with the wheel setting its
//  side count; a freeform one takes vertices here until it is closed or
//  finished.
//
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#ifndef HOBBYCAD_POLYGONTOOLHANDLER_H
#define HOBBYCAD_POLYGONTOOLHANDLER_H

#include "placementtoolhandler.h"

namespace hobbycad {

class PolygonToolHandler : public PlacementToolHandler {
public:
    SketchTool tool() const override { return SketchTool::Polygon; }
    sketch::PlacementKind kind(const SketchCanvas& canvas) const override;
    bool applyCreationMode(SketchCanvas& canvas, int modeValue) override;
    bool wheel(SketchCanvas& canvas, QWheelEvent* event) override;
    bool beginEntity(SketchCanvas& canvas, SketchEntity& entity) override;
    /// Freeform polygon: each release adds a vertex, and releasing near the
    /// first point closes the loop.
    bool mouseRelease(SketchCanvas& canvas, QMouseEvent* event,
                      const QPointF& world) override;
    /// Right-click ends the point run.
    bool finishesOnRightClick(const SketchCanvas& canvas) const override;
    /// The freeform polygon keeps collecting vertices, so the second click
    /// must not finish it.
    bool isMultiClick(const SketchCanvas& canvas) const override;
};

}  // namespace hobbycad

#endif
