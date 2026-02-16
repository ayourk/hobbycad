// =====================================================================
//  HobbyCAD — src/hobbycad/gui/sketchtoolbar.cpp — Sketch mode toolbar
// =====================================================================

#include "sketchtoolbar.h"
#include "toolbarbutton.h"
#include "toolbardropdown.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QStyle>
#include <QToolButton>

namespace hobbycad {

// Indices for create dropdown items
enum CreateIndex {
    CreateLine = 0,
    CreateRect,
    CreateCircle,
    CreateArc,
    CreateSpline,
    CreatePolygon,
    CreateSlot,
    CreateEllipse,
    CreatePoint
};

SketchToolbar::SketchToolbar(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("SketchToolbar"));

    setAutoFillBackground(true);

    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(4, 2, 4, 2);
    m_layout->setSpacing(4);

    createTools();

    // Add stretch at the end to left-align buttons
    m_layout->addStretch();
}

ToolbarButton* SketchToolbar::createToolButton(const QIcon& icon,
                                                const QString& text,
                                                const QString& tooltip)
{
    auto* btn = new ToolbarButton(icon, text, tooltip, this);
    btn->setCheckable(true);
    connect(btn, &ToolbarButton::clicked, this, &SketchToolbar::onToolClicked);
    m_layout->addWidget(btn);
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
        m_layout->addWidget(sep);
    };

    // ===== CREATE button with dropdown for geometry creation =====
    // Dropdown-first: user must choose from dropdown before button works
    m_defaultCreateIcon = QIcon::fromTheme(QStringLiteral("draw-freehand"),
                                           style()->standardIcon(QStyle::SP_FileDialogNewFolder));
    m_defaultCreateText = tr("Create");
    m_createBtn = createToolButton(
        m_defaultCreateIcon,
        m_defaultCreateText,
        tr("Create geometry"));

    // Populate dropdown with create tools (list style with submenus)
    auto* createDropdown = m_createBtn->dropdown();
    createDropdown->setIconSize(16);

    // Line - with variants
    createDropdown->addButton(
        QIcon::fromTheme(QStringLiteral("draw-line"),
                         style()->standardIcon(QStyle::SP_ArrowForward)),
        tr("Line"), tr("Draw line (L)"));
    createDropdown->addVariant(tr("Two Point"), static_cast<int>(CreationMode::LineTwoPoint));
    createDropdown->addVariant(tr("Horizontal"), static_cast<int>(CreationMode::LineHorizontal));
    createDropdown->addVariant(tr("Vertical"), static_cast<int>(CreationMode::LineVertical));
    createDropdown->addVariant(tr("Tangent"), static_cast<int>(CreationMode::LineTangent));
    createDropdown->addVariant(tr("Construction"), static_cast<int>(CreationMode::LineConstruction));

    // Rectangle - with variants
    createDropdown->addButton(
        QIcon::fromTheme(QStringLiteral("draw-rectangle"),
                         style()->standardIcon(QStyle::SP_DialogApplyButton)),
        tr("Rectangle"), tr("Draw rectangle (R)"));
    createDropdown->addVariant(tr("Corner to Corner"), static_cast<int>(CreationMode::RectCorner));
    createDropdown->addVariant(tr("Center"), static_cast<int>(CreationMode::RectCenter));
    createDropdown->addVariant(tr("3-Point (Angled)"), static_cast<int>(CreationMode::RectThreePoint));

    // Circle - with variants
    createDropdown->addButton(
        QIcon::fromTheme(QStringLiteral("draw-circle"),
                         style()->standardIcon(QStyle::SP_DialogHelpButton)),
        tr("Circle"), tr("Draw circle (C)"));
    createDropdown->addVariant(tr("Center + Radius"), static_cast<int>(CreationMode::CircleCenterRadius));
    createDropdown->addVariant(tr("2-Point (Diameter)"), static_cast<int>(CreationMode::CircleTwoPoint));
    createDropdown->addVariant(tr("3-Point"), static_cast<int>(CreationMode::CircleThreePoint));

    // Arc - with variants
    createDropdown->addButton(
        QIcon::fromTheme(QStringLiteral("draw-arc"),
                         style()->standardIcon(QStyle::SP_BrowserReload)),
        tr("Arc"), tr("Draw arc (A)"));
    createDropdown->addVariant(tr("3-Point"), static_cast<int>(CreationMode::ArcThreePoint));
    createDropdown->addVariant(tr("Center + Start + End"), static_cast<int>(CreationMode::ArcCenterStartEnd));
    createDropdown->addVariant(tr("Start + End + Radius"), static_cast<int>(CreationMode::ArcStartEndRadius));
    createDropdown->addVariant(tr("Tangent"), static_cast<int>(CreationMode::ArcTangent));

    // Spline - with variants
    createDropdown->addButton(
        QIcon::fromTheme(QStringLiteral("draw-bezier-curves"),
                         style()->standardIcon(QStyle::SP_DesktopIcon)),
        tr("Spline"), tr("Draw spline curve"));
    createDropdown->addVariant(tr("Control Points"), static_cast<int>(CreationMode::SplineControlPoints));
    createDropdown->addVariant(tr("Fit Points"), static_cast<int>(CreationMode::SplineFitPoints));

    // Polygon - with variants
    createDropdown->addButton(
        QIcon::fromTheme(QStringLiteral("draw-polygon"),
                         style()->standardIcon(QStyle::SP_DialogResetButton)),
        tr("Polygon"), tr("Draw polygon"));
    createDropdown->addVariant(tr("Inscribed"), static_cast<int>(CreationMode::PolygonInscribed));
    createDropdown->addVariant(tr("Circumscribed"), static_cast<int>(CreationMode::PolygonCircumscribed));

    // Slot - with variants
    createDropdown->addButton(
        QIcon::fromTheme(QStringLiteral("draw-rectangle"),
                         style()->standardIcon(QStyle::SP_BrowserStop)),
        tr("Slot"), tr("Draw slot"));
    createDropdown->addVariant(tr("Center to Center"), static_cast<int>(CreationMode::SlotCenterToCenter));
    createDropdown->addVariant(tr("Overall Length"), static_cast<int>(CreationMode::SlotOverall));
    createDropdown->addVariant(tr("Arc Slot (Radius)"), static_cast<int>(CreationMode::SlotArcRadius));
    createDropdown->addVariant(tr("Arc Slot (Ends)"), static_cast<int>(CreationMode::SlotArcEnds));

    // Ellipse - with variants
    createDropdown->addButton(
        QIcon::fromTheme(QStringLiteral("draw-ellipse"),
                         style()->standardIcon(QStyle::SP_MessageBoxInformation)),
        tr("Ellipse"), tr("Draw ellipse"));
    createDropdown->addVariant(tr("Center + Axes"), static_cast<int>(CreationMode::EllipseCenterAxes));
    createDropdown->addVariant(tr("3-Point"), static_cast<int>(CreationMode::EllipseThreePoint));

    // Point - no variants
    createDropdown->addButton(
        QIcon::fromTheme(QStringLiteral("draw-circle"),
                         style()->standardIcon(QStyle::SP_DialogCancelButton)),
        tr("Point"), tr("Place point (P)"));

    // Connect dropdown signals
    connect(m_createBtn, &ToolbarButton::dropdownClicked,
            this, &SketchToolbar::onCreateDropdownClicked);
    connect(createDropdown, &ToolbarDropdown::variantClicked,
            this, &SketchToolbar::onCreateVariantClicked);
    connect(createDropdown, &ToolbarDropdown::variantSelected,
            this, &SketchToolbar::onCreateVariantSelected);

    addSeparator();

    // ===== CONSTRAIN button with dropdown =====
    // Default to Dimension (most common constraint operation)
    m_defaultConstrainIcon = QIcon::fromTheme(QStringLiteral("measure"),
                                              style()->standardIcon(QStyle::SP_FileDialogInfoView));
    m_defaultConstrainText = tr("Dimension");
    m_constrainBtn = createToolButton(
        m_defaultConstrainIcon,
        m_defaultConstrainText,
        tr("Add dimension"));

    auto* constrainDropdown = m_constrainBtn->dropdown();
    constrainDropdown->setIconSize(16);

    constrainDropdown->addButton(
        QIcon::fromTheme(QStringLiteral("measure"),
                         style()->standardIcon(QStyle::SP_FileDialogInfoView)),
        tr("Dimension"), tr("Add dimension (D)"));

    constrainDropdown->addButton(
        QIcon::fromTheme(QStringLiteral("draw-connector"),
                         style()->standardIcon(QStyle::SP_DialogOkButton)),
        tr("Constraint"), tr("Add constraint (X)"));

    constrainDropdown->addButton(
        QIcon::fromTheme(QStringLiteral("draw-text"),
                         style()->standardIcon(QStyle::SP_FileDialogDetailedView)),
        tr("Text"), tr("Add text (T)"));

    connect(m_constrainBtn, &ToolbarButton::dropdownClicked,
            this, &SketchToolbar::onConstrainDropdownClicked);

    addSeparator();

    // ===== MODIFY button with dropdown =====
    // Default to Trim (most common modify operation)
    m_defaultModifyIcon = QIcon::fromTheme(QStringLiteral("edit-cut"),
                                           style()->standardIcon(QStyle::SP_DialogDiscardButton));
    m_defaultModifyText = tr("Trim");
    m_modifyBtn = createToolButton(
        m_defaultModifyIcon,
        m_defaultModifyText,
        tr("Trim entity at intersections"));

    auto* modifyDropdown = m_modifyBtn->dropdown();
    modifyDropdown->setIconSize(16);

    modifyDropdown->addButton(
        QIcon::fromTheme(QStringLiteral("edit-cut"),
                         style()->standardIcon(QStyle::SP_DialogDiscardButton)),
        tr("Trim"), tr("Trim entity at intersections"));

    modifyDropdown->addButton(
        QIcon::fromTheme(QStringLiteral("format-indent-more"),
                         style()->standardIcon(QStyle::SP_ArrowRight)),
        tr("Extend"), tr("Extend entity to nearest intersection"));

    modifyDropdown->addButton(
        QIcon::fromTheme(QStringLiteral("view-split-left-right"),
                         style()->standardIcon(QStyle::SP_DialogNoButton)),
        tr("Split"), tr("Split entity at intersections"));

    modifyDropdown->addButton(
        QIcon::fromTheme(QStringLiteral("object-order-raise"),
                         style()->standardIcon(QStyle::SP_FileDialogContentsView)),
        tr("Offset"), tr("Offset geometry (O)"));

    modifyDropdown->addButton(
        QIcon::fromTheme(QStringLiteral("draw-bezier-curves"),
                         style()->standardIcon(QStyle::SP_DialogApplyButton)),
        tr("Fillet"), tr("Fillet corners (F)"));

    modifyDropdown->addButton(
        QIcon::fromTheme(QStringLiteral("draw-polygon"),
                         style()->standardIcon(QStyle::SP_DialogDiscardButton)),
        tr("Chamfer"), tr("Chamfer corners"));

    connect(m_modifyBtn, &ToolbarButton::dropdownClicked,
            this, &SketchToolbar::onModifyDropdownClicked);

    addSeparator();

    // ===== PATTERN button with dropdown =====
    // Default to Rect Pattern (most common pattern type)
    m_defaultPatternIcon = QIcon::fromTheme(QStringLiteral("view-grid"),
                                            style()->standardIcon(QStyle::SP_FileDialogListView));
    m_defaultPatternText = tr("Rect\nPattern");
    m_patternBtn = createToolButton(
        m_defaultPatternIcon,
        m_defaultPatternText,
        tr("Create rectangular pattern"));

    auto* patternDropdown = m_patternBtn->dropdown();
    patternDropdown->setIconSize(16);

    patternDropdown->addButton(
        QIcon::fromTheme(QStringLiteral("view-grid"),
                         style()->standardIcon(QStyle::SP_FileDialogListView)),
        tr("Rect Pattern"), tr("Create rectangular pattern"));

    patternDropdown->addButton(
        QIcon::fromTheme(QStringLiteral("view-refresh"),
                         style()->standardIcon(QStyle::SP_BrowserReload)),
        tr("Circ Pattern"), tr("Create circular pattern"));

    patternDropdown->addButton(
        QIcon::fromTheme(QStringLiteral("transform-move"),
                         style()->standardIcon(QStyle::SP_ArrowDown)),
        tr("Project"), tr("Project geometry from other sketches"));

    connect(m_patternBtn, &ToolbarButton::dropdownClicked,
            this, &SketchToolbar::onPatternDropdownClicked);
}

