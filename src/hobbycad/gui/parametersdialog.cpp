// =====================================================================
//  src/hobbycad/gui/parametersdialog.cpp — Change Parameters dialog
// =====================================================================

#include "parametersdialog.h"
#include "formulaedit.h"

#include <QAbstractItemDelegate>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace hobbycad {

ParametersDialog::ParametersDialog(QWidget* parent)
    : QDialog(parent)
    , m_defaultUnit(QStringLiteral("mm"))
{
    setWindowTitle(tr("Change Parameters"));
    setMinimumSize(700, 500);
    setupUi();
}

void ParametersDialog::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);

    // Filter bar
    auto* filterLayout = new QHBoxLayout();

    m_filterCombo = new QComboBox(this);
    m_filterCombo->addItem(tr("All Parameters"));
    m_filterCombo->addItem(tr("User Parameters"));
    m_filterCombo->addItem(tr("Model Parameters"));
    filterLayout->addWidget(m_filterCombo);

    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setPlaceholderText(tr("Filter parameters..."));
    m_filterEdit->setClearButtonEnabled(true);
    filterLayout->addWidget(m_filterEdit, 1);

    mainLayout->addLayout(filterLayout);

    // Parameters table
    m_table = new QTableWidget(this);
    m_table->setColumnCount(ColCount);
    m_table->setHorizontalHeaderLabels({
        tr("Name"),
        tr("Unit"),
        tr("Expression"),
        tr("Value"),
        tr("Comment")
    });

    // Column sizing
    m_table->horizontalHeader()->setSectionResizeMode(ColName, QHeaderView::Interactive);
    m_table->horizontalHeader()->setSectionResizeMode(ColUnit, QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(ColExpression, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(ColValue, QHeaderView::Interactive);
    m_table->horizontalHeader()->setSectionResizeMode(ColComment, QHeaderView::Stretch);

    m_table->setColumnWidth(ColName, 120);
    m_table->setColumnWidth(ColUnit, 60);
    m_table->setColumnWidth(ColValue, 100);

    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);

    mainLayout->addWidget(m_table, 1);

    // Action buttons
    auto* buttonLayout = new QHBoxLayout();

    m_addButton = new QPushButton(tr("+ Add"), this);
    m_addButton->setToolTip(tr("Add a new user parameter"));
    buttonLayout->addWidget(m_addButton);

    m_deleteButton = new QPushButton(tr("Delete"), this);
    m_deleteButton->setToolTip(tr("Delete the selected parameter"));
    m_deleteButton->setEnabled(false);
    buttonLayout->addWidget(m_deleteButton);

    buttonLayout->addStretch();

    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet(QStringLiteral("color: #666;"));
    buttonLayout->addWidget(m_statusLabel);

    mainLayout->addLayout(buttonLayout);

    // Dialog buttons
    auto* dialogButtons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply,
        this);
    m_okButton = dialogButtons->button(QDialogButtonBox::Ok);
    m_applyButton = dialogButtons->button(QDialogButtonBox::Apply);
    mainLayout->addWidget(dialogButtons);

    // Connections
    connect(m_filterEdit, &QLineEdit::textChanged,
            this, &ParametersDialog::onFilterChanged);
    connect(m_filterCombo, &QComboBox::currentIndexChanged,
            this, [this]() { onFilterChanged(m_filterEdit->text()); });

    connect(m_table, &QTableWidget::cellChanged,
            this, &ParametersDialog::onCellChanged);
    connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &ParametersDialog::onSelectionChanged);

    // Connect to item delegate for real-time validation while editing
    connect(m_table->itemDelegate(), &QAbstractItemDelegate::commitData,
            this, [this](QWidget* editor) {
        auto* lineEdit = qobject_cast<QLineEdit*>(editor);
        if (!lineEdit) return;

        int row = m_table->currentRow();
        int col = m_table->currentColumn();
        if (col == ColName) {
            validateNameCell(row, lineEdit->text());
        }
    });

    connect(m_addButton, &QPushButton::clicked,
            this, &ParametersDialog::onAddParameter);
    connect(m_deleteButton, &QPushButton::clicked,
            this, &ParametersDialog::onDeleteParameter);

    connect(dialogButtons, &QDialogButtonBox::accepted, this, [this]() {
        emit parametersChanged(m_parameters);
        accept();
    });
    connect(dialogButtons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(dialogButtons->button(QDialogButtonBox::Apply), &QPushButton::clicked,
            this, [this]() {
        emit parametersChanged(m_parameters);
    });

    updateStatusLabel();
}

