// =====================================================================
//  planetransformpanel.cpp - In-place transform editor for a construction plane
//
//  Part of HobbyCAD.
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#include "planetransformpanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QPushButton>
#include <QShortcut>
#include <QtMath>

namespace hobbycad {

PlaneTransformPanel::PlaneTransformPanel(QWidget* parent)
    : QGroupBox(tr("Plane transform"), parent)
{
    auto* form = new QFormLayout(this);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);

    auto spin = [this](double lo, double hi, int dp, const QString& suffix) {
        auto* sb = new QDoubleSpinBox;
        sb->setRange(lo, hi); sb->setDecimals(dp); sb->setSuffix(suffix);
        sb->setKeyboardTracking(false);
        connect(sb, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &PlaneTransformPanel::onFieldChanged);
        return sb;
    };
    auto axisCombo = [this]() {
        auto* cb = new QComboBox;
        cb->addItems({tr("X"), tr("Y"), tr("Z")});
        connect(cb, qOverload<int>(&QComboBox::currentIndexChanged), this, &PlaneTransformPanel::onFieldChanged);
        return cb;
    };
    auto pair = [](QWidget* a, QWidget* b) {
        auto* box = new QWidget; auto* hl = new QHBoxLayout(box); hl->setContentsMargins(0, 0, 0, 0);
        hl->addWidget(a); hl->addWidget(b, 1); return box;
    };

    m_typeLabel = new QLabel;
    form->addRow(tr("Type:"), m_typeLabel);
    m_offset = spin(-100000, 100000, 3, QStringLiteral(" mm"));
    form->addRow(tr("Offset:"), m_offset);
    m_primaryAxis = axisCombo(); m_primaryAngle = spin(-360, 360, 2, QStringLiteral("°"));
    form->addRow(tr("Rotate 1:"), pair(m_primaryAxis, m_primaryAngle));
    m_secondaryAxis = axisCombo(); m_secondaryAngle = spin(-360, 360, 2, QStringLiteral("°"));
    form->addRow(tr("Rotate 2:"), pair(m_secondaryAxis, m_secondaryAngle));
    m_roll = spin(-360, 360, 2, QStringLiteral("°"));
    m_roll->setToolTip(tr("Spin about the plane's own normal; changes sketch orientation only"));
    form->addRow(tr("Roll:"), m_roll);
    // Center: absolute by default; "relative to" measures the same three
    // numbers along a reference plane's X, Y and normal, and the plane then
    // follows that reference.
    m_centerMode = new QComboBox;
    m_centerMode->addItems({tr("Absolute (global axes)"), tr("Relative to base plane"), tr("Relative to plane…")});
    m_centerMode->setToolTip(tr("Absolute: X/Y/Z are global coordinates. Relative: X/Y/Z are offsets along the reference plane's axes and normal"));
    connect(m_centerMode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) { updateCenterRows(); onFieldChanged(); });
    form->addRow(tr("Center is:"), m_centerMode);
    m_refPlane = new QComboBox;
    connect(m_refPlane, qOverload<int>(&QComboBox::currentIndexChanged), this, &PlaneTransformPanel::onFieldChanged);
    m_refPlaneLabel = new QLabel(tr("Reference:"));
    form->addRow(m_refPlaneLabel, m_refPlane);
    m_centerX = spin(-100000, 100000, 3, QStringLiteral(" mm"));
    m_centerY = spin(-100000, 100000, 3, QStringLiteral(" mm"));
    m_centerZ = spin(-100000, 100000, 3, QStringLiteral(" mm"));
    m_centerXLabel = new QLabel(tr("Center X:")); m_centerYLabel = new QLabel(tr("Center Y:")); m_centerZLabel = new QLabel(tr("Center Z:"));
    form->addRow(m_centerXLabel, m_centerX);
    form->addRow(m_centerYLabel, m_centerY);
    form->addRow(m_centerZLabel, m_centerZ);
    m_visible = new QCheckBox(tr("Visible in viewport"));
    connect(m_visible, &QCheckBox::toggled, this, &PlaneTransformPanel::onFieldChanged);
    form->addRow(m_visible);

    m_apply = new QPushButton(tr("Apply"));
    m_apply->setDefault(true);
    m_reset = new QPushButton(tr("Reset"));
    connect(m_apply, &QPushButton::clicked, this, &PlaneTransformPanel::onApply);
    connect(m_reset, &QPushButton::clicked, this, &PlaneTransformPanel::onReset);
    form->addRow(m_reset, m_apply);
    m_status = new QLabel; m_status->setWordWrap(true);
    // Two lines are always reserved: a status appearing must not shift the
    // fields the user is typing into.
    m_status->setMinimumHeight(2 * m_status->fontMetrics().height() + 4);
    form->addRow(m_status);

    for (auto key : {Qt::Key_Return, Qt::Key_Enter}) {
        auto* sc = new QShortcut(QKeySequence(key), this);
        sc->setContext(Qt::WidgetWithChildrenShortcut);
        connect(sc, &QShortcut::activated, this, &PlaneTransformPanel::onApply);
    }
    auto* esc = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    esc->setContext(Qt::WidgetWithChildrenShortcut);
    connect(esc, &QShortcut::activated, this, &PlaneTransformPanel::onReset);

    m_apply->setEnabled(false);
    m_reset->setEnabled(false);
}

