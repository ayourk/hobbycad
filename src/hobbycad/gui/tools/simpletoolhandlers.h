// =====================================================================
//  src/hobbycad/gui/tools/simpletoolhandlers.h — single-click tools
// =====================================================================
//
//  The editing operations and annotation tools. Grouped in one file, against
//  the one-class-per-file convention used for the placement tools, because
//  each is only a hint(): they take no dimension fields, have no sub-modes and
//  no multi-stage placement.
//
//  These are NOT stubs. Each has exactly one branch in mousePressEvent and
//  delegates the real work to libhobbycad (trimEntityAt,
//  sketch::findConnectedLineAtCorner, and so on); they are small because a
//  single-click operation genuinely needs little UI code.
//
//  Split any of these into its own file the moment it grows beyond a hint.
//
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#ifndef HOBBYCAD_SIMPLETOOLHANDLERS_H
#define HOBBYCAD_SIMPLETOOLHANDLERS_H

#include "sketchtoolhandler.h"

#include <memory>
#include <vector>

namespace hobbycad {

/// One hint, one tool. Everything else uses the base-class defaults.
class SimpleToolHandler : public SketchToolHandler {
public:
    SimpleToolHandler(SketchTool tool, const char* hintText)
        : m_tool(tool), m_hint(hintText) {}

    SketchTool tool() const override { return m_tool; }
    bool initDimFields(SketchCanvas&) override { return true; }  // none
    QString hint(const SketchCanvas&) const override;

private:
    SketchTool  m_tool;
    const char* m_hint;
};

/// Every single-click operation and annotation tool, ready to register.
std::vector<std::unique_ptr<SketchToolHandler>> makeSimpleToolHandlers();

}  // namespace hobbycad
#endif
