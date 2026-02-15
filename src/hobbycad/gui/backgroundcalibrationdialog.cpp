// =====================================================================
//  src/hobbycad/gui/backgroundcalibrationdialog.cpp — Background calibration
// =====================================================================
//
//  Part of HobbyCAD GUI.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include "backgroundcalibrationdialog.h"
#include "sketchcanvas.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QComboBox>
#include <QCheckBox>
#include <QRadioButton>
#include <QButtonGroup>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <QtMath>

namespace hobbycad {

BackgroundCalibrationDialog::BackgroundCalibrationDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Calibrate Background"));
    setMinimumWidth(450);
    setupUi();
}

void BackgroundCalibrationDialog::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);

    // Instructions
    m_instructionLabel = new QLabel(tr(
        "Calibrate the background image by selecting two reference points.\n\n"
        "1. Click 'Pick Points' to start\n"
        "2. Click two points on a known dimension\n"
        "3. Enter the real-world distance\n"
        "4. Optionally align to an axis\n"
        "5. Click 'Apply' to calibrate"));
    m_instructionLabel->setWordWrap(true);
    mainLayout->addWidget(m_instructionLabel);

    // Point picking section
    auto* pointGroup = new QGroupBox(tr("Reference Points"));
    auto* pointLayout = new QVBoxLayout(pointGroup);

    // Point 1
    auto* point1Layout = new QHBoxLayout;
    point1Layout->addWidget(new QLabel(tr("Point 1:")));
    m_point1Label = new QLabel(tr("Not set"));
    m_point1Label->setStyleSheet("QLabel { color: #666; font-style: italic; }");
    point1Layout->addWidget(m_point1Label, 1);
    pointLayout->addLayout(point1Layout);

    // Point 2
    auto* point2Layout = new QHBoxLayout;
    point2Layout->addWidget(new QLabel(tr("Point 2:")));
    m_point2Label = new QLabel(tr("Not set"));
    m_point2Label->setStyleSheet("QLabel { color: #666; font-style: italic; }");
    point2Layout->addWidget(m_point2Label, 1);
    pointLayout->addLayout(point2Layout);

    // Measured distance (in current image scale)
    auto* measuredLayout = new QHBoxLayout;
    measuredLayout->addWidget(new QLabel(tr("Current distance:")));
    m_measuredDistanceLabel = new QLabel(tr("--"));
    measuredLayout->addWidget(m_measuredDistanceLabel, 1);
    pointLayout->addLayout(measuredLayout);

    // Pick points button
    auto* pickLayout = new QHBoxLayout;
    m_pickPointsButton = new QPushButton(tr("Pick Points"));
    m_pickPointsButton->setToolTip(tr("Click to start selecting two points on the background image"));
    connect(m_pickPointsButton, &QPushButton::clicked, this, &BackgroundCalibrationDialog::onStartPointPicking);
    pickLayout->addWidget(m_pickPointsButton);

    m_resetButton = new QPushButton(tr("Reset"));
    m_resetButton->setToolTip(tr("Clear selected points"));
    connect(m_resetButton, &QPushButton::clicked, this, &BackgroundCalibrationDialog::resetPoints);
    pickLayout->addWidget(m_resetButton);
    pickLayout->addStretch();
    pointLayout->addLayout(pickLayout);

    mainLayout->addWidget(pointGroup);

    // Real distance input
    auto* distanceGroup = new QGroupBox(tr("Known Distance"));
    auto* distanceLayout = new QHBoxLayout(distanceGroup);

    distanceLayout->addWidget(new QLabel(tr("Real distance:")));

    m_realDistanceSpinBox = new QDoubleSpinBox;
    m_realDistanceSpinBox->setRange(0.001, 100000);
    m_realDistanceSpinBox->setDecimals(3);
    m_realDistanceSpinBox->setValue(100.0);
    m_realDistanceSpinBox->setToolTip(tr("Enter the actual distance between the two points"));
    connect(m_realDistanceSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &BackgroundCalibrationDialog::updatePreview);
    distanceLayout->addWidget(m_realDistanceSpinBox);

    m_unitComboBox = new QComboBox;
    m_unitComboBox->addItem(tr("mm"), 1.0);
    m_unitComboBox->addItem(tr("cm"), 10.0);
    m_unitComboBox->addItem(tr("m"), 1000.0);
    m_unitComboBox->addItem(tr("in"), 25.4);
    m_unitComboBox->addItem(tr("ft"), 304.8);
    m_unitComboBox->setToolTip(tr("Unit of measurement"));
    connect(m_unitComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &BackgroundCalibrationDialog::updatePreview);
    distanceLayout->addWidget(m_unitComboBox);

    distanceLayout->addStretch();

    mainLayout->addWidget(distanceGroup);

    // Alignment/Rotation section
    m_alignmentGroup = new QGroupBox(tr("Auto-Align Rotation"));
    auto* alignLayout = new QVBoxLayout(m_alignmentGroup);

    m_enableAlignmentCheckBox = new QCheckBox(tr("Align reference line"));
    m_enableAlignmentCheckBox->setToolTip(tr(
        "Rotate the image so the line between the two points aligns with a target"));
    connect(m_enableAlignmentCheckBox, &QCheckBox::toggled, this, &BackgroundCalibrationDialog::updatePreview);
    alignLayout->addWidget(m_enableAlignmentCheckBox);

    // Radio buttons for alignment mode
    auto* alignModeGroup = new QButtonGroup(this);

    m_alignToAxisRadio = new QRadioButton(tr("Sketch axis or angle:"));
    m_alignToAxisRadio->setChecked(true);
    m_alignToAxisRadio->setToolTip(tr("Align to the sketch X/Y axes or a specific angle"));
    connect(m_alignToAxisRadio, &QRadioButton::toggled, this, &BackgroundCalibrationDialog::updatePreview);
    alignModeGroup->addButton(m_alignToAxisRadio);

    m_alignToEntityRadio = new QRadioButton(tr("Sketch entity (line):"));
    m_alignToEntityRadio->setToolTip(tr("Align to an existing line or construction line in the sketch"));
    connect(m_alignToEntityRadio, &QRadioButton::toggled, this, &BackgroundCalibrationDialog::updatePreview);
    alignModeGroup->addButton(m_alignToEntityRadio);

    // Axis alignment controls
    auto* axisLayout = new QHBoxLayout;
    axisLayout->addWidget(m_alignToAxisRadio);

    m_alignmentAxisComboBox = new QComboBox;
    // Sketch coordinate axes (always available)
    m_alignmentAxisComboBox->addItem(tr("X Axis (Horizontal)"), 0.0);
    m_alignmentAxisComboBox->addItem(tr("Y Axis (Vertical)"), 90.0);
    // Common angles
    m_alignmentAxisComboBox->addItem(tr("45° Diagonal"), 45.0);
    m_alignmentAxisComboBox->addItem(tr("-45° Diagonal"), -45.0);
    m_alignmentAxisComboBox->addItem(tr("30°"), 30.0);
    m_alignmentAxisComboBox->addItem(tr("60°"), 60.0);
    m_alignmentAxisComboBox->addItem(tr("-30°"), -30.0);
    m_alignmentAxisComboBox->addItem(tr("-60°"), -60.0);
    m_alignmentAxisComboBox->addItem(tr("Custom Angle..."), -999.0);  // Sentinel for custom
    m_alignmentAxisComboBox->setToolTip(tr(
        "Target angle for alignment.\n"
        "X Axis and Y Axis refer to the sketch coordinate system."));
    connect(m_alignmentAxisComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &BackgroundCalibrationDialog::updatePreview);
    axisLayout->addWidget(m_alignmentAxisComboBox);

    m_customAngleSpinBox = new QDoubleSpinBox;
    m_customAngleSpinBox->setRange(-180.0, 180.0);
    m_customAngleSpinBox->setDecimals(1);
    m_customAngleSpinBox->setSuffix(tr("°"));
    m_customAngleSpinBox->setValue(0.0);
    m_customAngleSpinBox->setToolTip(tr("Enter a custom target angle"));
    m_customAngleSpinBox->setVisible(false);  // Hidden until "Custom" is selected
    connect(m_customAngleSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &BackgroundCalibrationDialog::updatePreview);
    axisLayout->addWidget(m_customAngleSpinBox);

    axisLayout->addStretch();
    alignLayout->addLayout(axisLayout);

    // Entity alignment controls
    auto* entityLayout = new QHBoxLayout;
    entityLayout->addWidget(m_alignToEntityRadio);

    m_selectEntityButton = new QPushButton(tr("Select Entity..."));
    m_selectEntityButton->setToolTip(tr("Click to select a line or construction geometry in the sketch"));
    connect(m_selectEntityButton, &QPushButton::clicked, this, &BackgroundCalibrationDialog::onSelectReferenceEntity);
    entityLayout->addWidget(m_selectEntityButton);

    m_selectedEntityLabel = new QLabel(tr("None selected"));
    m_selectedEntityLabel->setStyleSheet("QLabel { color: #666; font-style: italic; }");
    entityLayout->addWidget(m_selectedEntityLabel, 1);

    alignLayout->addLayout(entityLayout);

    // Display current angle and rotation needed
    auto* angleForm = new QFormLayout;
    angleForm->setSpacing(4);

    m_currentAngleLabel = new QLabel(tr("--"));
    angleForm->addRow(tr("Current line angle:"), m_currentAngleLabel);

    m_rotationNeededLabel = new QLabel(tr("--"));
    angleForm->addRow(tr("Rotation to apply:"), m_rotationNeededLabel);

    alignLayout->addLayout(angleForm);
    mainLayout->addWidget(m_alignmentGroup);

    // Preview
    auto* previewGroup = new QGroupBox(tr("Preview"));
    auto* previewLayout = new QVBoxLayout(previewGroup);
    m_previewLabel = new QLabel(tr("Select two points and enter a distance to see the calibration preview."));
    m_previewLabel->setWordWrap(true);
    previewLayout->addWidget(m_previewLabel);
    mainLayout->addWidget(previewGroup);

    mainLayout->addStretch();

    // Dialog buttons
    auto* buttonBox = new QDialogButtonBox;

    m_applyButton = buttonBox->addButton(tr("Apply Calibration"), QDialogButtonBox::AcceptRole);
    m_applyButton->setEnabled(false);
    connect(m_applyButton, &QPushButton::clicked, this, &BackgroundCalibrationDialog::onApplyCalibration);

    auto* cancelButton = buttonBox->addButton(QDialogButtonBox::Cancel);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);

    mainLayout->addWidget(buttonBox);
}