void PlaneTransformPanel::setPlane(const ConstructionPlaneData& plane,
                                   const std::vector<ConstructionPlaneData>& allPlanes)
{
    m_stored = plane;
    m_otherPlanes.clear();
    // Never offer a reference that would close a loop: not this plane, and
    // not any plane whose own chain already leads back to it.
    for (const auto& q : allPlanes)
        if (q.id != plane.id && !Project::planeDependsOn(allPlanes, plane.id, q.id))
            m_otherPlanes.push_back(q);
    loadFields(plane);
    m_status->clear();
    m_apply->setEnabled(false);
    m_reset->setEnabled(false);
}

void PlaneTransformPanel::loadFields(const ConstructionPlaneData& p)
{
    m_updating = true;
    QString type;
    switch (p.type) {
    case ConstructionPlaneType::OffsetFromOrigin: type = tr("Offset from origin plane"); break;
    case ConstructionPlaneType::OffsetFromPlane:  type = tr("Offset from another plane"); break;
    case ConstructionPlaneType::Angled:           type = tr("Angled"); break;
    }
    m_typeLabel->setText(type);
    m_offset->setValue(p.offset);
    m_primaryAxis->setCurrentIndex(int(p.primaryAxis));
    m_primaryAngle->setValue(p.primaryAngle);
    m_secondaryAxis->setCurrentIndex(int(p.secondaryAxis));
    m_secondaryAngle->setValue(p.secondaryAngle);
    m_roll->setValue(p.rollAngle);
    m_refPlane->clear();
    int refIndex = -1;
    for (const auto& q : m_otherPlanes) {
        m_refPlane->addItem(QString::fromStdString(q.name), q.id);
        if (q.id == p.centerRefPlaneId) refIndex = m_refPlane->count() - 1;
    }
    m_refPlane->setEnabled(m_refPlane->count() > 0);
    m_refPlane->setToolTip(m_refPlane->count() > 0
        ? tr("Planes that already depend on this one are left out, so no loop can form")
        : tr("No other plane can be a reference: the others all depend on this plane"));
    if (!p.centerRelative) m_centerMode->setCurrentIndex(0);
    else if (refIndex >= 0) { m_centerMode->setCurrentIndex(2); m_refPlane->setCurrentIndex(refIndex); }
    else m_centerMode->setCurrentIndex(1);
    m_centerX->setValue(p.originX);
    m_centerY->setValue(p.originY);
    m_centerZ->setValue(p.originZ);
    m_visible->setChecked(p.visible);
    m_updating = false;
    updateCenterRows();
}

