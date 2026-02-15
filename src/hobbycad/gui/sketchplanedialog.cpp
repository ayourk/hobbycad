// =====================================================================
//  src/hobbycad/gui/sketchplanedialog.cpp — Sketch plane selection dialog
// =====================================================================

#include "sketchplanedialog.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

namespace hobbycad {

SketchPlaneDialog::SketchPlaneDialog(QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("SketchPlaneDialog"));
    setWindowTitle(tr("Select Sketch Plane"));
    setMinimumWidth(380);
    setupUi();
}

void SketchPlaneDialog::setupUi()
{
    auto* layout = new QVBoxLayout(this);

    // Plane selection group
    auto* planeGroup = new QGroupBox(tr("Sketch Plane"));
    auto* planeLayout = new QVBoxLayout(planeGroup);

    m_planeGroup = new QButtonGroup(this);

    m_xyButton = new QRadioButton(tr("XY Plane (Top/Bottom)"));
    m_xzButton = new QRadioButton(tr("XZ Plane (Front/Back)"));
    m_yzButton = new QRadioButton(tr("YZ Plane (Left/Right)"));
    m_customButton = new QRadioButton(tr("Angled Plane"));
    m_constructionButton = new QRadioButton(tr("Construction Plane"));

    // Use IDs 0-3 for standard planes, 100 for construction plane
    m_planeGroup->addButton(m_xyButton, static_cast<int>(SketchPlane::XY));
    m_planeGroup->addButton(m_xzButton, static_cast<int>(SketchPlane::XZ));
    m_planeGroup->addButton(m_yzButton, static_cast<int>(SketchPlane::YZ));
    m_planeGroup->addButton(m_customButton, static_cast<int>(SketchPlane::Custom));
    m_planeGroup->addButton(m_constructionButton, 100);  // Special ID for construction plane

    planeLayout->addWidget(m_xyButton);
    planeLayout->addWidget(m_xzButton);
    planeLayout->addWidget(m_yzButton);
    planeLayout->addWidget(m_customButton);
    planeLayout->addWidget(m_constructionButton);

    // Default to XY plane
    m_xyButton->setChecked(true);

    layout->addWidget(planeGroup);

    // Angle settings group (for custom plane)
    m_angleGroup = new QGroupBox(tr("Plane Angle"));
    auto* angleLayout = new QFormLayout(m_angleGroup);

    m_axisCombo = new QComboBox;
    m_axisCombo->addItem(tr("X Axis"), static_cast<int>(PlaneRotationAxis::X));
    m_axisCombo->addItem(tr("Y Axis"), static_cast<int>(PlaneRotationAxis::Y));
    m_axisCombo->addItem(tr("Z Axis"), static_cast<int>(PlaneRotationAxis::Z));
    m_axisCombo->setToolTip(tr("Axis to rotate the plane around"));

    m_angleSpin = new QDoubleSpinBox;
    m_angleSpin->setRange(-180.0, 180.0);
    m_angleSpin->setDecimals(2);
    m_angleSpin->setValue(45.0);
    m_angleSpin->setSuffix(tr("°"));
    m_angleSpin->setToolTip(tr("Rotation angle in degrees"));

    angleLayout->addRow(tr("Rotate around:"), m_axisCombo);
    angleLayout->addRow(tr("Angle:"), m_angleSpin);

    layout->addWidget(m_angleGroup);

    // Construction plane selection group
    m_constructionGroup = new QGroupBox(tr("Construction Plane"));
    auto* constructionLayout = new QFormLayout(m_constructionGroup);

    m_constructionPlaneCombo = new QComboBox;
    m_constructionPlaneCombo->setToolTip(tr("Select an existing construction plane"));
    constructionLayout->addRow(tr("Plane:"), m_constructionPlaneCombo);

    layout->addWidget(m_constructionGroup);

    // Offset group
    auto* offsetGroup = new QGroupBox(tr("Offset from Origin"));
    auto* offsetLayout = new QFormLayout(offsetGroup);

    m_offsetSpin = new QDoubleSpinBox;
    m_offsetSpin->setRange(-10000.0, 10000.0);
    m_offsetSpin->setDecimals(3);
    m_offsetSpin->setValue(0.0);
    m_offsetSpin->setSuffix(tr(" mm"));
    m_offsetSpin->setToolTip(tr("Distance from origin along the plane's normal axis"));

    offsetLayout->addRow(tr("Distance:"), m_offsetSpin);

    layout->addWidget(offsetGroup);

    // Preview label showing the resulting plane equation
    m_previewLabel = new QLabel;
    m_previewLabel->setStyleSheet(
        QStringLiteral("QLabel { color: #666; font-style: italic; padding: 8px; }"));
    m_previewLabel->setAlignment(Qt::AlignCenter);
    m_previewLabel->setWordWrap(true);
    layout->addWidget(m_previewLabel);

    // Update preview and visibility when selection changes
    connect(m_planeGroup, &QButtonGroup::idToggled,
            this, [this](int, bool) {
                updateAngleWidgetsVisibility();
                updateConstructionPlaneVisibility();
                updatePreviewText();
            });
    connect(m_offsetSpin, &QDoubleSpinBox::valueChanged,
            this, [this](double) { updatePreviewText(); });
    connect(m_axisCombo, &QComboBox::currentIndexChanged,
            this, [this](int) { updatePreviewText(); });
    connect(m_angleSpin, &QDoubleSpinBox::valueChanged,
            this, [this](double) { updatePreviewText(); });
    connect(m_constructionPlaneCombo, &QComboBox::currentIndexChanged,
            this, [this](int) { updatePreviewText(); });

    updateAngleWidgetsVisibility();
    updateConstructionPlaneVisibility();
    updatePreviewText();

    // Dialog buttons
    auto* buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    layout->addWidget(buttonBox);

    // Set focus to OK button
    buttonBox->button(QDialogButtonBox::Ok)->setDefault(true);
    buttonBox->button(QDialogButtonBox::Ok)->setFocus();
}

