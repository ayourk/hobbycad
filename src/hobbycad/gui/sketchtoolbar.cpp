// =====================================================================
//  HobbyCAD — src/hobbycad/gui/sketchtoolbar.cpp — Sketch mode toolbar
// =====================================================================

#include "sketchtoolbar.h"

#include <initializer_list>
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

namespace {

/// Every tool label and tooltip this toolbar shows, keyed by the tool, and
/// every dropdown variant label, keyed by the creation mode.
///
/// One table rather than the same strings written out in createTools(), in
/// each dropdown handler and again in retranslate(). The handlers previously
/// indexed a `static const char* names[]` and called tr() on the element,
/// which lupdate cannot see: it parses source, it does not run it. Those
/// strings survived only because createTools() happened to contain the same
/// literals, and would have vanished from the catalogs the moment the two
/// drifted apart (silently, because lrelease drops obsolete entries and a
/// missing translation just shows English).
///
/// The context must match what tr() looks up at runtime, which for a member
/// function is the fully-qualified class name.
struct ToolText {
    SketchTool tool{};
    const char* label = nullptr;
    const char* tip = nullptr;
};

/// CreationMode restarts at 0 for every tool (LineTwoPoint, RectCorner,
/// CircleCenterRadius, ArcThreePoint and the rest all have the value 0), so a
/// mode is only meaningful ALONGSIDE its tool. Keying this table on the mode
/// alone made every lookup return whichever tool happened to be listed first,
/// which is why the Circle submenu used to read "Two Point / Center /
/// 3-Point (Angled)" (the Line and Rectangle labels) instead of its own.
struct ModeText {
    SketchTool tool{};
    CreationMode mode{};
    const char* label = nullptr;
};


const ToolText kToolText[] = {
    {SketchTool::Line,        QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Line"),         QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Draw line (L)")},
    {SketchTool::Rectangle,   QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Rectangle"),    QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Draw rectangle (R)")},
    {SketchTool::Circle,      QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Circle"),       QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Draw circle (C)")},
    {SketchTool::Arc,         QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Arc"),          QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Draw arc (A)")},
    {SketchTool::Spline,      QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Spline"),       QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Draw a Bezier or Catmull-Rom spline")},
    {SketchTool::Polygon,     QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Polygon"),      QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Draw polygon")},
    {SketchTool::Slot,        QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Slot"),         QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Draw slot")},
    {SketchTool::Ellipse,     QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Ellipse"),      QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Draw ellipse")},
    {SketchTool::Point,       QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Point"),        QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Place point (P)")},

    {SketchTool::Dimension,   QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Dimension"),    QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Add dimension (D)")},
    {SketchTool::Constraint,  QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Constraint"),   QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Add constraint (X)")},
    {SketchTool::Text,        QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Text"),         QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Add text (T)")},

    {SketchTool::Trim,        QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Trim"),         QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Trim entity at intersections")},
    {SketchTool::Extend,      QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Extend"),       QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Extend entity to nearest intersection")},
    {SketchTool::Split,       QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Split"),        QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Split entity at intersections")},
    {SketchTool::Offset,      QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Offset"),       QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Offset geometry (O)")},
    {SketchTool::Fillet,      QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Fillet"),       QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Fillet corners (F)")},
    {SketchTool::Chamfer,     QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Chamfer"),      QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Chamfer corners")},

    {SketchTool::RectPattern, QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Rect Pattern"), QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Create rectangular pattern")},
    {SketchTool::CircPattern, QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Circ Pattern"), QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Create circular pattern")},
    {SketchTool::Project,     QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Projection"),  QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Project geometry from other sketches")},
};

