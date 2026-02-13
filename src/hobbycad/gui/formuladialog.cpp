// =====================================================================
//  src/hobbycad/gui/formuladialog.cpp — Formula editor dialog
// =====================================================================

#include "formuladialog.h"

#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSplitter>
#include <QTextEdit>
#include <QVBoxLayout>

namespace hobbycad {

FormulaDialog::FormulaDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Formula Editor"));
    setMinimumSize(500, 400);
    setupUi();
}

void FormulaDialog::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);

    // Expression input area
    auto* exprGroup = new QGroupBox(tr("Expression"));
    auto* exprLayout = new QVBoxLayout(exprGroup);

    m_expressionEdit = new QTextEdit();
    m_expressionEdit->setPlaceholderText(tr("Enter a number, parameter name, or formula..."));
    m_expressionEdit->setMaximumHeight(80);
    m_expressionEdit->setTabChangesFocus(true);
    exprLayout->addWidget(m_expressionEdit);

    // Result display
    auto* resultLayout = new QHBoxLayout();
    resultLayout->addWidget(new QLabel(tr("Result:")));
    m_resultLabel = new QLabel(tr("—"));
    m_resultLabel->setStyleSheet(QStringLiteral(
        "QLabel { font-weight: bold; font-size: 14px; }"));
    resultLayout->addWidget(m_resultLabel);
    resultLayout->addStretch();
    exprLayout->addLayout(resultLayout);

    // Error display
    m_errorLabel = new QLabel();
    m_errorLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: #cc0000; }"));
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setVisible(false);
    exprLayout->addWidget(m_errorLabel);

    mainLayout->addWidget(exprGroup);

    // Parameters and functions side by side
    auto* splitter = new QSplitter(Qt::Horizontal);

    // Parameters list
    auto* paramGroup = new QGroupBox(tr("Parameters"));
    auto* paramLayout = new QVBoxLayout(paramGroup);
    m_parameterList = new QListWidget();
    m_parameterList->setToolTip(tr("Double-click to insert parameter"));
    paramLayout->addWidget(m_parameterList);
    splitter->addWidget(paramGroup);

    // Functions list
    auto* funcGroup = new QGroupBox(tr("Functions"));
    auto* funcLayout = new QVBoxLayout(funcGroup);
    m_functionList = new QListWidget();
    m_functionList->setToolTip(tr("Double-click to insert function"));
    funcLayout->addWidget(m_functionList);
    splitter->addWidget(funcGroup);

    splitter->setSizes({250, 250});
    mainLayout->addWidget(splitter, 1);

    // Dialog buttons
    auto* buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);

    // Connect signals
    connect(m_expressionEdit, &QTextEdit::textChanged,
            this, &FormulaDialog::onExpressionChanged);
    connect(m_parameterList, &QListWidget::itemDoubleClicked,
            this, &FormulaDialog::onParameterDoubleClicked);
    connect(m_functionList, &QListWidget::itemDoubleClicked,
            this, [this](QListWidgetItem* item) {
        // Insert function with parentheses
        QString func = item->data(Qt::UserRole).toString();
        QTextCursor cursor = m_expressionEdit->textCursor();
        cursor.insertText(func);
        m_expressionEdit->setFocus();
    });

    populateFunctionList();
}

void FormulaDialog::setPropertyName(const QString& name)
{
    m_propertyName = name;
    setWindowTitle(tr("Formula Editor — %1").arg(name));
}

void FormulaDialog::setUnitSuffix(const QString& suffix)
{
    m_unitSuffix = suffix;
    updateResult();
}

void FormulaDialog::setParameters(const QMap<QString, double>& params)
{
    m_parameters = params;
    populateParameterList();
    updateResult();
}

QString FormulaDialog::expression() const
{
    return m_expressionEdit->toPlainText().trimmed();
}

void FormulaDialog::setExpression(const QString& expr)
{
    m_expressionEdit->setPlainText(expr);
}

