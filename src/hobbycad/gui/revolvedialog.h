// =====================================================================
//  src/hobbycad/gui/revolvedialog.h — Revolve operation dialog
// =====================================================================
//
//  Dialog for configuring revolve parameters: axis, angle, and
//  operation type (new body, join, cut).
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_REVOLVEDIALOG_H
#define HOBBYCAD_REVOLVEDIALOG_H

#include <QDialog>

class QDoubleSpinBox;
class QComboBox;
class QDialogButtonBox;

namespace hobbycad {

/// Axis selection for revolve operation
enum class RevolveAxis {
    XAxis,          ///< Revolve around X axis
    YAxis,          ///< Revolve around Y axis
    SketchLine      ///< Revolve around a selected sketch line (construction line)
};

/// Operation type for revolve
enum class RevolveOperation {
    NewBody,    ///< Create a new solid body
    Join,       ///< Union with existing body
    Cut,        ///< Subtract from existing body
    Intersect   ///< Intersect with existing body
};

/// Dialog for configuring revolve operation
class RevolveDialog : public QDialog {
    Q_OBJECT

public:
    explicit RevolveDialog(QWidget* parent = nullptr);

    /// Get the revolution angle in degrees
    double angle() const;

    /// Set the revolution angle
    void setAngle(double angleDeg);

    /// Get the selected axis
    RevolveAxis axis() const;

    /// Set the axis
    void setAxis(RevolveAxis ax);

    /// Get the selected operation type
    RevolveOperation operation() const;

    /// Set the operation type
    void setOperation(RevolveOperation op);

    /// Get the selected sketch line ID for axis (-1 if not using sketch line)
    int axisLineId() const;

    /// Set available construction lines for axis selection
    void setAxisLines(const QVector<QPair<int, QString>>& lines);

private:
    void createWidgets();

    QDoubleSpinBox* m_angleSpinBox = nullptr;
    QComboBox* m_axisCombo = nullptr;
    QComboBox* m_axisLineCombo = nullptr;
    QComboBox* m_operationCombo = nullptr;
    QDialogButtonBox* m_buttonBox = nullptr;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_REVOLVEDIALOG_H