void PlaneTransformPanel::updateCenterRows()
{
    const int mode = m_centerMode->currentIndex();
    const bool relative = mode != 0;
    const bool named = mode == 2;
    m_refPlane->setVisible(named);
    m_refPlaneLabel->setVisible(named);
    const QString sfx = relative ? tr(" (offset)") : QString();
    m_centerXLabel->setText(tr("Center X:") + sfx);
    m_centerYLabel->setText(tr("Center Y:") + sfx);
    m_centerZLabel->setText(tr("Center Z:") + sfx);
    const QString tip = relative ? tr("Offset along the reference plane's axes: X and Y in the plane, Z along its normal")
                                 : tr("Global coordinate");
    for (auto* sb : {m_centerX, m_centerY, m_centerZ}) sb->setToolTip(tip);
}

ConstructionPlaneData PlaneTransformPanel::editedPlane() const
{
    ConstructionPlaneData p = m_stored;
    p.offset = m_offset->value();
    p.primaryAxis = static_cast<PlaneRotationAxis>(m_primaryAxis->currentIndex());
    p.primaryAngle = m_primaryAngle->value();
    p.secondaryAxis = static_cast<PlaneRotationAxis>(m_secondaryAxis->currentIndex());
    p.secondaryAngle = m_secondaryAngle->value();
    p.rollAngle = m_roll->value();
    p.originX = m_centerX->value();
    p.originY = m_centerY->value();
    p.originZ = m_centerZ->value();
    p.centerRelative = m_centerMode->currentIndex() != 0;
    p.centerRefPlaneId = (m_centerMode->currentIndex() == 2 && m_refPlane->currentIndex() >= 0)
                             ? m_refPlane->currentData().toInt() : -1;
    p.visible = m_visible->isChecked();
    // A rotation on an offset plane makes it an angled plane; the type
    // follows the values so the file and the tree describe what is there.
    if (p.type != ConstructionPlaneType::OffsetFromPlane) {
        const bool turned = !qFuzzyIsNull(p.primaryAngle) || !qFuzzyIsNull(p.secondaryAngle);
        p.type = turned ? ConstructionPlaneType::Angled : ConstructionPlaneType::OffsetFromOrigin;
    }
    return p;
}

bool PlaneTransformPanel::dirty() const
{
    const ConstructionPlaneData e = editedPlane();
    const ConstructionPlaneData& s = m_stored;
    return e.offset != s.offset || e.primaryAxis != s.primaryAxis || e.primaryAngle != s.primaryAngle
        || e.secondaryAxis != s.secondaryAxis || e.secondaryAngle != s.secondaryAngle || e.rollAngle != s.rollAngle
        || e.originX != s.originX || e.originY != s.originY || e.originZ != s.originZ || e.visible != s.visible
        || e.centerRelative != s.centerRelative || e.centerRefPlaneId != s.centerRefPlaneId;
}

void PlaneTransformPanel::setStatus(const QString& text, bool isError)
{
    m_status->setStyleSheet(isError ? "QLabel { color: #b00020; }" : "QLabel { color: #666; }");
    m_status->setText(text);
}

void PlaneTransformPanel::onFieldChanged()
{
    if (m_updating) return;
    const bool d = dirty();
    m_apply->setEnabled(d);
    m_reset->setEnabled(d);
    setStatus(d ? tr("Preview shown; Apply to commit, Reset to discard.") : QString(), false);
    emit previewRequested(editedPlane());
}

void PlaneTransformPanel::onReset()
{
    loadFields(m_stored);
    m_apply->setEnabled(false);
    m_reset->setEnabled(false);
    setStatus(QString(), false);
    emit resetRequested();
}

void PlaneTransformPanel::onApply()
{
    if (!isVisible() || !dirty()) return;
    emit applyRequested(editedPlane());
}

}  // namespace hobbycad
