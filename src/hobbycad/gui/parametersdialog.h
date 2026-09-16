// =====================================================================
//  src/hobbycad/gui/parametersdialog.h — Change Parameters dialog
// =====================================================================
//
//  A dialog for managing document parameters (named variables).
//  Shows all parameters in a table with name, expression, evaluated
//  value, unit, and comment.
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_PARAMETERSDIALOG_H
#define HOBBYCAD_PARAMETERSDIALOG_H

#include <hobbycad/parameters.h>

#include <QDialog>
#include <QMap>
#include <QSet>
#include <QString>

class QTableWidget;

namespace hobbycad { class ErrorOutlineDelegate; }
class QWidget;
class QLineEdit;
class QPushButton;
class QComboBox;
class QLabel;

namespace hobbycad {

// Parameter struct is now in <hobbycad/parameters.h>

class ParametersDialog : public QDialog {
    Q_OBJECT

public:
    explicit ParametersDialog(QWidget* parent = nullptr);

    /// Set the list of parameters to display
    void setParameters(const QList<Parameter>& params);

    /// Get the current list of parameters
    QList<Parameter> parameters() const;

    /// Set the default unit for new parameters
    void setDefaultUnit(const QString& unit);

signals:
    /// Emitted when parameters are changed and applied
    void parametersChanged(const QList<Parameter>& params);

private slots:
    void onAddParameter();
    void onAddReferenceParameter();
    void onDeleteParameter();
    void onCellChanged(int row, int column);
    void onSelectionChanged();
    void updateParameterValues();
    void onFilterChanged(const QString& text);

private:
    void setupUi();
    void refreshTable();
    double evaluateExpression(const QString& expr) const;
    bool isValidParameterName(const QString& name) const;
    void showError(int row, int column, const QString& message);
    void clearError(int row, int column);
    void updateStatusLabel();
    void updateSaveButtons();
    bool hasValidationErrors() const;
    void validateNameCell(int row, const QString& text);

    /// Reject an invalid name: keep the cursor in the cell, shake the
    /// editor, and leave what the user typed there to be corrected.
    ///
    /// Committing an invalid name and silently reverting it is the worst of
    /// the options: the typing disappears with no explanation of which
    /// rule it broke.
    void rejectNameEdit(int row);


    QList<Parameter> m_parameters;
    QString m_defaultUnit;

    // UI elements
    QLineEdit* m_filterEdit = nullptr;
    QComboBox* m_filterCombo = nullptr;
    QTableWidget* m_table = nullptr;
    ErrorOutlineDelegate* m_outlineDelegate = nullptr;
    QPushButton* m_addButton = nullptr;
    QPushButton* m_addReferenceButton = nullptr;
    QPushButton* m_deleteButton = nullptr;
    QLabel* m_statusLabel = nullptr;
    QPushButton* m_okButton = nullptr;
    QPushButton* m_applyButton = nullptr;

    // Track cells with validation errors (row -> set of columns)
    QMap<int, QSet<int>> m_errorCells;

    // Column indices
    enum Column {
        ColName = 0,
        ColUnit,
        ColExpression,
        ColValue,
        ColComment,
        ColCount
    };

    bool m_updating = false;  // Prevent recursive updates
};

}  // namespace hobbycad

#endif  // HOBBYCAD_PARAMETERSDIALOG_H
