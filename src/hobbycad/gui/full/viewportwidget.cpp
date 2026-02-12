// =====================================================================
//  src/hobbycad/gui/full/viewportwidget.cpp — OCCT AIS viewer widget
// =====================================================================
//
//  Uses a plain QWidget with WA_PaintOnScreen.  OCCT creates and
//  fully owns the OpenGL context via Aspect_NeutralWindow attached
//  to the widget's native X11/Win32 window handle.  Qt does not
//  create any GL context for this widget — no RHI conflict.
//
//  This is the same approach used by FreeCAD, Mayo, and other
//  production OCCT-based Qt 6 applications.
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include "viewportwidget.h"
#include "scalebarwidget.h"
#include "navorbitring.h"
#include "navhomebutton.h"

#include <AIS_InteractiveContext.hxx>
#include <AIS_Line.hxx>
#include <AIS_SelectionScheme.hxx>
#include <AIS_Shape.hxx>
#include <AIS_Trihedron.hxx>
#include <AIS_ViewCube.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <Aspect_NeutralWindow.hxx>
#include <Aspect_Grid.hxx>
#include <Geom_Axis2Placement.hxx>
#include <Geom_CartesianPoint.hxx>
#include <Graphic3d_TransformPers.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <Prs3d_DatumAspect.hxx>
#include <SelectMgr_EntityOwner.hxx>
#include <V3d_AmbientLight.hxx>
#include <V3d_DirectionalLight.hxx>
#include <Quantity_Color.hxx>

#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QResizeEvent>

#include <gp_Trsf.hxx>

#include <cmath>

namespace hobbycad {

ViewportWidget::ViewportWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("Viewport"));
    setMouseTracking(true);

    // Tell Qt that we are painting the entire widget ourselves.
    // This prevents Qt from creating any GL context or RHI surface
    // for this widget.  OCCT owns the OpenGL context exclusively.
    setAttribute(Qt::WA_PaintOnScreen, true);
    setAttribute(Qt::WA_NoSystemBackground, true);

    // We need a native window handle for OCCT to attach to.
    // By default, QWidget might use a non-native window.
    setAttribute(Qt::WA_NativeWindow, true);

    // Ensure no double-buffering conflicts
    setAttribute(Qt::WA_OpaquePaintEvent, true);

    // Minimum size so the viewport isn't zero-sized at startup
    setMinimumSize(200, 200);

    // Accept keyboard focus for PgUp/PgDn rotation
    setFocusPolicy(Qt::StrongFocus);

    // Continuous rotation timer (PgUp/PgDn): 5° every 100ms
    m_spinTimer.setInterval(10);
    connect(&m_spinTimer, &QTimer::timeout, this, [this]() {
        if (m_spinDirection != 0.0) {
            rotateCameraAxis(m_spinDirection * (m_spinStepDeg * M_PI / 180.0));
        }
    });

    // Animated 90° snap timer (Left/Right): 1° every 10ms = 0.9s total
    m_snapTimer.setInterval(10);
    connect(&m_snapTimer, &QTimer::timeout, this, [this]() {
        if (m_snapRemaining > 0) {
            rotateCameraAxis(m_snapStepRad);
            --m_snapRemaining;
        }
        if (m_snapRemaining <= 0) {
            m_snapTimer.stop();
        }
    });
}

ViewportWidget::~ViewportWidget()
{
    if (!m_context.IsNull()) {
        m_context->RemoveAll(false);
    }
    m_view.Nullify();
    m_context.Nullify();
    m_viewer.Nullify();
}

Handle(AIS_InteractiveContext) ViewportWidget::context() const
{
    return m_context;
}

Handle(V3d_View) ViewportWidget::view() const
{
    return m_view;
}

void ViewportWidget::fitAll()
{
    if (!m_view.IsNull()) {
        m_view->FitAll(0.01, false);
        m_view->Invalidate();
        updateScaleBar();
        update();
    }
}

void ViewportWidget::setGridVisible(bool visible)
{
    m_gridVisible = visible;
    if (!m_viewer.IsNull()) {
        if (visible)
            m_viewer->ActivateGrid(Aspect_GT_Rectangular,
                                   Aspect_GDM_Lines);
        else
            m_viewer->DeactivateGrid();

        if (!m_view.IsNull()) {
            m_view->Invalidate();
            update();
        }
    }
}

bool ViewportWidget::isGridVisible() const
{
    return m_gridVisible;
}

