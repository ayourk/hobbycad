// =====================================================================
//  src/hobbycad/gui/tools/linetoolhandler.h — Line tool handler
// =====================================================================
//
//  The rules are the library's (sketch::placement*). Lines go through the
//  canvas's two-click path, except the tangent line, which picks its
//  circle here first.
//
//  Part of HobbyCAD.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_LINETOOLHANDLER_H
#define HOBBYCAD_LINETOOLHANDLER_H

#include "placementtoolhandler.h"

#include <QCoreApplication>

namespace hobbycad {

class LineToolHandler : public PlacementToolHandler {
    Q_DECLARE_TR_FUNCTIONS(hobbycad::SketchCanvas)

public:
    SketchTool tool() const override { return SketchTool::Line; }
    sketch::PlacementKind kind(const SketchCanvas& canvas) const override;
    bool applyCreationMode(SketchCanvas& canvas, int modeValue) override;
    bool beginEntity(SketchCanvas& canvas, SketchEntity& entity) override;
    bool normalize(SketchCanvas& canvas, SketchEntity& entity, bool& valid) override;
    /// The placement's preview, and the Ctrl angle-snap guide over it.
    bool drawPreview(SketchCanvas& canvas, QPainter& painter) override;
    /// Tangent line placement. The first click picks the circle or arc to be
    /// tangent to, the second places the start, the third the end. They are
    /// entity-picking clicks, which is why they cannot ride the shared
    /// two-point machinery.
    bool mousePress(SketchCanvas& canvas, QMouseEvent* event,
                    const QPointF& world) override;
    /// Construction lines preview in the construction-geometry color.
    bool previewPen(const SketchCanvas& canvas, QPen& pen) const override;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_LINETOOLHANDLER_H