void SketchPlaneDialog::updateAngleWidgetsVisibility()
{
    bool isCustom = (selectedPlane() == SketchPlane::Custom) &&
                    (m_planeGroup->checkedId() != 100);  // Not construction plane
    m_angleGroup->setVisible(isCustom);
    adjustSize();
}

void SketchPlaneDialog::updateConstructionPlaneVisibility()
{
    bool isConstruction = (m_planeGroup->checkedId() == 100);
    m_constructionGroup->setVisible(isConstruction);

    // Hide construction plane option if no planes available
    m_constructionButton->setVisible(!m_availablePlanes.isEmpty());
    if (m_availablePlanes.isEmpty() && isConstruction) {
        m_xyButton->setChecked(true);
    }

    adjustSize();
}

void SketchPlaneDialog::updatePreviewText()
{
    double off = m_offsetSpin->value();
    QString text;

    // Check if construction plane is selected
    if (m_planeGroup->checkedId() == 100) {
        int idx = m_constructionPlaneCombo->currentIndex();
        if (idx >= 0 && idx < m_availablePlanes.size()) {
            QString planeName = m_availablePlanes[idx].name;
            if (qFuzzyIsNull(off)) {
                text = tr("Sketch on construction plane \"%1\"").arg(planeName);
            } else {
                text = tr("Sketch on construction plane \"%1\", offset %2 mm")
                           .arg(planeName)
                           .arg(off, 0, 'g', 6);
            }
        } else {
            text = tr("No construction plane selected");
        }
        m_previewLabel->setText(text);
        return;
    }

    switch (selectedPlane()) {
    case SketchPlane::XY:
        if (qFuzzyIsNull(off)) {
            text = tr("Sketch on XY plane at Z = 0");
        } else {
            text = tr("Sketch on XY plane at Z = %1").arg(off, 0, 'g', 6);
        }
        break;
    case SketchPlane::XZ:
        if (qFuzzyIsNull(off)) {
            text = tr("Sketch on XZ plane at Y = 0");
        } else {
            text = tr("Sketch on XZ plane at Y = %1").arg(off, 0, 'g', 6);
        }
        break;
    case SketchPlane::YZ:
        if (qFuzzyIsNull(off)) {
            text = tr("Sketch on YZ plane at X = 0");
        } else {
            text = tr("Sketch on YZ plane at X = %1").arg(off, 0, 'g', 6);
        }
        break;
    case SketchPlane::Custom: {
        QString axisName;
        switch (rotationAxis()) {
        case PlaneRotationAxis::X: axisName = QStringLiteral("X"); break;
        case PlaneRotationAxis::Y: axisName = QStringLiteral("Y"); break;
        case PlaneRotationAxis::Z: axisName = QStringLiteral("Z"); break;
        }
        double angle = rotationAngle();
        if (qFuzzyIsNull(off)) {
            text = tr("Sketch on plane rotated %1° around %2 axis")
                       .arg(angle, 0, 'g', 4)
                       .arg(axisName);
        } else {
            text = tr("Sketch on plane rotated %1° around %2 axis, offset %3 mm")
                       .arg(angle, 0, 'g', 4)
                       .arg(axisName)
                       .arg(off, 0, 'g', 6);
        }
        break;
    }
    default:
        text = tr("Custom plane");
        break;
    }

    m_previewLabel->setText(text);
}

