// =====================================================================
//  HobbyCAD — src/hobbycad/gui/modeltoolbar.cpp — 3D Model mode toolbar
// =====================================================================

#include "modeltoolbar.h"
#include "toolbarbutton.h"
#include "toolbardropdown.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QStyle>

namespace hobbycad {

ModelToolbar::ModelToolbar(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("ModelToolbar"));
    setAutoFillBackground(true);

    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(4, 2, 4, 2);
    m_layout->setSpacing(4);

    createTools();

    // Add stretch at the end to left-align buttons
    m_layout->addStretch();
}

ToolbarButton* ModelToolbar::createToolButton(const QIcon& icon,
                                               const QString& text,
                                               const QString& tooltip)
{
    auto* btn = new ToolbarButton(icon, text, tooltip, this);
    btn->setCheckable(true);
    connect(btn, &ToolbarButton::clicked, this, &ModelToolbar::onToolClicked);
    m_layout->addWidget(btn);
    return btn;
}

namespace {

/// Every label and tooltip this toolbar shows for a tool, keyed by the tool.
///
/// One table rather than the same strings spelled out in createTools(), in
/// each dropdown handler, and again in retranslate(). All three need the same
/// answer: the handlers set a button caption when a tool is picked, and
/// retranslate() has to re-derive whichever caption is currently showing.
/// Separate copies drift, and the copy that drifts is the one only seen after
/// a language switch.
///
/// QT_TRANSLATE_NOOP marks the strings for lupdate, which parses source and
/// does not run it: tr(table[i].label) on its own extracts nothing. The
/// symptom of getting that wrong is silent in both directions: the strings
/// vanish from the next .ts, every catalog marks them obsolete, lrelease
/// drops obsolete entries, and a translated build shows English with no
/// warning at any stage. The context has to match what tr() looks up at
/// runtime, which for a member function is the fully-qualified class name.
struct ToolText {
    ModelTool tool{};
    const char* label = nullptr;
    const char* tip = nullptr;
};


const ToolText kToolText[] = {
    {ModelTool::Sketch,       QT_TRANSLATE_NOOP("hobbycad::ModelToolbar", "Sketch"),             QT_TRANSLATE_NOOP("hobbycad::ModelToolbar", "Create sketch on a plane")},
    {ModelTool::SketchOnFace, QT_TRANSLATE_NOOP("hobbycad::ModelToolbar", "Sketch on\nFace"),    QT_TRANSLATE_NOOP("hobbycad::ModelToolbar", "Create sketch on existing face")},

    {ModelTool::ConstructionPlane, QT_TRANSLATE_NOOP("hobbycad::ModelToolbar", "Construction\nPlane"), QT_TRANSLATE_NOOP("hobbycad::ModelToolbar", "Create a construction plane")},

    {ModelTool::Extrude,     QT_TRANSLATE_NOOP("hobbycad::ModelToolbar", "Extrude"),      QT_TRANSLATE_NOOP("hobbycad::ModelToolbar", "Extrude to add material")},
    {ModelTool::CutExtrude,  QT_TRANSLATE_NOOP("hobbycad::ModelToolbar", "Cut\nExtrude"), QT_TRANSLATE_NOOP("hobbycad::ModelToolbar", "Extrude to remove material")},
    {ModelTool::Revolve,     QT_TRANSLATE_NOOP("hobbycad::ModelToolbar", "Revolve"),      QT_TRANSLATE_NOOP("hobbycad::ModelToolbar", "Revolve to add material")},
    {ModelTool::CutRevolve,  QT_TRANSLATE_NOOP("hobbycad::ModelToolbar", "Cut\nRevolve"), QT_TRANSLATE_NOOP("hobbycad::ModelToolbar", "Revolve to remove material")},
    {ModelTool::Loft,        QT_TRANSLATE_NOOP("hobbycad::ModelToolbar", "Loft"),         QT_TRANSLATE_NOOP("hobbycad::ModelToolbar", "Loft to add material")},
    {ModelTool::CutLoft,     QT_TRANSLATE_NOOP("hobbycad::ModelToolbar", "Cut\nLoft"),    QT_TRANSLATE_NOOP("hobbycad::ModelToolbar", "Loft to remove material")},
    {ModelTool::Sweep,       QT_TRANSLATE_NOOP("hobbycad::ModelToolbar", "Sweep"),        QT_TRANSLATE_NOOP("hobbycad::ModelToolbar", "Sweep to add material")},
    {ModelTool::CutSweep,    QT_TRANSLATE_NOOP("hobbycad::ModelToolbar", "Cut\nSweep"),   QT_TRANSLATE_NOOP("hobbycad::ModelToolbar", "Sweep to remove material")},
    {ModelTool::Box,         QT_TRANSLATE_NOOP("hobbycad::ModelToolbar", "Box"),          QT_TRANSLATE_NOOP("hobbycad::ModelToolbar", "Create a box")},
    {ModelTool::Cylinder,    QT_TRANSLATE_NOOP("hobbycad::ModelToolbar", "Cylinder"),     QT_TRANSLATE_NOOP("hobbycad::ModelToolbar", "Create a cylinder")},
    {ModelTool::Sphere,      QT_TRANSLATE_NOOP("hobbycad::ModelToolbar", "Sphere"),       QT_TRANSLATE_NOOP("hobbycad::ModelToolbar", "Create a sphere")},
    {ModelTool::Torus,       QT_TRANSLATE_NOOP("hobbycad::ModelToolbar", "Torus"),        QT_TRANSLATE_NOOP("hobbycad::ModelToolbar", "Create a torus")},
    {ModelTool::Coil,        QT_TRANSLATE_NOOP("hobbycad::ModelToolbar", "Coil"),         QT_TRANSLATE_NOOP("hobbycad::ModelToolbar", "Create a coil/helix")},
    {ModelTool::Pipe,        QT_TRANSLATE_NOOP("hobbycad::ModelToolbar", "Pipe"),         QT_TRANSLATE_NOOP("hobbycad::ModelToolbar", "Create a pipe along a path")},

    {ModelTool::Fillet,      QT_TRANSLATE_NOOP("hobbycad::ModelToolbar", "Fillet"),       QT_TRANSLATE_NOOP("hobbycad::ModelToolbar", "Round edges")},
    {ModelTool::Chamfer,     QT_TRANSLATE_NOOP("hobbycad::ModelToolbar", "Chamfer"),      QT_TRANSLATE_NOOP("hobbycad::ModelToolbar", "Bevel edges")},

    {ModelTool::SimpleHole,   QT_TRANSLATE_NOOP("hobbycad::ModelToolbar", "Simple\nHole"),     QT_TRANSLATE_NOOP("hobbycad::ModelToolbar", "Create a simple hole")},
    {ModelTool::Counterbore,  QT_TRANSLATE_NOOP("hobbycad::ModelToolbar", "Counter-\nbore"),   QT_TRANSLATE_NOOP("hobbycad::ModelToolbar", "Create a counterbore hole")},
    {ModelTool::Countersink,  QT_TRANSLATE_NOOP("hobbycad::ModelToolbar", "Counter-\nsink"),   QT_TRANSLATE_NOOP("hobbycad::ModelToolbar", "Create a countersink hole")},
    {ModelTool::ThreadedHole, QT_TRANSLATE_NOOP("hobbycad::ModelToolbar", "Threaded\nHole"),   QT_TRANSLATE_NOOP("hobbycad::ModelToolbar", "Create a threaded hole")},

    {ModelTool::MoveCopy,    QT_TRANSLATE_NOOP("hobbycad::ModelToolbar", "Move/\nCopy"),  QT_TRANSLATE_NOOP("hobbycad::ModelToolbar", "Move or copy objects")},
    {ModelTool::Align,       QT_TRANSLATE_NOOP("hobbycad::ModelToolbar", "Align"),        QT_TRANSLATE_NOOP("hobbycad::ModelToolbar", "Align objects")},

    {ModelTool::Mirror,      QT_TRANSLATE_NOOP("hobbycad::ModelToolbar", "Mirror"),       QT_TRANSLATE_NOOP("hobbycad::ModelToolbar", "Mirror bodies or features")},
    {ModelTool::Pattern,     QT_TRANSLATE_NOOP("hobbycad::ModelToolbar", "Pattern"),      QT_TRANSLATE_NOOP("hobbycad::ModelToolbar", "Create rectangular or circular pattern")},

    {ModelTool::Parameters,  QT_TRANSLATE_NOOP("hobbycad::ModelToolbar", "Change\nParameters"), QT_TRANSLATE_NOOP("hobbycad::ModelToolbar", "Edit document parameters")},
};


const ToolText* findToolText(ModelTool tool)
{
    for (const ToolText& entry : kToolText) {
        if (entry.tool == tool) {
            return &entry;
        }
    }
    return nullptr;
}

}  // namespace