void SketchToolbar::onToolClicked()
{
    auto* btn = qobject_cast<ToolbarButton*>(sender());
    if (!btn) return;

    // Create button is dropdown-first: must choose before button works
    if (btn == m_createBtn) {
        if (m_lastCreateTool == SketchTool::Select) {
            m_createBtn->showDropdown();
            return;
        }
        setActiveToolInternal(m_lastCreateTool, m_lastCreateMode, btn);
    }
    // Other buttons are default-first: clicking activates the last selected tool
    else if (btn == m_constrainBtn) {
        setActiveToolInternal(m_lastConstrainTool, CreationMode::Default, btn);
    }
    else if (btn == m_modifyBtn) {
        setActiveToolInternal(m_lastModifyTool, CreationMode::Default, btn);
    }
    else if (btn == m_patternBtn) {
        setActiveToolInternal(m_lastPatternTool, CreationMode::Default, btn);
    }
}

void SketchToolbar::onCreateDropdownClicked(int index)
{
    static const SketchTool tools[] = {
        SketchTool::Line, SketchTool::Rectangle, SketchTool::Circle,
        SketchTool::Arc, SketchTool::Spline, SketchTool::Polygon,
        SketchTool::Slot, SketchTool::Ellipse, SketchTool::Point
    };
    static const char* names[] = {
        "Line", "Rectangle", "Circle", "Arc", "Spline", "Polygon",
        "Slot", "Ellipse", "Point"
    };
    if (index >= 0 && index < static_cast<int>(sizeof(tools)/sizeof(tools[0]))) {
        SketchTool tool = tools[index];
        CreationMode mode = CreationMode::Default;

        // Store as last selected create tool
        m_lastCreateTool = tool;
        m_lastCreateMode = mode;

        // Update button text to show selected tool
        m_createBtn->setText(tr(names[index]));

        setActiveToolInternal(tool, mode, m_createBtn);
    }
}

