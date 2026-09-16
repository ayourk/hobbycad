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
#include <QIcon>

#include "../i18n/retranslatable.h"

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
    CircleTwoTangent,        // Tangent to 2 entities + radius
    CircleThreeTangent,      // Tangent to 3 entities

    // Arc modes
    ArcThreePoint = 0,    // 3-point arc (default)
    ArcCenterStartEnd,    // Center + start + end
    ArcStartEndRadius,    // Start + end + radius
    ArcTangent,           // Tangent to existing curve

    // Ellipse modes
    EllipseCenterAxes = 0,   // Center + axes (default)
    EllipseThreePoint,       // 3-point

    // Spline modes
    SplineControlPoints = 0, // Bezier with editable handles (default)
    SplineFitPoints,         // Catmull-Rom (through points, no handles)
    SplineRational,          // Rational (weighted) Bezier

    // Polygon modes
    PolygonInscribed = 0,    // Inscribed in circle (default)
    PolygonCircumscribed,    // Circumscribed around circle
    PolygonFreeform,         // Click each vertex, close by clicking start

    // Slot modes
    SlotCenterToCenter = 0,  // Center to center (default)
    SlotOverall,             // Overall length
    SlotArcRadius,           // Arc slot: Start -> Arc Center -> End (constrained to arc)
    SlotArcEnds              // Arc slot: Start -> End -> Arc Center (free placement)
};

/// What a group button is currently captioned with.
///
/// Held as identifiers rather than as the rendered string. The caption is
/// language-dependent, so a stored QString is correct only until the user
/// switches language, after which retranslate() has nothing to rebuild it
/// from, and reverting to a "previous" tool would restore the caption in the
/// old language.
struct ToolbarCaption {
    SketchTool tool = SketchTool::Select;
    CreationMode mode = CreationMode::Default;
    /// True when the caption shows the chosen dropdown variant rather than
    /// the tool itself.
    bool fromVariant = false;

    /// Nothing has been chosen yet, so the button shows its group default.
    bool isEmpty() const {
        return tool == SketchTool::Select && !fromVariant;
    }
};

class SketchToolbar : public QWidget, public Retranslatable {
    Q_OBJECT

public:
    explicit SketchToolbar(QWidget* parent = nullptr);

    /// Re-apply every caption, tooltip, dropdown row and submenu variant.
    /// Rebuilt from the tool and mode each button currently shows, not from
    /// remembered strings.
    void retranslate() override;

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

    /// Reflect the canvas's heads/tails state on the Flip latch without
    /// emitting a toggle (setChecked does not fire clicked).
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
    ToolbarButton* m_btn3d = nullptr;  ///< 2D/3D mode latch (checkable)
    ToolbarButton* m_btnFlip = nullptr;  ///< heads/tails view-flip latch (checkable)
    ToolbarButton* m_btnFinish = nullptr;  ///< Finish Sketch (plain button, sketch toolbar)

    // Track last selected tools (for re-clicking buttons)
    // All buttons are default-first: starts with default, ESC resets to default
    // This matches industry standard (SolidWorks, Fusion 360) where clicking
    // a button always does something immediately.

    /// Translated text for a tool or creation mode, from the one table that
    /// holds them. Members rather than free functions so tr() resolves in
    /// this class's context.
    QString toolLabel(SketchTool tool);
    QString toolTip(SketchTool tool);
    /// A CreationMode is only meaningful together with its tool (the enum
    /// restarts at 0 per tool), so the label lookup takes both.
    QString modeLabel(SketchTool tool, CreationMode mode);
    QString captionText(const ToolbarCaption& caption, const QString& fallback);

    /// What each group button is captioned with right now.
    ToolbarCaption m_captionCreate;
    ToolbarCaption m_captionConstrain;
    ToolbarCaption m_captionModify;
    ToolbarCaption m_captionPattern;
    /// The create button's previous caption, for the revert path.
    ToolbarCaption m_prevCaptionCreate;

    SketchTool m_lastCreateTool = SketchTool::Select;       // Dropdown-first: must choose
    CreationMode m_lastCreateMode = CreationMode::Default;
    SketchTool m_prevCreateTool = SketchTool::Select;        // Previous tool (for reverting)
    CreationMode m_prevCreateMode = CreationMode::Default;   // Previous mode (for reverting)
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