QString ModelToolbar::toolLabel(ModelTool tool)
{
    const ToolText* entry = findToolText(tool);
    return entry ? tr(entry->label) : QString();
}

QString ModelToolbar::toolTip(ModelTool tool)
{
    const ToolText* entry = findToolText(tool);
    return entry ? tr(entry->tip) : QString();
}

void ModelToolbar::createTools()
{
    auto addSeparator = [this]() {
        auto* sep = new QFrame(this);
        sep->setFrameShape(QFrame::VLine);
        sep->setFrameShadow(QFrame::Sunken);
        sep->setFixedWidth(2);
        m_layout->addWidget(sep);
    };

    // ===== SKETCH button (default-first: Sketch) =====
    m_defaultSketchIcon = QIcon::fromTheme(QStringLiteral("draw-freehand"),
                                           style()->standardIcon(QStyle::SP_FileDialogDetailedView));
    m_sketchBtn = createToolButton(m_defaultSketchIcon, QString(), QString());

    auto* sketchDrop = m_sketchBtn->dropdown();
    sketchDrop->setIconSize(16);
    sketchDrop->addButton(
        QIcon::fromTheme(QStringLiteral("draw-freehand"),
                         style()->standardIcon(QStyle::SP_FileDialogDetailedView)),
        QString(), QString());
    sketchDrop->addButton(
        QIcon::fromTheme(QStringLiteral("draw-polygon"),
                         style()->standardIcon(QStyle::SP_FileDialogContentsView)),
        QString(), QString());

    connect(sketchDrop, &ToolbarDropdown::buttonClicked,
            this, &ModelToolbar::onSketchDropdownClicked);

    // ===== PLANE button (default-first: Construction Plane) =====
    m_defaultPlaneIcon = QIcon::fromTheme(QStringLiteral("draw-rectangle"),
                                          style()->standardIcon(QStyle::SP_FileDialogListView));
    m_planeBtn = createToolButton(m_defaultPlaneIcon, QString(), QString());

    auto* planeDrop = m_planeBtn->dropdown();
    planeDrop->setIconSize(16);
    planeDrop->addButton(
        QIcon::fromTheme(QStringLiteral("draw-rectangle"),
                         style()->standardIcon(QStyle::SP_FileDialogListView)),
        QString(), QString());
    // Future: Surface creation
    // planeDrop->addButton(..., tr("Surface"), tr("Create a surface"));

    connect(planeDrop, &ToolbarDropdown::buttonClicked,
            this, &ModelToolbar::onPlaneDropdownClicked);

    addSeparator();

    // ===== SOLID button (default-first: Extrude) - combines Extrude, Revolve, Loft, Sweep, Primitives =====
    m_defaultSolidIcon = QIcon::fromTheme(QStringLiteral("go-up"),
                                          style()->standardIcon(QStyle::SP_ArrowUp));
    m_solidBtn = createToolButton(m_defaultSolidIcon, QString(), QString());

    auto* solidDrop = m_solidBtn->dropdown();
    solidDrop->setIconSize(16);
    // Extrude operations
    solidDrop->addButton(
        QIcon::fromTheme(QStringLiteral("go-up"),
                         style()->standardIcon(QStyle::SP_ArrowUp)),
        QString(), QString());
    solidDrop->addButton(
        QIcon::fromTheme(QStringLiteral("go-down"),
                         style()->standardIcon(QStyle::SP_ArrowDown)),
        QString(), QString());
    solidDrop->addSeparator();
    // Revolve operations
    solidDrop->addButton(
        QIcon::fromTheme(QStringLiteral("object-rotate-right"),
                         style()->standardIcon(QStyle::SP_BrowserReload)),
        QString(), QString());
    solidDrop->addButton(
        QIcon::fromTheme(QStringLiteral("object-rotate-left"),
                         style()->standardIcon(QStyle::SP_BrowserStop)),
        QString(), QString());
    solidDrop->addSeparator();
    // Loft operations
    solidDrop->addButton(
        QIcon::fromTheme(QStringLiteral("draw-bezier-curves"),
                         style()->standardIcon(QStyle::SP_DesktopIcon)),
        QString(), QString());
    solidDrop->addButton(
        QIcon::fromTheme(QStringLiteral("edit-cut"),
                         style()->standardIcon(QStyle::SP_DialogNoButton)),
        QString(), QString());
    solidDrop->addSeparator();
    // Sweep operations
    solidDrop->addButton(
        QIcon::fromTheme(QStringLiteral("draw-path"),
                         style()->standardIcon(QStyle::SP_ArrowForward)),
        QString(), QString());
    solidDrop->addButton(
        QIcon::fromTheme(QStringLiteral("draw-eraser"),
                         style()->standardIcon(QStyle::SP_DialogDiscardButton)),
        QString(), QString());
    solidDrop->addSeparator();
    // Primitives
    solidDrop->addButton(
        QIcon::fromTheme(QStringLiteral("draw-cube"),
                         style()->standardIcon(QStyle::SP_ComputerIcon)),
        QString(), QString());
    solidDrop->addButton(
        QIcon::fromTheme(QStringLiteral("draw-cylinder"),
                         style()->standardIcon(QStyle::SP_DriveHDIcon)),
        QString(), QString());
    solidDrop->addButton(
        QIcon::fromTheme(QStringLiteral("draw-sphere"),
                         style()->standardIcon(QStyle::SP_DialogHelpButton)),
        QString(), QString());
    solidDrop->addButton(
        QIcon::fromTheme(QStringLiteral("draw-donut"),
                         style()->standardIcon(QStyle::SP_DialogResetButton)),
        QString(), QString());
    solidDrop->addButton(
        QIcon::fromTheme(QStringLiteral("draw-spiral"),
                         style()->standardIcon(QStyle::SP_BrowserReload)),
        QString(), QString());
    solidDrop->addButton(
        QIcon::fromTheme(QStringLiteral("draw-path"),
                         style()->standardIcon(QStyle::SP_ArrowRight)),
        QString(), QString());

    connect(solidDrop, &ToolbarDropdown::buttonClicked,
            this, &ModelToolbar::onSolidDropdownClicked);

    addSeparator();

    // ===== FILLET button (default-first: Fillet) =====
    m_defaultFilletIcon = QIcon::fromTheme(QStringLiteral("format-stroke-color"),
                                           style()->standardIcon(QStyle::SP_DialogApplyButton));
    m_filletBtn = createToolButton(m_defaultFilletIcon, QString(), QString());

    auto* filletDrop = m_filletBtn->dropdown();
    filletDrop->setIconSize(16);
    filletDrop->addButton(
        QIcon::fromTheme(QStringLiteral("format-stroke-color"),
                         style()->standardIcon(QStyle::SP_DialogApplyButton)),
        QString(), QString());
    filletDrop->addButton(
        QIcon::fromTheme(QStringLiteral("draw-line"),
                         style()->standardIcon(QStyle::SP_DialogOkButton)),
        QString(), QString());

    connect(filletDrop, &ToolbarDropdown::buttonClicked,
            this, &ModelToolbar::onFilletDropdownClicked);

    // ===== HOLE button (default-first: Simple Hole) =====
    m_defaultHoleIcon = QIcon::fromTheme(QStringLiteral("draw-circle"),
                                         style()->standardIcon(QStyle::SP_DialogDiscardButton));
    m_holeBtn = createToolButton(m_defaultHoleIcon, QString(), QString());

    auto* holeDrop = m_holeBtn->dropdown();
    holeDrop->setIconSize(16);
    holeDrop->addButton(
        QIcon::fromTheme(QStringLiteral("draw-circle"),
                         style()->standardIcon(QStyle::SP_DialogDiscardButton)),
        QString(), QString());
    holeDrop->addButton(
        QIcon::fromTheme(QStringLiteral("draw-ellipse"),
                         style()->standardIcon(QStyle::SP_DialogNoButton)),
        QString(), QString());
    holeDrop->addButton(
        QIcon::fromTheme(QStringLiteral("draw-polygon"),
                         style()->standardIcon(QStyle::SP_DialogYesButton)),
        QString(), QString());
    holeDrop->addButton(
        QIcon::fromTheme(QStringLiteral("draw-spiral"),
                         style()->standardIcon(QStyle::SP_DialogSaveButton)),
        QString(), QString());

    connect(holeDrop, &ToolbarDropdown::buttonClicked,
            this, &ModelToolbar::onHoleDropdownClicked);

    addSeparator();

    // ===== MOVE button (default-first: Move/Copy) =====
    m_defaultMoveIcon = QIcon::fromTheme(QStringLiteral("transform-move"),
                                         style()->standardIcon(QStyle::SP_ArrowRight));
    m_moveBtn = createToolButton(m_defaultMoveIcon, QString(), QString());

    auto* moveDrop = m_moveBtn->dropdown();
    moveDrop->setIconSize(16);
    moveDrop->addButton(
        QIcon::fromTheme(QStringLiteral("transform-move"),
                         style()->standardIcon(QStyle::SP_ArrowRight)),
        QString(), QString());
    moveDrop->addButton(
        QIcon::fromTheme(QStringLiteral("align-horizontal-center"),
                         style()->standardIcon(QStyle::SP_ToolBarHorizontalExtensionButton)),
        QString(), QString());

    connect(moveDrop, &ToolbarDropdown::buttonClicked,
            this, &ModelToolbar::onMoveDropdownClicked);

    // ===== MIRROR button (default-first: Mirror) =====
    m_defaultMirrorIcon = QIcon::fromTheme(QStringLiteral("object-flip-horizontal"),
                                           style()->standardIcon(QStyle::SP_ArrowBack));
    m_mirrorBtn = createToolButton(m_defaultMirrorIcon, QString(), QString());

    auto* mirrorDrop = m_mirrorBtn->dropdown();
    mirrorDrop->setIconSize(16);
    mirrorDrop->addButton(
        QIcon::fromTheme(QStringLiteral("object-flip-horizontal"),
                         style()->standardIcon(QStyle::SP_ArrowBack)),
        QString(), QString());
    mirrorDrop->addButton(
        QIcon::fromTheme(QStringLiteral("edit-copy"),
                         style()->standardIcon(QStyle::SP_FileDialogDetailedView)),
        QString(), QString());

    connect(mirrorDrop, &ToolbarDropdown::buttonClicked,
            this, &ModelToolbar::onMirrorDropdownClicked);

    addSeparator();

    // ===== PARAMETERS button (single function, no dropdown behavior) =====
    m_paramsBtn = createToolButton(
        QIcon::fromTheme(QStringLiteral("document-properties"),
                         style()->standardIcon(QStyle::SP_FileDialogInfoView)),
        QString(), QString());

    auto* paramsDrop = m_paramsBtn->dropdown();
    paramsDrop->setIconSize(16);
    paramsDrop->addButton(
        QIcon::fromTheme(QStringLiteral("document-properties"),
                         style()->standardIcon(QStyle::SP_FileDialogInfoView)),
        QString(), QString());

    // Params button just emits signal directly
    connect(m_paramsBtn, &ToolbarButton::clicked, this, [this]() {
        emit parametersClicked();
    });
    // Its single dropdown row does the same thing. Without this the row is
    // inert: modeltoolbar connects dropdownClicked for no other button.
    connect(m_paramsBtn, &ToolbarButton::dropdownClicked, this, [this](int) {
        emit parametersClicked();
    });

    // Disable buttons that aren't implemented yet
    m_solidBtn->setEnabled(false);
    m_filletBtn->setEnabled(false);
    m_holeBtn->setEnabled(false);
    m_moveBtn->setEnabled(false);
    m_mirrorBtn->setEnabled(false);

    // Every caption and tooltip above is applied here, and only here, so a
    // new string has to be added in one place rather than two.
    retranslate();
}

