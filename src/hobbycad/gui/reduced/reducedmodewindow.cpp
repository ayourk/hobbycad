// =====================================================================
//  src/hobbycad/gui/reduced/reducedmodewindow.cpp — Reduced Mode window
// =====================================================================

#include "reducedmodewindow.h"
#include "reducedviewport.h"
#include "diagnosticdialog.h"
#include "gui/clipanel.h"
#include "gui/viewporttoolbar.h"
#include "gui/toolbarbutton.h"
#include "gui/toolbardropdown.h"
#include "gui/timelinewidget.h"
#include "gui/formulafield.h"
#include "gui/sketchtoolbar.h"
#include "gui/sketchcanvas.h"
#include "gui/sketchactionbar.h"
#include "gui/sketchplanedialog.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace hobbycad {

ReducedModeWindow::ReducedModeWindow(const OpenGLInfo& glInfo,
                                     QWidget* parent)
    : MainWindow(glInfo, parent)
{
    setObjectName(QStringLiteral("ReducedModeWindow"));

    // Create central widget container with toolbar + viewport + timeline
    auto* container = new QWidget(this);
    m_mainLayout = new QVBoxLayout(container);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);

    // Toolbar stack (normal toolbar vs sketch toolbar)
    m_toolbarStack = new QStackedWidget(container);

    m_toolbar = new ViewportToolbar(m_toolbarStack);
    m_toolbarStack->addWidget(m_toolbar);
    createToolbar();

    m_sketchToolbar = new SketchToolbar(m_toolbarStack);
    m_toolbarStack->addWidget(m_sketchToolbar);

    m_mainLayout->addWidget(m_toolbarStack);

    // Connect View > Toolbar toggle
    if (toolbarToggleAction()) {
        connect(toolbarToggleAction(), &QAction::toggled,
                m_toolbarStack, &QWidget::setVisible);
    }

    // Viewport stack (normal splitter vs sketch canvas)
    m_viewportStack = new QStackedWidget(container);

    // Normal mode: vertical splitter with viewport + CLI
    m_splitter = new QSplitter(Qt::Vertical, m_viewportStack);

    m_viewport = new ReducedViewport(m_splitter);
    m_splitter->addWidget(m_viewport);

    m_centralCli = new CliPanel(m_splitter);
    m_splitter->addWidget(m_centralCli);

    // Exit command in the central CLI panel closes the app
    connect(m_centralCli, &CliPanel::exitRequested,
            this, &QWidget::close);

    // Give most space to the CLI panel
    m_splitter->setStretchFactor(0, 1);   // viewport: small
    m_splitter->setStretchFactor(1, 3);   // CLI: large

    m_viewportStack->addWidget(m_splitter);

    // Sketch mode: 2D canvas
    m_sketchCanvas = new SketchCanvas(m_viewportStack);
    m_viewportStack->addWidget(m_sketchCanvas);

    m_mainLayout->addWidget(m_viewportStack, 1);  // stretch factor 1

    // Connect sketch toolbar
    connect(m_sketchToolbar, &SketchToolbar::toolSelected,
            this, &ReducedModeWindow::onSketchToolSelected);

    // Connect sketch canvas
    connect(m_sketchCanvas, &SketchCanvas::selectionChanged,
            this, &ReducedModeWindow::onSketchSelectionChanged);
    connect(m_sketchCanvas, &SketchCanvas::entityCreated,
            this, &ReducedModeWindow::onSketchEntityCreated);
    connect(m_sketchCanvas, &SketchCanvas::mousePositionChanged,
            this, [this](const QPointF& pos) {
        statusBar()->showMessage(
            tr("X: %1  Y: %2").arg(pos.x(), 0, 'f', 2).arg(pos.y(), 0, 'f', 2));
    });

    // Connect sketch action bar (Save/Cancel buttons in properties panel)
    if (sketchActionBar()) {
        connect(sketchActionBar(), &SketchActionBar::saveClicked,
                this, [this]() {
            // Save the sketch and exit sketch mode
            saveCurrentSketch();
            exitSketchMode();
        });
        connect(sketchActionBar(), &SketchActionBar::discardClicked,
                this, [this]() {
            // Discard changes and exit sketch mode
            discardCurrentSketch();
            exitSketchMode();
        });
    }

    // Timeline below the viewport stack
    m_timeline = new TimelineWidget(container);
    m_mainLayout->addWidget(m_timeline);
    createTimeline();

    setCentralWidget(container);
    finalizeLayout();

    // Initialize default parameters
    initDefaultParameters();

    connect(m_viewport, &ReducedViewport::viewportClicked,
            this, &ReducedModeWindow::onViewportClicked);

    // Hook into the View > Terminal toggle from MainWindow.
    // In Reduced Mode, toggling the terminal shows/hides the
    // central CLI panel (not the dock — we hide the dock entirely
    // since the central panel serves that role).
    if (auto* action = findChild<QAction*>(
            QString(), Qt::FindDirectChildrenOnly)) {
        // We need the specific toggle action — use the one we stored
    }

    // Connect to the toggle action created in MainWindow::createMenus()
    // The action is m_actionToggleTerminal, accessible via the menu.
    // We override the dock behavior: in Reduced Mode, toggle controls
    // the central CLI panel instead.
    connect(terminalToggleAction(), &QAction::toggled,
            this, &ReducedModeWindow::onTerminalToggled);

    // Start with terminal visible and action checked
    terminalToggleAction()->setChecked(true);

    // Hide the dock-based terminal — not needed in Reduced Mode
    // since we have the central one
    hideDockTerminal();

    // Show the diagnostic dialog on first launch
    showDiagnosticDialog();

    // Focus the CLI panel
    m_centralCli->focusInput();
}

void ReducedModeWindow::onTerminalToggled(bool visible)
{
    m_centralCli->setVisible(visible);
    if (visible) {
        m_centralCli->focusInput();
    }
}

void ReducedModeWindow::applyPreferences()
{
    // Call base class to apply standard bindings
    MainWindow::applyPreferences();

    // Reload sketch canvas key bindings
    if (m_sketchCanvas) {
        m_sketchCanvas->reloadBindings();
    }
}