void SketchToolbar::onCreateVariantClicked(int index, int variantId)
{
    // This is called for backward compatibility but we use variantSelected now
    // which includes the variant name
    static const SketchTool tools[] = {
        SketchTool::Line, SketchTool::Rectangle, SketchTool::Circle,
        SketchTool::Arc, SketchTool::Spline, SketchTool::Polygon,
        SketchTool::Slot, SketchTool::Ellipse, SketchTool::Point
    };
    static const char* names[] = {
        "Line", "Rectangle", "Circle", "Arc", "Spline", "Polygon",
        "Slot", "Ellipse", "Point"
    };
    if (index >= 0 && index < static_cast<int>(sizeof(tools)/sizeof(tools[0]))) {
        SketchTool tool = tools[index];
        CreationMode mode = static_cast<CreationMode>(variantId);

        // Store as last selected create tool
        m_lastCreateTool = tool;
        m_lastCreateMode = mode;

        // Button text will be updated by onCreateVariantSelected

        setActiveToolInternal(tool, mode, m_createBtn);
    }
}

void SketchToolbar::onCreateVariantSelected(int index, int variantId, const QString& variantName)
{
    Q_UNUSED(index)
    Q_UNUSED(variantId)
    // Update button text to show the specific variant name
    m_createBtn->setText(variantName);
}

