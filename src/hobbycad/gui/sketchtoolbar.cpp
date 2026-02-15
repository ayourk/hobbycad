// =====================================================================
//  src/hobbycad/gui/sketchtoolbar.cpp — Sketch mode toolbar
// =====================================================================

#include "sketchtoolbar.h"
#include "toolbarbutton.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QStyle>

namespace hobbycad {

SketchToolbar::SketchToolbar(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("SketchToolbar"));

    setAutoFillBackground(true);

    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(4, 2, 4, 2);
    m_layout->setSpacing(4);

    // Add stretch at the end by default to left-align buttons
    m_layout->addStretch();

    createTools();
}

ToolbarButton* SketchToolbar::createToolButton(const QIcon& icon,
                                                const QString& text,
                                                const QString& tooltip)
{
    auto* btn = new ToolbarButton(icon, text, tooltip, this);
    btn->setCheckable(true);
    connect(btn, &ToolbarButton::clicked, this, &SketchToolbar::onToolClicked);

    // Insert before the final stretch
    int idx = m_layout->count() - 1;
    if (idx < 0) idx = 0;
    m_layout->insertWidget(idx, btn);

    return btn;
}

void SketchToolbar::createTools()
{
    // Helper to add separator
    auto addSeparator = [this]() {
        auto* sep = new QFrame(this);
        sep->setFrameShape(QFrame::VLine);
        sep->setFrameShadow(QFrame::Sunken);
        sep->setFixedWidth(2);
        int idx = m_layout->count() - 1;
        if (idx < 0) idx = 0;
        m_layout->insertWidget(idx, sep);
    };

    // Line tool
    m_lineBtn = createToolButton(
        QIcon::fromTheme(QStringLiteral("draw-line"),
                         style()->standardIcon(QStyle::SP_ArrowForward)),
        tr("Line"),
        tr("Draw line (L)"));
    m_lineBtn->setChecked(true);  // Default tool

    // Rectangle tool
    m_rectBtn = createToolButton(
        QIcon::fromTheme(QStringLiteral("draw-rectangle"),
                         style()->standardIcon(QStyle::SP_DialogApplyButton)),
        tr("Rectangle"),
        tr("Draw rectangle (R)"));

    // Circle tool
    m_circleBtn = createToolButton(
        QIcon::fromTheme(QStringLiteral("draw-circle"),
                         style()->standardIcon(QStyle::SP_DialogHelpButton)),
        tr("Circle"),
        tr("Draw circle (C)"));

    // Arc tool
    m_arcBtn = createToolButton(
        QIcon::fromTheme(QStringLiteral("draw-arc"),
                         style()->standardIcon(QStyle::SP_BrowserReload)),
        tr("Arc"),
        tr("Draw arc (A)"));

    // Spline tool
    m_splineBtn = createToolButton(
        QIcon::fromTheme(QStringLiteral("draw-bezier-curves"),
                         style()->standardIcon(QStyle::SP_DesktopIcon)),
        tr("Spline"),
        tr("Draw spline curve"));

    // Polygon tool
    m_polygonBtn = createToolButton(
        QIcon::fromTheme(QStringLiteral("draw-polygon"),
                         style()->standardIcon(QStyle::SP_DialogResetButton)),
        tr("Polygon"),
        tr("Draw polygon"));

    // Slot tool
    m_slotBtn = createToolButton(
        QIcon::fromTheme(QStringLiteral("draw-rectangle"),
                         style()->standardIcon(QStyle::SP_BrowserStop)),
        tr("Slot"),
        tr("Draw slot"));

    // Ellipse tool
    m_ellipseBtn = createToolButton(
        QIcon::fromTheme(QStringLiteral("draw-ellipse"),
                         style()->standardIcon(QStyle::SP_MessageBoxInformation)),
        tr("Ellipse"),
        tr("Draw ellipse"));

    // Point tool
    m_pointBtn = createToolButton(
        QIcon::fromTheme(QStringLiteral("draw-circle"),
                         style()->standardIcon(QStyle::SP_DialogCancelButton)),
        tr("Point"),
        tr("Place point (P)"));

    addSeparator();

    // Dimension tool
    m_dimBtn = createToolButton(
        QIcon::fromTheme(QStringLiteral("measure"),
                         style()->standardIcon(QStyle::SP_FileDialogInfoView)),
        tr("Dimension"),
        tr("Add dimension (D)"));

    // Constraint tool
    m_constraintBtn = createToolButton(
        QIcon::fromTheme(QStringLiteral("draw-connector"),
                         style()->standardIcon(QStyle::SP_DialogOkButton)),
        tr("Constraint"),
        tr("Add constraint (X)"));

    // Text tool
    m_textBtn = createToolButton(
        QIcon::fromTheme(QStringLiteral("draw-text"),
                         style()->standardIcon(QStyle::SP_FileDialogDetailedView)),
        tr("Text"),
        tr("Add text (T)"));

    addSeparator();

    // Trim tool
    m_trimBtn = createToolButton(
        QIcon::fromTheme(QStringLiteral("edit-cut"),
                         style()->standardIcon(QStyle::SP_DialogDiscardButton)),
        tr("Trim"),
        tr("Trim entity at intersections"));

    // Extend tool
    m_extendBtn = createToolButton(
        QIcon::fromTheme(QStringLiteral("format-indent-more"),
                         style()->standardIcon(QStyle::SP_ArrowRight)),
        tr("Extend"),
        tr("Extend entity to nearest intersection"));

    // Split tool
    m_splitBtn = createToolButton(
        QIcon::fromTheme(QStringLiteral("view-split-left-right"),
                         style()->standardIcon(QStyle::SP_DialogNoButton)),
        tr("Split"),
        tr("Split entity at intersections"));

    addSeparator();

    // Offset tool
    m_offsetBtn = createToolButton(
        QIcon::fromTheme(QStringLiteral("object-order-raise"),
                         style()->standardIcon(QStyle::SP_FileDialogContentsView)),
        tr("Offset"),
        tr("Offset geometry (O)"));

    // Fillet tool
    m_filletBtn = createToolButton(
        QIcon::fromTheme(QStringLiteral("draw-bezier-curves"),
                         style()->standardIcon(QStyle::SP_DialogApplyButton)),
        tr("Fillet"),
        tr("Fillet corners (F)"));

    // Chamfer tool
    m_chamferBtn = createToolButton(
        QIcon::fromTheme(QStringLiteral("draw-polygon"),
                         style()->standardIcon(QStyle::SP_DialogDiscardButton)),
        tr("Chamfer"),
        tr("Chamfer corners"));

    addSeparator();

    // Rectangular Pattern tool
    m_rectPatternBtn = createToolButton(
        QIcon::fromTheme(QStringLiteral("view-grid"),
                         style()->standardIcon(QStyle::SP_FileDialogListView)),
        tr("Rect Pattern"),
        tr("Create rectangular pattern"));

    // Circular Pattern tool
    m_circPatternBtn = createToolButton(
        QIcon::fromTheme(QStringLiteral("view-refresh"),
                         style()->standardIcon(QStyle::SP_BrowserReload)),
        tr("Circ Pattern"),
        tr("Create circular pattern"));

    addSeparator();

    // Project tool
    m_projectBtn = createToolButton(
        QIcon::fromTheme(QStringLiteral("transform-move"),
                         style()->standardIcon(QStyle::SP_ArrowDown)),
        tr("Project"),
        tr("Project geometry from other sketches"));
}

