// =====================================================================
//  src/hobbycad/gui/formulafield.cpp — Formula field with fx button
// =====================================================================

#include "formulafield.h"
#include "formuladialog.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QToolButton>

namespace hobbycad {

// Clickable label that emits doubleClicked signal
class ClickableLabel : public QLabel {
public:
    using QLabel::QLabel;

    std::function<void()> onDoubleClick;

protected:
    void mouseDoubleClickEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && onDoubleClick) {
            onDoubleClick();
        }
        QLabel::mouseDoubleClickEvent(event);
    }
};

FormulaField::FormulaField(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    // Value/formula display label
    auto* clickableLabel = new ClickableLabel();
    clickableLabel->onDoubleClick = [this]() { onLabelDoubleClicked(); };
    m_valueLabel = clickableLabel;
    m_valueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_valueLabel->setCursor(Qt::IBeamCursor);
    layout->addWidget(m_valueLabel, 1);

    // fx button
    m_fxButton = new QToolButton();
    m_fxButton->setText(QStringLiteral("fx"));
    m_fxButton->setToolTip(tr("Edit formula..."));
    m_fxButton->setFixedSize(24, 20);
    m_fxButton->setStyleSheet(QStringLiteral(
        "QToolButton {"
        "  background: #e8e8e8;"
        "  border: 1px solid #aaa;"
        "  border-radius: 2px;"
        "  font-size: 10px;"
        "  font-style: italic;"
        "  font-weight: bold;"
        "  color: #444;"
        "}"
        "QToolButton:hover {"
        "  background: #d0d0d0;"
        "  border-color: #888;"
        "}"
        "QToolButton:pressed {"
        "  background: #c0c0c0;"
        "}"
    ));
    connect(m_fxButton, &QToolButton::clicked,
            this, &FormulaField::onFxButtonClicked);
    layout->addWidget(m_fxButton);

    updateDisplay();
}

void FormulaField::setPropertyName(const QString& name)
{
    m_propertyName = name;
}

void FormulaField::setUnitSuffix(const QString& suffix)
{
    m_unitSuffix = suffix;
    updateDisplay();
}

void FormulaField::setParameters(const QMap<QString, double>& params)
{
    m_parameters = params;
    m_value.evaluate(m_parameters);
    updateDisplay();
}

QString FormulaField::expression() const
{
    return m_value.expression();
}

void FormulaField::setExpression(const QString& expr)
{
    m_value.setExpression(expr);
    m_value.evaluate(m_parameters);
    updateDisplay();
}

ParametricValue FormulaField::parametricValue() const
{
    return m_value;
}

double FormulaField::evaluatedValue() const
{
    return m_value.value();
}

bool FormulaField::isValid() const
{
    return m_value.isValid();
}

bool FormulaField::isFormula() const
{
    return m_value.type() != ParametricValue::Type::Number;
}

void FormulaField::onFxButtonClicked()
{
    FormulaDialog dlg(this);
    dlg.setPropertyName(m_propertyName);
    dlg.setUnitSuffix(m_unitSuffix);
    dlg.setParameters(m_parameters);
    dlg.setExpression(m_value.expression());

    if (dlg.exec() == QDialog::Accepted) {
        QString newExpr = dlg.expression();
        if (newExpr != m_value.expression()) {
            m_value.setExpression(newExpr);
            m_value.evaluate(m_parameters);
            updateDisplay();
            emit expressionChanged(newExpr);
            if (m_value.isValid()) {
                emit valueChanged(m_value.value());
            }
        }
    }
}

void FormulaField::onLabelDoubleClicked()
{
    // Double-clicking the label also opens the formula editor
    onFxButtonClicked();
}

void FormulaField::updateDisplay()
{
    QString displayText;
    QString styleSheet;

    if (m_value.type() == ParametricValue::Type::Number) {
        // Plain number - show value with units
        if (m_unitSuffix.isEmpty()) {
            displayText = QString::number(m_value.value(), 'g', 10);
        } else {
            displayText = QStringLiteral("%1 %2")
                .arg(m_value.value(), 0, 'g', 10)
                .arg(m_unitSuffix);
        }
        styleSheet = QStringLiteral("QLabel { color: #000; }");
    } else {
        // Formula or parameter - show expression (no units)
        displayText = m_value.expression();

        if (m_value.isValid()) {
            // Valid formula - show in blue with result tooltip
            styleSheet = QStringLiteral("QLabel { color: #2a6fdb; }");
            QString tooltip;
            if (m_unitSuffix.isEmpty()) {
                tooltip = QStringLiteral("= %1").arg(m_value.value(), 0, 'g', 10);
            } else {
                tooltip = QStringLiteral("= %1 %2")
                    .arg(m_value.value(), 0, 'g', 10)
                    .arg(m_unitSuffix);
            }
            m_valueLabel->setToolTip(tooltip);
        } else {
            // Invalid formula - show in red
            styleSheet = QStringLiteral("QLabel { color: #cc0000; }");
            m_valueLabel->setToolTip(m_value.errorMessage());
        }
    }

    m_valueLabel->setText(displayText);
    m_valueLabel->setStyleSheet(styleSheet);
}

}  // namespace hobbycad
