// =====================================================================
//  src/hobbycad/gui/tools/texttoolhandler.h — Text tool handler
// =====================================================================
//
//  Text places a single point and carries a string. It has no staged
//  placement and no dimension fields, so the handler only owns the two
//  things the canvas used to special-case: typing the pending entity and
//  validating it (a text entity needs a position AND non-empty text, and
//  gets a rotation handle once it is valid).
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================
#ifndef HOBBYCAD_TEXTTOOLHANDLER_H
#define HOBBYCAD_TEXTTOOLHANDLER_H

#include "sketchtoolhandler.h"

namespace hobbycad {

class TextToolHandler : public SketchToolHandler {
public:
    SketchTool tool() const override { return SketchTool::Text; }

    QString hint(const SketchCanvas& canvas) const override;
    bool beginEntity(SketchCanvas& canvas, SketchEntity& entity) override;
    bool normalize(SketchCanvas& canvas, SketchEntity& entity, bool& valid) override;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_TEXTTOOLHANDLER_H
