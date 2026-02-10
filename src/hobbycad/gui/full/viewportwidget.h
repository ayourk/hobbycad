// =====================================================================
//  src/hobbycad/gui/full/viewportwidget.h — OCCT AIS viewer widget
// =====================================================================
//
//  Uses a plain QWidget with WA_PaintOnScreen so OCCT fully owns the
//  OpenGL context.  This avoids the Qt 6 RHI / QOpenGLWidget conflict
//  where both Qt and OCCT try to manage GL contexts on the same
//  drawable.
//
//  Mouse-driven camera: rotate (middle-drag), pan (shift+middle-drag),
//  zoom (scroll wheel).
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_VIEWPORTWIDGET_H
#define HOBBYCAD_VIEWPORTWIDGET_H

#include <QWidget>

#include <AIS_InteractiveContext.hxx>
#include <V3d_View.hxx>
#include <V3d_Viewer.hxx>
#include <AIS_ViewController.hxx>

class QMouseEvent;
class QWheelEvent;

class AIS_Trihedron;
class AIS_InteractiveObject;

namespace hobbycad {

class ScaleBarWidget;

class ViewportWidget : public QWidget, protected AIS_ViewController {
    Q_OBJECT

public:
    explicit ViewportWidget(QWidget* parent = nullptr);
    ~ViewportWidget() override;

    /// Access the AIS interactive context for displaying shapes.
    Handle(AIS_InteractiveContext) context() const;

    /// Access the V3d_View (needed by scale bar, etc.).
    Handle(V3d_View) view() const;

    /// Reset the camera to view all displayed objects.
    void fitAll();

    /// Show or hide the XZ ground grid.
    void setGridVisible(bool visible);

    /// Returns true if the grid is currently visible.
    bool isGridVisible() const;

    /// QPaintEngine must return nullptr for WA_PaintOnScreen widgets.
    QPaintEngine* paintEngine() const override { return nullptr; }

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

    // Mouse interaction
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void initViewer();
    void setupAxisTrihedron();
    void setupGrid();
    void updateScaleBar();

    Handle(V3d_Viewer)             m_viewer;
    Handle(V3d_View)               m_view;
    Handle(AIS_InteractiveContext)  m_context;
    bool                           m_initialized = false;
    bool                           m_gridVisible = true;

    ScaleBarWidget* m_scaleBar = nullptr;

    // Mouse tracking
    QPoint m_lastMousePos;
    bool   m_rotating = false;
    bool   m_panning  = false;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_VIEWPORTWIDGET_H

