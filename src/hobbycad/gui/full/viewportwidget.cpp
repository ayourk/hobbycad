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

#include <AIS_InteractiveContext.hxx>
#include <AIS_Line.hxx>
#include <AIS_Shape.hxx>
#include <AIS_Trihedron.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <Aspect_NeutralWindow.hxx>
#include <Aspect_Grid.hxx>
#include <Geom_Axis2Placement.hxx>
#include <Geom_CartesianPoint.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <Prs3d_DatumAspect.hxx>
#include <V3d_AmbientLight.hxx>
#include <V3d_DirectionalLight.hxx>
#include <Quantity_Color.hxx>

#include <QMouseEvent>
#include <QWheelEvent>
#include <QResizeEvent>

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

    // Scale bar overlay (child widget, bottom-left)
    m_scaleBar = new ScaleBarWidget(this);
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

    // Position scale bar at bottom-left (width is auto-sized)
    if (m_scaleBar) {
        updateScaleBar();
        m_scaleBar->move(0, height() - m_scaleBar->height());
    }
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
        static_cast<Aspect_Drawable>(winId());
    nativeWindow->SetNativeHandle(nativeHandle);
    nativeWindow->SetSize(width(), height());

    m_view->SetWindow(nativeWindow);

    // Create the interactive context
    m_context = new AIS_InteractiveContext(m_viewer);
    m_context->SetDisplayMode(AIS_Shaded, true);

    // Set up the axis trihedron and grid
    setupAxisTrihedron();
    setupGrid();

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

    // Initialize scale bar
    m_scaleBar->setView(m_view);
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

// ---- Mouse interaction ----------------------------------------------

void ViewportWidget::mousePressEvent(QMouseEvent* event)
{
    m_lastMousePos = event->pos();

    if (event->button() == Qt::MiddleButton) {
        if (event->modifiers() & Qt::ShiftModifier) {
            m_panning = true;
        } else {
            m_rotating = true;
            if (!m_view.IsNull()) {
                m_view->StartRotation(event->pos().x(),
                                      event->pos().y());
            }
        }
    }

    QWidget::mousePressEvent(event);
}

void ViewportWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::MiddleButton) {
        m_rotating = false;
        m_panning  = false;
    }

    QWidget::mouseReleaseEvent(event);
}

void ViewportWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (m_view.IsNull()) return;

    QPoint pos = event->pos();

    if (m_rotating) {
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

// ---- Scale bar helper -----------------------------------------------

void ViewportWidget::updateScaleBar()
{
    if (m_scaleBar) {
        m_scaleBar->updateScale();
        m_scaleBar->move(0, height() - m_scaleBar->height());
    }
}

}  // namespace hobbycad

