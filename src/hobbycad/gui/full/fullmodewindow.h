// =====================================================================
//  src/hobbycad/gui/full/fullmodewindow.h — Full Mode window (OpenGL 3.3+)
// =====================================================================

#ifndef HOBBYCAD_FULLMODEWINDOW_H
#define HOBBYCAD_FULLMODEWINDOW_H

#include <QHash>
#include "gui/mainwindow.h"
#include "gui/modeltoolbar.h"
#include "gui/parametersdialog.h"
#include "gui/sketchcanvas.h"
#include "gui/timelinewidget.h"
#include "gui/full/aissketchplane.h"

#include <hobbycad/sketch/profiles.h>

#include <AIS_Shape.hxx>
#include <Graphic3d_ClipPlane.hxx>
#include <Graphic3d_Camera.hxx>
#include <TopoDS_Shape.hxx>

#include <QList>
#include <optional>

class QLabel;
class QStackedWidget;
class QTreeWidgetItem;

namespace hobbycad {

class ViewportWidget;

class FullModeWindow : public MainWindow {
    Q_OBJECT

public:
    /// Abandon the 3D viewport and keep running without it.
    ///
    /// Called after an OCCT failure escaped an event handler. Everything
    /// that does not need a GL context (sketching, the object tree, file
    /// operations) keeps working, so this is a real degradation rather
    /// than a polite way of dying.
    ///
    /// @return true if the window is usable afterwards.
    bool dropViewport();

    explicit FullModeWindow(const OpenGLInfo& glInfo,
                            QWidget* parent = nullptr);

public slots:
    void beginStartupSketch(SketchPlane plane) override;
    void exitSketchMode() override;

    /// Orient the 3D camera square onto the active sketch's plane (Fusion's
    /// "Look At"). Also called automatically when switching to 3D in a sketch.
    void lookAtSketchPlane();

    /// Match the 3D viewport to the 2D sketch view when entering 3D: orient
    /// flat-on to the plane, center on the 2D view center, and match its
    /// scale (no fit-all); also drops the transient plane highlight.
    void syncViewportToSketchView();

    /// Save the model camera and match the viewport to the 2D sketch view;
    /// the first-3D-view seed, guarded so it runs once when the view is ready.
    void seedSketch3DView();

protected:
    // Full mode adds its drawing to the shared views: bodies and sketch
    // wireframes in the viewport, and the construction planes.
    void onDocumentLoaded() override;
    void onDocumentRecipeChanged() override;
    void onDocumentClosed() override;
    void refreshModelViews() override;
    void showSketchPlaneHighlight(SketchPlane plane, double offset,
                                  PlaneRotationAxis axis, double angle) override;
    void applyPreferences() override;
    SketchCanvas* activeSketchCanvas() const override;

private slots:
    void onNewConstructionPlane();
    void onConstructionPlaneSelected(int planeId);
    void onSketchEntityModified(int entityId);

    // Model tool handlers
    void onModelToolSelected(ModelTool tool);
    void performExtrude();
    void performRevolve();

private:
    // Overrides from MainWindow
    void showOriginPlaneProperties(SketchPlane plane) override;
    void hidePlaneHighlight() override { hideSketchPlane(); }

    void orientViewToSketchPlane(const Handle(V3d_View)& v);
    // Constructor wiring, one function per concern.
    void connectViewportSignals();
    void connectSketchModeToggle();
    void connectSketchToolbarValidation();
    void connectSketchCanvasSignals();
    void connectViewActions();
    void connectCliViewportCommands();
    void connectConstructionSignals();
    /// Show the project's bodies, index-aligned with m_solidAisShapes.
    void displayShapes();
    Handle(AIS_Shape) createSketchWireframe(const SketchData& sketch);

    // Sketch plane visualization
    void showSketchPlane(SketchPlane plane, double offset,
                         PlaneRotationAxis rotAxis = PlaneRotationAxis::X,
                         double rotAngle = 0.0);
    void hideSketchPlane();

    /// Show or hide one body in the viewport, by body ID.
    /// @return false if no such body.
    bool setBodyVisible(int bodyId, bool visible) override;

    // Construction plane display
    void displayConstructionPlane(int planeId);
    void eraseConstructionPlane(int planeId);
    /// Redraw every visible construction plane from the project (after
    /// undo/redo, Apply, or a project load).
    void refreshConstructionPlaneVisuals();
    /// The selection highlight at an arbitrary frame (construction planes,
    /// and the live preview of an edit).
    void showPlaneFrame(const gp_Ax3& frame);
    void previewConstructionPlane(const ConstructionPlaneData& plane);
    void applyConstructionPlaneEdit(const ConstructionPlaneData& plane);
    void resetConstructionPlanePreview();

    ViewportWidget*  m_viewport      = nullptr;
    QLabel*          m_axisLabel = nullptr;

    /// Each stored sketch's wireframe, by sketch id. Views of the project,
    /// rebuilt by refreshModelViews(); the sketches themselves are the session's.
    QHash<int, Handle(AIS_Shape)> m_sketchWireframes;

    // Sketch plane visualization
    Handle(Graphic3d_ClipPlane) m_sliceClipPlane;   ///< Active "Slice" section plane, if any
    Handle(AisSketchPlane) m_sketchPlaneVis;
    QHash<int, Handle(AisSketchPlane)> m_constructionPlaneVis;   ///< Committed planes by id

    /// Shaded handles of the project's bodies, index-aligned with them.
    QVector<Handle(AIS_Shape)> m_solidAisShapes;

    // 3D camera save/restore across the 2D/3D sketch toggle. The viewport is
    // one reused OCCT view, so entering a sketch saves the model camera before
    // looking flat-on at the plane; toggling preserves the in-sketch view; and
    // finishing the sketch restores the pre-sketch model camera.
    Handle(Graphic3d_Camera) m_preSketch3DCam;  ///< model camera before the sketch
    Handle(Graphic3d_Camera) m_inSketch3DCam;   ///< the in-sketch 3D orientation
    bool m_sketch3DSeeded = false;              ///< first 3D view of this sketch done
    bool m_pendingSketchSeed = false;           ///< 3D toggled before the view was ready; seed on viewInitialized
    std::optional<SketchPlane> m_pendingStartupSketch;  ///< --exec sketch waiting for full viewport init
};

}  // namespace hobbycad

#endif  // HOBBYCAD_FULLMODEWINDOW_H

