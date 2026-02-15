// =====================================================================
//  src/hobbycad/gui/backgroundcalibrationdialog.h — Background calibration
// =====================================================================
//
//  Dialog for calibrating background image scale using a known dimension.
//  User clicks two points on the image and enters the real-world distance.
//
//  Part of HobbyCAD GUI.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_GUI_BACKGROUNDCALIBRATIONDIALOG_H
#define HOBBYCAD_GUI_BACKGROUNDCALIBRATIONDIALOG_H

#include <QDialog>
#include <QPointF>
#include <hobbycad/sketch/background.h>

class QLabel;
class QDoubleSpinBox;
class QPushButton;
class QComboBox;
class QCheckBox;
class QGroupBox;
class QRadioButton;

namespace hobbycad {

class SketchCanvas;

/// Dialog for calibrating background image scale from two points
class BackgroundCalibrationDialog : public QDialog
{
    Q_OBJECT

public:
    explicit BackgroundCalibrationDialog(QWidget* parent = nullptr);
    ~BackgroundCalibrationDialog() override = default;

    /// Set the canvas for point picking
    void setSketchCanvas(SketchCanvas* canvas);

    /// Set the current background image
    void setBackgroundImage(const sketch::BackgroundImage& bg);

    /// Get the calibrated background image
    sketch::BackgroundImage calibratedBackground() const;

    /// Check if calibration was applied
    bool wasCalibrated() const { return m_calibrated; }

signals:
    /// Emitted when calibration mode should be started
    void calibrationModeRequested(bool enabled);

    /// Emitted when entity selection mode should be started
    void entitySelectionRequested(bool enabled);

protected:
    void showEvent(QShowEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

public slots:
    /// Called when a sketch entity is selected for alignment reference
    void onEntitySelected(int entityId, double angle);

private slots:
    void onStartPointPicking();
    void onPointPicked(const QPointF& point);
    void onSelectReferenceEntity();
    void onApplyCalibration();
    void updatePreview();

private:
    void setupUi();
    void updatePointDisplay();
    void resetPoints();

    SketchCanvas* m_canvas = nullptr;
    sketch::BackgroundImage m_background;
    bool m_calibrated = false;

    // Point selection state
    enum class PickState { Idle, PickingFirst, PickingSecond };
    PickState m_pickState = PickState::Idle;
    QPointF m_point1;  // First point in sketch coordinates
    QPointF m_point2;  // Second point in sketch coordinates
    bool m_hasPoint1 = false;
    bool m_hasPoint2 = false;

    // UI elements
    QLabel* m_instructionLabel = nullptr;
    QLabel* m_point1Label = nullptr;
    QLabel* m_point2Label = nullptr;
    QLabel* m_measuredDistanceLabel = nullptr;
    QDoubleSpinBox* m_realDistanceSpinBox = nullptr;
    QComboBox* m_unitComboBox = nullptr;
    QPushButton* m_pickPointsButton = nullptr;
    QPushButton* m_resetButton = nullptr;
    QPushButton* m_applyButton = nullptr;
    QLabel* m_previewLabel = nullptr;

    // Auto-rotation UI elements
    QGroupBox* m_alignmentGroup = nullptr;
    QCheckBox* m_enableAlignmentCheckBox = nullptr;
    QRadioButton* m_alignToAxisRadio = nullptr;
    QRadioButton* m_alignToEntityRadio = nullptr;
    QComboBox* m_alignmentAxisComboBox = nullptr;
    QDoubleSpinBox* m_customAngleSpinBox = nullptr;
    QPushButton* m_selectEntityButton = nullptr;
    QLabel* m_selectedEntityLabel = nullptr;
    QLabel* m_currentAngleLabel = nullptr;
    QLabel* m_rotationNeededLabel = nullptr;

    // Reference entity state
    bool m_hasReferenceEntity = false;
    int m_referenceEntityId = -1;
    double m_referenceEntityAngle = 0.0;
    bool m_selectingEntity = false;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_GUI_BACKGROUNDCALIBRATIONDIALOG_H
