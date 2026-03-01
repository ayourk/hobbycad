// =====================================================================
//  src/hobbycad/gui/formulaedit.cpp — Formula editor widget
// =====================================================================

#include "formulaedit.h"

#include <map>
#include <string>

#include <QCompleter>
#include <QPainter>
#include <QStringListModel>
#include <QStyle>

namespace hobbycad {

// ---- FormulaEdit ----------------------------------------------------

FormulaEdit::FormulaEdit(QWidget* parent)
    : QLineEdit(parent)
{
    m_completer = new QCompleter(this);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setCompletionMode(QCompleter::PopupCompletion);
    setCompleter(m_completer);

    connect(this, &QLineEdit::textChanged,
            this, &FormulaEdit::onTextChanged);

    // Style for formula/parameter values
    setStyleSheet(QStringLiteral(
        "FormulaEdit[hasFormula=\"true\"] { color: #4a90d9; }"
        "FormulaEdit[hasError=\"true\"] { background-color: #ffcccc; }"
    ));
}

void FormulaEdit::setParameters(const QStringList& params)
{
    m_parameters = params;
    auto* model = new QStringListModel(params, m_completer);
    m_completer->setModel(model);
}

void FormulaEdit::setParameterValues(const QMap<QString, double>& values)
{
    m_parameterValues = values;
    updateValidation();
}

void FormulaEdit::setUnitSuffix(const QString& suffix)
{
    m_unitSuffix = suffix;
    updateResultDisplay();
}

ParametricValue FormulaEdit::parametricValue() const
{
    return m_value;
}

void FormulaEdit::setParametricValue(const ParametricValue& value)
{
    m_value = value;
    setText(QString::fromStdString(value.expression()));
    updateValidation();
}

double FormulaEdit::evaluatedValue() const
{
    return m_value.value();
}

bool FormulaEdit::isValid() const
{
    return m_value.isValid();
}

void FormulaEdit::onTextChanged(const QString& text)
{
    m_value.setExpression(text.toStdString());
    updateValidation();
}

void FormulaEdit::updateValidation()
{
    // Convert QMap<QString,double> to std::map<std::string,double> for library
    std::map<std::string, double> stdParams;
    for (auto it = m_parameterValues.constBegin(); it != m_parameterValues.constEnd(); ++it) {
        stdParams[it.key().toStdString()] = it.value();
    }
    m_value.evaluate(stdParams);

    // Update dynamic properties for styling
    setProperty("hasFormula", m_value.type() != ParametricValue::Type::Number);
    setProperty("hasError", !m_value.isValid());
    style()->unpolish(this);
    style()->polish(this);

    updateResultDisplay();

    emit validationChanged(m_value.isValid(), QString::fromStdString(m_value.errorMessage()));

    if (m_value.isValid()) {
        emit valueChanged(m_value.value());
    }
}

void FormulaEdit::updateResultDisplay()
{
    if (m_value.type() == ParametricValue::Type::Number) {
        m_resultDisplay.clear();
    } else if (m_value.isValid()) {
        // Show computed result for formulas/parameters
        if (m_unitSuffix.isEmpty()) {
            m_resultDisplay = QStringLiteral(" = %1").arg(m_value.value());
        } else {
            m_resultDisplay = QStringLiteral(" = %1 %2").arg(m_value.value()).arg(m_unitSuffix);
        }
    } else {
        m_resultDisplay = QStringLiteral(" (error)");
    }

    update();  // Trigger repaint
}

void FormulaEdit::paintEvent(QPaintEvent* event)
{
    QLineEdit::paintEvent(event);

    // Draw result display on the right side when editing a formula
    if (!m_resultDisplay.isEmpty() && hasFocus()) {
        QPainter painter(this);
        painter.setPen(m_value.isValid() ? QColor("#888") : QColor("#cc0000"));

        QFont font = this->font();
        font.setItalic(true);
        painter.setFont(font);

        QRect textRect = rect();
        textRect.setRight(textRect.right() - 4);
        painter.drawText(textRect, Qt::AlignRight | Qt::AlignVCenter, m_resultDisplay);
    }
}

void FormulaEdit::focusInEvent(QFocusEvent* event)
{
    QLineEdit::focusInEvent(event);
    updateResultDisplay();
}

void FormulaEdit::focusOutEvent(QFocusEvent* event)
{
    QLineEdit::focusOutEvent(event);
    m_resultDisplay.clear();
    update();
}

}  // namespace hobbycad