void BackgroundCalibrationDialog::setSketchCanvas(SketchCanvas* canvas)
{
    if (m_canvas) {
        disconnect(m_canvas, nullptr, this, nullptr);
        disconnect(this, nullptr, m_canvas, nullptr);
    }

    m_canvas = canvas;

    if (m_canvas) {
        // Connect canvas signals to dialog slots
        connect(m_canvas, &SketchCanvas::calibrationPointPicked,
                this, &BackgroundCalibrationDialog::onPointPicked);
        connect(m_canvas, &SketchCanvas::calibrationEntitySelected,
                this, &BackgroundCalibrationDialog::onEntitySelected);

        // Connect dialog signals to canvas slots
        connect(this, &BackgroundCalibrationDialog::calibrationModeRequested,
                m_canvas, &SketchCanvas::setBackgroundCalibrationMode);
        connect(this, &BackgroundCalibrationDialog::entitySelectionRequested,
                m_canvas, &SketchCanvas::setCalibrationEntitySelectionMode);
    }
}

void BackgroundCalibrationDialog::setBackgroundImage(const sketch::BackgroundImage& bg)
{
    m_background = bg;
    m_calibrated = false;
    resetPoints();
}

sketch::BackgroundImage BackgroundCalibrationDialog::calibratedBackground() const
{
    return m_background;
}

void BackgroundCalibrationDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    resetPoints();
}

void BackgroundCalibrationDialog::closeEvent(QCloseEvent* event)
{
    // Exit calibration mode when dialog closes
    if (m_pickState != PickState::Idle) {
        m_pickState = PickState::Idle;
        emit calibrationModeRequested(false);
    }
    if (m_selectingEntity) {
        m_selectingEntity = false;
        emit entitySelectionRequested(false);
    }
    QDialog::closeEvent(event);
}

void BackgroundCalibrationDialog::onStartPointPicking()
{
    // Cancel entity selection if active
    if (m_selectingEntity) {
        m_selectingEntity = false;
        m_selectEntityButton->setText(tr("Select Entity..."));
        emit entitySelectionRequested(false);
    }

    if (m_pickState != PickState::Idle) {
        // Cancel current picking
        m_pickState = PickState::Idle;
        m_pickPointsButton->setText(tr("Pick Points"));
        emit calibrationModeRequested(false);
        return;
    }

    // Start picking first point
    resetPoints();
    m_pickState = PickState::PickingFirst;
    m_pickPointsButton->setText(tr("Cancel Picking"));
    m_instructionLabel->setText(tr(
        "<b>Click the FIRST point</b> on the background image.\n\n"
        "Choose a point at one end of a known dimension."));

    emit calibrationModeRequested(true);
}

