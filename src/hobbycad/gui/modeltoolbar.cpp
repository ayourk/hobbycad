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
    m_defaultSketchText = tr("Sketch");
    m_sketchBtn = createToolButton(m_defaultSketchIcon, m_defaultSketchText,
                                   tr("Create a 2D sketch"));

    auto* sketchDrop = m_sketchBtn->dropdown();
    sketchDrop->setIconSize(16);
    sketchDrop->addButton(
        QIcon::fromTheme(QStringLiteral("draw-freehand"),
                         style()->standardIcon(QStyle::SP_FileDialogDetailedView)),
        tr("Sketch"), tr("Create sketch on a plane"));
    sketchDrop->addButton(
        QIcon::fromTheme(QStringLiteral("draw-polygon"),
                         style()->standardIcon(QStyle::SP_FileDialogContentsView)),
        tr("Sketch on\nFace"), tr("Create sketch on existing face"));

    connect(sketchDrop, &ToolbarDropdown::buttonClicked,
            this, &ModelToolbar::onSketchDropdownClicked);

    // ===== PLANE button (default-first: Construction Plane) =====
    m_defaultPlaneIcon = QIcon::fromTheme(QStringLiteral("draw-rectangle"),
                                          style()->standardIcon(QStyle::SP_FileDialogListView));
    m_defaultPlaneText = tr("Plane");
    m_planeBtn = createToolButton(m_defaultPlaneIcon, m_defaultPlaneText,
                                  tr("Create construction plane"));

    auto* planeDrop = m_planeBtn->dropdown();
    planeDrop->setIconSize(16);
    planeDrop->addButton(
        QIcon::fromTheme(QStringLiteral("draw-rectangle"),
                         style()->standardIcon(QStyle::SP_FileDialogListView)),
        tr("Construction\nPlane"), tr("Create a construction plane"));
    // Future: Surface creation
    // planeDrop->addButton(..., tr("Surface"), tr("Create a surface"));

    connect(planeDrop, &ToolbarDropdown::buttonClicked,
            this, &ModelToolbar::onPlaneDropdownClicked);

    addSeparator();

    // ===== SOLID button (default-first: Extrude) - combines Extrude, Revolve, Loft, Sweep, Primitives =====
    m_defaultSolidIcon = QIcon::fromTheme(QStringLiteral("go-up"),
                                          style()->standardIcon(QStyle::SP_ArrowUp));
    m_defaultSolidText = tr("Solid");
    m_solidBtn = createToolButton(m_defaultSolidIcon, m_defaultSolidText,
                                  tr("Create solid geometry"));

    auto* solidDrop = m_solidBtn->dropdown();
    solidDrop->setIconSize(16);
    // Extrude operations
    solidDrop->addButton(
        QIcon::fromTheme(QStringLiteral("go-up"),
                         style()->standardIcon(QStyle::SP_ArrowUp)),
        tr("Extrude"), tr("Extrude to add material"));
    solidDrop->addButton(
        QIcon::fromTheme(QStringLiteral("go-down"),
                         style()->standardIcon(QStyle::SP_ArrowDown)),
        tr("Cut\nExtrude"), tr("Extrude to remove material"));
    solidDrop->addSeparator();
    // Revolve operations
    solidDrop->addButton(
        QIcon::fromTheme(QStringLiteral("object-rotate-right"),
                         style()->standardIcon(QStyle::SP_BrowserReload)),
        tr("Revolve"), tr("Revolve to add material"));
    solidDrop->addButton(
        QIcon::fromTheme(QStringLiteral("object-rotate-left"),
                         style()->standardIcon(QStyle::SP_BrowserStop)),
        tr("Cut\nRevolve"), tr("Revolve to remove material"));
    solidDrop->addSeparator();
    // Loft operations
    solidDrop->addButton(
        QIcon::fromTheme(QStringLiteral("draw-bezier-curves"),
                         style()->standardIcon(QStyle::SP_DesktopIcon)),
        tr("Loft"), tr("Loft to add material"));
    solidDrop->addButton(
        QIcon::fromTheme(QStringLiteral("edit-cut"),
                         style()->standardIcon(QStyle::SP_DialogNoButton)),
        tr("Cut\nLoft"), tr("Loft to remove material"));
    solidDrop->addSeparator();
    // Sweep operations
    solidDrop->addButton(
        QIcon::fromTheme(QStringLiteral("draw-path"),
                         style()->standardIcon(QStyle::SP_ArrowForward)),
        tr("Sweep"), tr("Sweep to add material"));
    solidDrop->addButton(
        QIcon::fromTheme(QStringLiteral("draw-eraser"),
                         style()->standardIcon(QStyle::SP_DialogDiscardButton)),
        tr("Cut\nSweep"), tr("Sweep to remove material"));
    solidDrop->addSeparator();
    // Primitives
    solidDrop->addButton(
        QIcon::fromTheme(QStringLiteral("draw-cube"),
                         style()->standardIcon(QStyle::SP_ComputerIcon)),
        tr("Box"), tr("Create a box"));
    solidDrop->addButton(
        QIcon::fromTheme(QStringLiteral("draw-cylinder"),
                         style()->standardIcon(QStyle::SP_DriveHDIcon)),
        tr("Cylinder"), tr("Create a cylinder"));
    solidDrop->addButton(
        QIcon::fromTheme(QStringLiteral("draw-sphere"),
                         style()->standardIcon(QStyle::SP_DialogHelpButton)),
        tr("Sphere"), tr("Create a sphere"));
    solidDrop->addButton(
        QIcon::fromTheme(QStringLiteral("draw-donut"),
                         style()->standardIcon(QStyle::SP_DialogResetButton)),
        tr("Torus"), tr("Create a torus"));
    solidDrop->addButton(
        QIcon::fromTheme(QStringLiteral("draw-spiral"),
                         style()->standardIcon(QStyle::SP_BrowserReload)),
        tr("Coil"), tr("Create a coil/helix"));
    solidDrop->addButton(
        QIcon::fromTheme(QStringLiteral("draw-path"),
                         style()->standardIcon(QStyle::SP_ArrowRight)),
        tr("Pipe"), tr("Create a pipe along a path"));

    connect(solidDrop, &ToolbarDropdown::buttonClicked,
            this, &ModelToolbar::onSolidDropdownClicked);

    addSeparator();

    // ===== FILLET button (default-first: Fillet) =====
    m_defaultFilletIcon = QIcon::fromTheme(QStringLiteral("format-stroke-color"),
                                           style()->standardIcon(QStyle::SP_DialogApplyButton));
    m_defaultFilletText = tr("Fillet");
    m_filletBtn = createToolButton(m_defaultFilletIcon, m_defaultFilletText,
                                   tr("Round or bevel edges"));

    auto* filletDrop = m_filletBtn->dropdown();
    filletDrop->setIconSize(16);
    filletDrop->addButton(
        QIcon::fromTheme(QStringLiteral("format-stroke-color"),
                         style()->standardIcon(QStyle::SP_DialogApplyButton)),
        tr("Fillet"), tr("Round edges"));
    filletDrop->addButton(
        QIcon::fromTheme(QStringLiteral("draw-line"),
                         style()->standardIcon(QStyle::SP_DialogOkButton)),
        tr("Chamfer"), tr("Bevel edges"));

    connect(filletDrop, &ToolbarDropdown::buttonClicked,
            this, &ModelToolbar::onFilletDropdownClicked);

    // ===== HOLE button (default-first: Simple Hole) =====
    m_defaultHoleIcon = QIcon::fromTheme(QStringLiteral("draw-circle"),
                                         style()->standardIcon(QStyle::SP_DialogDiscardButton));
    m_defaultHoleText = tr("Simple\nHole");
    m_holeBtn = createToolButton(m_defaultHoleIcon, m_defaultHoleText,
                                 tr("Create a simple hole"));

    auto* holeDrop = m_holeBtn->dropdown();
    holeDrop->setIconSize(16);
    holeDrop->addButton(
        QIcon::fromTheme(QStringLiteral("draw-circle"),
                         style()->standardIcon(QStyle::SP_DialogDiscardButton)),
        tr("Simple\nHole"), tr("Create a simple hole"));
    holeDrop->addButton(
        QIcon::fromTheme(QStringLiteral("draw-ellipse"),
                         style()->standardIcon(QStyle::SP_DialogNoButton)),
        tr("Counter-\nbore"), tr("Create a counterbore hole"));
    holeDrop->addButton(
        QIcon::fromTheme(QStringLiteral("draw-polygon"),
                         style()->standardIcon(QStyle::SP_DialogYesButton)),
        tr("Counter-\nsink"), tr("Create a countersink hole"));
    holeDrop->addButton(
        QIcon::fromTheme(QStringLiteral("draw-spiral"),
                         style()->standardIcon(QStyle::SP_DialogSaveButton)),
        tr("Threaded\nHole"), tr("Create a threaded hole"));

    connect(holeDrop, &ToolbarDropdown::buttonClicked,
            this, &ModelToolbar::onHoleDropdownClicked);

    addSeparator();

    // ===== MOVE button (default-first: Move/Copy) =====
    m_defaultMoveIcon = QIcon::fromTheme(QStringLiteral("transform-move"),
                                         style()->standardIcon(QStyle::SP_ArrowRight));
    m_defaultMoveText = tr("Move");
    m_moveBtn = createToolButton(m_defaultMoveIcon, m_defaultMoveText,
                                 tr("Transform objects"));

    auto* moveDrop = m_moveBtn->dropdown();
    moveDrop->setIconSize(16);
    moveDrop->addButton(
        QIcon::fromTheme(QStringLiteral("transform-move"),
                         style()->standardIcon(QStyle::SP_ArrowRight)),
        tr("Move/\nCopy"), tr("Move or copy objects"));
    moveDrop->addButton(
        QIcon::fromTheme(QStringLiteral("align-horizontal-center"),
                         style()->standardIcon(QStyle::SP_ToolBarHorizontalExtensionButton)),
        tr("Align"), tr("Align objects"));

    connect(moveDrop, &ToolbarDropdown::buttonClicked,
            this, &ModelToolbar::onMoveDropdownClicked);

    // ===== MIRROR button (default-first: Mirror) =====
    m_defaultMirrorIcon = QIcon::fromTheme(QStringLiteral("object-flip-horizontal"),
                                           style()->standardIcon(QStyle::SP_ArrowBack));
    m_defaultMirrorText = tr("Mirror");
    m_mirrorBtn = createToolButton(m_defaultMirrorIcon, m_defaultMirrorText,
                                   tr("Mirror or pattern objects"));

    auto* mirrorDrop = m_mirrorBtn->dropdown();
    mirrorDrop->setIconSize(16);
    mirrorDrop->addButton(
        QIcon::fromTheme(QStringLiteral("object-flip-horizontal"),
                         style()->standardIcon(QStyle::SP_ArrowBack)),
        tr("Mirror"), tr("Mirror bodies or features"));
    mirrorDrop->addButton(
        QIcon::fromTheme(QStringLiteral("edit-copy"),
                         style()->standardIcon(QStyle::SP_FileDialogDetailedView)),
        tr("Pattern"), tr("Create rectangular or circular pattern"));

    connect(mirrorDrop, &ToolbarDropdown::buttonClicked,
            this, &ModelToolbar::onMirrorDropdownClicked);

    addSeparator();

    // ===== PARAMETERS button (single function, no dropdown behavior) =====
    m_paramsBtn = createToolButton(
        QIcon::fromTheme(QStringLiteral("document-properties"),
                         style()->standardIcon(QStyle::SP_FileDialogInfoView)),
        tr("Params"), tr("Manage parameters"));

    auto* paramsDrop = m_paramsBtn->dropdown();
    paramsDrop->setIconSize(16);
    paramsDrop->addButton(
        QIcon::fromTheme(QStringLiteral("document-properties"),
                         style()->standardIcon(QStyle::SP_FileDialogInfoView)),
        tr("Change\nParameters"), tr("Edit document parameters"));

    // Params button just emits signal directly
    connect(m_paramsBtn, &ToolbarButton::clicked, this, [this]() {
        emit parametersClicked();
    });

    // Disable buttons that aren't implemented yet
    m_solidBtn->setEnabled(false);
    m_filletBtn->setEnabled(false);
    m_holeBtn->setEnabled(false);
    m_moveBtn->setEnabled(false);
    m_mirrorBtn->setEnabled(false);
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
    static const char* names[] = { "Sketch", "Sketch on\nFace" };

    if (index >= 0 && index < 2) {
        m_lastSketchTool = tools[index];
        m_sketchBtn->setText(tr(names[index]));
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
    static const char* names[] = { "Construction\nPlane" };

    if (index >= 0 && index < 1) {
        m_lastPlaneTool = tools[index];
        m_planeBtn->setText(tr(names[index]));
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
    static const char* names[] = {
        "Extrude", "Cut\nExtrude",
        "Revolve", "Cut\nRevolve",
        "Loft", "Cut\nLoft",
        "Sweep", "Cut\nSweep",
        "Box", "Cylinder", "Sphere", "Torus", "Coil", "Pipe"
    };

    if (index >= 0 && index < 14) {
        m_lastSolidTool = tools[index];
        m_solidBtn->setText(tr(names[index]));
        setActiveToolInternal(tools[index], m_solidBtn);
    }
}

void ModelToolbar::onFilletDropdownClicked(int index)
{
    static const ModelTool tools[] = { ModelTool::Fillet, ModelTool::Chamfer };
    static const char* names[] = { "Fillet", "Chamfer" };

    if (index >= 0 && index < 2) {
        m_lastFilletTool = tools[index];
        m_filletBtn->setText(tr(names[index]));
        setActiveToolInternal(tools[index], m_filletBtn);
    }
}

void ModelToolbar::onHoleDropdownClicked(int index)
{
    static const ModelTool tools[] = {
        ModelTool::SimpleHole, ModelTool::Counterbore,
        ModelTool::Countersink, ModelTool::ThreadedHole
    };
    static const char* names[] = { "Simple\nHole", "Counter-\nbore", "Counter-\nsink", "Threaded\nHole" };

    if (index >= 0 && index < 4) {
        m_lastHoleTool = tools[index];
        m_holeBtn->setText(tr(names[index]));
        setActiveToolInternal(tools[index], m_holeBtn);
    }
}

void ModelToolbar::onMoveDropdownClicked(int index)
{
    static const ModelTool tools[] = { ModelTool::MoveCopy, ModelTool::Align };
    static const char* names[] = { "Move/\nCopy", "Align" };

    if (index >= 0 && index < 2) {
        m_lastMoveTool = tools[index];
        m_moveBtn->setText(tr(names[index]));
        setActiveToolInternal(tools[index], m_moveBtn);
    }
}

void ModelToolbar::onMirrorDropdownClicked(int index)
{
    static const ModelTool tools[] = { ModelTool::Mirror, ModelTool::Pattern };
    static const char* names[] = { "Mirror", "Pattern" };

    if (index >= 0 && index < 2) {
        m_lastMirrorTool = tools[index];
        m_mirrorBtn->setText(tr(names[index]));
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

void ModelToolbar::resetAllButtons()
{
    // Reset default-first buttons to their default tool
    m_lastSketchTool = ModelTool::Sketch;
    m_sketchBtn->setIcon(m_defaultSketchIcon);
    m_sketchBtn->setText(m_defaultSketchText);

    m_lastSolidTool = ModelTool::Extrude;
    m_solidBtn->setIcon(m_defaultSolidIcon);
    m_solidBtn->setText(m_defaultSolidText);

    m_lastFilletTool = ModelTool::Fillet;
    m_filletBtn->setIcon(m_defaultFilletIcon);
    m_filletBtn->setText(m_defaultFilletText);

    m_lastMoveTool = ModelTool::MoveCopy;
    m_moveBtn->setIcon(m_defaultMoveIcon);
    m_moveBtn->setText(m_defaultMoveText);

    m_lastMirrorTool = ModelTool::Mirror;
    m_mirrorBtn->setIcon(m_defaultMirrorIcon);
    m_mirrorBtn->setText(m_defaultMirrorText);

    m_lastPlaneTool = ModelTool::ConstructionPlane;
    m_planeBtn->setIcon(m_defaultPlaneIcon);
    m_planeBtn->setText(m_defaultPlaneText);

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
