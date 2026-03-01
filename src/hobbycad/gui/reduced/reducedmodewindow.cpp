// =====================================================================
//  src/hobbycad/gui/reduced/reducedmodewindow.cpp — Reduced Mode window
// =====================================================================

#include "reducedmodewindow.h"
#include "reducedviewport.h"
#include "diagnosticdialog.h"
#include "gui/changelogpanel.h"
#include "gui/clipanel.h"
#include "gui/modeltoolbar.h"
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
#include <QtMath>
#include <cmath>

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

    m_toolbar = new ModelToolbar(m_toolbarStack);
    m_toolbarStack->addWidget(m_toolbar);

    // Connect ModelToolbar signals
    connect(m_toolbar, &ModelToolbar::createSketchClicked,
            this, &ReducedModeWindow::onCreateSketchClicked);
    connect(m_toolbar, &ModelToolbar::parametersClicked,
            this, &ReducedModeWindow::showParametersDialog);

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
    m_sketchCanvas->setUnitSuffix(unitSuffix());
    m_viewportStack->addWidget(m_sketchCanvas);

    m_mainLayout->addWidget(m_viewportStack, 1);  // stretch factor 1

    // Connect sketch toolbar
    connect(m_sketchToolbar, &SketchToolbar::toolChanged,
            this, &ReducedModeWindow::onSketchToolSelected);

    // Connect sketch canvas
    connect(m_sketchCanvas, &SketchCanvas::selectionChanged,
            this, &ReducedModeWindow::onSketchSelectionChanged);
    connect(m_sketchCanvas, &SketchCanvas::entityCreated,
            this, &ReducedModeWindow::onSketchEntityCreated);
    connect(m_sketchCanvas, &SketchCanvas::constraintSelectionChanged,
            this, &ReducedModeWindow::onConstraintSelectionChanged);
    connect(m_sketchCanvas, &SketchCanvas::mousePositionChanged,
            this, [this](const QPointF& pos) {
        statusBar()->showMessage(
            tr("X: %1  Y: %2").arg(pos.x(), 0, 'f', 2).arg(pos.y(), 0, 'f', 2));
    });

    // Connect undo/redo actions to sketch canvas
    if (undoAction()) {
        connect(undoAction(), &QAction::triggered,
                m_sketchCanvas, &SketchCanvas::undo);
        connect(m_sketchCanvas, &SketchCanvas::undoAvailabilityChanged,
                undoAction(), &QAction::setEnabled);
    }
    if (redoAction()) {
        connect(redoAction(), &QAction::triggered,
                m_sketchCanvas, &SketchCanvas::redo);
        connect(m_sketchCanvas, &SketchCanvas::redoAvailabilityChanged,
                redoAction(), &QAction::setEnabled);
    }

    // Connect changelog panel to sketch canvas
    if (changelogPanel()) {
        changelogPanel()->setSketchCanvas(m_sketchCanvas);
    }

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

    // Connect property editing for sketch entities
    if (QTreeWidget* propsTree = propertiesTree()) {
        connect(propsTree, &QTreeWidget::itemChanged,
                this, &ReducedModeWindow::onSketchPropertyItemChanged);
    }

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

