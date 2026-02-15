// =====================================================================
//  src/hobbycad/gui/full/fullmodewindow.cpp — Full Mode window
// =====================================================================

#include "fullmodewindow.h"
#include "viewportwidget.h"
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
#include "gui/constructionplanedialog.h"

#include <QComboBox>
#include <QLabel>
#include <QSettings>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <AIS_InteractiveContext.hxx>
#include <AIS_ListOfInteractive.hxx>
#include <AIS_Shape.hxx>
#include <BRep_Builder.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <gp_Ax1.hxx>
#include <gp_Circ.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_LineAspect.hxx>
#include <Quantity_Color.hxx>
#include <Standard_Type.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Shape.hxx>

namespace hobbycad {

FullModeWindow::FullModeWindow(const OpenGLInfo& glInfo, QWidget* parent)
    : MainWindow(glInfo, parent)
{
    setObjectName(QStringLiteral("FullModeWindow"));

    // Create central widget container with toolbar + viewport
    auto* container = new QWidget(this);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Toolbar stack (normal toolbar vs sketch toolbar)
    m_toolbarStack = new QStackedWidget(container);

    m_toolbar = new ViewportToolbar(m_toolbarStack);
    m_toolbarStack->addWidget(m_toolbar);
    createToolbar();

    m_sketchToolbar = new SketchToolbar(m_toolbarStack);
    m_toolbarStack->addWidget(m_sketchToolbar);

    layout->addWidget(m_toolbarStack);

    // Viewport stack (3D viewport vs 2D sketch canvas)
    m_viewportStack = new QStackedWidget(container);

    m_viewport = new ViewportWidget(m_viewportStack);
    m_viewportStack->addWidget(m_viewport);

    m_sketchCanvas = new SketchCanvas(m_viewportStack);
    m_viewportStack->addWidget(m_sketchCanvas);

    layout->addWidget(m_viewportStack, 1);  // stretch factor 1

    // Connect sketch toolbar
    connect(m_sketchToolbar, &SketchToolbar::toolSelected,
            this, &FullModeWindow::onSketchToolSelected);

    // Connect sketch canvas
    connect(m_sketchCanvas, &SketchCanvas::selectionChanged,
            this, &FullModeWindow::onSketchSelectionChanged);
    connect(m_sketchCanvas, &SketchCanvas::entityCreated,
            this, &FullModeWindow::onSketchEntityCreated);
    connect(m_sketchCanvas, &SketchCanvas::entityModified,
            this, &FullModeWindow::onSketchEntityModified);
    connect(m_sketchCanvas, &SketchCanvas::entityDragging,
            this, &FullModeWindow::onSketchEntityModified);  // Same handler for real-time updates
    connect(m_sketchCanvas, &SketchCanvas::toolChangeRequested,
            this, [this](SketchTool tool) {
        // Update toolbar to reflect tool change from canvas (e.g., Escape key)
        m_sketchToolbar->setActiveTool(tool);
        onSketchToolSelected(tool);
    });
    connect(m_sketchCanvas, &SketchCanvas::mousePositionChanged,
            this, [this](const QPointF& pos) {
        statusBar()->showMessage(
            tr("X: %1  Y: %2").arg(pos.x(), 0, 'f', 2).arg(pos.y(), 0, 'f', 2));
    });
    connect(m_sketchCanvas, &SketchCanvas::sketchDeselected,
            this, [this]() {
        // Sketch deselected - clear properties panel
        if (QTreeWidget* propsTree = propertiesTree()) {
            propsTree->clear();
        }
    });
    connect(m_sketchCanvas, &SketchCanvas::exitRequested,
            this, [this]() {
        // Escape pressed with sketch deselected - show Save/Discard and flash
        if (sketchActionBar()) {
            sketchActionBar()->showAndFlash();
        }
    });

    // Connect sketch action bar (Save/Cancel buttons in properties panel)
    if (sketchActionBar()) {
        connect(sketchActionBar(), &SketchActionBar::saveClicked,
                this, [this]() {
            saveCurrentSketch();
            exitSketchMode();
        });
        connect(sketchActionBar(), &SketchActionBar::discardClicked,
                this, [this]() {
            discardCurrentSketch();
            exitSketchMode();
        });
    }

    // Timeline below the viewport
    m_timeline = new TimelineWidget(container);
    layout->addWidget(m_timeline);
    createTimeline();

    setCentralWidget(container);

    // Connect View > Reset View to the viewport
    if (resetViewAction()) {
        connect(resetViewAction(), &QAction::triggered,
                m_viewport, &ViewportWidget::resetCamera);
    }

    // Connect View > Rotate Left/Right (90° around Z axis)
    if (rotateLeftAction()) {
        connect(rotateLeftAction(), &QAction::triggered,
                this, [this]() { m_viewport->rotateCamera90(-2); });
    }
    if (rotateRightAction()) {
        connect(rotateRightAction(), &QAction::triggered,
                this, [this]() { m_viewport->rotateCamera90(2); });
    }

    // Connect View > Toolbar toggle
    if (toolbarToggleAction()) {
        connect(toolbarToggleAction(), &QAction::toggled,
                m_toolbarStack, &QWidget::setVisible);
    }

    // Connect View > Show Grid toggle
    if (showGridAction()) {
        connect(showGridAction(), &QAction::toggled,
                m_sketchCanvas, &SketchCanvas::setGridVisible);
    }

    // Connect View > Snap to Grid toggle
    if (snapToGridAction()) {
        connect(snapToGridAction(), &QAction::toggled,
                m_sketchCanvas, &SketchCanvas::setSnapToGrid);
    }

    // Connect View > Z-Up Orientation toggle
    if (zUpAction()) {
        connect(zUpAction(), &QAction::toggled,
                m_viewport, &ViewportWidget::setZUpOrientation);
    }

    // Connect View > Orbit Selected Object toggle
    if (orbitSelectedAction()) {
        connect(orbitSelectedAction(), &QAction::toggled,
                m_viewport, &ViewportWidget::setOrbitSelectedObject);
    }

    // Connect units change to viewport scale bar
    connect(this, &MainWindow::unitsChanged,
            m_viewport, &ViewportWidget::setUnitSystem);

    // Connect CLI panel viewport commands (only work in full mode)
    if (cliPanel()) {
        connect(cliPanel(), &CliPanel::zoomRequested,
                m_viewport, &ViewportWidget::setZoomPercent);
        connect(cliPanel(), &CliPanel::zoomHomeRequested,
                m_viewport, &ViewportWidget::fitAll);
        connect(cliPanel(), &CliPanel::panToRequested,
                m_viewport, &ViewportWidget::panTo);
        connect(cliPanel(), &CliPanel::panHomeRequested,
                this, [this]() { m_viewport->panTo(0.0, 0.0, 0.0); });
        connect(cliPanel(), &CliPanel::rotateRequested,
                m_viewport, &ViewportWidget::rotateOnAxis);
        connect(cliPanel(), &CliPanel::rotateHomeRequested,
                m_viewport, &ViewportWidget::resetCamera);

        // Mark viewport as connected so CLI knows commands will work
        cliPanel()->setViewportConnected(true);
    }

    // Connect Construct > New Construction Plane
    if (newConstructionPlaneAction()) {
        connect(newConstructionPlaneAction(), &QAction::triggered,
                this, &FullModeWindow::onNewConstructionPlane);
    }

    // Connect construction plane selection from feature tree
    connect(this, &MainWindow::constructionPlaneSelected,
            this, &FullModeWindow::onConstructionPlaneSelected);

    finalizeLayout();

    // Axis indicator in the status bar (added after finalizeLayout
    // so restoreState doesn't interfere with widget ordering)
    m_axisLabel = new QLabel(tr("Axis: X"), this);
    m_axisLabel->setObjectName(QStringLiteral("AxisLabel"));
    statusBar()->addPermanentWidget(m_axisLabel);

    connect(m_viewport, &ViewportWidget::rotationAxisChanged,
            this, [this](ViewportWidget::RotationAxis axis) {
        static const char* names[] = { "X", "Y", "Z" };
        m_axisLabel->setText(tr("Axis: %1").arg(names[axis]));
    });

    // Apply saved preferences (rotation axis, spin/snap params, grid)
    applyPreferences();

    // Initialize default parameters
    initDefaultParameters();
}

void FullModeWindow::onDocumentLoaded()
{
    // Display geometry in 3D viewport
    displayShapes();

    // If we have a project loaded, populate the UI with project data
    if (!m_project.isNew()) {
        loadProjectData();
    }
}

void FullModeWindow::onDocumentClosed()
{
    if (!m_viewport || m_viewport->context().IsNull()) return;

    auto ctx = m_viewport->context();

    // Remove only user shapes (AIS_Shape), preserving the trihedron,
    // grid, and ViewCube.
    AIS_ListOfInteractive displayed;
    ctx->DisplayedObjects(displayed);
    for (auto it = displayed.begin(); it != displayed.end(); ++it) {
        if ((*it)->IsKind(STANDARD_TYPE(AIS_Shape)))
            ctx->Remove(*it, false);
    }

    ctx->UpdateCurrentViewer();
    m_viewport->resetCamera();
}

void FullModeWindow::applyPreferences()
{
    QSettings s;
    s.beginGroup(QStringLiteral("preferences"));

    // Rotation axis
    int axis = s.value(QStringLiteral("defaultAxis"), 0).toInt();
    m_viewport->setRotationAxis(
        static_cast<ViewportWidget::RotationAxis>(qBound(0, axis, 2)));

    // PgUp/PgDn
    int pgStep = s.value(QStringLiteral("pgUpStepDeg"), 10).toInt();
    int pgInt  = s.value(QStringLiteral("spinInterval"), 10).toInt();
    m_viewport->setSpinParams(pgStep, pgInt);

    // Arrow snap animation
    int snapStep = s.value(QStringLiteral("snapStepDeg"), 10).toInt();
    int snapInt  = s.value(QStringLiteral("snapInterval"), 10).toInt();
    m_viewport->setSnapParams(snapStep, snapInt);

    // Grid
    bool showGrid = s.value(QStringLiteral("showGrid"), true).toBool();
    m_viewport->setGridVisible(showGrid);

    // Coordinate system and orbit behavior
    bool zUp = s.value(QStringLiteral("zUpOrientation"), true).toBool();
    m_viewport->setZUpOrientation(zUp);
    if (zUpAction()) {
        zUpAction()->setChecked(zUp);
    }

    bool orbitSelected = s.value(QStringLiteral("orbitSelected"), false).toBool();
    m_viewport->setOrbitSelectedObject(orbitSelected);
    if (orbitSelectedAction()) {
        orbitSelectedAction()->setChecked(orbitSelected);
    }

    s.endGroup();

    // Update axis label
    static const char* names[] = { "X", "Y", "Z" };
    m_axisLabel->setText(tr("Axis: %1").arg(names[m_viewport->rotationAxis()]));

    // Reload sketch canvas key bindings
    if (m_sketchCanvas) {
        m_sketchCanvas->reloadBindings();
    }
}

void FullModeWindow::createToolbar()
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
            this, &FullModeWindow::showParametersDialog);

    // Connect Create button - enters sketch mode
    connect(sketchBtn, &ToolbarButton::clicked,
            this, &FullModeWindow::onCreateSketchClicked);

    // Most buttons are disabled until functionality is implemented
    // sketchBtn is now enabled - 2D sketching works!
    boxBtn->setEnabled(false);
    extrudeBtn->setEnabled(false);
    revolveBtn->setEnabled(false);
    filletBtn->setEnabled(false);
    holeBtn->setEnabled(false);
    moveBtn->setEnabled(false);
    mirrorBtn->setEnabled(false);
    // paramsBtn is now enabled!
}

