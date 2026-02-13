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
}

void SketchToolbar::onToolClicked()
{
    auto* btn = qobject_cast<ToolbarButton*>(sender());
    if (!btn) return;

    SketchTool newTool = SketchTool::Line;  // Default

    if (btn == m_lineBtn)            newTool = SketchTool::Line;
    else if (btn == m_rectBtn)       newTool = SketchTool::Rectangle;
    else if (btn == m_circleBtn)     newTool = SketchTool::Circle;
    else if (btn == m_arcBtn)        newTool = SketchTool::Arc;
    else if (btn == m_splineBtn)     newTool = SketchTool::Spline;
    else if (btn == m_pointBtn)      newTool = SketchTool::Point;
    else if (btn == m_textBtn)       newTool = SketchTool::Text;
    else if (btn == m_dimBtn)        newTool = SketchTool::Dimension;
    else if (btn == m_constraintBtn) newTool = SketchTool::Constraint;

    // Update checked state - make tools mutually exclusive
    m_lineBtn->setChecked(btn == m_lineBtn);
    m_rectBtn->setChecked(btn == m_rectBtn);
    m_circleBtn->setChecked(btn == m_circleBtn);
    m_arcBtn->setChecked(btn == m_arcBtn);
    m_splineBtn->setChecked(btn == m_splineBtn);
    m_pointBtn->setChecked(btn == m_pointBtn);
    m_textBtn->setChecked(btn == m_textBtn);
    m_dimBtn->setChecked(btn == m_dimBtn);
    m_constraintBtn->setChecked(btn == m_constraintBtn);

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
    m_pointBtn->setChecked(tool == SketchTool::Point);
    m_textBtn->setChecked(tool == SketchTool::Text);
    m_dimBtn->setChecked(tool == SketchTool::Dimension);
    m_constraintBtn->setChecked(tool == SketchTool::Constraint);
}

}  // namespace hobbycad
