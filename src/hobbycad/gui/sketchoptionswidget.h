// =====================================================================
//  src/hobbycad/gui/sketchoptionswidget.h — Sketch options ("palette") dock
// =====================================================================
//  Fusion-style Sketch Palette: display and show-toggles for the active
//  sketch, a construction toggle for the selection, and Look At. Wraps the
//  SketchCanvas capabilities that already exist; a thin view over them.
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#ifndef HOBBYCAD_SKETCHOPTIONSWIDGET_H
#define HOBBYCAD_SKETCHOPTIONSWIDGET_H

#include <QWidget>

class QCheckBox;
class QComboBox;
class QPushButton;

class QAction;

namespace hobbycad {

class SketchCanvas;

class SketchOptionsWidget : public QWidget {
    Q_OBJECT
public:
    explicit SketchOptionsWidget(QWidget* parent = nullptr);

    /// Bind to the active sketch canvas (nullptr to unbind).
    void setSketchCanvas(SketchCanvas* canvas);
    /// Bind the Slice palette toggle to the app's Slice action (two-way).
    void setSliceAction(QAction* action);
    /// Pull the canvas's current state into the controls.
    void syncFromCanvas();

private slots:
    void onSelectionChanged();

private:
    SketchCanvas* m_canvas = nullptr;
    bool m_syncing = false;             ///< guards control->canvas writes during sync

    QCheckBox* m_grid = nullptr;
    QCheckBox* m_snap = nullptr;
    QCheckBox* m_profiles = nullptr;
    QCheckBox* m_points = nullptr;
    QCheckBox* m_dims = nullptr;
    QCheckBox* m_constraints = nullptr;
    QCheckBox* m_coincidenceMarkers = nullptr;  ///< sub of Constraints
    QCheckBox* m_showConstruction = nullptr;  ///< filter: hide construction geometry
    QCheckBox* m_showProjected = nullptr;     ///< filter: hide projected (reference) geometry
    /// Default for ellipse axis display (Properties overrides it per ellipse).
    QCheckBox* m_ellipseAxes = nullptr;
    QComboBox* m_linetype = nullptr;          ///< linetype of the selection (Normal/Construction/Centerline)
    QPushButton* m_lookAt = nullptr;
    QCheckBox* m_slice = nullptr;   ///< Section model at the sketch plane (bound to sliceAction)
};

}  // namespace hobbycad

#endif  // HOBBYCAD_SKETCHOPTIONSWIDGET_H
