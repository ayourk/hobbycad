// =====================================================================
//  src/hobbycad/gui/formulaedit.cpp — Formula editor widget
// =====================================================================

#include "formulaedit.h"

#include <QCompleter>
#include <QPainter>
#include <QRegularExpression>
#include <QStringListModel>
#include <QStyle>

#include <cmath>
#include <stack>

namespace hobbycad {

// ---- ParametricValue ------------------------------------------------

ParametricValue::ParametricValue(double value)
    : m_type(Type::Number)
    , m_expression(QString::number(value))
    , m_value(value)
    , m_valid(true)
{
}

ParametricValue::ParametricValue(const QString& expression)
    : m_expression(expression.trimmed())
{
    parse();
}

void ParametricValue::setExpression(const QString& expr)
{
    m_expression = expr.trimmed();
    parse();
}

void ParametricValue::parse()
{
    m_valid = true;
    m_errorMessage.clear();
    m_usedParams.clear();

    if (m_expression.isEmpty()) {
        m_type = Type::Number;
        m_value = 0.0;
        return;
    }

    // Try to parse as a plain number first
    bool ok;
    double num = m_expression.toDouble(&ok);
    if (ok) {
        m_type = Type::Number;
        m_value = num;
        return;
    }

    // Check if it's a single parameter name (identifier only)
    static QRegularExpression identifierRx(QStringLiteral("^[a-zA-Z_][a-zA-Z0-9_]*$"));
    if (identifierRx.match(m_expression).hasMatch()) {
        m_type = Type::Parameter;
        m_usedParams.append(m_expression);
        // Value will be resolved when evaluate() is called
        return;
    }

    // Otherwise it's a formula - extract parameter names
    m_type = Type::Formula;
    static QRegularExpression paramRx(QStringLiteral("\\b([a-zA-Z_][a-zA-Z0-9_]*)\\b"));
    auto it = paramRx.globalMatch(m_expression);
    while (it.hasNext()) {
        auto match = it.next();
        QString name = match.captured(1);
        // Skip known function names
        static QStringList functions = {
            QStringLiteral("sin"), QStringLiteral("cos"), QStringLiteral("tan"),
            QStringLiteral("sqrt"), QStringLiteral("abs"), QStringLiteral("floor"),
            QStringLiteral("ceil"), QStringLiteral("round"), QStringLiteral("min"),
            QStringLiteral("max"), QStringLiteral("pow"), QStringLiteral("log"),
            QStringLiteral("exp"), QStringLiteral("pi")
        };
        if (!functions.contains(name.toLower()) && !m_usedParams.contains(name)) {
            m_usedParams.append(name);
        }
    }
}

bool ParametricValue::containsParameters() const
{
    return !m_usedParams.isEmpty();
}

QStringList ParametricValue::usedParameters() const
{
    return m_usedParams;
}

// Simple expression evaluator using shunting-yard algorithm
class ExpressionEvaluator {
public:
    ExpressionEvaluator(const QMap<QString, double>& params)
        : m_params(params)
    {
    }

    bool evaluate(const QString& expr, double& result, QString& error)
    {
        m_error.clear();
        m_pos = 0;
        m_expr = expr;

        try {
            result = parseExpression();
            if (m_pos < m_expr.length()) {
                error = QStringLiteral("Unexpected character at position %1").arg(m_pos);
                return false;
            }
            return true;
        } catch (const std::exception& e) {
            error = QString::fromLatin1(e.what());
            return false;
        }
    }

private:
    void skipWhitespace()
    {
        while (m_pos < m_expr.length() && m_expr[m_pos].isSpace())
            ++m_pos;
    }

    double parseExpression()
    {
        return parseAddSub();
    }

    double parseAddSub()
    {
        double left = parseMulDiv();
        skipWhitespace();

        while (m_pos < m_expr.length()) {
            QChar op = m_expr[m_pos];
            if (op != '+' && op != '-')
                break;
            ++m_pos;
            double right = parseMulDiv();
            if (op == '+')
                left += right;
            else
                left -= right;
            skipWhitespace();
        }
        return left;
    }

    double parseMulDiv()
    {
        double left = parsePower();
        skipWhitespace();

        while (m_pos < m_expr.length()) {
            QChar op = m_expr[m_pos];
            if (op != '*' && op != '/')
                break;
            ++m_pos;
            double right = parsePower();
            if (op == '*')
                left *= right;
            else {
                if (right == 0.0)
                    throw std::runtime_error("Division by zero");
                left /= right;
            }
            skipWhitespace();
        }
        return left;
    }

    double parsePower()
    {
        double base = parseUnary();
        skipWhitespace();

        if (m_pos < m_expr.length() && m_expr[m_pos] == '^') {
            ++m_pos;
            double exp = parsePower();  // Right associative
            return std::pow(base, exp);
        }
        return base;
    }

    double parseUnary()
    {
        skipWhitespace();
        if (m_pos < m_expr.length()) {
            if (m_expr[m_pos] == '-') {
                ++m_pos;
                return -parseUnary();
            }
            if (m_expr[m_pos] == '+') {
                ++m_pos;
                return parseUnary();
            }
        }
        return parsePrimary();
    }

