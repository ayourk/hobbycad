// =====================================================================
//  src/hobbycad/gui/extrudedialog.h — Extrude operation dialog
// =====================================================================
//
//  Dialog for configuring extrusion parameters: distance, direction,
//  symmetric mode, and operation type (new body, join, cut).
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_EXTRUDEDIALOG_H
#define HOBBYCAD_EXTRUDEDIALOG_H

#include <QDialog>

class QDoubleSpinBox;
class QComboBox;
class QCheckBox;
class QDialogButtonBox;

namespace hobbycad {

/// Operation type for extrusion
enum class ExtrudeOperation {
    NewBody,    ///< Create a new solid body
    Join,       ///< Union with existing body
    Cut,        ///< Subtract from existing body
    Intersect   ///< Intersect with existing body
};

/// Extrusion direction mode
enum class ExtrudeDirection {
    Normal,         ///< Along sketch plane normal (default)
    NormalReverse,  ///< Opposite to sketch plane normal
    TwoSided        ///< Symmetric about sketch plane
};

/// Dialog for configuring extrude operation
class ExtrudeDialog : public QDialog {
    Q_OBJECT

public:
    explicit ExtrudeDialog(QWidget* parent = nullptr);

    /// Get the extrusion distance in mm
    double distance() const;

    /// Set the extrusion distance
    void setDistance(double dist);

    /// Get the selected operation type
    ExtrudeOperation operation() const;

    /// Set the operation type
    void setOperation(ExtrudeOperation op);

    /// Get the direction mode
    ExtrudeDirection direction() const;

    /// Set the direction mode
    void setDirection(ExtrudeDirection dir);

    /// Whether a second distance is used (for TwoSided mode)
    double distance2() const;

    /// Set the second distance
    void setDistance2(double dist);

private slots:
    void onDirectionChanged(int index);

private:
    void createWidgets();

    QDoubleSpinBox* m_distanceSpinBox = nullptr;
    QDoubleSpinBox* m_distance2SpinBox = nullptr;
    QComboBox* m_directionCombo = nullptr;
    QComboBox* m_operationCombo = nullptr;
    QDialogButtonBox* m_buttonBox = nullptr;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_EXTRUDEDIALOG_H
