// =====================================================================
//  src/hobbycad/gui/extrudedialog.cpp — Extrude operation dialog
// =====================================================================
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include "extrudedialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QVBoxLayout>

namespace hobbycad {

ExtrudeDialog::ExtrudeDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Extrude"));
    setMinimumWidth(300);

    createWidgets();
}

void ExtrudeDialog::createWidgets()
{
    auto* mainLayout = new QVBoxLayout(this);

    // Distance group
    auto* distanceGroup = new QGroupBox(tr("Distance"), this);
    auto* distanceLayout = new QFormLayout(distanceGroup);

    m_distanceSpinBox = new QDoubleSpinBox(this);
    m_distanceSpinBox->setRange(0.01, 10000.0);
    m_distanceSpinBox->setValue(10.0);
    m_distanceSpinBox->setSuffix(tr(" mm"));
    m_distanceSpinBox->setDecimals(2);
    distanceLayout->addRow(tr("Distance:"), m_distanceSpinBox);

    m_distance2SpinBox = new QDoubleSpinBox(this);
    m_distance2SpinBox->setRange(0.01, 10000.0);
    m_distance2SpinBox->setValue(10.0);
    m_distance2SpinBox->setSuffix(tr(" mm"));
    m_distance2SpinBox->setDecimals(2);
    m_distance2SpinBox->setEnabled(false);
    m_distance2SpinBox->setVisible(false);
    distanceLayout->addRow(tr("Distance 2:"), m_distance2SpinBox);

    mainLayout->addWidget(distanceGroup);

    // Direction group
    auto* directionGroup = new QGroupBox(tr("Direction"), this);
    auto* directionLayout = new QFormLayout(directionGroup);

    m_directionCombo = new QComboBox(this);
    m_directionCombo->addItem(tr("One Side"), static_cast<int>(ExtrudeDirection::Normal));
    m_directionCombo->addItem(tr("One Side (Reverse)"), static_cast<int>(ExtrudeDirection::NormalReverse));
    m_directionCombo->addItem(tr("Two Sides (Symmetric)"), static_cast<int>(ExtrudeDirection::TwoSided));
    directionLayout->addRow(tr("Direction:"), m_directionCombo);

    connect(m_directionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ExtrudeDialog::onDirectionChanged);

    mainLayout->addWidget(directionGroup);

    // Operation group
    auto* operationGroup = new QGroupBox(tr("Operation"), this);
    auto* operationLayout = new QFormLayout(operationGroup);

    m_operationCombo = new QComboBox(this);
    m_operationCombo->addItem(tr("New Body"), static_cast<int>(ExtrudeOperation::NewBody));
    m_operationCombo->addItem(tr("Join"), static_cast<int>(ExtrudeOperation::Join));
    m_operationCombo->addItem(tr("Cut"), static_cast<int>(ExtrudeOperation::Cut));
    m_operationCombo->addItem(tr("Intersect"), static_cast<int>(ExtrudeOperation::Intersect));
    operationLayout->addRow(tr("Operation:"), m_operationCombo);

    mainLayout->addWidget(operationGroup);

    // Buttons
    m_buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    mainLayout->addWidget(m_buttonBox);
}

double ExtrudeDialog::distance() const
{
    return m_distanceSpinBox->value();
}

void ExtrudeDialog::setDistance(double dist)
{
    m_distanceSpinBox->setValue(dist);
}

ExtrudeOperation ExtrudeDialog::operation() const
{
    return static_cast<ExtrudeOperation>(m_operationCombo->currentData().toInt());
}

void ExtrudeDialog::setOperation(ExtrudeOperation op)
{
    int index = m_operationCombo->findData(static_cast<int>(op));
    if (index >= 0) {
        m_operationCombo->setCurrentIndex(index);
    }
}

ExtrudeDirection ExtrudeDialog::direction() const
{
    return static_cast<ExtrudeDirection>(m_directionCombo->currentData().toInt());
}

void ExtrudeDialog::setDirection(ExtrudeDirection dir)
{
    int index = m_directionCombo->findData(static_cast<int>(dir));
    if (index >= 0) {
        m_directionCombo->setCurrentIndex(index);
    }
}

double ExtrudeDialog::distance2() const
{
    return m_distance2SpinBox->value();
}

void ExtrudeDialog::setDistance2(double dist)
{
    m_distance2SpinBox->setValue(dist);
}

void ExtrudeDialog::onDirectionChanged(int index)
{
    auto dir = static_cast<ExtrudeDirection>(m_directionCombo->itemData(index).toInt());
    bool twoSided = (dir == ExtrudeDirection::TwoSided);

    // Show/hide second distance for two-sided extrusion
    m_distance2SpinBox->setEnabled(twoSided);
    m_distance2SpinBox->setVisible(twoSided);

    // Find the label for distance2 and show/hide it too
    if (auto* layout = qobject_cast<QFormLayout*>(m_distance2SpinBox->parentWidget()->layout())) {
        if (auto* label = layout->labelForField(m_distance2SpinBox)) {
            label->setVisible(twoSided);
        }
    }
}

}  // namespace hobbycad