double FormulaDialog::evaluatedValue() const
{
    return m_value.value();
}

bool FormulaDialog::isValid() const
{
    return m_value.isValid();
}

void FormulaDialog::onExpressionChanged()
{
    m_value.setExpression(expression());
    updateResult();
}

void FormulaDialog::onParameterDoubleClicked()
{
    QListWidgetItem* item = m_parameterList->currentItem();
    if (!item)
        return;

    QString paramName = item->data(Qt::UserRole).toString();
    QTextCursor cursor = m_expressionEdit->textCursor();
    cursor.insertText(paramName);
    m_expressionEdit->setFocus();
}

void FormulaDialog::updateResult()
{
    m_value.evaluate(m_parameters);

    if (m_value.isValid()) {
        QString resultText;
        if (m_unitSuffix.isEmpty()) {
            resultText = QString::number(m_value.value(), 'g', 10);
        } else {
            resultText = QStringLiteral("%1 %2")
                .arg(m_value.value(), 0, 'g', 10)
                .arg(m_unitSuffix);
        }
        m_resultLabel->setText(resultText);
        m_resultLabel->setStyleSheet(QStringLiteral(
            "QLabel { font-weight: bold; font-size: 14px; color: #000; }"));
        m_errorLabel->setVisible(false);
    } else {
        m_resultLabel->setText(tr("Error"));
        m_resultLabel->setStyleSheet(QStringLiteral(
            "QLabel { font-weight: bold; font-size: 14px; color: #cc0000; }"));
        m_errorLabel->setText(m_value.errorMessage());
        m_errorLabel->setVisible(true);
    }
}

void FormulaDialog::populateParameterList()
{
    m_parameterList->clear();

    for (auto it = m_parameters.constBegin(); it != m_parameters.constEnd(); ++it) {
        QString display = QStringLiteral("%1 = %2").arg(it.key()).arg(it.value());
        auto* item = new QListWidgetItem(display);
        item->setData(Qt::UserRole, it.key());
        item->setToolTip(tr("Double-click to insert '%1'").arg(it.key()));
        m_parameterList->addItem(item);
    }

    if (m_parameters.isEmpty()) {
        auto* item = new QListWidgetItem(tr("(No parameters defined)"));
        item->setFlags(Qt::NoItemFlags);
        item->setForeground(Qt::gray);
        m_parameterList->addItem(item);
    }
}

void FormulaDialog::populateFunctionList()
{
    // Math functions with descriptions
    struct FuncInfo {
        const char* name;
        const char* syntax;
        const char* description;
    };

    static const FuncInfo functions[] = {
        {"sin",   "sin(angle)",     "Sine (angle in degrees)"},
        {"cos",   "cos(angle)",     "Cosine (angle in degrees)"},
        {"tan",   "tan(angle)",     "Tangent (angle in degrees)"},
        {"sqrt",  "sqrt(x)",        "Square root"},
        {"abs",   "abs(x)",         "Absolute value"},
        {"floor", "floor(x)",       "Round down to integer"},
        {"ceil",  "ceil(x)",        "Round up to integer"},
        {"round", "round(x)",       "Round to nearest integer"},
        {"min",   "min(a, b)",      "Minimum of two values"},
        {"max",   "max(a, b)",      "Maximum of two values"},
        {"pow",   "pow(base, exp)", "Power (base^exp)"},
        {"log",   "log(x)",         "Natural logarithm"},
        {"exp",   "exp(x)",         "e raised to power x"},
        {"pi",    "pi",             "Constant: 3.14159..."},
    };

    for (const auto& func : functions) {
        QString display = QStringLiteral("%1 — %2")
            .arg(QString::fromLatin1(func.syntax))
            .arg(tr(func.description));
        auto* item = new QListWidgetItem(display);
        item->setData(Qt::UserRole, QString::fromLatin1(func.syntax));
        item->setToolTip(tr(func.description));
        m_functionList->addItem(item);
    }
}

}  // namespace hobbycad
