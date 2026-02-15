// =====================================================================
//  src/hobbycad/gui/constructionplanedialog.cpp — Construction plane dialog
// =====================================================================

#include "constructionplanedialog.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace hobbycad {

ConstructionPlaneDialog::ConstructionPlaneDialog(QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("ConstructionPlaneDialog"));
    setWindowTitle(tr("Construction Plane"));
    setMinimumWidth(420);
    setupUi();
}

void ConstructionPlaneDialog::setupUi()
{
    auto* layout = new QVBoxLayout(this);

    // Name field
    auto* nameLayout = new QHBoxLayout;
    nameLayout->addWidget(new QLabel(tr("Name:")));
    m_nameEdit = new QLineEdit;
    m_nameEdit->setPlaceholderText(tr("Plane 1"));
    nameLayout->addWidget(m_nameEdit);
    layout->addLayout(nameLayout);

    // Type selection group
    auto* typeGroup = new QGroupBox(tr("Plane Type"));
    auto* typeLayout = new QVBoxLayout(typeGroup);

    m_typeGroup = new QButtonGroup(this);

    m_offsetFromOriginButton = new QRadioButton(tr("Offset from Origin Plane"));
    m_offsetFromOriginButton->setToolTip(tr("Create a plane parallel to XY, XZ, or YZ origin plane"));
    m_offsetFromPlaneButton = new QRadioButton(tr("Offset from Construction Plane"));
    m_offsetFromPlaneButton->setToolTip(tr("Create a plane parallel to another construction plane"));
    m_angledButton = new QRadioButton(tr("Angled Plane"));
    m_angledButton->setToolTip(tr("Create a plane rotated around one or two axes"));

    m_typeGroup->addButton(m_offsetFromOriginButton, static_cast<int>(ConstructionPlaneType::OffsetFromOrigin));
    m_typeGroup->addButton(m_offsetFromPlaneButton, static_cast<int>(ConstructionPlaneType::OffsetFromPlane));
    m_typeGroup->addButton(m_angledButton, static_cast<int>(ConstructionPlaneType::Angled));

    typeLayout->addWidget(m_offsetFromOriginButton);
    typeLayout->addWidget(m_offsetFromPlaneButton);
    typeLayout->addWidget(m_angledButton);

    // Default to offset from origin
    m_offsetFromOriginButton->setChecked(true);

    layout->addWidget(typeGroup);

    // Options stack - different widgets for each type
    m_optionsStack = new QStackedWidget;

    setupOffsetFromOriginWidgets();
    setupOffsetFromPlaneWidgets();
    setupAngledWidgets();

    m_optionsStack->addWidget(m_offsetOriginPage);
    m_optionsStack->addWidget(m_offsetPlanePage);
    m_optionsStack->addWidget(m_angledPage);

    layout->addWidget(m_optionsStack);

    // Origin point group (plane center in absolute coordinates)
    auto* originGroup = new QGroupBox(tr("Plane Center (Absolute Coordinates)"));
    auto* originLayout = new QHBoxLayout(originGroup);

    originLayout->addWidget(new QLabel(tr("X:")));
    m_originXSpin = new QDoubleSpinBox;
    m_originXSpin->setRange(-100000.0, 100000.0);
    m_originXSpin->setDecimals(3);
    m_originXSpin->setValue(0.0);
    m_originXSpin->setSuffix(tr(" mm"));
    m_originXSpin->setToolTip(tr("X coordinate of plane center in absolute space"));
    originLayout->addWidget(m_originXSpin);

    originLayout->addWidget(new QLabel(tr("Y:")));
    m_originYSpin = new QDoubleSpinBox;
    m_originYSpin->setRange(-100000.0, 100000.0);
    m_originYSpin->setDecimals(3);
    m_originYSpin->setValue(0.0);
    m_originYSpin->setSuffix(tr(" mm"));
    m_originYSpin->setToolTip(tr("Y coordinate of plane center in absolute space"));
    originLayout->addWidget(m_originYSpin);

    originLayout->addWidget(new QLabel(tr("Z:")));
    m_originZSpin = new QDoubleSpinBox;
    m_originZSpin->setRange(-100000.0, 100000.0);
    m_originZSpin->setDecimals(3);
    m_originZSpin->setValue(0.0);
    m_originZSpin->setSuffix(tr(" mm"));
    m_originZSpin->setToolTip(tr("Z coordinate of plane center in absolute space"));
    originLayout->addWidget(m_originZSpin);

    layout->addWidget(originGroup);

    // Visibility checkbox
    m_visibleCheck = new QCheckBox(tr("Visible in 3D view"));
    m_visibleCheck->setChecked(true);
    layout->addWidget(m_visibleCheck);

    // Preview label
    m_previewLabel = new QLabel;
    m_previewLabel->setStyleSheet(
        QStringLiteral("QLabel { color: #666; font-style: italic; padding: 8px; "
                       "background: #f5f5f5; border-radius: 4px; }"));
    m_previewLabel->setAlignment(Qt::AlignCenter);
    m_previewLabel->setWordWrap(true);
    layout->addWidget(m_previewLabel);

    // Connections for type change
    connect(m_typeGroup, &QButtonGroup::idToggled,
            this, [this](int, bool) {
                updateVisibility();
                updatePreviewText();
            });

    // Connections for value changes
    connect(m_basePlaneCombo, &QComboBox::currentIndexChanged,
            this, &ConstructionPlaneDialog::updatePreviewText);
    connect(m_originOffsetSpin, &QDoubleSpinBox::valueChanged,
            this, &ConstructionPlaneDialog::updatePreviewText);
    connect(m_referencePlaneCombo, &QComboBox::currentIndexChanged,
            this, &ConstructionPlaneDialog::updatePreviewText);
    connect(m_planeOffsetSpin, &QDoubleSpinBox::valueChanged,
            this, &ConstructionPlaneDialog::updatePreviewText);
    connect(m_primaryAxisCombo, &QComboBox::currentIndexChanged,
            this, &ConstructionPlaneDialog::updatePreviewText);
    connect(m_primaryAngleSpin, &QDoubleSpinBox::valueChanged,
            this, &ConstructionPlaneDialog::updatePreviewText);
    connect(m_secondaryAxisCombo, &QComboBox::currentIndexChanged,
            this, &ConstructionPlaneDialog::updatePreviewText);
    connect(m_secondaryAngleSpin, &QDoubleSpinBox::valueChanged,
            this, &ConstructionPlaneDialog::updatePreviewText);
    connect(m_angledOffsetSpin, &QDoubleSpinBox::valueChanged,
            this, &ConstructionPlaneDialog::updatePreviewText);
    connect(m_originRollSpin, &QDoubleSpinBox::valueChanged,
            this, &ConstructionPlaneDialog::updatePreviewText);
    connect(m_planeRollSpin, &QDoubleSpinBox::valueChanged,
            this, &ConstructionPlaneDialog::updatePreviewText);
    connect(m_angledRollSpin, &QDoubleSpinBox::valueChanged,
            this, &ConstructionPlaneDialog::updatePreviewText);
    connect(m_originXSpin, &QDoubleSpinBox::valueChanged,
            this, &ConstructionPlaneDialog::updatePreviewText);
    connect(m_originYSpin, &QDoubleSpinBox::valueChanged,
            this, &ConstructionPlaneDialog::updatePreviewText);
    connect(m_originZSpin, &QDoubleSpinBox::valueChanged,
            this, &ConstructionPlaneDialog::updatePreviewText);

    updateVisibility();
    updatePreviewText();

    // Dialog buttons
    auto* buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttonBox);

    buttonBox->button(QDialogButtonBox::Ok)->setDefault(true);
}