void ParametersDialog::setParameters(const QList<Parameter>& params)
{
    m_parameters = params;

    // Sanitize parameter names that start with a digit by prefixing with underscore.
    // This handles manually edited project files with invalid parameter names.
    // TODO: When document serialization is implemented, this same sanitization
    // should also be applied during Document::loadParameters() to catch invalid
    // names before they reach the UI.
    for (auto& param : m_parameters) {
        if (!param.name.isEmpty() && param.name[0].isDigit()) {
            param.name = QLatin1Char('_') + param.name;
        }
    }

    refreshTable();
}

QList<Parameter> ParametersDialog::parameters() const
{
    return m_parameters;
}

void ParametersDialog::setDefaultUnit(const QString& unit)
{
    m_defaultUnit = unit;
}

void ParametersDialog::refreshTable()
{
    m_updating = true;

    // Clear error tracking when refreshing
    m_errorCells.clear();

    QString filter = m_filterEdit->text().toLower();
    int filterType = m_filterCombo->currentIndex();  // 0=all, 1=user, 2=model

    m_table->setRowCount(0);

    for (int i = 0; i < m_parameters.size(); ++i) {
        const Parameter& param = m_parameters[i];

        // Apply type filter
        if (filterType == 1 && !param.isUserParam) continue;
        if (filterType == 2 && param.isUserParam) continue;

        // Apply text filter
        if (!filter.isEmpty()) {
            bool matches = param.name.toLower().contains(filter) ||
                          param.expression.toLower().contains(filter) ||
                          param.comment.toLower().contains(filter);
            if (!matches) continue;
        }

        int row = m_table->rowCount();
        m_table->insertRow(row);

        // Name
        auto* nameItem = new QTableWidgetItem(param.name);
        nameItem->setData(Qt::UserRole, i);  // Store original index
        if (!param.isUserParam) {
            nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
            nameItem->setForeground(QColor(100, 100, 100));
        }
        m_table->setItem(row, ColName, nameItem);

        // Unit (combo box for user params)
        if (param.isUserParam) {
            auto* unitCombo = new QComboBox(m_table);
            unitCombo->addItems({
                QStringLiteral(""),      // No unit
                QStringLiteral("mm"),
                QStringLiteral("cm"),
                QStringLiteral("m"),
                QStringLiteral("in"),
                QStringLiteral("ft"),
                QStringLiteral("deg")
            });
            int idx = unitCombo->findText(param.unit);
            if (idx >= 0) unitCombo->setCurrentIndex(idx);

            connect(unitCombo, &QComboBox::currentTextChanged,
                    this, [this, row](const QString& text) {
                auto* item = m_table->item(row, ColName);
                if (!item) return;
                int paramIdx = item->data(Qt::UserRole).toInt();
                if (paramIdx >= 0 && paramIdx < m_parameters.size()) {
                    m_parameters[paramIdx].unit = text;
                }
            });

            m_table->setCellWidget(row, ColUnit, unitCombo);
        } else {
            auto* unitItem = new QTableWidgetItem(param.unit);
            unitItem->setFlags(unitItem->flags() & ~Qt::ItemIsEditable);
            unitItem->setForeground(QColor(100, 100, 100));
            m_table->setItem(row, ColUnit, unitItem);
        }

        // Expression
        auto* exprItem = new QTableWidgetItem(param.expression);
        if (!param.isUserParam) {
            exprItem->setFlags(exprItem->flags() & ~Qt::ItemIsEditable);
            exprItem->setForeground(QColor(100, 100, 100));
        }
        m_table->setItem(row, ColExpression, exprItem);

        // Value (read-only, evaluated)
        auto* valueItem = new QTableWidgetItem();
        valueItem->setFlags(valueItem->flags() & ~Qt::ItemIsEditable);
        valueItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);

        double val = evaluateExpression(param.expression);
        if (std::isnan(val)) {
            valueItem->setText(tr("Error"));
            valueItem->setForeground(Qt::red);
        } else {
            QString valStr = QString::number(val, 'g', 10);
            if (!param.unit.isEmpty()) {
                valStr += QStringLiteral(" ") + param.unit;
            }
            valueItem->setText(valStr);
        }
        m_table->setItem(row, ColValue, valueItem);

        // Comment
        auto* commentItem = new QTableWidgetItem(param.comment);
        if (!param.isUserParam) {
            commentItem->setFlags(commentItem->flags() & ~Qt::ItemIsEditable);
            commentItem->setForeground(QColor(100, 100, 100));
        }
        m_table->setItem(row, ColComment, commentItem);
    }

    m_updating = false;
    updateStatusLabel();
    updateSaveButtons();
}