void FullModeWindow::createTimeline()
{
    // Start with just the Origin item - other items added as features are created
    m_timeline->addItem(TimelineFeature::Origin, tr("Origin"));

    // Connect timeline item selection to properties panel
    connect(m_timeline, &TimelineWidget::itemClicked,
            this, &FullModeWindow::showFeatureProperties);
}

void FullModeWindow::showFeatureProperties(int index)
{
    QTreeWidget* propsTree = propertiesTree();
    if (!propsTree)
        return;

    propsTree->clear();

    // Hide any existing plane visualization
    hideSketchPlane();

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

    // Helper to set property and editable length value with document units
    auto setLengthProperty = [&units](QTreeWidgetItem* item, const QString& prop, const QString& value) {
        QString display = QStringLiteral("%1 %2").arg(value, units);
        item->setText(0, prop);
        item->setText(1, display);
        item->setToolTip(0, prop);
        item->setToolTip(1, display);
        item->setFlags(item->flags() | Qt::ItemIsEditable);
    };

    // Helper to set property and editable angle value (degrees)
    auto setAngleProperty = [](QTreeWidgetItem* item, const QString& prop, const QString& value) {
        QString display = QStringLiteral("%1°").arg(value);
        item->setText(0, prop);
        item->setText(1, display);
        item->setToolTip(0, prop);
        item->setToolTip(1, display);
        item->setFlags(item->flags() | Qt::ItemIsEditable);
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
            setProperty(item, tr("Position"), tr("(0, 0, 0)"));  // Origin position is fixed
        }
        break;

    case TimelineFeature::Sketch:
        {
            // Find which sketch this timeline item corresponds to
            // by counting Sketch items before this index
            int sketchIdx = -1;
            int sketchCount = 0;
            for (int i = 0; i <= index; ++i) {
                if (m_timeline->featureAt(i) == TimelineFeature::Sketch) {
                    if (i == index) {
                        sketchIdx = sketchCount;
                        break;
                    }
                    ++sketchCount;
                }
            }

            // Get real sketch data if available
            int planeIdx = 0;
            int entityCount = 0;
            SketchPlane sketchPlane = SketchPlane::XY;
            double sketchOffset = 0.0;
            PlaneRotationAxis rotAxis = PlaneRotationAxis::X;
            double rotAngle = 0.0;

            if (sketchIdx >= 0 && sketchIdx < m_completedSketches.size()) {
                const CompletedSketch& sketch = m_completedSketches[sketchIdx];
                sketchPlane = sketch.plane;
                sketchOffset = sketch.planeOffset;
                rotAxis = sketch.rotationAxis;
                rotAngle = sketch.rotationAngle;

                switch (sketch.plane) {
                case SketchPlane::XY: planeIdx = 0; break;
                case SketchPlane::XZ: planeIdx = 1; break;
                case SketchPlane::YZ: planeIdx = 2; break;
                case SketchPlane::Custom: planeIdx = 3; break;
                }
                entityCount = sketch.entities.size();

                // Show the sketch plane visualization in 3D viewport
                showSketchPlane(sketchPlane, sketchOffset, rotAxis, rotAngle);
            }

            auto* planeItem = new QTreeWidgetItem(propsHeader);
            QStringList planeOptions = {tr("XY"), tr("XZ"), tr("YZ"), tr("Custom")};
            setDropdownProperty(planeItem, tr("Plane"), planeOptions, planeIdx);

            // Show offset if non-zero
            if (!qFuzzyIsNull(sketchOffset)) {
                auto* offsetItem = new QTreeWidgetItem(propsHeader);
                setProperty(offsetItem, tr("Offset"),
                    QStringLiteral("%1 %2").arg(sketchOffset, 0, 'g', 6).arg(units));
            }

            auto* entitiesItem = new QTreeWidgetItem(propsHeader);
            setProperty(entitiesItem, tr("Entities"), QString::number(entityCount));
            auto* constraintsItem = new QTreeWidgetItem(propsHeader);
            setProperty(constraintsItem, tr("Constraints"), tr("0"));  // No constraints yet
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
            setFormulaProperty(radiusItem, tr("Radius"), QStringLiteral("radius"), units);  // Uses parameter
            auto* edgesItem = new QTreeWidgetItem(propsHeader);
            setProperty(edgesItem, tr("Edges"), tr("4"));  // Read-only count
        }
        break;

    case TimelineFeature::Chamfer:
        {
            auto* distItem = new QTreeWidgetItem(propsHeader);
            setFormulaProperty(distItem, tr("Distance"), QStringLiteral("1"), units);
            auto* edgesItem = new QTreeWidgetItem(propsHeader);
            setProperty(edgesItem, tr("Edges"), tr("2"));  // Read-only count
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
            setProperty(bodiesItem, tr("Bodies"), tr("1"));  // Read-only count
        }
        break;

    case TimelineFeature::Pattern:
        {
            auto* typeP = new QTreeWidgetItem(propsHeader);
            setDropdownProperty(typeP, tr("Pattern Type"),
                {tr("Rectangular"), tr("Circular")}, 0);
            auto* countItem = new QTreeWidgetItem(propsHeader);
            setEditableProperty(countItem, tr("Count"), tr("3 x 2"));  // No units - it's a count
            auto* spacingItem = new QTreeWidgetItem(propsHeader);
            setEditableProperty(spacingItem, tr("Spacing"), QStringLiteral("15 x 10 %1").arg(units));  // Length units
        }
        break;

    case TimelineFeature::Box:
        {
            auto* dimItem = new QTreeWidgetItem(propsHeader);
            setEditableProperty(dimItem, tr("Dimensions"), QStringLiteral("50 x 30 x 20 %1").arg(units));  // Length units
        }
        break;

    case TimelineFeature::Cylinder:
        {
            auto* diamItem = new QTreeWidgetItem(propsHeader);
            setFormulaProperty(diamItem, tr("Diameter"), QStringLiteral("width / 2"), units);  // Formula example
            auto* heightItem = new QTreeWidgetItem(propsHeader);
            setFormulaProperty(heightItem, tr("Height"), QStringLiteral("height + depth"), units);  // Formula example
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
            setEditableProperty(transItem, tr("Translation"), QStringLiteral("(10, 5, 0) %1").arg(units));  // Length units
            auto* rotItem = new QTreeWidgetItem(propsHeader);
            setAngleProperty(rotItem, tr("Rotation"), tr("0"));
        }
        break;

    case TimelineFeature::Join:
    case TimelineFeature::Cut:
    case TimelineFeature::Intersect:
        {
            auto* bodiesItem = new QTreeWidgetItem(propsHeader);
            setProperty(bodiesItem, tr("Target Bodies"), tr("2"));  // Read-only (selection-based)
            auto* toolItem = new QTreeWidgetItem(propsHeader);
            setProperty(toolItem, tr("Tool Bodies"), tr("1"));  // Read-only (selection-based)
        }
        break;
    }

    propsTree->expandAll();

    // Connect double-click to show dropdown for dropdown properties
    // Disconnect any existing connection first to avoid duplicates
    disconnect(propsTree, &QTreeWidget::itemDoubleClicked, nullptr, nullptr);
    connect(propsTree, &QTreeWidget::itemDoubleClicked,
            this, [this, propsTree](QTreeWidgetItem* item, int column) {
        if (column != 1)
            return;

        // Check if this is a dropdown property
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
            // Defer widget removal to avoid deleting during signal
            QTimer::singleShot(0, this, [propsTree, item]() {
                propsTree->setItemWidget(item, 1, nullptr);
            });
        });
    });
}