void ModelToolbar::onToolClicked()
{
    auto* btn = qobject_cast<ToolbarButton*>(sender());
    if (!btn) return;

    // Handle params button separately (no tool state)
    if (btn == m_paramsBtn) {
        return;  // Signal already connected in createTools
    }

    // Default-first buttons: activate last selection (always has a default)
    if (btn == m_sketchBtn) {
        setActiveToolInternal(m_lastSketchTool, btn);
        // Emit specific signal
        if (m_lastSketchTool == ModelTool::Sketch) {
            emit createSketchClicked();
        } else if (m_lastSketchTool == ModelTool::SketchOnFace) {
            emit createSketchOnFaceClicked();
        }
    }
    else if (btn == m_solidBtn) {
        setActiveToolInternal(m_lastSolidTool, btn);
    }
    else if (btn == m_filletBtn) {
        setActiveToolInternal(m_lastFilletTool, btn);
    }
    else if (btn == m_moveBtn) {
        setActiveToolInternal(m_lastMoveTool, btn);
    }
    else if (btn == m_mirrorBtn) {
        setActiveToolInternal(m_lastMirrorTool, btn);
    }
    else if (btn == m_planeBtn) {
        setActiveToolInternal(m_lastPlaneTool, btn);
        if (m_lastPlaneTool == ModelTool::ConstructionPlane) {
            emit createConstructionPlaneClicked();
        }
    }
    else if (btn == m_holeBtn) {
        setActiveToolInternal(m_lastHoleTool, btn);
    }
}