void ConstructionPlaneDialog::setupOffsetFromOriginWidgets()
{
    m_offsetOriginPage = new QWidget;
    auto* layout = new QFormLayout(m_offsetOriginPage);
    layout->setContentsMargins(0, 0, 0, 0);

    m_basePlaneCombo = new QComboBox;
    m_basePlaneCombo->addItem(tr("XY Plane (Top/Bottom)"), static_cast<int>(SketchPlane::XY));
    m_basePlaneCombo->addItem(tr("XZ Plane (Front/Back)"), static_cast<int>(SketchPlane::XZ));
    m_basePlaneCombo->addItem(tr("YZ Plane (Left/Right)"), static_cast<int>(SketchPlane::YZ));
    layout->addRow(tr("Base plane:"), m_basePlaneCombo);

    m_originOffsetSpin = new QDoubleSpinBox;
    m_originOffsetSpin->setRange(-10000.0, 10000.0);
    m_originOffsetSpin->setDecimals(3);
    m_originOffsetSpin->setValue(0.0);
    m_originOffsetSpin->setSuffix(tr(" mm"));
    m_originOffsetSpin->setToolTip(tr("Distance from origin along the plane's normal"));
    layout->addRow(tr("Offset:"), m_originOffsetSpin);

    m_originRollSpin = new QDoubleSpinBox;
    m_originRollSpin->setRange(-180.0, 180.0);
    m_originRollSpin->setDecimals(2);
    m_originRollSpin->setValue(0.0);
    m_originRollSpin->setSuffix(tr("°"));
    m_originRollSpin->setToolTip(tr("Rotation around the plane's normal (affects sketch X/Y orientation)"));
    layout->addRow(tr("Roll:"), m_originRollSpin);
}

