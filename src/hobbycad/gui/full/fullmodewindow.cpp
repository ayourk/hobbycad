// =====================================================================
//  src/hobbycad/gui/full/fullmodewindow.cpp — Full Mode window
// =====================================================================

#include <algorithm>
#include "../propertyrow.h"
#include <hobbycad/units.h>
#include "fullmodewindow.h"
#include "../planetransformpanel.h"
#include <QPushButton>
#include "viewportwidget.h"

#include <V3d_View.hxx>
#include <Graphic3d_ClipPlane.hxx>
#include <gp_Pln.hxx>
#include "gui/changelogpanel.h"
#include "gui/clipanel.h"
#include "gui/modeltoolbar.h"
#include "gui/toolbarbutton.h"
#include "gui/toolbardropdown.h"
#include "gui/timelinewidget.h"
#include "gui/formulafield.h"
#include <hobbycad/plane_frame.h>
#include "gui/sketchtoolbar.h"
#include "gui/sketchcanvas.h"
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
#include <AIS_InteractiveObject.hxx>
#include <NCollection_List.hxx>
#include <AIS_Shape.hxx>
#include <BRep_Builder.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <gp_Ax1.hxx>
#include <hobbycad/project_undo.h>
#include <gp_Dir.hxx>
#include <gp_Vec.hxx>
#include <gp_Ax3.hxx>
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

namespace {

/// The session's body operation for a dialog's (Extrude and Revolve name
/// theirs alike).
template <typename Operation>
hobbycad::BodyOperation bodyOperationFor(Operation op)
{
    switch (op) {
    case Operation::Join:      return hobbycad::BodyOperation::Join;
    case Operation::Cut:       return hobbycad::BodyOperation::Cut;
    case Operation::Intersect: return hobbycad::BodyOperation::Intersect;
    case Operation::NewBody:   break;
    }
    return hobbycad::BodyOperation::NewBody;
}

}  // namespace

// constructionPlaneFrame/originPlaneFrame/axisDir now live in the library
// (hobbycad/plane_frame.h) so the viewport and the sketch mapping share
// one authoritative right-handed frame.

// Viewport lifecycle: a failed view is dropped; a startup --exec sketch and a
// deferred sketch 3D seed wait for the view to come up.
void FullModeWindow::connectViewportSignals()
{
    // A view that fails to come up is dropped (with a notice) rather than left
    // blank. Queued so the drop runs after the paintEvent that reported it.
    connect(m_viewport, &ViewportWidget::viewInitFailed, this,
            [this]() {
        dropViewport();
        // The 3D view never came up, but a --exec sketch still has to open; 2D
        // does not need the viewport. Enter it now that startup has settled.
        if (m_pendingStartupSketch.has_value()) {
            const SketchPlane pl = *m_pendingStartupSketch;
            m_pendingStartupSketch.reset();
            createSketchOnPlane(pl);
        }
    }, Qt::QueuedConnection);
    // If 3D was toggled before the viewport had painted (e.g. run-script starts
    // straight in a sketch), seed the sketch 3D view the moment the view is up.
    connect(m_viewport, &ViewportWidget::viewInitialized, this, [this]() {
        // Startup --exec sketch: the viewport is now fully up, so switch to the
        // 2D sketch (deferred so it runs after this first paint completes).
        if (m_pendingStartupSketch.has_value()) {
            const SketchPlane pl = *m_pendingStartupSketch;
            m_pendingStartupSketch.reset();
            QTimer::singleShot(0, this, [this, pl]() { createSketchOnPlane(pl); });
        }
        if (m_pendingSketchSeed && m_inSketchMode) {
            seedSketch3DView();
            // The first paint may not have final widget geometry yet, so re-apply
            // orientation/center/scale/axes once the event loop settles. The view
            // is non-null here, so this is safe (unlike the removed pre-init one).
            QTimer::singleShot(0, this, [this]() {
                if (m_inSketchMode && m_sketch3DSeeded && m_viewport
                    && m_viewportStack->currentWidget() == m_viewport
                    && !m_viewport->view().IsNull())
                    syncViewportToSketchView();
            });
        }
        m_pendingSketchSeed = false;
    });
}