void ReducedModeWindow::onViewportClicked()
{
    showDiagnosticDialog();
}

void ReducedModeWindow::showDiagnosticDialog()
{
    if (m_suppressDialog) {
        QApplication::beep();
        return;
    }

    DiagnosticDialog dlg(m_glInfo, this);
    int result = dlg.exec();

    if (result == 2) {
        QApplication::quit();
        return;
    }

    if (dlg.dontShowAgain()) {
        m_suppressDialog = true;
        m_viewport->setSuppressDialog(true);
    }
}

void ReducedModeWindow::createToolbar()
{
    // Buttons with icons above labels
    // Using standard freedesktop icons as placeholders

    // Create - start a 2D sketch or create construction geometry
    auto* sketchBtn = m_toolbar->addButton(
        QIcon::fromTheme(QStringLiteral("draw-freehand"),
                         style()->standardIcon(QStyle::SP_FileDialogDetailedView)),
        tr("Create"));
    auto* sketchDrop = sketchBtn->dropdown();
    sketchDrop->addButton(
        QIcon::fromTheme(QStringLiteral("draw-freehand"),
                         style()->standardIcon(QStyle::SP_FileDialogDetailedView)),
        tr("Sketch"));
    sketchDrop->addButton(
        QIcon::fromTheme(QStringLiteral("draw-rectangle"),
                         style()->standardIcon(QStyle::SP_FileDialogListView)),
        tr("Construction\nPlane"));
    sketchDrop->addButton(
        QIcon::fromTheme(QStringLiteral("draw-polygon"),
                         style()->standardIcon(QStyle::SP_FileDialogContentsView)),
        tr("Sketch on\nFace"));

    // Box - create primitive box
    auto* boxBtn = m_toolbar->addButton(
        QIcon::fromTheme(QStringLiteral("draw-cube"),
                         style()->standardIcon(QStyle::SP_ComputerIcon)),
        tr("Box"));
    auto* boxDrop = boxBtn->dropdown();
    boxDrop->addButton(
        QIcon::fromTheme(QStringLiteral("draw-cube"),
                         style()->standardIcon(QStyle::SP_ComputerIcon)),
        tr("Box"));
    boxDrop->addButton(
        QIcon::fromTheme(QStringLiteral("draw-cylinder"),
                         style()->standardIcon(QStyle::SP_DriveHDIcon)),
        tr("Cylinder"));
    boxDrop->addButton(
        QIcon::fromTheme(QStringLiteral("draw-sphere"),
                         style()->standardIcon(QStyle::SP_DialogHelpButton)),
        tr("Sphere"));
    boxDrop->addButton(
        QIcon::fromTheme(QStringLiteral("draw-donut"),
                         style()->standardIcon(QStyle::SP_DialogResetButton)),
        tr("Torus"));
    boxDrop->addButton(
        QIcon::fromTheme(QStringLiteral("draw-cone"),
                         style()->standardIcon(QStyle::SP_ArrowUp)),
        tr("Cone"));

    // Extrude - extrude sketch profiles
    auto* extrudeBtn = m_toolbar->addButton(
        QIcon::fromTheme(QStringLiteral("go-up"),
                         style()->standardIcon(QStyle::SP_ArrowUp)),
        tr("Extrude"));
    auto* extrudeDrop = extrudeBtn->dropdown();
    extrudeDrop->addButton(
        QIcon::fromTheme(QStringLiteral("go-up"),
                         style()->standardIcon(QStyle::SP_ArrowUp)),
        tr("Extrude"));
    extrudeDrop->addButton(
        QIcon::fromTheme(QStringLiteral("go-down"),
                         style()->standardIcon(QStyle::SP_ArrowDown)),
        tr("Cut\nExtrude"));

    // Revolve - revolve sketch profiles around an axis
    auto* revolveBtn = m_toolbar->addButton(
        QIcon::fromTheme(QStringLiteral("object-rotate-right"),
                         style()->standardIcon(QStyle::SP_BrowserReload)),
        tr("Revolve"));
    auto* revolveDrop = revolveBtn->dropdown();
    revolveDrop->addButton(
        QIcon::fromTheme(QStringLiteral("object-rotate-right"),
                         style()->standardIcon(QStyle::SP_BrowserReload)),
        tr("Revolve"));
    revolveDrop->addButton(
        QIcon::fromTheme(QStringLiteral("object-rotate-left"),
                         style()->standardIcon(QStyle::SP_BrowserStop)),
        tr("Cut\nRevolve"));

    m_toolbar->addSeparator();

    // Fillet - round edges
    auto* filletBtn = m_toolbar->addButton(
        QIcon::fromTheme(QStringLiteral("format-stroke-color"),
                         style()->standardIcon(QStyle::SP_DialogApplyButton)),
        tr("Fillet"));
    auto* filletDrop = filletBtn->dropdown();
    filletDrop->addButton(
        QIcon::fromTheme(QStringLiteral("format-stroke-color"),
                         style()->standardIcon(QStyle::SP_DialogApplyButton)),
        tr("Fillet"));
    filletDrop->addButton(
        QIcon::fromTheme(QStringLiteral("draw-line"),
                         style()->standardIcon(QStyle::SP_DialogOkButton)),
        tr("Chamfer"));

    // Hole - create holes
    auto* holeBtn = m_toolbar->addButton(
        QIcon::fromTheme(QStringLiteral("draw-donut"),
                         style()->standardIcon(QStyle::SP_DialogDiscardButton)),
        tr("Hole"));
    auto* holeDrop = holeBtn->dropdown();
    holeDrop->addButton(
        QIcon::fromTheme(QStringLiteral("draw-circle"),
                         style()->standardIcon(QStyle::SP_DialogDiscardButton)),
        tr("Simple\nHole"));
    holeDrop->addButton(
        QIcon::fromTheme(QStringLiteral("draw-ellipse"),
                         style()->standardIcon(QStyle::SP_DialogNoButton)),
        tr("Counter-\nbore"));
    holeDrop->addButton(
        QIcon::fromTheme(QStringLiteral("draw-polygon"),
                         style()->standardIcon(QStyle::SP_DialogYesButton)),
        tr("Counter-\nsink"));
    holeDrop->addButton(
        QIcon::fromTheme(QStringLiteral("draw-spiral"),
                         style()->standardIcon(QStyle::SP_DialogSaveButton)),
        tr("Threaded\nHole"));

    m_toolbar->addSeparator();

    // Move - transform objects
    auto* moveBtn = m_toolbar->addButton(
        QIcon::fromTheme(QStringLiteral("transform-move"),
                         style()->standardIcon(QStyle::SP_ArrowRight)),
        tr("Move"));
    auto* moveDrop = moveBtn->dropdown();
    moveDrop->addButton(
        QIcon::fromTheme(QStringLiteral("transform-move"),
                         style()->standardIcon(QStyle::SP_ArrowRight)),
        tr("Move/\nCopy"));
    moveDrop->addButton(
        QIcon::fromTheme(QStringLiteral("align-horizontal-center"),
                         style()->standardIcon(QStyle::SP_ToolBarHorizontalExtensionButton)),
        tr("Align"));

    // Mirror - mirror bodies or features
    auto* mirrorBtn = m_toolbar->addButton(
        QIcon::fromTheme(QStringLiteral("object-flip-horizontal"),
                         style()->standardIcon(QStyle::SP_ArrowBack)),
        tr("Mirror"));
    auto* mirrorDrop = mirrorBtn->dropdown();
    mirrorDrop->addButton(
        QIcon::fromTheme(QStringLiteral("object-flip-horizontal"),
                         style()->standardIcon(QStyle::SP_ArrowBack)),
        tr("Mirror"));
    mirrorDrop->addButton(
        QIcon::fromTheme(QStringLiteral("edit-copy"),
                         style()->standardIcon(QStyle::SP_FileDialogDetailedView)),
        tr("Pattern"));

    m_toolbar->addSeparator();

    // Parameters - manage object parameters/variables
    auto* paramsBtn = m_toolbar->addButton(
        QIcon::fromTheme(QStringLiteral("document-properties"),
                         style()->standardIcon(QStyle::SP_FileDialogInfoView)),
        tr("Params"),
        tr("Parameters"));
    auto* paramsDrop = paramsBtn->dropdown();
    paramsDrop->addButton(
        QIcon::fromTheme(QStringLiteral("document-properties"),
                         style()->standardIcon(QStyle::SP_FileDialogInfoView)),
        tr("Change\nParameters"));

    // Connect Params button to show parameters dialog
    connect(paramsBtn, &ToolbarButton::clicked,
            this, &ReducedModeWindow::showParametersDialog);

    // Connect Create button - enters sketch mode in reduced mode
    connect(sketchBtn, &ToolbarButton::clicked,
            this, &ReducedModeWindow::onCreateSketchClicked);

    // Most buttons are disabled until functionality is implemented
    // sketchBtn is now enabled - 2D sketching works without 3D viewport!
    boxBtn->setEnabled(false);
    extrudeBtn->setEnabled(false);
    revolveBtn->setEnabled(false);
    filletBtn->setEnabled(false);
    holeBtn->setEnabled(false);
    moveBtn->setEnabled(false);
    mirrorBtn->setEnabled(false);
    // paramsBtn is now enabled!
}