// ---- Paint / resize -------------------------------------------------

void ViewportWidget::paintEvent(QPaintEvent* /*event*/)
{
    // Lazy initialization: create the viewer on first paint, when
    // the native window handle is guaranteed to be valid.
    if (!m_initialized) {
        initViewer();
        m_initialized = true;
    }

    if (!m_view.IsNull()) {
        m_view->Redraw();
    }
}

void ViewportWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);

    if (!m_view.IsNull()) {
        Handle(Aspect_NeutralWindow) aWindow =
            Handle(Aspect_NeutralWindow)::DownCast(m_view->Window());
        if (!aWindow.IsNull()) {
            aWindow->SetSize(event->size().width(),
                             event->size().height());
        }
        m_view->MustBeResized();
        m_view->Invalidate();
    }

    // Position and size are handled by TMF_2d persistence.
    if (!m_scaleBar.IsNull())
        updateScaleBar();
}

// ---- Viewer initialization ------------------------------------------

void ViewportWidget::initViewer()
{
    // Create the graphic driver with a display connection
    Handle(Aspect_DisplayConnection) displayConnection =
        new Aspect_DisplayConnection();
    Handle(OpenGl_GraphicDriver) graphicDriver =
        new OpenGl_GraphicDriver(displayConnection, false);

    // Create the viewer
    m_viewer = new V3d_Viewer(graphicDriver);

    // Lighting
    m_viewer->SetDefaultLights();
    m_viewer->SetLightOn();

    // Background gradient (dark blue)
    Quantity_Color topColor(0.15, 0.18, 0.22, Quantity_TOC_RGB);
    Quantity_Color botColor(0.35, 0.40, 0.48, Quantity_TOC_RGB);

    // Create the view
    m_view = m_viewer->CreateView();
    m_view->SetBgGradientColors(topColor, botColor,
                                Aspect_GradientFillMethod_Vertical);
    m_view->SetImmediateUpdate(false);

    // Wrap the widget's native window handle.
    Handle(Aspect_NeutralWindow) nativeWindow =
        new Aspect_NeutralWindow();

    Aspect_Drawable nativeHandle =
#ifdef _WIN32
        reinterpret_cast<Aspect_Drawable>(winId());
#else
        static_cast<Aspect_Drawable>(winId());
#endif
    nativeWindow->SetNativeHandle(nativeHandle);
    nativeWindow->SetSize(width(), height());

    m_view->SetWindow(nativeWindow);

    // Create the interactive context
    m_context = new AIS_InteractiveContext(m_viewer);
    m_context->SetDisplayMode(AIS_Shaded, true);

    // Set up the axis trihedron and grid
    setupAxisTrihedron();
    setupGrid();
    setupViewCube();

    // ---- Camera orientation ---------------------------------------------
    //
    // Isometric view with:
    //   X (red)   — lower-right
    //   Y (green) — upper-right (with -Y extension to lower-left)
    //   Z (blue)  — up
    //
    // Eye at (+1, -1, +1) looking toward origin.

    m_view->SetEye(1.0, -1.0, 1.0);
    m_view->SetUp(0.0, 0.0, 1.0);
    m_view->SetAt(0.0, 0.0, 0.0);
    m_view->FitAll(0.5, false);
    m_view->SetAt(0.0, 0.0, 30.0);

    // Initialize scale bar (AIS overlay, bottom-left)
    m_scaleBar = new ScaleBarWidget();
    m_scaleBar->setView(m_view);
    m_context->Display(m_scaleBar, false);
    updateScaleBar();
}

// ---- Axis trihedron (RGB) -------------------------------------------

