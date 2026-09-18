// =====================================================================
//  src/hobbycad/gui/tools/slottoolhandler.h — Slot tool handler
// =====================================================================
//
//  The rules are the library's (sketch::placement*). The arc slots are
//  staged here, the straight ones go through the canvas's two-click path,
//  and the wheel sets the width throughout.
//
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#ifndef HOBBYCAD_SLOTTOOLHANDLER_H
#define HOBBYCAD_SLOTTOOLHANDLER_H

#include "placementtoolhandler.h"

#include <QCoreApplication>

namespace hobbycad {

class SlotToolHandler : public PlacementToolHandler {
    Q_DECLARE_TR_FUNCTIONS(hobbycad::SketchCanvas)

public:
    SketchTool tool() const override { return SketchTool::Slot; }
    sketch::PlacementKind kind(const SketchCanvas& canvas) const override;
    bool applyCreationMode(SketchCanvas& canvas, int modeValue) override;
    bool wheel(SketchCanvas& canvas, QWheelEvent* event) override;
    QString cursorHint(const SketchCanvas& canvas) const override;
    bool beginEntity(SketchCanvas& canvas, SketchEntity& entity) override;
    bool mousePress(SketchCanvas& canvas, QMouseEvent* event, const QPointF& world) override;
    bool mouseRelease(SketchCanvas& canvas, QMouseEvent* event, const QPointF& world) override;
};

}  // namespace hobbycad

#endif