void FullModeWindow::displayShapes()
{
    if (!m_viewport || m_viewport->context().IsNull()) return;

    auto ctx = m_viewport->context();

    // Remove only user shapes (AIS_Shape), preserving the trihedron
    // and any other non-shape interactive objects.
    AIS_ListOfInteractive displayed;
    ctx->DisplayedObjects(displayed);
    for (auto it = displayed.begin(); it != displayed.end(); ++it) {
        if ((*it)->IsKind(STANDARD_TYPE(AIS_Shape)))
            ctx->Remove(*it, false);
    }

    // Display each shape from the document with edge outlines
    for (const auto& shape : m_document.shapes()) {
        if (!shape.IsNull()) {
            // Shaded body
            Handle(AIS_Shape) aisShape = new AIS_Shape(shape);
            ctx->Display(aisShape, AIS_Shaded, 0, false);

            // Wireframe overlay for visible edge outlines
            Handle(AIS_Shape) wireShape = new AIS_Shape(shape);
            Handle(Prs3d_Drawer) wireDrw = wireShape->Attributes();
            wireDrw->SetWireAspect(
                new Prs3d_LineAspect(
                    Quantity_Color(Quantity_NOC_WHITE),
                    Aspect_TOL_SOLID,
                    1.0));
            ctx->Display(wireShape, AIS_WireFrame, 0, false);
            ctx->Deactivate(wireShape);  // not selectable
        }
    }

    m_viewport->context()->UpdateCurrentViewer();
}