void ReducedModeWindow::createTimeline()
{
    // Add example timeline items to demonstrate scrolling behavior
    // These will be replaced with actual feature history
    m_timeline->addItem(TimelineFeature::Origin, tr("Origin"));
    m_timeline->addItem(TimelineFeature::Sketch, tr("Sketch1"));
    m_timeline->addItem(TimelineFeature::Extrude, tr("Extrude1"));
    m_timeline->addItem(TimelineFeature::Sketch, tr("Sketch2"));
    m_timeline->addItem(TimelineFeature::Extrude, tr("Extrude2"));
    m_timeline->addItem(TimelineFeature::Fillet, tr("Fillet1"));
    m_timeline->addItem(TimelineFeature::Hole, tr("Hole1"));
    m_timeline->addItem(TimelineFeature::Mirror, tr("Mirror1"));
    m_timeline->addItem(TimelineFeature::Chamfer, tr("Chamfer1"));
    m_timeline->addItem(TimelineFeature::Pattern, tr("Pattern1"));

    // Connect timeline item selection to properties panel
    connect(m_timeline, &TimelineWidget::itemClicked,
            this, &ReducedModeWindow::showFeatureProperties);
}

void ReducedModeWindow::initDefaultParameters()
{
    // Initialize with some example user parameters
    m_parameters.clear();

    Parameter width;
    width.name = QStringLiteral("width");
    width.expression = QStringLiteral("50");
    width.value = 50.0;
    width.unit = unitSuffix();
    width.comment = tr("Base width dimension");
    width.isUserParam = true;
    m_parameters.append(width);

    Parameter height;
    height.name = QStringLiteral("height");
    height.expression = QStringLiteral("30");
    height.value = 30.0;
    height.unit = unitSuffix();
    height.comment = tr("Base height dimension");
    height.isUserParam = true;
    m_parameters.append(height);

    Parameter depth;
    depth.name = QStringLiteral("depth");
    depth.expression = QStringLiteral("20");
    depth.value = 20.0;
    depth.unit = unitSuffix();
    depth.comment = tr("Base depth dimension");
    depth.isUserParam = true;
    m_parameters.append(depth);

    Parameter radius;
    radius.name = QStringLiteral("radius");
    radius.expression = QStringLiteral("5");
    radius.value = 5.0;
    radius.unit = unitSuffix();
    radius.comment = tr("Default fillet radius");
    radius.isUserParam = true;
    m_parameters.append(radius);

    Parameter angle;
    angle.name = QStringLiteral("angle");
    angle.expression = QStringLiteral("45");
    angle.value = 45.0;
    angle.unit = QStringLiteral("deg");
    angle.comment = tr("Default angle");
    angle.isUserParam = true;
    m_parameters.append(angle);

    // Example object parameters (from features - read-only)
    Parameter extrude1Dist;
    extrude1Dist.name = QStringLiteral("Extrude1_Distance");
    extrude1Dist.expression = QStringLiteral("10");
    extrude1Dist.value = 10.0;
    extrude1Dist.unit = unitSuffix();
    extrude1Dist.comment = tr("Extrude1 distance");
    extrude1Dist.isUserParam = false;
    m_parameters.append(extrude1Dist);

    Parameter fillet1Rad;
    fillet1Rad.name = QStringLiteral("Fillet1_Radius");
    fillet1Rad.expression = QStringLiteral("radius");
    fillet1Rad.value = 5.0;
    fillet1Rad.unit = unitSuffix();
    fillet1Rad.comment = tr("Fillet1 radius (uses 'radius' param)");
    fillet1Rad.isUserParam = false;
    m_parameters.append(fillet1Rad);
}