const ModeText kModeText[] = {
    {SketchTool::Line, CreationMode::LineTwoPoint,         QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Two Point")},
    {SketchTool::Line, CreationMode::LineTangent,          QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Tangent")},
    {SketchTool::Line, CreationMode::LineConstruction,     QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Construction")},

    {SketchTool::Rectangle, CreationMode::RectCorner,           QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Corner to Corner")},
    {SketchTool::Rectangle, CreationMode::RectCenter,           QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Center")},
    {SketchTool::Rectangle, CreationMode::RectThreePoint,       QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "3-Point (Angled)")},
    {SketchTool::Rectangle, CreationMode::RectParallelogram,    QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Parallelogram")},

    {SketchTool::Circle, CreationMode::CircleCenterRadius,   QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Center + Radius")},
    {SketchTool::Circle, CreationMode::CircleTwoPoint,       QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "2-Point (Diameter)")},
    {SketchTool::Circle, CreationMode::CircleThreePoint,     QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "3-Point")},
    {SketchTool::Circle, CreationMode::CircleTwoTangent,     QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Tangent to 2")},
    {SketchTool::Circle, CreationMode::CircleThreeTangent,   QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Tangent to 3")},

    {SketchTool::Arc, CreationMode::ArcCenterStartEnd,    QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Center + Start + End")},
    {SketchTool::Arc, CreationMode::ArcStartEndRadius,    QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Start + End + Radius")},
    {SketchTool::Arc, CreationMode::ArcTangent,           QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Tangent")},
    {SketchTool::Arc, CreationMode::ArcThreePoint,        QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "3-Point")},

    {SketchTool::Spline, CreationMode::SplineControlPoints,  QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Cubic Bezier")},
    {SketchTool::Spline, CreationMode::SplineFitPoints,      QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Catmull-Rom")},
    {SketchTool::Spline, CreationMode::SplineRational,      QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Rational Bezier")},
    {SketchTool::Spline, CreationMode::SplineConic,
     QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Conic Arc (Rho)")},

    {SketchTool::Polygon, CreationMode::PolygonInscribed,     QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Inscribed")},
    {SketchTool::Polygon, CreationMode::PolygonCircumscribed, QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Circumscribed")},
    {SketchTool::Polygon, CreationMode::PolygonFreeform,      QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Freeform")},

    {SketchTool::Slot, CreationMode::SlotCenterToCenter,   QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Center to Center")},
    {SketchTool::Slot, CreationMode::SlotOverall,          QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Overall Length")},
    {SketchTool::Slot, CreationMode::SlotArcRadius,        QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Arc Slot (Radius)")},
    {SketchTool::Slot, CreationMode::SlotArcEnds,          QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Arc Slot (Ends)")},

    {SketchTool::Ellipse, CreationMode::EllipseCenterAxes,    QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Center + Axes")},
    {SketchTool::Ellipse, CreationMode::EllipseThreePoint,    QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "3-Point")},
    {SketchTool::Ellipse, CreationMode::EllipseArc,
     QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Elliptical Arc")},
    {SketchTool::Ellipse, CreationMode::EllipseSpanRiseArc,
     QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Span + Rise Elliptical Arc")},
    {SketchTool::Ellipse, CreationMode::EllipseCornerArc,
     QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Corner Elliptical Arc")},
    {SketchTool::Ellipse, CreationMode::EllipseEndpointsArc,
     QT_TRANSLATE_NOOP("hobbycad::SketchToolbar", "Endpoints Elliptical Arc")},
};


const ToolText* findToolText(SketchTool tool)
{
    for (const ToolText& entry : kToolText) {
        if (entry.tool == tool) {
            return &entry;
        }
    }
    return nullptr;
}

const ModeText* findModeText(SketchTool tool, CreationMode mode)
{
    for (const ModeText& entry : kModeText) {
        if (entry.tool == tool && entry.mode == mode) {
            return &entry;
        }
    }
    return nullptr;
}

}  // namespace

QString SketchToolbar::toolLabel(SketchTool tool)
{
    const ToolText* e = findToolText(tool);
    return e ? tr(e->label) : QString();
}

QString SketchToolbar::toolTip(SketchTool tool)
{
    const ToolText* e = findToolText(tool);
    return e ? tr(e->tip) : QString();
}

QString SketchToolbar::modeLabel(SketchTool tool, CreationMode mode)
{
    const ModeText* e = findModeText(tool, mode);
    return e ? tr(e->label) : QString();
}

/// The caption a group button should show, rebuilt from the identifiers
/// rather than restored from a remembered string.
QString SketchToolbar::captionText(const ToolbarCaption& caption,
                                   const QString& fallback)
{
    if (caption.fromVariant) {
        const QString label = modeLabel(caption.tool, caption.mode);
        if (!label.isEmpty()) {
            return label;
        }
    }
    if (caption.tool != SketchTool::Select) {
        const QString label = toolLabel(caption.tool);
        if (!label.isEmpty()) {
            return label;
        }
    }
    return fallback;
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
    // Note: Horizontal/Vertical modes removed - use constraints instead
    createDropdown->addButton(
        QIcon::fromTheme(QStringLiteral("draw-line"),
                         style()->standardIcon(QStyle::SP_ArrowForward)),
        tr("Line"), tr("Draw line (L)"));
    createDropdown->addVariant(tr("Two Point"), static_cast<int>(CreationMode::LineTwoPoint));
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
    createDropdown->addVariant(tr("Parallelogram"), static_cast<int>(CreationMode::RectParallelogram));

    // Circle - with variants
    createDropdown->addButton(
        QIcon::fromTheme(QStringLiteral("draw-circle"),
                         style()->standardIcon(QStyle::SP_DialogHelpButton)),
        tr("Circle"), tr("Draw circle (C)"));
    createDropdown->addVariant(tr("Center + Radius"), static_cast<int>(CreationMode::CircleCenterRadius));
    createDropdown->addVariant(tr("2-Point (Diameter)"), static_cast<int>(CreationMode::CircleTwoPoint));
    createDropdown->addVariant(tr("3-Point"), static_cast<int>(CreationMode::CircleThreePoint));
    createDropdown->addVariant(tr("Tangent to 2"), static_cast<int>(CreationMode::CircleTwoTangent));
    createDropdown->addVariant(tr("Tangent to 3"), static_cast<int>(CreationMode::CircleThreeTangent));

    // Arc - with variants
    createDropdown->addButton(
        QIcon::fromTheme(QStringLiteral("draw-arc"),
                         style()->standardIcon(QStyle::SP_BrowserReload)),
        tr("Arc"), tr("Draw arc (A)"));
    createDropdown->addVariant(tr("Center + Start + End"), static_cast<int>(CreationMode::ArcCenterStartEnd));
    createDropdown->addVariant(tr("Start + End + Radius"), static_cast<int>(CreationMode::ArcStartEndRadius));
    createDropdown->addVariant(tr("Tangent"), static_cast<int>(CreationMode::ArcTangent));
    createDropdown->addVariant(tr("3-Point"), static_cast<int>(CreationMode::ArcThreePoint));

    // Spline - with variants
    createDropdown->addButton(
        QIcon::fromTheme(QStringLiteral("draw-bezier-curves"),
                         style()->standardIcon(QStyle::SP_DesktopIcon)),
        tr("Spline"), tr("Draw a Bezier or Catmull-Rom spline"));
    createDropdown->addVariant(tr("Cubic Bezier"), static_cast<int>(CreationMode::SplineControlPoints));
    createDropdown->addVariant(tr("Catmull-Rom"), static_cast<int>(CreationMode::SplineFitPoints));
    createDropdown->addVariant(tr("Rational Bezier"), static_cast<int>(CreationMode::SplineRational));
    createDropdown->addVariant(tr("Conic Arc (Rho)"), static_cast<int>(CreationMode::SplineConic));

    // Polygon - with variants
    createDropdown->addButton(
        QIcon::fromTheme(QStringLiteral("draw-polygon"),
                         style()->standardIcon(QStyle::SP_DialogResetButton)),
        tr("Polygon"), tr("Draw polygon"));
    createDropdown->addVariant(tr("Inscribed"), static_cast<int>(CreationMode::PolygonInscribed));
    createDropdown->addVariant(tr("Circumscribed"), static_cast<int>(CreationMode::PolygonCircumscribed));
    createDropdown->addVariant(tr("Freeform"), static_cast<int>(CreationMode::PolygonFreeform));

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
    createDropdown->addVariant(tr("Elliptical Arc"), static_cast<int>(CreationMode::EllipseArc));
    createDropdown->addVariant(tr("Span + Rise Elliptical Arc"),
                               static_cast<int>(CreationMode::EllipseSpanRiseArc));
    createDropdown->addVariant(tr("Corner Elliptical Arc"),
                               static_cast<int>(CreationMode::EllipseCornerArc));
    createDropdown->addVariant(tr("Endpoints Elliptical Arc"),
                               static_cast<int>(CreationMode::EllipseEndpointsArc));

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

    // Transform the selection. These open the Transform section of the
    // properties panel rather than becoming a persistent tool, so they are
    // handled apart from the modify tools above.
    modifyDropdown->addButton(
        QIcon::fromTheme(QStringLiteral("transform-move"),
                         style()->standardIcon(QStyle::SP_ArrowUp)),
        tr("Move"), tr("Move the selected geometry"));
    modifyDropdown->addButton(
        QIcon::fromTheme(QStringLiteral("object-rotate-left"),
                         style()->standardIcon(QStyle::SP_BrowserReload)),
        tr("Rotate"), tr("Rotate the selected geometry"));
    modifyDropdown->addButton(
        QIcon::fromTheme(QStringLiteral("zoom-fit-best"),
                         style()->standardIcon(QStyle::SP_FileDialogDetailedView)),
        tr("Scale"), tr("Scale the selected geometry"));
    modifyDropdown->addButton(
        QIcon::fromTheme(QStringLiteral("object-flip-horizontal"),
                         style()->standardIcon(QStyle::SP_DialogResetButton)),
        tr("Mirror"), tr("Mirror the selected geometry"));
    modifyDropdown->addButton(
        QIcon::fromTheme(QStringLiteral("edit-copy"),
                         style()->standardIcon(QStyle::SP_DialogSaveButton)),
        tr("Copy"), tr("Copy the selected geometry"));

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
        tr("Projection"), tr("Project geometry from other sketches"));

    connect(m_patternBtn, &ToolbarButton::dropdownClicked,
            this, &SketchToolbar::onPatternDropdownClicked);

    // ===== 3D mode: an independent checkable latch, NOT part of the tool
    // radio groups (so picking a tool never clears it). Emits its own signal.
    addSeparator();
    m_btn3d = new ToolbarButton(
        QIcon::fromTheme(QStringLiteral("draw-cuboid"),
                         style()->standardIcon(QStyle::SP_FileDialogDetailedView)),
        tr("3D"), tr("Toggle 3D sketch mode"), this);
    m_btn3d->setCheckable(true);
    connect(m_btn3d, &ToolbarButton::clicked, this, [this]() {
        emit sketch3DModeToggled(m_btn3d->isChecked());
    });
    m_layout->addWidget(m_btn3d);

    // ===== Heads/tails: an independent checkable latch, like 3D mode. Draws
    // from the far side of the plane (u mirrored on screen); a view flip only.
    m_btnFlip = new ToolbarButton(
        style()->standardIcon(QStyle::SP_BrowserReload),
        tr("Flip"), tr("Draw from the far side of the plane (heads/tails)"), this);
    m_btnFlip->setCheckable(true);
    connect(m_btnFlip, &ToolbarButton::clicked, this, [this]() {
        emit sketchFlipToggled(m_btnFlip->isChecked());
    });
    m_layout->addWidget(m_btnFlip);

    // ===== Finish Sketch: a plain button (not a latch) that leaves the sketch.
    // It lives on the sketch toolbar, so it only shows while a sketch is open.
    addSeparator();
    m_btnFinish = new ToolbarButton(
        style()->standardIcon(QStyle::SP_DialogApplyButton),
        tr("Finish Sketch"), tr("Finish editing the sketch"), this);
    connect(m_btnFinish, &ToolbarButton::clicked, this, [this]() {
        emit finishSketchRequested();
    });
    m_layout->addWidget(m_btnFinish);

    // Every caption, tooltip and variant above is applied here, and only
    // here, so a new string has to be added in one place rather than two.
    retranslate();
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
    if (index >= 0 && index < static_cast<int>(sizeof(tools)/sizeof(tools[0]))) {
        SketchTool tool = tools[index];
        CreationMode mode = CreationMode::Default;

        // Save previous before updating (for potential revert)
        m_prevCreateTool = m_lastCreateTool;
        m_prevCreateMode = m_lastCreateMode;
        m_prevCaptionCreate = m_captionCreate;

        // Store as last selected create tool
        m_lastCreateTool = tool;
        m_lastCreateMode = mode;
        m_captionCreate = ToolbarCaption{tool, mode, false};

        // Update button text to show selected tool
        m_createBtn->setText(toolLabel(tool));

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
    if (index >= 0 && index < static_cast<int>(sizeof(tools)/sizeof(tools[0]))) {
        SketchTool tool = tools[index];
        CreationMode mode = static_cast<CreationMode>(variantId);

        // Save previous before updating (for potential revert)
        m_prevCreateTool = m_lastCreateTool;
        m_prevCreateMode = m_lastCreateMode;
        m_prevCaptionCreate = m_captionCreate;

        // Store as last selected create tool
        m_lastCreateTool = tool;
        m_lastCreateMode = mode;
        // m_captionCreate is updated by onCreateVariantSelected

        setActiveToolInternal(tool, mode, m_createBtn);
    }
}

void SketchToolbar::onCreateVariantSelected(int index, int variantId, const QString& variantName)
{
    Q_UNUSED(index)
    Q_UNUSED(variantId)
    // Remember which variant is showing, by mode rather than by the rendered
    // name: the name is language-dependent and retranslate() has to be able
    // to rebuild it.
    m_captionCreate = ToolbarCaption{m_lastCreateTool,
                                     static_cast<CreationMode>(variantId), true};
    m_createBtn->setText(variantName);
}

void SketchToolbar::onConstrainDropdownClicked(int index)
{
    static const SketchTool tools[] = {
        SketchTool::Dimension, SketchTool::Constraint, SketchTool::Text
    };
    if (index >= 0 && index < static_cast<int>(sizeof(tools)/sizeof(tools[0]))) {
        m_lastConstrainTool = tools[index];
        m_captionConstrain = ToolbarCaption{tools[index], CreationMode::Default, false};
        m_constrainBtn->setText(toolLabel(tools[index]));
        setActiveToolInternal(tools[index], CreationMode::Default, m_constrainBtn);
    }
}

void SketchToolbar::onModifyDropdownClicked(int index)
{
    static const SketchTool tools[] = {
        SketchTool::Trim, SketchTool::Extend, SketchTool::Split,
        SketchTool::Offset, SketchTool::Fillet, SketchTool::Chamfer
    };
    const int nTools = static_cast<int>(sizeof(tools)/sizeof(tools[0]));
    if (index >= 0 && index < nTools) {
        m_lastModifyTool = tools[index];
        m_captionModify = ToolbarCaption{tools[index], CreationMode::Default, false};
        m_modifyBtn->setText(toolLabel(tools[index]));
        setActiveToolInternal(tools[index], CreationMode::Default, m_modifyBtn);
        return;
    }
    // Transform entries: Move, Rotate, Scale, Mirror, Copy -> sketch::TransformType
    // (Move=0, Copy=1, Rotate=2, Scale=3, Mirror=4). Not persistent tools.
    static const int xf[] = {0 /*Move*/, 2 /*Rotate*/, 3 /*Scale*/, 4 /*Mirror*/, 1 /*Copy*/};
    const int xi = index - nTools;
    if (xi >= 0 && xi < static_cast<int>(sizeof(xf)/sizeof(xf[0]))) {
        emit transformRequested(xf[xi]);
    }
}

void SketchToolbar::onPatternDropdownClicked(int index)
{
    static const SketchTool tools[] = {
        SketchTool::RectPattern, SketchTool::CircPattern, SketchTool::Project
    };
    if (index >= 0 && index < static_cast<int>(sizeof(tools)/sizeof(tools[0]))) {
        m_lastPatternTool = tools[index];
        m_captionPattern = ToolbarCaption{tools[index], CreationMode::Default, false};
        m_patternBtn->setText(toolLabel(tools[index]));
        setActiveToolInternal(tools[index], CreationMode::Default, m_patternBtn);
    }
}

// ---- Retranslation --------------------------------------------------

void SketchToolbar::retranslate()
{
    // Group defaults: what a button shows before anything is picked from its
    // dropdown. These name the group rather than any one tool, so they are
    // not in kToolText.
    m_defaultCreateText    = tr("Create");
    m_defaultConstrainText = tr("Dimension");
    m_defaultModifyText    = tr("Trim");
    m_defaultPatternText   = tr("Rect\nPattern");

    // Captions, rebuilt from the tool/mode currently on each button rather
    // than restored from a remembered string.
    const struct {
        ToolbarButton* button;
        const ToolbarCaption& caption;
        const QString& fallback;
        const char* tip;
    } groups[] = {
        {m_createBtn,    m_captionCreate,    m_defaultCreateText,
         QT_TR_NOOP("Create geometry")},
        {m_constrainBtn, m_captionConstrain, m_defaultConstrainText,
         QT_TR_NOOP("Add dimension")},
        {m_modifyBtn,    m_captionModify,    m_defaultModifyText,
         QT_TR_NOOP("Trim entity at intersections")},
        {m_patternBtn,   m_captionPattern,   m_defaultPatternText,
         QT_TR_NOOP("Create rectangular pattern")},
    };
    for (const auto& g : groups) {
        if (!g.button) {
            continue;
        }
        g.button->setText(captionText(g.caption, g.fallback));
        g.button->setToolTip(g.caption.isEmpty()
                                 ? tr(g.tip)
                                 : toolTip(g.caption.tool));
    }

    // Dropdown rows and their submenu variants. Row order matches the order
    // they were added in createTools(), which is what setButtonText() indexes
    // by; variants are addressed by their CreationMode id, so their order does
    // not matter.
    struct Row {
        SketchTool tool{};
        std::initializer_list<CreationMode> variants;
    };
    const struct {
        ToolbarButton* button;
        std::initializer_list<Row> rows;
    } dropdowns[] = {
        {m_createBtn, {
            {SketchTool::Line,      {CreationMode::LineTwoPoint,
                                     CreationMode::LineTangent,
                                     CreationMode::LineConstruction}},
            {SketchTool::Rectangle, {CreationMode::RectCorner,
                                     CreationMode::RectCenter,
                                     CreationMode::RectThreePoint,
                                     CreationMode::RectParallelogram}},
            {SketchTool::Circle,    {CreationMode::CircleCenterRadius,
                                     CreationMode::CircleTwoPoint,
                                     CreationMode::CircleThreePoint,
                                     CreationMode::CircleTwoTangent,
                                     CreationMode::CircleThreeTangent}},
            {SketchTool::Arc,       {CreationMode::ArcCenterStartEnd,
                                     CreationMode::ArcStartEndRadius,
                                     CreationMode::ArcTangent,
                                     CreationMode::ArcThreePoint}},
            {SketchTool::Spline,    {CreationMode::SplineControlPoints,
                                     CreationMode::SplineFitPoints,
                                     CreationMode::SplineRational,
                                     CreationMode::SplineConic}},
            {SketchTool::Polygon,   {CreationMode::PolygonInscribed,
                                     CreationMode::PolygonCircumscribed,
                                     CreationMode::PolygonFreeform}},
            {SketchTool::Slot,      {CreationMode::SlotCenterToCenter,
                                     CreationMode::SlotOverall,
                                     CreationMode::SlotArcRadius,
                                     CreationMode::SlotArcEnds}},
            {SketchTool::Ellipse,   {CreationMode::EllipseCenterAxes,
                                     CreationMode::EllipseThreePoint,
                                     CreationMode::EllipseArc,
                                     CreationMode::EllipseSpanRiseArc,
                                     CreationMode::EllipseCornerArc,
                                     CreationMode::EllipseEndpointsArc}},
            {SketchTool::Point,     {}},
        }},
        {m_constrainBtn, {
            {SketchTool::Dimension,  {}},
            {SketchTool::Constraint, {}},
            {SketchTool::Text,       {}},
        }},
        {m_modifyBtn, {
            {SketchTool::Trim,    {}}, {SketchTool::Extend,  {}},
            {SketchTool::Split,   {}}, {SketchTool::Offset,  {}},
            {SketchTool::Fillet,  {}}, {SketchTool::Chamfer, {}},
        }},
        {m_patternBtn, {
            {SketchTool::RectPattern, {}},
            {SketchTool::CircPattern, {}},
            {SketchTool::Project,     {}},
        }},
    };
    for (const auto& d : dropdowns) {
        if (!d.button || !d.button->dropdown()) {
            continue;
        }
        ToolbarDropdown* menu = d.button->dropdown();
        int row = 0;
        for (const Row& r : d.rows) {
            menu->setButtonText(row, toolLabel(r.tool), toolTip(r.tool));
            for (CreationMode mode : r.variants) {
                menu->setVariantText(row, static_cast<int>(mode),
                                     modeLabel(r.tool, mode));
            }
            ++row;
        }
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
    m_captionCreate = ToolbarCaption{};
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

void SketchToolbar::revertCreationMode(SketchTool tool)
{
    Q_UNUSED(tool)  // Currently only used for Arc, but parameter allows future extension

    // Revert to previous tool/mode
    m_lastCreateTool = m_prevCreateTool;
    m_lastCreateMode = m_prevCreateMode;
    m_captionCreate = m_prevCaptionCreate;
    m_activeTool = m_prevCreateTool;
    m_creationMode = m_prevCreateMode;

    // Update button appearance
    if (m_prevCreateTool == SketchTool::Select || m_prevCaptionCreate.isEmpty()) {
        // No previous tool - reset to default
        m_createBtn->setIcon(m_defaultCreateIcon);
        m_createBtn->setText(m_defaultCreateText);
        m_createBtn->setChecked(false);
    } else {
        // Restore previous button text
        m_createBtn->setText(captionText(m_prevCaptionCreate,
                                         m_defaultCreateText));
    }
}

void SketchToolbar::setFlipChecked(bool on)
{
    if (m_btnFlip && m_btnFlip->isChecked() != on)
        m_btnFlip->setChecked(on);
}

void SketchToolbar::set3DChecked(bool on)
{
    if (m_btn3d && m_btn3d->isChecked() != on)
        m_btn3d->setChecked(on);   // setChecked does not fire clicked
}

}  // namespace hobbycad