void FullModeWindow::initDefaultParameters()
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

QMap<QString, double> FullModeWindow::parameterValues() const
{
    QMap<QString, double> values;
    for (const auto& p : m_parameters) {
        values[p.name] = p.value;
    }
    return values;
}

void FullModeWindow::showParametersDialog()
{
    ParametersDialog dlg(this);
    dlg.setDefaultUnit(unitSuffix());
    dlg.setParameters(m_parameters);

    connect(&dlg, &ParametersDialog::parametersChanged,
            this, &FullModeWindow::onParametersChanged);

    dlg.exec();
}

void FullModeWindow::onParametersChanged(const QList<Parameter>& params)
{
    m_parameters = params;

    // TODO: Re-evaluate all features that use these parameters
    // and regenerate the model

    // For now, just update the status bar
    statusBar()->showMessage(tr("Parameters updated"), 3000);
}

void FullModeWindow::onCreateSketchClicked()
{
    // Show plane selection dialog
    SketchPlaneDialog dialog(this);

    // Provide available construction planes
    dialog.setAvailableConstructionPlanes(m_project.constructionPlanes());

    if (dialog.exec() != QDialog::Accepted) {
        return;  // User cancelled
    }

    SketchPlane plane = dialog.selectedPlane();
    double offset = dialog.offset();
    int constructionPlaneId = dialog.constructionPlaneId();

    // Store parameters for use in 3D wireframe generation
    m_pendingSketchOffset = offset;
    m_pendingRotationAxis = dialog.rotationAxis();
    m_pendingRotationAngle = dialog.rotationAngle();

    // If using a construction plane, get its parameters
    if (constructionPlaneId >= 0) {
        const ConstructionPlaneData* cpData = m_project.constructionPlaneById(constructionPlaneId);
        if (cpData) {
            // Set plane to Custom and use the construction plane's parameters
            plane = SketchPlane::Custom;
            m_pendingSketchOffset = offset + cpData->offset;
            m_pendingRotationAxis = cpData->primaryAxis;
            m_pendingRotationAngle = cpData->primaryAngle;
            // TODO: Support secondary axis rotation
        }
    }

    enterSketchMode(plane);
}

void FullModeWindow::onNewConstructionPlane()
{
    ConstructionPlaneDialog dialog(this);
    dialog.setEditMode(false);

    // Provide existing construction planes for "offset from plane" option
    dialog.setAvailablePlanes(m_project.constructionPlanes());

    // Generate default name
    int planeCount = m_project.constructionPlanes().size();
    QString defaultName = tr("Plane %1").arg(planeCount + 1);

    // Pre-fill placeholder text
    ConstructionPlaneData defaultData;
    defaultData.name = defaultName;
    dialog.setPlaneData(defaultData);

    if (dialog.exec() != QDialog::Accepted) {
        return;  // User cancelled
    }

    ConstructionPlaneData planeData = dialog.planeData();
    planeData.id = m_project.nextConstructionPlaneId();

    // Add to project
    m_project.addConstructionPlane(planeData);

    // Add to feature tree
    addConstructionPlaneToTree(planeData.name, planeData.id);

    // Display in viewport if visible
    if (planeData.visible) {
        displayConstructionPlane(planeData.id);
    }

    // Select in tree
    selectConstructionPlaneInTree(planeData.id);

    statusBar()->showMessage(
        tr("Construction plane '%1' created").arg(planeData.name),
        3000);
}

void FullModeWindow::onConstructionPlaneSelected(int planeId)
{
    const ConstructionPlaneData* planeData = m_project.constructionPlaneById(planeId);
    if (!planeData) return;

    // Show plane properties in the properties panel
    QTreeWidget* props = propertiesTree();
    if (!props) return;

    props->clear();

    // Name
    auto* nameItem = new QTreeWidgetItem(props);
    nameItem->setText(0, tr("Name"));
    nameItem->setText(1, planeData->name);
    nameItem->setFlags(nameItem->flags() | Qt::ItemIsEditable);

    // Type
    auto* typeItem = new QTreeWidgetItem(props);
    typeItem->setText(0, tr("Type"));
    QString typeName;
    switch (planeData->type) {
    case ConstructionPlaneType::OffsetFromOrigin:
        typeName = tr("Offset from Origin");
        break;
    case ConstructionPlaneType::OffsetFromPlane:
        typeName = tr("Offset from Plane");
        break;
    case ConstructionPlaneType::Angled:
        typeName = tr("Angled");
        break;
    }
    typeItem->setText(1, typeName);

    // Base plane (for offset from origin)
    if (planeData->type == ConstructionPlaneType::OffsetFromOrigin) {
        auto* basePlaneItem = new QTreeWidgetItem(props);
        basePlaneItem->setText(0, tr("Base Plane"));
        QString planeName;
        switch (planeData->basePlane) {
        case SketchPlane::XY: planeName = tr("XY"); break;
        case SketchPlane::XZ: planeName = tr("XZ"); break;
        case SketchPlane::YZ: planeName = tr("YZ"); break;
        default: planeName = tr("Custom"); break;
        }
        basePlaneItem->setText(1, planeName);
    }

    // Reference plane (for offset from plane)
    if (planeData->type == ConstructionPlaneType::OffsetFromPlane) {
        auto* refPlaneItem = new QTreeWidgetItem(props);
        refPlaneItem->setText(0, tr("Reference Plane"));
        const ConstructionPlaneData* refPlane = m_project.constructionPlaneById(planeData->basePlaneId);
        refPlaneItem->setText(1, refPlane ? refPlane->name : tr("(none)"));
    }

    // Offset
    auto* offsetItem = new QTreeWidgetItem(props);
    offsetItem->setText(0, tr("Offset"));
    offsetItem->setText(1, tr("%1 mm").arg(planeData->offset, 0, 'g', 6));
    offsetItem->setFlags(offsetItem->flags() | Qt::ItemIsEditable);

    // Rotation (for angled planes)
    if (planeData->type == ConstructionPlaneType::Angled) {
        auto* primaryItem = new QTreeWidgetItem(props);
        primaryItem->setText(0, tr("Primary Rotation"));
        QString axisName;
        switch (planeData->primaryAxis) {
        case PlaneRotationAxis::X: axisName = tr("X"); break;
        case PlaneRotationAxis::Y: axisName = tr("Y"); break;
        case PlaneRotationAxis::Z: axisName = tr("Z"); break;
        }
        primaryItem->setText(1, tr("%1° around %2")
                             .arg(planeData->primaryAngle, 0, 'g', 4)
                             .arg(axisName));

        if (!qFuzzyIsNull(planeData->secondaryAngle)) {
            auto* secondaryItem = new QTreeWidgetItem(props);
            secondaryItem->setText(0, tr("Secondary Rotation"));
            switch (planeData->secondaryAxis) {
            case PlaneRotationAxis::X: axisName = tr("X"); break;
            case PlaneRotationAxis::Y: axisName = tr("Y"); break;
            case PlaneRotationAxis::Z: axisName = tr("Z"); break;
            }
            secondaryItem->setText(1, tr("%1° around %2")
                                   .arg(planeData->secondaryAngle, 0, 'g', 4)
                                   .arg(axisName));
        }
    }

    // Roll angle
    if (!qFuzzyIsNull(planeData->rollAngle)) {
        auto* rollItem = new QTreeWidgetItem(props);
        rollItem->setText(0, tr("Roll"));
        rollItem->setText(1, tr("%1°").arg(planeData->rollAngle, 0, 'g', 4));
        rollItem->setFlags(rollItem->flags() | Qt::ItemIsEditable);
    }

    // Origin point (plane center in absolute coordinates)
    if (planeData->hasCustomOrigin()) {
        auto* originItem = new QTreeWidgetItem(props);
        originItem->setText(0, tr("Center (Absolute)"));
        originItem->setText(1, tr("(%1, %2, %3) mm")
                            .arg(planeData->originX, 0, 'g', 6)
                            .arg(planeData->originY, 0, 'g', 6)
                            .arg(planeData->originZ, 0, 'g', 6));
        originItem->setFlags(originItem->flags() | Qt::ItemIsEditable);
    }

    // Visibility
    auto* visibleItem = new QTreeWidgetItem(props);
    visibleItem->setText(0, tr("Visible"));
    visibleItem->setText(1, planeData->visible ? tr("Yes") : tr("No"));
    visibleItem->setFlags(visibleItem->flags() | Qt::ItemIsEditable);

    // Expand all to show properties
    props->expandAll();
    props->resizeColumnToContents(0);

    // Show the sketch plane in 3D view
    showSketchPlane(planeData->basePlane, planeData->offset,
                    planeData->primaryAxis, planeData->primaryAngle);
}

