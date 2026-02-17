// =====================================================================
//  HobbyCAD — src/hobbycad/gui/sketchtoolbar.h — Sketch mode toolbar
// =====================================================================
//
//  Horizontal toolbar for 2D sketch operations. Groups tools into
//  dropdown menus for better usability at small window sizes:
//
//    - Create: Line, Rectangle, Circle, Arc, Spline, etc.
//    - Constrain: Dimension, Constraint, Text
//    - Modify: Trim, Extend, Split, Offset, Fillet, Chamfer
//    - Pattern: Rect Pattern, Circ Pattern, Project
//
//  Each creation tool may have multiple modes (e.g., Rectangle can
//  be Corner, Center, or 3-Point). Clicking the tool directly uses
//  the default mode; clicking the arrow shows alternate modes.
//
//  Uses ToolbarButton with ToolbarDropdown for consistent look.
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_SKETCHTOOLBAR_H
#define HOBBYCAD_SKETCHTOOLBAR_H

#include <QWidget>

class QHBoxLayout;
class QToolButton;

namespace hobbycad {

class ToolbarButton;

/// Active sketch tool (base entity type)
enum class SketchTool {
    Select,
    Line,
    Rectangle,
    Circle,
    Arc,
    Spline,
    Polygon,
    Slot,
    Ellipse,
    Point,
    Text,
    Dimension,
    Constraint,
    Trim,
    Extend,
    Split,
    Offset,
    Fillet,
    Chamfer,
    RectPattern,
    CircPattern,
    Project
};

/// Creation mode variants for tools with multiple input methods
enum class CreationMode {
    Default = 0,

    // Line modes
    LineTwoPoint = 0,
    LineHorizontal,
    LineVertical,
    LineTangent,
    LineConstruction,

    // Rectangle modes
    RectCorner = 0,       // Corner to corner (default)
    RectCenter,           // Center + corner
    RectThreePoint,       // 3-point (angled)
    RectParallelogram,    // 3-point parallelogram

    // Circle modes
    CircleCenterRadius = 0,  // Center + radius (default)
    CircleTwoPoint,          // Diameter (2 points)
    CircleThreePoint,        // Through 3 points

    // Arc modes
    ArcThreePoint = 0,    // 3-point arc (default)
    ArcCenterStartEnd,    // Center + start + end
    ArcStartEndRadius,    // Start + end + radius
    ArcTangent,           // Tangent to existing curve

    // Ellipse modes
    EllipseCenterAxes = 0,   // Center + axes (default)
    EllipseThreePoint,       // 3-point

    // Spline modes
    SplineControlPoints = 0, // Control points (default)
    SplineFitPoints,         // Fit through points

    // Polygon modes
    PolygonInscribed = 0,    // Inscribed in circle (default)
    PolygonCircumscribed,    // Circumscribed around circle

    // Slot modes
    SlotCenterToCenter = 0,  // Center to center (default)
    SlotOverall,             // Overall length
    SlotArcRadius,           // Arc slot: Start -> Arc Center -> End (constrained to arc)
    SlotArcEnds              // Arc slot: Start -> End -> Arc Center (free placement)
};

class SketchToolbar : public QWidget {
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

    /// Reset Create button to default state (shows "Create" with dropdown)
    void resetCreateButton();

    /// Revert to the previous creation mode for a tool (when mode selection is rejected)
    void revertCreationMode(SketchTool tool);

signals:
    /// Emitted when a tool is selected (for basic tool changes)
    void toolChanged(SketchTool tool);

    /// Emitted when a tool with specific mode is selected
    void toolSelected(SketchTool tool, CreationMode mode);

private slots:
    void onToolClicked();
    void onCreateDropdownClicked(int index);
    void onCreateVariantClicked(int index, int variantId);
    void onCreateVariantSelected(int index, int variantId, const QString& variantName);
    void onConstrainDropdownClicked(int index);
    void onModifyDropdownClicked(int index);
    void onPatternDropdownClicked(int index);

private:
    void createTools();
    ToolbarButton* createToolButton(const QIcon& icon, const QString& text,
                                    const QString& tooltip);
    void setActiveToolInternal(SketchTool tool, CreationMode mode,
                               ToolbarButton* activeBtn);

    QHBoxLayout* m_layout = nullptr;
    SketchTool m_activeTool = SketchTool::Line;
    CreationMode m_creationMode = CreationMode::Default;

    // Main toolbar buttons (with dropdowns)
    ToolbarButton* m_createBtn = nullptr;
    ToolbarButton* m_constrainBtn = nullptr;
    ToolbarButton* m_modifyBtn = nullptr;
    ToolbarButton* m_patternBtn = nullptr;

    // Track last selected tools (for re-clicking buttons)
    // All buttons are default-first: starts with default, ESC resets to default
    // This matches industry standard (SolidWorks, Fusion 360) where clicking
    // a button always does something immediately.

    SketchTool m_lastCreateTool = SketchTool::Select;       // Dropdown-first: must choose
    CreationMode m_lastCreateMode = CreationMode::Default;
    QString m_lastCreateText;                                // Button text for current tool
    SketchTool m_prevCreateTool = SketchTool::Select;        // Previous tool (for reverting)
    CreationMode m_prevCreateMode = CreationMode::Default;   // Previous mode (for reverting)
    QString m_prevCreateText;                                // Button text for previous tool
    QIcon m_defaultCreateIcon;
    QString m_defaultCreateText;

    SketchTool m_lastConstrainTool = SketchTool::Dimension; // Default to Dimension
    QIcon m_defaultConstrainIcon;
    QString m_defaultConstrainText;

    SketchTool m_lastModifyTool = SketchTool::Trim;         // Default to Trim
    QIcon m_defaultModifyIcon;
    QString m_defaultModifyText;

    SketchTool m_lastPatternTool = SketchTool::RectPattern; // Default to Rect Pattern
    QIcon m_defaultPatternIcon;
    QString m_defaultPatternText;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_SKETCHTOOLBAR_H
