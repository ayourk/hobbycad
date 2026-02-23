// =====================================================================
//  src/libhobbycad/parameters.cpp — Parametric value engine
// =====================================================================
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include <hobbycad/parameters.h>

#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <QRegularExpression>

#include <stack>
#include <stdexcept>

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
            QStringLiteral("asin"), QStringLiteral("acos"), QStringLiteral("atan"),
            QStringLiteral("atan2"), QStringLiteral("sinr"), QStringLiteral("cosr"),
            QStringLiteral("tanr"), QStringLiteral("sqrt"), QStringLiteral("abs"),
            QStringLiteral("floor"), QStringLiteral("ceil"), QStringLiteral("round"),
            QStringLiteral("min"), QStringLiteral("max"), QStringLiteral("pow"),
            QStringLiteral("log"), QStringLiteral("log10"), QStringLiteral("log2"),
            QStringLiteral("exp"), QStringLiteral("sign"), QStringLiteral("mod"),
            QStringLiteral("if"), QStringLiteral("pi"), QStringLiteral("e"),
            QStringLiteral("tau")
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

bool ParametricValue::evaluate(const QMap<QString, double>& parameters)
{
    if (m_type == Type::Number) {
        // Already a plain number
        return true;
    }

    double result;
    QString error;

    if (evaluateExpression(m_expression, result, parameters, &error)) {
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

// ---- Expression Evaluator ----
// In anonymous namespace — implementation detail of this translation unit.

namespace {

class ExpressionEvaluator {
public:
    explicit ExpressionEvaluator(const QMap<QString, double>& params,
                                 LengthUnit defaultUnit = LengthUnit::Millimeters,
                                 bool unitAware = false)
        : m_params(params), m_defaultUnit(defaultUnit), m_unitAware(unitAware)
    {
    }

    bool evaluate(const QString& expr, double& result, QString& error)
    {
        m_error.clear();
        m_pos = 0;
        m_expr = expr.trimmed();

        if (m_expr.isEmpty()) {
            result = 0.0;
            return true;
        }

        try {
            result = parseExpression();
            skipWhitespace();
            if (m_pos < m_expr.length()) {
                error = QStringLiteral("Unexpected character '%1' at position %2")
                    .arg(m_expr[m_pos]).arg(m_pos);
                return false;
            }
            return true;
        } catch (const std::exception& e) {
            error = QString::fromUtf8(e.what());
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
            if (op != '*' && op != '/' && op != '%')
                break;
            ++m_pos;
            double right = parsePower();
            if (op == '*')
                left *= right;
            else if (op == '/') {
                if (right == 0.0)
                    throw std::runtime_error("Division by zero");
                left /= right;
            } else {
                if (right == 0.0)
                    throw std::runtime_error("Modulo by zero");
                left = std::fmod(left, right);
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
            QStringLiteral("Unexpected character '%1'").arg(m_expr[m_pos]).toStdString());
    }

    double parseNumber()
    {
        int start = m_pos;
        bool hasDecimal = false;
        bool hasExponent = false;

        while (m_pos < m_expr.length()) {
            QChar c = m_expr[m_pos];
            if (c.isDigit()) {
                ++m_pos;
            } else if (c == '.' && !hasDecimal && !hasExponent) {
                hasDecimal = true;
                ++m_pos;
            } else if ((c == 'e' || c == 'E') && !hasExponent) {
                hasExponent = true;
                ++m_pos;
                // Handle optional sign after exponent
                if (m_pos < m_expr.length() && (m_expr[m_pos] == '+' || m_expr[m_pos] == '-'))
                    ++m_pos;
            } else {
                break;
            }
        }

        QString numStr = m_expr.mid(start, m_pos - start);
        bool ok;
        double num = numStr.toDouble(&ok);
        if (!ok)
            throw std::runtime_error(
                QStringLiteral("Invalid number '%1'").arg(numStr).toStdString());

        // Unit-aware mode: check for unit suffix after the number.
        // "Convert final result" approach: bare numbers stay as-is (in display
        // units). Numbers with explicit unit suffixes are converted TO the
        // default display unit so they integrate correctly in the expression.
        // The caller converts the final result from display units to mm.
        if (m_unitAware && m_pos < m_expr.length() && m_expr[m_pos].isLetter()) {
            int suffixStart = m_pos;
            // Read potential suffix (letters only)
            while (m_pos < m_expr.length() && m_expr[m_pos].isLetter())
                ++m_pos;
            QString suffix = m_expr.mid(suffixStart, m_pos - suffixStart).toLower();

            // Check if it's a known unit suffix
            // Must NOT be followed by more alphanumeric chars (to avoid eating function names)
            bool nextIsAlphaNum = (m_pos < m_expr.length() &&
                                   (m_expr[m_pos].isLetterOrNumber() || m_expr[m_pos] == '_'));
            if (!nextIsAlphaNum) {
                LengthUnit suffixUnit;
                bool knownSuffix = true;
                if (suffix == QStringLiteral("mm")) {
                    suffixUnit = LengthUnit::Millimeters;
                } else if (suffix == QStringLiteral("cm")) {
                    suffixUnit = LengthUnit::Centimeters;
                } else if (suffix == QStringLiteral("m")) {
                    suffixUnit = LengthUnit::Meters;
                } else if (suffix == QStringLiteral("in") || suffix == QStringLiteral("inch") ||
                           suffix == QStringLiteral("inches")) {
                    suffixUnit = LengthUnit::Inches;
                } else if (suffix == QStringLiteral("ft") || suffix == QStringLiteral("foot") ||
                           suffix == QStringLiteral("feet")) {
                    suffixUnit = LengthUnit::Feet;
                } else {
                    knownSuffix = false;
                }
                if (knownSuffix) {
                    // Convert from explicit unit to default display unit
                    num = convertLength(num, suffixUnit, m_defaultUnit);
                } else {
                    // Not a known unit suffix — restore position, no conversion
                    m_pos = suffixStart;
                }
            } else {
                // Followed by alphanumeric — not a unit suffix, restore position
                m_pos = suffixStart;
            }
        }
        // else: no suffix, bare number stays as-is in display units

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
        QString lower = name.toLower();
        if (lower == QStringLiteral("pi"))
            return M_PI;
        if (lower == QStringLiteral("e"))
            return M_E;
        if (lower == QStringLiteral("tau"))
            return 2.0 * M_PI;

        // Parameter lookup
        if (m_params.contains(name))
            return m_params[name];

        throw std::runtime_error(
            QStringLiteral("Unknown parameter '%1'").arg(name).toStdString());
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

        // Single-argument functions
        if (args.size() == 1) {
            double x = args[0];

            // Trigonometric (degrees)
            if (fn == QStringLiteral("sin"))
                return std::sin(x * M_PI / 180.0);
            if (fn == QStringLiteral("cos"))
                return std::cos(x * M_PI / 180.0);
            if (fn == QStringLiteral("tan"))
                return std::tan(x * M_PI / 180.0);
            if (fn == QStringLiteral("asin"))
                return std::asin(x) * 180.0 / M_PI;
            if (fn == QStringLiteral("acos"))
                return std::acos(x) * 180.0 / M_PI;
            if (fn == QStringLiteral("atan"))
                return std::atan(x) * 180.0 / M_PI;

            // Trigonometric (radians)
            if (fn == QStringLiteral("sinr"))
                return std::sin(x);
            if (fn == QStringLiteral("cosr"))
                return std::cos(x);
            if (fn == QStringLiteral("tanr"))
                return std::tan(x);

            // Other math
            if (fn == QStringLiteral("sqrt"))
                return std::sqrt(x);
            if (fn == QStringLiteral("abs"))
                return std::abs(x);
            if (fn == QStringLiteral("floor"))
                return std::floor(x);
            if (fn == QStringLiteral("ceil"))
                return std::ceil(x);
            if (fn == QStringLiteral("round"))
                return std::round(x);
            if (fn == QStringLiteral("log"))
                return std::log(x);
            if (fn == QStringLiteral("log10"))
                return std::log10(x);
            if (fn == QStringLiteral("log2"))
                return std::log2(x);
            if (fn == QStringLiteral("exp"))
                return std::exp(x);
            if (fn == QStringLiteral("sign"))
                return (x > 0) ? 1.0 : ((x < 0) ? -1.0 : 0.0);
        }

        // Two-argument functions
        if (args.size() == 2) {
            double a = args[0];
            double b = args[1];

            if (fn == QStringLiteral("min"))
                return std::min(a, b);
            if (fn == QStringLiteral("max"))
                return std::max(a, b);
            if (fn == QStringLiteral("pow"))
                return std::pow(a, b);
            if (fn == QStringLiteral("atan2"))
                return std::atan2(a, b) * 180.0 / M_PI;
            if (fn == QStringLiteral("mod"))
                return std::fmod(a, b);
        }

        // Variable-argument functions
        if (fn == QStringLiteral("min") && args.size() > 2) {
            double result = args[0];
            for (int i = 1; i < args.size(); ++i)
                result = std::min(result, args[i]);
            return result;
        }
        if (fn == QStringLiteral("max") && args.size() > 2) {
            double result = args[0];
            for (int i = 1; i < args.size(); ++i)
                result = std::max(result, args[i]);
            return result;
        }

        // Conditional: if(condition, trueValue, falseValue)
        if (fn == QStringLiteral("if") && args.size() == 3) {
            return (args[0] != 0.0) ? args[1] : args[2];
        }

        throw std::runtime_error(
            QStringLiteral("Unknown function '%1' or wrong number of arguments (%2)")
                .arg(name).arg(args.size()).toStdString());
    }

    QMap<QString, double> m_params;
    LengthUnit m_defaultUnit = LengthUnit::Millimeters;
    bool m_unitAware = false;
    QString m_expr;
    int m_pos = 0;
    QString m_error;
};

}  // anonymous namespace

// ---- ParameterEngine Implementation ----

class ParameterEngine::Impl {
public:
    QMap<QString, Parameter> parameters;
    QStringList evaluationOrder;
    QStringList circularChain;
    bool hasCircular = false;

    void buildDependencyGraph()
    {
        // Clear previous state
        evaluationOrder.clear();
        circularChain.clear();
        hasCircular = false;

        // Extract dependencies for each parameter
        for (auto it = parameters.begin(); it != parameters.end(); ++it) {
            it->dependencies = usedParams(it->expression);
        }

        // Topological sort using Kahn's algorithm
        QMap<QString, int> inDegree;
        QMap<QString, QStringList> dependents;

        for (auto it = parameters.begin(); it != parameters.end(); ++it) {
            inDegree[it->name] = 0;
        }

        for (auto it = parameters.begin(); it != parameters.end(); ++it) {
            for (const QString& dep : it->dependencies) {
                if (parameters.contains(dep)) {
                    dependents[dep].append(it->name);
                }
            }
        }

        for (auto it = parameters.begin(); it != parameters.end(); ++it) {
            for (const QString& dep : it->dependencies) {
                if (parameters.contains(dep)) {
                    inDegree[it->name]++;
                }
            }
        }

        // Queue parameters with no dependencies
        QStringList queue;
        for (auto it = inDegree.begin(); it != inDegree.end(); ++it) {
            if (it.value() == 0) {
                queue.append(it.key());
            }
        }

        while (!queue.isEmpty()) {
            QString current = queue.takeFirst();
            evaluationOrder.append(current);

            for (const QString& dependent : dependents[current]) {
                inDegree[dependent]--;
                if (inDegree[dependent] == 0) {
                    queue.append(dependent);
                }
            }
        }

        // Check for cycles
        if (evaluationOrder.size() < parameters.size()) {
            hasCircular = true;
            // Find a cycle for error reporting
            for (auto it = inDegree.begin(); it != inDegree.end(); ++it) {
                if (it.value() > 0) {
                    circularChain = findCycle(it.key());
                    break;
                }
            }
        }
    }

    QStringList findCycle(const QString& start)
    {
        QStringList path;
        QSet<QString> visited;
        findCycleHelper(start, path, visited);
        return path;
    }

    bool findCycleHelper(const QString& node, QStringList& path, QSet<QString>& visited)
    {
        if (path.contains(node)) {
            // Found cycle, trim path to start at cycle
            int idx = path.indexOf(node);
            path = path.mid(idx);
            path.append(node);
            return true;
        }

        if (visited.contains(node))
            return false;

        visited.insert(node);
        path.append(node);

        if (parameters.contains(node)) {
            for (const QString& dep : parameters[node].dependencies) {
                if (parameters.contains(dep)) {
                    if (findCycleHelper(dep, path, visited))
                        return true;
                }
            }
        }

        path.removeLast();
        return false;
    }

    QStringList usedParams(const QString& expression)
    {
        QStringList result;
        static QRegularExpression paramRx(QStringLiteral("\\b([a-zA-Z_][a-zA-Z0-9_]*)\\b"));

        // Known function names to exclude
        static QSet<QString> functions = {
            "sin", "cos", "tan", "asin", "acos", "atan", "atan2",
            "sinr", "cosr", "tanr",
            "sqrt", "abs", "floor", "ceil", "round",
            "min", "max", "pow", "log", "log10", "log2", "exp",
            "sign", "mod", "if",
            "pi", "e", "tau"
        };

        auto it = paramRx.globalMatch(expression);
        while (it.hasNext()) {
            auto match = it.next();
            QString name = match.captured(1);
            if (!functions.contains(name.toLower()) && !result.contains(name)) {
                result.append(name);
            }
        }

        return result;
    }
};

ParameterEngine::ParameterEngine()
    : d(new Impl)
{
}

ParameterEngine::~ParameterEngine()
{
    delete d;
}

void ParameterEngine::setParameters(const QList<Parameter>& params)
{
    d->parameters.clear();
    for (const Parameter& p : params) {
        d->parameters[p.name] = p;
    }
    d->buildDependencyGraph();
}

QList<Parameter> ParameterEngine::parameters() const
{
    return d->parameters.values();
}

void ParameterEngine::setParameter(const QString& name, const QString& expression,
                                    const QString& unit, const QString& comment)
{
    Parameter p;
    p.name = name;
    p.expression = expression;
    p.unit = unit;
    p.comment = comment;
    p.isUserParam = true;

    d->parameters[name] = p;
    d->buildDependencyGraph();
}

void ParameterEngine::removeParameter(const QString& name)
{
    d->parameters.remove(name);
    d->buildDependencyGraph();
}

void ParameterEngine::clear()
{
    d->parameters.clear();
    d->evaluationOrder.clear();
    d->circularChain.clear();
    d->hasCircular = false;
}

bool ParameterEngine::hasParameter(const QString& name) const
{
    return d->parameters.contains(name);
}

const Parameter* ParameterEngine::parameter(const QString& name) const
{
    auto it = d->parameters.find(name);
    if (it != d->parameters.end())
        return &it.value();
    return nullptr;
}

double ParameterEngine::value(const QString& name) const
{
    auto it = d->parameters.find(name);
    if (it != d->parameters.end())
        return it->value;
    return 0.0;
}

EvaluationResult ParameterEngine::evaluate()
{
    EvaluationResult result;
    result.evaluationOrder = d->evaluationOrder;

    if (d->hasCircular) {
        result.success = false;
        result.errorCount = 1;
        result.errorMessages.append(
            QStringLiteral("Circular dependency detected: %1")
                .arg(d->circularChain.join(" -> ")));
        return result;
    }

    // Build current values map
    QMap<QString, double> values;

    // Evaluate in topological order
    for (const QString& name : d->evaluationOrder) {
        Parameter& p = d->parameters[name];

        double val;
        QString error;
        ExpressionEvaluator eval(values);

        if (eval.evaluate(p.expression, val, error)) {
            p.value = val;
            p.isValid = true;
            p.errorMessage.clear();
            values[name] = val;
        } else {
            p.isValid = false;
            p.errorMessage = error;
            result.errorCount++;
            result.errorMessages.append(
                QStringLiteral("%1: %2").arg(name, error));
        }
    }

    // Also evaluate parameters with unresolved dependencies
    for (auto it = d->parameters.begin(); it != d->parameters.end(); ++it) {
        if (!d->evaluationOrder.contains(it->name)) {
            it->isValid = false;
            it->errorMessage = QStringLiteral("Circular dependency or unresolved reference");
            result.errorCount++;
        }
    }

    result.success = (result.errorCount == 0);
    return result;
}

bool ParameterEngine::evaluateExpression(const QString& expression, double& result,
                                          QString* errorMsg) const
{
    // Build values map from current parameters
    QMap<QString, double> values;
    for (auto it = d->parameters.begin(); it != d->parameters.end(); ++it) {
        if (it->isValid) {
            values[it->name] = it->value;
        }
    }

    QString error;
    ExpressionEvaluator eval(values);
    bool ok = eval.evaluate(expression, result, error);

    if (errorMsg && !ok) {
        *errorMsg = error;
    }

    return ok;
}

bool ParameterEngine::evaluateExpression(const QString& expression, double& result,
                                          LengthUnit defaultUnit,
                                          QString* errorMsg) const
{
    // Build values map from current parameters
    QMap<QString, double> values;
    for (auto it = d->parameters.begin(); it != d->parameters.end(); ++it) {
        if (it->isValid) {
            values[it->name] = it->value;
        }
    }

    QString error;
    ExpressionEvaluator eval(values, defaultUnit, /*unitAware=*/true);
    bool ok = eval.evaluate(expression, result, error);

    if (errorMsg && !ok) {
        *errorMsg = error;
    }

    return ok;
}

QStringList ParameterEngine::evaluationOrder() const
{
    return d->evaluationOrder;
}

QStringList ParameterEngine::dependenciesOf(const QString& name) const
{
    auto it = d->parameters.find(name);
    if (it != d->parameters.end()) {
        return it->dependencies;
    }
    return {};
}

QStringList ParameterEngine::dependentsOf(const QString& name) const
{
    QStringList result;
    for (auto it = d->parameters.begin(); it != d->parameters.end(); ++it) {
        if (it->dependencies.contains(name)) {
            result.append(it->name);
        }
    }
    return result;
}

bool ParameterEngine::hasCircularDependencies() const
{
    return d->hasCircular;
}

QStringList ParameterEngine::circularDependencyChain() const
{
    return d->circularChain;
}

bool ParameterEngine::isValidName(const QString& name)
{
    if (name.isEmpty())
        return false;

    // Must start with letter or underscore
    if (!name[0].isLetter() && name[0] != '_')
        return false;

    // Must contain only alphanumeric and underscore
    for (int i = 1; i < name.length(); ++i) {
        QChar c = name[i];
        if (!c.isLetterOrNumber() && c != '_')
            return false;
    }

    // Cannot be a reserved word
    static QSet<QString> reserved = {
        "sin", "cos", "tan", "asin", "acos", "atan", "atan2",
        "sqrt", "abs", "floor", "ceil", "round",
        "min", "max", "pow", "log", "log10", "log2", "exp",
        "pi", "e", "tau", "if", "mod", "sign"
    };

    return !reserved.contains(name.toLower());
}

bool ParameterEngine::isValidSyntax(const QString& expression, QString* errorMsg) const
{
    double result;
    QString error;

    // Use empty parameter map for syntax check
    ExpressionEvaluator eval({});

    // Try to evaluate - will fail on unknown params but that's expected
    eval.evaluate(expression, result, error);

    // Check for syntax errors (not "Unknown parameter" errors)
    if (error.startsWith("Unexpected") || error.startsWith("Missing") ||
        error.startsWith("Invalid number")) {
        if (errorMsg) *errorMsg = error;
        return false;
    }

    return true;
}

QStringList ParameterEngine::usedParameters(const QString& expression)
{
    // Static wrapper for Impl method
    ParameterEngine temp;
    return temp.d->usedParams(expression);
}

QJsonObject ParameterEngine::toJson() const
{
    QJsonArray params;
    for (auto it = d->parameters.begin(); it != d->parameters.end(); ++it) {
        QJsonObject p;
        p["name"] = it->name;
        p["expression"] = it->expression;
        if (!it->unit.isEmpty())
            p["unit"] = it->unit;
        if (!it->comment.isEmpty())
            p["comment"] = it->comment;
        if (!it->isUserParam)
            p["isUserParam"] = false;
        params.append(p);
    }

    QJsonObject obj;
    obj["parameters"] = params;
    return obj;
}

bool ParameterEngine::fromJson(const QJsonObject& json, QString* errorMsg)
{
    clear();

    if (!json.contains("parameters")) {
        return true;  // Empty is valid
    }

    QJsonArray params = json["parameters"].toArray();
    for (const QJsonValue& val : params) {
        QJsonObject p = val.toObject();

        Parameter param;
        param.name = p["name"].toString();
        param.expression = p["expression"].toString();
        param.unit = p["unit"].toString();
        param.comment = p["comment"].toString();
        param.isUserParam = p.value("isUserParam").toBool(true);

        if (param.name.isEmpty()) {
            if (errorMsg) *errorMsg = "Parameter missing name";
            return false;
        }

        d->parameters[param.name] = param;
    }

    d->buildDependencyGraph();
    return true;
}

// ---- Standalone expression evaluation ----

bool evaluateExpression(const QString& expression, double& result,
                        const QMap<QString, double>& params,
                        QString* errorMsg)
{
    QString error;
    ExpressionEvaluator eval(params);
    bool ok = eval.evaluate(expression, result, error);

    if (errorMsg && !ok) {
        *errorMsg = error;
    }

    return ok;
}

}  // namespace hobbycad