void ModelToolbar::onSketchDropdownClicked(int index)
{
    static const ModelTool tools[] = { ModelTool::Sketch, ModelTool::SketchOnFace };

    if (index >= 0 && index < 2) {
        m_lastSketchTool = tools[index];
        m_sketchBtn->setText(toolLabel(tools[index]));
        m_captionSketch = tools[index];
        setActiveToolInternal(tools[index], m_sketchBtn);

        // Emit specific signals
        if (tools[index] == ModelTool::Sketch) {
            emit createSketchClicked();
        } else if (tools[index] == ModelTool::SketchOnFace) {
            emit createSketchOnFaceClicked();
        }
    }
}

void ModelToolbar::onPlaneDropdownClicked(int index)
{
    static const ModelTool tools[] = { ModelTool::ConstructionPlane };

    if (index >= 0 && index < 1) {
        m_lastPlaneTool = tools[index];
        m_planeBtn->setText(toolLabel(tools[index]));
        m_captionPlane = tools[index];
        setActiveToolInternal(tools[index], m_planeBtn);

        if (tools[index] == ModelTool::ConstructionPlane) {
            emit createConstructionPlaneClicked();
        }
    }
}

void ModelToolbar::onSolidDropdownClicked(int index)
{
    // Extrude (0-1), Revolve (2-3), Loft (4-5), Sweep (6-7), Primitives (8-13)
    // Note: Separators don't add to index, they're just visual
    static const ModelTool tools[] = {
        ModelTool::Extrude, ModelTool::CutExtrude,
        ModelTool::Revolve, ModelTool::CutRevolve,
        ModelTool::Loft, ModelTool::CutLoft,
        ModelTool::Sweep, ModelTool::CutSweep,
        ModelTool::Box, ModelTool::Cylinder, ModelTool::Sphere,
        ModelTool::Torus, ModelTool::Coil, ModelTool::Pipe
    };

    if (index >= 0 && index < 14) {
        m_lastSolidTool = tools[index];
        m_solidBtn->setText(toolLabel(tools[index]));
        m_captionSolid = tools[index];
        setActiveToolInternal(tools[index], m_solidBtn);
    }
}

