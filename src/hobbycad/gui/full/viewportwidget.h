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

class AIS_Trihedron;
class AIS_InteractiveObject;

namespace hobbycad {

class ScaleBarWidget;
class NavOrbitRing;
class NavHomeButton;

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

    /// Set PgUp/PgDn step size in degrees and interval in ms.
    void setSpinParams(int stepDeg, int intervalMs);

    /// Set arrow key snap step size in degrees and interval in ms.
    void setSnapParams(int stepDeg, int intervalMs);

    /// Set the display unit system for the scale bar.
    void setUnitSystem(int units);

    /// Start an animated 90-degree snap rotation around the given axis.
    /// @param axis  Which world axis to rotate around.
    /// @param direction  +1 = positive rotation, -1 = negative.
    void startSnapRotation(RotationAxis axis, int direction);

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
    void setupNavControls();
    void updateScaleBar();

    /// Handle a click on a NavControlOwner in the AIS context.
    /// Returns true if a nav control was clicked and handled.
    bool handleNavControlClick(int theX, int theY);

    Handle(V3d_Viewer)             m_viewer;
    Handle(V3d_View)               m_view;
    Handle(AIS_InteractiveContext)  m_context;
    Handle(AIS_ViewCube)           m_viewCube;
    Handle(NavOrbitRing)           m_ringX;
    Handle(NavOrbitRing)           m_ringY;
    Handle(NavOrbitRing)           m_ringZ;
    Handle(NavHomeButton)          m_navHome;
    Handle(ScaleBarWidget)         m_scaleBar;
    bool                           m_initialized = false;
    bool                           m_gridVisible = true;

    // Mouse tracking
    QPoint m_lastMousePos;
    bool   m_rotating = false;
    bool   m_panning  = false;
    bool   m_draggingViewCube = false;
    QPoint m_viewCubeDragStart;

    // Continuous rotation (PgUp/PgDn)
    QTimer  m_spinTimer;
    double  m_spinDirection = 0.0;  // +1 = CW, -1 = CCW, 0 = idle
    int     m_spinStepDeg   = 10;   // degrees per tick
    RotationAxis m_rotationAxis = AxisX;  // default: rotate around X

    // Animated 90° snap rotation (Left/Right arrows)
    QTimer  m_snapTimer;
    double  m_snapStepRad = 0.0;   // per-tick step (radians)
    int     m_snapRemaining = 0;   // ticks left
    int     m_snapStepDeg   = 10;  // degrees per frame
};

}  // namespace hobbycad

#endif  // HOBBYCAD_VIEWPORTWIDGET_H

