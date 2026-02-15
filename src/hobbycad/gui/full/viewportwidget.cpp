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
#include "aisgrid.h"
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
#include <Bnd_Box.hxx>
#include <Geom_Axis2Placement.hxx>
#include <Geom_CartesianPoint.hxx>
#include <Graphic3d_TransformPers.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <Prs3d_DatumAspect.hxx>
#include <SelectMgr_EntityOwner.hxx>
#include <V3d.hxx>
#include <V3d_AmbientLight.hxx>
#include <V3d_DirectionalLight.hxx>
#include <Quantity_Color.hxx>

#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QResizeEvent>

#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <cmath>

namespace hobbycad {

ViewportWidget::ViewportWidget(QWidget* parent)
    : QWidget(parent)
    , m_orbitCenter(0.0, 0.0, 0.0)
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

    // ViewCube animation timer: smooth orbit transition (~16ms = 60fps)
    m_viewCubeAnimTimer.setInterval(16);
    connect(&m_viewCubeAnimTimer, &QTimer::timeout, this, [this]() {
        if (m_view.IsNull()) {
            m_viewCubeAnimTimer.stop();
            return;
        }

        m_animStep++;
        double t = static_cast<double>(m_animStep) / m_animTotalSteps;

        // Smooth ease-in-out interpolation
        t = t < 0.5 ? 2.0 * t * t : 1.0 - std::pow(-2.0 * t + 2.0, 2.0) / 2.0;

        if (m_animStep >= m_animTotalSteps) {
            // Final position - set exact target
            m_view->SetEye(m_animEndEye.X(), m_animEndEye.Y(), m_animEndEye.Z());
            m_view->SetAt(m_animOrbitCenter.X(), m_animOrbitCenter.Y(), m_animOrbitCenter.Z());
            m_view->SetUp(m_animEndUp.X(), m_animEndUp.Y(), m_animEndUp.Z());
            m_viewCubeAnimTimer.stop();
        } else {
            // Interpolate eye position (spherical interpolation around orbit center)
            gp_Vec startVec(m_animOrbitCenter, m_animStartEye);
            gp_Vec endVec(m_animOrbitCenter, m_animEndEye);
            double radius = startVec.Magnitude();

            // Normalize and slerp the direction
            if (startVec.Magnitude() > 1e-6 && endVec.Magnitude() > 1e-6) {
                gp_Dir startDir(startVec);
                gp_Dir endDir(endVec);

                // Linear interpolation of direction (good enough for small angles)
                // then renormalize and apply radius
                double x = startDir.X() * (1.0 - t) + endDir.X() * t;
                double y = startDir.Y() * (1.0 - t) + endDir.Y() * t;
                double z = startDir.Z() * (1.0 - t) + endDir.Z() * t;
                double len = std::sqrt(x*x + y*y + z*z);
                if (len > 1e-6) {
                    x /= len; y /= len; z /= len;
                }

                gp_Pnt interpEye(
                    m_animOrbitCenter.X() + x * radius,
                    m_animOrbitCenter.Y() + y * radius,
                    m_animOrbitCenter.Z() + z * radius);

                // Interpolate up vector
                double upX = m_animStartUp.X() * (1.0 - t) + m_animEndUp.X() * t;
                double upY = m_animStartUp.Y() * (1.0 - t) + m_animEndUp.Y() * t;
                double upZ = m_animStartUp.Z() * (1.0 - t) + m_animEndUp.Z() * t;
                double upLen = std::sqrt(upX*upX + upY*upY + upZ*upZ);
                if (upLen > 1e-6) {
                    upX /= upLen; upY /= upLen; upZ /= upLen;
                }

                m_view->SetEye(interpEye.X(), interpEye.Y(), interpEye.Z());
                m_view->SetAt(m_animOrbitCenter.X(), m_animOrbitCenter.Y(), m_animOrbitCenter.Z());
                m_view->SetUp(upX, upY, upZ);
            }
        }

        m_view->Redraw();
        updateScaleBar();
        updateOrbitRingFlips();
        update();
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
    if (!m_context.IsNull() && !m_grid.IsNull()) {
        if (visible) {
            m_context->Display(m_grid, false);
        } else {
            m_context->Erase(m_grid, false);
        }

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

void ViewportWidget::setZUpOrientation(bool zUp)
{
    if (m_zUpOrientation == zUp) return;

    m_zUpOrientation = zUp;

    // Update the ViewCube orientation
    if (!m_viewCube.IsNull()) {
        m_viewCube->SetYup(zUp ? Standard_False : Standard_True);
        if (!m_context.IsNull()) {
            m_context->Redisplay(m_viewCube, Standard_True);
        }
    }

    // Reset camera to match new orientation
    resetCamera();
}

bool ViewportWidget::isZUpOrientation() const
{
    return m_zUpOrientation;
}

void ViewportWidget::setOrbitSelectedObject(bool enabled)
{
    if (m_orbitSelectedObject == enabled) return;
    if (m_view.IsNull() || m_context.IsNull()) {
        m_orbitSelectedObject = enabled;
        return;
    }

    m_orbitSelectedObject = enabled;

    // Get current camera state for animation
    Standard_Real eyeX, eyeY, eyeZ;
    m_view->Eye(eyeX, eyeY, eyeZ);
    m_animStartEye = gp_Pnt(eyeX, eyeY, eyeZ);

    Standard_Real upX, upY, upZ;
    m_view->Up(upX, upY, upZ);
    m_animStartUp = gp_Dir(upX, upY, upZ);
    m_animEndUp = m_animStartUp;  // up vector doesn't change

    if (enabled) {
        // Save current orbit center before switching
        m_savedOrbitCenter = m_orbitCenter;

        // Compute selected object's center
        if (m_context->NbSelected() > 0) {
            Bnd_Box selBox;
            for (m_context->InitSelected(); m_context->MoreSelected(); m_context->NextSelected()) {
                Handle(AIS_InteractiveObject) obj = m_context->SelectedInteractive();
                if (!obj.IsNull()) {
                    Bnd_Box objBox;
                    obj->BoundingBox(objBox);
                    if (!objBox.IsVoid()) {
                        selBox.Add(objBox);
                    }
                }
            }
            if (!selBox.IsVoid()) {
                Standard_Real xMin, yMin, zMin, xMax, yMax, zMax;
                selBox.Get(xMin, yMin, zMin, xMax, yMax, zMax);
                m_animOrbitCenter = gp_Pnt(
                    (xMin + xMax) / 2.0,
                    (yMin + yMax) / 2.0,
                    (zMin + zMax) / 2.0);
            } else {
                // No valid bounding box, stay where we are
                return;
            }
        } else {
            // Nothing selected, no change
            return;
        }
    } else {
        // Restore saved orbit center
        m_animOrbitCenter = m_savedOrbitCenter;
    }

    // Compute new eye position: maintain same distance and direction from new center
    gp_Vec viewVec(m_orbitCenter, m_animStartEye);
    Standard_Real distance = viewVec.Magnitude();
    if (distance < 1e-6) distance = 300.0;

    gp_Dir viewDir(viewVec);
    m_animEndEye = gp_Pnt(
        m_animOrbitCenter.X() + viewDir.X() * distance,
        m_animOrbitCenter.Y() + viewDir.Y() * distance,
        m_animOrbitCenter.Z() + viewDir.Z() * distance);

    // Update orbit center to target
    m_orbitCenter = m_animOrbitCenter;

    // Start animation
    m_animStep = 0;
    m_viewCubeAnimTimer.start();
}

bool ViewportWidget::isOrbitSelectedObject() const
{
    return m_orbitSelectedObject;
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

    // Frame the grid, not the axis lines.  Build a bounding box
    // matching the grid extent and let FitAll compute the proper
    // camera center and size from that.
    Bnd_Box gridBox;
    gridBox.Update(-100.0, -1.0, -100.0);  // grid is 100 mm in each
    gridBox.Update( 100.0,  1.0,  100.0);  // direction on XZ plane
    m_view->FitAll(gridBox, 0.01);

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

    // Mark as infinite so it's excluded from FitAll bounding box calculations
    trihedron->SetInfiniteState(Standard_True);

    // Display as wireframe (default mode) — not selectable
    m_context->Display(trihedron, false);
    m_context->Deactivate(trihedron);

    // Extend axes into the negative direction at the same length.
    // AIS_Trihedron only draws the positive direction, so we add
    // separate lines from origin into -X, -Y, and -Z.
    // All are marked infinite to exclude from FitAll bounding box.
    {
        Handle(Geom_CartesianPoint) p1 = new Geom_CartesianPoint(
            gp_Pnt(0.0, 0.0, 0.0));
        Handle(Geom_CartesianPoint) p2 = new Geom_CartesianPoint(
            gp_Pnt(0.0, -300.0, 0.0));
        Handle(AIS_Line) negY = new AIS_Line(p1, p2);
        negY->SetColor(Quantity_Color(Quantity_NOC_GREEN));
        negY->SetWidth(1.0);
        negY->SetInfiniteState(Standard_True);
        m_context->Display(negY, false);
        m_context->Deactivate(negY);
    }

    // Extend the X axis into the -X direction.
    {
        Handle(Geom_CartesianPoint) p1 = new Geom_CartesianPoint(
            gp_Pnt(0.0, 0.0, 0.0));
        Handle(Geom_CartesianPoint) p2 = new Geom_CartesianPoint(
            gp_Pnt(-300.0, 0.0, 0.0));
        Handle(AIS_Line) negX = new AIS_Line(p1, p2);
        negX->SetColor(Quantity_Color(Quantity_NOC_RED));
        negX->SetWidth(1.0);
        negX->SetInfiniteState(Standard_True);
        m_context->Display(negX, false);
        m_context->Deactivate(negX);
    }

    // Extend the Z axis into the -Z direction.
    {
        Handle(Geom_CartesianPoint) p1 = new Geom_CartesianPoint(
            gp_Pnt(0.0, 0.0, 0.0));
        Handle(Geom_CartesianPoint) p2 = new Geom_CartesianPoint(
            gp_Pnt(0.0, 0.0, -300.0));
        Handle(AIS_Line) negZ = new AIS_Line(p1, p2);
        negZ->SetColor(Quantity_Color(Quantity_NOC_BLUE1));
        negZ->SetWidth(1.0);
        negZ->SetInfiniteState(Standard_True);
        m_context->Display(negZ, false);
        m_context->Deactivate(negZ);
    }
}

// ---- Ground grid (XY plane, Z=0) ------------------------------------

void ViewportWidget::setupGrid()
{
    if (m_context.IsNull()) return;

    // Create a custom AIS grid on the XY plane (Z=0).
    // This replaces the V3d_Viewer built-in grid, allowing us to:
    //   - Mark it infinite (excluded from FitAll bounding box)
    //   - Have it rotate naturally with the view
    //
    // Grid: 100mm extent, 10mm minor spacing, 100mm major divisions
    m_grid = new AisGrid(100.0, 10.0, 100.0);

    // Grid line colors (matching previous V3d grid)
    m_grid->SetMinorColor(Quantity_Color(0.35, 0.38, 0.42, Quantity_TOC_RGB));
    m_grid->SetMajorColor(Quantity_Color(0.50, 0.53, 0.58, Quantity_TOC_RGB));

    // Display the grid (not selectable)
    if (m_gridVisible) {
        m_context->Display(m_grid, false);
    }
    m_context->Deactivate(m_grid);
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
    m_viewCube->SetYup(Standard_False);  // Z-up coordinate system
    m_viewCube->SetFitSelected(Standard_False);  // don't refit on click
    m_viewCube->SetResetCamera(Standard_False);  // preserve camera target point

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
                        // Get the orientation from the clicked ViewCube face
                        V3d_TypeOfOrientation orient = cubeOwner->MainOrientation();

                        // Get the view direction for this orientation
                        gp_Dir viewDir = V3d::GetProjAxis(orient);

                        // Determine orbit center
                        if (m_orbitSelectedObject && m_context->NbSelected() > 0) {
                            // Compute bounding box center of selected objects
                            Bnd_Box selBox;
                            for (m_context->InitSelected(); m_context->MoreSelected(); m_context->NextSelected()) {
                                Handle(AIS_InteractiveObject) obj = m_context->SelectedInteractive();
                                if (!obj.IsNull()) {
                                    Bnd_Box objBox;
                                    obj->BoundingBox(objBox);
                                    if (!objBox.IsVoid()) {
                                        selBox.Add(objBox);
                                    }
                                }
                            }
                            if (!selBox.IsVoid()) {
                                Standard_Real xMin, yMin, zMin, xMax, yMax, zMax;
                                selBox.Get(xMin, yMin, zMin, xMax, yMax, zMax);
                                m_orbitCenter = gp_Pnt(
                                    (xMin + xMax) / 2.0,
                                    (yMin + yMax) / 2.0,
                                    (zMin + zMax) / 2.0);
                            }
                        }
                        m_animOrbitCenter = m_orbitCenter;

                        // Get current eye position and up vector
                        Standard_Real eyeX, eyeY, eyeZ;
                        m_view->Eye(eyeX, eyeY, eyeZ);
                        m_animStartEye = gp_Pnt(eyeX, eyeY, eyeZ);

                        Standard_Real upX, upY, upZ;
                        m_view->Up(upX, upY, upZ);
                        m_animStartUp = gp_Dir(upX, upY, upZ);

                        // Compute distance to orbit center (preserve zoom)
                        Standard_Real distance = m_animStartEye.Distance(m_animOrbitCenter);

                        // Compute target eye position: orbit center + distance * viewDir
                        // V3d::GetProjAxis returns the direction where the eye is located
                        // (e.g., V3d_Zpos = eye at +Z looking toward origin = Top view)
                        m_animEndEye = gp_Pnt(
                            m_animOrbitCenter.X() + viewDir.X() * distance,
                            m_animOrbitCenter.Y() + viewDir.Y() * distance,
                            m_animOrbitCenter.Z() + viewDir.Z() * distance);

                        // Determine target up vector based on orientation convention
                        if (m_zUpOrientation) {
                            // Z-up: Z is up for most views
                            m_animEndUp = gp_Dir(0.0, 0.0, 1.0);
                            if (orient == V3d_Zpos) {
                                // Top view: Y is up
                                m_animEndUp = gp_Dir(0.0, 1.0, 0.0);
                            } else if (orient == V3d_Zneg) {
                                // Bottom view: -Y is up (so text reads correctly)
                                m_animEndUp = gp_Dir(0.0, -1.0, 0.0);
                            }
                        } else {
                            // Y-up: Y is up for most views
                            m_animEndUp = gp_Dir(0.0, 1.0, 0.0);
                            if (orient == V3d_Ypos) {
                                // Top view: -Z is up
                                m_animEndUp = gp_Dir(0.0, 0.0, -1.0);
                            } else if (orient == V3d_Yneg) {
                                // Bottom view: +Z is up (so text reads correctly)
                                m_animEndUp = gp_Dir(0.0, 0.0, 1.0);
                            }
                        }

                        // Start animation
                        m_animStep = 0;
                        m_viewCubeAnimTimer.start();
                    }
                }
            }
        }
        m_draggingViewCube = false;
        updateOrbitRingFlips();
    } else if (event->button() == Qt::LeftButton) {
        // Not on the ViewCube — check for navigation control clicks
        handleNavControlClick(event->pos().x(), event->pos().y());
    } else if (event->button() == Qt::RightButton) {
        m_rotating = false;
        updateOrbitRingFlips();
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

        // Update orbit center to match the new camera target (At point)
        Standard_Real atX, atY, atZ;
        m_view->At(atX, atY, atZ);
        m_orbitCenter = gp_Pnt(atX, atY, atZ);

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
    updateOrbitRingFlips();
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

void ViewportWidget::setUnitSystem(int units)
{
    if (m_scaleBar.IsNull()) return;
    m_scaleBar->setUnitSystem(static_cast<UnitSystem>(units));
    m_scaleBar->updateScale();
    if (!m_context.IsNull())
        m_context->Redisplay(m_scaleBar, true);
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

    // Get current camera state for animation start
    Standard_Real eyeX, eyeY, eyeZ;
    m_view->Eye(eyeX, eyeY, eyeZ);
    m_animStartEye = gp_Pnt(eyeX, eyeY, eyeZ);

    Standard_Real upX, upY, upZ;
    m_view->Up(upX, upY, upZ);
    m_animStartUp = gp_Dir(upX, upY, upZ);

    // Target: orbit around origin (reset the orbit center)
    m_orbitCenter = gp_Pnt(0.0, 0.0, 0.0);
    m_animOrbitCenter = m_orbitCenter;

    // Compute target eye position for isometric view
    // Use current distance to orbit center (preserve zoom)
    Standard_Real distance = m_animStartEye.Distance(m_animOrbitCenter);
    if (distance < 1.0) distance = 300.0;  // fallback if too close

    // Isometric direction: normalized (1, -1, 1) for Z-up, (1, 1, 1) for Y-up
    if (m_zUpOrientation) {
        gp_Dir isoDir(1.0, -1.0, 1.0);
        m_animEndEye = gp_Pnt(
            m_animOrbitCenter.X() + isoDir.X() * distance,
            m_animOrbitCenter.Y() + isoDir.Y() * distance,
            m_animOrbitCenter.Z() + isoDir.Z() * distance);
        m_animEndUp = gp_Dir(0.0, 0.0, 1.0);
    } else {
        gp_Dir isoDir(1.0, 1.0, 1.0);
        m_animEndEye = gp_Pnt(
            m_animOrbitCenter.X() + isoDir.X() * distance,
            m_animOrbitCenter.Y() + isoDir.Y() * distance,
            m_animOrbitCenter.Z() + isoDir.Z() * distance);
        m_animEndUp = gp_Dir(0.0, 1.0, 0.0);
    }

    // Start animation
    m_animStep = 0;
    m_viewCubeAnimTimer.start();
}

// ---- Scale bar helper -----------------------------------------------

void ViewportWidget::updateScaleBar()
{
    if (!m_scaleBar.IsNull() && !m_context.IsNull()) {
        m_scaleBar->updateScale();
        m_context->Redisplay(m_scaleBar, false);
    }
}

// ---- Orbit ring flip state ------------------------------------------

void ViewportWidget::updateOrbitRingFlips()
{
    if (m_view.IsNull()) return;

    // Get camera direction (where the camera is looking)
    Handle(Graphic3d_Camera) cam = m_view->Camera();
    if (cam.IsNull()) return;

    gp_Dir viewDir = cam->Direction();

    // For each axis, determine when arrows need to flip based on camera direction.
    // Camera Direction() points FROM eye TO target (where camera is looking).
    //
    // Flip when the camera is looking in the positive axis direction:
    // - Z-axis ring: flip when viewDir.Z > 0 (looking toward +Z, bottom view)
    // - Y-axis ring: flip when viewDir.Y > 0 (looking toward +Y, back view)
    // - X-axis ring: flip when viewDir.X > 0 (looking toward +X, left view)

    constexpr double kThreshold = 0.1;  // Small threshold to avoid flicker at edge

    if (!m_ringZ.IsNull()) {
        bool flipZ = viewDir.Z() > kThreshold;
        if (m_ringZ->isFlipped() != flipZ) {
            m_ringZ->setFlipped(flipZ);
            m_context->Redisplay(m_ringZ, false);
        }
    }

    if (!m_ringY.IsNull()) {
        bool flipY = viewDir.Y() > kThreshold;
        if (m_ringY->isFlipped() != flipY) {
            m_ringY->setFlipped(flipY);
            m_context->Redisplay(m_ringY, false);
        }
    }

    if (!m_ringX.IsNull()) {
        bool flipX = viewDir.X() > kThreshold;
        if (m_ringX->isFlipped() != flipX) {
            m_ringX->setFlipped(flipX);
            m_context->Redisplay(m_ringX, false);
        }
    }
}

// ---- CLI viewport control commands ----------------------------------

void ViewportWidget::setZoomPercent(double percent)
{
    if (m_view.IsNull()) return;

    // Get current zoom level and compute factor to reach target
    double current = zoomPercent();
    if (current <= 0.0) return;

    double factor = percent / current;
    if (factor <= 0.0) return;

    m_view->SetZoom(factor);
    m_view->Redraw();
    updateScaleBar();
    update();
}

double ViewportWidget::zoomPercent() const
{
    if (m_view.IsNull()) return 100.0;

    // Zoom percentage is inversely related to the view scale
    // A larger scale value means we're "zoomed out" (smaller objects on screen)
    // We use FitAll to establish a baseline, then compare to current scale
    Handle(Graphic3d_Camera) cam = m_view->Camera();
    if (cam.IsNull()) return 100.0;

    // Use the camera scale (inverse of zoom)
    // Scale of 1.0 corresponds to 100% zoom
    double scale = cam->Scale();
    if (scale <= 0.0) return 100.0;

    // Approximate: scale of 100 world units fitting in view = 100%
    // This is a heuristic - actual percentage depends on content
    // Return inverse of scale as percentage (smaller scale = more zoomed in)
    return 100.0 / scale * 100.0;
}

void ViewportWidget::panTo(double x, double y, double z)
{
    if (m_view.IsNull()) return;

    // Get current camera position
    Standard_Real eyeX, eyeY, eyeZ;
    m_view->Eye(eyeX, eyeY, eyeZ);
    gp_Pnt currentEye(eyeX, eyeY, eyeZ);

    Standard_Real atX, atY, atZ;
    m_view->At(atX, atY, atZ);
    gp_Pnt currentAt(atX, atY, atZ);

    // Compute offset from current target to new target
    gp_Vec offset(currentAt, gp_Pnt(x, y, z));

    // Move both eye and target by this offset
    gp_Pnt newEye(
        currentEye.X() + offset.X(),
        currentEye.Y() + offset.Y(),
        currentEye.Z() + offset.Z());
    gp_Pnt newAt(x, y, z);

    // Set the new camera position
    m_view->SetEye(newEye.X(), newEye.Y(), newEye.Z());
    m_view->SetAt(newAt.X(), newAt.Y(), newAt.Z());

    // Update orbit center
    m_orbitCenter = newAt;

    m_view->Redraw();
    updateScaleBar();
    update();
}

void ViewportWidget::cameraTarget(double& x, double& y, double& z) const
{
    if (m_view.IsNull()) {
        x = y = z = 0.0;
        return;
    }

    Standard_Real atX, atY, atZ;
    m_view->At(atX, atY, atZ);
    x = atX;
    y = atY;
    z = atZ;
}

void ViewportWidget::rotateOnAxis(char axis, double degrees)
{
    if (m_view.IsNull()) return;

    // Convert degrees to radians
    double radians = degrees * M_PI / 180.0;

    Handle(Graphic3d_Camera) cam = m_view->Camera();
    gp_Dir eye = cam->Direction();
    gp_Dir up  = cam->Up();

    // Determine rotation axis
    gp_Dir axisDir;
    char axisUpper = static_cast<char>(std::toupper(static_cast<unsigned char>(axis)));
    switch (axisUpper) {
        case 'X': axisDir = gp_Dir(1, 0, 0); break;
        case 'Y': axisDir = gp_Dir(0, 1, 0); break;
        case 'Z':
        default:  axisDir = gp_Dir(0, 0, 1); break;
    }

    // Apply rotation
    gp_Trsf rot;
    rot.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), axisDir), radians);

    cam->SetDirection(eye.Transformed(rot));
    cam->SetUp(up.Transformed(rot));

    m_view->Redraw();
    updateScaleBar();
    updateOrbitRingFlips();
    update();
}

}  // namespace hobbycad