void ModelToolbar::onFilletDropdownClicked(int index)
{
    static const ModelTool tools[] = { ModelTool::Fillet, ModelTool::Chamfer };

    if (index >= 0 && index < 2) {
        m_lastFilletTool = tools[index];
        m_filletBtn->setText(toolLabel(tools[index]));
        m_captionFillet = tools[index];
        setActiveToolInternal(tools[index], m_filletBtn);
    }
}

void ModelToolbar::onHoleDropdownClicked(int index)
{
    static const ModelTool tools[] = {
        ModelTool::SimpleHole, ModelTool::Counterbore,
        ModelTool::Countersink, ModelTool::ThreadedHole
    };

    if (index >= 0 && index < 4) {
        m_lastHoleTool = tools[index];
        m_holeBtn->setText(toolLabel(tools[index]));
        m_captionHole = tools[index];
        setActiveToolInternal(tools[index], m_holeBtn);
    }
}

void ModelToolbar::onMoveDropdownClicked(int index)
{
    static const ModelTool tools[] = { ModelTool::MoveCopy, ModelTool::Align };

    if (index >= 0 && index < 2) {
        m_lastMoveTool = tools[index];
        m_moveBtn->setText(toolLabel(tools[index]));
        m_captionMove = tools[index];
        setActiveToolInternal(tools[index], m_moveBtn);
    }
}

