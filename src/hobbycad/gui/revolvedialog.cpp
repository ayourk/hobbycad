// =====================================================================
//  src/hobbycad/gui/revolvedialog.cpp — Revolve operation dialog
// =====================================================================
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include "revolvedialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QVBoxLayout>

namespace hobbycad {

RevolveDialog::RevolveDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Revolve"));
    setMinimumWidth(300);

    createWidgets();
}

void RevolveDialog::createWidgets()
{
    auto* mainLayout = new QVBoxLayout(this);

    // Angle group
    auto* angleGroup = new QGroupBox(tr("Angle"), this);
    auto* angleLayout = new QFormLayout(angleGroup);

    m_angleSpinBox = new QDoubleSpinBox(this);
    m_angleSpinBox->setRange(0.1, 360.0);
    m_angleSpinBox->setValue(360.0);
    m_angleSpinBox->setSuffix(tr("\u00B0"));  // Degree symbol
    m_angleSpinBox->setDecimals(1);
    angleLayout->addRow(tr("Angle:"), m_angleSpinBox);

    mainLayout->addWidget(angleGroup);

    // Axis group
    auto* axisGroup = new QGroupBox(tr("Axis"), this);
    auto* axisLayout = new QFormLayout(axisGroup);

    m_axisCombo = new QComboBox(this);
    m_axisCombo->addItem(tr("X Axis"), static_cast<int>(RevolveAxis::XAxis));
    m_axisCombo->addItem(tr("Y Axis"), static_cast<int>(RevolveAxis::YAxis));
    m_axisCombo->addItem(tr("Sketch Line"), static_cast<int>(RevolveAxis::SketchLine));
    axisLayout->addRow(tr("Axis:"), m_axisCombo);

    m_axisLineCombo = new QComboBox(this);
    m_axisLineCombo->setEnabled(false);
    m_axisLineCombo->setVisible(false);
    axisLayout->addRow(tr("Line:"), m_axisLineCombo);

    connect(m_axisCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
        auto axis = static_cast<RevolveAxis>(m_axisCombo->itemData(index).toInt());
        bool useSketchLine = (axis == RevolveAxis::SketchLine);
        m_axisLineCombo->setEnabled(useSketchLine);
        m_axisLineCombo->setVisible(useSketchLine);

        // Show/hide the label
        if (auto* layout = qobject_cast<QFormLayout*>(m_axisLineCombo->parentWidget()->layout())) {
            if (auto* label = layout->labelForField(m_axisLineCombo)) {
                label->setVisible(useSketchLine);
            }
        }
    });

    mainLayout->addWidget(axisGroup);

    // Operation group
    auto* operationGroup = new QGroupBox(tr("Operation"), this);
    auto* operationLayout = new QFormLayout(operationGroup);

    m_operationCombo = new QComboBox(this);
    m_operationCombo->addItem(tr("New Body"), static_cast<int>(RevolveOperation::NewBody));
    m_operationCombo->addItem(tr("Join"), static_cast<int>(RevolveOperation::Join));
    m_operationCombo->addItem(tr("Cut"), static_cast<int>(RevolveOperation::Cut));
    m_operationCombo->addItem(tr("Intersect"), static_cast<int>(RevolveOperation::Intersect));
    operationLayout->addRow(tr("Operation:"), m_operationCombo);

    mainLayout->addWidget(operationGroup);

    // Buttons
    m_buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    mainLayout->addWidget(m_buttonBox);
}

double RevolveDialog::angle() const
{
    return m_angleSpinBox->value();
}

void RevolveDialog::setAngle(double angleDeg)
{
    m_angleSpinBox->setValue(angleDeg);
}

RevolveAxis RevolveDialog::axis() const
{
    return static_cast<RevolveAxis>(m_axisCombo->currentData().toInt());
}

void RevolveDialog::setAxis(RevolveAxis ax)
{
    int index = m_axisCombo->findData(static_cast<int>(ax));
    if (index >= 0) {
        m_axisCombo->setCurrentIndex(index);
    }
}

RevolveOperation RevolveDialog::operation() const
{
    return static_cast<RevolveOperation>(m_operationCombo->currentData().toInt());
}

void RevolveDialog::setOperation(RevolveOperation op)
{
    int index = m_operationCombo->findData(static_cast<int>(op));
    if (index >= 0) {
        m_operationCombo->setCurrentIndex(index);
    }
}

int RevolveDialog::axisLineId() const
{
    if (axis() != RevolveAxis::SketchLine || m_axisLineCombo->count() == 0) {
        return -1;
    }
    return m_axisLineCombo->currentData().toInt();
}

void RevolveDialog::setAxisLines(const QVector<QPair<int, QString>>& lines)
{
    m_axisLineCombo->clear();
    for (const auto& line : lines) {
        m_axisLineCombo->addItem(line.second, line.first);
    }
}

}  // namespace hobbycad