void ConstructionPlaneDialog::setupOffsetFromPlaneWidgets()
{
    m_offsetPlanePage = new QWidget;
    auto* layout = new QFormLayout(m_offsetPlanePage);
    layout->setContentsMargins(0, 0, 0, 0);

    m_referencePlaneCombo = new QComboBox;
    m_referencePlaneCombo->setToolTip(tr("Construction plane to offset from"));
    layout->addRow(tr("Reference plane:"), m_referencePlaneCombo);

    m_planeOffsetSpin = new QDoubleSpinBox;
    m_planeOffsetSpin->setRange(-10000.0, 10000.0);
    m_planeOffsetSpin->setDecimals(3);
    m_planeOffsetSpin->setValue(10.0);
    m_planeOffsetSpin->setSuffix(tr(" mm"));
    m_planeOffsetSpin->setToolTip(tr("Distance from reference plane along its normal"));
    layout->addRow(tr("Offset:"), m_planeOffsetSpin);

    m_planeRollSpin = new QDoubleSpinBox;
    m_planeRollSpin->setRange(-180.0, 180.0);
    m_planeRollSpin->setDecimals(2);
    m_planeRollSpin->setValue(0.0);
    m_planeRollSpin->setSuffix(tr("°"));
    m_planeRollSpin->setToolTip(tr("Rotation around the plane's normal (affects sketch X/Y orientation)"));
    layout->addRow(tr("Roll:"), m_planeRollSpin);
}