void ModelToolbar::onMirrorDropdownClicked(int index)
{
    static const ModelTool tools[] = { ModelTool::Mirror, ModelTool::Pattern };

    if (index >= 0 && index < 2) {
        m_lastMirrorTool = tools[index];
        m_mirrorBtn->setText(toolLabel(tools[index]));
        m_captionMirror = tools[index];
        setActiveToolInternal(tools[index], m_mirrorBtn);
    }
}

void ModelToolbar::setActiveToolInternal(ModelTool tool, ToolbarButton* activeBtn)
{
    // If clicking the same tool, deselect
    ModelTool newTool = (tool == m_activeTool) ? ModelTool::None : tool;

    // Update checked states
    m_sketchBtn->setChecked(activeBtn == m_sketchBtn && newTool != ModelTool::None);
    m_planeBtn->setChecked(activeBtn == m_planeBtn && newTool != ModelTool::None);
    m_solidBtn->setChecked(activeBtn == m_solidBtn && newTool != ModelTool::None);
    m_filletBtn->setChecked(activeBtn == m_filletBtn && newTool != ModelTool::None);
    m_holeBtn->setChecked(activeBtn == m_holeBtn && newTool != ModelTool::None);
    m_moveBtn->setChecked(activeBtn == m_moveBtn && newTool != ModelTool::None);
    m_mirrorBtn->setChecked(activeBtn == m_mirrorBtn && newTool != ModelTool::None);

    if (newTool != m_activeTool) {
        m_activeTool = newTool;
        emit toolSelected(m_activeTool);
    }
}

