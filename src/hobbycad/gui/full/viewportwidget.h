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
#include <gp_Pnt.hxx>
#include <gp_Dir.hxx>

class QMouseEvent;
class QWheelEvent;
class QKeyEvent;

class AIS_Trihedron;
class AIS_InteractiveObject;

namespace hobbycad {

class AisGrid;
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

    /// Set Z-up (true) or Y-up (false) coordinate convention.
    void setZUpOrientation(bool zUp);

    /// Returns true if using Z-up orientation.
    bool isZUpOrientation() const;

    /// Set whether to orbit around selected object's center.
    void setOrbitSelectedObject(bool enabled);

    /// Returns true if orbiting around selected object.
    bool isOrbitSelectedObject() const;

    // ---- CLI viewport control commands ----

    /// Set zoom level as a percentage (100 = 1:1, 200 = 2x zoom in).
    void setZoomPercent(double percent);

    /// Get current approximate zoom percentage.
    double zoomPercent() const;

    /// Pan the camera to center on the specified world coordinates.
    void panTo(double x, double y, double z);

    /// Get the current camera target (center) point.
    void cameraTarget(double& x, double& y, double& z) const;

    /// Rotate the camera by specified degrees around a world axis.
    /// @param axis  'x', 'y', or 'z' (case insensitive)
    /// @param degrees  Rotation angle in degrees
    void rotateOnAxis(char axis, double degrees);

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

    /// Update orbit ring arrow directions based on camera orientation.
    /// When viewing from "behind" an axis, the arrows should flip.
    void updateOrbitRingFlips();

    Handle(V3d_Viewer)             m_viewer;
    Handle(V3d_View)               m_view;
    Handle(AIS_InteractiveContext)  m_context;
    Handle(AIS_ViewCube)           m_viewCube;
    Handle(NavOrbitRing)           m_ringX;
    Handle(NavOrbitRing)           m_ringY;
    Handle(NavOrbitRing)           m_ringZ;
    Handle(NavHomeButton)          m_navHome;
    Handle(ScaleBarWidget)         m_scaleBar;
    Handle(AisGrid)                m_grid;
    bool                           m_initialized = false;
    bool                           m_gridVisible = true;
    bool                           m_zUpOrientation = true;
    bool                           m_orbitSelectedObject = false;

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

    // Orbit center - updated by panning, used by ViewCube and rotations
    gp_Pnt  m_orbitCenter;
    gp_Pnt  m_savedOrbitCenter;  // saved when switching to orbit-selected mode

    // ViewCube animation (smooth orbit transition)
    QTimer  m_viewCubeAnimTimer;
    gp_Pnt  m_animStartEye;
    gp_Pnt  m_animEndEye;
    gp_Dir  m_animStartUp;
    gp_Dir  m_animEndUp;
    gp_Pnt  m_animOrbitCenter;
    int     m_animStep = 0;
    int     m_animTotalSteps = 15;  // ~250ms at 60fps
};

}  // namespace hobbycad

#endif  // HOBBYCAD_VIEWPORTWIDGET_H