SketchPlane SketchPlaneDialog::selectedPlane() const
{
    int id = m_planeGroup->checkedId();
    if (id < 0) return SketchPlane::XY;
    return static_cast<SketchPlane>(id);
}

double SketchPlaneDialog::offset() const
{
    return m_offsetSpin->value();
}

PlaneRotationAxis SketchPlaneDialog::rotationAxis() const
{
    int idx = m_axisCombo->currentIndex();
    return static_cast<PlaneRotationAxis>(m_axisCombo->itemData(idx).toInt());
}

double SketchPlaneDialog::rotationAngle() const
{
    return m_angleSpin->value();
}

void SketchPlaneDialog::setSelectedPlane(SketchPlane plane)
{
    switch (plane) {
    case SketchPlane::XY:
        m_xyButton->setChecked(true);
        break;
    case SketchPlane::XZ:
        m_xzButton->setChecked(true);
        break;
    case SketchPlane::YZ:
        m_yzButton->setChecked(true);
        break;
    case SketchPlane::Custom:
        m_customButton->setChecked(true);
        break;
    default:
        m_xyButton->setChecked(true);
        break;
    }
    updateAngleWidgetsVisibility();
    updatePreviewText();
}

void SketchPlaneDialog::setOffset(double offset)
{
    m_offsetSpin->setValue(offset);
}

int SketchPlaneDialog::constructionPlaneId() const
{
    if (m_planeGroup->checkedId() != 100) {
        return -1;  // Not using construction plane
    }

    int idx = m_constructionPlaneCombo->currentIndex();
    if (idx >= 0 && idx < m_availablePlanes.size()) {
        return m_availablePlanes[idx].id;
    }
    return -1;
}

void SketchPlaneDialog::setAvailableConstructionPlanes(const QVector<ConstructionPlaneData>& planes)
{
    m_availablePlanes = planes;

    m_constructionPlaneCombo->clear();
    for (const auto& plane : planes) {
        m_constructionPlaneCombo->addItem(plane.name, plane.id);
    }

    // Show/hide construction plane option based on availability
    m_constructionButton->setVisible(!planes.isEmpty());
    if (planes.isEmpty() && m_planeGroup->checkedId() == 100) {
        m_xyButton->setChecked(true);
    }

    updateConstructionPlaneVisibility();
}

}  // namespace hobbycad
