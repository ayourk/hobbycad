// =====================================================================
//  src/hobbycad/gui/formuladialog.h — Formula editor dialog
// =====================================================================
//
//  A dialog for editing parametric formulas with:
//  - Large text area for complex formulas
//  - Parameter list with click-to-insert
//  - Live result preview
//  - Error display
//  - Function reference
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_FORMULADIALOG_H
#define HOBBYCAD_FORMULADIALOG_H

#include "formulaedit.h"

#include <QDialog>
#include <QMap>
#include <QString>

class QLabel;
class QLineEdit;
class QListWidget;
class QTextEdit;

namespace hobbycad {

class FormulaDialog : public QDialog {
    Q_OBJECT

public:
    explicit FormulaDialog(QWidget* parent = nullptr);

    /// Set the property name being edited (shown in title)
    void setPropertyName(const QString& name);

    /// Set the unit suffix for result display
    void setUnitSuffix(const QString& suffix);

    /// Set available parameters and their current values
    void setParameters(const QMap<QString, double>& params);

    /// Get/set the formula expression
    QString expression() const;
    void setExpression(const QString& expr);

    /// Get the evaluated result (if valid)
    double evaluatedValue() const;

    /// Check if current expression is valid
    bool isValid() const;

private slots:
    void onExpressionChanged();
    void onParameterDoubleClicked();

private:
    void setupUi();
    void updateResult();
    void populateParameterList();
    void populateFunctionList();

    QString m_propertyName;
    QString m_unitSuffix;
    QMap<QString, double> m_parameters;
    ParametricValue m_value;

    // UI elements
    QTextEdit* m_expressionEdit = nullptr;
    QLabel* m_resultLabel = nullptr;
    QLabel* m_errorLabel = nullptr;
    QListWidget* m_parameterList = nullptr;
    QListWidget* m_functionList = nullptr;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_FORMULADIALOG_H
