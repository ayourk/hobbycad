// =====================================================================
//  src/hobbycad/gui/tools/pointtoolhandler.h — Point tool handler
// =====================================================================
//
//  The most primitive tool: a single placement, and the ONLY tool that
//  finishes on mouse RELEASE rather than on a second click.  It offers no
//  dimension fields.
//
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#ifndef HOBBYCAD_POINTTOOLHANDLER_H
#define HOBBYCAD_POINTTOOLHANDLER_H

#include "sketchtoolhandler.h"

namespace hobbycad {

class PointToolHandler : public SketchToolHandler {
public:
    SketchTool tool() const override { return SketchTool::Point; }
    bool initDimFields(SketchCanvas&) override { return true; }  // owns them: none
    QString hint(const SketchCanvas& canvas) const override;
    bool beginEntity(SketchCanvas& canvas, SketchEntity& entity) override;
    bool normalize(SketchCanvas& canvas, SketchEntity& entity, bool& valid) override;
    bool drawPreview(SketchCanvas& canvas, QPainter& painter) override;

    /// Point finishes on release, not on a second click, so the shared
    /// two-point finish must not fire for it.
    bool isMultiClick(const SketchCanvas&) const override { return true; }

    /// Point commits on release, not on a second click.
    bool mouseRelease(SketchCanvas& canvas, QMouseEvent* event,
                      const QPointF& world) override;
};

}  // namespace hobbycad
#endif
