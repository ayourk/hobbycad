// =====================================================================
//  src/hobbycad/gui/tools/arctoolhandler.h — Arc tool handler
// =====================================================================
//
//  The rules are the library's (sketch::placement*). The three point modes
//  are staged here; the tangent arc picks its line first, and says why
//  when it cannot.
//
//  Part of HobbyCAD.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_ARCTOOLHANDLER_H
#define HOBBYCAD_ARCTOOLHANDLER_H

#include "placementtoolhandler.h"

#include <QCoreApplication>

namespace hobbycad {

class ArcToolHandler : public PlacementToolHandler {
    Q_DECLARE_TR_FUNCTIONS(hobbycad::SketchCanvas)

public:
    SketchTool tool() const override { return SketchTool::Arc; }
    sketch::PlacementKind kind(const SketchCanvas& canvas) const override;
    bool applyCreationMode(SketchCanvas& canvas, int modeValue) override;
    bool normalize(SketchCanvas& canvas, SketchEntity& entity, bool& valid) override;
    /// A click places the next point of the staged modes; the tangent arc's
    /// first click picks its line, its second places the end.
    bool mousePress(SketchCanvas& canvas, QMouseEvent* event, const QPointF& world) override;
    /// A drag through a stage places the next point here.
    bool mouseRelease(SketchCanvas& canvas, QMouseEvent* event, const QPointF& world) override;
    /// Every arc mode except Tangent keeps collecting points past the second
    /// click.
    bool isMultiClick(const SketchCanvas& canvas) const override;
    bool beginsOnFirstClick(const SketchCanvas& canvas) const override;
    /// Shift flips the sweep; Ctrl redraws Start+End+Radius for its exact
    /// half circle.
    bool keyPress(SketchCanvas& canvas, QKeyEvent* event) override;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_ARCTOOLHANDLER_H