void SketchToolbar::onToolClicked()
{
    auto* btn = qobject_cast<ToolbarButton*>(sender());
    if (!btn) return;

    SketchTool clickedTool = SketchTool::Line;  // Default

    if (btn == m_lineBtn)            clickedTool = SketchTool::Line;
    else if (btn == m_rectBtn)       clickedTool = SketchTool::Rectangle;
    else if (btn == m_circleBtn)     clickedTool = SketchTool::Circle;
    else if (btn == m_arcBtn)        clickedTool = SketchTool::Arc;
    else if (btn == m_splineBtn)     clickedTool = SketchTool::Spline;
    else if (btn == m_polygonBtn)    clickedTool = SketchTool::Polygon;
    else if (btn == m_slotBtn)       clickedTool = SketchTool::Slot;
    else if (btn == m_ellipseBtn)    clickedTool = SketchTool::Ellipse;
    else if (btn == m_pointBtn)      clickedTool = SketchTool::Point;
    else if (btn == m_textBtn)       clickedTool = SketchTool::Text;
    else if (btn == m_dimBtn)        clickedTool = SketchTool::Dimension;
    else if (btn == m_constraintBtn) clickedTool = SketchTool::Constraint;
    else if (btn == m_trimBtn)       clickedTool = SketchTool::Trim;
    else if (btn == m_extendBtn)     clickedTool = SketchTool::Extend;
    else if (btn == m_splitBtn)      clickedTool = SketchTool::Split;
    else if (btn == m_offsetBtn)     clickedTool = SketchTool::Offset;
    else if (btn == m_filletBtn)     clickedTool = SketchTool::Fillet;
    else if (btn == m_chamferBtn)    clickedTool = SketchTool::Chamfer;
    else if (btn == m_rectPatternBtn) clickedTool = SketchTool::RectPattern;
    else if (btn == m_circPatternBtn) clickedTool = SketchTool::CircPattern;
    else if (btn == m_projectBtn)    clickedTool = SketchTool::Project;

    // If clicking the same tool, deselect it and switch to Select mode
    SketchTool newTool = (clickedTool == m_activeTool) ? SketchTool::Select : clickedTool;

    // Update checked state - all unchecked for Select mode, otherwise one checked
    m_lineBtn->setChecked(newTool == SketchTool::Line);
    m_rectBtn->setChecked(newTool == SketchTool::Rectangle);
    m_circleBtn->setChecked(newTool == SketchTool::Circle);
    m_arcBtn->setChecked(newTool == SketchTool::Arc);
    m_splineBtn->setChecked(newTool == SketchTool::Spline);
    m_polygonBtn->setChecked(newTool == SketchTool::Polygon);
    m_slotBtn->setChecked(newTool == SketchTool::Slot);
    m_ellipseBtn->setChecked(newTool == SketchTool::Ellipse);
    m_pointBtn->setChecked(newTool == SketchTool::Point);
    m_textBtn->setChecked(newTool == SketchTool::Text);
    m_dimBtn->setChecked(newTool == SketchTool::Dimension);
    m_constraintBtn->setChecked(newTool == SketchTool::Constraint);
    m_trimBtn->setChecked(newTool == SketchTool::Trim);
    m_extendBtn->setChecked(newTool == SketchTool::Extend);
    m_splitBtn->setChecked(newTool == SketchTool::Split);
    m_offsetBtn->setChecked(newTool == SketchTool::Offset);
    m_filletBtn->setChecked(newTool == SketchTool::Fillet);
    m_chamferBtn->setChecked(newTool == SketchTool::Chamfer);
    m_rectPatternBtn->setChecked(newTool == SketchTool::RectPattern);
    m_circPatternBtn->setChecked(newTool == SketchTool::CircPattern);
    m_projectBtn->setChecked(newTool == SketchTool::Project);

    if (newTool != m_activeTool) {
        m_activeTool = newTool;
        emit toolSelected(m_activeTool);
    }
}