void BackgroundCalibrationDialog::onSelectReferenceEntity()
{
    // Cancel point picking if active
    if (m_pickState != PickState::Idle) {
        m_pickState = PickState::Idle;
        m_pickPointsButton->setText(tr("Pick Points"));
        emit calibrationModeRequested(false);
    }

    if (m_selectingEntity) {
        // Cancel entity selection
        m_selectingEntity = false;
        m_selectEntityButton->setText(tr("Select Entity..."));
        emit entitySelectionRequested(false);
        return;
    }

    // Start entity selection
    m_selectingEntity = true;
    m_selectEntityButton->setText(tr("Cancel Selection"));
    m_instructionLabel->setText(tr(
        "<b>Click a LINE or CONSTRUCTION LINE</b> in the sketch.\n\n"
        "The background will be rotated to align with this entity."));

    emit entitySelectionRequested(true);
}

void BackgroundCalibrationDialog::onEntitySelected(int entityId, double angle)
{
    m_selectingEntity = false;
    m_selectEntityButton->setText(tr("Select Entity..."));
    emit entitySelectionRequested(false);

    m_hasReferenceEntity = true;
    m_referenceEntityId = entityId;
    m_referenceEntityAngle = angle;

    // Normalize angle for display using library function
    double displayAngle = sketch::normalizeAngle360(angle);

    m_selectedEntityLabel->setText(tr("Entity #%1 (angle: %2°)")
        .arg(entityId)
        .arg(displayAngle, 0, 'f', 1));
    m_selectedEntityLabel->setStyleSheet("");

    // Auto-select entity alignment mode
    m_alignToEntityRadio->setChecked(true);

    m_instructionLabel->setText(tr(
        "Reference entity selected! The background will be aligned to this entity's angle."));

    updatePreview();
}

void BackgroundCalibrationDialog::onPointPicked(const QPointF& point)
{
    if (m_pickState == PickState::PickingFirst) {
        m_point1 = point;
        m_hasPoint1 = true;
        m_pickState = PickState::PickingSecond;

        m_instructionLabel->setText(tr(
            "<b>Click the SECOND point</b> on the background image.\n\n"
            "Choose a point at the other end of the known dimension."));

        updatePointDisplay();

    } else if (m_pickState == PickState::PickingSecond) {
        m_point2 = point;
        m_hasPoint2 = true;
        m_pickState = PickState::Idle;
        m_pickPointsButton->setText(tr("Pick Points"));

        m_instructionLabel->setText(tr(
            "Points selected! Now enter the real-world distance "
            "between these points and click 'Apply Calibration'."));

        emit calibrationModeRequested(false);
        updatePointDisplay();
        updatePreview();
    }
}