void SketchToolbar::onConstrainDropdownClicked(int index)
{
    static const SketchTool tools[] = {
        SketchTool::Dimension, SketchTool::Constraint, SketchTool::Text
    };
    static const char* names[] = {
        "Dimension", "Constraint", "Text"
    };
    if (index >= 0 && index < static_cast<int>(sizeof(tools)/sizeof(tools[0]))) {
        m_lastConstrainTool = tools[index];
        m_constrainBtn->setText(tr(names[index]));
        setActiveToolInternal(tools[index], CreationMode::Default, m_constrainBtn);
    }
}

void SketchToolbar::onModifyDropdownClicked(int index)
{
    static const SketchTool tools[] = {
        SketchTool::Trim, SketchTool::Extend, SketchTool::Split,
        SketchTool::Offset, SketchTool::Fillet, SketchTool::Chamfer
    };
    static const char* names[] = {
        "Trim", "Extend", "Split", "Offset", "Fillet", "Chamfer"
    };
    if (index >= 0 && index < static_cast<int>(sizeof(tools)/sizeof(tools[0]))) {
        m_lastModifyTool = tools[index];
        m_modifyBtn->setText(tr(names[index]));
        setActiveToolInternal(tools[index], CreationMode::Default, m_modifyBtn);
    }
}