void ViewportWidget::setupAxisTrihedron()
{
    if (m_context.IsNull()) return;

    // Create a trihedron at the origin.
    // Axis2Placement defines the coordinate system:
    //   origin, Z direction (main axis), X direction
    Handle(Geom_Axis2Placement) placement =
        new Geom_Axis2Placement(
            gp_Pnt(0.0, 0.0, 0.0),    // origin
            gp_Dir(0.0, 0.0, 1.0),    // Z axis (main/up)
            gp_Dir(1.0, 0.0, 0.0));   // X axis

    Handle(AIS_Trihedron) trihedron = new AIS_Trihedron(placement);

    // Configure axis colors: X=Red, Y=Green, Z=Blue
    trihedron->SetDatumPartColor(Prs3d_DatumParts_XAxis,
        Quantity_Color(Quantity_NOC_RED));
    trihedron->SetDatumPartColor(Prs3d_DatumParts_YAxis,
        Quantity_Color(Quantity_NOC_GREEN));
    trihedron->SetDatumPartColor(Prs3d_DatumParts_ZAxis,
        Quantity_Color(Quantity_NOC_BLUE1));

    // Set axis length proportional to default view (300 mm)
    trihedron->SetSize(300.0);

    // Display as wireframe (default mode) — not selectable
    m_context->Display(trihedron, false);
    m_context->Deactivate(trihedron);

    // Extend the Y axis into the -Y direction at the same length.
    // AIS_Trihedron only draws the positive direction, so we add a
    // separate green line from origin to (0, -300, 0).
    {
        Handle(Geom_CartesianPoint) p1 = new Geom_CartesianPoint(
            gp_Pnt(0.0, 0.0, 0.0));
        Handle(Geom_CartesianPoint) p2 = new Geom_CartesianPoint(
            gp_Pnt(0.0, -300.0, 0.0));
        Handle(AIS_Line) negY = new AIS_Line(p1, p2);
        negY->SetColor(Quantity_Color(Quantity_NOC_GREEN));
        negY->SetWidth(1.0);
        m_context->Display(negY, false);
        m_context->Deactivate(negY);
    }
}

// ---- Ground grid (XZ plane) -----------------------------------------

void ViewportWidget::setupGrid()
{
    if (m_viewer.IsNull()) return;

    // Set the grid to lie on the XZ plane (Y = 0) since Y points
    // toward the user.  The grid plane is defined by the viewer's
    // "private grid" coordinate system.
    //
    // Rectangular grid with 10 mm spacing, 100 mm major divisions.
    m_viewer->SetRectangularGridValues(
        0.0, 0.0,        // origin X, Z offset on the plane
        10.0, 10.0,      // step in X and Z (10 mm)
        0.0);            // rotation angle

    m_viewer->SetRectangularGridGraphicValues(
        100.0, 100.0,    // size X, Z (rendered extent — OCCT auto-tiles)
        0.0);            // offset from plane

    // Grid line colors
    Quantity_Color gridColor(0.35, 0.38, 0.42, Quantity_TOC_RGB);
    Quantity_Color tenthColor(0.50, 0.53, 0.58, Quantity_TOC_RGB);

    m_viewer->Grid()->SetColors(gridColor, tenthColor);

    // Activate the grid
    if (m_gridVisible)
        m_viewer->ActivateGrid(Aspect_GT_Rectangular,
                               Aspect_GDM_Lines);
}

// ---- Navigation cube (top-right corner) -----------------------------

void ViewportWidget::setupViewCube()
{
    if (m_context.IsNull()) return;

    m_viewCube = new AIS_ViewCube();

    // Appearance
    m_viewCube->SetSize(40.0);
    m_viewCube->SetBoxColor(Quantity_Color(0.30, 0.34, 0.40, Quantity_TOC_RGB));
    m_viewCube->SetTransparency(0.2);
    m_viewCube->SetFont("Arial");
    m_viewCube->SetFontHeight(12.0);
    m_viewCube->SetTextColor(Quantity_Color(Quantity_NOC_WHITE));

    // Behaviour
    m_viewCube->SetFixedAnimationLoop(false);
    m_viewCube->SetDrawAxes(false);  // we have our own trihedron

    // Position in the top-right corner of the viewport
    m_viewCube->SetTransformPersistence(
        new Graphic3d_TransformPers(
            Graphic3d_TMF_TriedronPers,
            Aspect_TOTP_RIGHT_UPPER,
            Graphic3d_Vec2i(85, 85)));

    m_context->Display(m_viewCube, false);

    // Set up the AIS navigation controls (arrows + home) around the cube
    setupNavControls();
}

// ---- Mouse interaction ----------------------------------------------
//
// Right-click drag   = rotate
// Middle-click drag  = pan
// Scroll wheel       = zoom
//
// TODO: Make this configurable with presets (Fusion 360, FreeCAD,
//       SolidWorks, Blender).

