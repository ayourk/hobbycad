// =====================================================================
//  src/hobbycad/gui/full/fullmodewindow.h — Full Mode window (OpenGL 3.3+)
// =====================================================================

#ifndef HOBBYCAD_FULLMODEWINDOW_H
#define HOBBYCAD_FULLMODEWINDOW_H

#include "gui/mainwindow.h"
#include "gui/modeltoolbar.h"
#include "gui/parametersdialog.h"
#include "gui/sketchcanvas.h"
#include "gui/timelinewidget.h"
#include "gui/full/aissketchplane.h"

#include <hobbycad/sketch/profiles.h>

#include <AIS_Shape.hxx>
#include <TopoDS_Shape.hxx>

#include <QList>
#include <optional>

class QLabel;
class QStackedWidget;
class QTreeWidgetItem;

namespace hobbycad {

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

public slots:
    void enterSketchMode(SketchPlane plane = SketchPlane::XY) override;
    void exitSketchMode() override;

protected:
    void onDocumentLoaded() override;
    void onDocumentClosed() override;
    void applyPreferences() override;
    SketchCanvas* activeSketchCanvas() const override;
    bool getSelectedSketchForExport(
        QVector<sketch::Entity>& outEntities,
        QVector<sketch::Constraint>& outConstraints) const override;

private slots:
    void onNewConstructionPlane();
    void onConstructionPlaneSelected(int planeId);
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
    // Overrides from MainWindow
    void onSketchDeselected() override;
    void populateSketchFeatureProperties(QTreeWidgetItem* parent,
                                          int timelineIndex,
                                          const QString& units) override;
    void onCreateSketchClicked() override;
    void saveCurrentSketch() override;
    void discardCurrentSketch() override;

    void createTimeline();
    void displayShapes();
    void showSketchProperties();
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

    /// Validate that a timeline index is in bounds and not the Origin.
    /// Returns the feature type on success, or std::nullopt (with status message).
    std::optional<TimelineFeature> validateFeatureAction(
        int index, const QString& actionVerb) const;

    /// Retrieve a selected sketch's profiles for 3D operations (extrude/revolve).
    /// Returns std::nullopt with appropriate error messages on failure.
    struct SketchProfilesResult {
        const CompletedSketch* sketch = nullptr;
        std::vector<sketch::Entity> libEntities;
        std::vector<sketch::Profile> profiles;
    };
    std::optional<SketchProfilesResult> getSelectedSketchProfiles(
        const QString& operationName);

    ViewportWidget*  m_viewport      = nullptr;
    QLabel*          m_axisLabel = nullptr;

    // Completed sketches
    QVector<CompletedSketch> m_completedSketches;
    int m_currentSketchIndex = -1;  ///< Index of sketch being edited (-1 if new)
    int m_pendingSketchTimelineIdx = -1;  ///< Timeline index of sketch being created
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