void ParametersDialog::onAddParameter()
{
    // Generate unique name
    QString baseName = QStringLiteral("param");
    int num = 1;
    QString newName;
    do {
        newName = baseName + QString::number(num++);
    } while (std::any_of(m_parameters.begin(), m_parameters.end(),
                         [&newName](const Parameter& p) { return p.name == newName; }));

    Parameter param;
    param.name = newName;
    param.expression = QStringLiteral("0");
    param.value = 0.0;
    param.unit = m_defaultUnit;
    param.comment = QString();
    param.isUserParam = true;

    m_parameters.append(param);
    refreshTable();

    // Select and edit the new row
    int newRow = m_table->rowCount() - 1;
    m_table->selectRow(newRow);
    m_table->editItem(m_table->item(newRow, ColName));
}

void ParametersDialog::onDeleteParameter()
{
    int row = m_table->currentRow();
    if (row < 0) return;

    auto* item = m_table->item(row, ColName);
    if (!item) return;

    int paramIdx = item->data(Qt::UserRole).toInt();
    if (paramIdx < 0 || paramIdx >= m_parameters.size()) return;

    const Parameter& param = m_parameters[paramIdx];

    // Don't allow deleting model parameters
    if (!param.isUserParam) {
        QMessageBox::warning(this, tr("Cannot Delete"),
            tr("Model parameters cannot be deleted. They are defined by features."));
        return;
    }

    // Check if parameter is used by other expressions
    QString paramName = param.name;
    QStringList usedBy;
    for (const auto& p : m_parameters) {
        if (p.name != paramName && p.expression.contains(paramName)) {
            usedBy << p.name;
        }
    }

    if (!usedBy.isEmpty()) {
        auto result = QMessageBox::question(this, tr("Parameter In Use"),
            tr("Parameter '%1' is used by: %2\n\nDeleting it will cause errors. Continue?")
                .arg(paramName, usedBy.join(QStringLiteral(", "))),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (result != QMessageBox::Yes) return;
    }

    m_parameters.removeAt(paramIdx);
    refreshTable();
}

void ParametersDialog::onCellChanged(int row, int column)
{
    if (m_updating) return;

    auto* nameItem = m_table->item(row, ColName);
    if (!nameItem) return;

    int paramIdx = nameItem->data(Qt::UserRole).toInt();
    if (paramIdx < 0 || paramIdx >= m_parameters.size()) return;

    Parameter& param = m_parameters[paramIdx];

    switch (column) {
    case ColName: {
        QString newName = nameItem->text().trimmed();
        validateNameCell(row, newName);

        // Only update param name if valid
        if (!m_errorCells.contains(row) || !m_errorCells[row].contains(ColName)) {
            param.name = newName;
        }
        break;
    }

    case ColExpression: {
        auto* item = m_table->item(row, ColExpression);
        if (!item) return;

        QString newExpr = item->text().trimmed();
        param.expression = newExpr;

        // Re-evaluate value
        double val = evaluateExpression(newExpr);
        param.value = val;

        auto* valueItem = m_table->item(row, ColValue);
        if (valueItem) {
            m_updating = true;
            if (std::isnan(val)) {
                valueItem->setText(tr("Error"));
                valueItem->setForeground(Qt::red);
                showError(row, ColExpression, tr("Invalid expression"));
            } else {
                QString valStr = QString::number(val, 'g', 10);
                if (!param.unit.isEmpty()) {
                    valStr += QStringLiteral(" ") + param.unit;
                }
                valueItem->setText(valStr);
                valueItem->setForeground(QPalette().text().color());
                clearError(row, ColExpression);
            }
            m_updating = false;
        }

        // Update all dependent parameters
        updateParameterValues();
        break;
    }

    case ColComment: {
        auto* item = m_table->item(row, ColComment);
        if (item) {
            param.comment = item->text();
        }
        break;
    }
    }
}

void ParametersDialog::onSelectionChanged()
{
    int row = m_table->currentRow();
    bool canDelete = false;

    if (row >= 0) {
        auto* item = m_table->item(row, ColName);
        if (item) {
            int paramIdx = item->data(Qt::UserRole).toInt();
            if (paramIdx >= 0 && paramIdx < m_parameters.size()) {
                canDelete = m_parameters[paramIdx].isUserParam;
            }
        }
    }

    m_deleteButton->setEnabled(canDelete);
}

void ParametersDialog::updateParameterValues()
{
    // Build parameter map for evaluation
    QMap<QString, double> paramMap;
    for (const auto& p : m_parameters) {
        paramMap[p.name] = p.value;
    }

    // Re-evaluate all expressions with updated parameter values
    bool changed = true;
    int iterations = 0;
    const int maxIterations = 10;  // Prevent infinite loops

    while (changed && iterations++ < maxIterations) {
        changed = false;
        for (auto& param : m_parameters) {
            double newVal = evaluateExpression(param.expression);
            if (!std::isnan(newVal) && newVal != param.value) {
                param.value = newVal;
                paramMap[param.name] = newVal;
                changed = true;
            }
        }
    }

    // Update displayed values
    m_updating = true;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        auto* nameItem = m_table->item(row, ColName);
        if (!nameItem) continue;

        int paramIdx = nameItem->data(Qt::UserRole).toInt();
        if (paramIdx < 0 || paramIdx >= m_parameters.size()) continue;

        const Parameter& param = m_parameters[paramIdx];
        auto* valueItem = m_table->item(row, ColValue);
        if (valueItem) {
            double val = param.value;
            if (std::isnan(val)) {
                valueItem->setText(tr("Error"));
                valueItem->setForeground(Qt::red);
            } else {
                QString valStr = QString::number(val, 'g', 10);
                if (!param.unit.isEmpty()) {
                    valStr += QStringLiteral(" ") + param.unit;
                }
                valueItem->setText(valStr);
                valueItem->setForeground(QPalette().text().color());
            }
        }
    }
    m_updating = false;
}