void ViewportWidget::mousePressEvent(QMouseEvent* event)
{
    m_lastMousePos = event->pos();

    if (event->button() == Qt::LeftButton) {
        // Forward to AIS context for ViewCube click detection.
        if (!m_context.IsNull() && !m_view.IsNull()) {
            m_context->MoveTo(event->pos().x(), event->pos().y(),
                              m_view, false);

            // Check if the ViewCube is under the cursor
            if (!m_viewCube.IsNull() && m_context->HasDetected()) {
                Handle(AIS_InteractiveObject) detected =
                    m_context->DetectedInteractive();
                if (!detected.IsNull() &&
                    detected == Handle(AIS_InteractiveObject)(m_viewCube)) {
                    m_draggingViewCube = true;
                    m_viewCubeDragStart = event->pos();

                    // Start rotation for ViewCube drag
                    m_view->StartRotation(event->pos().x(),
                                          event->pos().y());
                }
            }
        }
    } else if (event->button() == Qt::RightButton) {
        // RMB = rotate
        m_rotating = true;
        if (!m_view.IsNull()) {
            m_view->StartRotation(event->pos().x(),
                                  event->pos().y());
        }
    } else if (event->button() == Qt::MiddleButton) {
        // MMB = pan
        m_panning = true;
    }

    QWidget::mousePressEvent(event);
}

void ViewportWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_draggingViewCube) {
        // If the mouse barely moved, treat as a click → snap to face
        QPoint delta = event->pos() - m_viewCubeDragStart;
        if (delta.manhattanLength() < 5) {
            if (!m_context.IsNull() && !m_view.IsNull()) {
                m_context->MoveTo(event->pos().x(), event->pos().y(),
                                  m_view, false);
                if (m_context->HasDetected()) {
                    Handle(SelectMgr_EntityOwner) owner =
                        m_context->DetectedOwner();
                    Handle(AIS_ViewCubeOwner) cubeOwner =
                        Handle(AIS_ViewCubeOwner)::DownCast(owner);
                    if (!cubeOwner.IsNull()) {
                        m_viewCube->HandleClick(cubeOwner);
                        for (int i = 0; i < 100; ++i) {
                            if (!m_viewCube->UpdateAnimation(true)) break;
                            m_view->Redraw();
                        }
                        updateScaleBar();
                        update();
                    }
                }
            }
        }
        m_draggingViewCube = false;
    } else if (event->button() == Qt::LeftButton) {
        // Not on the ViewCube — check for navigation control clicks
        handleNavControlClick(event->pos().x(), event->pos().y());
    } else if (event->button() == Qt::RightButton) {
        m_rotating = false;
    } else if (event->button() == Qt::MiddleButton) {
        m_panning = false;
    }

    QWidget::mouseReleaseEvent(event);
}

void ViewportWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (m_view.IsNull()) return;

    QPoint pos = event->pos();

    // Always update AIS detection for ViewCube hover highlighting
    if (!m_context.IsNull()) {
        m_context->MoveTo(pos.x(), pos.y(), m_view, true);
    }

    if (m_draggingViewCube) {
        // Drag on ViewCube = free rotate
        m_view->Rotation(pos.x(), pos.y());
        updateScaleBar();
        update();
    } else if (m_rotating) {
        m_view->Rotation(pos.x(), pos.y());
        updateScaleBar();
        update();
    } else if (m_panning) {
        m_view->Pan(pos.x() - m_lastMousePos.x(),
                    m_lastMousePos.y() - pos.y());
        m_lastMousePos = pos;
        updateScaleBar();
        update();
    }

    QWidget::mouseMoveEvent(event);
}

void ViewportWidget::wheelEvent(QWheelEvent* event)
{
    if (m_view.IsNull()) return;

    double delta = event->angleDelta().y();
    if (delta > 0) {
        m_view->SetZoom(1.1);
    } else if (delta < 0) {
        m_view->SetZoom(0.9);
    }

    updateScaleBar();
    update();
    QWidget::wheelEvent(event);
}

// ---- Keyboard interaction -------------------------------------------
//
// PgUp = rotate CW around Z (5° per tick, 1 sec interval)
// PgDn = rotate CCW around Z

