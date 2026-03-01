// =====================================================================
//  src/hobbycad/gui/full/fullmodewindow.cpp — Full Mode window
// =====================================================================

#include "fullmodewindow.h"
#include "viewportwidget.h"
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
#include "gui/constructionplanedialog.h"
#include "gui/extrudedialog.h"
#include "gui/revolvedialog.h"
#include "gui/sketchutils.h"

#include <hobbycad/brep/operations.h>
#include <hobbycad/sketch/export.h>
#include <hobbycad/sketch/profiles.h>

#include <QAction>
#include <QComboBox>
#include <QFileDialog>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QSettings>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtMath>

#include <cmath>

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

    m_toolbar = new ModelToolbar(m_toolbarStack);
    m_toolbarStack->addWidget(m_toolbar);

    // Connect FullMode-specific ModelToolbar signals
    connect(m_toolbar, &ModelToolbar::createConstructionPlaneClicked,
            this, &FullModeWindow::onNewConstructionPlane);
    connect(m_toolbar, &ModelToolbar::toolSelected,
            this, &FullModeWindow::onModelToolSelected);

    m_sketchToolbar = new SketchToolbar(m_toolbarStack);
    m_toolbarStack->addWidget(m_sketchToolbar);

    layout->addWidget(m_toolbarStack);

    // Viewport stack (3D viewport vs 2D sketch canvas)
    m_viewportStack = new QStackedWidget(container);

    m_viewport = new ViewportWidget(m_viewportStack);
    m_viewportStack->addWidget(m_viewport);

    m_sketchCanvas = new SketchCanvas(m_viewportStack);
    m_sketchCanvas->setUnitSuffix(unitSuffix());
    m_viewportStack->addWidget(m_sketchCanvas);

    layout->addWidget(m_viewportStack, 1);  // stretch factor 1

    // Connect FullMode-specific sketch toolbar signals (tangent validation)
    connect(m_sketchToolbar, &SketchToolbar::toolSelected,
            this, [this](SketchTool tool, CreationMode mode) {
        // Validate mode before applying
        if (tool == SketchTool::Arc && mode == CreationMode::ArcTangent) {
            // Check if there are supported entities for tangent arc
            bool hasSupportedEntity = false;
            for (const auto& entity : m_sketchCanvas->entities()) {
                if (entity.type == SketchEntityType::Line ||
                    entity.type == SketchEntityType::Rectangle) {
                    hasSupportedEntity = true;
                    break;
                }
            }
            if (!hasSupportedEntity) {
                QMessageBox::information(this, tr("Tangent Arc"),
                    tr("There are no lines or rectangles to create a tangent arc from.\n"
                       "Please draw a line or rectangle first."));
                m_sketchToolbar->revertCreationMode(tool);
                return;
            }
        }

        if (tool == SketchTool::Line && mode == CreationMode::LineTangent) {
            // Check if there are supported entities for tangent line (circles, arcs)
            bool hasSupportedEntity = false;
            for (const auto& entity : m_sketchCanvas->entities()) {
                if (entity.type == SketchEntityType::Circle ||
                    entity.type == SketchEntityType::Arc) {
                    hasSupportedEntity = true;
                    break;
                }
            }
            if (!hasSupportedEntity) {
                QMessageBox::information(this, tr("Tangent Line"),
                    tr("There are no circles or arcs to create a tangent line from.\n"
                       "Please draw a circle or arc first."));
                m_sketchToolbar->revertCreationMode(tool);
                return;
            }
        }

        m_sketchCanvas->setActiveTool(tool);
        m_sketchCanvas->setCreationMode(mode);
    });

    // Connect FullMode-specific sketch canvas signals
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
    connect(m_sketchCanvas, &SketchCanvas::sketchDeselected,
            this, [this]() {
        // Sketch deselected - clear properties panel and reset Create button
        if (QTreeWidget* propsTree = propertiesTree()) {
            propsTree->clear();
        }
        m_sketchToolbar->resetCreateButton();
    });
    connect(m_sketchCanvas, &SketchCanvas::exitRequested,
            this, [this]() {
        // Escape pressed with sketch deselected - show Save/Discard and flash
        if (sketchActionBar()) {
            sketchActionBar()->showAndFlash();
        }
    });

    // Connect delete action to sketch canvas
    if (deleteAction()) {
        connect(deleteAction(), &QAction::triggered,
                m_sketchCanvas, &SketchCanvas::deleteSelectedEntities);
        // Enable/disable based on selection
        connect(m_sketchCanvas, &SketchCanvas::selectionChanged,
                this, [this](int entityId) {
            if (deleteAction()) {
                deleteAction()->setEnabled(entityId >= 0 || !m_sketchCanvas->selectedEntityIds().isEmpty());
            }
            if (selectAllAction()) {
                selectAllAction()->setEnabled(!m_sketchCanvas->entities().isEmpty());
            }
        });
    }

    // Connect select all action
    if (selectAllAction()) {
        connect(selectAllAction(), &QAction::triggered,
                this, [this]() {
            for (const auto& entity : m_sketchCanvas->entities()) {
                m_sketchCanvas->selectEntity(entity.id, true);  // Add to selection
            }
        });
    }

    // Timeline below the viewport
    m_timeline = new TimelineWidget(container);
    layout->addWidget(m_timeline);
    createTimeline();

    // Connect shared sketch signals (toolbar, canvas, undo/redo, action bar, etc.)
    initSketchConnections();

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

    // Initialize default parameters (now in base class)
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

SketchCanvas* FullModeWindow::activeSketchCanvas() const
{
    return m_inSketchMode ? m_sketchCanvas : nullptr;
}

