// =====================================================================
//  src/hobbycad/gui/sketchtoolbar.h — Sketch mode toolbar
// =====================================================================
//
//  Horizontal toolbar for 2D sketch operations. Shows tools for
//  creating lines, circles, rectangles, arcs, and other 2D entities.
//  Also includes constraint tools and sketch exit button.
//
//  Uses the same ToolbarButton style as the viewport toolbar for
//  consistent look (icons above labels).
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_SKETCHTOOLBAR_H
#define HOBBYCAD_SKETCHTOOLBAR_H

#include <QWidget>

class QHBoxLayout;

namespace hobbycad {

class ToolbarButton;

/// Active sketch tool
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

class SketchToolbar : public QWidget {
    Q_OBJECT

public:
    explicit SketchToolbar(QWidget* parent = nullptr);

    /// Get the currently active tool
    SketchTool activeTool() const { return m_activeTool; }

    /// Set the active tool
    void setActiveTool(SketchTool tool);

signals:
    /// Emitted when a tool is selected
    void toolSelected(SketchTool tool);

private slots:
    void onToolClicked();

private:
    void createTools();
    ToolbarButton* createToolButton(const QIcon& icon, const QString& text,
                                    const QString& tooltip);

    QHBoxLayout* m_layout = nullptr;
    SketchTool m_activeTool = SketchTool::Select;

    ToolbarButton* m_lineBtn = nullptr;
    ToolbarButton* m_rectBtn = nullptr;
    ToolbarButton* m_circleBtn = nullptr;
    ToolbarButton* m_arcBtn = nullptr;
    ToolbarButton* m_splineBtn = nullptr;
    ToolbarButton* m_polygonBtn = nullptr;
    ToolbarButton* m_slotBtn = nullptr;
    ToolbarButton* m_ellipseBtn = nullptr;
    ToolbarButton* m_pointBtn = nullptr;
    ToolbarButton* m_textBtn = nullptr;
    ToolbarButton* m_dimBtn = nullptr;
    ToolbarButton* m_constraintBtn = nullptr;
    ToolbarButton* m_trimBtn = nullptr;
    ToolbarButton* m_extendBtn = nullptr;
    ToolbarButton* m_splitBtn = nullptr;
    ToolbarButton* m_offsetBtn = nullptr;
    ToolbarButton* m_filletBtn = nullptr;
    ToolbarButton* m_chamferBtn = nullptr;
    ToolbarButton* m_rectPatternBtn = nullptr;
    ToolbarButton* m_circPatternBtn = nullptr;
    ToolbarButton* m_projectBtn = nullptr;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_SKETCHTOOLBAR_H