void SketchToolbar::setActiveTool(SketchTool tool)
{
    m_activeTool = tool;

    // Update button states
    m_lineBtn->setChecked(tool == SketchTool::Line);
    m_rectBtn->setChecked(tool == SketchTool::Rectangle);
    m_circleBtn->setChecked(tool == SketchTool::Circle);
    m_arcBtn->setChecked(tool == SketchTool::Arc);
    m_splineBtn->setChecked(tool == SketchTool::Spline);
    m_polygonBtn->setChecked(tool == SketchTool::Polygon);
    m_slotBtn->setChecked(tool == SketchTool::Slot);
    m_ellipseBtn->setChecked(tool == SketchTool::Ellipse);
    m_pointBtn->setChecked(tool == SketchTool::Point);
    m_textBtn->setChecked(tool == SketchTool::Text);
    m_dimBtn->setChecked(tool == SketchTool::Dimension);
    m_constraintBtn->setChecked(tool == SketchTool::Constraint);
    m_trimBtn->setChecked(tool == SketchTool::Trim);
    m_extendBtn->setChecked(tool == SketchTool::Extend);
    m_splitBtn->setChecked(tool == SketchTool::Split);
    m_offsetBtn->setChecked(tool == SketchTool::Offset);
    m_filletBtn->setChecked(tool == SketchTool::Fillet);
    m_chamferBtn->setChecked(tool == SketchTool::Chamfer);
    m_rectPatternBtn->setChecked(tool == SketchTool::RectPattern);
    m_circPatternBtn->setChecked(tool == SketchTool::CircPattern);
    m_projectBtn->setChecked(tool == SketchTool::Project);
}

}  // namespace hobbycad