bool FullModeWindow::getSelectedSketchForExport(
    QVector<sketch::Entity>& outEntities,
    QVector<sketch::Constraint>& outConstraints) const
{
    // When in sketch mode, the base class uses activeSketchCanvas() instead
    if (m_inSketchMode)
        return false;

    // Check if a sketch is selected in the timeline
    int selectedIndex = m_timeline->selectedIndex();
    if (selectedIndex < 0)
        return false;

    TimelineFeature feature = m_timeline->featureAt(selectedIndex);
    if (feature != TimelineFeature::Sketch)
        return false;

    int sketchIdx = sketchIndexFromTimelineIndex(selectedIndex);
    if (sketchIdx < 0 || sketchIdx >= m_completedSketches.size())
        return false;

    const CompletedSketch& sketch = m_completedSketches[sketchIdx];
    if (sketch.entities.isEmpty())
        return false;

    // Convert GUI entities to library entities
    std::vector<sketch::Entity> libEntities = toLibraryEntities(sketch.entities);
    outEntities = QVector<sketch::Entity>(libEntities.begin(), libEntities.end());

    // Completed sketches don't store constraints separately, so
    // outConstraints remains empty (constraints are already baked
    // into the entity positions)
    Q_UNUSED(outConstraints);

    return true;
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

// createToolbar() is no longer needed - ModelToolbar handles all button setup internally

void FullModeWindow::createTimeline()
{
    // Start with just the Origin item - other items added as features are created
    m_timeline->addItem(TimelineFeature::Origin, tr("Origin"));
    m_timeline->setFeatureId(0, 0);  // Origin has feature ID 0

    // Connect timeline item selection to properties panel
    connect(m_timeline, &TimelineWidget::itemClicked,
            this, [this](int index) { showFeatureProperties(index); });

    // Connect context menu signals for feature editing
    connect(m_timeline, &TimelineWidget::editFeatureRequested,
            this, &FullModeWindow::onEditFeature);
    connect(m_timeline, &TimelineWidget::renameFeatureRequested,
            this, &FullModeWindow::onRenameFeature);
    connect(m_timeline, &TimelineWidget::deleteFeatureRequested,
            this, &FullModeWindow::onDeleteFeature);
    connect(m_timeline, &TimelineWidget::suppressFeatureRequested,
            this, &FullModeWindow::onSuppressFeature);

    // Connect export signals for sketch context menu
    connect(m_timeline, &TimelineWidget::exportDXFRequested,
            this, &FullModeWindow::onExportSketchDXF);
    connect(m_timeline, &TimelineWidget::exportSVGRequested,
            this, &FullModeWindow::onExportSketchSVG);

    // Connect double-click to edit feature
    connect(m_timeline, &TimelineWidget::itemDoubleClicked,
            this, &FullModeWindow::onEditFeature);

    // Connect drag reorder to update underlying data
    connect(m_timeline, &TimelineWidget::itemMoved,
            this, &FullModeWindow::onFeatureMoved);

    // Connect rollback to show/hide geometry
    connect(m_timeline, &TimelineWidget::rollbackChanged,
            this, &FullModeWindow::onRollbackChanged);
}

void FullModeWindow::populateSketchFeatureProperties(QTreeWidgetItem* parent,
                                                      int timelineIndex,
                                                      const QString& units)
{
    // Find which sketch this timeline item corresponds to
    int sketchIdx = sketchIndexFromTimelineIndex(timelineIndex);

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

    auto* planeItem = new QTreeWidgetItem(parent);
    QStringList planeOptions = {tr("XY"), tr("XZ"), tr("YZ"), tr("Custom")};
    planeItem->setText(0, tr("Plane"));
    planeItem->setText(1, planeOptions.value(planeIdx));
    planeItem->setToolTip(0, tr("Plane"));
    planeItem->setToolTip(1, planeOptions.value(planeIdx));
    planeItem->setData(1, Qt::UserRole, QStringLiteral("dropdown"));
    planeItem->setData(1, Qt::UserRole + 1, planeOptions);
    planeItem->setData(1, Qt::UserRole + 2, planeIdx);

    // Show offset if non-zero
    if (!qFuzzyIsNull(sketchOffset)) {
        auto* offsetItem = new QTreeWidgetItem(parent);
        offsetItem->setText(0, tr("Offset"));
        offsetItem->setText(1, QStringLiteral("%1 %2").arg(sketchOffset, 0, 'g', 6).arg(units));
        offsetItem->setToolTip(0, tr("Offset"));
        offsetItem->setToolTip(1, QStringLiteral("%1 %2").arg(sketchOffset, 0, 'g', 6).arg(units));
    }

    auto* entitiesItem = new QTreeWidgetItem(parent);
    entitiesItem->setText(0, tr("Entities"));
    entitiesItem->setText(1, QString::number(entityCount));
    entitiesItem->setToolTip(0, tr("Entities"));
    entitiesItem->setToolTip(1, QString::number(entityCount));

    auto* constraintsItem = new QTreeWidgetItem(parent);
    constraintsItem->setText(0, tr("Constraints"));
    constraintsItem->setText(1, tr("0"));
    constraintsItem->setToolTip(0, tr("Constraints"));
    constraintsItem->setToolTip(1, tr("0"));
}

void FullModeWindow::onSketchDeselected()
{
    MainWindow::onSketchDeselected();
    showSketchProperties();
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

void FullModeWindow::onCreateSketchClicked()
{
    // Check if a plane is selected in the objects tree
    if (m_objectsTree) {
        QTreeWidgetItem* current = m_objectsTree->currentItem();
        if (current) {
            QString itemType = current->data(0, Qt::UserRole).toString();

            if (itemType == QStringLiteral("origin_plane")) {
                // Origin plane (XY/XZ/YZ) selected - use that plane
                int planeValue = current->data(0, Qt::UserRole + 1).toInt();
                SketchPlane plane = static_cast<SketchPlane>(planeValue);
                m_pendingSketchOffset = 0.0;
                m_pendingRotationAxis = PlaneRotationAxis::X;
                m_pendingRotationAngle = 0.0;
                enterSketchMode(plane);
                return;
            }

            if (itemType == QStringLiteral("construction_plane")) {
                // Construction plane selected - use its parameters
                int planeId = current->data(0, Qt::UserRole + 1).toInt();
                const ConstructionPlaneData* cpData = m_project.constructionPlaneById(planeId);
                if (cpData) {
                    m_pendingSketchOffset = cpData->offset;
                    m_pendingRotationAxis = cpData->primaryAxis;
                    m_pendingRotationAngle = cpData->primaryAngle;
                    enterSketchMode(SketchPlane::Custom);
                    return;
                }
            }
        }
    }

    // No plane selected - show plane selection dialog
    SketchPlaneDialog dialog(this);
    dialog.setAvailableConstructionPlanes(m_project.constructionPlanes());

    if (dialog.exec() != QDialog::Accepted) {
        return;  // User cancelled
    }

    SketchPlane plane = dialog.selectedPlane();
    int constructionPlaneId = dialog.constructionPlaneId();

    m_pendingSketchOffset = dialog.offset();
    m_pendingRotationAxis = dialog.rotationAxis();
    m_pendingRotationAngle = dialog.rotationAngle();

    // If using a construction plane, get its parameters
    if (constructionPlaneId >= 0) {
        const ConstructionPlaneData* cpData = m_project.constructionPlaneById(constructionPlaneId);
        if (cpData) {
            plane = SketchPlane::Custom;
            m_pendingSketchOffset = dialog.offset() + cpData->offset;
            m_pendingRotationAxis = cpData->primaryAxis;
            m_pendingRotationAngle = cpData->primaryAngle;
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
    defaultData.name = defaultName.toStdString();
    dialog.setPlaneData(defaultData);

    if (dialog.exec() != QDialog::Accepted) {
        return;  // User cancelled
    }

    ConstructionPlaneData planeData = dialog.planeData();
    planeData.id = m_project.nextConstructionPlaneId();

    // Add to project
    m_project.addConstructionPlane(planeData);

    // Add to feature tree
    addConstructionPlaneToTree(QString::fromStdString(planeData.name), planeData.id);

    // Display in viewport if visible
    if (planeData.visible) {
        displayConstructionPlane(planeData.id);
    }

    // Select in tree
    selectConstructionPlaneInTree(planeData.id);

    statusBar()->showMessage(
        tr("Construction plane '%1' created").arg(QString::fromStdString(planeData.name)),
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
    nameItem->setText(1, QString::fromStdString(planeData->name));
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
        refPlaneItem->setText(1, refPlane ? QString::fromStdString(refPlane->name) : tr("(none)"));
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
    MainWindow::enterSketchMode(plane);

    // Determine if we're creating a new sketch or editing existing
    bool isNewSketch = (m_currentSketchIndex < 0);

    QString sketchName;
    if (isNewSketch) {
        int sketchCount = 0;
        for (int i = 0; i < m_timeline->itemCount(); ++i) {
            if (m_timeline->featureAt(i) == TimelineFeature::Sketch) {
                ++sketchCount;
            }
        }
        sketchName = tr("Sketch%1").arg(sketchCount + 1);
        m_pendingSketchTimelineIdx = m_timeline->addItemAtRollback(TimelineFeature::Sketch, sketchName);

        int pendingIndex = m_completedSketches.size();
        addSketchToTree(sketchName, pendingIndex);
    } else {
        sketchName = m_completedSketches[m_currentSketchIndex].name;
        m_pendingSketchTimelineIdx = timelineIndexFromSketchIndex(m_currentSketchIndex);
    }

    m_timeline->setSelectedIndex(m_pendingSketchTimelineIdx >= 0 ? m_pendingSketchTimelineIdx : m_timeline->itemCount() - 1);

    // Update properties to show sketch settings
    QTreeWidget* propsTree = propertiesTree();
    if (propsTree) {
        propsTree->clear();

        auto* nameItem = new QTreeWidgetItem(propsTree);
        nameItem->setText(0, tr("Name"));
        nameItem->setText(1, sketchName);
        nameItem->setFlags(nameItem->flags() | Qt::ItemIsEditable);

        auto* planeItem = new QTreeWidgetItem(propsTree);
        planeItem->setText(0, tr("Plane"));
        QStringList planes = {tr("XY"), tr("XZ"), tr("YZ")};
        int planeIdx = static_cast<int>(plane);
        planeItem->setText(1, planes.value(planeIdx));
        planeItem->setData(1, Qt::UserRole, QStringLiteral("dropdown"));
        planeItem->setData(1, Qt::UserRole + 1, planes);
        planeItem->setData(1, Qt::UserRole + 2, planeIdx);

        auto* offsetItem = new QTreeWidgetItem(propsTree);
        offsetItem->setText(0, tr("Offset"));
        offsetItem->setText(1, tr("%1 mm").arg(m_pendingSketchOffset, 0, 'g', 6));

        auto* entitiesItem = new QTreeWidgetItem(propsTree);
        entitiesItem->setText(0, tr("Entities"));
        entitiesItem->setText(1, QString::number(m_sketchCanvas->entities().size()));

        propsTree->expandAll();
    }
}

void FullModeWindow::exitSketchMode()
{
    MainWindow::exitSketchMode();

    // Switch back to 3D viewport
    m_viewportStack->setCurrentWidget(m_viewport);
}

void FullModeWindow::onSketchEntityModified(int entityId)
{
    // Defer tree refresh so Qt can close any active inline editor
    // before we destroy its item via clear().
    QTimer::singleShot(0, this, [this, entityId]() {
        if (QTreeWidget* pt = propertiesTree()) pt->blockSignals(true);
        showSketchEntityProperties(entityId);
        if (QTreeWidget* pt = propertiesTree()) pt->blockSignals(false);
    });
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
            if (!entity.points.empty()) {
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
                gp_Pnt p2 = to3D(QPointF(entity.points[1].x, entity.points[0].y));
                gp_Pnt p3 = to3D(entity.points[1]);
                gp_Pnt p4 = to3D(QPointF(entity.points[0].x, entity.points[1].y));

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
            if (!entity.points.empty() && entity.radius > 1e-6) {
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
            if (!entity.points.empty() && entity.radius > 1e-6 &&
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
    // Get the sketch from the timeline at the pending index
    if (m_timeline->itemCount() == 0)
        return;

    int timelineIdx = m_pendingSketchTimelineIdx;
    if (timelineIdx < 0 || timelineIdx >= m_timeline->itemCount())
        timelineIdx = m_timeline->itemCount() - 1;

    if (m_timeline->featureAt(timelineIdx) != TimelineFeature::Sketch)
        return;

    QString sketchName = m_timeline->nameAt(timelineIdx);
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
        // Assign a new feature ID
        sketch.featureId = m_nextFeatureId++;

        // New sketch - calculate correct position in sketches array based on timeline position
        // Count how many sketches come before this timeline index
        int insertPos = 0;
        for (int i = 0; i < timelineIdx; ++i) {
            if (m_timeline->featureAt(i) == TimelineFeature::Sketch) {
                ++insertPos;
            }
        }

        // Insert at calculated position
        m_completedSketches.insert(insertPos, sketch);
        sketchIndex = insertPos;

        // Update feature tree (need to rebuild since indices shifted)
        clearSketchesInTree();
        for (int i = 0; i < m_completedSketches.size(); ++i) {
            addSketchToTree(m_completedSketches[i].name, i);
        }

        // Set the feature ID in the timeline for dependency tracking
        m_timeline->setFeatureId(timelineIdx, sketch.featureId);
        // Sketches don't depend on anything by default (just the Origin, implicitly)
        m_timeline->setDependencies(timelineIdx, {});
    } else {
        // Editing existing sketch - remove old wireframe, update in place
        sketchIndex = m_currentSketchIndex;
        CompletedSketch& existing = m_completedSketches[sketchIndex];

        // Preserve the feature ID when editing
        sketch.featureId = existing.featureId;

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
    m_timeline->setSelectedIndex(timelineIdx);

    // Reset editing indices
    m_currentSketchIndex = -1;
    m_pendingSketchTimelineIdx = -1;

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

// ---- Timeline Context Menu Handlers ---------------------------------

int FullModeWindow::sketchIndexFromTimelineIndex(int timelineIndex) const
{
    // Count how many Sketch items come before this index
    int sketchCount = 0;
    for (int i = 0; i <= timelineIndex; ++i) {
        if (m_timeline->featureAt(i) == TimelineFeature::Sketch) {
            if (i == timelineIndex) {
                return sketchCount;
            }
            ++sketchCount;
        }
    }
    return -1;  // Not a sketch
}

int FullModeWindow::timelineIndexFromSketchIndex(int sketchIndex) const
{
    // Find the timeline index for the N-th sketch
    int sketchCount = 0;
    for (int i = 0; i < m_timeline->itemCount(); ++i) {
        if (m_timeline->featureAt(i) == TimelineFeature::Sketch) {
            if (sketchCount == sketchIndex) {
                return i;
            }
            ++sketchCount;
        }
    }
    return -1;  // Not found
}

std::optional<TimelineFeature> FullModeWindow::validateFeatureAction(
    int index, const QString& actionVerb) const
{
    if (index < 0 || index >= m_timeline->itemCount())
        return std::nullopt;

    TimelineFeature feature = m_timeline->featureAt(index);
    if (feature == TimelineFeature::Origin) {
        statusBar()->showMessage(
            tr("Origin cannot be %1").arg(actionVerb), 3000);
        return std::nullopt;
    }
    return feature;
}

std::optional<FullModeWindow::SketchProfilesResult>
FullModeWindow::getSelectedSketchProfiles(const QString& operationName)
{
    int selectedIndex = m_timeline->selectedIndex();
    if (selectedIndex < 0) {
        QMessageBox::information(this, operationName,
            tr("Please select a sketch in the timeline first."));
        return std::nullopt;
    }

    TimelineFeature feature = m_timeline->featureAt(selectedIndex);
    if (feature != TimelineFeature::Sketch) {
        QMessageBox::information(this, operationName,
            tr("Please select a sketch to %1.").arg(operationName.toLower()));
        return std::nullopt;
    }

    int sketchIdx = sketchIndexFromTimelineIndex(selectedIndex);
    if (sketchIdx < 0 || sketchIdx >= m_completedSketches.size()) {
        QMessageBox::warning(this, operationName,
            tr("Could not find sketch data."));
        return std::nullopt;
    }

    const CompletedSketch& sketch = m_completedSketches[sketchIdx];
    std::vector<sketch::Entity> libEntities = toLibraryEntities(sketch.entities);

    sketch::ProfileDetectionOptions options;
    options.excludeConstruction = true;
    std::vector<sketch::Profile> profiles = sketch::detectProfiles(libEntities, options);

    if (profiles.empty()) {
        QMessageBox::warning(this, operationName,
            tr("No closed profiles found in the sketch.\n"
               "Make sure the sketch contains a closed loop."));
        return std::nullopt;
    }

    return SketchProfilesResult{&sketch, std::move(libEntities), std::move(profiles)};
}

void FullModeWindow::onEditFeature(int index)
{
    auto feature = validateFeatureAction(index, tr("edited"));
    if (!feature) return;

    switch (*feature) {
    case TimelineFeature::Origin:
        break;  // unreachable — validateFeatureAction filters Origin

    case TimelineFeature::Sketch:
        {
            int sketchIdx = sketchIndexFromTimelineIndex(index);
            if (sketchIdx >= 0 && sketchIdx < m_completedSketches.size()) {
                // Set up for editing existing sketch
                m_currentSketchIndex = sketchIdx;
                const CompletedSketch& sketch = m_completedSketches[sketchIdx];

                // Store the plane parameters for editing
                m_pendingSketchOffset = sketch.planeOffset;
                m_pendingRotationAxis = sketch.rotationAxis;
                m_pendingRotationAngle = sketch.rotationAngle;

                // Load the sketch entities into the canvas
                m_sketchCanvas->setEntities(sketch.entities);

                // Enter sketch mode on the same plane
                enterSketchMode(sketch.plane);
            }
        }
        break;

    default:
        // TODO: Implement editing for other feature types (Extrude, Revolve, etc.)
        statusBar()->showMessage(
            tr("Editing %1 features not yet implemented").arg(m_timeline->nameAt(index)),
            3000);
        break;
    }
}

void FullModeWindow::onRenameFeature(int index)
{
    auto feature = validateFeatureAction(index, tr("renamed"));
    if (!feature) return;

    QString currentName = m_timeline->nameAt(index);

    // Show rename dialog
    bool ok;
    QString newName = QInputDialog::getText(this,
        tr("Rename Feature"),
        tr("New name:"),
        QLineEdit::Normal,
        currentName,
        &ok);

    if (ok && !newName.isEmpty() && newName != currentName) {
        // Update the timeline item
        // We need to remove and re-add since TimelineWidget doesn't have a rename API
        // For now, just update the internal data structures

        if (*feature == TimelineFeature::Sketch) {
            int sketchIdx = sketchIndexFromTimelineIndex(index);
            if (sketchIdx >= 0 && sketchIdx < m_completedSketches.size()) {
                m_completedSketches[sketchIdx].name = newName;

                // Rebuild the timeline and tree with the new name
                // (A cleaner approach would be to add a rename method to TimelineWidget)
                populateTimeline();
                clearSketchesInTree();
                for (int i = 0; i < m_completedSketches.size(); ++i) {
                    addSketchToTree(m_completedSketches[i].name, i);
                }

                // Mark project as modified
                m_project.setModified(true);

                statusBar()->showMessage(
                    tr("Renamed to '%1'").arg(newName), 3000);
            }
        }
        // TODO: Handle other feature types
    }
}

void FullModeWindow::onDeleteFeature(int index)
{
    auto feature = validateFeatureAction(index, tr("deleted"));
    if (!feature) return;

    QString featureName = m_timeline->nameAt(index);

    // Confirm deletion
    QMessageBox::StandardButton reply = QMessageBox::question(this,
        tr("Delete Feature"),
        tr("Are you sure you want to delete '%1'?\n\n"
           "This action cannot be undone.").arg(featureName),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);

    if (reply != QMessageBox::Yes)
        return;

    if (*feature == TimelineFeature::Sketch) {
        int sketchIdx = sketchIndexFromTimelineIndex(index);
        if (sketchIdx >= 0 && sketchIdx < m_completedSketches.size()) {
            // Remove from viewport if displayed
            const CompletedSketch& sketch = m_completedSketches[sketchIdx];
            if (!sketch.aisShape.IsNull() && m_viewport && !m_viewport->context().IsNull()) {
                m_viewport->context()->Remove(sketch.aisShape, false);
                m_viewport->context()->UpdateCurrentViewer();
            }

            // Remove from completed sketches
            m_completedSketches.remove(sketchIdx);

            // Remove from timeline
            m_timeline->removeItem(index);

            // Rebuild feature tree
            clearSketchesInTree();
            for (int i = 0; i < m_completedSketches.size(); ++i) {
                addSketchToTree(m_completedSketches[i].name, i);
            }

            // Mark project as modified
            m_project.setModified(true);

            statusBar()->showMessage(
                tr("Deleted '%1'").arg(featureName), 3000);
        }
    }
    // TODO: Handle other feature types
}

void FullModeWindow::onSuppressFeature(int index, bool suppress)
{
    auto feature = validateFeatureAction(index, tr("suppressed"));
    if (!feature) return;

    QString featureName = m_timeline->nameAt(index);

    if (*feature == TimelineFeature::Sketch) {
        int sketchIdx = sketchIndexFromTimelineIndex(index);
        if (sketchIdx >= 0 && sketchIdx < m_completedSketches.size()) {
            CompletedSketch& sketch = m_completedSketches[sketchIdx];

            if (suppress) {
                // Hide the sketch from viewport
                if (!sketch.aisShape.IsNull() && m_viewport && !m_viewport->context().IsNull()) {
                    m_viewport->context()->Erase(sketch.aisShape, false);
                    m_viewport->context()->UpdateCurrentViewer();
                }
                statusBar()->showMessage(
                    tr("Suppressed '%1'").arg(featureName), 3000);
            } else {
                // Show the sketch in viewport
                if (!sketch.aisShape.IsNull() && m_viewport && !m_viewport->context().IsNull()) {
                    m_viewport->context()->Display(sketch.aisShape, false);
                    m_viewport->context()->UpdateCurrentViewer();
                }
                statusBar()->showMessage(
                    tr("Unsuppressed '%1'").arg(featureName), 3000);
            }

            // Mark project as modified
            m_project.setModified(true);
        }
    }
    // TODO: Handle other feature types, store suppression state
}

void FullModeWindow::onExportSketchDXF(int index)
{
    int sketchIdx = sketchIndexFromTimelineIndex(index);
    if (sketchIdx < 0 || sketchIdx >= m_completedSketches.size())
        return;

    const CompletedSketch& sketch = m_completedSketches[sketchIdx];
    if (sketch.entities.isEmpty()) {
        QMessageBox::information(this, tr("Export DXF"),
            tr("The sketch '%1' is empty.").arg(sketch.name));
        return;
    }

    QString filePath = QFileDialog::getSaveFileName(
        this,
        tr("Export DXF File"),
        sketch.name + QStringLiteral(".dxf"),
        tr("DXF Files (*.dxf);;All Files (*)"));

    if (filePath.isEmpty()) return;

    if (!filePath.toLower().endsWith(QLatin1String(".dxf"))) {
        filePath += QStringLiteral(".dxf");
    }

    std::vector<sketch::Entity> entities = toLibraryEntities(sketch.entities);

    sketch::DXFExportOptions options;
    bool success = sketch::exportSketchToDXF(entities, filePath.toStdString(), options);

    if (!success) {
        QMessageBox::critical(this, tr("Export Failed"),
            tr("Failed to export DXF file."));
        return;
    }

    statusBar()->showMessage(
        tr("Exported '%1' (%2 entities) to DXF file")
            .arg(sketch.name).arg(entities.size()), 5000);
}

void FullModeWindow::onExportSketchSVG(int index)
{
    int sketchIdx = sketchIndexFromTimelineIndex(index);
    if (sketchIdx < 0 || sketchIdx >= m_completedSketches.size())
        return;

    const CompletedSketch& sketch = m_completedSketches[sketchIdx];
    if (sketch.entities.isEmpty()) {
        QMessageBox::information(this, tr("Export SVG"),
            tr("The sketch '%1' is empty.").arg(sketch.name));
        return;
    }

    QString filePath = QFileDialog::getSaveFileName(
        this,
        tr("Export SVG File"),
        sketch.name + QStringLiteral(".svg"),
        tr("SVG Files (*.svg);;All Files (*)"));

    if (filePath.isEmpty()) return;

    if (!filePath.toLower().endsWith(QLatin1String(".svg"))) {
        filePath += QStringLiteral(".svg");
    }

    std::vector<sketch::Entity> entities = toLibraryEntities(sketch.entities);

    sketch::SVGExportOptions options;
    std::vector<sketch::Constraint> stdConstraints;  // No constraints for completed sketches
    bool success = sketch::exportSketchToSVG(entities, stdConstraints, filePath.toStdString(), options);

    if (!success) {
        QMessageBox::critical(this, tr("Export Failed"),
            tr("Failed to export SVG file."));
        return;
    }

    statusBar()->showMessage(
        tr("Exported '%1' (%2 entities) to SVG file")
            .arg(sketch.name).arg(entities.size()), 5000);
}

void FullModeWindow::onFeatureMoved(int fromIndex, int toIndex)
{
    // The timeline UI has already been reordered by TimelineWidget::moveItem().
    // Now we need to update the underlying data structures to match.
    // Note: Dependencies are validated by TimelineWidget::canMoveItem() before the move.

    if (fromIndex < 0 || toIndex < 0)
        return;

    // Use feature ID to find the moved item in our data structures
    int movedFeatureId = m_timeline->featureIdAt(toIndex);
    TimelineFeature featureType = m_timeline->featureAt(toIndex);

    if (featureType == TimelineFeature::Sketch && movedFeatureId > 0) {
        // Find the sketch with this feature ID
        int sketchIdx = -1;
        for (int i = 0; i < m_completedSketches.size(); ++i) {
            if (m_completedSketches[i].featureId == movedFeatureId) {
                sketchIdx = i;
                break;
            }
        }

        if (sketchIdx >= 0) {
            // Calculate target position based on other sketches' positions
            // Count how many sketches come before toIndex in the timeline
            int targetPos = 0;
            for (int i = 0; i < toIndex; ++i) {
                if (m_timeline->featureAt(i) == TimelineFeature::Sketch) {
                    ++targetPos;
                }
            }

            // Reorder if needed
            if (sketchIdx != targetPos) {
                CompletedSketch sketch = m_completedSketches.takeAt(sketchIdx);
                // Adjust target if we removed from before target
                if (sketchIdx < targetPos) {
                    --targetPos;
                }
                m_completedSketches.insert(targetPos, sketch);

                // Update the feature tree to match
                clearSketchesInTree();
                for (int i = 0; i < m_completedSketches.size(); ++i) {
                    addSketchToTree(m_completedSketches[i].name, i);
                }

                // Mark project as modified
                m_project.setModified(true);

                statusBar()->showMessage(
                    tr("Moved '%1'").arg(sketch.name), 3000);
            }
        }
    }

    // If the moved item isn't a sketch, we don't need to do anything yet
    // (no other feature types have backing data structures currently)
}

void FullModeWindow::onRollbackChanged(int index)
{
    // Show/hide 3D geometry based on rollback position
    // Features after the rollback position should be hidden

    if (!m_viewport || m_viewport->context().IsNull())
        return;

    Handle(AIS_InteractiveContext) ctx = m_viewport->context();

    // Process all sketches
    for (int i = 0; i < m_completedSketches.size(); ++i) {
        CompletedSketch& sketch = m_completedSketches[i];

        if (sketch.aisShape.IsNull())
            continue;

        // Find this sketch's timeline index
        int timelineIdx = timelineIndexFromSketchIndex(i);
        if (timelineIdx < 0)
            continue;

        // Determine if this sketch should be visible
        bool shouldBeVisible = (index < 0 || timelineIdx <= index);

        // Also check individual suppression
        if (sketch.suppressed)
            shouldBeVisible = false;

        // Show or hide accordingly
        if (shouldBeVisible) {
            if (!ctx->IsDisplayed(sketch.aisShape)) {
                ctx->Display(sketch.aisShape, Standard_False);
            }
        } else {
            if (ctx->IsDisplayed(sketch.aisShape)) {
                ctx->Erase(sketch.aisShape, Standard_False);
            }
        }
    }

    // TODO: When extrudes, revolves, etc. are implemented, process them here too

    ctx->UpdateCurrentViewer();

    // Update status bar
    if (index < 0) {
        statusBar()->showMessage(tr("Rollback cleared - all features active"), 3000);
    } else {
        QString featureName = m_timeline->nameAt(index);
        statusBar()->showMessage(tr("Rolled back to '%1'").arg(featureName), 3000);
    }
}

// ---- Project Loading ------------------------------------------------

void FullModeWindow::loadProjectData()
{
    // Clear existing data first
    clearProjectData();

    // Load project units
    setUnitsFromString(QString::fromStdString(m_project.units()));
    if (m_sketchCanvas) {
        m_sketchCanvas->setUnitSuffix(unitSuffix());
    }

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

        m_timeline->addItem(tlFeature, QString::fromStdString(feature.name));
    }
}

void FullModeWindow::loadSketchesFromProject()
{
    const auto& projectSketches = m_project.sketches();

    for (const auto& sketchData : projectSketches) {
        CompletedSketch sketch;
        sketch.name = QString::fromStdString(sketchData.name);
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
            entity.sides = entityData.sides;
            entity.majorRadius = entityData.majorRadius;
            entity.minorRadius = entityData.minorRadius;
            entity.text = entityData.text;
            entity.fontFamily = entityData.fontFamily;
            entity.fontSize = entityData.fontSize;
            entity.fontBold = entityData.fontBold;
            entity.fontItalic = entityData.fontItalic;
            entity.textRotation = entityData.textRotation;
            entity.arcFlipped = entityData.arcFlipped;
            entity.constrained = entityData.constrained;
            entity.isConstruction = entityData.isConstruction;
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
        addConstructionPlaneToTree(QString::fromStdString(planeData.name), planeData.id);

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

// ---- Model Tool Handlers ----

void FullModeWindow::onModelToolSelected(ModelTool tool)
{
    switch (tool) {
    case ModelTool::Extrude:
    case ModelTool::CutExtrude:
        performExtrude();
        break;
    case ModelTool::Revolve:
    case ModelTool::CutRevolve:
        performRevolve();
        break;
    default:
        // Other tools not yet implemented
        break;
    }
}

void FullModeWindow::performExtrude()
{
    auto sketchData = getSelectedSketchProfiles(tr("Extrude"));
    if (!sketchData) return;

    const CompletedSketch& sketch = *sketchData->sketch;
    auto& profiles = sketchData->profiles;
    auto& libEntities = sketchData->libEntities;

    // Show extrude dialog
    ExtrudeDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    double distance = dialog.distance();
    ExtrudeDirection direction = dialog.direction();
    ExtrudeOperation operation = dialog.operation();

    // Determine extrusion direction based on sketch plane
    gp_Dir extrudeDir(0, 0, 1);  // Default: Z-up for XY plane
    switch (sketch.plane) {
    case SketchPlane::XY:
        extrudeDir = gp_Dir(0, 0, 1);
        break;
    case SketchPlane::XZ:
        extrudeDir = gp_Dir(0, 1, 0);
        break;
    case SketchPlane::YZ:
        extrudeDir = gp_Dir(1, 0, 0);
        break;
    case SketchPlane::Custom:
        // TODO: Calculate normal from rotation parameters
        extrudeDir = gp_Dir(0, 0, 1);
        break;
    }

    if (direction == ExtrudeDirection::NormalReverse) {
        extrudeDir.Reverse();
    }

    // Perform extrusion using library
    brep::OperationResult result;

    if (direction == ExtrudeDirection::TwoSided) {
        result = brep::extrudeProfileSymmetric(
            profiles[0], libEntities, extrudeDir, distance, true);
    } else {
        result = brep::extrudeProfile(
            profiles[0], libEntities, extrudeDir, distance);
    }

    if (!result.success) {
        QMessageBox::critical(this, tr("Extrude Failed"),
            tr("Extrusion failed: %1").arg(QString::fromStdString(result.errorMessage)));
        return;
    }

    // Handle operation type (NewBody, Join, Cut, Intersect)
    TopoDS_Shape finalShape = result.shape;

    if (operation != ExtrudeOperation::NewBody && !m_solidBodies.isEmpty()) {
        // Combine with existing body
        TopoDS_Shape existingBody = m_solidBodies.last();
        brep::OperationResult boolResult;

        switch (operation) {
        case ExtrudeOperation::Join:
            boolResult = brep::fuseShapes(existingBody, finalShape);
            break;
        case ExtrudeOperation::Cut:
            boolResult = brep::cutShape(existingBody, finalShape);
            break;
        case ExtrudeOperation::Intersect:
            boolResult = brep::intersectShapes(existingBody, finalShape);
            break;
        default:
            break;
        }

        if (boolResult.success) {
            // Replace the last body
            m_solidBodies.last() = boolResult.shape;
            finalShape = boolResult.shape;

            // Update display
            Handle(AIS_InteractiveContext) ctx = m_viewport->context();
            ctx->Remove(m_solidAisShapes.last(), Standard_False);
            m_solidAisShapes.last() = new AIS_Shape(finalShape);
            ctx->Display(m_solidAisShapes.last(), Standard_True);
        } else {
            QMessageBox::warning(this, tr("Boolean Operation Failed"),
                tr("Boolean operation failed: %1").arg(QString::fromStdString(boolResult.errorMessage)));
        }
    } else {
        // Add as new body
        m_solidBodies.append(finalShape);
        m_document.addShape(finalShape);  // Add to document for export

        Handle(AIS_Shape) aisShape = new AIS_Shape(finalShape);
        m_solidAisShapes.append(aisShape);

        Handle(AIS_InteractiveContext) ctx = m_viewport->context();
        ctx->Display(aisShape, Standard_True);
    }

    // Add extrude feature to timeline
    int featureId = m_nextFeatureId++;
    int insertIdx = m_timeline->addItemAtRollback(TimelineFeature::Extrude,
        tr("Extrude%1").arg(m_solidBodies.size()));
    m_timeline->setFeatureId(insertIdx, featureId);
    m_timeline->setDependencies(insertIdx, {sketch.featureId});

    // Fit view to show the new solid
    m_viewport->fitAll();

    statusBar()->showMessage(tr("Extrusion completed"), 3000);
}

void FullModeWindow::performRevolve()
{
    auto sketchData = getSelectedSketchProfiles(tr("Revolve"));
    if (!sketchData) return;

    const CompletedSketch& sketch = *sketchData->sketch;
    auto& profiles = sketchData->profiles;
    auto& libEntities = sketchData->libEntities;

    // Show revolve dialog
    RevolveDialog dialog(this);

    // Find construction lines for potential axis
    QVector<QPair<int, QString>> axisLines;
    for (const SketchEntity& e : sketch.entities) {
        if (e.type == SketchEntityType::Line && e.isConstruction) {
            axisLines.append({e.id, tr("Line %1").arg(e.id)});
        }
    }
    dialog.setAxisLines(axisLines);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    double angle = dialog.angle();
    RevolveAxis axisType = dialog.axis();
    RevolveOperation operation = dialog.operation();

    // Determine revolution axis
    gp_Ax1 axis;
    gp_Pnt origin(0, 0, 0);

    switch (axisType) {
    case RevolveAxis::XAxis:
        axis = gp_Ax1(origin, gp_Dir(1, 0, 0));
        break;
    case RevolveAxis::YAxis:
        axis = gp_Ax1(origin, gp_Dir(0, 1, 0));
        break;
    case RevolveAxis::SketchLine: {
        int lineId = dialog.axisLineId();
        if (lineId < 0) {
            QMessageBox::warning(this, tr("Revolve"),
                tr("Please select a construction line for the axis."));
            return;
        }

        // Find the line entity
        const SketchEntity* lineEntity = nullptr;
        for (const SketchEntity& e : sketch.entities) {
            if (e.id == lineId) {
                lineEntity = &e;
                break;
            }
        }

        if (!lineEntity || lineEntity->points.size() < 2) {
            QMessageBox::warning(this, tr("Revolve"),
                tr("Invalid axis line selected."));
            return;
        }

        QPointF p1 = lineEntity->points[0];
        QPointF p2 = lineEntity->points[1];
        gp_Pnt pt1(p1.x(), p1.y(), 0);
        gp_Pnt pt2(p2.x(), p2.y(), 0);
        gp_Dir dir(pt2.X() - pt1.X(), pt2.Y() - pt1.Y(), 0);
        axis = gp_Ax1(pt1, dir);
        break;
    }
    }

    // Perform revolution
    brep::OperationResult result = brep::revolveProfile(
        profiles[0], libEntities, axis, angle);

    if (!result.success) {
        QMessageBox::critical(this, tr("Revolve Failed"),
            tr("Revolution failed: %1").arg(QString::fromStdString(result.errorMessage)));
        return;
    }

    // Handle operation type
    TopoDS_Shape finalShape = result.shape;

    if (operation != RevolveOperation::NewBody && !m_solidBodies.isEmpty()) {
        TopoDS_Shape existingBody = m_solidBodies.last();
        brep::OperationResult boolResult;

        switch (operation) {
        case RevolveOperation::Join:
            boolResult = brep::fuseShapes(existingBody, finalShape);
            break;
        case RevolveOperation::Cut:
            boolResult = brep::cutShape(existingBody, finalShape);
            break;
        case RevolveOperation::Intersect:
            boolResult = brep::intersectShapes(existingBody, finalShape);
            break;
        default:
            break;
        }

        if (boolResult.success) {
            m_solidBodies.last() = boolResult.shape;
            finalShape = boolResult.shape;

            Handle(AIS_InteractiveContext) ctx = m_viewport->context();
            ctx->Remove(m_solidAisShapes.last(), Standard_False);
            m_solidAisShapes.last() = new AIS_Shape(finalShape);
            ctx->Display(m_solidAisShapes.last(), Standard_True);
        } else {
            QMessageBox::warning(this, tr("Boolean Operation Failed"),
                tr("Boolean operation failed: %1").arg(QString::fromStdString(boolResult.errorMessage)));
        }
    } else {
        m_solidBodies.append(finalShape);
        m_document.addShape(finalShape);  // Add to document for export

        Handle(AIS_Shape) aisShape = new AIS_Shape(finalShape);
        m_solidAisShapes.append(aisShape);

        Handle(AIS_InteractiveContext) ctx = m_viewport->context();
        ctx->Display(aisShape, Standard_True);
    }

    // Add revolve feature to timeline
    int featureId = m_nextFeatureId++;
    int insertIdx = m_timeline->addItemAtRollback(TimelineFeature::Revolve,
        tr("Revolve%1").arg(m_solidBodies.size()));
    m_timeline->setFeatureId(insertIdx, featureId);
    m_timeline->setDependencies(insertIdx, {sketch.featureId});

    m_viewport->fitAll();

    statusBar()->showMessage(tr("Revolution completed"), 3000);
}

}  // namespace hobbycad