QMap<QString, double> ReducedModeWindow::parameterValues() const
{
    QMap<QString, double> values;
    for (const auto& p : m_parameters) {
        values[p.name] = p.value;
    }
    return values;
}

void ReducedModeWindow::showParametersDialog()
{
    ParametersDialog dlg(this);
    dlg.setDefaultUnit(unitSuffix());
    dlg.setParameters(m_parameters);

    connect(&dlg, &ParametersDialog::parametersChanged,
            this, &ReducedModeWindow::onParametersChanged);

    dlg.exec();
}

void ReducedModeWindow::onParametersChanged(const QList<Parameter>& params)
{
    m_parameters = params;

    // TODO: Re-evaluate all features that use these parameters
    // and regenerate the model

    // For now, just update the status bar
    statusBar()->showMessage(tr("Parameters updated"), 3000);
}

void ReducedModeWindow::showFeatureProperties(int index)
{
    QTreeWidget* propsTree = propertiesTree();
    if (!propsTree)
        return;

    propsTree->clear();

    if (index < 0 || index >= m_timeline->itemCount())
        return;

    TimelineFeature feature = m_timeline->featureAt(index);
    QString featureName = m_timeline->nameAt(index);
    QString units = unitSuffix();

    // Helper to set property (column 0) and value (column 1), read-only
    auto setProperty = [](QTreeWidgetItem* item, const QString& prop, const QString& value) {
        item->setText(0, prop);
        item->setText(1, value);
        item->setToolTip(0, prop);
        item->setToolTip(1, value);
    };

    // Helper to set property and editable value (no units)
    auto setEditableProperty = [](QTreeWidgetItem* item, const QString& prop, const QString& value) {
        item->setText(0, prop);
        item->setText(1, value);
        item->setToolTip(0, prop);
        item->setToolTip(1, value);
        item->setFlags(item->flags() | Qt::ItemIsEditable);
    };

    // Helper for section headers (spans both columns)
    auto setHeader = [](QTreeWidgetItem* item, const QString& text) {
        item->setText(0, text);
        item->setToolTip(0, text);
    };

    // Helper to create dropdown property
    auto setDropdownProperty = [](QTreeWidgetItem* item, const QString& prop,
                                  const QStringList& options, int currentIndex) {
        item->setText(0, prop);
        item->setText(1, options.value(currentIndex));
        item->setToolTip(0, prop);
        item->setToolTip(1, options.value(currentIndex));
        item->setData(1, Qt::UserRole, QStringLiteral("dropdown"));
        item->setData(1, Qt::UserRole + 1, options);
        item->setData(1, Qt::UserRole + 2, currentIndex);
    };

    // Get parameters from the document for formula support
    QMap<QString, double> params = parameterValues();

    // Helper to create formula-enabled property with fx button
    auto setFormulaProperty = [propsTree, &params](QTreeWidgetItem* item, const QString& prop,
                                                    const QString& expr, const QString& unitSuffix) {
        item->setText(0, prop);
        item->setToolTip(0, prop);

        auto* formulaField = new FormulaField(propsTree);
        formulaField->setPropertyName(prop);
        formulaField->setUnitSuffix(unitSuffix);
        formulaField->setParameters(params);
        formulaField->setExpression(expr);

        propsTree->setItemWidget(item, 1, formulaField);
    };

    // Feature name (editable)
    auto* nameItem = new QTreeWidgetItem(propsTree);
    setEditableProperty(nameItem, tr("Name"), featureName);

    // Feature type (read-only)
    auto* typeItem = new QTreeWidgetItem(propsTree);
    QString typeName;
    switch (feature) {
    case TimelineFeature::Origin:    typeName = tr("Origin"); break;
    case TimelineFeature::Sketch:    typeName = tr("Sketch"); break;
    case TimelineFeature::Extrude:   typeName = tr("Extrude"); break;
    case TimelineFeature::Revolve:   typeName = tr("Revolve"); break;
    case TimelineFeature::Fillet:    typeName = tr("Fillet"); break;
    case TimelineFeature::Chamfer:   typeName = tr("Chamfer"); break;
    case TimelineFeature::Hole:      typeName = tr("Hole"); break;
    case TimelineFeature::Mirror:    typeName = tr("Mirror"); break;
    case TimelineFeature::Pattern:   typeName = tr("Pattern"); break;
    case TimelineFeature::Box:       typeName = tr("Box"); break;
    case TimelineFeature::Cylinder:  typeName = tr("Cylinder"); break;
    case TimelineFeature::Sphere:    typeName = tr("Sphere"); break;
    case TimelineFeature::Move:      typeName = tr("Move"); break;
    case TimelineFeature::Join:      typeName = tr("Join"); break;
    case TimelineFeature::Cut:       typeName = tr("Cut"); break;
    case TimelineFeature::Intersect: typeName = tr("Intersect"); break;
    }
    setProperty(typeItem, tr("Type"), typeName);

    // Feature-specific properties (placeholder values for now)
    auto* propsHeader = new QTreeWidgetItem(propsTree);
    setHeader(propsHeader, tr("Properties"));
    propsHeader->setExpanded(true);

    switch (feature) {
    case TimelineFeature::Origin:
        {
            auto* item = new QTreeWidgetItem(propsHeader);
            setProperty(item, tr("Position"), tr("(0, 0, 0)"));
        }
        break;

    case TimelineFeature::Sketch:
        {
            auto* planeItem = new QTreeWidgetItem(propsHeader);
            setDropdownProperty(planeItem, tr("Plane"),
                {tr("XY"), tr("XZ"), tr("YZ")}, 0);
            auto* entitiesItem = new QTreeWidgetItem(propsHeader);
            setProperty(entitiesItem, tr("Entities"), tr("5"));
            auto* constraintsItem = new QTreeWidgetItem(propsHeader);
            setProperty(constraintsItem, tr("Constraints"), tr("8"));
        }
        break;

    case TimelineFeature::Extrude:
        {
            auto* distItem = new QTreeWidgetItem(propsHeader);
            setFormulaProperty(distItem, tr("Distance"), QStringLiteral("10"), units);
            auto* dirItem = new QTreeWidgetItem(propsHeader);
            setDropdownProperty(dirItem, tr("Direction"),
                {tr("One Side"), tr("Two Sides"), tr("Symmetric")}, 0);
            auto* opItem = new QTreeWidgetItem(propsHeader);
            setDropdownProperty(opItem, tr("Operation"),
                {tr("Join"), tr("Cut"), tr("Intersect"), tr("New Body")}, 0);
        }
        break;

    case TimelineFeature::Revolve:
        {
            auto* angleItem = new QTreeWidgetItem(propsHeader);
            setFormulaProperty(angleItem, tr("Angle"), QStringLiteral("360"), QStringLiteral("°"));
            auto* axisItem = new QTreeWidgetItem(propsHeader);
            setDropdownProperty(axisItem, tr("Axis"),
                {tr("X Axis"), tr("Y Axis"), tr("Z Axis")}, 0);
        }
        break;

    case TimelineFeature::Fillet:
        {
            auto* radiusItem = new QTreeWidgetItem(propsHeader);
            setFormulaProperty(radiusItem, tr("Radius"), QStringLiteral("radius"), units);
            auto* edgesItem = new QTreeWidgetItem(propsHeader);
            setProperty(edgesItem, tr("Edges"), tr("4"));
        }
        break;

    case TimelineFeature::Chamfer:
        {
            auto* distItem = new QTreeWidgetItem(propsHeader);
            setFormulaProperty(distItem, tr("Distance"), QStringLiteral("1"), units);
            auto* edgesItem = new QTreeWidgetItem(propsHeader);
            setProperty(edgesItem, tr("Edges"), tr("2"));
        }
        break;

    case TimelineFeature::Hole:
        {
            auto* diamItem = new QTreeWidgetItem(propsHeader);
            setFormulaProperty(diamItem, tr("Diameter"), QStringLiteral("5"), units);
            auto* depthItem = new QTreeWidgetItem(propsHeader);
            setDropdownProperty(depthItem, tr("Depth"),
                {tr("Through All"), tr("To Depth"), tr("To Face")}, 0);
            auto* typeHole = new QTreeWidgetItem(propsHeader);
            setDropdownProperty(typeHole, tr("Hole Type"),
                {tr("Simple"), tr("Counterbore"), tr("Countersink"), tr("Threaded")}, 0);
        }
        break;

    case TimelineFeature::Mirror:
        {
            auto* planeItem = new QTreeWidgetItem(propsHeader);
            setDropdownProperty(planeItem, tr("Mirror Plane"),
                {tr("XY"), tr("XZ"), tr("YZ")}, 2);
            auto* bodiesItem = new QTreeWidgetItem(propsHeader);
            setProperty(bodiesItem, tr("Bodies"), tr("1"));
        }
        break;

    case TimelineFeature::Pattern:
        {
            auto* typeP = new QTreeWidgetItem(propsHeader);
            setDropdownProperty(typeP, tr("Pattern Type"),
                {tr("Rectangular"), tr("Circular")}, 0);
            auto* countItem = new QTreeWidgetItem(propsHeader);
            setEditableProperty(countItem, tr("Count"), tr("3 x 2"));
            auto* spacingItem = new QTreeWidgetItem(propsHeader);
            setEditableProperty(spacingItem, tr("Spacing"), QStringLiteral("15 x 10 %1").arg(units));
        }
        break;

    case TimelineFeature::Box:
        {
            auto* dimItem = new QTreeWidgetItem(propsHeader);
            setEditableProperty(dimItem, tr("Dimensions"), QStringLiteral("50 x 30 x 20 %1").arg(units));
        }
        break;

    case TimelineFeature::Cylinder:
        {
            auto* diamItem = new QTreeWidgetItem(propsHeader);
            setFormulaProperty(diamItem, tr("Diameter"), QStringLiteral("width / 2"), units);
            auto* heightItem = new QTreeWidgetItem(propsHeader);
            setFormulaProperty(heightItem, tr("Height"), QStringLiteral("height + depth"), units);
        }
        break;

    case TimelineFeature::Sphere:
        {
            auto* diamItem = new QTreeWidgetItem(propsHeader);
            setFormulaProperty(diamItem, tr("Diameter"), QStringLiteral("25"), units);
        }
        break;

    case TimelineFeature::Move:
        {
            auto* transItem = new QTreeWidgetItem(propsHeader);
            setEditableProperty(transItem, tr("Translation"), QStringLiteral("(10, 5, 0) %1").arg(units));
            auto* rotItem = new QTreeWidgetItem(propsHeader);
            setFormulaProperty(rotItem, tr("Rotation"), QStringLiteral("0"), QStringLiteral("°"));
        }
        break;

    case TimelineFeature::Join:
    case TimelineFeature::Cut:
    case TimelineFeature::Intersect:
        {
            auto* bodiesItem = new QTreeWidgetItem(propsHeader);
            setProperty(bodiesItem, tr("Target Bodies"), tr("2"));
            auto* toolItem = new QTreeWidgetItem(propsHeader);
            setProperty(toolItem, tr("Tool Bodies"), tr("1"));
        }
        break;
    }

    propsTree->expandAll();

    // Connect double-click to show dropdown for dropdown properties
    disconnect(propsTree, &QTreeWidget::itemDoubleClicked, nullptr, nullptr);
    connect(propsTree, &QTreeWidget::itemDoubleClicked,
            this, [this, propsTree](QTreeWidgetItem* item, int column) {
        if (column != 1)
            return;

        if (item->data(1, Qt::UserRole).toString() != QStringLiteral("dropdown"))
            return;

        QStringList options = item->data(1, Qt::UserRole + 1).toStringList();
        int currentIndex = item->data(1, Qt::UserRole + 2).toInt();

        auto* combo = new QComboBox(propsTree);
        combo->addItems(options);
        combo->setCurrentIndex(currentIndex);

        propsTree->setItemWidget(item, 1, combo);
        combo->showPopup();

        connect(combo, &QComboBox::activated, this,
                [this, item, combo, propsTree](int index) {
            item->setText(1, combo->currentText());
            item->setToolTip(1, combo->currentText());
            item->setData(1, Qt::UserRole + 2, index);
            QTimer::singleShot(0, this, [propsTree, item]() {
                propsTree->setItemWidget(item, 1, nullptr);
            });
        });
    });
}