void ModelToolbar::setActiveTool(ModelTool tool)
{
    m_activeTool = tool;
    // Update button checked states based on which group the tool belongs to
    // (simplified - in practice would need to map tool to button group)
}

// ---- Retranslation --------------------------------------------------

void ModelToolbar::retranslate()
{
    // Group defaults. These are the captions a button shows before anything
    // has been picked from its dropdown, and they name the group rather than
    // any one tool, so they are not in kToolText.
    m_defaultSketchText = tr("Sketch");
    m_defaultPlaneText  = tr("Plane");
    m_defaultSolidText  = tr("Solid");
    m_defaultFilletText = tr("Fillet");
    m_defaultHoleText   = tr("Simple\nHole");
    m_defaultMoveText   = tr("Move");
    m_defaultMirrorText = tr("Mirror");

    // A caption is rebuilt from the tool currently on the button, not
    // restored from a remembered string: the user may have picked something
    // from the dropdown since the last time this ran, and ModelTool::None
    // means the group default is showing.
    const struct {
        ToolbarButton* button;
        ModelTool caption;
        const QString& defaultText;
        const char* defaultTip;
    } groups[] = {
        {m_sketchBtn, m_captionSketch, m_defaultSketchText, QT_TR_NOOP("Create a 2D sketch")},
        {m_planeBtn,  m_captionPlane,  m_defaultPlaneText,  QT_TR_NOOP("Create construction plane")},
        {m_solidBtn,  m_captionSolid,  m_defaultSolidText,  QT_TR_NOOP("Create solid geometry")},
        {m_filletBtn, m_captionFillet, m_defaultFilletText, QT_TR_NOOP("Round or bevel edges")},
        {m_holeBtn,   m_captionHole,   m_defaultHoleText,   QT_TR_NOOP("Create a simple hole")},
        {m_moveBtn,   m_captionMove,   m_defaultMoveText,   QT_TR_NOOP("Transform objects")},
        {m_mirrorBtn, m_captionMirror, m_defaultMirrorText, QT_TR_NOOP("Mirror or pattern objects")},
    };
    for (const auto& group : groups) {
        if (!group.button) {
            continue;
        }
        group.button->setText(group.caption == ModelTool::None
                                  ? group.defaultText
                                  : toolLabel(group.caption));
        group.button->setToolTip(group.caption == ModelTool::None
                                     ? tr(group.defaultTip)
                                     : toolTip(group.caption));
    }

    if (m_paramsBtn) {
        m_paramsBtn->setText(tr("Params"));
        m_paramsBtn->setToolTip(tr("Manage parameters"));
        // The dropdown row is added with an empty caption; addButton() is the
        // only way text gets in, so it has to be set here like every other
        // row. ModelTool::Parameters already carries the strings.
        if (m_paramsBtn->dropdown()) {
            m_paramsBtn->dropdown()->setButtonText(
                0, toolLabel(ModelTool::Parameters),
                toolTip(ModelTool::Parameters));
        }
    }

    // Dropdown rows. Order matches the tools[] arrays in the handlers, which
    // is the order they were added; separators do not take an index.
    static const ModelTool sketchTools[] = {ModelTool::Sketch, ModelTool::SketchOnFace};
    static const ModelTool planeTools[]  = {ModelTool::ConstructionPlane};
    static const ModelTool solidTools[]  = {
        ModelTool::Extrude, ModelTool::CutExtrude, ModelTool::Revolve,
        ModelTool::CutRevolve, ModelTool::Loft, ModelTool::CutLoft,
        ModelTool::Sweep, ModelTool::CutSweep, ModelTool::Box,
        ModelTool::Cylinder, ModelTool::Sphere, ModelTool::Torus,
        ModelTool::Coil, ModelTool::Pipe};
    static const ModelTool filletTools[] = {ModelTool::Fillet, ModelTool::Chamfer};
    static const ModelTool holeTools[]   = {
        ModelTool::SimpleHole, ModelTool::Counterbore,
        ModelTool::Countersink, ModelTool::ThreadedHole};
    static const ModelTool moveTools[]   = {ModelTool::MoveCopy, ModelTool::Align};
    static const ModelTool mirrorTools[] = {ModelTool::Mirror, ModelTool::Pattern};

    const auto relabel = [this](ToolbarButton* button, const ModelTool* tools,
                                int count) {
        if (!button || !button->dropdown()) {
            return;
        }
        for (int i = 0; i < count; ++i) {
            button->dropdown()->setButtonText(i, toolLabel(tools[i]),
                                              toolTip(tools[i]));
        }
    };
    relabel(m_sketchBtn, sketchTools, 2);
    relabel(m_planeBtn,  planeTools,  1);
    relabel(m_solidBtn,  solidTools,  14);
    relabel(m_filletBtn, filletTools, 2);
    relabel(m_holeBtn,   holeTools,   4);
    relabel(m_moveBtn,   moveTools,   2);
    relabel(m_mirrorBtn, mirrorTools, 2);
}

