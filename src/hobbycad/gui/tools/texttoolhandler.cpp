// =====================================================================
//  src/hobbycad/gui/tools/texttoolhandler.cpp
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#include "texttoolhandler.h"
#include "../sketchcanvas.h"

#include <QCoreApplication>

namespace hobbycad {

QString TextToolHandler::hint(const SketchCanvas&) const
{
    return QCoreApplication::translate("hobbycad::SketchCanvas",
                                       "Text: click to place a text label");
}

bool TextToolHandler::beginEntity(SketchCanvas&, SketchEntity& entity)
{
    entity.type = SketchEntityType::Text;
    return true;
}

bool TextToolHandler::normalize(SketchCanvas&, SketchEntity& entity,
                                bool& valid)
{
    if (entity.type != SketchEntityType::Text) {
        return false;
    }
    // A text entity needs both a position and something to say.
    valid = !entity.points.empty() && !entity.text.empty();
    if (valid) {
        SketchCanvas::ensureTextRotationHandle(entity);
    }
    return true;
}

}  // namespace hobbycad