void SketchToolbar::onPatternDropdownClicked(int index)
{
    static const SketchTool tools[] = {
        SketchTool::RectPattern, SketchTool::CircPattern, SketchTool::Project
    };
    static const char* names[] = {
        "Rect Pattern", "Circ Pattern", "Project"
    };
    if (index >= 0 && index < static_cast<int>(sizeof(tools)/sizeof(tools[0]))) {
        m_lastPatternTool = tools[index];
        m_patternBtn->setText(tr(names[index]));
        setActiveToolInternal(tools[index], CreationMode::Default, m_patternBtn);
    }
}

void SketchToolbar::setActiveToolInternal(SketchTool tool, CreationMode mode,
                                           ToolbarButton* activeBtn)
{
    // If clicking the same tool AND same mode, deselect and switch to Select mode
    // But if mode is different (e.g., switching from Slot to Arc Slot), keep the tool active
    bool sameToolAndMode = (tool == m_activeTool && mode == m_creationMode);
    SketchTool newTool = sameToolAndMode ? SketchTool::Select : tool;
    CreationMode newMode = (newTool == SketchTool::Select) ? CreationMode::Default : mode;

    // Update checked states - only one group can be active
    m_createBtn->setChecked(activeBtn == m_createBtn && newTool != SketchTool::Select);
    m_constrainBtn->setChecked(activeBtn == m_constrainBtn && newTool != SketchTool::Select);
    m_modifyBtn->setChecked(activeBtn == m_modifyBtn && newTool != SketchTool::Select);
    m_patternBtn->setChecked(activeBtn == m_patternBtn && newTool != SketchTool::Select);

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

    // Determine which button group this tool belongs to
    bool isCreate = (tool == SketchTool::Line || tool == SketchTool::Rectangle ||
                     tool == SketchTool::Circle || tool == SketchTool::Arc ||
                     tool == SketchTool::Spline || tool == SketchTool::Polygon ||
                     tool == SketchTool::Slot || tool == SketchTool::Ellipse ||
                     tool == SketchTool::Point);
    bool isConstrain = (tool == SketchTool::Dimension || tool == SketchTool::Constraint ||
                        tool == SketchTool::Text);
    bool isModify = (tool == SketchTool::Trim || tool == SketchTool::Extend ||
                     tool == SketchTool::Split || tool == SketchTool::Offset ||
                     tool == SketchTool::Fillet || tool == SketchTool::Chamfer);
    bool isPattern = (tool == SketchTool::RectPattern || tool == SketchTool::CircPattern ||
                      tool == SketchTool::Project);

    m_createBtn->setChecked(isCreate);
    m_constrainBtn->setChecked(isConstrain);
    m_modifyBtn->setChecked(isModify);
    m_patternBtn->setChecked(isPattern);
}

void SketchToolbar::resetCreateButton()
{
    // Reset Create button to dropdown-first (must choose)
    m_lastCreateTool = SketchTool::Select;
    m_lastCreateMode = CreationMode::Default;
    m_createBtn->setIcon(m_defaultCreateIcon);
    m_createBtn->setText(m_defaultCreateText);

    // Reset dropdown item variant selections (e.g., "Arc Slot" back to "Slot")
    m_createBtn->dropdown()->resetAllItems();

    // Reset other buttons to their default tools
    m_lastConstrainTool = SketchTool::Dimension;
    m_constrainBtn->setIcon(m_defaultConstrainIcon);
    m_constrainBtn->setText(m_defaultConstrainText);

    m_lastModifyTool = SketchTool::Trim;
    m_modifyBtn->setIcon(m_defaultModifyIcon);
    m_modifyBtn->setText(m_defaultModifyText);

    m_lastPatternTool = SketchTool::RectPattern;
    m_patternBtn->setIcon(m_defaultPatternIcon);
    m_patternBtn->setText(m_defaultPatternText);
}

}  // namespace hobbycad