void BackgroundCalibrationDialog::onApplyCalibration()
{
    if (!m_hasPoint1 || !m_hasPoint2) {
        return;
    }

    // Get distance in mm
    double distance = m_realDistanceSpinBox->value();
    double unitMultiplier = m_unitComboBox->currentData().toDouble();
    double distanceMm = distance * unitMultiplier;

    if (distanceMm <= 0) {
        return;
    }

    // Convert sketch points to image pixel coordinates
    QPointF imgPoint1 = sketch::sketchToImageCoords(m_background, m_point1);
    QPointF imgPoint2 = sketch::sketchToImageCoords(m_background, m_point2);

    // Calibrate the background (scale)
    m_background = sketch::calibrateBackground(m_background, imgPoint1, imgPoint2, distanceMm);

    // Apply rotation alignment if enabled
    if (m_enableAlignmentCheckBox->isChecked()) {
        // Calculate current angle of the reference line using library function
        double lineAngle = sketch::calculateLineAngle(m_point1, m_point2);

        // Get target angle
        double targetAngle = 0.0;
        if (m_alignToEntityRadio->isChecked() && m_hasReferenceEntity) {
            // Align to entity angle
            targetAngle = m_referenceEntityAngle;
        } else {
            // Align to axis/custom angle
            targetAngle = m_alignmentAxisComboBox->currentData().toDouble();
            if (targetAngle == -999.0) {
                targetAngle = m_customAngleSpinBox->value();
            }
        }

        // Calculate rotation needed using library function
        double rotationNeeded = sketch::calculateAlignmentRotation(lineAngle, targetAngle);

        // Apply rotation (using setRotation which normalizes)
        m_background.setRotation(m_background.rotation + rotationNeeded);
    }

    m_calibrated = true;
    accept();
}

void BackgroundCalibrationDialog::updatePreview()
{
    // Update alignment UI state
    bool alignEnabled = m_enableAlignmentCheckBox->isChecked();
    bool alignToAxis = m_alignToAxisRadio->isChecked();
    bool alignToEntity = m_alignToEntityRadio->isChecked();

    // Enable/disable controls based on alignment mode
    m_alignToAxisRadio->setEnabled(alignEnabled);
    m_alignToEntityRadio->setEnabled(alignEnabled);
    m_alignmentAxisComboBox->setEnabled(alignEnabled && alignToAxis);
    m_selectEntityButton->setEnabled(alignEnabled && alignToEntity);

    // Show/hide custom angle spinbox based on selection
    bool isCustom = (m_alignmentAxisComboBox->currentData().toDouble() == -999.0);
    m_customAngleSpinBox->setVisible(alignEnabled && alignToAxis && isCustom);
    m_customAngleSpinBox->setEnabled(alignEnabled && alignToAxis && isCustom);

    if (!m_hasPoint1 || !m_hasPoint2) {
        m_previewLabel->setText(tr("Select two points and enter a distance to see the calibration preview."));
        m_currentAngleLabel->setText(tr("--"));
        m_rotationNeededLabel->setText(tr("--"));
        m_applyButton->setEnabled(false);
        return;
    }

    // Calculate current distance between points (in sketch coords = mm)
    double dx = m_point2.x() - m_point1.x();
    double dy = m_point2.y() - m_point1.y();
    double currentDistance = qSqrt(dx * dx + dy * dy);

    // Calculate angle of the line between points using library function
    double lineAngle = sketch::calculateLineAngle(m_point1, m_point2);

    // Normalize to 0-360 range for display using library function
    double displayAngle = sketch::normalizeAngle360(lineAngle);

    m_currentAngleLabel->setText(tr("%1°").arg(displayAngle, 0, 'f', 1));

    // Calculate rotation needed for alignment
    double targetAngle = 0.0;
    if (m_alignToEntityRadio->isChecked() && m_hasReferenceEntity) {
        // Align to entity angle
        targetAngle = m_referenceEntityAngle;
    } else {
        // Align to axis/custom angle
        targetAngle = m_alignmentAxisComboBox->currentData().toDouble();
        if (targetAngle == -999.0) {
            // Custom angle
            targetAngle = m_customAngleSpinBox->value();
        }
    }

    // Calculate rotation needed using library function (returns -180 to +180 for shortest path)
    double rotationNeeded = sketch::calculateAlignmentRotation(lineAngle, targetAngle);

    if (m_enableAlignmentCheckBox->isChecked()) {
        QString targetDesc;
        if (m_alignToEntityRadio->isChecked() && m_hasReferenceEntity) {
            targetDesc = tr(" (to entity #%1)").arg(m_referenceEntityId);
        }
        m_rotationNeededLabel->setText(tr("%1°%2").arg(rotationNeeded, 0, 'f', 1).arg(targetDesc));
    } else {
        m_rotationNeededLabel->setText(tr("-- (disabled)"));
    }

    // Get desired distance in mm
    double desiredDistance = m_realDistanceSpinBox->value();
    double unitMultiplier = m_unitComboBox->currentData().toDouble();
    double desiredMm = desiredDistance * unitMultiplier;

    if (desiredMm <= 0 || currentDistance <= 0) {
        m_previewLabel->setText(tr("Invalid distance values."));
        m_applyButton->setEnabled(false);
        return;
    }

    // Calculate new size
    double scaleFactor = desiredMm / currentDistance;
    double newWidth = m_background.width * scaleFactor;
    double newHeight = m_background.height * scaleFactor;

    // Build preview text
    QString previewText = tr(
        "<b>Scale Calibration:</b><br>"
        "Current size: %1 x %2 mm<br>"
        "New size: %3 x %4 mm<br>"
        "Scale factor: %5")
        .arg(m_background.width, 0, 'f', 2)
        .arg(m_background.height, 0, 'f', 2)
        .arg(newWidth, 0, 'f', 2)
        .arg(newHeight, 0, 'f', 2)
        .arg(scaleFactor, 0, 'f', 4);

    if (m_enableAlignmentCheckBox->isChecked()) {
        // Calculate new rotation using library function for normalization
        double currentRotation = m_background.rotation;
        double newRotation = sketch::normalizeAngle360(currentRotation + rotationNeeded);

        previewText += tr(
            "<br><br><b>Rotation Alignment:</b><br>"
            "Current rotation: %1°<br>"
            "New rotation: %2°")
            .arg(currentRotation, 0, 'f', 1)
            .arg(newRotation, 0, 'f', 1);
    }

    m_previewLabel->setText(previewText);
    m_applyButton->setEnabled(true);
}