void FullModeWindow::enterSketchMode(SketchPlane plane)
{
    if (m_inSketchMode) return;

    m_inSketchMode = true;

    // Notify CLI panel that viewport commands won't work in sketch mode
    if (cliPanel()) {
        cliPanel()->setSketchModeActive(true);
    }

    // Switch to sketch toolbar and set to Select mode (object select)
    m_toolbarStack->setCurrentWidget(m_sketchToolbar);
    m_sketchToolbar->setActiveTool(SketchTool::Select);

    // Switch to sketch canvas
    m_sketchCanvas->setSketchPlane(plane);
    m_sketchCanvas->clear();
    m_sketchCanvas->resetView();
    m_sketchCanvas->setActiveTool(SketchTool::Select);
    m_sketchCanvas->setSketchSelected(true);  // Sketch starts selected
    m_viewportStack->setCurrentWidget(m_sketchCanvas);

    // Show the action bar in properties panel and reset its state
    if (sketchActionBar()) {
        sketchActionBar()->reset();
    }
    setSketchActionBarVisible(true);

    // Determine if we're creating a new sketch or editing existing
    // m_currentSketchIndex will be set to -1 for new, or >= 0 for edit
    // (set before calling this function when editing)
    bool isNewSketch = (m_currentSketchIndex < 0);

    QString sketchName;
    if (isNewSketch) {
        // Add new sketch to timeline
        int sketchCount = 0;
        for (int i = 0; i < m_timeline->itemCount(); ++i) {
            if (m_timeline->featureAt(i) == TimelineFeature::Sketch) {
                ++sketchCount;
            }
        }
        sketchName = tr("Sketch%1").arg(sketchCount + 1);
        m_timeline->addItem(TimelineFeature::Sketch, sketchName);

        // Also add to feature tree - use the pending index (will be added on save)
        int pendingIndex = m_completedSketches.size();
        addSketchToTree(sketchName, pendingIndex);
    } else {
        // Editing existing sketch - get its name
        sketchName = m_completedSketches[m_currentSketchIndex].name;
    }

    // Select the sketch in timeline
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

void FullModeWindow::exitSketchMode()
{
    if (!m_inSketchMode) return;

    m_inSketchMode = false;

    // Notify CLI panel that viewport commands are available again
    if (cliPanel()) {
        cliPanel()->setSketchModeActive(false);
    }

    // Switch back to normal toolbar
    m_toolbarStack->setCurrentWidget(m_toolbar);

    // Switch back to 3D viewport
    m_viewportStack->setCurrentWidget(m_viewport);

    // Hide the Save/Cancel action bar
    setSketchActionBarVisible(false);

    // Clear properties
    if (QTreeWidget* propsTree = propertiesTree()) {
        propsTree->clear();
    }

    // Deselect timeline item
    m_timeline->setSelectedIndex(-1);

    // Update status bar
    statusBar()->showMessage(tr("Sketch finished"), 3000);
}

void FullModeWindow::onSketchToolSelected(SketchTool tool)
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

void FullModeWindow::onSketchSelectionChanged(int entityId)
{
    if (entityId < 0) {
        // Entity deselected - show sketch properties (re-select sketch)
        m_sketchCanvas->setSketchSelected(true);
        showSketchProperties();
        return;
    }

    // Show entity properties (sketch remains selected)
    m_sketchCanvas->setSketchSelected(true);
    showSketchEntityProperties(entityId);
}

void FullModeWindow::onSketchEntityCreated(int entityId)
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

void FullModeWindow::onSketchEntityModified(int entityId)
{
    // Refresh the properties panel to show updated values
    showSketchEntityProperties(entityId);
}

void FullModeWindow::showSketchEntityProperties(int entityId)
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

void FullModeWindow::showSketchProperties()
{
    QTreeWidget* propsTree = propertiesTree();
    if (!propsTree) return;

    propsTree->clear();

    // Determine sketch name
    QString sketchName;
    if (m_currentSketchIndex >= 0 && m_currentSketchIndex < m_completedSketches.size()) {
        sketchName = m_completedSketches[m_currentSketchIndex].name;
    } else {
        // New sketch - find name from timeline
        int sketchCount = 0;
        for (int i = 0; i < m_timeline->itemCount(); ++i) {
            if (m_timeline->featureAt(i) == TimelineFeature::Sketch) {
                ++sketchCount;
            }
        }
        sketchName = tr("Sketch%1").arg(sketchCount);
    }

    // Sketch name
    auto* nameItem = new QTreeWidgetItem(propsTree);
    nameItem->setText(0, tr("Name"));
    nameItem->setText(1, sketchName);
    nameItem->setFlags(nameItem->flags() | Qt::ItemIsEditable);

    // Plane selection
    auto* planeItem = new QTreeWidgetItem(propsTree);
    planeItem->setText(0, tr("Plane"));
    QStringList planes = {tr("XY"), tr("XZ"), tr("YZ")};
    int planeIdx = static_cast<int>(m_sketchCanvas->sketchPlane());
    planeItem->setText(1, planes.value(planeIdx));
    planeItem->setData(1, Qt::UserRole, QStringLiteral("dropdown"));
    planeItem->setData(1, Qt::UserRole + 1, planes);
    planeItem->setData(1, Qt::UserRole + 2, planeIdx);

    // Entities count
    auto* entitiesItem = new QTreeWidgetItem(propsTree);
    entitiesItem->setText(0, tr("Entities"));
    entitiesItem->setText(1, QString::number(m_sketchCanvas->entities().size()));

    propsTree->expandAll();
}

Handle(AIS_Shape) FullModeWindow::createSketchWireframe(const CompletedSketch& sketch)
{
    BRep_Builder builder;
    TopoDS_Compound compound;
    builder.MakeCompound(compound);

    // Build transformation for custom angled plane
    // For standard planes, we just apply offset
    // For custom planes, we rotate around the specified axis then apply offset
    double off = sketch.planeOffset;
    gp_Trsf customTransform;
    bool useCustomTransform = (sketch.plane == SketchPlane::Custom);

    if (useCustomTransform) {
        // Start with XY plane, then rotate around the specified axis
        gp_Ax1 rotAxis;
        switch (sketch.rotationAxis) {
        case PlaneRotationAxis::X:
            rotAxis = gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(1, 0, 0));
            break;
        case PlaneRotationAxis::Y:
            rotAxis = gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(0, 1, 0));
            break;
        case PlaneRotationAxis::Z:
            rotAxis = gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1));
            break;
        }
        double angleRad = sketch.rotationAngle * M_PI / 180.0;
        customTransform.SetRotation(rotAxis, angleRad);
    }

    // Transform 2D point to 3D based on sketch plane and offset
    // Offset is applied along the plane's normal axis
    auto to3D = [&sketch, off, useCustomTransform, &customTransform](const QPointF& p) -> gp_Pnt {
        gp_Pnt pt;
        switch (sketch.plane) {
        case SketchPlane::XY:
            pt = gp_Pnt(p.x(), p.y(), off);  // Z offset
            break;
        case SketchPlane::XZ:
            pt = gp_Pnt(p.x(), off, p.y());  // Y offset
            break;
        case SketchPlane::YZ:
            pt = gp_Pnt(off, p.x(), p.y());  // X offset
            break;
        case SketchPlane::Custom:
            // Start on XY plane at Z=0, then transform
            pt = gp_Pnt(p.x(), p.y(), 0);
            pt.Transform(customTransform);
            // Apply offset along transformed normal (Z after rotation)
            if (!qFuzzyIsNull(off)) {
                gp_Dir normal(0, 0, 1);
                normal.Transform(customTransform);
                pt.SetX(pt.X() + normal.X() * off);
                pt.SetY(pt.Y() + normal.Y() * off);
                pt.SetZ(pt.Z() + normal.Z() * off);
            }
            break;
        default:
            pt = gp_Pnt(p.x(), p.y(), off);
            break;
        }
        return pt;
    };

    // Get the plane normal and axes for circles/arcs
    // Note: plane normal direction determines which way positive offset goes
    gp_Dir planeNormal, planeXDir;
    switch (sketch.plane) {
    case SketchPlane::XY:
        planeNormal = gp_Dir(0, 0, 1);  // +Z normal
        planeXDir = gp_Dir(1, 0, 0);
        break;
    case SketchPlane::XZ:
        planeNormal = gp_Dir(0, 1, 0);  // +Y normal
        planeXDir = gp_Dir(1, 0, 0);
        break;
    case SketchPlane::YZ:
        planeNormal = gp_Dir(1, 0, 0);  // +X normal
        planeXDir = gp_Dir(0, 1, 0);
        break;
    case SketchPlane::Custom:
        // Start with XY plane orientation, then rotate
        planeNormal = gp_Dir(0, 0, 1);
        planeXDir = gp_Dir(1, 0, 0);
        planeNormal.Transform(customTransform);
        planeXDir.Transform(customTransform);
        break;
    default:
        planeNormal = gp_Dir(0, 0, 1);
        planeXDir = gp_Dir(1, 0, 0);
        break;
    }

    for (const SketchEntity& entity : sketch.entities) {
        switch (entity.type) {
        case SketchEntityType::Point:
            if (!entity.points.isEmpty()) {
                gp_Pnt pt = to3D(entity.points[0]);
                BRepBuilderAPI_MakeVertex mv(pt);
                if (mv.IsDone()) {
                    builder.Add(compound, mv.Vertex());
                }
            }
            break;

        case SketchEntityType::Line:
            if (entity.points.size() >= 2) {
                gp_Pnt p1 = to3D(entity.points[0]);
                gp_Pnt p2 = to3D(entity.points[1]);
                if (p1.Distance(p2) > 1e-6) {
                    BRepBuilderAPI_MakeEdge me(p1, p2);
                    if (me.IsDone()) {
                        TopoDS_Shape edge = me.Edge();
                        builder.Add(compound, edge);
                    }
                }
            }
            break;

        case SketchEntityType::Rectangle:
            if (entity.points.size() >= 2) {
                gp_Pnt p1 = to3D(entity.points[0]);
                gp_Pnt p2 = to3D(QPointF(entity.points[1].x(), entity.points[0].y()));
                gp_Pnt p3 = to3D(entity.points[1]);
                gp_Pnt p4 = to3D(QPointF(entity.points[0].x(), entity.points[1].y()));

                auto addEdge = [&](const gp_Pnt& a, const gp_Pnt& b) {
                    if (a.Distance(b) > 1e-6) {
                        BRepBuilderAPI_MakeEdge me(a, b);
                        if (me.IsDone()) {
                            TopoDS_Shape edge = me.Edge();
                            builder.Add(compound, edge);
                        }
                    }
                };
                addEdge(p1, p2);
                addEdge(p2, p3);
                addEdge(p3, p4);
                addEdge(p4, p1);
            }
            break;

        case SketchEntityType::Circle:
            if (!entity.points.isEmpty() && entity.radius > 1e-6) {
                gp_Pnt center = to3D(entity.points[0]);
                gp_Ax2 axis(center, planeNormal, planeXDir);
                gp_Circ circle(axis, entity.radius);
                BRepBuilderAPI_MakeEdge me(circle);
                if (me.IsDone()) {
                    TopoDS_Shape edge = me.Edge();
                    builder.Add(compound, edge);
                }
            }
            break;

        case SketchEntityType::Arc:
            if (!entity.points.isEmpty() && entity.radius > 1e-6 &&
                std::abs(entity.sweepAngle) > 1e-6) {
                gp_Pnt center = to3D(entity.points[0]);
                gp_Ax2 axis(center, planeNormal, planeXDir);
                gp_Circ circle(axis, entity.radius);

                // Convert angles to radians
                double startRad = entity.startAngle * M_PI / 180.0;
                double endRad = (entity.startAngle + entity.sweepAngle) * M_PI / 180.0;

                BRepBuilderAPI_MakeEdge me(circle, startRad, endRad);
                if (me.IsDone()) {
                    TopoDS_Shape edge = me.Edge();
                    builder.Add(compound, edge);
                }
            }
            break;

        default:
            // TODO: Handle splines, text, dimensions
            break;
        }
    }

    Handle(AIS_Shape) aisShape = new AIS_Shape(compound);

    // Set wireframe display mode with a distinct color
    aisShape->SetDisplayMode(AIS_WireFrame);
    Handle(Prs3d_Drawer) drawer = aisShape->Attributes();
    drawer->SetLineAspect(new Prs3d_LineAspect(
        Quantity_Color(0.2, 0.6, 1.0, Quantity_TOC_RGB),  // Light blue
        Aspect_TOL_SOLID, 2.0));

    return aisShape;
}

