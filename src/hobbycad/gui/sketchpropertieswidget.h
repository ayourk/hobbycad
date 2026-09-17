// =====================================================================
//  src/hobbycad/gui/sketchpropertieswidget.h — Sketch properties widget
// =====================================================================
//
//  Dockable widget for viewing and editing sketch properties including:
//  - Background image settings (opacity, position, size)
//  - Grid settings
//  - Selected entity properties
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_SKETCHPROPERTIESWIDGET_H
#define HOBBYCAD_SKETCHPROPERTIESWIDGET_H

#include <hobbycad/sketch/background.h>

#include <QWidget>
class QFormLayout;
#include <QList>
#include <QPointF>
#include <hobbycad/sketch/transform.h>

class QLabel;
class QComboBox;
class QDockWidget;
class QShortcut;
class QLineEdit;
class QSlider;
class QSpinBox;
class QDoubleSpinBox;
class QCheckBox;
class QPushButton;
class QGroupBox;
class QStackedWidget;

namespace hobbycad {

class SketchCanvas;
struct SketchEntity;

/// Widget for displaying and editing sketch properties
class SketchPropertiesWidget : public QWidget {
    Q_OBJECT

public:
    explicit SketchPropertiesWidget(QWidget* parent = nullptr);

    /// Set the sketch canvas to monitor/edit
    void setSketchCanvas(SketchCanvas* canvas);

    /// Set the current background image for editing
    void setBackgroundImage(const sketch::BackgroundImage& bg);

    /// Get the current background image settings
    sketch::BackgroundImage backgroundImage() const { return m_background; }

    /// Set the project directory (for relative path handling)
    void setProjectDirectory(const QString& projectDir) { m_projectDir = projectDir; }

signals:
    /// Emitted when background image settings change
    void backgroundImageChanged(const sketch::BackgroundImage& bg);

    /// Emitted when user requests to remove background image
    void removeBackgroundImageRequested();

    /// Emitted when user toggles background edit mode
    void backgroundEditModeRequested(bool enabled);

    /// Emitted when user requests background scale calibration
    void calibrateBackgroundRequested();

public slots:
    /// Update display when selection changes
    void updateForSelection();

    /// The canvas context menu asked for a transform: expand the Transform
    /// section, select the move type, raise the dock. `transformType` is a
    /// sketch::TransformType as int (Move, Copy, Rotate, Scale, Mirror).
    void onTransformSectionRequested(int transformType);
    /// Commit the composed transform (Apply button, Enter, or the canvas).
    void applyTransform();

    /// Update the Edit Position button state
    void setBackgroundEditMode(bool enabled);

private slots:
    void onOpacityChanged(int percent);
    void onPositionXChanged(double x);
    void onPositionYChanged(double y);
    void onWidthChanged(double w);
    void onHeightChanged(double h);
    void onRotationChanged(double deg);
    void onScaleChanged(double mmPerPixel);
    void refreshScaleReadout();
    void onLockAspectChanged(bool locked);
    void onGrayscaleChanged(bool grayscale);
    void onContrastChanged(double contrast);
    void onBrightnessChanged(double brightness);
    void onFlipHorizontal();
    void onFlipVertical();
    void onRotate90CW();
    void onRotate90CCW();
    void onRotate180();
    void onBrowseForImage();
    void onExportToProject();
    void onEditPositionToggled(bool checked);
    void onLabelAngleChanged(double degrees);
    void onCoordChanged();  // an editable coordinate spin changed
    // Ellipse editor. Each goes through one compound canvas edit, because a
    // radius or rotation change moves both axis points at once.
    void onEllipseMajorChanged(double v);
    void onEllipseMinorChanged(double v);
    void onEllipseRotationChanged(double deg);
    void onEllipseArcToggled(bool on);
    void onEllipseStartChanged(double deg);
    void onEllipseSweepChanged(double deg);
    void onEllipseShowAxesToggled(bool on);
    void onConicRhoChanged(double rho);   // the conic's stored rho (Conic section)
    void onCoordPointChanged(int index);  // the point-selector combo changed
    void loadCoordSpinsFromPoint(int index);  // fill u/v/w from the entity's point
    void onBezierAngleChanged(double deg);
    void onBezierInLenChanged(double len);
    void onBezierOutLenChanged(double len);
    void onBezierWeightChanged(double w);
    void onBezierLegLengthChanged(double len);

