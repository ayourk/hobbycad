// =====================================================================
//  src/hobbycad/gui/tools/simpletoolhandlers.cpp
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#include "simpletoolhandlers.h"

#include <QCoreApplication>

namespace hobbycad {

QString SimpleToolHandler::hint(const SketchCanvas&) const
{
    return QCoreApplication::translate("hobbycad::SketchCanvas", m_hint);
}

std::vector<std::unique_ptr<SketchToolHandler>> makeSimpleToolHandlers()
{
    std::vector<std::unique_ptr<SketchToolHandler>> v;
    auto add = [&v](SketchTool t, const char* h) {
        v.push_back(std::make_unique<SimpleToolHandler>(t, h));
    };

    // The default tool. Not a placement tool, but it is what the user sees
    // most, so it gets a prompt too.
    add(SketchTool::Select, "Select: click an entity, or drag a box  (Ctrl = add to selection)");



    // Annotation and reference tools (the editing operations live in
    // optoolhandlers.{h,cpp}).
    // Text, Dimension and Constraint have their own handlers.

    return v;
}

}  // namespace hobbycad
