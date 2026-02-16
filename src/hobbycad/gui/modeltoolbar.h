// =====================================================================
//  HobbyCAD — src/hobbycad/gui/modeltoolbar.h — 3D Model mode toolbar
// =====================================================================
//
//  Toolbar for 3D modeling operations with dropdown menus.
//  Supports dropdown-first-then-remember behavior for some buttons
//  and default-to-first for others.
//
//  Button behavior:
//  - Default-first: Button works immediately with default tool, remembers
//    selection from dropdown. ESC resets to default.
//  - Dropdown-first: Must select from dropdown first. After selection,
//    button remembers choice. ESC resets to show dropdown again.
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_MODELTOOLBAR_H
#define HOBBYCAD_MODELTOOLBAR_H

#include <QIcon>
#include <QWidget>

class QHBoxLayout;

namespace hobbycad {

class ToolbarButton;

/// Tools available in 3D model mode.
/// These IDs are stable and can be used for serialization in toolbar configs.
/// When adding new tools, append to the end of the relevant group to maintain
/// backward compatibility with saved user configurations.
enum class ModelTool {
    None = 0,

    // --- Sketch group (default: Sketch) ---
    Sketch,
    SketchOnFace,

    // --- Plane group (default: ConstructionPlane) ---
    ConstructionPlane,
    // Future: Surface

    // --- Solid group (default: Extrude) ---
    // Extrude operations
    Extrude,
    CutExtrude,
    // Revolve operations
    Revolve,
    CutRevolve,
    // Loft operations
    Loft,
    CutLoft,
    // Sweep operations
    Sweep,
    CutSweep,
    // Primitives
    Box,
    Cylinder,
    Sphere,
    Torus,
    Coil,
    Pipe,

    // --- Fillet group (default: Fillet) ---
    Fillet,
    Chamfer,

    // --- Hole group (default: SimpleHole) ---
    SimpleHole,
    Counterbore,
    Countersink,
    ThreadedHole,

    // --- Move group (default: MoveCopy) ---
    MoveCopy,
    Align,

    // --- Mirror group (default: Mirror) ---
    Mirror,
    Pattern,

    // --- Standalone tools ---
    Parameters,

    // Keep this last for iteration/bounds checking
    _Count
};

/// Toolbar button group IDs for configuration.
/// Each group can contain multiple ModelTool items in its dropdown.
enum class ToolbarGroup {
    Sketch,
    Plane,
    Solid,
    Fillet,
    Hole,
    Move,
    Mirror,
    Params,
    _Count
};

/// Returns the default ModelTool for a toolbar group
inline ModelTool defaultToolForGroup(ToolbarGroup group) {
    switch (group) {
        case ToolbarGroup::Sketch: return ModelTool::Sketch;
        case ToolbarGroup::Plane:  return ModelTool::ConstructionPlane;
        case ToolbarGroup::Solid:  return ModelTool::Extrude;
        case ToolbarGroup::Fillet: return ModelTool::Fillet;
        case ToolbarGroup::Hole:   return ModelTool::SimpleHole;
        case ToolbarGroup::Move:   return ModelTool::MoveCopy;
        case ToolbarGroup::Mirror: return ModelTool::Mirror;
        case ToolbarGroup::Params: return ModelTool::Parameters;
        default: return ModelTool::None;
    }
}

// ============================================================================
// Future: ToolbarConfig class for user-customizable toolbar layouts
// ============================================================================
// struct ToolbarConfig {
//     struct ButtonConfig {
//         ToolbarGroup group;
//         int position;                    // Order in toolbar
//         QVector<ModelTool> dropdownItems; // Custom dropdown contents
//         ModelTool defaultTool;           // Override default
//     };
//     QVector<ButtonConfig> buttons;
//     QString name;                        // "Default", "Modeling", etc.
//
//     void loadFromSettings(QSettings& settings);
//     void saveToSettings(QSettings& settings) const;
//     static ToolbarConfig factoryDefault();
// };
// ============================================================================

class ModelToolbar : public QWidget {
    Q_OBJECT

public:
    explicit ModelToolbar(QWidget* parent = nullptr);

    /// Get the currently active tool
    ModelTool activeTool() const { return m_activeTool; }

    /// Programmatically set the active tool
    void setActiveTool(ModelTool tool);

    /// Reset all buttons to their default state (called on ESC)
    void resetAllButtons();

    // Future: void applyConfig(const ToolbarConfig& config);

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

private slots:
    void onToolClicked();
    void onSketchDropdownClicked(int index);
    void onPlaneDropdownClicked(int index);
    void onSolidDropdownClicked(int index);
    void onFilletDropdownClicked(int index);
    void onHoleDropdownClicked(int index);
    void onMoveDropdownClicked(int index);
    void onMirrorDropdownClicked(int index);

private:
    ToolbarButton* createToolButton(const QIcon& icon,
                                    const QString& text,
                                    const QString& tooltip);
    void createTools();
    void setActiveToolInternal(ModelTool tool, ToolbarButton* activeBtn);

    QHBoxLayout* m_layout = nullptr;
    ModelTool m_activeTool = ModelTool::None;

    // Toolbar buttons
    ToolbarButton* m_sketchBtn    = nullptr;  // Sketch, Sketch on Face
    ToolbarButton* m_planeBtn     = nullptr;  // Construction Plane, (Surface future)
    ToolbarButton* m_solidBtn     = nullptr;  // Extrude, Revolve, Loft, Sweep, Primitives
    ToolbarButton* m_filletBtn    = nullptr;  // Fillet, Chamfer
    ToolbarButton* m_holeBtn      = nullptr;  // Simple, Counterbore, Countersink, Threaded
    ToolbarButton* m_moveBtn      = nullptr;  // Move/Copy, Align
    ToolbarButton* m_mirrorBtn    = nullptr;  // Mirror, Pattern
    ToolbarButton* m_paramsBtn    = nullptr;  // Parameters

    // Default icons and text for all buttons (for ESC reset)
    QIcon m_defaultSketchIcon;
    QString m_defaultSketchText;
    QIcon m_defaultPlaneIcon;
    QString m_defaultPlaneText;
    QIcon m_defaultSolidIcon;
    QString m_defaultSolidText;
    QIcon m_defaultFilletIcon;
    QString m_defaultFilletText;
    QIcon m_defaultHoleIcon;
    QString m_defaultHoleText;
    QIcon m_defaultMoveIcon;
    QString m_defaultMoveText;
    QIcon m_defaultMirrorIcon;
    QString m_defaultMirrorText;

    // Last selected tool for each button group
    // All buttons are default-first: starts with default, ESC resets to default
    // This matches industry standard (SolidWorks, Fusion 360) where clicking
    // a button always does something immediately.

    ModelTool m_lastSketchTool    = ModelTool::Sketch;            // Default to Sketch
    ModelTool m_lastPlaneTool     = ModelTool::ConstructionPlane; // Default to Construction Plane
    ModelTool m_lastSolidTool     = ModelTool::Extrude;           // Default to Extrude
    ModelTool m_lastFilletTool    = ModelTool::Fillet;            // Default to Fillet
    ModelTool m_lastHoleTool      = ModelTool::SimpleHole;        // Default to Simple Hole
    ModelTool m_lastMoveTool      = ModelTool::MoveCopy;          // Default to Move/Copy
    ModelTool m_lastMirrorTool    = ModelTool::Mirror;            // Default to Mirror
};

}  // namespace hobbycad

#endif  // HOBBYCAD_MODELTOOLBAR_H