void ReducedModeWindow::onCreateSketchClicked()
{
    // Show plane selection dialog
    SketchPlaneDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) {
        return;  // User cancelled
    }

    SketchPlane plane = dialog.selectedPlane();
    double offset = dialog.offset();

    // Store offset for display in properties
    m_pendingSketchOffset = offset;

    enterSketchMode(plane);
}

void ReducedModeWindow::enterSketchMode(SketchPlane plane)
{
    if (m_inSketchMode) return;

    m_inSketchMode = true;

    // Switch to sketch toolbar
    m_toolbarStack->setCurrentWidget(m_sketchToolbar);

    // Show the action bar in properties panel and reset its state
    if (sketchActionBar()) {
        sketchActionBar()->reset();
    }
    setSketchActionBarVisible(true);

    // Switch to sketch canvas
    m_sketchCanvas->setSketchPlane(plane);
    m_sketchCanvas->clear();
    m_sketchCanvas->resetView();
    m_viewportStack->setCurrentWidget(m_sketchCanvas);

    // Add new sketch to timeline
    int sketchCount = 0;
    for (int i = 0; i < m_timeline->itemCount(); ++i) {
        if (m_timeline->featureAt(i) == TimelineFeature::Sketch) {
            ++sketchCount;
        }
    }
    QString sketchName = tr("Sketch%1").arg(sketchCount + 1);
    m_timeline->addItem(TimelineFeature::Sketch, sketchName);

    // Select the new sketch in timeline
    m_timeline->setSelectedIndex(m_timeline->itemCount() - 1);

    // Update properties to show sketch settings
    QTreeWidget* propsTree = propertiesTree();
    if (propsTree) {
        propsTree->clear();

        // Sketch name
        auto* nameItem = new QTreeWidgetItem(propsTree);
        nameItem->setText(0, tr("Name"));
        nameItem->setText(1, sketchName);
        nameItem->setFlags(nameItem->flags() | Qt::ItemIsEditable);

        // Plane selection
        auto* planeItem = new QTreeWidgetItem(propsTree);
        planeItem->setText(0, tr("Plane"));
        QStringList planes = {tr("XY"), tr("XZ"), tr("YZ")};
        int planeIdx = static_cast<int>(plane);
        planeItem->setText(1, planes.value(planeIdx));
        planeItem->setData(1, Qt::UserRole, QStringLiteral("dropdown"));
        planeItem->setData(1, Qt::UserRole + 1, planes);
        planeItem->setData(1, Qt::UserRole + 2, planeIdx);

        // Plane offset
        auto* offsetItem = new QTreeWidgetItem(propsTree);
        offsetItem->setText(0, tr("Offset"));
        offsetItem->setText(1, tr("%1 mm").arg(m_pendingSketchOffset, 0, 'g', 6));

        // Grid settings
        auto* gridHeader = new QTreeWidgetItem(propsTree);
        gridHeader->setText(0, tr("Grid"));

        auto* showGridItem = new QTreeWidgetItem(gridHeader);
        showGridItem->setText(0, tr("Show Grid"));
        showGridItem->setText(1, m_sketchCanvas->isGridVisible() ? tr("Yes") : tr("No"));
        showGridItem->setData(1, Qt::UserRole, QStringLiteral("dropdown"));
        showGridItem->setData(1, Qt::UserRole + 1, QStringList{tr("Yes"), tr("No")});
        showGridItem->setData(1, Qt::UserRole + 2, m_sketchCanvas->isGridVisible() ? 0 : 1);

        auto* snapItem = new QTreeWidgetItem(gridHeader);
        snapItem->setText(0, tr("Snap to Grid"));
        snapItem->setText(1, m_sketchCanvas->snapToGrid() ? tr("Yes") : tr("No"));
        snapItem->setData(1, Qt::UserRole, QStringLiteral("dropdown"));
        snapItem->setData(1, Qt::UserRole + 1, QStringList{tr("Yes"), tr("No")});
        snapItem->setData(1, Qt::UserRole + 2, m_sketchCanvas->snapToGrid() ? 0 : 1);

        auto* spacingItem = new QTreeWidgetItem(gridHeader);
        spacingItem->setText(0, tr("Grid Spacing"));
        spacingItem->setText(1, QStringLiteral("%1 %2")
                             .arg(m_sketchCanvas->gridSpacing())
                             .arg(unitSuffix()));
        spacingItem->setFlags(spacingItem->flags() | Qt::ItemIsEditable);

        // Entities count
        auto* entitiesItem = new QTreeWidgetItem(propsTree);
        entitiesItem->setText(0, tr("Entities"));
        entitiesItem->setText(1, QString::number(m_sketchCanvas->entities().size()));

        propsTree->expandAll();
    }

    // Update status bar
    statusBar()->showMessage(tr("Sketch mode - Draw entities or press Escape to finish"));

    // Focus canvas
    m_sketchCanvas->setFocus();
}