void ConstructionPlaneDialog::setupAngledWidgets()
{
    m_angledPage = new QWidget;
    auto* layout = new QFormLayout(m_angledPage);
    layout->setContentsMargins(0, 0, 0, 0);

    // Primary rotation
    auto* primaryLabel = new QLabel(tr("<b>Primary Rotation</b>"));
    layout->addRow(primaryLabel);

    m_primaryAxisCombo = new QComboBox;
    m_primaryAxisCombo->addItem(tr("X Axis"), static_cast<int>(PlaneRotationAxis::X));
    m_primaryAxisCombo->addItem(tr("Y Axis"), static_cast<int>(PlaneRotationAxis::Y));
    m_primaryAxisCombo->addItem(tr("Z Axis"), static_cast<int>(PlaneRotationAxis::Z));
    m_primaryAxisCombo->setToolTip(tr("First axis to rotate around"));
    layout->addRow(tr("Rotate around:"), m_primaryAxisCombo);

    m_primaryAngleSpin = new QDoubleSpinBox;
    m_primaryAngleSpin->setRange(-180.0, 180.0);
    m_primaryAngleSpin->setDecimals(2);
    m_primaryAngleSpin->setValue(45.0);
    m_primaryAngleSpin->setSuffix(tr("°"));
    m_primaryAngleSpin->setToolTip(tr("Primary rotation angle in degrees"));
    layout->addRow(tr("Angle:"), m_primaryAngleSpin);

    // Secondary rotation
    auto* secondaryLabel = new QLabel(tr("<b>Secondary Rotation</b> (optional)"));
    layout->addRow(secondaryLabel);

    m_secondaryAxisCombo = new QComboBox;
    m_secondaryAxisCombo->addItem(tr("Y Axis"), static_cast<int>(PlaneRotationAxis::Y));
    m_secondaryAxisCombo->addItem(tr("X Axis"), static_cast<int>(PlaneRotationAxis::X));
    m_secondaryAxisCombo->addItem(tr("Z Axis"), static_cast<int>(PlaneRotationAxis::Z));
    m_secondaryAxisCombo->setToolTip(tr("Second axis to rotate around (after primary rotation)"));
    layout->addRow(tr("Rotate around:"), m_secondaryAxisCombo);

    m_secondaryAngleSpin = new QDoubleSpinBox;
    m_secondaryAngleSpin->setRange(-180.0, 180.0);
    m_secondaryAngleSpin->setDecimals(2);
    m_secondaryAngleSpin->setValue(0.0);
    m_secondaryAngleSpin->setSuffix(tr("°"));
    m_secondaryAngleSpin->setToolTip(tr("Secondary rotation angle (0 = no secondary rotation)"));
    layout->addRow(tr("Angle:"), m_secondaryAngleSpin);

    // Offset after rotation
    m_angledOffsetSpin = new QDoubleSpinBox;
    m_angledOffsetSpin->setRange(-10000.0, 10000.0);
    m_angledOffsetSpin->setDecimals(3);
    m_angledOffsetSpin->setValue(0.0);
    m_angledOffsetSpin->setSuffix(tr(" mm"));
    m_angledOffsetSpin->setToolTip(tr("Offset along rotated plane's normal"));
    layout->addRow(tr("Offset:"), m_angledOffsetSpin);

    // Roll (rotation around normal)
    m_angledRollSpin = new QDoubleSpinBox;
    m_angledRollSpin->setRange(-180.0, 180.0);
    m_angledRollSpin->setDecimals(2);
    m_angledRollSpin->setValue(0.0);
    m_angledRollSpin->setSuffix(tr("°"));
    m_angledRollSpin->setToolTip(tr("Rotation around the plane's normal (affects sketch X/Y orientation)"));
    layout->addRow(tr("Roll:"), m_angledRollSpin);
}

void ConstructionPlaneDialog::updateVisibility()
{
    int typeId = m_typeGroup->checkedId();
    auto type = static_cast<ConstructionPlaneType>(typeId);

    switch (type) {
    case ConstructionPlaneType::OffsetFromOrigin:
        m_optionsStack->setCurrentWidget(m_offsetOriginPage);
        break;
    case ConstructionPlaneType::OffsetFromPlane:
        m_optionsStack->setCurrentWidget(m_offsetPlanePage);
        // Disable if no planes available
        m_offsetFromPlaneButton->setEnabled(!m_availablePlanes.isEmpty());
        if (m_availablePlanes.isEmpty() && type == ConstructionPlaneType::OffsetFromPlane) {
            m_offsetFromOriginButton->setChecked(true);
        }
        break;
    case ConstructionPlaneType::Angled:
        m_optionsStack->setCurrentWidget(m_angledPage);
        break;
    }

    adjustSize();
}

