// =====================================================================
//  HobbyCAD — src/hobbycad/gui/sketchtoolbar.cpp — Sketch mode toolbar
// =====================================================================

#include "sketchtoolbar.h"

#include <hobbycad/layout/arrangement.h>

#include <cstring>

namespace hobbycad {

namespace {

bool hasPrefix(const char* s, const char* prefix)
{
    return std::strncmp(s, prefix, std::strlen(prefix)) == 0;
}

}  // namespace

SketchToolbar::SketchToolbar(QWidget* parent)
    : ArrangedToolbar(layout::kSketchToolbar, parent)
{
    setObjectName(QStringLiteral("SketchToolbar"));
}

void SketchToolbar::commandChosen(const commands::Command& command,
                                  const std::string& groupId, Via /*via*/, bool checked)
{
    const std::string id = command.id;
    switch (command.kind) {
    case commands::Kind::Tool:
        activate(command.sketchTool,
                 command.hasMode ? command.mode : CreationMode::Default, groupId);
        break;
    case commands::Kind::Action:
        // Transforms act on the selection once; they are not a tool.
        if (hasPrefix(command.id, "sketch.transform.") && command.arg >= 0) {
            emit transformRequested(command.arg);
        } else if (id == "sketch.finish") {
            emit finishSketchRequested();
        }
        break;
    case commands::Kind::Toggle:
        if (id == "sketch.toggle3d") {
            emit sketch3DModeToggled(checked);
        } else if (id == "sketch.flip") {
            emit sketchFlipToggled(checked);
        }
        break;
    default:
        break;
    }
}

void SketchToolbar::activate(SketchTool tool, CreationMode mode, const std::string& groupId)
{
    // Choosing the tool and mode already active puts the toolbar back on
    // Select; a different mode of the same tool (Slot, then Arc Slot) keeps
    // the tool.
    const bool same = (tool == m_activeTool && mode == m_creationMode);
    const SketchTool newTool = same ? SketchTool::Select : tool;
    const CreationMode newMode =
        (newTool == SketchTool::Select) ? CreationMode::Default : mode;

    markActiveGroup(newTool != SketchTool::Select ? groupId : std::string());

    if (newTool != m_activeTool || newMode != m_creationMode) {
        m_activeTool = newTool;
        m_creationMode = newMode;
        emit toolChanged(m_activeTool);
        emit toolSelected(m_activeTool, m_creationMode);
    }
}

void SketchToolbar::setActiveTool(SketchTool tool)
{
    setActiveTool(tool, CreationMode::Default);
}

void SketchToolbar::setActiveTool(SketchTool tool, CreationMode mode)
{
    m_activeTool = tool;
    m_creationMode = mode;
    const commands::Command* cmd = commands::commandForTool(tool);
    markActiveGroup(cmd ? groupOf(*cmd) : std::string());
}

void SketchToolbar::resetCreateButton()
{
    resetGroups();
}

void SketchToolbar::revertCreationMode(SketchTool tool)
{
    const commands::Command* cmd = commands::commandForTool(tool);
    if (!cmd) return;
    const std::string group = groupOf(*cmd);
    revertGroup(group);
    const commands::Command* now = groupCommand(group);
    m_activeTool = now ? now->sketchTool : SketchTool::Select;
    m_creationMode = (now && now->hasMode) ? now->mode : CreationMode::Default;
}

void SketchToolbar::setFlipChecked(bool on)
{
    setButtonChecked("sketch.flip", on);
}

void SketchToolbar::set3DChecked(bool on)
{
    setButtonChecked("sketch.toggle3d", on);
}

}  // namespace hobbycad
