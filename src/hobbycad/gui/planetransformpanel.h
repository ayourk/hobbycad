// =====================================================================
//  planetransformpanel.h - In-place transform editor for a construction plane
//
//  The 3D counterpart of the sketch Transform section: offset, the two
//  rotations, roll, and the center of a construction plane, edited in the
//  Properties dock with a live preview in the viewport. Nothing is written
//  until Apply; Reset returns every field to the stored plane.
//
//  Part of HobbyCAD.
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#ifndef HOBBYCAD_PLANETRANSFORMPANEL_H
#define HOBBYCAD_PLANETRANSFORMPANEL_H

#include <QGroupBox>
#include <hobbycad/project.h>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;

namespace hobbycad {

class PlaneTransformPanel : public QGroupBox {
    Q_OBJECT

public:
    explicit PlaneTransformPanel(QWidget* parent = nullptr);

    /// The plane as stored in the project; every field is reset to it.
    /// `allPlanes` fills the "relative to" chooser (this plane is left out).
    void setPlane(const ConstructionPlaneData& plane,
                  const std::vector<ConstructionPlaneData>& allPlanes = {});
    const ConstructionPlaneData& storedPlane() const { return m_stored; }
    /// The stored plane with the panel's current values applied.
    ConstructionPlaneData editedPlane() const;
    int planeId() const { return m_stored.id; }
    bool dirty() const;
    void setStatus(const QString& text, bool isError);

signals:
    /// A field changed: show this plane where it WOULD be.
    void previewRequested(const hobbycad::ConstructionPlaneData& plane);
    /// Apply pressed (or Enter): commit this plane, one undo step.
    void applyRequested(const hobbycad::ConstructionPlaneData& plane);
    /// Reset pressed (or Escape): the preview goes back to the stored plane.
    void resetRequested();

private:
    void onFieldChanged();
    void onReset();
    void onApply();
    void loadFields(const ConstructionPlaneData& plane);
    void updateCenterRows();
    std::vector<ConstructionPlaneData> m_otherPlanes;

    ConstructionPlaneData m_stored;
    bool m_updating = false;
    QLabel* m_typeLabel = nullptr;
    QDoubleSpinBox* m_offset = nullptr;
    QComboBox* m_primaryAxis = nullptr;
    QDoubleSpinBox* m_primaryAngle = nullptr;
    QComboBox* m_secondaryAxis = nullptr;
    QDoubleSpinBox* m_secondaryAngle = nullptr;
    QDoubleSpinBox* m_roll = nullptr;
    QComboBox* m_centerMode = nullptr;      ///< Absolute / Relative to base / Relative to plane
    QComboBox* m_refPlane = nullptr;        ///< Other construction planes, by id in item data
    QLabel* m_refPlaneLabel = nullptr;
    QLabel* m_centerXLabel = nullptr; QLabel* m_centerYLabel = nullptr; QLabel* m_centerZLabel = nullptr;
    QDoubleSpinBox* m_centerX = nullptr;
    QDoubleSpinBox* m_centerY = nullptr;
    QDoubleSpinBox* m_centerZ = nullptr;
    QCheckBox* m_visible = nullptr;
    QPushButton* m_apply = nullptr;
    QPushButton* m_reset = nullptr;
    QLabel* m_status = nullptr;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_PLANETRANSFORMPANEL_H