void ConstructionPlaneDialog::updatePreviewText()
{
    QString text;
    int typeId = m_typeGroup->checkedId();
    auto type = static_cast<ConstructionPlaneType>(typeId);

    switch (type) {
    case ConstructionPlaneType::OffsetFromOrigin: {
        auto basePlane = static_cast<SketchPlane>(m_basePlaneCombo->currentData().toInt());
        double offset = m_originOffsetSpin->value();

        QString planeName;
        QString axisName;
        switch (basePlane) {
        case SketchPlane::XY: planeName = QStringLiteral("XY"); axisName = QStringLiteral("Z"); break;
        case SketchPlane::XZ: planeName = QStringLiteral("XZ"); axisName = QStringLiteral("Y"); break;
        case SketchPlane::YZ: planeName = QStringLiteral("YZ"); axisName = QStringLiteral("X"); break;
        default: planeName = QStringLiteral("XY"); axisName = QStringLiteral("Z"); break;
        }

        if (qFuzzyIsNull(offset)) {
            text = tr("Plane parallel to %1 origin plane at %2 = 0")
                       .arg(planeName, axisName);
        } else {
            text = tr("Plane parallel to %1 origin plane at %2 = %3 mm")
                       .arg(planeName, axisName)
                       .arg(offset, 0, 'g', 6);
        }
        break;
    }

    case ConstructionPlaneType::OffsetFromPlane: {
        int idx = m_referencePlaneCombo->currentIndex();
        double offset = m_planeOffsetSpin->value();

        if (idx >= 0 && idx < m_availablePlanes.size()) {
            QString refName = m_availablePlanes[idx].name;
            text = tr("Plane offset %1 mm from \"%2\"")
                       .arg(offset, 0, 'g', 6)
                       .arg(refName);
        } else {
            text = tr("No reference plane available");
        }
        break;
    }

    case ConstructionPlaneType::Angled: {
        auto primaryAxis = static_cast<PlaneRotationAxis>(m_primaryAxisCombo->currentData().toInt());
        double primaryAngle = m_primaryAngleSpin->value();
        auto secondaryAxis = static_cast<PlaneRotationAxis>(m_secondaryAxisCombo->currentData().toInt());
        double secondaryAngle = m_secondaryAngleSpin->value();
        double offset = m_angledOffsetSpin->value();

        QString primaryAxisName;
        switch (primaryAxis) {
        case PlaneRotationAxis::X: primaryAxisName = QStringLiteral("X"); break;
        case PlaneRotationAxis::Y: primaryAxisName = QStringLiteral("Y"); break;
        case PlaneRotationAxis::Z: primaryAxisName = QStringLiteral("Z"); break;
        }

        QString secondaryAxisName;
        switch (secondaryAxis) {
        case PlaneRotationAxis::X: secondaryAxisName = QStringLiteral("X"); break;
        case PlaneRotationAxis::Y: secondaryAxisName = QStringLiteral("Y"); break;
        case PlaneRotationAxis::Z: secondaryAxisName = QStringLiteral("Z"); break;
        }

        if (qFuzzyIsNull(secondaryAngle)) {
            // Single axis rotation
            if (qFuzzyIsNull(offset)) {
                text = tr("Plane rotated %1° around %2 axis")
                           .arg(primaryAngle, 0, 'g', 4)
                           .arg(primaryAxisName);
            } else {
                text = tr("Plane rotated %1° around %2 axis, offset %3 mm")
                           .arg(primaryAngle, 0, 'g', 4)
                           .arg(primaryAxisName)
                           .arg(offset, 0, 'g', 6);
            }
        } else {
            // Two axis rotation
            if (qFuzzyIsNull(offset)) {
                text = tr("Plane rotated %1° around %2, then %3° around %4")
                           .arg(primaryAngle, 0, 'g', 4)
                           .arg(primaryAxisName)
                           .arg(secondaryAngle, 0, 'g', 4)
                           .arg(secondaryAxisName);
            } else {
                text = tr("Plane rotated %1° around %2, %3° around %4, offset %5 mm")
                           .arg(primaryAngle, 0, 'g', 4)
                           .arg(primaryAxisName)
                           .arg(secondaryAngle, 0, 'g', 4)
                           .arg(secondaryAxisName)
                           .arg(offset, 0, 'g', 6);
            }
        }
        break;
    }
    }

    m_previewLabel->setText(text);
}