void FullModeWindow::showSketchPlane(SketchPlane plane, double offset,
                                      PlaneRotationAxis rotAxis, double rotAngle)
{
    if (!m_viewport) return;

    Handle(AIS_InteractiveContext) ctx = m_viewport->context();
    if (ctx.IsNull()) return;

    // Remove existing plane visualization if any
    hideSketchPlane();

    // Create new plane visualization
    m_sketchPlaneVis = new AisSketchPlane(200.0);  // 200mm square plane

    if (plane == SketchPlane::Custom) {
        m_sketchPlaneVis->setCustomPlane(rotAxis, rotAngle, offset);
    } else {
        m_sketchPlaneVis->setPlane(plane, offset);
    }

    // Set transparency
    m_sketchPlaneVis->SetTransparency(0.7);

    // Display the plane
    ctx->Display(m_sketchPlaneVis, Standard_True);
}

void FullModeWindow::hideSketchPlane()
{
    if (m_sketchPlaneVis.IsNull()) return;
    if (!m_viewport) return;

    Handle(AIS_InteractiveContext) ctx = m_viewport->context();
    if (ctx.IsNull()) return;

    ctx->Remove(m_sketchPlaneVis, Standard_True);
    m_sketchPlaneVis.Nullify();
}

void FullModeWindow::saveCurrentSketch()
{
    // Get the sketch name from the timeline
    if (m_timeline->itemCount() == 0)
        return;

    int lastIdx = m_timeline->itemCount() - 1;
    if (m_timeline->featureAt(lastIdx) != TimelineFeature::Sketch)
        return;

    QString sketchName = m_timeline->nameAt(lastIdx);
    bool isNewSketch = (m_currentSketchIndex < 0);

    // Create the completed sketch structure
    CompletedSketch sketch;
    sketch.name = sketchName;
    sketch.plane = m_sketchCanvas->sketchPlane();
    sketch.planeOffset = m_pendingSketchOffset;
    sketch.rotationAxis = m_pendingRotationAxis;
    sketch.rotationAngle = m_pendingRotationAngle;
    sketch.entities = m_sketchCanvas->entities();

    // Create and display the 3D wireframe
    sketch.aisShape = createSketchWireframe(sketch);
    if (!sketch.aisShape.IsNull() && m_viewport) {
        Handle(AIS_InteractiveContext) ctx = m_viewport->context();
        if (!ctx.IsNull()) {
            ctx->Display(sketch.aisShape, Standard_True);
        }
    }

    int sketchIndex;
    if (isNewSketch) {
        // New sketch - add to completed sketches
        // Tree item was already added in enterSketchMode()
        sketchIndex = m_completedSketches.size();
        m_completedSketches.append(sketch);
    } else {
        // Editing existing sketch - remove old wireframe, update in place
        sketchIndex = m_currentSketchIndex;
        CompletedSketch& existing = m_completedSketches[sketchIndex];
        if (!existing.aisShape.IsNull() && m_viewport) {
            Handle(AIS_InteractiveContext) ctx = m_viewport->context();
            if (!ctx.IsNull()) {
                ctx->Remove(existing.aisShape, Standard_False);
            }
        }
        existing = sketch;
    }

    selectSketchInTree(sketchIndex);

    // Select the sketch in the timeline
    m_timeline->setSelectedIndex(lastIdx);

    // Reset editing index
    m_currentSketchIndex = -1;

    statusBar()->showMessage(
        tr("Sketch '%1' saved with %2 entities")
            .arg(sketchName)
            .arg(sketch.entities.size()),
        3000);
}