    // Transform section
    void onTransformToggled(bool on);
    void onMoveTypeChanged(int index);
    void onTransformFieldChanged();
    void onPivotFieldChanged();
    void onSetPivotClicked(bool checked);
    void onCenterPivotClicked();
    void onPickButtonClicked(int pick);
    void onCanvasPivotChanged(const QPointF& world, bool stored);
    void onCanvasPickCompleted(int pick, const QPointF& world);
    void onCanvasFreeMoveChanged(const QPointF& delta, double angleDeg);
    void onCanvasTransformCanceled();
    void onCanvasEntityModified(int entityId);

private:
    void setupUi();
    void setupBezierAnchorSection();
    void setupBackgroundSection();
    void setupEntitySection();
    void setupTransformSection();
    void setupEllipseSection();
    void setupConicSection();
    QString conicKindText(double rho) const;   // elliptical / parabolic / hyperbolic, translated
    void refreshConicReadout(const SketchEntity& conic);   // kind + apex labels
    void updateTransformRows();
    void resetTransformForm();
    bool currentTransformParams(sketch::GroupTransformParams& out, QString* whyNot = nullptr) const;
    void refreshTransformPreview();
    void showTransformStatus(const QString& text, bool isError);
    static QString pointText(const QPointF& p);
    void updateBackgroundUi();
    void emitBackgroundChanged();

    // Reference to canvas
    SketchCanvas* m_canvas = nullptr;

    // Background image data
    sketch::BackgroundImage m_background;
    QString m_projectDir;       // Project root directory
    bool m_updatingUi = false;  // Prevent feedback loops

    // Transform section
    enum class MoveType { Translate, Rotate, Scale, Mirror, PointToPoint, PointToPosition, FreeMove };
    QGroupBox* m_transformGroup = nullptr;
    // ---- Bezier anchor editor ----
    QGroupBox* m_bezierAnchorGroup = nullptr;
    QDoubleSpinBox* m_baAngle = nullptr;
    QDoubleSpinBox* m_baInLen = nullptr;
    QDoubleSpinBox* m_baOutLen = nullptr;
    QDoubleSpinBox* m_baWeight = nullptr;
    int m_baSplineId = -1;
    int m_baAnchorIdx = -1;
    QGroupBox* m_bezierLegGroup = nullptr;
    QDoubleSpinBox* m_blLength = nullptr;
    int m_blSplineId = -1; int m_blI0 = -1; int m_blI1 = -1;
    // ---- Ellipse editor ----
    QGroupBox* m_ellipseGroup = nullptr;
    QDoubleSpinBox* m_ellMajor = nullptr;
    QDoubleSpinBox* m_ellMinor = nullptr;
    QDoubleSpinBox* m_ellRotation = nullptr;
    QCheckBox* m_ellArcCheck = nullptr;
    QDoubleSpinBox* m_ellStart = nullptr;
    QDoubleSpinBox* m_ellSweep = nullptr;
    QCheckBox* m_ellShowAxes = nullptr;
    int m_ellEntityId = -1;
    // ---- Conic editor: a Spline authored by rho (conicRho > 0) ----
    QGroupBox* m_conicGroup = nullptr;
    QDoubleSpinBox* m_conicRho = nullptr;
    QLabel* m_conicKind = nullptr;
    QLabel* m_conicApex = nullptr;
    int m_conicId = -1;
    QWidget* m_transformBody = nullptr;
    QComboBox* m_moveType = nullptr;
    QDoubleSpinBox* m_txDx = nullptr; QDoubleSpinBox* m_txDy = nullptr;
    QDoubleSpinBox* m_rotAngle = nullptr;
    QDoubleSpinBox* m_scaleFactor = nullptr;
    QComboBox* m_mirrorAxis = nullptr;
    QPushButton* m_pickMirrorA = nullptr; QPushButton* m_pickMirrorB = nullptr;
    QLabel* m_mirrorALabel = nullptr; QLabel* m_mirrorBLabel = nullptr;
    QPushButton* m_pickFrom = nullptr; QPushButton* m_pickTo = nullptr;
    QLabel* m_fromLabel = nullptr; QLabel* m_toLabel = nullptr; QLabel* m_deltaLabel = nullptr;
    QPushButton* m_pickPoint = nullptr; QLabel* m_pickedLabel = nullptr;
    QDoubleSpinBox* m_targetX = nullptr; QDoubleSpinBox* m_targetY = nullptr;
    QComboBox* m_targetMode = nullptr;                 ///< Absolute / Relative to reference
    QPushButton* m_pickRef = nullptr; QLabel* m_refLabel = nullptr;
    QLabel* m_targetXLabel = nullptr; QLabel* m_targetYLabel = nullptr;
    QList<QWidget*> m_rowsP2PosRef;
    bool m_haveRef = false; QPointF m_refPt;
    QDoubleSpinBox* m_fmDx = nullptr; QDoubleSpinBox* m_fmDy = nullptr; QDoubleSpinBox* m_fmAngle = nullptr;
    QLabel* m_fmHint = nullptr;
    QDoubleSpinBox* m_pivotX = nullptr; QDoubleSpinBox* m_pivotY = nullptr;
    QPushButton* m_setPivotBtn = nullptr; QPushButton* m_centerPivotBtn = nullptr;
    QLabel* m_pivotSourceLabel = nullptr;
    QCheckBox* m_createCopy = nullptr;
    QPushButton* m_applyBtn = nullptr;
    QLabel* m_transformStatus = nullptr;
    QList<QWidget*> m_rowsTranslate, m_rowsRotate, m_rowsScale, m_rowsMirror, m_rowsMirrorLine,
                    m_rowsP2P, m_rowsP2Pos, m_rowsFreeMove, m_rowsPivot;
    bool m_haveFrom = false, m_haveTo = false, m_havePicked = false, m_haveMirrorA = false, m_haveMirrorB = false;
    QPointF m_fromPt, m_toPt, m_pickedPt, m_mirrorAPt, m_mirrorBPt;
    bool m_entityModifiedQueued = false;

