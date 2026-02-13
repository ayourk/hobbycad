// =====================================================================
//  src/hobbycad/gui/formulaedit.h — Formula editor widget
// =====================================================================
//
//  A line edit widget for editing parametric values that can contain:
//  - Plain numeric values (e.g., "10")
//  - Named parameters (e.g., "width")
//  - Formulas (e.g., "width * 2 + 5")
//
//  Features:
//  - Autocomplete for parameter names
//  - Formula validation with error indication
//  - Shows computed result alongside formula
//  - Unit suffix display for numeric results
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_FORMULAEDIT_H
#define HOBBYCAD_FORMULAEDIT_H

#include <QLineEdit>
#include <QMap>
#include <QString>
#include <QVariant>

class QCompleter;

namespace hobbycad {

/// Represents a parametric value that can be a number, parameter, or formula
class ParametricValue {
public:
    enum class Type {
        Number,      // Plain numeric value
        Parameter,   // Named parameter reference
        Formula      // Mathematical expression
    };

    ParametricValue() = default;
    explicit ParametricValue(double value);
    explicit ParametricValue(const QString& expression);

    /// Get the type of this value
    Type type() const { return m_type; }

    /// Get the raw expression string
    QString expression() const { return m_expression; }

    /// Get the numeric value (evaluates if formula/parameter)
    double value() const { return m_value; }

    /// Check if the expression is valid
    bool isValid() const { return m_valid; }

    /// Get error message if invalid
    QString errorMessage() const { return m_errorMessage; }

    /// Set the expression and re-evaluate
    void setExpression(const QString& expr);

    /// Evaluate the expression with given parameter values
    bool evaluate(const QMap<QString, double>& parameters);

    /// Check if expression contains any parameters
    bool containsParameters() const;

    /// Get list of parameter names used in the expression
    QStringList usedParameters() const;

private:
    void parse();

    Type m_type = Type::Number;
    QString m_expression;
    double m_value = 0.0;
    bool m_valid = true;
    QString m_errorMessage;
    QStringList m_usedParams;
};

/// Line edit widget for editing parametric values
class FormulaEdit : public QLineEdit {
    Q_OBJECT

public:
    explicit FormulaEdit(QWidget* parent = nullptr);

    /// Set the list of available parameter names for autocomplete
    void setParameters(const QStringList& params);

    /// Set the current parameter values for evaluation
    void setParameterValues(const QMap<QString, double>& values);

    /// Set the unit suffix to display (e.g., "mm", "°")
    void setUnitSuffix(const QString& suffix);

    /// Get the current parametric value
    ParametricValue parametricValue() const;

    /// Set from a parametric value
    void setParametricValue(const ParametricValue& value);

    /// Get the evaluated numeric result
    double evaluatedValue() const;

    /// Check if current expression is valid
    bool isValid() const;

signals:
    /// Emitted when the value changes and is valid
    void valueChanged(double value);

    /// Emitted when validation state changes
    void validationChanged(bool valid, const QString& errorMessage);

protected:
    void paintEvent(QPaintEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private slots:
    void onTextChanged(const QString& text);

private:
    void updateValidation();
    void updateResultDisplay();

    QCompleter* m_completer = nullptr;
    QStringList m_parameters;
    QMap<QString, double> m_parameterValues;
    QString m_unitSuffix;
    ParametricValue m_value;
    QString m_resultDisplay;  // Shows evaluated result when editing formula
};

}  // namespace hobbycad

#endif  // HOBBYCAD_FORMULAEDIT_H