void FullModeWindow::discardCurrentSketch()
{
    bool isNewSketch = (m_currentSketchIndex < 0);
    int entityCount = m_sketchCanvas->entities().size();

    if (isNewSketch) {
        // New sketch being discarded - remove from both timeline and feature tree
        if (m_timeline->itemCount() > 0) {
            int lastIdx = m_timeline->itemCount() - 1;
            if (m_timeline->featureAt(lastIdx) == TimelineFeature::Sketch) {
                m_timeline->removeItem(lastIdx);
            }
        }

        // Remove from feature tree - the pending sketch was added at m_completedSketches.size()
        // So we need to remove the last sketch item in the tree
        clearSketchesInTree();
        // Re-add existing sketches
        for (int i = 0; i < m_completedSketches.size(); ++i) {
            addSketchToTree(m_completedSketches[i].name, i);
        }

        if (entityCount == 0) {
            statusBar()->showMessage(tr("Empty sketch discarded"), 3000);
        } else {
            statusBar()->showMessage(
                tr("Sketch discarded (%1 entities)").arg(entityCount),
                3000);
        }
    } else {
        // Editing existing sketch - just discard changes, keep original
        statusBar()->showMessage(
            tr("Changes to '%1' discarded").arg(m_completedSketches[m_currentSketchIndex].name),
            3000);
    }

    // Reset the editing index
    m_currentSketchIndex = -1;
}

// ---- Project Loading ------------------------------------------------

void FullModeWindow::loadProjectData()
{
    // Clear existing data first
    clearProjectData();

    // Load project units
    setUnitsFromString(m_project.units());

    // Load parameters
    loadParametersFromProject();

    // Load sketches
    loadSketchesFromProject();

    // Load construction planes
    loadConstructionPlanesFromProject();

    // Populate UI elements
    populateFeatureTree();
    populateTimeline();
}