// createToolbar() is no longer needed - ModelToolbar handles all button setup internally

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
    width.name = "width";
    width.expression = "50";
    width.value = 50.0;
    width.unit = unitSuffix().toStdString();
    width.comment = tr("Base width dimension").toStdString();
    width.isUserParam = true;
    m_parameters.append(width);

    Parameter height;
    height.name = "height";
    height.expression = "30";
    height.value = 30.0;
    height.unit = unitSuffix().toStdString();
    height.comment = tr("Base height dimension").toStdString();
    height.isUserParam = true;
    m_parameters.append(height);

    Parameter depth;
    depth.name = "depth";
    depth.expression = "20";
    depth.value = 20.0;
    depth.unit = unitSuffix().toStdString();
    depth.comment = tr("Base depth dimension").toStdString();
    depth.isUserParam = true;
    m_parameters.append(depth);

    Parameter radius;
    radius.name = "radius";
    radius.expression = "5";
    radius.value = 5.0;
    radius.unit = unitSuffix().toStdString();
    radius.comment = tr("Default fillet radius").toStdString();
    radius.isUserParam = true;
    m_parameters.append(radius);

    Parameter angle;
    angle.name = "angle";
    angle.expression = "45";
    angle.value = 45.0;
    angle.unit = "deg";
    angle.comment = tr("Default angle").toStdString();
    angle.isUserParam = true;
    m_parameters.append(angle);

    // Example object parameters (from features - read-only)
    Parameter extrude1Dist;
    extrude1Dist.name = "Extrude1_Distance";
    extrude1Dist.expression = "10";
    extrude1Dist.value = 10.0;
    extrude1Dist.unit = unitSuffix().toStdString();
    extrude1Dist.comment = tr("Extrude1 distance").toStdString();
    extrude1Dist.isUserParam = false;
    m_parameters.append(extrude1Dist);

    Parameter fillet1Rad;
    fillet1Rad.name = "Fillet1_Radius";
    fillet1Rad.expression = "radius";
    fillet1Rad.value = 5.0;
    fillet1Rad.unit = unitSuffix().toStdString();
    fillet1Rad.comment = tr("Fillet1 radius (uses 'radius' param)").toStdString();
    fillet1Rad.isUserParam = false;
    m_parameters.append(fillet1Rad);
}