void ReducedModeWindow::exitSketchMode()
{
    if (!m_inSketchMode) return;

    m_inSketchMode = false;

    // Switch back to normal toolbar
    m_toolbarStack->setCurrentWidget(m_toolbar);

    // Hide the Save/Cancel action bar
    setSketchActionBarVisible(false);

    // Switch back to normal viewport (splitter with disabled viewport + CLI)
    m_viewportStack->setCurrentWidget(m_splitter);

    // Clear properties
    if (QTreeWidget* propsTree = propertiesTree()) {
        propsTree->clear();
    }

    // Deselect timeline item
    m_timeline->setSelectedIndex(-1);

    // Update status bar
    statusBar()->showMessage(tr("Sketch finished"), 3000);

    // Focus CLI
    m_centralCli->focusInput();
}

void ReducedModeWindow::onSketchToolSelected(SketchTool tool)
{
    m_sketchCanvas->setActiveTool(tool);

    // Update status bar with tool hint
    QString hint;
    switch (tool) {
    case SketchTool::Select:
        hint = tr("Click to select entities, drag to move");
        break;
    case SketchTool::Line:
        hint = tr("Click to start line, click again to end");
        break;
    case SketchTool::Rectangle:
        hint = tr("Click and drag to draw rectangle");
        break;
    case SketchTool::Circle:
        hint = tr("Click center, drag to set radius");
        break;
    case SketchTool::Arc:
        hint = tr("Click center, drag to set radius and arc");
        break;
    case SketchTool::Point:
        hint = tr("Click to place construction point");
        break;
    case SketchTool::Dimension:
        hint = tr("Click two points or an entity to add dimension");
        break;
    case SketchTool::Constraint:
        hint = tr("Select entities to add constraints");
        break;
    default:
        hint = tr("Select a tool to start drawing");
    }
    statusBar()->showMessage(hint);
}