    // UI elements - Background section
    QGroupBox* m_backgroundGroup = nullptr;
    QLabel* m_bgFilePathLabel = nullptr;
    QLineEdit* m_bgFilePathEdit = nullptr;
    QPushButton* m_bgBrowseButton = nullptr;
    QPushButton* m_bgExportButton = nullptr;
    QPushButton* m_bgRemoveButton = nullptr;
    QPushButton* m_bgEditPositionButton = nullptr;
    QPushButton* m_bgCalibrateButton = nullptr;

    QSlider* m_bgOpacitySlider = nullptr;
    QSpinBox* m_bgOpacitySpinBox = nullptr;

    QDoubleSpinBox* m_bgPositionX = nullptr;
    QDoubleSpinBox* m_bgPositionY = nullptr;
    QDoubleSpinBox* m_bgWidth = nullptr;
    QDoubleSpinBox* m_bgHeight = nullptr;
    QDoubleSpinBox* m_bgRotation = nullptr;
    QDoubleSpinBox* m_bgScaleFactor = nullptr;   ///< millimeters per image pixel
    QLabel* m_bgScaleInfo = nullptr;             ///< pixel size and the scale, spelled out
    QCheckBox* m_bgLockAspect = nullptr;

    QCheckBox* m_bgGrayscale = nullptr;
    QDoubleSpinBox* m_bgContrast = nullptr;
    QDoubleSpinBox* m_bgBrightness = nullptr;

    // Flip/rotate buttons
    QPushButton* m_bgFlipHButton = nullptr;
    QPushButton* m_bgFlipVButton = nullptr;
    QPushButton* m_bgRotateCWButton = nullptr;
    QPushButton* m_bgRotateCCWButton = nullptr;
    QPushButton* m_bgRotate180Button = nullptr;

    // UI elements - Entity section
    QGroupBox* m_entityGroup = nullptr;
    QStackedWidget* m_entityStack = nullptr;

    // Entity properties (page 1)
    QLabel* m_entityTypeLabel = nullptr;
    QLabel* m_entityIdLabel = nullptr;
    QLabel* m_entityConstructionLabel = nullptr;
    // Coordinates of the selected entity's primary point, labeled by the
    // sketch plane's axes (planeAxisLabels). The off-plane (w) row shows only
    // in 3D mode. Editable: writes back via SketchCanvas::applyPointEdit.
    QFormLayout* m_entityForm = nullptr;
    QLabel* m_coordPointLabel = nullptr; QComboBox* m_coordPointCombo = nullptr;
    QLabel* m_coordULabel = nullptr; QDoubleSpinBox* m_coordUSpin = nullptr;
    QLabel* m_coordVLabel = nullptr; QDoubleSpinBox* m_coordVSpin = nullptr;
    QLabel* m_coordWLabel = nullptr; QDoubleSpinBox* m_coordWSpin = nullptr;
    int m_coordEntityId = -1;      // entity whose point the coord spins edit
    int m_coordPointIndex = -1;    // which point (the primary point, index 0)
    // Projected entities are driven by a source sketch: show that source and
    // mark the coordinates read-only (edit happens in the source).
    QLabel* m_projectionRowLabel = nullptr;
    QLabel* m_projectionValue = nullptr;

    // Constraint properties (page 2)
    QLabel* m_constraintTypeLabel = nullptr;
    QLabel* m_constraintValueLabel = nullptr;
    QLabel* m_constraintDrivingLabel = nullptr;
    QLabel* m_constraintLabelAngleRow = nullptr;
    QDoubleSpinBox* m_constraintLabelAngleSpin = nullptr;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_SKETCHPROPERTIESWIDGET_H