    double parsePrimary()
    {
        skipWhitespace();

        if (m_pos >= m_expr.length())
            throw std::runtime_error("Unexpected end of expression");

        // Parentheses
        if (m_expr[m_pos] == '(') {
            ++m_pos;
            double result = parseExpression();
            skipWhitespace();
            if (m_pos >= m_expr.length() || m_expr[m_pos] != ')')
                throw std::runtime_error("Missing closing parenthesis");
            ++m_pos;
            return result;
        }

        // Number
        if (m_expr[m_pos].isDigit() || m_expr[m_pos] == '.') {
            return parseNumber();
        }

        // Identifier (parameter or function)
        if (m_expr[m_pos].isLetter() || m_expr[m_pos] == '_') {
            return parseIdentifier();
        }

        throw std::runtime_error(
            QString("Unexpected character '%1'").arg(m_expr[m_pos]).toStdString());
    }

    double parseNumber()
    {
        int start = m_pos;
        while (m_pos < m_expr.length() &&
               (m_expr[m_pos].isDigit() || m_expr[m_pos] == '.'))
            ++m_pos;

        QString numStr = m_expr.mid(start, m_pos - start);
        bool ok;
        double num = numStr.toDouble(&ok);
        if (!ok)
            throw std::runtime_error(
                QString("Invalid number '%1'").arg(numStr).toStdString());
        return num;
    }

    double parseIdentifier()
    {
        int start = m_pos;
        while (m_pos < m_expr.length() &&
               (m_expr[m_pos].isLetterOrNumber() || m_expr[m_pos] == '_'))
            ++m_pos;

        QString name = m_expr.mid(start, m_pos - start);
        skipWhitespace();

        // Check for function call
        if (m_pos < m_expr.length() && m_expr[m_pos] == '(') {
            return parseFunction(name);
        }

        // Built-in constants
        if (name.toLower() == QStringLiteral("pi"))
            return M_PI;
        if (name.toLower() == QStringLiteral("e"))
            return M_E;

        // Parameter lookup
        if (m_params.contains(name))
            return m_params[name];

        throw std::runtime_error(
            QString("Unknown parameter '%1'").arg(name).toStdString());
    }

    double parseFunction(const QString& name)
    {
        ++m_pos;  // Skip '('

        QList<double> args;
        skipWhitespace();

        if (m_pos < m_expr.length() && m_expr[m_pos] != ')') {
            args.append(parseExpression());
            skipWhitespace();

            while (m_pos < m_expr.length() && m_expr[m_pos] == ',') {
                ++m_pos;
                args.append(parseExpression());
                skipWhitespace();
            }
        }

        if (m_pos >= m_expr.length() || m_expr[m_pos] != ')')
            throw std::runtime_error("Missing closing parenthesis in function call");
        ++m_pos;

        return callFunction(name, args);
    }

    double callFunction(const QString& name, const QList<double>& args)
    {
        QString fn = name.toLower();

        if (fn == QStringLiteral("sin") && args.size() == 1)
            return std::sin(args[0] * M_PI / 180.0);  // Degrees
        if (fn == QStringLiteral("cos") && args.size() == 1)
            return std::cos(args[0] * M_PI / 180.0);
        if (fn == QStringLiteral("tan") && args.size() == 1)
            return std::tan(args[0] * M_PI / 180.0);
        if (fn == QStringLiteral("sqrt") && args.size() == 1)
            return std::sqrt(args[0]);
        if (fn == QStringLiteral("abs") && args.size() == 1)
            return std::abs(args[0]);
        if (fn == QStringLiteral("floor") && args.size() == 1)
            return std::floor(args[0]);
        if (fn == QStringLiteral("ceil") && args.size() == 1)
            return std::ceil(args[0]);
        if (fn == QStringLiteral("round") && args.size() == 1)
            return std::round(args[0]);
        if (fn == QStringLiteral("min") && args.size() == 2)
            return std::min(args[0], args[1]);
        if (fn == QStringLiteral("max") && args.size() == 2)
            return std::max(args[0], args[1]);
        if (fn == QStringLiteral("pow") && args.size() == 2)
            return std::pow(args[0], args[1]);
        if (fn == QStringLiteral("log") && args.size() == 1)
            return std::log(args[0]);
        if (fn == QStringLiteral("exp") && args.size() == 1)
            return std::exp(args[0]);

        throw std::runtime_error(
            QString("Unknown function '%1' or wrong number of arguments").arg(name).toStdString());
    }

    QMap<QString, double> m_params;
    QString m_expr;
    int m_pos = 0;
    QString m_error;
};

bool ParametricValue::evaluate(const QMap<QString, double>& parameters)
{
    if (m_type == Type::Number) {
        // Already a plain number
        return true;
    }

    ExpressionEvaluator eval(parameters);
    double result;
    QString error;

    if (eval.evaluate(m_expression, result, error)) {
        m_value = result;
        m_valid = true;
        m_errorMessage.clear();
        return true;
    } else {
        m_valid = false;
        m_errorMessage = error;
        return false;
    }
}

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
    setText(value.expression());
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
    m_value.setExpression(text);
    updateValidation();
}

void FormulaEdit::updateValidation()
{
    m_value.evaluate(m_parameterValues);

    // Update dynamic properties for styling
    setProperty("hasFormula", m_value.type() != ParametricValue::Type::Number);
    setProperty("hasError", !m_value.isValid());
    style()->unpolish(this);
    style()->polish(this);

    updateResultDisplay();

    emit validationChanged(m_value.isValid(), m_value.errorMessage());

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