void ReducedModeWindow::onSketchSelectionChanged(int entityId)
{
    if (entityId < 0) {
        // Deselected - show sketch properties
        if (m_inSketchMode) {
            enterSketchMode(m_sketchCanvas->sketchPlane());
        }
        return;
    }

    // Show entity properties
    showSketchEntityProperties(entityId);
}

void ReducedModeWindow::onSketchEntityCreated(int entityId)
{
    // Update entity count in properties
    if (QTreeWidget* propsTree = propertiesTree()) {
        for (int i = 0; i < propsTree->topLevelItemCount(); ++i) {
            auto* item = propsTree->topLevelItem(i);
            if (item && item->text(0) == tr("Entities")) {
                item->setText(1, QString::number(m_sketchCanvas->entities().size()));
                break;
            }
        }
    }

    // Select the new entity
    showSketchEntityProperties(entityId);
}

void ReducedModeWindow::showSketchEntityProperties(int entityId)
{
    const SketchEntity* entity = nullptr;
    for (const auto& e : m_sketchCanvas->entities()) {
        if (e.id == entityId) {
            entity = &e;
            break;
        }
    }

    if (!entity) return;

    QTreeWidget* propsTree = propertiesTree();
    if (!propsTree) return;

    propsTree->clear();
    QString units = unitSuffix();

    // Entity type
    auto* typeItem = new QTreeWidgetItem(propsTree);
    typeItem->setText(0, tr("Type"));
    QString typeName;
    switch (entity->type) {
    case SketchEntityType::Point:     typeName = tr("Point"); break;
    case SketchEntityType::Line:      typeName = tr("Line"); break;
    case SketchEntityType::Rectangle: typeName = tr("Rectangle"); break;
    case SketchEntityType::Circle:    typeName = tr("Circle"); break;
    case SketchEntityType::Arc:       typeName = tr("Arc"); break;
    case SketchEntityType::Spline:    typeName = tr("Spline"); break;
    case SketchEntityType::Text:      typeName = tr("Text"); break;
    case SketchEntityType::Dimension: typeName = tr("Dimension"); break;
    }
    typeItem->setText(1, typeName);

    // Entity ID
    auto* idItem = new QTreeWidgetItem(propsTree);
    idItem->setText(0, tr("ID"));
    idItem->setText(1, QString::number(entity->id));

    // Geometry header
    auto* geomHeader = new QTreeWidgetItem(propsTree);
    geomHeader->setText(0, tr("Geometry"));

    // Entity-specific properties
    switch (entity->type) {
    case SketchEntityType::Point:
        if (!entity->points.isEmpty()) {
            auto* posItem = new QTreeWidgetItem(geomHeader);
            posItem->setText(0, tr("Position"));
            posItem->setText(1, QStringLiteral("(%1, %2) %3")
                             .arg(entity->points[0].x(), 0, 'f', 2)
                             .arg(entity->points[0].y(), 0, 'f', 2)
                             .arg(units));
            posItem->setFlags(posItem->flags() | Qt::ItemIsEditable);
        }
        break;

    case SketchEntityType::Line:
        if (entity->points.size() >= 2) {
            auto* p1Item = new QTreeWidgetItem(geomHeader);
            p1Item->setText(0, tr("Start"));
            p1Item->setText(1, QStringLiteral("(%1, %2) %3")
                             .arg(entity->points[0].x(), 0, 'f', 2)
                             .arg(entity->points[0].y(), 0, 'f', 2)
                             .arg(units));
            p1Item->setFlags(p1Item->flags() | Qt::ItemIsEditable);

            auto* p2Item = new QTreeWidgetItem(geomHeader);
            p2Item->setText(0, tr("End"));
            p2Item->setText(1, QStringLiteral("(%1, %2) %3")
                             .arg(entity->points[1].x(), 0, 'f', 2)
                             .arg(entity->points[1].y(), 0, 'f', 2)
                             .arg(units));
            p2Item->setFlags(p2Item->flags() | Qt::ItemIsEditable);

            auto* lenItem = new QTreeWidgetItem(geomHeader);
            lenItem->setText(0, tr("Length"));
            double len = QLineF(entity->points[0], entity->points[1]).length();
            lenItem->setText(1, QStringLiteral("%1 %2").arg(len, 0, 'f', 2).arg(units));
            lenItem->setFlags(lenItem->flags() | Qt::ItemIsEditable);
        }
        break;

    case SketchEntityType::Rectangle:
        if (entity->points.size() >= 2) {
            auto* p1Item = new QTreeWidgetItem(geomHeader);
            p1Item->setText(0, tr("Corner 1"));
            p1Item->setText(1, QStringLiteral("(%1, %2) %3")
                             .arg(entity->points[0].x(), 0, 'f', 2)
                             .arg(entity->points[0].y(), 0, 'f', 2)
                             .arg(units));

            auto* p2Item = new QTreeWidgetItem(geomHeader);
            p2Item->setText(0, tr("Corner 2"));
            p2Item->setText(1, QStringLiteral("(%1, %2) %3")
                             .arg(entity->points[1].x(), 0, 'f', 2)
                             .arg(entity->points[1].y(), 0, 'f', 2)
                             .arg(units));

            auto* widthItem = new QTreeWidgetItem(geomHeader);
            widthItem->setText(0, tr("Width"));
            double w = qAbs(entity->points[1].x() - entity->points[0].x());
            widthItem->setText(1, QStringLiteral("%1 %2").arg(w, 0, 'f', 2).arg(units));
            widthItem->setFlags(widthItem->flags() | Qt::ItemIsEditable);

            auto* heightItem = new QTreeWidgetItem(geomHeader);
            heightItem->setText(0, tr("Height"));
            double h = qAbs(entity->points[1].y() - entity->points[0].y());
            heightItem->setText(1, QStringLiteral("%1 %2").arg(h, 0, 'f', 2).arg(units));
            heightItem->setFlags(heightItem->flags() | Qt::ItemIsEditable);
        }
        break;

    case SketchEntityType::Circle:
        if (!entity->points.isEmpty()) {
            auto* centerItem = new QTreeWidgetItem(geomHeader);
            centerItem->setText(0, tr("Center"));
            centerItem->setText(1, QStringLiteral("(%1, %2) %3")
                                .arg(entity->points[0].x(), 0, 'f', 2)
                                .arg(entity->points[0].y(), 0, 'f', 2)
                                .arg(units));
            centerItem->setFlags(centerItem->flags() | Qt::ItemIsEditable);

            auto* radiusItem = new QTreeWidgetItem(geomHeader);
            radiusItem->setText(0, tr("Radius"));
            radiusItem->setText(1, QStringLiteral("%1 %2").arg(entity->radius, 0, 'f', 2).arg(units));
            radiusItem->setFlags(radiusItem->flags() | Qt::ItemIsEditable);

            auto* diamItem = new QTreeWidgetItem(geomHeader);
            diamItem->setText(0, tr("Diameter"));
            diamItem->setText(1, QStringLiteral("%1 %2").arg(entity->radius * 2, 0, 'f', 2).arg(units));
            diamItem->setFlags(diamItem->flags() | Qt::ItemIsEditable);
        }
        break;

    case SketchEntityType::Arc:
        if (!entity->points.isEmpty()) {
            auto* centerItem = new QTreeWidgetItem(geomHeader);
            centerItem->setText(0, tr("Center"));
            centerItem->setText(1, QStringLiteral("(%1, %2) %3")
                                .arg(entity->points[0].x(), 0, 'f', 2)
                                .arg(entity->points[0].y(), 0, 'f', 2)
                                .arg(units));

            auto* radiusItem = new QTreeWidgetItem(geomHeader);
            radiusItem->setText(0, tr("Radius"));
            radiusItem->setText(1, QStringLiteral("%1 %2").arg(entity->radius, 0, 'f', 2).arg(units));
            radiusItem->setFlags(radiusItem->flags() | Qt::ItemIsEditable);

            auto* startItem = new QTreeWidgetItem(geomHeader);
            startItem->setText(0, tr("Start Angle"));
            startItem->setText(1, QStringLiteral("%1°").arg(entity->startAngle, 0, 'f', 1));
            startItem->setFlags(startItem->flags() | Qt::ItemIsEditable);

            auto* sweepItem = new QTreeWidgetItem(geomHeader);
            sweepItem->setText(0, tr("Sweep Angle"));
            sweepItem->setText(1, QStringLiteral("%1°").arg(entity->sweepAngle, 0, 'f', 1));
            sweepItem->setFlags(sweepItem->flags() | Qt::ItemIsEditable);
        }
        break;

    default:
        break;
    }

    // Constraints
    auto* constraintItem = new QTreeWidgetItem(propsTree);
    constraintItem->setText(0, tr("Constrained"));
    constraintItem->setText(1, entity->constrained ? tr("Yes") : tr("No"));

    propsTree->expandAll();
}