void ParametersDialog::onFilterChanged(const QString& /*text*/)
{
    refreshTable();
}

double ParametersDialog::evaluateExpression(const QString& expr) const
{
    // Build parameter map
    QMap<QString, double> paramMap;
    for (const auto& p : m_parameters) {
        paramMap[p.name] = p.value;
    }

    // Use the expression evaluator from ParametricValue
    ParametricValue pv;
    pv.setExpression(expr);
    return pv.evaluate(paramMap);
}

bool ParametersDialog::isValidParameterName(const QString& name) const
{
    if (name.isEmpty()) return false;

    // Must start with letter or underscore
    QChar first = name[0];
    if (!first.isLetter() && first != QLatin1Char('_')) return false;

    // Rest must be letters, numbers, or underscores
    for (int i = 1; i < name.length(); ++i) {
        QChar c = name[i];
        if (!c.isLetterOrNumber() && c != QLatin1Char('_')) return false;
    }

    // Check for reserved words (math functions)
    static const QStringList reserved = {
        QStringLiteral("sin"), QStringLiteral("cos"), QStringLiteral("tan"),
        QStringLiteral("asin"), QStringLiteral("acos"), QStringLiteral("atan"),
        QStringLiteral("sqrt"), QStringLiteral("abs"), QStringLiteral("floor"),
        QStringLiteral("ceil"), QStringLiteral("round"), QStringLiteral("log"),
        QStringLiteral("log10"), QStringLiteral("exp"), QStringLiteral("pow"),
        QStringLiteral("min"), QStringLiteral("max"), QStringLiteral("pi"),
        QStringLiteral("e")
    };

    return !reserved.contains(name.toLower());
}