void ViewportWidget::keyPressEvent(QKeyEvent* event)
{
    if (event->isAutoRepeat()) {
        event->accept();
        return;  // ignore OS key repeat — we use our own timer
    }

    switch (event->key()) {
        case Qt::Key_Up:
            m_spinDirection = 1.0;  // CW
            rotateCameraAxis(m_spinDirection * (m_spinStepDeg * M_PI / 180.0));
            m_spinTimer.start();
            event->accept();
            return;

        case Qt::Key_Down:
            m_spinDirection = -1.0;  // CCW
            rotateCameraAxis(m_spinDirection * (m_spinStepDeg * M_PI / 180.0));
            m_spinTimer.start();
            event->accept();
            return;

        case Qt::Key_Left:
            startSnapRotation(m_rotationAxis, -1);
            event->accept();
            return;

        case Qt::Key_Right:
            startSnapRotation(m_rotationAxis, +1);
            event->accept();
            return;

        case Qt::Key_X:
            setRotationAxis(AxisX);
            event->accept();
            return;

        case Qt::Key_Y:
            setRotationAxis(AxisY);
            event->accept();
            return;

        case Qt::Key_Z:
            setRotationAxis(AxisZ);
            event->accept();
            return;

        default:
            break;
    }

    QWidget::keyPressEvent(event);
}

void ViewportWidget::keyReleaseEvent(QKeyEvent* event)
{
    if (event->isAutoRepeat()) {
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_Up ||
        event->key() == Qt::Key_Down) {
        m_spinDirection = 0.0;
        m_spinTimer.stop();
        event->accept();
    } else {
        QWidget::keyReleaseEvent(event);
    }
}

void ViewportWidget::rotateCameraZ(double angleRad)
{
    if (m_view.IsNull()) return;

    Handle(Graphic3d_Camera) cam = m_view->Camera();
    gp_Dir eye = cam->Direction();
    gp_Dir up  = cam->Up();

    gp_Ax1 zAxis(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1));
    gp_Trsf rot;
    rot.SetRotation(zAxis, angleRad);

    cam->SetDirection(eye.Transformed(rot));
    cam->SetUp(up.Transformed(rot));

    m_view->Redraw();
    updateScaleBar();
    update();
}

void ViewportWidget::rotateCameraAxis(double angleRad)
{
    if (m_view.IsNull()) return;

    Handle(Graphic3d_Camera) cam = m_view->Camera();
    gp_Dir eye = cam->Direction();
    gp_Dir up  = cam->Up();

    gp_Dir axisDir;
    switch (m_rotationAxis) {
        case AxisX: axisDir = gp_Dir(1, 0, 0); break;
        case AxisY: axisDir = gp_Dir(0, 1, 0); break;
        case AxisZ: axisDir = gp_Dir(0, 0, 1); break;
    }

    gp_Ax1 rotAxis(gp_Pnt(0, 0, 0), axisDir);
    gp_Trsf rot;
    rot.SetRotation(rotAxis, angleRad);

    cam->SetDirection(eye.Transformed(rot));
    cam->SetUp(up.Transformed(rot));

    m_view->Redraw();
    updateScaleBar();
    update();
}

void ViewportWidget::setRotationAxis(RotationAxis axis)
{
    if (m_rotationAxis != axis) {
        m_rotationAxis = axis;
        emit rotationAxisChanged(axis);
    }
}

ViewportWidget::RotationAxis ViewportWidget::rotationAxis() const
{
    return m_rotationAxis;
}

void ViewportWidget::setSpinParams(int stepDeg, int intervalMs)
{
    m_spinStepDeg = qBound(1, stepDeg, 45);
    m_spinTimer.setInterval(qBound(1, intervalMs, 1000));
}

void ViewportWidget::setSnapParams(int stepDeg, int intervalMs)
{
    m_snapStepDeg = qBound(1, stepDeg, 15);
    m_snapTimer.setInterval(qBound(1, intervalMs, 100));
}

// ---- AIS navigation controls (arrows + home around ViewCube) --------

void ViewportWidget::setupNavControls()
{
    if (m_context.IsNull()) return;

    // Orbit ring radius (adjustable via Preferences, 50–100, default 55).
    constexpr double kRadius = 55.0;

    // Three arc sections of 100° each with 20° gaps between them.
    // Angles are CCW from east (3-o'clock position).
    //   Z (blue)  :  30° to 130°  (top)
    //   Y (green) : 150° to 250°  (left)
    //   X (red)   : 270° to 370°  (bottom-right)

    Quantity_Color red  (1.0, 0.2, 0.2, Quantity_TOC_RGB);
    Quantity_Color green(0.2, 1.0, 0.2, Quantity_TOC_RGB);
    Quantity_Color blue (0.3, 0.5, 1.0, Quantity_TOC_RGB);

    m_ringZ = new NavOrbitRing( 30.0, 100.0,
                                NavCtrl_ZMinus, NavCtrl_ZPlus,
                                blue, kRadius);
    m_context->Display(m_ringZ, false);

    m_ringY = new NavOrbitRing(150.0, 100.0,
                                NavCtrl_YMinus, NavCtrl_YPlus,
                                green, kRadius);
    m_context->Display(m_ringY, false);

    m_ringX = new NavOrbitRing(270.0, 100.0,
                                NavCtrl_XMinus, NavCtrl_XPlus,
                                red, kRadius);
    m_context->Display(m_ringX, false);

    // Home button — lower-left of the cube area.
    m_navHome = new NavHomeButton();
    m_context->Display(m_navHome, false);
}

