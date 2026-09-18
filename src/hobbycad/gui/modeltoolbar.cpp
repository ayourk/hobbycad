// =====================================================================
//  HobbyCAD — src/hobbycad/gui/modeltoolbar.cpp — 3D Model mode toolbar
// =====================================================================

#include "modeltoolbar.h"

#include <hobbycad/layout/arrangement.h>

namespace hobbycad {

ModelToolbar::ModelToolbar(QWidget* parent)
    : ArrangedToolbar(layout::kModelToolbar, parent)
{
    setObjectName(QStringLiteral("ModelToolbar"));
}

void ModelToolbar::commandChosen(const commands::Command& command,
                                 const std::string& groupId, Via /*via*/, bool /*checked*/)
{
    if (command.kind != commands::Kind::Tool) return;
    const ModelTool tool = command.modelTool;

    // Parameters opens a dialog; it is never the active tool, so the click
    // must not leave its button checked.
    if (tool == ModelTool::Parameters) {
        const commands::Command* active = commands::commandForModelTool(m_activeTool);
        markActiveGroup(active ? groupOf(*active) : std::string());
        emit parametersClicked();
        return;
    }

    // Choosing the active tool again deselects it.
    const ModelTool newTool = (tool == m_activeTool) ? ModelTool::None : tool;
    markActiveGroup(newTool != ModelTool::None ? groupId : std::string());
    if (newTool != m_activeTool) {
        m_activeTool = newTool;
        emit toolSelected(m_activeTool);
    }

    switch (tool) {
    case ModelTool::Sketch:
        emit createSketchClicked();
        break;
    case ModelTool::SketchOnFace:
        emit createSketchOnFaceClicked();
        break;
    case ModelTool::ConstructionPlane:
        emit createConstructionPlaneClicked();
        break;
    default:
        break;
    }
}

void ModelToolbar::setActiveTool(ModelTool tool)
{
    m_activeTool = tool;
    const commands::Command* cmd = commands::commandForModelTool(tool);
    markActiveGroup(cmd ? groupOf(*cmd) : std::string());
}

void ModelToolbar::resetAllButtons()
{
    resetGroups();
    m_activeTool = ModelTool::None;
    markActiveGroup(std::string());
}

}  // namespace hobbycad