// The sketch's 2D/3D toggle drives the viewport stack.
void FullModeWindow::connectSketchModeToggle()
{
    // 2D/3D sketch toggle drives the viewport stack (agreed architecture,
    // 2026-09-05): 3D shows the GL viewport, 2D the QPainter canvas.
    //
    // The OCCT view is created lazily on the viewport's first paintEvent, and a
    // QStackedWidget only paints its visible page. So we must switch TO the
    // viewport here to let it initialize; gating on view()-is-ready deadlocked
    // (unshown -> never painted -> never initialized -> never shown, so 3D never
    // hid the 2D canvas). [Aaron item 9, 2026-09-09; deadlock fixed 2026-09-11]
    connect(m_sketchCanvas, &SketchCanvas::sketchModeChanged, this,
            [this](bool threeD) {
        if (!m_inSketchMode) return;
        m_viewportStack->setCurrentWidget(
            (threeD && m_viewport) ? static_cast<QWidget*>(m_viewport)
                                   : static_cast<QWidget*>(m_sketchCanvas));
        if (threeD && m_viewport) {
            // The 3D viewport is one reused OCCT view. The FIRST 3D view of a
            // sketch seeds the in-sketch orientation (matching the 2D view) and
            // saves the model camera; every switch to 3D re-matches the 2D view.
            // The OCCT view initializes lazily on its first paint, so if the
            // view is not up yet (run-script starts straight in a sketch) the
            // seed is deferred to the viewInitialized signal.
            if (!m_sketch3DSeeded) {
                if (!m_viewport->view().IsNull()) seedSketch3DView();
                else m_pendingSketchSeed = true;
            } else if (!m_viewport->view().IsNull()) {
                // Re-match the current 2D framing on every switch to 3D, so the
                // scale and center stay seamless with the 2D view (a 2D zoom is
                // carried over rather than restoring the last 3D framing).
                syncViewportToSketchView();
            }
        }
    });
}

// Sketch toolbar tool selection, with the tangent-arc / tangent-line
// precondition checks.
void FullModeWindow::connectSketchToolbarValidation()
{
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
}

// FullMode-specific sketch canvas signals, plus Edit > Delete / Select All.
void FullModeWindow::connectSketchCanvasSignals()
{
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
        // Escape with the sketch deselected finishes it, through the one
        // Save/Discard/Cancel path shared with the toolbar and menu.
        finishSketchInteractive();
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
}

// View menu actions that need the 3D viewport or the canvas: Reset View /
// Home, Look At, Slice, Rotate, Toolbar, Grid, Snap, Z-Up, Orbit, units.
void FullModeWindow::connectViewActions()
{
    // View > Reset View and the nav Home button are sketch-aware: inside a
    // sketch, "home" is the top-down plane view at the 2D scale; outside, the
    // default camera reset.
    auto goHome = [this]() {
        if (m_inSketchMode) syncViewportToSketchView();
        else if (m_viewport) m_viewport->resetCamera();
    };
    if (resetViewAction())
        connect(resetViewAction(), &QAction::triggered, this, goHome);
    connect(m_viewport, &ViewportWidget::homeRequested, this, goHome);

    // Connect View > Look At Sketch Plane: orient the 3D camera square onto
    // the active sketch's plane, the way Fusion's "Look At" does.
    if (lookAtAction()) {
        connect(lookAtAction(), &QAction::triggered,
                this, &FullModeWindow::lookAtSketchPlane);
    }

    // Connect View > Slice at Sketch Plane: a clip plane that sections the
    // model at the active sketch's plane so you can sketch against its
    // interior. Removable; toggling off restores the whole model.
    if (sliceAction()) {
        connect(sliceAction(), &QAction::toggled, this, [this](bool on) {
            if (!m_viewport) return;
            Handle(V3d_View) v = m_viewport->view();
            if (v.IsNull()) return;

            // Clear any previous slice first.
            if (!m_sliceClipPlane.IsNull()) {
                v->RemoveClipPlane(m_sliceClipPlane);
                m_sliceClipPlane.Nullify();
            }

            if (on && m_sketchCanvas) {
                gp_Dir normal(0, 0, 1);
                switch (m_sketchCanvas->sketchPlane()) {
                case SketchPlane::XY: normal = gp_Dir(0, 0, 1); break;
                case SketchPlane::XZ: normal = gp_Dir(0, 1, 0); break;
                case SketchPlane::YZ: normal = gp_Dir(1, 0, 0); break;
                default:              normal = gp_Dir(0, 0, 1); break;
                }
                const gp_Pln plane(gp_Pnt(0, 0, 0), normal);
                m_sliceClipPlane = new Graphic3d_ClipPlane(plane);
                m_sliceClipPlane->SetCapping(true);   // fill the cut face
                m_sliceClipPlane->SetOn(true);
                v->AddClipPlane(m_sliceClipPlane);
            }
            v->Redraw();
        });
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
}

// CLI panel viewport commands (zoom, pan, rotate); only work in full mode.
void FullModeWindow::connectCliViewportCommands()
{
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
}

// Construction planes and the Project tree: New Construction Plane, plane
// selection, sketch selection, and the plane transform editor.
void FullModeWindow::connectConstructionSignals()
{
    // Connect Construct > New Construction Plane
    if (newConstructionPlaneAction()) {
        connect(newConstructionPlaneAction(), &QAction::triggered,
                this, &FullModeWindow::onNewConstructionPlane);
    }

    // Connect construction plane selection from feature tree
    connect(this, &MainWindow::constructionPlaneSelected,
            this, &FullModeWindow::onConstructionPlaneSelected);

    // Construction plane transform editor: preview, apply, reset.
    if (PlaneTransformPanel* panel = planeTransformPanel()) {
        connect(panel, &PlaneTransformPanel::previewRequested,
                this, &FullModeWindow::previewConstructionPlane);
        connect(panel, &PlaneTransformPanel::applyRequested,
                this, &FullModeWindow::applyConstructionPlaneEdit);
        connect(panel, &PlaneTransformPanel::resetRequested,
                this, &FullModeWindow::resetConstructionPlanePreview);
    }
}

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
    connectViewportSignals();

    m_sketchCanvas = new SketchCanvas(m_viewportStack);
    installSketchCanvasResolvers();
    m_sketchCanvas->setUnitSuffix(unitSuffix());
    m_viewportStack->addWidget(m_sketchCanvas);
    connectSketchModeToggle();

    layout->addWidget(m_viewportStack, 1);  // stretch factor 1

    connectSketchToolbarValidation();
    connectSketchCanvasSignals();

    // Wire Edit > Cut/Copy/Paste to the sketch canvas.
    connectClipboardActions();

    // Timeline below the viewport
    m_timeline = new TimelineWidget(container);
    layout->addWidget(m_timeline);
    connectTimeline();

    // Connect shared sketch signals (toolbar, canvas, undo/redo, action bar, etc.)
    initSketchConnections();

    setCentralWidget(container);

    connectViewActions();
    connectCliViewportCommands();
    connectConstructionSignals();

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
    hidePlaneTransformPanel();
    MainWindow::onDocumentLoaded();   // timeline, tree, parameters, bodies, wireframes
    refreshConstructionPlaneVisuals();
}