bool ViewportWidget::handleNavControlClick(int theX, int theY)
{
    if (m_context.IsNull() || m_view.IsNull())
        return false;

    m_context->MoveTo(theX, theY, m_view, false);

    if (!m_context->HasDetected())
        return false;

    Handle(SelectMgr_EntityOwner) owner = m_context->DetectedOwner();
    Handle(NavControlOwner) navOwner =
        Handle(NavControlOwner)::DownCast(owner);

    if (navOwner.IsNull())
        return false;

    switch (navOwner->ControlId()) {
        case NavCtrl_XPlus:   startSnapRotation(AxisX, +1); break;
        case NavCtrl_XMinus:  startSnapRotation(AxisX, -1); break;
        case NavCtrl_YPlus:   startSnapRotation(AxisY, +1); break;
        case NavCtrl_YMinus:  startSnapRotation(AxisY, -1); break;
        case NavCtrl_ZPlus:   startSnapRotation(AxisZ, +1); break;
        case NavCtrl_ZMinus:  startSnapRotation(AxisZ, -1); break;
        case NavCtrl_Home:    resetCamera();                 break;
        default:              return false;
    }

    return true;
}

void ViewportWidget::startSnapRotation(RotationAxis axis, int direction)
{
    // Save and set the axis so rotateCameraAxis() uses the right one
    RotationAxis prevAxis = m_rotationAxis;
    setRotationAxis(axis);

    // Configure and start the animated 90-degree snap
    m_snapStepRad   = direction * (m_snapStepDeg * M_PI / 180.0);
    m_snapRemaining = 90 / m_snapStepDeg;
    m_snapTimer.start();

    // Note: we intentionally leave the rotation axis set to the new
    // value so subsequent PgUp/PgDn and arrow key rotations continue
    // on the same axis the user just clicked.
    (void)prevAxis;
}

void ViewportWidget::rotateCamera90(int axisDir)
{
    if (m_view.IsNull()) return;

    // axisDir: ±1 = X-tilt, ±2 = Z-spin (sign = direction)
    int axis = (axisDir > 0) ? axisDir : -axisDir;
    double angle = (axisDir > 0) ? M_PI / 2.0 : -M_PI / 2.0;

    // Get current camera direction and up
    Handle(Graphic3d_Camera) cam = m_view->Camera();
    gp_Dir eye = cam->Direction();
    gp_Dir up  = cam->Up();

    // Build rotation around the world axis
    gp_Ax1 rotAxis;
    switch (axis) {
        case 1: rotAxis = gp_Ax1(gp_Pnt(0,0,0), gp_Dir(1,0,0)); break;  // X
        case 2: rotAxis = gp_Ax1(gp_Pnt(0,0,0), gp_Dir(0,0,1)); break;  // Z
        default: return;
    }

    gp_Trsf rot;
    rot.SetRotation(rotAxis, angle);

    gp_Dir newEye = eye.Transformed(rot);
    gp_Dir newUp  = up.Transformed(rot);

    cam->SetDirection(newEye);
    cam->SetUp(newUp);

    m_view->Redraw();
    updateScaleBar();
    update();
}

void ViewportWidget::resetCamera()
{
    if (m_view.IsNull()) return;

    // Restore the default startup camera position
    m_view->SetEye(1.0, -1.0, 1.0);
    m_view->SetUp(0.0, 0.0, 1.0);
    m_view->SetAt(0.0, 0.0, 0.0);
    m_view->FitAll(0.5, false);
    m_view->SetAt(0.0, 0.0, 30.0);

    m_view->Redraw();
    updateScaleBar();
    update();
}

// ---- Scale bar helper -----------------------------------------------

void ViewportWidget::updateScaleBar()
{
    if (!m_scaleBar.IsNull() && !m_context.IsNull()) {
        m_scaleBar->updateScale();
        m_context->Redisplay(m_scaleBar, false);
    }
}

}  // namespace hobbycad

