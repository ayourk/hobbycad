// =====================================================================
//  src/hobbycad/gui/full/viewportwidget.h — OCCT AIS viewer widget
// =====================================================================
//
//  Uses a plain QWidget with WA_PaintOnScreen so OCCT fully owns the
//  OpenGL context.  This avoids the Qt 6 RHI / QOpenGLWidget conflict
//  where both Qt and OCCT try to manage GL contexts on the same
//  drawable.
//
//  Mouse-driven camera (Fusion 360 default):
//    pan (middle-drag), rotate (shift+middle-drag), zoom (scroll wheel).
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_VIEWPORTWIDGET_H
#define HOBBYCAD_VIEWPORTWIDGET_H

#include <QWidget>
#include <QTimer>

#include <AIS_InteractiveContext.hxx>
#include <AIS_ViewCube.hxx>
#include <V3d_View.hxx>
#include <V3d_Viewer.hxx>
#include <AIS_ViewController.hxx>

class QMouseEvent;
class QWheelEvent;
class QKeyEvent;
class QPushButton;

class AIS_Trihedron;
class AIS_InteractiveObject;

namespace hobbycad {

class ScaleBarWidget;

class ViewportWidget : public QWidget, protected AIS_ViewController {
    Q_OBJECT

public:
    /// Active rotation axis for PgUp/PgDn and arrow key rotation.
    enum RotationAxis { AxisX = 0, AxisY = 1, AxisZ = 2 };

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

    /// Reset camera to the default startup position.
    void resetCamera();

    /// Rotate the camera 90° around a world axis.
    /// axisDir: ±1 = X-tilt, ±2 = Z-spin (sign = direction).
    void rotateCamera90(int axisDir);

    /// Rotate the camera by an arbitrary angle (radians) around Z.
    void rotateCameraZ(double angleRad);

    /// Rotate the camera by an arbitrary angle (radians) around the
    /// currently selected axis.
    void rotateCameraAxis(double angleRad);

    /// Set the active rotation axis.
    void setRotationAxis(RotationAxis axis);

    /// Get the active rotation axis.
    RotationAxis rotationAxis() const;

    /// QPaintEngine must return nullptr for WA_PaintOnScreen widgets.
    QPaintEngine* paintEngine() const override { return nullptr; }

signals:
    /// Emitted when the active rotation axis changes.
    void rotationAxisChanged(RotationAxis axis);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

    // Mouse interaction
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

    // Keyboard interaction
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

private:
    void initViewer();
    void setupAxisTrihedron();
    void setupGrid();
    void setupViewCube();
    void setupRotationArrows();
    void positionRotationArrows();
    void updateScaleBar();

    Handle(V3d_Viewer)             m_viewer;
    Handle(V3d_View)               m_view;
    Handle(AIS_InteractiveContext)  m_context;
    Handle(AIS_ViewCube)           m_viewCube;
    bool                           m_initialized = false;
    bool                           m_gridVisible = true;

    ScaleBarWidget* m_scaleBar = nullptr;

    // Navigation buttons around the ViewCube (unused — WA_PaintOnScreen)
    QPushButton* m_arrowXPlus  = nullptr;
    QPushButton* m_arrowXMinus = nullptr;
    QPushButton* m_arrowZPlus  = nullptr;
    QPushButton* m_arrowZMinus = nullptr;
    QPushButton* m_homeButton  = nullptr;

    // Mouse tracking
    QPoint m_lastMousePos;
    bool   m_rotating = false;
    bool   m_panning  = false;
    bool   m_draggingViewCube = false;
    QPoint m_viewCubeDragStart;

    // Continuous rotation (PgUp/PgDn)
    QTimer  m_spinTimer;
    double  m_spinDirection = 0.0;  // +1 = CW, -1 = CCW, 0 = idle
    RotationAxis m_rotationAxis = AxisZ;  // default: rotate around Z
};

}  // namespace hobbycad

#endif  // HOBBYCAD_VIEWPORTWIDGET_H

