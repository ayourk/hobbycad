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

#include <AIS_InteractiveContext.hxx>
#include <AIS_Shape.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <Aspect_NeutralWindow.hxx>
#include <OpenGl_GraphicDriver.hxx>
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

void ViewportWidget::fitAll()
{
    if (!m_view.IsNull()) {
        m_view->FitAll(0.01, false);
        m_view->Invalidate();
        update();
    }
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
    // Because we set WA_NativeWindow + WA_PaintOnScreen, the widget
    // has a real X11 Window (or HWND on Windows) that OCCT can use.
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

    // Default view orientation
    m_view->SetProj(V3d_XposYnegZpos);
    m_view->FitAll(0.01, false);
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
        update();
    } else if (m_panning) {
        m_view->Pan(pos.x() - m_lastMousePos.x(),
                    m_lastMousePos.y() - pos.y());
        m_lastMousePos = pos;
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

    update();
    QWidget::wheelEvent(event);
}

}  // namespace hobbycad