void FullModeWindow::onDocumentRecipeChanged()
{
    MainWindow::onDocumentRecipeChanged();

    // Construction planes live in the project's own list; an undone plane
    // edit has to move the green square back too.
    refreshConstructionPlaneVisuals();
    if (PlaneTransformPanel* panel = planeTransformPanel(); panel && panel->isVisible()) {
        if (const ConstructionPlaneData* p = m_project.constructionPlaneById(panel->planeId()))
            onConstructionPlaneSelected(p->id);
        else
            hidePlaneTransformPanel();
    }
}

void FullModeWindow::onDocumentClosed()
{
    for (int id : m_constructionPlaneVis.keys()) eraseConstructionPlane(id);
    hidePlaneTransformPanel();
    MainWindow::onDocumentClosed();   // empties the timeline, the tree and the viewport
    if (m_viewport) m_viewport->resetCamera();
}

void FullModeWindow::showSketchPlaneHighlight(SketchPlane plane, double offset,
                                              PlaneRotationAxis axis, double angle)
{
    showSketchPlane(plane, offset, axis, angle);
}

void FullModeWindow::refreshModelViews()
{
    // Bodies from the project, then each stored sketch's wireframe by id. A
    // suppressed sketch, or one past the rollback marker, is not drawn.
    displayShapes();
    m_sketchWireframes.clear();
    if (!m_viewport || m_viewport->context().IsNull()) return;

    Handle(AIS_InteractiveContext) ctx = m_viewport->context();
    const int rollback = m_timeline ? m_timeline->rollbackPosition() : -1;
    for (const SketchData& s : m_project.sketches()) {
        if (const FeatureData* f = m_session.featureById(s.id); f && f->suppressed) continue;
        const int at = m_timeline ? m_timeline->indexOfFeatureId(s.id) : -1;
        if (rollback >= 0 && at > rollback) continue;
        Handle(AIS_Shape) wire = createSketchWireframe(s);
        if (wire.IsNull()) continue;
        m_sketchWireframes.insert(s.id, wire);
        ctx->Display(wire, false);
    }
    ctx->UpdateCurrentViewer();
}