void ConstructionPlaneDialog::setPlaneData(const ConstructionPlaneData& data)
{
    m_planeId = data.id;
    m_nameEdit->setText(data.name);

    // Set type
    switch (data.type) {
    case ConstructionPlaneType::OffsetFromOrigin:
        m_offsetFromOriginButton->setChecked(true);
        m_basePlaneCombo->setCurrentIndex(
            m_basePlaneCombo->findData(static_cast<int>(data.basePlane)));
        m_originOffsetSpin->setValue(data.offset);
        m_originRollSpin->setValue(data.rollAngle);
        break;

    case ConstructionPlaneType::OffsetFromPlane:
        m_offsetFromPlaneButton->setChecked(true);
        // Find reference plane in combo
        for (int i = 0; i < m_availablePlanes.size(); ++i) {
            if (m_availablePlanes[i].id == data.basePlaneId) {
                m_referencePlaneCombo->setCurrentIndex(i);
                break;
            }
        }
        m_planeOffsetSpin->setValue(data.offset);
        m_planeRollSpin->setValue(data.rollAngle);
        break;

    case ConstructionPlaneType::Angled:
        m_angledButton->setChecked(true);
        m_primaryAxisCombo->setCurrentIndex(
            m_primaryAxisCombo->findData(static_cast<int>(data.primaryAxis)));
        m_primaryAngleSpin->setValue(data.primaryAngle);
        m_secondaryAxisCombo->setCurrentIndex(
            m_secondaryAxisCombo->findData(static_cast<int>(data.secondaryAxis)));
        m_secondaryAngleSpin->setValue(data.secondaryAngle);
        m_angledOffsetSpin->setValue(data.offset);
        m_angledRollSpin->setValue(data.rollAngle);
        break;
    }

    // Set origin point (plane center in absolute coordinates)
    m_originXSpin->setValue(data.originX);
    m_originYSpin->setValue(data.originY);
    m_originZSpin->setValue(data.originZ);

    m_visibleCheck->setChecked(data.visible);

    updateVisibility();
    updatePreviewText();
}

ConstructionPlaneData ConstructionPlaneDialog::planeData() const
{
    ConstructionPlaneData data;
    data.id = m_planeId;
    data.name = m_nameEdit->text().trimmed();
    if (data.name.isEmpty()) {
        data.name = m_nameEdit->placeholderText();
    }

    int typeId = m_typeGroup->checkedId();
    data.type = static_cast<ConstructionPlaneType>(typeId);

    switch (data.type) {
    case ConstructionPlaneType::OffsetFromOrigin:
        data.basePlane = static_cast<SketchPlane>(m_basePlaneCombo->currentData().toInt());
        data.offset = m_originOffsetSpin->value();
        data.rollAngle = m_originRollSpin->value();
        break;

    case ConstructionPlaneType::OffsetFromPlane: {
        int idx = m_referencePlaneCombo->currentIndex();
        if (idx >= 0 && idx < m_availablePlanes.size()) {
            data.basePlaneId = m_availablePlanes[idx].id;
        }
        data.offset = m_planeOffsetSpin->value();
        data.rollAngle = m_planeRollSpin->value();
        break;
    }

    case ConstructionPlaneType::Angled:
        data.primaryAxis = static_cast<PlaneRotationAxis>(m_primaryAxisCombo->currentData().toInt());
        data.primaryAngle = m_primaryAngleSpin->value();
        data.secondaryAxis = static_cast<PlaneRotationAxis>(m_secondaryAxisCombo->currentData().toInt());
        data.secondaryAngle = m_secondaryAngleSpin->value();
        data.offset = m_angledOffsetSpin->value();
        data.rollAngle = m_angledRollSpin->value();
        break;
    }

    // Origin point (plane center in absolute coordinates)
    data.originX = m_originXSpin->value();
    data.originY = m_originYSpin->value();
    data.originZ = m_originZSpin->value();

    data.visible = m_visibleCheck->isChecked();

    return data;
}

void ConstructionPlaneDialog::setAvailablePlanes(const QVector<ConstructionPlaneData>& planes)
{
    m_availablePlanes = planes;

    // Update reference plane combo
    m_referencePlaneCombo->clear();
    for (const auto& plane : planes) {
        m_referencePlaneCombo->addItem(plane.name, plane.id);
    }

    // Disable offset from plane option if no planes available
    m_offsetFromPlaneButton->setEnabled(!planes.isEmpty());
    if (planes.isEmpty() && m_offsetFromPlaneButton->isChecked()) {
        m_offsetFromOriginButton->setChecked(true);
    }

    updateVisibility();
}

void ConstructionPlaneDialog::setEditMode(bool editing)
{
    if (editing) {
        setWindowTitle(tr("Edit Construction Plane"));
    } else {
        setWindowTitle(tr("New Construction Plane"));
    }
}

}  // namespace hobbycad
