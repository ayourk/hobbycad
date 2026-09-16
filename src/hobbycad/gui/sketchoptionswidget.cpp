// =====================================================================
//  src/hobbycad/gui/sketchoptionswidget.cpp
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#include "sketchoptionswidget.h"
#include "sketchcanvas.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace hobbycad {

SketchOptionsWidget::SketchOptionsWidget(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(8);

    // ---- Display ----
    auto* disp = new QGroupBox(tr("Display"), this);
    auto* dl = new QVBoxLayout(disp);
    m_grid = new QCheckBox(tr("Sketch grid"), disp);
    m_snap = new QCheckBox(tr("Snap to grid"), disp);
    dl->addWidget(m_grid);
    dl->addWidget(m_snap);
    root->addWidget(disp);

    // ---- Show ----
    auto* show = new QGroupBox(tr("Show"), this);
    auto* sl = new QVBoxLayout(show);
    m_profiles    = new QCheckBox(tr("Profiles"), show);
    m_points      = new QCheckBox(tr("Points"), show);
    m_dims        = new QCheckBox(tr("Dimensions"), show);
    m_constraints = new QCheckBox(tr("Constraints"), show);
    m_coincidenceMarkers = new QCheckBox(tr("Coincidence markers"), show);
    m_coincidenceMarkers->setToolTip(
        tr("Show a marker where points are joined by a coincidence "
           "(off by default, like Fusion)"));
    m_showConstruction = new QCheckBox(tr("Construction geometry"), show);
    m_showProjected    = new QCheckBox(tr("Projected geometry"), show);
    sl->addWidget(m_profiles);
    sl->addWidget(m_points);
    sl->addWidget(m_dims);
    sl->addWidget(m_constraints);
    { auto* sub = new QHBoxLayout; sub->setContentsMargins(0, 0, 0, 0);
      sub->addSpacing(18); sub->addWidget(m_coincidenceMarkers); sl->addLayout(sub); }
    sl->addWidget(m_showConstruction);
    sl->addWidget(m_showProjected);
    root->addWidget(show);

    // ---- Selection ----
    auto* sel = new QGroupBox(tr("Selection"), this);
    auto* xl = new QFormLayout(sel);
    m_linetype = new QComboBox(sel);
    m_linetype->addItem(tr("Normal"));
    m_linetype->addItem(tr("Construction"));
    m_linetype->addItem(tr("Centerline"));
    m_linetype->setToolTip(tr("Linetype of the selected geometry"));
    m_linetype->setEnabled(false);
    xl->addRow(tr("Linetype:"), m_linetype);
    root->addWidget(sel);

    m_lookAt = new QPushButton(tr("Look At"), this);
    m_lookAt->setToolTip(tr("Fit and center the sketch in the view"));
    root->addWidget(m_lookAt);

    m_slice = new QCheckBox(tr("Slice"), this);
    m_slice->setToolTip(tr("Section the model at the sketch plane, to sketch against its interior"));
    m_slice->setEnabled(false);   // enabled when bound to the Slice action (full/3D mode)
    root->addWidget(m_slice);

    root->addStretch(1);

    // ---- Wiring (each write guarded so syncFromCanvas does not echo back) ----
    connect(m_grid, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_syncing && m_canvas) m_canvas->setGridVisible(on); });
    connect(m_snap, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_syncing && m_canvas) m_canvas->setSnapToGrid(on); });
    connect(m_profiles, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_syncing && m_canvas) m_canvas->setShowProfiles(on); });
    connect(m_points, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_syncing && m_canvas) m_canvas->setShowUnconstrainedPoints(on); });
    connect(m_dims, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_syncing && m_canvas) m_canvas->setShowDimensions(on); });
    connect(m_constraints, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_syncing && m_canvas) m_canvas->setShowConstraints(on); });
    // "Coincidence markers" is independent of "Show Constraints" (it toggles a
    // marker for otherwise badge-less point coincidences), so it stays enabled.
    connect(m_coincidenceMarkers, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_syncing && m_canvas) m_canvas->setShowCoincidenceMarkers(on); });
    connect(m_showConstruction, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_syncing && m_canvas) m_canvas->setShowConstruction(on); });
    connect(m_showProjected, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_syncing && m_canvas) m_canvas->setShowProjected(on); });
    connect(m_linetype, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int index) {
        if (m_syncing || !m_canvas) return;
        // 0 Normal, 1 Construction, 2 Centerline (mutually exclusive linetypes).
        for (int id : m_canvas->selectedEntityIds()) {
            m_canvas->setEntityConstruction(id, index == 1);
            m_canvas->setEntityCenterline(id, index == 2);
        }
    });
    connect(m_lookAt, &QPushButton::clicked, this, [this]() {
        if (m_canvas) m_canvas->resetView(); });
}

void SketchOptionsWidget::setSketchCanvas(SketchCanvas* canvas) {
    if (m_canvas) disconnect(m_canvas, nullptr, this, nullptr);
    m_canvas = canvas;
    if (m_canvas) {
        connect(m_canvas, &SketchCanvas::selectionChanged,
                this, &SketchOptionsWidget::onSelectionChanged);
    }
    syncFromCanvas();
}

void SketchOptionsWidget::syncFromCanvas() {
    if (!m_canvas) return;
    m_syncing = true;
    m_grid->setChecked(m_canvas->isGridVisible());
    m_snap->setChecked(m_canvas->snapToGrid());
    m_profiles->setChecked(m_canvas->showProfiles());
    m_points->setChecked(m_canvas->showUnconstrainedPoints());
    m_dims->setChecked(m_canvas->showDimensions());
    m_constraints->setChecked(m_canvas->showConstraints());
    m_coincidenceMarkers->setChecked(m_canvas->showCoincidenceMarkers());
    m_coincidenceMarkers->setEnabled(m_canvas->showConstraints());
    m_showConstruction->setChecked(m_canvas->showConstruction());
    m_showProjected->setChecked(m_canvas->showProjected());
    m_syncing = false;
    onSelectionChanged();
}

void SketchOptionsWidget::onSelectionChanged() {
    if (!m_canvas) return;
    const bool hasSel = m_canvas->selectionCount() > 0;
    m_linetype->setEnabled(hasSel);
    m_syncing = true;
    const SketchEntity* e = m_canvas->selectedEntity();
    int idx = 0;
    if (hasSel && e) idx = e->isCenterline ? 2 : (e->isConstruction ? 1 : 0);
    m_linetype->setCurrentIndex(idx);
    m_syncing = false;
}


void SketchOptionsWidget::setSliceAction(QAction* action)
{
    if (!m_slice || !action) return;
    m_slice->setEnabled(true);
    m_slice->setChecked(action->isChecked());
    connect(m_slice, &QCheckBox::toggled, action, [action](bool on) {
        if (action->isChecked() != on) action->setChecked(on);
    });
    connect(action, &QAction::toggled, m_slice, [this](bool on) {
        if (m_slice->isChecked() != on) m_slice->setChecked(on);
    });
}

}  // namespace hobbycad