void ModelToolbar::resetAllButtons()
{
    // Reset default-first buttons to their default tool
    m_captionSketch = ModelTool::None;
    m_lastSketchTool = ModelTool::Sketch;
    m_sketchBtn->setIcon(m_defaultSketchIcon);
    m_sketchBtn->setText(m_defaultSketchText);

    m_captionSolid = ModelTool::None;
    m_lastSolidTool = ModelTool::Extrude;
    m_solidBtn->setIcon(m_defaultSolidIcon);
    m_solidBtn->setText(m_defaultSolidText);

    m_captionFillet = ModelTool::None;
    m_lastFilletTool = ModelTool::Fillet;
    m_filletBtn->setIcon(m_defaultFilletIcon);
    m_filletBtn->setText(m_defaultFilletText);

    m_captionMove = ModelTool::None;
    m_lastMoveTool = ModelTool::MoveCopy;
    m_moveBtn->setIcon(m_defaultMoveIcon);
    m_moveBtn->setText(m_defaultMoveText);

    m_captionMirror = ModelTool::None;
    m_lastMirrorTool = ModelTool::Mirror;
    m_mirrorBtn->setIcon(m_defaultMirrorIcon);
    m_mirrorBtn->setText(m_defaultMirrorText);

    m_captionPlane = ModelTool::None;
    m_lastPlaneTool = ModelTool::ConstructionPlane;
    m_planeBtn->setIcon(m_defaultPlaneIcon);
    m_planeBtn->setText(m_defaultPlaneText);

    m_captionHole = ModelTool::None;
    m_lastHoleTool = ModelTool::SimpleHole;
    m_holeBtn->setIcon(m_defaultHoleIcon);
    m_holeBtn->setText(m_defaultHoleText);

    // Clear active tool
    m_activeTool = ModelTool::None;

    // Uncheck all buttons
    m_sketchBtn->setChecked(false);
    m_planeBtn->setChecked(false);
    m_solidBtn->setChecked(false);
    m_filletBtn->setChecked(false);
    m_holeBtn->setChecked(false);
    m_moveBtn->setChecked(false);
    m_mirrorBtn->setChecked(false);
}

}  // namespace hobbycad
