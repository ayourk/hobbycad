// =====================================================================
//  src/hobbycad/gui/sketchplanedialog.h — Sketch plane selection dialog
// =====================================================================
//
//  Dialog shown when creating a new sketch to select the sketch plane
//  and optional offset from origin.
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_SKETCHPLANEDIALOG_H
#define HOBBYCAD_SKETCHPLANEDIALOG_H

#include <hobbycad/project.h>

#include <QDialog>

class QButtonGroup;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QRadioButton;

namespace hobbycad {

/// Dialog for selecting sketch plane and offset when creating a new sketch.
class SketchPlaneDialog : public QDialog {
    Q_OBJECT

public:
    explicit SketchPlaneDialog(QWidget* parent = nullptr);

    /// Get the selected sketch plane.
    SketchPlane selectedPlane() const;

    /// Get the offset distance from origin.
    double offset() const;

    /// Get the rotation axis for custom planes.
    PlaneRotationAxis rotationAxis() const;

    /// Get the rotation angle for custom planes (degrees).
    double rotationAngle() const;

    /// Get the selected construction plane ID (-1 if using origin plane).
    int constructionPlaneId() const;

    /// Set the initial plane selection.
    void setSelectedPlane(SketchPlane plane);

    /// Set the initial offset value.
    void setOffset(double offset);

    /// Set available construction planes for selection.
    void setAvailableConstructionPlanes(const QVector<ConstructionPlaneData>& planes);

private:
    void setupUi();
    void updatePreviewText();
    void updateAngleWidgetsVisibility();
    void updateConstructionPlaneVisibility();

    QButtonGroup*    m_planeGroup     = nullptr;
    QRadioButton*    m_xyButton       = nullptr;
    QRadioButton*    m_xzButton       = nullptr;
    QRadioButton*    m_yzButton       = nullptr;
    QRadioButton*    m_customButton   = nullptr;
    QRadioButton*    m_constructionButton = nullptr;
    QGroupBox*       m_angleGroup     = nullptr;
    QGroupBox*       m_constructionGroup = nullptr;
    QComboBox*       m_axisCombo      = nullptr;
    QComboBox*       m_constructionPlaneCombo = nullptr;
    QDoubleSpinBox*  m_angleSpin      = nullptr;
    QDoubleSpinBox*  m_offsetSpin     = nullptr;
    QLabel*          m_previewLabel   = nullptr;

    QVector<ConstructionPlaneData> m_availablePlanes;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_SKETCHPLANEDIALOG_H