void BackgroundCalibrationDialog::updatePointDisplay()
{
    if (m_hasPoint1) {
        m_point1Label->setText(tr("(%.2f, %.2f) mm").arg(m_point1.x()).arg(m_point1.y()));
        m_point1Label->setStyleSheet("");
    } else {
        m_point1Label->setText(tr("Not set"));
        m_point1Label->setStyleSheet("QLabel { color: #666; font-style: italic; }");
    }

    if (m_hasPoint2) {
        m_point2Label->setText(tr("(%.2f, %.2f) mm").arg(m_point2.x()).arg(m_point2.y()));
        m_point2Label->setStyleSheet("");
    } else {
        m_point2Label->setText(tr("Not set"));
        m_point2Label->setStyleSheet("QLabel { color: #666; font-style: italic; }");
    }

    // Show measured distance
    if (m_hasPoint1 && m_hasPoint2) {
        double dx = m_point2.x() - m_point1.x();
        double dy = m_point2.y() - m_point1.y();
        double distance = qSqrt(dx * dx + dy * dy);
        m_measuredDistanceLabel->setText(tr("%.2f mm").arg(distance));
    } else {
        m_measuredDistanceLabel->setText(tr("--"));
    }
}

void BackgroundCalibrationDialog::resetPoints()
{
    m_hasPoint1 = false;
    m_hasPoint2 = false;
    m_point1 = QPointF();
    m_point2 = QPointF();

    if (m_pickState != PickState::Idle) {
        m_pickState = PickState::Idle;
        m_pickPointsButton->setText(tr("Pick Points"));
        emit calibrationModeRequested(false);
    }

    if (m_selectingEntity) {
        m_selectingEntity = false;
        m_selectEntityButton->setText(tr("Select Entity..."));
        emit entitySelectionRequested(false);
    }

    // Reset entity reference (but don't clear it - user might want to keep it)
    // m_hasReferenceEntity = false;
    // m_referenceEntityId = -1;

    m_instructionLabel->setText(tr(
        "Calibrate the background image by selecting two reference points.\n\n"
        "1. Click 'Pick Points' to start\n"
        "2. Click two points on a known dimension\n"
        "3. Enter the real-world distance\n"
        "4. Optionally align to an axis or entity\n"
        "5. Click 'Apply' to calibrate"));

    updatePointDisplay();
    updatePreview();
}

}  // namespace hobbycad
