// =====================================================================
//  src/hobbycad/gui/formulafield.h — Formula field with fx button
// =====================================================================
//
//  A compact widget for displaying and editing parametric values.
//  Shows the current value/formula with an "fx" button that opens
//  the full formula editor dialog.
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_FORMULAFIELD_H
#define HOBBYCAD_FORMULAFIELD_H

#include "formulaedit.h"

#include <QWidget>
#include <QMap>

class QLabel;
class QToolButton;

namespace hobbycad {

class FormulaField : public QWidget {
    Q_OBJECT

public:
    explicit FormulaField(QWidget* parent = nullptr);

    /// Set the property name (for dialog title)
    void setPropertyName(const QString& name);

    /// Set the unit suffix (e.g., "mm", "°")
    void setUnitSuffix(const QString& suffix);

    /// Set available parameters and their values
    void setParameters(const QMap<QString, double>& params);

    /// Get/set the expression
    QString expression() const;
    void setExpression(const QString& expr);

    /// Get the parametric value
    ParametricValue parametricValue() const;

    /// Get evaluated result
    double evaluatedValue() const;

    /// Check if current value is valid
    bool isValid() const;

    /// Check if this contains a formula (vs plain number)
    bool isFormula() const;

signals:
    /// Emitted when the value changes
    void valueChanged(double value);

    /// Emitted when the expression changes
    void expressionChanged(const QString& expr);

private slots:
    void onFxButtonClicked();
    void onLabelDoubleClicked();

private:
    void updateDisplay();

    QString m_propertyName;
    QString m_unitSuffix;
    QMap<QString, double> m_parameters;
    ParametricValue m_value;

    QLabel* m_valueLabel = nullptr;
    QToolButton* m_fxButton = nullptr;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_FORMULAFIELD_H