void FullModeWindow::clearProjectData()
{
    // Clear sketches
    m_completedSketches.clear();
    clearSketchesInTree();

    // Clear bodies in tree
    clearBodiesInTree();

    // Clear construction planes in tree
    clearConstructionPlanesInTree();

    // Clear timeline (except Origin)
    while (m_timeline->itemCount() > 1) {
        m_timeline->removeItem(m_timeline->itemCount() - 1);
    }

    // Reset parameters to defaults
    initDefaultParameters();
}

void FullModeWindow::populateFeatureTree()
{
    // Add bodies to tree
    const auto& shapes = m_project.shapes();
    for (int i = 0; i < shapes.size(); ++i) {
        QString name = QStringLiteral("Body%1").arg(i + 1);
        addBodyToTree(name, i);
    }

    // Add sketches to tree
    for (int i = 0; i < m_completedSketches.size(); ++i) {
        addSketchToTree(m_completedSketches[i].name, i);
    }
}

void FullModeWindow::populateTimeline()
{
    // Add features from project to timeline
    const auto& features = m_project.features();

    for (const auto& feature : features) {
        // Skip Origin (already in timeline)
        if (feature.type == FeatureType::Origin)
            continue;

        TimelineFeature tlFeature;
        switch (feature.type) {
        case FeatureType::Sketch:
            tlFeature = TimelineFeature::Sketch;
            break;
        case FeatureType::Extrude:
            tlFeature = TimelineFeature::Extrude;
            break;
        case FeatureType::Revolve:
            tlFeature = TimelineFeature::Revolve;
            break;
        case FeatureType::Fillet:
            tlFeature = TimelineFeature::Fillet;
            break;
        case FeatureType::Chamfer:
            tlFeature = TimelineFeature::Chamfer;
            break;
        case FeatureType::Hole:
            tlFeature = TimelineFeature::Hole;
            break;
        case FeatureType::Mirror:
            tlFeature = TimelineFeature::Mirror;
            break;
        case FeatureType::Pattern:
            tlFeature = TimelineFeature::Pattern;
            break;
        case FeatureType::Box:
            tlFeature = TimelineFeature::Box;
            break;
        case FeatureType::Cylinder:
            tlFeature = TimelineFeature::Cylinder;
            break;
        case FeatureType::Sphere:
            tlFeature = TimelineFeature::Sphere;
            break;
        case FeatureType::Move:
            tlFeature = TimelineFeature::Move;
            break;
        case FeatureType::Join:
            tlFeature = TimelineFeature::Join;
            break;
        case FeatureType::Cut:
            tlFeature = TimelineFeature::Cut;
            break;
        case FeatureType::Intersect:
            tlFeature = TimelineFeature::Intersect;
            break;
        default:
            continue;  // Skip unknown types
        }

        m_timeline->addItem(tlFeature, feature.name);
    }
}

void FullModeWindow::loadSketchesFromProject()
{
    const auto& projectSketches = m_project.sketches();

    for (const auto& sketchData : projectSketches) {
        CompletedSketch sketch;
        sketch.name = sketchData.name;
        sketch.plane = sketchData.plane;
        sketch.planeOffset = sketchData.planeOffset;
        sketch.rotationAxis = sketchData.rotationAxis;
        sketch.rotationAngle = sketchData.rotationAngle;

        // Convert SketchEntityData to SketchEntity
        for (const auto& entityData : sketchData.entities) {
            SketchEntity entity;
            entity.id = entityData.id;
            entity.type = entityData.type;
            entity.points = entityData.points;
            entity.radius = entityData.radius;
            entity.startAngle = entityData.startAngle;
            entity.sweepAngle = entityData.sweepAngle;
            entity.text = entityData.text;
            entity.constrained = entityData.constrained;
            entity.selected = false;

            sketch.entities.append(entity);
        }

        // Create the 3D wireframe representation
        sketch.aisShape = createSketchWireframe(sketch);

        m_completedSketches.append(sketch);
    }
}

void FullModeWindow::loadParametersFromProject()
{
    const auto& projectParams = m_project.parameters();

    m_parameters.clear();

    for (const auto& paramData : projectParams) {
        Parameter param;
        param.name = paramData.name;
        param.expression = paramData.expression;
        param.value = paramData.value;
        param.unit = paramData.unit;
        param.comment = paramData.comment;
        m_parameters.append(param);
    }

    // If no parameters loaded, use defaults
    if (m_parameters.isEmpty()) {
        initDefaultParameters();
    }
}

void FullModeWindow::loadConstructionPlanesFromProject()
{
    const auto& planes = m_project.constructionPlanes();

    for (const auto& planeData : planes) {
        // Add to feature tree
        addConstructionPlaneToTree(planeData.name, planeData.id);

        // Display in viewport if visible
        if (planeData.visible) {
            displayConstructionPlane(planeData.id);
        }
    }
}

void FullModeWindow::displayConstructionPlane(int planeId)
{
    const ConstructionPlaneData* planeData = m_project.constructionPlaneById(planeId);
    if (!planeData) return;
    if (!m_viewport) return;

    Handle(AIS_InteractiveContext) ctx = m_viewport->context();
    if (ctx.IsNull()) return;

    // Create AisSketchPlane to visualize this construction plane
    Handle(AisSketchPlane) planeVis = new AisSketchPlane(200.0);

    switch (planeData->type) {
    case ConstructionPlaneType::OffsetFromOrigin:
        planeVis->setPlane(planeData->basePlane, planeData->offset);
        break;

    case ConstructionPlaneType::OffsetFromPlane:
        // For offset from another plane, we need to compute the combined transform
        // For now, just use the offset value (simplified)
        // TODO: Properly chain transforms from reference plane
        planeVis->setPlane(SketchPlane::XY, planeData->offset);
        break;

    case ConstructionPlaneType::Angled:
        // Angled plane - use custom transform
        // For now, use primary axis rotation only
        // TODO: Support two-axis rotation
        planeVis->setCustomPlane(planeData->primaryAxis, planeData->primaryAngle, planeData->offset);
        break;
    }

    // Set construction plane appearance (different from sketch plane)
    planeVis->setFillColor(Quantity_Color(0.3, 0.8, 0.3, Quantity_TOC_RGB));  // Green tint
    planeVis->setBorderColor(Quantity_Color(0.2, 0.6, 0.2, Quantity_TOC_RGB));
    planeVis->SetTransparency(0.8);

    // Store reference to plane visualization for later removal
    // For now, just display it - proper management would track by ID
    ctx->Display(planeVis, Standard_True);
}

void FullModeWindow::hideConstructionPlane(int planeId)
{
    Q_UNUSED(planeId);
    // TODO: Track plane visualizations by ID and remove specific one
    // For now, this is a placeholder
}

}  // namespace hobbycad