void ReducedModeWindow::saveCurrentSketch()
{
    // Save the sketch entities to the document
    // For now, just mark the sketch as saved in the timeline
    if (m_timeline->itemCount() > 0) {
        int lastIdx = m_timeline->itemCount() - 1;
        if (m_timeline->featureAt(lastIdx) == TimelineFeature::Sketch) {
            // The sketch is already in the timeline - entities are stored
            // TODO: Persist sketch entities to the document
            statusBar()->showMessage(
                tr("Sketch '%1' saved with %2 entities")
                    .arg(m_timeline->nameAt(lastIdx))
                    .arg(m_sketchCanvas->entities().size()),
                3000);
        }
    }
}

void ReducedModeWindow::discardCurrentSketch()
{
    // Discard the sketch - remove from timeline if it was newly created
    if (m_timeline->itemCount() > 0) {
        int lastIdx = m_timeline->itemCount() - 1;
        if (m_timeline->featureAt(lastIdx) == TimelineFeature::Sketch) {
            // Check if sketch has any entities
            if (m_sketchCanvas->entities().isEmpty()) {
                // Empty sketch - remove it from timeline
                m_timeline->removeItem(lastIdx);
                statusBar()->showMessage(tr("Empty sketch discarded"), 3000);
            } else {
                // Has entities but user cancelled - ask what to do
                // For now, just warn and keep the sketch
                statusBar()->showMessage(
                    tr("Sketch changes discarded (%1 entities)")
                        .arg(m_sketchCanvas->entities().size()),
                    3000);
                // TODO: Restore original sketch state if editing existing sketch
            }
        }
    }
}

}  // namespace hobbycad