QMap<QString, double> ReducedModeWindow::parameterValues() const
{
    QMap<QString, double> values;
    for (const auto& p : m_parameters) {
        values[QString::fromStdString(p.name)] = p.value;
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
        // Entity deselected — check if a constraint is selected instead
        int cid = m_sketchCanvas->selectedConstraintId();
        if (cid >= 0) {
            showSketchConstraintProperties(cid);
            return;
        }
        // No constraint either — show sketch properties
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
        if (!entity->points.empty()) {
            auto* posItem = new QTreeWidgetItem(geomHeader);
            posItem->setText(0, tr("Position"));
            posItem->setText(1, QStringLiteral("(%1, %2) %3")
                             .arg(entity->points[0].x, 0, 'f', 2)
                             .arg(entity->points[0].y, 0, 'f', 2)
                             .arg(units));
            posItem->setFlags(posItem->flags() | Qt::ItemIsEditable);
            posItem->setData(0, Qt::UserRole, entityId);
            posItem->setData(0, Qt::UserRole + 1, QStringLiteral("point0"));
        }
        break;

    case SketchEntityType::Line:
        if (entity->points.size() >= 2) {
            auto* p1Item = new QTreeWidgetItem(geomHeader);
            p1Item->setText(0, tr("Start"));
            p1Item->setText(1, QStringLiteral("(%1, %2) %3")
                             .arg(entity->points[0].x, 0, 'f', 2)
                             .arg(entity->points[0].y, 0, 'f', 2)
                             .arg(units));
            p1Item->setFlags(p1Item->flags() | Qt::ItemIsEditable);
            p1Item->setData(0, Qt::UserRole, entityId);
            p1Item->setData(0, Qt::UserRole + 1, QStringLiteral("point0"));

            auto* p2Item = new QTreeWidgetItem(geomHeader);
            p2Item->setText(0, tr("End"));
            p2Item->setText(1, QStringLiteral("(%1, %2) %3")
                             .arg(entity->points[1].x, 0, 'f', 2)
                             .arg(entity->points[1].y, 0, 'f', 2)
                             .arg(units));
            p2Item->setFlags(p2Item->flags() | Qt::ItemIsEditable);
            p2Item->setData(0, Qt::UserRole, entityId);
            p2Item->setData(0, Qt::UserRole + 1, QStringLiteral("point1"));

            auto* lenItem = new QTreeWidgetItem(geomHeader);
            lenItem->setText(0, tr("Length"));
            double len = QLineF(entity->points[0], entity->points[1]).length();
            lenItem->setText(1, QStringLiteral("%1 %2").arg(len, 0, 'f', 2).arg(units));
            lenItem->setFlags(lenItem->flags() | Qt::ItemIsEditable);
            lenItem->setData(0, Qt::UserRole, entityId);
            lenItem->setData(0, Qt::UserRole + 1, QStringLiteral("length"));
        }
        break;

    case SketchEntityType::Rectangle:
        if (entity->points.size() >= 2) {
            auto* p1Item = new QTreeWidgetItem(geomHeader);
            p1Item->setText(0, tr("Corner 1"));
            p1Item->setText(1, QStringLiteral("(%1, %2) %3")
                             .arg(entity->points[0].x, 0, 'f', 2)
                             .arg(entity->points[0].y, 0, 'f', 2)
                             .arg(units));

            auto* p2Item = new QTreeWidgetItem(geomHeader);
            p2Item->setText(0, tr("Corner 2"));
            p2Item->setText(1, QStringLiteral("(%1, %2) %3")
                             .arg(entity->points[1].x, 0, 'f', 2)
                             .arg(entity->points[1].y, 0, 'f', 2)
                             .arg(units));

            auto* widthItem = new QTreeWidgetItem(geomHeader);
            widthItem->setText(0, tr("Width"));
            double w = qAbs(entity->points[1].x - entity->points[0].x);
            widthItem->setText(1, QStringLiteral("%1 %2").arg(w, 0, 'f', 2).arg(units));
            widthItem->setFlags(widthItem->flags() | Qt::ItemIsEditable);
            widthItem->setData(0, Qt::UserRole, entityId);
            widthItem->setData(0, Qt::UserRole + 1, QStringLiteral("width"));

            auto* heightItem = new QTreeWidgetItem(geomHeader);
            heightItem->setText(0, tr("Height"));
            double h = qAbs(entity->points[1].y - entity->points[0].y);
            heightItem->setText(1, QStringLiteral("%1 %2").arg(h, 0, 'f', 2).arg(units));
            heightItem->setFlags(heightItem->flags() | Qt::ItemIsEditable);
            heightItem->setData(0, Qt::UserRole, entityId);
            heightItem->setData(0, Qt::UserRole + 1, QStringLiteral("height"));
        }
        break;

    case SketchEntityType::Circle:
        if (!entity->points.empty()) {
            auto* centerItem = new QTreeWidgetItem(geomHeader);
            centerItem->setText(0, tr("Center"));
            centerItem->setText(1, QStringLiteral("(%1, %2) %3")
                                .arg(entity->points[0].x, 0, 'f', 2)
                                .arg(entity->points[0].y, 0, 'f', 2)
                                .arg(units));
            centerItem->setFlags(centerItem->flags() | Qt::ItemIsEditable);
            centerItem->setData(0, Qt::UserRole, entityId);
            centerItem->setData(0, Qt::UserRole + 1, QStringLiteral("point0"));

            auto* radiusItem = new QTreeWidgetItem(geomHeader);
            radiusItem->setText(0, tr("Radius"));
            radiusItem->setText(1, QStringLiteral("%1 %2").arg(entity->radius, 0, 'f', 2).arg(units));
            radiusItem->setFlags(radiusItem->flags() | Qt::ItemIsEditable);
            radiusItem->setData(0, Qt::UserRole, entityId);
            radiusItem->setData(0, Qt::UserRole + 1, QStringLiteral("radius"));

            auto* diamItem = new QTreeWidgetItem(geomHeader);
            diamItem->setText(0, tr("Diameter"));
            diamItem->setText(1, QStringLiteral("%1 %2").arg(entity->radius * 2, 0, 'f', 2).arg(units));
            diamItem->setFlags(diamItem->flags() | Qt::ItemIsEditable);
            diamItem->setData(0, Qt::UserRole, entityId);
            diamItem->setData(0, Qt::UserRole + 1, QStringLiteral("diameter"));
        }
        break;

    case SketchEntityType::Arc:
        if (!entity->points.empty()) {
            auto* centerItem = new QTreeWidgetItem(geomHeader);
            centerItem->setText(0, tr("Center"));
            centerItem->setText(1, QStringLiteral("(%1, %2) %3")
                                .arg(entity->points[0].x, 0, 'f', 2)
                                .arg(entity->points[0].y, 0, 'f', 2)
                                .arg(units));

            auto* radiusItem = new QTreeWidgetItem(geomHeader);
            radiusItem->setText(0, tr("Radius"));
            radiusItem->setText(1, QStringLiteral("%1 %2").arg(entity->radius, 0, 'f', 2).arg(units));
            radiusItem->setFlags(radiusItem->flags() | Qt::ItemIsEditable);
            radiusItem->setData(0, Qt::UserRole, entityId);
            radiusItem->setData(0, Qt::UserRole + 1, QStringLiteral("radius"));

            auto* startItem = new QTreeWidgetItem(geomHeader);
            startItem->setText(0, tr("Start Angle"));
            startItem->setText(1, QStringLiteral("%1°").arg(entity->startAngle, 0, 'f', 1));
            startItem->setFlags(startItem->flags() | Qt::ItemIsEditable);
            startItem->setData(0, Qt::UserRole, entityId);
            startItem->setData(0, Qt::UserRole + 1, QStringLiteral("startAngle"));

            auto* sweepItem = new QTreeWidgetItem(geomHeader);
            sweepItem->setText(0, tr("Sweep Angle"));
            sweepItem->setText(1, QStringLiteral("%1°").arg(entity->sweepAngle, 0, 'f', 1));
            sweepItem->setFlags(sweepItem->flags() | Qt::ItemIsEditable);
            sweepItem->setData(0, Qt::UserRole, entityId);
            sweepItem->setData(0, Qt::UserRole + 1, QStringLiteral("sweepAngle"));
        }
        break;

    case SketchEntityType::Text:
        if (!entity->points.empty()) {
            auto* posItem = new QTreeWidgetItem(geomHeader);
            posItem->setText(0, tr("Position"));
            posItem->setText(1, QStringLiteral("(%1, %2) %3")
                             .arg(entity->points[0].x, 0, 'f', 2)
                             .arg(entity->points[0].y, 0, 'f', 2)
                             .arg(units));
            posItem->setFlags(posItem->flags() | Qt::ItemIsEditable);
            posItem->setData(0, Qt::UserRole, entityId);
            posItem->setData(0, Qt::UserRole + 1, QStringLiteral("point0"));

            auto* textItem = new QTreeWidgetItem(geomHeader);
            textItem->setText(0, tr("Text"));
            textItem->setText(1, QString::fromStdString(entity->text));
            textItem->setFlags(textItem->flags() | Qt::ItemIsEditable);
            textItem->setData(0, Qt::UserRole, entityId);
            textItem->setData(0, Qt::UserRole + 1, QStringLiteral("text"));

            auto* sizeItem = new QTreeWidgetItem(geomHeader);
            sizeItem->setText(0, tr("Font Size"));
            sizeItem->setText(1, QStringLiteral("%1 %2").arg(entity->fontSize, 0, 'f', 1).arg(units));
            sizeItem->setFlags(sizeItem->flags() | Qt::ItemIsEditable);
            sizeItem->setData(0, Qt::UserRole, entityId);
            sizeItem->setData(0, Qt::UserRole + 1, QStringLiteral("fontSize"));

            auto* rotItem = new QTreeWidgetItem(geomHeader);
            rotItem->setText(0, tr("Rotation"));
            rotItem->setText(1, QStringLiteral("%1%2").arg(entity->textRotation, 0, 'f', 1).arg(QChar(0x00B0)));
            rotItem->setFlags(rotItem->flags() | Qt::ItemIsEditable);
            rotItem->setData(0, Qt::UserRole, entityId);
            rotItem->setData(0, Qt::UserRole + 1, QStringLiteral("textRotation"));
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

// Parse a scalar value from text like "10.00 mm" or "90.0°"
static double parseScalarRMW(const QString& text)
{
    QString cleaned;
    for (const QChar& ch : text) {
        if (ch.isDigit() || ch == QLatin1Char('.') || ch == QLatin1Char('-'))
            cleaned += ch;
    }
    bool ok = false;
    double val = cleaned.toDouble(&ok);
    return ok ? val : std::numeric_limits<double>::quiet_NaN();
}

// Parse a point value from text like "(10.00, 20.00) mm"
static bool parsePointRMW(const QString& text, double& x, double& y)
{
    int lp = text.indexOf(QLatin1Char('('));
    int rp = text.indexOf(QLatin1Char(')'));
    if (lp < 0 || rp < 0 || rp <= lp) return false;
    QString inner = text.mid(lp + 1, rp - lp - 1);
    QStringList parts = inner.split(QLatin1Char(','));
    if (parts.size() != 2) return false;
    bool okX = false, okY = false;
    x = parts[0].trimmed().toDouble(&okX);
    y = parts[1].trimmed().toDouble(&okY);
    return okX && okY;
}

void ReducedModeWindow::onSketchPropertyItemChanged(QTreeWidgetItem* item, int column)
{
    if (!item || !m_sketchCanvas || column != 1) return;

    QString propertyName = item->data(0, Qt::UserRole + 1).toString();
    if (propertyName.isEmpty()) return;

    // Handle constraint value edits
    if (propertyName == QStringLiteral("constraintValue")) {
        int constraintId = item->data(0, Qt::UserRole).toInt();
        if (constraintId <= 0) return;

        QString text = item->text(1);
        double newValue = parseScalarRMW(text);
        if (std::isnan(newValue)) return;

        // Delegate to the canvas's editConstraintValue-style logic
        m_sketchCanvas->setConstraintValue(constraintId, newValue);

        // Refresh the properties display
        QTimer::singleShot(0, this, [this, constraintId]() {
            if (QTreeWidget* pt = propertiesTree()) pt->blockSignals(true);
            showSketchConstraintProperties(constraintId);
            if (QTreeWidget* pt = propertiesTree()) pt->blockSignals(false);
        });
        return;
    }

    int entityId = item->data(0, Qt::UserRole).toInt();
    if (entityId <= 0) return;

    SketchEntity* entity = m_sketchCanvas->entityById(entityId);
    if (!entity) return;

    // Capture entity state before modification for undo
    const SketchEntity oldEntity = *entity;

    QString text = item->text(1);
    bool changed = false;

    if (propertyName.startsWith(QStringLiteral("point"))) {
        // Point property: "point0", "point1", etc.
        int idx = propertyName.mid(5).toInt();
        if (idx >= 0 && idx < static_cast<int>(entity->points.size())) {
            double x, y;
            if (parsePointRMW(text, x, y)) {
                entity->points[idx] = {x, y};
                changed = true;
            }
        }
    }
    else if (propertyName == QStringLiteral("radius")) {
        double val = parseScalarRMW(text);
        if (!std::isnan(val) && val > 0) {
            entity->radius = val;
            // Recompute arc/circle perimeter points
            if (entity->type == SketchEntityType::Arc
                    && entity->points.size() >= 3) {
                QPointF center(entity->points[0]);
                double startRad = qDegreesToRadians(entity->startAngle);
                double endRad = qDegreesToRadians(
                    entity->startAngle + entity->sweepAngle);
                entity->points[1] = {
                    center.x() + val * qCos(startRad),
                    center.y() + val * qSin(startRad)};
                entity->points[2] = {
                    center.x() + val * qCos(endRad),
                    center.y() + val * qSin(endRad)};
            } else if (entity->type == SketchEntityType::Circle) {
                QPointF center(entity->points[0]);
                for (int i = 1; i < static_cast<int>(entity->points.size()); ++i) {
                    QPointF dir = QPointF(entity->points[i]) - center;
                    double len = std::sqrt(dir.x() * dir.x()
                                         + dir.y() * dir.y());
                    if (len > 1e-6)
                        entity->points[i] = center + dir * (val / len);
                }
            }
            changed = true;
        }
    }
    else if (propertyName == QStringLiteral("diameter")) {
        double val = parseScalarRMW(text);
        if (!std::isnan(val) && val > 0) {
            double r = val / 2.0;
            entity->radius = r;
            if (entity->type == SketchEntityType::Circle) {
                QPointF center(entity->points[0]);
                for (int i = 1; i < static_cast<int>(entity->points.size()); ++i) {
                    QPointF dir = QPointF(entity->points[i]) - center;
                    double len = std::sqrt(dir.x() * dir.x()
                                         + dir.y() * dir.y());
                    if (len > 1e-6)
                        entity->points[i] = center + dir * (r / len);
                }
            }
            changed = true;
        }
    }
    else if (propertyName == QStringLiteral("startAngle")) {
        double val = parseScalarRMW(text);
        if (!std::isnan(val)) {
            entity->startAngle = val;
            // Recompute start and end points from angles
            if (entity->points.size() >= 3) {
                QPointF center(entity->points[0]);
                double r = entity->radius;
                double startRad = qDegreesToRadians(val);
                double endRad = qDegreesToRadians(
                    val + entity->sweepAngle);
                entity->points[1] = {
                    center.x() + r * qCos(startRad),
                    center.y() + r * qSin(startRad)};
                entity->points[2] = {
                    center.x() + r * qCos(endRad),
                    center.y() + r * qSin(endRad)};
            }
            changed = true;
        }
    }
    else if (propertyName == QStringLiteral("sweepAngle")) {
        double val = parseScalarRMW(text);
        if (!std::isnan(val)) {
            entity->sweepAngle = val;
            // Recompute end point from angles
            if (entity->points.size() >= 3) {
                QPointF center(entity->points[0]);
                double r = entity->radius;
                double endRad = qDegreesToRadians(
                    entity->startAngle + val);
                entity->points[2] = {
                    center.x() + r * qCos(endRad),
                    center.y() + r * qSin(endRad)};
            }
            changed = true;
        }
    }
    else if (propertyName == QStringLiteral("length")) {
        double val = parseScalarRMW(text);
        if (!std::isnan(val) && val > 0
                && entity->points.size() >= 2) {
            // Keep start fixed, move end along same direction
            QPointF p0(entity->points[0]);
            QPointF p1(entity->points[1]);
            QPointF dir = p1 - p0;
            double curLen = std::sqrt(dir.x() * dir.x()
                                    + dir.y() * dir.y());
            if (curLen > 1e-6) {
                QPointF unit = dir / curLen;
                entity->points[1] = p0 + unit * val;
                changed = true;
            }
        }
    }
    else if (propertyName == QStringLiteral("width")) {
        double val = parseScalarRMW(text);
        if (!std::isnan(val) && val > 0
                && entity->points.size() >= 2) {
            double sign = (entity->points[1].x >= entity->points[0].x)
                ? 1.0 : -1.0;
            entity->points[1].x = entity->points[0].x + sign * val;
            changed = true;
        }
    }
    else if (propertyName == QStringLiteral("height")) {
        double val = parseScalarRMW(text);
        if (!std::isnan(val) && val > 0
                && entity->points.size() >= 2) {
            double sign = (entity->points[1].y >= entity->points[0].y)
                ? 1.0 : -1.0;
            entity->points[1].y = entity->points[0].y + sign * val;
            changed = true;
        }
    }
    else if (propertyName == QStringLiteral("sides")) {
        double val = parseScalarRMW(text);
        if (!std::isnan(val) && val >= 3) {
            entity->sides = static_cast<int>(val);
            changed = true;
        }
    }
    else if (propertyName == QStringLiteral("text")) {
        entity->text = text.toStdString();
        changed = true;
    }
    else if (propertyName == QStringLiteral("fontSize")) {
        double val = parseScalarRMW(text);
        if (!std::isnan(val) && val > 0) {
            entity->fontSize = val;
            changed = true;
        }
    }
    else if (propertyName == QStringLiteral("textRotation")) {
        double val = parseScalarRMW(text);
        if (!std::isnan(val)) {
            entity->textRotation = val;
            changed = true;
        }
    }

    if (changed) {
        // Keep text rotation handle in sync after property edits
        SketchCanvas::recomputeTextRotationHandle(*entity);

        // Push undo command for property edit
        m_sketchCanvas->pushUndoCommand(sketch::UndoCommand::modifyEntity(
            oldEntity, *entity,
            "Edit " + propertyName.toStdString()));

        m_sketchCanvas->notifyEntityChanged(entityId);
        // Defer tree refresh so Qt can close the inline editor
        // before we destroy its item via clear().
        QTimer::singleShot(0, this, [this, entityId]() {
            if (QTreeWidget* pt = propertiesTree()) pt->blockSignals(true);
            showSketchEntityProperties(entityId);
            if (QTreeWidget* pt = propertiesTree()) pt->blockSignals(false);
        });
    }
}

void ReducedModeWindow::onConstraintSelectionChanged(int constraintId)
{
    if (constraintId < 0) {
        // Constraint deselected — revert to sketch-level properties
        if (m_inSketchMode) {
            enterSketchMode(m_sketchCanvas->sketchPlane());
        }
        return;
    }
    showSketchConstraintProperties(constraintId);
}

void ReducedModeWindow::showSketchConstraintProperties(int constraintId)
{
    const SketchConstraint* constraint = m_sketchCanvas->constraintById(constraintId);
    if (!constraint) return;

    QTreeWidget* propsTree = propertiesTree();
    if (!propsTree) return;

    propsTree->blockSignals(true);
    propsTree->clear();

    // Constraint type name
    auto* typeItem = new QTreeWidgetItem(propsTree);
    typeItem->setText(0, tr("Type"));
    QString typeName;
    bool isSweep = false;
    switch (constraint->type) {
    case ConstraintType::Distance:     typeName = tr("Distance"); break;
    case ConstraintType::Radius:       typeName = tr("Radius"); break;
    case ConstraintType::Diameter:     typeName = tr("Diameter"); break;
    case ConstraintType::Angle:
        // Check if sweep angle
        for (const auto& g : m_sketchCanvas->groups()) {
            if (m_sketchCanvas->isSweepAngleGroup(g.id)
                    && g.containsConstraint(constraintId)) {
                isSweep = true;
                break;
            }
        }
        typeName = isSweep ? tr("Sweep Angle") : tr("Angle");
        break;
    case ConstraintType::FixedAngle:   typeName = tr("Fixed Angle"); break;
    case ConstraintType::Horizontal:   typeName = tr("Horizontal"); break;
    case ConstraintType::Vertical:     typeName = tr("Vertical"); break;
    case ConstraintType::Parallel:     typeName = tr("Parallel"); break;
    case ConstraintType::Perpendicular:typeName = tr("Perpendicular"); break;
    case ConstraintType::Coincident:   typeName = tr("Coincident"); break;
    case ConstraintType::Tangent:      typeName = tr("Tangent"); break;
    case ConstraintType::Equal:        typeName = tr("Equal"); break;
    case ConstraintType::Midpoint:     typeName = tr("Midpoint"); break;
    case ConstraintType::Symmetric:    typeName = tr("Symmetric"); break;
    case ConstraintType::Concentric:   typeName = tr("Concentric"); break;
    case ConstraintType::Collinear:    typeName = tr("Collinear"); break;
    case ConstraintType::PointOnLine:  typeName = tr("Point On Line"); break;
    case ConstraintType::PointOnCircle:typeName = tr("Point On Circle"); break;
    case ConstraintType::FixedPoint:   typeName = tr("Fixed Point"); break;
    }
    typeItem->setText(1, typeName);

    // Constraint ID
    auto* idItem = new QTreeWidgetItem(propsTree);
    idItem->setText(0, tr("ID"));
    idItem->setText(1, QString::number(constraint->id));

    // Value (for dimensional constraints)
    bool hasDimensionalValue = (constraint->type == ConstraintType::Distance
                                || constraint->type == ConstraintType::Radius
                                || constraint->type == ConstraintType::Diameter
                                || constraint->type == ConstraintType::Angle
                                || constraint->type == ConstraintType::FixedAngle);
    if (hasDimensionalValue) {
        auto* valueItem = new QTreeWidgetItem(propsTree);
        valueItem->setText(0, tr("Value"));
        bool isAngle = (constraint->type == ConstraintType::Angle
                        || constraint->type == ConstraintType::FixedAngle);
        QString suffix = isAngle ? QStringLiteral("°") : QStringLiteral(" ") + unitSuffix();
        valueItem->setText(1, QStringLiteral("%1%2")
                           .arg(constraint->value, 0, 'f', 2)
                           .arg(suffix));
        if (constraint->isDriving) {
            valueItem->setFlags(valueItem->flags() | Qt::ItemIsEditable);
            valueItem->setData(0, Qt::UserRole, constraintId);
            valueItem->setData(0, Qt::UserRole + 1, QStringLiteral("constraintValue"));
        }
    }

    // Driving / Reference
    auto* drivingItem = new QTreeWidgetItem(propsTree);
    drivingItem->setText(0, tr("Mode"));
    drivingItem->setText(1, constraint->isDriving ? tr("Driving") : tr("Reference"));

    // Constrained flag
    auto* constrainedItem = new QTreeWidgetItem(propsTree);
    constrainedItem->setText(0, tr("Constrained"));
    constrainedItem->setText(1, constraint->enabled ? tr("Yes") : tr("No"));

    // Parent entity/entities
    // For sweep-angle constraints, show the arc from the group
    // For other constraints, show the directly referenced entities
    auto* entitiesHeader = new QTreeWidgetItem(propsTree);
    entitiesHeader->setText(0, tr("Entities"));

    auto entityTypeName = [](SketchEntityType t) -> QString {
        switch (t) {
        case SketchEntityType::Point:     return QStringLiteral("Point");
        case SketchEntityType::Line:      return QStringLiteral("Line");
        case SketchEntityType::Rectangle: return QStringLiteral("Rectangle");
        case SketchEntityType::Circle:    return QStringLiteral("Circle");
        case SketchEntityType::Arc:       return QStringLiteral("Arc");
        case SketchEntityType::Spline:    return QStringLiteral("Spline");
        case SketchEntityType::Text:      return QStringLiteral("Text");
        case SketchEntityType::Dimension: return QStringLiteral("Dimension");
        default:                          return QStringLiteral("Unknown");
        }
    };

    if (isSweep) {
        // For sweep angle, find the arc entity in the group
        for (const auto& g : m_sketchCanvas->groups()) {
            if (m_sketchCanvas->isSweepAngleGroup(g.id)
                    && g.containsConstraint(constraintId)) {
                for (int eid : g.entityIds) {
                    const SketchEntity* e = m_sketchCanvas->entityById(eid);
                    if (e && e->type == SketchEntityType::Arc) {
                        auto* eItem = new QTreeWidgetItem(entitiesHeader);
                        eItem->setText(0, entityTypeName(e->type));
                        eItem->setText(1, QStringLiteral("ID %1").arg(e->id));
                    }
                }
                break;
            }
        }
    } else {
        // Show all referenced entities
        for (int eid : constraint->entityIds) {
            const SketchEntity* e = m_sketchCanvas->entityById(eid);
            if (e) {
                auto* eItem = new QTreeWidgetItem(entitiesHeader);
                eItem->setText(0, entityTypeName(e->type));
                eItem->setText(1, QStringLiteral("ID %1").arg(e->id));
            }
        }
    }

    propsTree->expandAll();
    propsTree->blockSignals(false);
}

}  // namespace hobbycad