void ParametersDialog::showError(int row, int column, const QString& message)
{
    auto* item = m_table->item(row, column);
    if (item) {
        item->setBackground(QColor(255, 180, 180));  // Red background
        item->setToolTip(message);
    }

    // Track this error cell
    m_errorCells[row].insert(column);

    m_statusLabel->setText(message);
    m_statusLabel->setStyleSheet(QStringLiteral("color: red;"));

    updateSaveButtons();
}

void ParametersDialog::clearError(int row, int column)
{
    auto* item = m_table->item(row, column);
    if (item) {
        item->setBackground(QPalette().base().color());
        item->setToolTip(QString());
    }

    // Remove from error tracking
    if (m_errorCells.contains(row)) {
        m_errorCells[row].remove(column);
        if (m_errorCells[row].isEmpty()) {
            m_errorCells.remove(row);
        }
    }

    updateStatusLabel();
    updateSaveButtons();
}

void ParametersDialog::updateStatusLabel()
{
    // Don't update if there are errors - keep showing error message
    if (hasValidationErrors()) return;

    int userCount = 0;
    int modelCount = 0;
    for (const auto& p : m_parameters) {
        if (p.isUserParam) ++userCount;
        else ++modelCount;
    }

    m_statusLabel->setText(tr("%1 user, %2 model parameters").arg(userCount).arg(modelCount));
    m_statusLabel->setStyleSheet(QStringLiteral("color: #666;"));
}

void ParametersDialog::updateSaveButtons()
{
    bool canSave = !hasValidationErrors();
    if (m_okButton) m_okButton->setEnabled(canSave);
    if (m_applyButton) m_applyButton->setEnabled(canSave);
}

bool ParametersDialog::hasValidationErrors() const
{
    return !m_errorCells.isEmpty();
}

void ParametersDialog::validateNameCell(int row, const QString& text)
{
    QString name = text.trimmed();

    // Get the parameter index for this row
    auto* nameItem = m_table->item(row, ColName);
    if (!nameItem) return;
    int paramIdx = nameItem->data(Qt::UserRole).toInt();

    // Check for various validation errors
    if (name.isEmpty()) {
        showError(row, ColName, tr("Parameter name cannot be empty."));
        return;
    }

    if (name[0].isDigit()) {
        showError(row, ColName, tr("Parameter name cannot start with a digit."));
        return;
    }

    if (!name[0].isLetter() && name[0] != QLatin1Char('_')) {
        showError(row, ColName, tr("Parameter name must start with a letter or underscore."));
        return;
    }

    // Check remaining characters
    for (int i = 1; i < name.length(); ++i) {
        QChar c = name[i];
        if (!c.isLetterOrNumber() && c != QLatin1Char('_')) {
            showError(row, ColName, tr("Invalid character '%1' in parameter name.").arg(c));
            return;
        }
    }

    // Check for reserved words
    static const QStringList reserved = {
        QStringLiteral("sin"), QStringLiteral("cos"), QStringLiteral("tan"),
        QStringLiteral("asin"), QStringLiteral("acos"), QStringLiteral("atan"),
        QStringLiteral("sqrt"), QStringLiteral("abs"), QStringLiteral("floor"),
        QStringLiteral("ceil"), QStringLiteral("round"), QStringLiteral("log"),
        QStringLiteral("log10"), QStringLiteral("exp"), QStringLiteral("pow"),
        QStringLiteral("min"), QStringLiteral("max"), QStringLiteral("pi"),
        QStringLiteral("e")
    };
    if (reserved.contains(name.toLower())) {
        showError(row, ColName, tr("'%1' is a reserved word and cannot be used as a parameter name.").arg(name));
        return;
    }

    // Check for duplicates
    for (int i = 0; i < m_parameters.size(); ++i) {
        if (i != paramIdx && m_parameters[i].name == name) {
            showError(row, ColName, tr("Parameter '%1' already exists.").arg(name));
            return;
        }
    }

    // All checks passed
    clearError(row, ColName);
}

}  // namespace hobbycad
