// =====================================================================
//  HobbyCAD — src/hobbycad/gui/modeltoolbar.h — 3D Model mode toolbar
// =====================================================================
//
//  Toolbar for 3D modeling operations, laid out from the model toolbar of
//  the arrangement (hobbycad/layout/arrangement.h).
//
//  Every group button works at once: it runs its group's default tool
//  until another is picked from its dropdown, and remembers the pick until
//  reset (Escape). A group none of whose tools exist yet is disabled.
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_MODELTOOLBAR_H
#define HOBBYCAD_MODELTOOLBAR_H

#include <hobbycad/commands.h>

#include "arrangedtoolbar.h"

namespace hobbycad {

class ModelToolbar : public ArrangedToolbar {
    Q_OBJECT

public:
    explicit ModelToolbar(QWidget* parent = nullptr);

    /// Get the currently active tool
    ModelTool activeTool() const { return m_activeTool; }

    /// Programmatically set the active tool
    void setActiveTool(ModelTool tool);

    /// Reset all buttons to their default state (called on ESC)
    void resetAllButtons();

signals:
    /// Emitted when a tool is selected
    void toolSelected(ModelTool tool);

    /// Emitted when Sketch > Sketch is clicked
    void createSketchClicked();

    /// Emitted when Sketch > Sketch on Face is clicked
    void createSketchOnFaceClicked();

    /// Emitted when Plane > Construction Plane is clicked
    void createConstructionPlaneClicked();

    /// Emitted when Parameters button is clicked
    void parametersClicked();

protected:
    void commandChosen(const commands::Command& command, const std::string& groupId,
                       Via via, bool checked) override;

private:
    ModelTool m_activeTool = ModelTool::None;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_MODELTOOLBAR_H
