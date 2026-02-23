// =====================================================================
//  src/hobbycad/gui/full/fullmodewindow.h — Full Mode window (OpenGL 3.3+)
// =====================================================================

#ifndef HOBBYCAD_FULLMODEWINDOW_H
#define HOBBYCAD_FULLMODEWINDOW_H

#include "gui/mainwindow.h"
#include "gui/modeltoolbar.h"
#include "gui/parametersdialog.h"
#include "gui/sketchcanvas.h"
#include "gui/full/aissketchplane.h"

#include <AIS_Shape.hxx>
#include <TopoDS_Shape.hxx>

#include <QList>

class QLabel;
class QStackedWidget;

namespace hobbycad {

class ModelToolbar;
class SketchCanvas;
class SketchToolbar;
class TimelineWidget;
class ViewportWidget;

/// A completed sketch with its 3D representation
struct CompletedSketch {
    int featureId = 0;          ///< Unique feature ID for dependency tracking
    QString name;
    SketchPlane plane;
    double planeOffset = 0.0;   ///< Offset from origin along plane normal
    // Custom plane parameters (only used when plane == Custom)
    PlaneRotationAxis rotationAxis = PlaneRotationAxis::X;
    double rotationAngle = 0.0;  ///< Rotation angle in degrees
    QVector<SketchEntity> entities;
    Handle(AIS_Shape) aisShape;  ///< 3D wireframe for viewport display
    bool suppressed = false;    ///< True if suppressed in timeline
};

class FullModeWindow : public MainWindow {
    Q_OBJECT

public:
    explicit FullModeWindow(const OpenGLInfo& glInfo,
                            QWidget* parent = nullptr);

    /// Get document parameters (for formula fields)
    QMap<QString, double> parameterValues() const;

    /// Check if currently in sketch mode
    bool isSketchMode() const { return m_inSketchMode; }

public slots:
    /// Enter sketch editing mode
    void enterSketchMode(SketchPlane plane = SketchPlane::XY);

    /// Exit sketch editing mode
    void exitSketchMode();

protected:
    void onDocumentLoaded() override;
    void onDocumentClosed() override;
    void applyPreferences() override;
    SketchCanvas* activeSketchCanvas() const override;
    bool getSelectedSketchForExport(
        QVector<sketch::Entity>& outEntities,
        QVector<sketch::Constraint>& outConstraints) const override;

private slots:
    void showParametersDialog();
    void onParametersChanged(const QList<Parameter>& params);
    void onCreateSketchClicked();
    void onNewConstructionPlane();
    void onConstructionPlaneSelected(int planeId);
    void onSketchToolSelected(SketchTool tool);
    void onSketchSelectionChanged(int entityId);
    void onSketchEntityCreated(int entityId);
    void onSketchEntityModified(int entityId);

    // Timeline context menu handlers
    void onEditFeature(int index);
    void onRenameFeature(int index);
    void onDeleteFeature(int index);
    void onSuppressFeature(int index, bool suppress);
    void onFeatureMoved(int fromIndex, int toIndex);
    void onRollbackChanged(int index);
    void onExportSketchDXF(int index);
    void onExportSketchSVG(int index);

    // Model tool handlers
    void onModelToolSelected(ModelTool tool);
    void performExtrude();
    void performRevolve();

private:
    void createTimeline();
    void displayShapes();
    void showFeatureProperties(int index);
    void initDefaultParameters();
    void showSketchEntityProperties(int entityId);
    void showSketchProperties();
    void saveCurrentSketch();
    void discardCurrentSketch();
    Handle(AIS_Shape) createSketchWireframe(const CompletedSketch& sketch);

    // Sketch plane visualization
    void showSketchPlane(SketchPlane plane, double offset,
                         PlaneRotationAxis rotAxis = PlaneRotationAxis::X,
                         double rotAngle = 0.0);
    void hideSketchPlane();

    // Project loading helpers
    void loadProjectData();
    void populateFeatureTree();
    void populateTimeline();
    void loadSketchesFromProject();
    void loadParametersFromProject();
    void loadConstructionPlanesFromProject();
    void clearProjectData();

    // Construction plane display
    void displayConstructionPlane(int planeId);
    void hideConstructionPlane(int planeId);

    // Timeline feature helpers
    int sketchIndexFromTimelineIndex(int timelineIndex) const;
    int timelineIndexFromSketchIndex(int sketchIndex) const;

    // Toolbar stack (normal vs sketch mode)
    QStackedWidget*  m_toolbarStack  = nullptr;
    ModelToolbar*    m_toolbar       = nullptr;
    SketchToolbar*   m_sketchToolbar = nullptr;

    // Viewport stack (3D viewport vs sketch canvas)
    QStackedWidget*  m_viewportStack = nullptr;
    ViewportWidget*  m_viewport      = nullptr;
    SketchCanvas*    m_sketchCanvas  = nullptr;

    TimelineWidget*  m_timeline  = nullptr;
    QLabel*          m_axisLabel = nullptr;

    bool             m_inSketchMode = false;
    QList<Parameter> m_parameters;  ///< Document parameters

    // Completed sketches
    QVector<CompletedSketch> m_completedSketches;
    int m_currentSketchIndex = -1;  ///< Index of sketch being edited (-1 if new)
    int m_pendingSketchTimelineIdx = -1;  ///< Timeline index of sketch being created
    double m_pendingSketchOffset = 0.0;  ///< Offset for sketch being created
    PlaneRotationAxis m_pendingRotationAxis = PlaneRotationAxis::X;
    double m_pendingRotationAngle = 0.0;

    // Sketch plane visualization
    Handle(AisSketchPlane) m_sketchPlaneVis;

    // Feature ID tracking for dependencies
    int m_nextFeatureId = 1;  ///< Next feature ID to assign (0 reserved for Origin)

    // 3D solid bodies from extrude/revolve operations
    QVector<TopoDS_Shape> m_solidBodies;
    QVector<Handle(AIS_Shape)> m_solidAisShapes;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_FULLMODEWINDOW_H