SketchCanvas* FullModeWindow::activeSketchCanvas() const
{
    return m_inSketchMode ? m_sketchCanvas : nullptr;
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

void FullModeWindow::showOriginPlaneProperties(SketchPlane plane)
{
    MainWindow::showOriginPlaneProperties(plane);
    if (plane != SketchPlane::Custom)
        showSketchPlane(plane, 0.0, PlaneRotationAxis::X, 0.0);
}

void FullModeWindow::displayShapes()
{
    if (!m_viewport || m_viewport->context().IsNull()) return;

    auto ctx = m_viewport->context();

    // Remove only user shapes (AIS_Shape), preserving the trihedron
    // and any other non-shape interactive objects.
    NCollection_List<opencascade::handle<AIS_InteractiveObject>> displayed;
    ctx->DisplayedObjects(displayed);
    for (auto it = displayed.begin(); it != displayed.end(); ++it) {
        if ((*it)->IsKind(STANDARD_TYPE(AIS_Shape)))
            ctx->Remove(*it, false);
    }

    // Keep the shaded handles index-aligned with m_project.bodies(), so the
    // Bodies folder can hide and show a body by its row. Extrude and revolve
    // already append here; without doing it on load too the alignment only
    // held for bodies made during the current session.
    m_solidAisShapes.clear();

    // Display each body of the project with edge outlines
    for (const auto& body : m_project.bodies()) {
        const TopoDS_Shape& shape = body.shape;
        if (!shape.IsNull()) {
            // Shaded body
            Handle(AIS_Shape) aisShape = new AIS_Shape(shape);
            m_solidAisShapes.append(aisShape);
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

void FullModeWindow::beginStartupSketch(SketchPlane plane)
{
    // Aaron, 2026-09-11: full startup (viewport init and all) should finish
    // first, THEN switch to the 2D sketch. If the OCCT view is already up, begin
    // now; otherwise wait for viewInitialized (or viewInitFailed) to enter, so
    // the run-script path behaves like an interactive sketch on a warmed-up app.
    if (m_viewport && !m_viewport->view().IsNull()) {
        createSketchOnPlane(plane);
    } else {
        m_pendingStartupSketch = plane;
    }
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
        return;  // User canceled
    }

    ConstructionPlaneData planeData = dialog.planeData();

    // The project owns identity: it assigns the id and the design, and
    // hands the id back. Allocating it here as well meant two places knew
    // how plane ids are made.
    planeData.id = m_project.addConstructionPlane(planeData);

    // Add to feature tree
    rebuildObjectsTree();

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
        if (planeData->centerRelative) {
            originItem->setText(0, tr("Center (Relative)"));
            auto* refItem = new QTreeWidgetItem(props);
            refItem->setText(0, tr("Relative To"));
            const ConstructionPlaneData* rp = planeData->centerRefPlaneId >= 0
                ? m_project.constructionPlaneById(planeData->centerRefPlaneId) : nullptr;
            refItem->setText(1, rp ? QString::fromStdString(rp->name) : tr("Base plane"));
        }
    }

    // Visibility
    auto* visibleItem = new QTreeWidgetItem(props);
    visibleItem->setText(0, tr("Visible"));
    visibleItem->setText(1, planeData->visible ? tr("Yes") : tr("No"));
    visibleItem->setFlags(visibleItem->flags() | Qt::ItemIsEditable);

    // Expand all to show properties
    props->expandAll();
    props->resizeColumnToContents(0);

    // Highlight the plane where it really is, and open the transform editor
    showPlaneFrame(constructionPlaneFrame(*planeData, m_project));
    if (PlaneTransformPanel* panel = planeTransformPanel()) {
        panel->setPlane(*planeData, m_project.constructionPlanes());
        panel->setVisible(true);
    }
}

void FullModeWindow::exitSketchMode()
{
    MainWindow::exitSketchMode();

    // Switch back to the 3D viewport, unless it was dropped (a null
    // setCurrentWidget would blank the stack); then keep the current page.
    if (m_viewport)
        m_viewportStack->setCurrentWidget(m_viewport);

    // The sketch is finished: discard the in-sketch 3D orientation and restore
    // the pre-sketch model camera saved when 3D was first entered this sketch.
    if (m_sketch3DSeeded && m_viewport && !m_viewport->view().IsNull()
        && !m_preSketch3DCam.IsNull())
        m_viewport->setCameraState(m_preSketch3DCam);
    if (m_viewport) m_viewport->setAxisColorsNeutral(false);   // restore RGB axes
    m_preSketch3DCam.Nullify();
    m_inSketch3DCam.Nullify();
    m_sketch3DSeeded = false;
    m_pendingSketchSeed = false;
}

// Flat-on to the sketch plane: the projection vector is the plane normal and
// the up vector its second in-plane axis, matching OCCT's Top/Front/Right.
void FullModeWindow::orientViewToSketchPlane(const Handle(V3d_View)& v)
{
    double px = 0, py = 0, pz = 1;   // XY (top) by default
    double ux = 0, uy = 1, uz = 0;
    switch (m_sketchCanvas->sketchPlane()) {
    case SketchPlane::XY:  px = 0; py = 0; pz = 1; ux = 0; uy = 1; uz = 0; break;
    case SketchPlane::XZ:  px = 0; py = -1; pz = 0; ux = 0; uy = 0; uz = 1; break;
    case SketchPlane::YZ:  px = 1; py = 0; pz = 0; ux = 0; uy = 0; uz = 1; break;
    default:               px = 0; py = 0; pz = 1; ux = 0; uy = 1; uz = 0; break;
    }
    v->SetProj(px, py, pz);
    v->SetUp(ux, uy, uz);
}

void FullModeWindow::lookAtSketchPlane()
{
    if (!m_viewport || !m_sketchCanvas) return;
    Handle(V3d_View) v = m_viewport->view();
    if (v.IsNull()) return;

    // Projection vector points from the scene toward the eye, so it is the
    // plane normal; the up vector is the plane's second in-plane axis. These
    // match OCCT's standard Top/Front/Right views.
    orientViewToSketchPlane(v);
    m_viewport->fitAll();
    v->Redraw();
}

void FullModeWindow::syncViewportToSketchView()
{
    if (!m_viewport || !m_sketchCanvas) return;
    Handle(V3d_View) v = m_viewport->view();
    if (v.IsNull()) return;

    // Orientation: flat-on to the sketch plane (same vectors as Look At).
    orientViewToSketchPlane(v);

    // Center: the 2D view center (u,v on the plane) mapped to 3D world, so the
    // 3D view frames the same point the 2D canvas did (not a blind fit-all).
    const QPointF c = m_sketchCanvas->viewCenter();
    double wx = 0, wy = 0, wz = 0;
    switch (m_sketchCanvas->sketchPlane()) {
    case SketchPlane::XY:  wx = c.x(); wy = c.y(); wz = 0;      break;
    case SketchPlane::XZ:  wx = c.x(); wy = 0;     wz = c.y();  break;
    case SketchPlane::YZ:  wx = 0;     wy = c.x(); wz = c.y();  break;
    default:               wx = c.x(); wy = c.y(); wz = 0;      break;
    }
    v->SetAt(wx, wy, wz);

    // Scale: match the 2D zoom (pixels per mm). The 2D view spans
    // height_px / zoom mm; the stacked viewport is the same pixel size, so
    // SetSize maps that world span in. (OCCT SetSize semantics are version
    // dependent, so the match may want a small runtime tweak.)
    const double zoom = m_sketchCanvas->zoomFactor();
    if (zoom > geometry::kZeroEps && m_viewport->height() > 0)
        v->SetSize(static_cast<double>(m_viewport->height()) / zoom);

    // Flat sketch view: neutral (gray) axes rather than the model view's RGB.
    m_viewport->setAxisColorsNeutral(true);

    // The sketch plane's blue highlight overwhelms the 3D view and is transient
    // (never saved with the camera); drop it here.
    hideSketchPlane();

    v->Redraw();
}

void FullModeWindow::seedSketch3DView()
{
    if (m_sketch3DSeeded || !m_viewport || m_viewport->view().IsNull()) return;
    m_preSketch3DCam = m_viewport->cameraState();   // save before reorienting
    syncViewportToSketchView();                     // orient + center + scale to 2D
    m_inSketch3DCam  = m_viewport->cameraState();   // seed the in-sketch view
    m_sketch3DSeeded = true;
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

Handle(AIS_Shape) FullModeWindow::createSketchWireframe(const SketchData& sketch)
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
        double angleRad = degreesToRadians(sketch.rotationAngle);
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

    for (const sketch::Entity& entity : sketch.entities) {
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
                if (p1.Distance(p2) > geometry::kDegenerateLen) {
                    BRepBuilderAPI_MakeEdge me(p1, p2);
                    if (me.IsDone()) {
                        TopoDS_Shape edge = me.Edge();
                        builder.Add(compound, edge);
                    }
                }
            }
            break;

        case SketchEntityType::Rectangle:
        case SketchEntityType::Parallelogram:
            if (Point2D c[4]; sketch::quadCorners(entity, c)) {
                // From the corners: a rotated rectangle or a parallelogram is
                // not the axis-aligned box of its first two points.
                gp_Pnt p1 = to3D(QPointF(c[0].x, c[0].y));
                gp_Pnt p2 = to3D(QPointF(c[1].x, c[1].y));
                gp_Pnt p3 = to3D(QPointF(c[2].x, c[2].y));
                gp_Pnt p4 = to3D(QPointF(c[3].x, c[3].y));

                auto addEdge = [&](const gp_Pnt& a, const gp_Pnt& b) {
                    if (a.Distance(b) > geometry::kDegenerateLen) {
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
            if (!entity.points.empty() && entity.radius > geometry::kDegenerateLen) {
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
            if (!entity.points.empty() && entity.radius > geometry::kDegenerateLen &&
                std::abs(entity.sweepAngle) > geometry::kAngleEpsDeg) {
                gp_Pnt center = to3D(entity.points[0]);
                gp_Ax2 axis(center, planeNormal, planeXDir);
                gp_Circ circle(axis, entity.radius);

                // Convert angles to radians
                double startRad = degreesToRadians(entity.startAngle);
                double endRad = degreesToRadians(entity.startAngle + entity.sweepAngle);

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
    ctx->Display(m_sketchPlaneVis, true);
}

void FullModeWindow::hideSketchPlane()
{
    if (m_sketchPlaneVis.IsNull()) return;
    if (!m_viewport) return;

    Handle(AIS_InteractiveContext) ctx = m_viewport->context();
    if (ctx.IsNull()) return;

    ctx->Remove(m_sketchPlaneVis, true);
    m_sketchPlaneVis.Nullify();
    // Remove() redraws the OCCT viewer, but the Qt widget only repaints on
    // its next paint event; without this the removed plane stayed on screen
    // until something else moved the view.
    m_viewport->update();
}

// ---- Timeline Context Menu Handlers ---------------------------------

// ---- Project Loading ------------------------------------------------

bool FullModeWindow::setBodyVisible(int bodyId, bool visible)
{
    // The tree keys bodies by ID now, not by position, so the AIS handle
    // has to be found rather than indexed. Indexing directly would have
    // shown or hidden the wrong body: ids start at 1 and positions at 0,
    // so with a single body it addressed nothing at all.
    const auto& bodies = m_project.bodies();
    int index = -1;
    for (size_t i = 0; i < bodies.size(); ++i) {
        if (bodies[i].id == bodyId) {
            index = static_cast<int>(i);
            break;
        }
    }
    if (index < 0 || index >= m_solidAisShapes.size() || !m_viewport) {
        return false;
    }

    Handle(AIS_InteractiveContext) ctx = m_viewport->context();
    if (ctx.IsNull()) {
        return false;
    }

    if (visible) {
        ctx->Display(m_solidAisShapes[index], false);
    } else {
        ctx->Erase(m_solidAisShapes[index], false);
    }
    ctx->UpdateCurrentViewer();
    return true;
}

bool FullModeWindow::dropViewport()
{
    try {
        // Stop anything that would touch OCCT again. The handles are
        // released rather than used: the driver that just failed must not
        // be asked to render, clear, or even remove an object.
        m_solidAisShapes.clear();
        m_sketchWireframes.clear();

        // The 3D viewport is only ONE page of m_viewportStack, which lives
        // inside the central widget alongside the sketch canvas, timeline and
        // toolbars. Replacing the central widget (setCentralWidget) would DELETE
        // all of that, the sketch canvas included, so "sketching continues"
        // would be a lie. Instead drop just the 3D page and keep everything
        // else, whether we are mid-sketch or in the model view.
        if (m_viewport && m_viewportStack) {
            m_viewportStack->removeWidget(m_viewport);
            m_viewport->hide();
            m_viewport->setParent(nullptr);
            m_viewport->deleteLater();
            m_viewport = nullptr;

            if (m_inSketchMode && m_sketchCanvas) {
                // Mid-sketch: fall back to the 2D canvas and clear the 3D latch.
                m_viewportStack->setCurrentWidget(m_sketchCanvas);
                m_sketchCanvas->setSketchMode(false);
                if (m_sketchToolbar) m_sketchToolbar->set3DChecked(false);
            } else {
                // Model view: fill the viewport's slot with a notice PAGE (not
                // the central widget) so the sketch UI survives.
                auto* notice = new QLabel(
                    tr("The 3D viewport is unavailable.\n\n"
                       "Sketching and file operations still work."),
                    m_viewportStack);
                notice->setAlignment(Qt::AlignCenter);
                notice->setObjectName(QStringLiteral("ViewportUnavailableNotice"));
                m_viewportStack->addWidget(notice);
                m_viewportStack->setCurrentWidget(notice);
            }

            setGlModeText(QStringLiteral("\u26A0 ")
                              + tr("Reduced Mode: 3D viewport stopped"),
                          tr("The 3D viewport failed during this session and was "
                             "switched off. Sketching and file operations "
                             "continue; details are in the crash log."));
            return true;
        }

        // No stack to fall back into (should not happen in Full Mode): keep the
        // last-resort central-widget replacement so the window is not a gray
        // hole.
        if (m_viewport) {
            m_viewport->hide();
            m_viewport->setParent(nullptr);
            m_viewport->deleteLater();
            m_viewport = nullptr;
        }
        auto* notice = new QLabel(
            tr("The 3D viewport is unavailable.\n\n"
               "Sketching and file operations still work."), this);
        notice->setAlignment(Qt::AlignCenter);
        notice->setObjectName(QStringLiteral("ViewportUnavailableNotice"));
        setCentralWidget(notice);

        setGlModeText(QStringLiteral("\u26A0 ")
                          + tr("Reduced Mode: 3D viewport stopped"),
                      tr("The 3D viewport failed during this session and was "
                         "switched off. Details are in the crash log."));
        return true;
    } catch (...) {
        // Tearing down failed too. Say no; the caller then saves and exits
        // rather than pretending the window is usable.
        return false;
    }
}



void FullModeWindow::eraseConstructionPlane(int planeId)
{
    auto it = m_constructionPlaneVis.find(planeId);
    if (it == m_constructionPlaneVis.end()) return;
    if (m_viewport) {
        Handle(AIS_InteractiveContext) ctx = m_viewport->context();
        if (!ctx.IsNull() && !it.value().IsNull()) ctx->Remove(it.value(), false);
    }
    m_constructionPlaneVis.erase(it);
}

void FullModeWindow::refreshConstructionPlaneVisuals()
{
    for (int id : m_constructionPlaneVis.keys()) eraseConstructionPlane(id);
    for (const ConstructionPlaneData& p : m_project.constructionPlanes())
        if (p.visible) displayConstructionPlane(p.id);
    if (m_viewport && !m_viewport->context().IsNull()) m_viewport->context()->UpdateCurrentViewer();
}

void FullModeWindow::showPlaneFrame(const gp_Ax3& frame)
{
    if (!m_viewport) return;
    Handle(AIS_InteractiveContext) ctx = m_viewport->context();
    if (ctx.IsNull()) return;
    hideSketchPlane();
    m_sketchPlaneVis = new AisSketchPlane(200.0);
    m_sketchPlaneVis->setFrame(frame);
    m_sketchPlaneVis->SetTransparency(0.7);
    ctx->Display(m_sketchPlaneVis, true);
}

void FullModeWindow::previewConstructionPlane(const ConstructionPlaneData& plane)
{
    if (plane.centerRelative && plane.centerRefPlaneId >= 0
        && m_project.constructionPlaneDependsOn(plane.id, plane.centerRefPlaneId)) {
        if (PlaneTransformPanel* panel = planeTransformPanel())
            panel->setStatus(tr("That reference depends on this plane; a loop is not allowed."), true);
        return;
    }
    // The committed plane (green) stays; the highlight (blue) moves to
    // where the edit would put it.
    showPlaneFrame(constructionPlaneFrame(plane, m_project));
}

void FullModeWindow::resetConstructionPlanePreview()
{
    PlaneTransformPanel* panel = planeTransformPanel();
    if (!panel) return;
    if (!panel->isVisible()) { hideSketchPlane(); return; }
    if (const ConstructionPlaneData* stored = m_project.constructionPlaneById(panel->planeId()))
        showPlaneFrame(constructionPlaneFrame(*stored, m_project));
}

void FullModeWindow::applyConstructionPlaneEdit(const ConstructionPlaneData& plane)
{
    const auto& planes = m_project.constructionPlanes();
    int index = -1;
    for (size_t i = 0; i < planes.size(); ++i) if (planes[i].id == plane.id) { index = int(i); break; }
    PlaneTransformPanel* panel = planeTransformPanel();
    if (index < 0) {
        if (panel) panel->setStatus(tr("This plane no longer exists."), true);
        return;
    }
    if (plane.centerRelative && plane.centerRefPlaneId >= 0
        && m_project.constructionPlaneDependsOn(plane.id, plane.centerRefPlaneId)) {
        if (panel) panel->setStatus(tr("Not applied: that reference already depends on this plane, which would make a loop."), true);
        return;
    }
    const std::vector<ConstructionPlaneData> before = planes;
    m_project.setConstructionPlane(index, plane);
    const std::vector<ConstructionPlaneData> after = m_project.constructionPlanes();
    pushDocumentCommand(hobbycad::makePlaneListCommand(
        before, after, "Edit plane " + plane.name));

    eraseConstructionPlane(plane.id);
    if (plane.visible) displayConstructionPlane(plane.id);
    if (m_viewport && !m_viewport->context().IsNull()) m_viewport->context()->UpdateCurrentViewer();
    rebuildObjectsTree();
    onConstructionPlaneSelected(plane.id);      // page and panel reload from the stored plane
    if (panel) panel->setStatus(tr("Applied. Undo reverses it."), false);
    statusBar()->showMessage(tr("Construction plane '%1' updated").arg(QString::fromStdString(plane.name)), 3000);
}

void FullModeWindow::displayConstructionPlane(int planeId)
{
    const ConstructionPlaneData* planeData = m_project.constructionPlaneById(planeId);
    if (!planeData) return;
    if (!m_viewport) return;

    Handle(AIS_InteractiveContext) ctx = m_viewport->context();
    if (ctx.IsNull()) return;

    // One visual per plane id; a redraw replaces the old one instead of
    // stacking a second square on top of it.
    eraseConstructionPlane(planeId);

    // The full frame: base or reference plane, both rotations, roll,
    // center and offset. Offset-from-plane chains through the reference.
    Handle(AisSketchPlane) planeVis = new AisSketchPlane(200.0);
    planeVis->setFrame(constructionPlaneFrame(*planeData, m_project));

    // Set construction plane appearance (different from sketch plane)
    planeVis->setFillColor(Quantity_Color(0.3, 0.8, 0.3, Quantity_TOC_RGB));  // Green tint
    planeVis->setBorderColor(Quantity_Color(0.2, 0.6, 0.2, Quantity_TOC_RGB));
    planeVis->SetTransparency(0.8);

    m_constructionPlaneVis.insert(planeId, planeVis);
    ctx->Display(planeVis, true);
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
    const int sketchId = selectedTimelineSketchId(tr("Extrude"));
    if (sketchId < 0) return;
    const QString sketchName = QString::fromStdString(m_session.sketchById(sketchId)->name);

    ExtrudeDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const hobbycad::ExtrudeExtent extent =
        dialog.direction() == ExtrudeDirection::NormalReverse ? hobbycad::ExtrudeExtent::Reverse
        : dialog.direction() == ExtrudeDirection::TwoSided    ? hobbycad::ExtrudeExtent::Symmetric
                                                               : hobbycad::ExtrudeExtent::Normal;

    // The session builds the body on the sketch's own plane and records the
    // feature: the same work the CLI's "extrude" does, so Reduced mode's
    // terminal gets it too.
    const hobbycad::ModelResult result = m_session.extrudeSketch(
        sketchId, dialog.distance(), extent, bodyOperationFor(dialog.operation()),
        tr("Extrude %1").arg(sketchName).toStdString());
    if (!result.ok) {
        QMessageBox::critical(this, tr("Extrude Failed"),
            tr("Extrusion failed: %1").arg(QString::fromStdString(result.error)));
        return;
    }

    onDocumentRecipeChanged();
    updateUndoActions();
    if (m_viewport) m_viewport->fitAll();
    statusBar()->showMessage(tr("Extrusion completed"), 3000);
}

void FullModeWindow::performRevolve()
{
    const int sketchId = selectedTimelineSketchId(tr("Revolve"));
    if (sketchId < 0) return;
    const SketchData source = *m_session.sketchById(sketchId);
    const QString sketchName = QString::fromStdString(source.name);

    RevolveDialog dialog(this);

    // Construction lines of the sketch can serve as the axis.
    QVector<QPair<int, QString>> axisLines;
    for (const auto& e : source.entities) {
        if (e.type == SketchEntityType::Line && e.isConstruction) {
            axisLines.append({e.id, tr("Line %1").arg(e.id)});
        }
    }
    dialog.setAxisLines(axisLines);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    hobbycad::RevolveAxisKind axis = hobbycad::RevolveAxisKind::SketchXAxis;
    int lineId = -1;
    switch (dialog.axis()) {
    case RevolveAxis::XAxis: axis = hobbycad::RevolveAxisKind::SketchXAxis; break;
    case RevolveAxis::YAxis: axis = hobbycad::RevolveAxisKind::SketchYAxis; break;
    case RevolveAxis::SketchLine:
        lineId = dialog.axisLineId();
        if (lineId < 0) {
            QMessageBox::warning(this, tr("Revolve"),
                tr("Please select a construction line for the axis."));
            return;
        }
        axis = hobbycad::RevolveAxisKind::SketchLine;
        break;
    }

    const hobbycad::ModelResult result = m_session.revolveSketch(
        sketchId, dialog.angle(), axis, lineId, bodyOperationFor(dialog.operation()),
        tr("Revolve %1").arg(sketchName).toStdString());
    if (!result.ok) {
        QMessageBox::critical(this, tr("Revolve Failed"),
            tr("Revolution failed: %1").arg(QString::fromStdString(result.error)));
        return;
    }

    onDocumentRecipeChanged();
    updateUndoActions();
    if (m_viewport) m_viewport->fitAll();
    statusBar()->showMessage(tr("Revolution completed"), 3000);
}

}  // namespace hobbycad

