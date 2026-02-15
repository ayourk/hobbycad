// =====================================================================
//  src/hobbycad/gui/constructionplanedialog.h — Construction plane dialog
// =====================================================================
//
//  Dialog for creating and editing construction planes. Construction
//  planes are first-class objects that can be referenced by sketches
//  and other features.
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_CONSTRUCTIONPLANEDIALOG_H
#define HOBBYCAD_CONSTRUCTIONPLANEDIALOG_H

#include <hobbycad/project.h>

#include <QDialog>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QRadioButton;
class QStackedWidget;

namespace hobbycad {

/// Dialog for creating or editing construction planes.
class ConstructionPlaneDialog : public QDialog {
    Q_OBJECT

public:
    explicit ConstructionPlaneDialog(QWidget* parent = nullptr);

    /// Set the plane data to edit (for editing existing planes).
    void setPlaneData(const ConstructionPlaneData& data);

    /// Get the configured plane data.
    ConstructionPlaneData planeData() const;

    /// Set available construction planes for "offset from plane" option.
    void setAvailablePlanes(const QVector<ConstructionPlaneData>& planes);

    /// Set whether this is creating a new plane or editing existing.
    void setEditMode(bool editing);

private slots:
    void updatePreviewText();
    void updateVisibility();

private:
    void setupUi();
    void setupTypeWidgets();
    void setupOffsetFromOriginWidgets();
    void setupOffsetFromPlaneWidgets();
    void setupAngledWidgets();

    // Plane data
    int m_planeId = 0;

    // Name
    QLineEdit* m_nameEdit = nullptr;

    // Type selection
    QButtonGroup* m_typeGroup = nullptr;
    QRadioButton* m_offsetFromOriginButton = nullptr;
    QRadioButton* m_offsetFromPlaneButton = nullptr;
    QRadioButton* m_angledButton = nullptr;

    // Type-specific widgets container
    QStackedWidget* m_optionsStack = nullptr;

    // Offset from origin widgets
    QWidget* m_offsetOriginPage = nullptr;
    QComboBox* m_basePlaneCombo = nullptr;
    QDoubleSpinBox* m_originOffsetSpin = nullptr;
    QDoubleSpinBox* m_originRollSpin = nullptr;  // Roll angle for origin-based planes

    // Offset from plane widgets
    QWidget* m_offsetPlanePage = nullptr;
    QComboBox* m_referencePlaneCombo = nullptr;
    QDoubleSpinBox* m_planeOffsetSpin = nullptr;
    QDoubleSpinBox* m_planeRollSpin = nullptr;  // Roll angle for offset planes

    // Angled plane widgets
    QWidget* m_angledPage = nullptr;
    QComboBox* m_primaryAxisCombo = nullptr;
    QDoubleSpinBox* m_primaryAngleSpin = nullptr;
    QComboBox* m_secondaryAxisCombo = nullptr;
    QDoubleSpinBox* m_secondaryAngleSpin = nullptr;
    QDoubleSpinBox* m_angledOffsetSpin = nullptr;
    QDoubleSpinBox* m_angledRollSpin = nullptr;  // Roll angle for angled planes

    // Origin point (plane center in absolute coordinates)
    QDoubleSpinBox* m_originXSpin = nullptr;
    QDoubleSpinBox* m_originYSpin = nullptr;
    QDoubleSpinBox* m_originZSpin = nullptr;

    // Visibility
    QCheckBox* m_visibleCheck = nullptr;

    // Preview
    QLabel* m_previewLabel = nullptr;

    // Available planes for reference
    QVector<ConstructionPlaneData> m_availablePlanes;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_CONSTRUCTIONPLANEDIALOG_H
