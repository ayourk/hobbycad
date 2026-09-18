// =====================================================================
//  HobbyCAD — src/hobbycad/gui/sketchtoolbar.h — Sketch mode toolbar
// =====================================================================
//
//  Horizontal toolbar for 2D sketch operations, laid out from the sketch
//  toolbar of the arrangement (hobbycad/layout/arrangement.h): tool groups
//  with dropdowns (Create, Constrain, Modify, Pattern by default), the
//  3D and Flip latches and Finish Sketch.
//
//  Each creation tool may have several modes (Rectangle: Corner, Center,
//  3-Point); the arrow beside a dropdown row lists them.
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_SKETCHTOOLBAR_H
#define HOBBYCAD_SKETCHTOOLBAR_H

#include <hobbycad/commands.h>

#include "arrangedtoolbar.h"

namespace hobbycad {

class SketchToolbar : public ArrangedToolbar {
    Q_OBJECT

public:
    explicit SketchToolbar(QWidget* parent = nullptr);

    /// Get the currently active tool
    SketchTool activeTool() const { return m_activeTool; }

    /// Get the current creation mode for the active tool
    CreationMode creationMode() const { return m_creationMode; }

    /// Set the active tool (uses default creation mode)
    void setActiveTool(SketchTool tool);

    /// Set the active tool with specific creation mode
    void setActiveTool(SketchTool tool, CreationMode mode);

    /// Put every group back on its first tool (Create back to its list).
    void resetCreateButton();

    /// Revert to the previous creation mode for a tool (when mode selection
    /// is rejected)
    void revertCreationMode(SketchTool tool);

    /// Reflect the canvas's heads/tails state on the Flip latch without
    /// emitting a toggle.
    void setFlipChecked(bool on);

    /// Reflect the 3D-mode state on the 3D latch without emitting a toggle
    /// (used when the viewport is dropped and the sketch falls back to 2D).
    void set3DChecked(bool on);

signals:
    /// Emitted when a tool is selected (for basic tool changes)
    void toolChanged(SketchTool tool);

    /// Emitted when a tool with specific mode is selected
    void toolSelected(SketchTool tool, CreationMode mode);
    /// A transform (Move/Rotate/Scale/Mirror/Copy) was chosen from the
    /// Modify dropdown. Carries a sketch::TransformType as int. Transform is
    /// not a persistent tool, so this is separate from toolSelected.
    void transformRequested(int transformType);
    /// The 3D-mode checkmark toggled (independent latch, not a tool).
    void sketch3DModeToggled(bool on);

    /// Heads/tails view flip toggled (independent latch): draw from the far
    /// side of the plane. A view flip only: coordinates never change.
    void sketchFlipToggled(bool on);

    /// The Finish Sketch toolbar button was pressed.
    void finishSketchRequested();

protected:
    void commandChosen(const commands::Command& command, const std::string& groupId,
                       Via via, bool checked) override;

private:
    void activate(SketchTool tool, CreationMode mode, const std::string& groupId);

    SketchTool m_activeTool = SketchTool::Line;
    CreationMode m_creationMode = CreationMode::Default;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_SKETCHTOOLBAR_H
