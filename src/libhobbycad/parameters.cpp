// =====================================================================
//  src/libhobbycad/parameters.cpp — Parametric value engine
// =====================================================================
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include <hobbycad/parameters.h>
#include <hobbycad/units.h>
#include <hobbycad/format.h>

#if HOBBYCAD_HAS_QT
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#else
#include <nlohmann/json.hpp>
#endif

#include <algorithm>
#include <cctype>
#include <cmath>
#include <regex>
#include <stack>
#include <stdexcept>
#include <unordered_set>

#include <hobbycad/math_constants.h>
#ifndef M_E
#define M_E 2.71828182845904523536
#endif

namespace hobbycad {

std::string renameIdentifierInExpression(const std::string& expr,
                                         const std::string& from,
                                         const std::string& to)
{
    if (from.empty()) {
        return expr;
    }
    auto isIdentChar = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
            || (c >= '0' && c <= '9') || c == '_';
    };

    std::string out;
    size_t i = 0;
    while (i < expr.size()) {
        if (expr.compare(i, from.size(), from) == 0) {
            const bool leftOk = (i == 0) || !isIdentChar(expr[i - 1]);
            const size_t after = i + from.size();
            const bool rightOk = (after >= expr.size()) || !isIdentChar(expr[after]);
            if (leftOk && rightOk) {
                out += to;
                i = after;
                continue;
            }
        }
        out += expr[i];
        ++i;
    }
    return out;
}


// ---- ParametricValue ------------------------------------------------

ParametricValue::ParametricValue(double value)
    : m_type(Type::Number)
    , m_expression(formatDouble(value, 15))
    , m_value(value)
    , m_valid(true)
{
}

ParametricValue::ParametricValue(const std::string& expression)
    : m_expression(expression)
{
    // Trim whitespace
    auto start = m_expression.find_first_not_of(" \t\r\n");
    auto end = m_expression.find_last_not_of(" \t\r\n");
    if (start == std::string::npos) {
        m_expression.clear();
    } else {
        m_expression = m_expression.substr(start, end - start + 1);
    }
    parse();
}

void ParametricValue::setExpression(const std::string& expr)
{
    m_expression = expr;
    // Trim whitespace
    auto start = m_expression.find_first_not_of(" \t\r\n");
    auto end = m_expression.find_last_not_of(" \t\r\n");
    if (start == std::string::npos) {
        m_expression.clear();
    } else {
        m_expression = m_expression.substr(start, end - start + 1);
    }
    parse();
}

void ParametricValue::parse()
{
    m_valid = true;
    m_errorMessage.clear();
    m_usedParams.clear();

    if (m_expression.empty()) {
        m_type = Type::Number;
        m_value = 0.0;
        return;
    }

    // Try to parse as a plain number first
    try {
        size_t pos = 0;
        double num = std::stod(m_expression, &pos);
        if (pos == m_expression.size()) {
            m_type = Type::Number;
            m_value = num;
            return;
        }
    } catch (...) {
        // Not a plain number, continue
    }

    // Check if it's a single parameter name (identifier only)
    static std::regex identifierRx("^[a-zA-Z_][a-zA-Z0-9_]*$");
    if (std::regex_match(m_expression, identifierRx)) {
        m_type = Type::Parameter;
        m_usedParams.push_back(m_expression);
        // Value will be resolved when evaluate() is called
        return;
    }

    // Otherwise it's a formula - extract parameter names
    m_type = Type::Formula;
    static std::regex paramRx("\\b([a-zA-Z_][a-zA-Z0-9_]*)\\b");

    static std::unordered_set<std::string> functions = {
        "sin", "cos", "tan", "asin", "acos", "atan", "atan2",
        "sinr", "cosr", "tanr", "sinh", "cosh", "tanh",
        "sec", "csc", "cot", "asec", "acsc", "acot",
        "sech", "csch", "coth", "asinh", "acosh", "atanh",
        "acsch", "asech", "acoth",
        "sqrt", "abs", "floor", "ceil",
        "round", "min", "max", "pow", "log", "log10", "log2",
        "exp", "sign", "mod", "clamp", "pi", "e", "tau",
        "bif", "belse", "bfor", "bwhile", "bdo"
    };

    auto begin = std::sregex_iterator(m_expression.begin(), m_expression.end(), paramRx);
    auto end_it = std::sregex_iterator();
    for (auto it = begin; it != end_it; ++it) {
        std::string name = (*it)[1].str();
        // Convert name to lowercase for function check
        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (functions.find(lower) == functions.end()) {
            // Check not already in usedParams
            if (std::find(m_usedParams.begin(), m_usedParams.end(), name) == m_usedParams.end()) {
                m_usedParams.push_back(name);
            }
        }
    }
}

bool ParametricValue::containsParameters() const
{
    return !m_usedParams.empty();
}

std::vector<std::string> ParametricValue::usedParameters() const
{
    return m_usedParams;
}

bool ParametricValue::evaluate(const std::map<std::string, double>& parameters)
{
    if (m_type == Type::Number) {
        // Already a plain number
        return true;
    }

    double result;
    std::string error;

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
// In anonymous namespace, an implementation detail of this translation unit.

namespace {

class ExpressionEvaluator {
public:
    explicit ExpressionEvaluator(const std::map<std::string, double>& params,
                                 LengthUnit defaultUnit = LengthUnit::Millimeters,
                                 bool unitAware = false)
        : m_params(params), m_defaultUnit(defaultUnit), m_unitAware(unitAware)
    {
    }

    bool evaluate(const std::string& expr, double& result, std::string& error)
    {
        m_error.clear();
        m_pos = 0;
        m_expr = expr;

        // Trim whitespace
        {
            auto start = m_expr.find_first_not_of(" \t\r\n");
            auto end = m_expr.find_last_not_of(" \t\r\n");
            if (start == std::string::npos) {
                m_expr.clear();
            } else {
                m_expr = m_expr.substr(start, end - start + 1);
            }
        }

        if (m_expr.empty()) {
            result = 0.0;
            return true;
        }

        m_vars.clear();
        m_loopIters = 0;
        try {
            result = execProgram();
            skipWhitespace();
            if (m_pos < m_expr.length()) {
                error = hobbycad::format("Unexpected character '%c' at position %d",
                                         m_expr[m_pos], static_cast<int>(m_pos));
                return false;
            }
            return true;
        } catch (const std::exception& e) {
            error = e.what();
            return false;
        }
    }

private:
    void skipWhitespace()
    {
        while (m_pos < m_expr.length() && std::isspace(static_cast<unsigned char>(m_expr[m_pos])))
            ++m_pos;
    }

    // C-style boolean layer (Path A): comparisons and logical operators yield
    // 1.0 / 0.0; truthiness is (x != 0.0). Minimal Qt-free fallback condition
    // language (see HobbyCAD-outside/qjsengine-scripting-web-*.md). Runtime
    // errors (div-by-zero, unknown name) are suppressed inside the not-taken
    // branch of &&, || and ?: so short-circuit is safe; syntax errors still
    // propagate so the dead branch's extent can still be parsed.
    struct SuppressGuard {
        int& counter;
        bool applied;
        SuppressGuard(int& c, bool on) : counter(c), applied(on) { if (on) ++counter; }
        ~SuppressGuard() { if (applied) --counter; }
    };

    double parseExpression()
    {
        return parseAssignment();
    }

    // Assignment is an expression (JS-style): name = expr, right-associative.
    // Writes a script-local variable; suppressed inside a not-taken branch.
    double parseAssignment()
    {
        size_t save = m_pos;
        skipWhitespace();
        if (m_pos < m_expr.length() &&
            (std::isalpha(static_cast<unsigned char>(m_expr[m_pos])) || m_expr[m_pos] == '_')) {
            size_t s = m_pos;
            while (m_pos < m_expr.length() &&
                   (std::isalnum(static_cast<unsigned char>(m_expr[m_pos])) || m_expr[m_pos] == '_'))
                ++m_pos;
            std::string name = m_expr.substr(s, m_pos - s);
            skipWhitespace();
            if (m_pos < m_expr.length() && m_expr[m_pos] == '=' &&
                (m_pos + 1 >= m_expr.length() || m_expr[m_pos + 1] != '=')) {
                ++m_pos;  // consume '='
                double v = parseAssignment();
                if (!m_suppress)
                    m_vars[name] = v;
                return v;
            }
        }
        m_pos = save;
        return parseTernary();
    }

    // Lowest precedence: ternary  cond ? a : b   (right-associative)
    double parseTernary()
    {
        double cond = parseLogicalOr();
        skipWhitespace();
        if (m_pos < m_expr.length() && m_expr[m_pos] == '?') {
            ++m_pos;
            double a;
            {
                SuppressGuard g(m_suppress, cond == 0.0);
                a = parseTernary();
            }
            skipWhitespace();
            if (m_pos >= m_expr.length() || m_expr[m_pos] != ':')
                throw std::runtime_error("Expected ':' in conditional expression");
            ++m_pos;
            double b;
            {
                SuppressGuard g(m_suppress, cond != 0.0);
                b = parseTernary();
            }
            return (cond != 0.0) ? a : b;
        }
        return cond;
    }

    double parseLogicalOr()
    {
        double left = parseLogicalAnd();
        skipWhitespace();
        while (m_pos + 1 < m_expr.length() &&
               m_expr[m_pos] == '|' && m_expr[m_pos + 1] == '|') {
            m_pos += 2;
            double right;
            {
                SuppressGuard g(m_suppress, left != 0.0);   // short-circuit
                right = parseLogicalAnd();
            }
            left = (left != 0.0 || right != 0.0) ? 1.0 : 0.0;
            skipWhitespace();
        }
        return left;
    }

    double parseLogicalAnd()
    {
        double left = parseEquality();
        skipWhitespace();
        while (m_pos + 1 < m_expr.length() &&
               m_expr[m_pos] == '&' && m_expr[m_pos + 1] == '&') {
            m_pos += 2;
            double right;
            {
                SuppressGuard g(m_suppress, left == 0.0);   // short-circuit
                right = parseEquality();
            }
            left = (left != 0.0 && right != 0.0) ? 1.0 : 0.0;
            skipWhitespace();
        }
        return left;
    }

    double parseEquality()
    {
        double left = parseRelational();
        skipWhitespace();
        while (m_pos + 1 < m_expr.length()) {
            char a = m_expr[m_pos], b = m_expr[m_pos + 1];
            if (a == '=' && b == '=') {
                m_pos += 2;
                left = (left == parseRelational()) ? 1.0 : 0.0;
            } else if (a == '!' && b == '=') {
                m_pos += 2;
                left = (left != parseRelational()) ? 1.0 : 0.0;
            } else {
                break;
            }
            skipWhitespace();
        }
        return left;
    }

    double parseRelational()
    {
        double left = parseAddSub();
        skipWhitespace();
        while (m_pos < m_expr.length()) {
            char a = m_expr[m_pos];
            char b = (m_pos + 1 < m_expr.length()) ? m_expr[m_pos + 1] : '\0';
            if (a == '<' && b == '=') { m_pos += 2; left = (left <= parseAddSub()) ? 1.0 : 0.0; }
            else if (a == '>' && b == '=') { m_pos += 2; left = (left >= parseAddSub()) ? 1.0 : 0.0; }
            else if (a == '<') { m_pos += 1; left = (left <  parseAddSub()) ? 1.0 : 0.0; }
            else if (a == '>') { m_pos += 1; left = (left >  parseAddSub()) ? 1.0 : 0.0; }
            else break;
            skipWhitespace();
        }
        return left;
    }

    double parseAddSub()
    {
        double left = parseMulDiv();
        skipWhitespace();

        while (m_pos < m_expr.length()) {
            char op = m_expr[m_pos];
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
            char op = m_expr[m_pos];
            if (op != '*' && op != '/' && op != '%')
                break;
            ++m_pos;
            double right = parsePower();
            if (op == '*')
                left *= right;
            else if (op == '/') {
                if (right == 0.0) {
                    if (!m_suppress)
                        throw std::runtime_error("Division by zero");
                    left = 0.0;
                } else {
                    left /= right;
                }
            } else {
                if (right == 0.0) {
                    if (!m_suppress)
                        throw std::runtime_error("Modulo by zero");
                    left = 0.0;
                } else {
                    left = std::fmod(left, right);
                }
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
            if (m_expr[m_pos] == '!' &&
                !(m_pos + 1 < m_expr.length() && m_expr[m_pos + 1] == '=')) {
                ++m_pos;
                return (parseUnary() != 0.0) ? 0.0 : 1.0;
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
        if (std::isdigit(static_cast<unsigned char>(m_expr[m_pos])) || m_expr[m_pos] == '.') {
            return parseNumber();
        }

        // Identifier (parameter or function)
        if (std::isalpha(static_cast<unsigned char>(m_expr[m_pos])) || m_expr[m_pos] == '_') {
            return parseIdentifier();
        }

        throw std::runtime_error(
            hobbycad::format("Unexpected character '%c'", m_expr[m_pos]));
    }

    double parseNumber()
    {
        size_t start = m_pos;
        bool hasDecimal = false;
        bool hasExponent = false;

        while (m_pos < m_expr.length()) {
            char c = m_expr[m_pos];
            if (std::isdigit(static_cast<unsigned char>(c))) {
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

        std::string numStr = m_expr.substr(start, m_pos - start);
        double num;
        try {
            size_t pos = 0;
            num = std::stod(numStr, &pos);
            if (pos != numStr.size())
                throw std::runtime_error("Invalid number '" + numStr + "'");
        } catch (const std::invalid_argument&) {
            throw std::runtime_error("Invalid number '" + numStr + "'");
        }

        // Unit-aware mode: check for unit suffix after the number.
        if (m_unitAware && m_pos < m_expr.length() &&
            std::isalpha(static_cast<unsigned char>(m_expr[m_pos]))) {
            size_t suffixStart = m_pos;
            // Read potential suffix (letters only)
            while (m_pos < m_expr.length() &&
                   std::isalpha(static_cast<unsigned char>(m_expr[m_pos])))
                ++m_pos;
            std::string suffix = m_expr.substr(suffixStart, m_pos - suffixStart);
            // Convert to lowercase
            std::transform(suffix.begin(), suffix.end(), suffix.begin(), ::tolower);

            // Check if it's a known unit suffix
            bool nextIsAlphaNum = (m_pos < m_expr.length() &&
                                   (std::isalnum(static_cast<unsigned char>(m_expr[m_pos])) ||
                                    m_expr[m_pos] == '_'));
            if (!nextIsAlphaNum) {
                LengthUnit suffixUnit{};
                bool knownSuffix = true;
                if (suffix == "mm") {
                    suffixUnit = LengthUnit::Millimeters;
                } else if (suffix == "cm") {
                    suffixUnit = LengthUnit::Centimeters;
                } else if (suffix == "m") {
                    suffixUnit = LengthUnit::Meters;
                } else if (suffix == "in" || suffix == "inch" || suffix == "inches") {
                    suffixUnit = LengthUnit::Inches;
                } else if (suffix == "ft" || suffix == "foot" || suffix == "feet") {
                    suffixUnit = LengthUnit::Feet;
                } else {
                    knownSuffix = false;
                }
                if (knownSuffix) {
                    // Convert from explicit unit to default display unit
                    num = convertLength(num, suffixUnit, m_defaultUnit);
                } else {
                    // Not a known unit suffix: restore position, no conversion
                    m_pos = suffixStart;
                }
            } else {
                // Followed by alphanumeric: not a unit suffix, restore position
                m_pos = suffixStart;
            }
        }

        return num;
    }

    double parseIdentifier()
    {
        size_t start = m_pos;
        while (m_pos < m_expr.length() &&
               (std::isalnum(static_cast<unsigned char>(m_expr[m_pos])) || m_expr[m_pos] == '_'))
            ++m_pos;

        std::string name = m_expr.substr(start, m_pos - start);
        skipWhitespace();

        // Check for function call
        if (m_pos < m_expr.length() && m_expr[m_pos] == '(') {
            return parseFunction(name);
        }

        // Script-local variables (assignment) shadow params and constants.
        {
            auto vit = m_vars.find(name);
            if (vit != m_vars.end())
                return vit->second;
        }

        // Built-in constants
        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (lower == "pi")
            return M_PI;
        if (lower == "e")
            return M_E;
        if (lower == "tau")
            return 2.0 * M_PI;

        // Parameter lookup
        auto it = m_params.find(name);
        if (it != m_params.end())
            return it->second;

        if (m_suppress)
            return 0.0;
        throw std::runtime_error("Unknown parameter '" + name + "'");
    }

    double parseFunction(const std::string& name)
    {
        ++m_pos;  // Skip '('

        std::vector<double> args;
        skipWhitespace();

        if (m_pos < m_expr.length() && m_expr[m_pos] != ')') {
            args.push_back(parseExpression());
            skipWhitespace();

            while (m_pos < m_expr.length() && m_expr[m_pos] == ',') {
                ++m_pos;
                args.push_back(parseExpression());
                skipWhitespace();
            }
        }

        if (m_pos >= m_expr.length() || m_expr[m_pos] != ')')
            throw std::runtime_error("Missing closing parenthesis in function call");
        ++m_pos;

        return callFunction(name, args);
    }

    double callFunction(const std::string& name, const std::vector<double>& args)
    {
        std::string fn = name;
        std::transform(fn.begin(), fn.end(), fn.begin(), ::tolower);

        // Single-argument functions
        if (args.size() == 1) {
            double x = args[0];

            // Trigonometric (degrees)
            if (fn == "sin")
                return std::sin(degreesToRadians(x));
            if (fn == "cos")
                return std::cos(degreesToRadians(x));
            if (fn == "tan")
                return std::tan(degreesToRadians(x));
            if (fn == "sec")
                return 1.0 / std::cos(degreesToRadians(x));
            if (fn == "csc")
                return 1.0 / std::sin(degreesToRadians(x));
            if (fn == "cot")
                return std::cos(degreesToRadians(x)) / std::sin(degreesToRadians(x));
            if (fn == "asin")
                return radiansToDegrees(std::asin(x));
            if (fn == "acos")
                return radiansToDegrees(std::acos(x));
            if (fn == "atan")
                return radiansToDegrees(std::atan(x));
            if (fn == "asec")
                return radiansToDegrees(std::acos(1.0 / x));
            if (fn == "acsc")
                return radiansToDegrees(std::asin(1.0 / x));
            if (fn == "acot")
                return radiansToDegrees(std::atan(1.0 / x));

            // Trigonometric (radians)
            if (fn == "sinr")
                return std::sin(x);
            if (fn == "cosr")
                return std::cos(x);
            if (fn == "tanr")
                return std::tan(x);

            // Hyperbolic (argument is a plain real, not an angle in degrees)
            if (fn == "sinh")
                return std::sinh(x);
            if (fn == "cosh")
                return std::cosh(x);
            if (fn == "tanh")
                return std::tanh(x);
            if (fn == "sech")
                return 1.0 / std::cosh(x);
            if (fn == "csch")
                return 1.0 / std::sinh(x);
            if (fn == "coth")
                return std::cosh(x) / std::sinh(x);

            // Inverse hyperbolic (plain real in and out)
            if (fn == "asinh")
                return std::asinh(x);
            if (fn == "acosh")
                return std::acosh(x);
            if (fn == "atanh")
                return std::atanh(x);
            if (fn == "acsch")
                return std::asinh(1.0 / x);
            if (fn == "asech")
                return std::acosh(1.0 / x);
            if (fn == "acoth")
                return std::atanh(1.0 / x);

            // Other math
            if (fn == "sqrt")
                return std::sqrt(x);
            if (fn == "abs")
                return std::abs(x);
            if (fn == "floor")
                return std::floor(x);
            if (fn == "ceil")
                return std::ceil(x);
            if (fn == "round")
                return std::round(x);
            if (fn == "log")
                return std::log(x);
            if (fn == "log10")
                return std::log10(x);
            if (fn == "log2")
                return std::log2(x);
            if (fn == "exp")
                return std::exp(x);
            if (fn == "sign")
                return (x > 0) ? 1.0 : ((x < 0) ? -1.0 : 0.0);
        }

        // Two-argument functions
        if (args.size() == 2) {
            double a = args[0];
            double b = args[1];

            if (fn == "min")
                return std::min(a, b);
            if (fn == "max")
                return std::max(a, b);
            if (fn == "pow")
                return std::pow(a, b);
            if (fn == "atan2")
                return radiansToDegrees(std::atan2(a, b));
            if (fn == "mod")
                return std::fmod(a, b);
        }

        // Variable-argument functions
        if (fn == "min" && args.size() > 2) {
            double result = args[0];
            for (size_t i = 1; i < args.size(); ++i)
                result = std::min(result, args[i]);
            return result;
        }
        if (fn == "max" && args.size() > 2) {
            double result = args[0];
            for (size_t i = 1; i < args.size(); ++i)
                result = std::max(result, args[i]);
            return result;
        }

        // clamp(x, lo, hi) == max(lo, min(hi, x)); assumes lo <= hi
        if (fn == "clamp" && args.size() == 3) {
            return std::max(args[1], std::min(args[2], args[0]));
        }

        if (m_suppress)
            return 0.0;
        throw std::runtime_error(
            "Unknown function '" + name + "' or wrong number of arguments (" +
            std::to_string(args.size()) + ")");
    }

    // ---- Statement / control-flow layer (Path A, C-style, 'b'-prefixed) ----
    // if->bif, else->belse, for->bfor, while->bwhile, do->bdo, so the syntax
    // mirrors the QtJS (Path B) host with only the prefix differing. Loop bodies
    // and conditions are re-evaluated by rewinding m_pos; the suppress counter
    // skips an untaken branch (and positions past a loop body without running
    // it). Values are doubles; truthiness is (x != 0).

    void expectChar(char c, const char* what)
    {
        skipWhitespace();
        if (m_pos >= m_expr.length() || m_expr[m_pos] != c)
            throw std::runtime_error(std::string("Expected '") + c + "' " + what);
        ++m_pos;
    }

    std::string readIdentLower()
    {
        skipWhitespace();
        size_t s = m_pos;
        while (m_pos < m_expr.length() &&
               (std::isalnum(static_cast<unsigned char>(m_expr[m_pos])) || m_expr[m_pos] == '_'))
            ++m_pos;
        std::string id = m_expr.substr(s, m_pos - s);
        std::transform(id.begin(), id.end(), id.begin(), ::tolower);
        return id;
    }

    std::string peekIdentLower()
    {
        size_t save = m_pos;
        std::string id = readIdentLower();
        m_pos = save;
        return id;
    }

    // A program is a ';'-separated statement list; its value is the last one.
    double execProgram() { return execStatementList('\0'); }

    // term is '}' inside a block, '\0' at top level.
    double execStatementList(char term)
    {
        double last = 0.0;
        skipWhitespace();
        while (m_pos < m_expr.length() && m_expr[m_pos] != term) {
            if (m_expr[m_pos] == ';') { ++m_pos; skipWhitespace(); continue; }
            last = execStatement();
            skipWhitespace();
            if (m_pos < m_expr.length() && m_expr[m_pos] == ';') { ++m_pos; skipWhitespace(); }
            else break;
        }
        return last;
    }

    double execBlock()
    {
        expectChar('{', "to open a block");
        double v = execStatementList('}');
        expectChar('}', "to close a block");
        return v;
    }

    double execStatement()
    {
        skipWhitespace();
        if (m_pos < m_expr.length() && m_expr[m_pos] == '{')
            return execBlock();
        std::string kw = peekIdentLower();
        if (kw == "bif")    return execIf();
        if (kw == "bwhile") return execWhile();
        if (kw == "bfor")   return execFor();
        if (kw == "bdo")    return execDo();
        return parseExpression();   // expression statement (includes assignment)
    }

    double execBlockOrStatement()
    {
        skipWhitespace();
        if (m_pos < m_expr.length() && m_expr[m_pos] == '{')
            return execBlock();
        return execStatement();
    }

    void bumpLoopGuard(const char* which)
    {
        if (++m_loopIters > kMaxLoopIters)
            throw std::runtime_error(std::string("Loop iteration limit exceeded in ") + which);
    }

    double execIf()
    {
        readIdentLower();                       // 'bif'
        expectChar('(', "after bif");
        double cond = parseExpression();
        expectChar(')', "after bif condition");
        double result = 0.0;
        {
            SuppressGuard g(m_suppress, cond == 0.0);
            double t = execBlockOrStatement();
            if (cond != 0.0) result = t;
        }
        skipWhitespace();
        if (peekIdentLower() == "belse") {
            readIdentLower();                   // 'belse'
            SuppressGuard g(m_suppress, cond != 0.0);
            double e = execBlockOrStatement();
            if (cond == 0.0) result = e;
        }
        return result;
    }

    double execWhile()
    {
        readIdentLower();                       // 'bwhile'
        expectChar('(', "after bwhile");
        size_t condPos = m_pos;
        { SuppressGuard g(m_suppress, true); parseExpression(); }   // position past cond
        expectChar(')', "after bwhile condition");
        size_t bodyPos = m_pos;
        { SuppressGuard g(m_suppress, true); m_pos = bodyPos; execBlockOrStatement(); }
        size_t bodyEnd = m_pos;
        double last = 0.0;
        if (!m_suppress) {
            while (true) {
                m_pos = condPos;
                if (parseExpression() == 0.0) break;
                m_pos = bodyPos;
                last = execBlockOrStatement();
                bumpLoopGuard("bwhile");
            }
        }
        m_pos = bodyEnd;
        return last;
    }

    double execFor()
    {
        readIdentLower();                       // 'bfor'
        expectChar('(', "after bfor");
        skipWhitespace();
        if (m_pos < m_expr.length() && m_expr[m_pos] != ';')
            parseExpression();                  // init: runs once
        expectChar(';', "after bfor init");
        size_t condPos = m_pos;
        skipWhitespace();
        bool condEmpty = (m_pos < m_expr.length() && m_expr[m_pos] == ';');
        { SuppressGuard g(m_suppress, true); if (!condEmpty) parseExpression(); }
        expectChar(';', "after bfor condition");
        size_t postPos = m_pos;
        skipWhitespace();
        bool postEmpty = (m_pos < m_expr.length() && m_expr[m_pos] == ')');
        { SuppressGuard g(m_suppress, true); if (!postEmpty) parseExpression(); }
        expectChar(')', "after bfor clauses");
        size_t bodyPos = m_pos;
        { SuppressGuard g(m_suppress, true); m_pos = bodyPos; execBlockOrStatement(); }
        size_t bodyEnd = m_pos;
        double last = 0.0;
        if (!m_suppress) {
            while (true) {
                if (!condEmpty) { m_pos = condPos; if (parseExpression() == 0.0) break; }
                m_pos = bodyPos;
                last = execBlockOrStatement();
                if (!postEmpty) { m_pos = postPos; parseExpression(); }
                bumpLoopGuard("bfor");
            }
        }
        m_pos = bodyEnd;
        return last;
    }

    double execDo()
    {
        readIdentLower();                       // 'bdo'
        size_t bodyPos = m_pos;
        { SuppressGuard g(m_suppress, true); m_pos = bodyPos; execBlockOrStatement(); }
        size_t bodyEnd = m_pos;
        m_pos = bodyEnd;
        if (readIdentLower() != "bwhile")
            throw std::runtime_error("Expected bwhile after bdo body");
        expectChar('(', "after bdo's bwhile");
        size_t condPos = m_pos;
        { SuppressGuard g(m_suppress, true); parseExpression(); }
        expectChar(')', "after bdo condition");
        size_t endPos = m_pos;
        double last = 0.0;
        if (!m_suppress) {
            while (true) {
                m_pos = bodyPos;
                last = execBlockOrStatement();
                m_pos = condPos;
                if (parseExpression() == 0.0) break;
                bumpLoopGuard("bdo");
            }
        }
        m_pos = endPos;
        return last;
    }

    std::map<std::string, double> m_params;
    LengthUnit m_defaultUnit = LengthUnit::Millimeters;
    bool m_unitAware = false;
    std::string m_expr;
    size_t m_pos = 0;
    std::string m_error;
    int m_suppress = 0;   // >0 while parsing a short-circuited / not-taken branch
    std::map<std::string, double> m_vars;   // mutable script locals (assignment)
    long m_loopIters = 0;                   // total loop iterations, this evaluate()
    static constexpr long kMaxLoopIters = 1000000;  // runaway-loop guard
};

}  // anonymous namespace

// ---- ParameterEngine Implementation ----

class ParameterEngine::Impl {
public:
    std::map<std::string, Parameter> parameters;
    std::vector<std::string> evaluationOrder;
    std::vector<std::string> circularChain;
    bool hasCircular = false;

    void buildDependencyGraph()
    {
        // Clear previous state
        evaluationOrder.clear();
        circularChain.clear();
        hasCircular = false;

        // Extract dependencies for each parameter
        for (auto it = parameters.begin(); it != parameters.end(); ++it) {
            it->second.dependencies = usedParams(it->second.expression);
        }

        // Topological sort using Kahn's algorithm
        std::map<std::string, int> inDegree;
        std::map<std::string, std::vector<std::string>> dependents;

        for (auto it = parameters.begin(); it != parameters.end(); ++it) {
            inDegree[it->first] = 0;
        }

        for (auto it = parameters.begin(); it != parameters.end(); ++it) {
            for (const std::string& dep : it->second.dependencies) {
                if (parameters.count(dep)) {
                    dependents[dep].push_back(it->first);
                }
            }
        }

        for (auto it = parameters.begin(); it != parameters.end(); ++it) {
            for (const std::string& dep : it->second.dependencies) {
                if (parameters.count(dep)) {
                    inDegree[it->first]++;
                }
            }
        }

        // Queue parameters with no dependencies
        std::vector<std::string> queue;
        for (auto it = inDegree.begin(); it != inDegree.end(); ++it) {
            if (it->second == 0) {
                queue.push_back(it->first);
            }
        }

        while (!queue.empty()) {
            std::string current = queue.front();
            queue.erase(queue.begin());
            evaluationOrder.push_back(current);

            for (const std::string& dependent : dependents[current]) {
                inDegree[dependent]--;
                if (inDegree[dependent] == 0) {
                    queue.push_back(dependent);
                }
            }
        }

        // Check for cycles
        if (evaluationOrder.size() < parameters.size()) {
            hasCircular = true;
            // Find a cycle for error reporting
            for (auto it = inDegree.begin(); it != inDegree.end(); ++it) {
                if (it->second > 0) {
                    circularChain = findCycle(it->first);
                    break;
                }
            }
        }
    }

    std::vector<std::string> findCycle(const std::string& start)
    {
        std::vector<std::string> path;
        std::unordered_set<std::string> visited;
        findCycleHelper(start, path, visited);
        return path;
    }

    bool findCycleHelper(const std::string& node, std::vector<std::string>& path,
                         std::unordered_set<std::string>& visited)
    {
        // Check if node is already in path (cycle found)
        auto pathIt = std::find(path.begin(), path.end(), node);
        if (pathIt != path.end()) {
            // Found cycle, trim path to start at cycle
            path.erase(path.begin(), pathIt);
            path.push_back(node);
            return true;
        }

        if (visited.count(node))
            return false;

        visited.insert(node);
        path.push_back(node);

        if (parameters.count(node)) {
            for (const std::string& dep : parameters[node].dependencies) {
                if (parameters.count(dep)) {
                    if (findCycleHelper(dep, path, visited))
                        return true;
                }
            }
        }

        path.pop_back();
        return false;
    }

    std::vector<std::string> usedParams(const std::string& expression)
    {
        std::vector<std::string> result;
        static std::regex paramRx("\\b([a-zA-Z_][a-zA-Z0-9_]*)\\b");

        // Known function names to exclude
        static std::unordered_set<std::string> functions = {
            "sin", "cos", "tan", "asin", "acos", "atan", "atan2",
            "sinr", "cosr", "tanr",
            "sinh", "cosh", "tanh", "sec", "csc", "cot",
            "asec", "acsc", "acot", "sech", "csch", "coth",
            "asinh", "acosh", "atanh", "acsch", "asech", "acoth",
            "sqrt", "abs", "floor", "ceil", "round",
            "min", "max", "pow", "log", "log10", "log2", "exp",
            "sign", "mod", "clamp",
            "pi", "e", "tau"
        };

        auto begin = std::sregex_iterator(expression.begin(), expression.end(), paramRx);
        auto end_it = std::sregex_iterator();
        for (auto it = begin; it != end_it; ++it) {
            std::string name = (*it)[1].str();
            std::string lower = name;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (functions.find(lower) == functions.end() &&
                std::find(result.begin(), result.end(), name) == result.end()) {
                result.push_back(name);
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

void ParameterEngine::setParameters(const std::vector<Parameter>& params)
{
    d->parameters.clear();
    for (const Parameter& p : params) {
        d->parameters[p.name] = p;
    }
    d->buildDependencyGraph();
}

void ParameterEngine::setReferenceParameter(const std::string& name,
                                            const std::string& referenceSource,
                                            const std::string& unit,
                                            const std::string& comment)
{
    Parameter& p = d->parameters[name];
    p.name = name;
    p.isReference = true;
    p.referenceSource = referenceSource;
    p.expression.clear();          // no authored expression; value is measured
    if (!unit.empty())    p.unit = unit;
    if (!comment.empty()) p.comment = comment;
    p.isUserParam = true;
    p.isValid = true;
    d->buildDependencyGraph();     // a reference param has no dependencies (leaf)
}

void ParameterEngine::setReferenceValue(const std::string& name, double value)
{
    auto it = d->parameters.find(name);
    if (it == d->parameters.end() || !it->second.isReference) return;
    it->second.value = value;
    it->second.isValid = true;
}

std::vector<Parameter> ParameterEngine::parameters() const
{
    std::vector<Parameter> result;
    result.reserve(d->parameters.size());
    for (const auto& kv : d->parameters) {
        result.push_back(kv.second);
    }
    return result;
}

void ParameterEngine::setParameter(const std::string& name, const std::string& expression,
                                    const std::string& unit, const std::string& comment)
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

void ParameterEngine::removeParameter(const std::string& name)
{
    d->parameters.erase(name);
    d->buildDependencyGraph();
}

void ParameterEngine::clear()
{
    d->parameters.clear();
    d->evaluationOrder.clear();
    d->circularChain.clear();
    d->hasCircular = false;
}

bool ParameterEngine::hasParameter(const std::string& name) const
{
    return d->parameters.count(name) > 0;
}

const Parameter* ParameterEngine::parameter(const std::string& name) const
{
    auto it = d->parameters.find(name);
    if (it != d->parameters.end())
        return &it->second;
    return nullptr;
}

double ParameterEngine::value(const std::string& name) const
{
    auto it = d->parameters.find(name);
    if (it != d->parameters.end())
        return it->second.value;
    return 0.0;
}

EvaluationResult ParameterEngine::evaluate()
{
    EvaluationResult result;
    result.evaluationOrder = d->evaluationOrder;

    if (d->hasCircular) {
        result.success = false;
        result.errorCount = 1;
        // Build chain string: "a -> b -> c -> a"
        std::string chainStr;
        for (size_t i = 0; i < d->circularChain.size(); ++i) {
            if (i > 0) chainStr += " -> ";
            chainStr += d->circularChain[i];
        }
        result.errorMessages.push_back("Circular dependency detected: " + chainStr);
        return result;
    }

    // Build current values map
    std::map<std::string, double> values;

    // Evaluate in topological order
    for (const std::string& name : d->evaluationOrder) {
        Parameter& p = d->parameters[name];

        if (p.isReference) {
            // Value is measured from the solved model (setReferenceValue); take
            // it as-is so dependent expressions can reference it.
            p.isValid = true;
            p.errorMessage.clear();
            values[name] = p.value;
            continue;
        }

        double val;
        std::string error;
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
            result.errorMessages.push_back(name + ": " + error);
        }
    }

    // Also evaluate parameters with unresolved dependencies
    for (auto it = d->parameters.begin(); it != d->parameters.end(); ++it) {
        if (std::find(d->evaluationOrder.begin(), d->evaluationOrder.end(),
                      it->second.name) == d->evaluationOrder.end()) {
            it->second.isValid = false;
            it->second.errorMessage = "Circular dependency or unresolved reference";
            result.errorCount++;
        }
    }

    result.success = (result.errorCount == 0);
    return result;
}

bool ParameterEngine::evaluateExpression(const std::string& expression, double& result,
                                          std::string* errorMsg) const
{
    // Build values map from current parameters
    std::map<std::string, double> values;
    for (auto it = d->parameters.begin(); it != d->parameters.end(); ++it) {
        if (it->second.isValid) {
            values[it->second.name] = it->second.value;
        }
    }

    std::string error;
    ExpressionEvaluator eval(values);
    bool ok = eval.evaluate(expression, result, error);

    if (errorMsg && !ok) {
        *errorMsg = error;
    }

    return ok;
}

bool ParameterEngine::evaluateExpression(const std::string& expression, double& result,
                                          LengthUnit defaultUnit,
                                          std::string* errorMsg) const
{
    // Build values map from current parameters
    std::map<std::string, double> values;
    for (auto it = d->parameters.begin(); it != d->parameters.end(); ++it) {
        if (it->second.isValid) {
            values[it->second.name] = it->second.value;
        }
    }

    std::string error;
    ExpressionEvaluator eval(values, defaultUnit, /*unitAware=*/true);
    bool ok = eval.evaluate(expression, result, error);

    if (errorMsg && !ok) {
        *errorMsg = error;
    }

    return ok;
}

std::vector<std::string> ParameterEngine::evaluationOrder() const
{
    return d->evaluationOrder;
}

std::vector<std::string> ParameterEngine::dependenciesOf(const std::string& name) const
{
    auto it = d->parameters.find(name);
    if (it != d->parameters.end()) {
        return it->second.dependencies;
    }
    return {};
}

std::vector<std::string> ParameterEngine::dependentsOf(const std::string& name) const
{
    std::vector<std::string> result;
    for (auto it = d->parameters.begin(); it != d->parameters.end(); ++it) {
        const auto& deps = it->second.dependencies;
        if (std::find(deps.begin(), deps.end(), name) != deps.end()) {
            result.push_back(it->second.name);
        }
    }
    return result;
}

bool ParameterEngine::hasCircularDependencies() const
{
    return d->hasCircular;
}

std::vector<std::string> ParameterEngine::circularDependencyChain() const
{
    return d->circularChain;
}

bool ParameterEngine::isValidName(const std::string& name)
{
    return checkName(name).problem == NameProblem::None;
}

NameCheck ParameterEngine::checkName(const std::string& name)
{
    // The whole UTF-8 sequence starting at `i`, so a refused character is
    // reported as the user typed it.
    const auto characterAt = [&name](size_t i) {
        const unsigned char lead = static_cast<unsigned char>(name[i]);
        size_t len = 1;
        if (lead >= 0xF0) len = 4;
        else if (lead >= 0xE0) len = 3;
        else if (lead >= 0xC0) len = 2;
        return name.substr(i, len);
    };
    const auto refuse = [](NameProblem problem, std::string character = {}) {
        NameCheck check;
        check.problem = problem;
        check.character = std::move(character);
        return check;
    };

    if (name.empty())
        return refuse(NameProblem::Empty);

    // Must start with letter or underscore
    const unsigned char first = static_cast<unsigned char>(name[0]);
    if (std::isdigit(first))
        return refuse(NameProblem::StartsWithDigit);
    if (first >= 0x80 || (!std::isalpha(first) && first != '_'))
        return refuse(NameProblem::BadStart, characterAt(0));

    // Must contain only alphanumeric and underscore
    for (size_t i = 1; i < name.length(); ++i) {
        const unsigned char c = static_cast<unsigned char>(name[i]);
        if (c >= 0x80 || (!std::isalnum(c) && c != '_'))
            return refuse(NameProblem::BadCharacter, characterAt(i));
    }

    // Cannot be a reserved word
    static std::unordered_set<std::string> reserved = {
        "sin", "cos", "tan", "asin", "acos", "atan", "atan2",
        "sinh", "cosh", "tanh", "sec", "csc", "cot",
        "asec", "acsc", "acot", "sech", "csch", "coth",
        "asinh", "acosh", "atanh", "acsch", "asech", "acoth",
        "sqrt", "abs", "floor", "ceil", "round",
        "min", "max", "pow", "log", "log10", "log2", "exp",
        "pi", "e", "tau", "mod", "sign", "clamp"
    };

    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (reserved.find(lower) != reserved.end())
        return refuse(NameProblem::Reserved);
    return NameCheck{};
}

bool ParameterEngine::isValidSyntax(const std::string& expression, std::string* errorMsg) const
{
    double result;
    std::string error;

    // Use empty parameter map for syntax check
    ExpressionEvaluator eval({});

    // Try to evaluate - will fail on unknown params but that's expected
    eval.evaluate(expression, result, error);

    // Check for syntax errors (not "Unknown parameter" errors)
    if (error.substr(0, 10) == "Unexpected" ||
        error.substr(0, 7) == "Missing" ||
        error.substr(0, 14) == "Invalid number") {
        if (errorMsg) *errorMsg = error;
        return false;
    }

    return true;
}

std::vector<std::string> ParameterEngine::usedParameters(const std::string& expression)
{
    // Static wrapper for Impl method
    ParameterEngine temp;
    return temp.d->usedParams(expression);
}

#if HOBBYCAD_HAS_QT
QJsonObject ParameterEngine::toJson() const
{
    QJsonArray params;
    for (auto it = d->parameters.begin(); it != d->parameters.end(); ++it) {
        QJsonObject p;
        p["name"] = QString::fromStdString(it->second.name);
        p["expression"] = QString::fromStdString(it->second.expression);
        if (!it->second.unit.empty())
            p["unit"] = QString::fromStdString(it->second.unit);
        if (!it->second.comment.empty())
            p["comment"] = QString::fromStdString(it->second.comment);
        if (!it->second.isUserParam)
            p["isUserParam"] = false;
        if (it->second.isReference) {
            p["isReference"] = true;
            if (!it->second.referenceSource.empty())
                p["referenceSource"] = QString::fromStdString(it->second.referenceSource);
        }
        params.append(p);
    }

    QJsonObject obj;
    obj["parameters"] = params;
    return obj;
}

bool ParameterEngine::fromJson(const QJsonObject& json, std::string* errorMsg)
{
    clear();

    if (!json.contains("parameters")) {
        return true;  // Empty is valid
    }

    QJsonArray params = json["parameters"].toArray();
    for (const QJsonValue& val : params) {
        QJsonObject p = val.toObject();

        Parameter param;
        param.name = p["name"].toString().toStdString();
        param.expression = p["expression"].toString().toStdString();
        param.unit = p["unit"].toString().toStdString();
        param.comment = p["comment"].toString().toStdString();
        param.isUserParam = p.value("isUserParam").toBool(true);
        param.isReference = p.value("isReference").toBool(false);
        param.referenceSource = p["referenceSource"].toString().toStdString();

        if (param.name.empty()) {
            if (errorMsg) *errorMsg = "Parameter missing name";
            return false;
        }

        d->parameters[param.name] = param;
    }

    d->buildDependencyGraph();
    return true;
}

#else  // !HOBBYCAD_HAS_QT: nlohmann/json fallback

nlohmann::json ParameterEngine::toJson() const
{
    nlohmann::json params = nlohmann::json::array();
    for (auto it = d->parameters.begin(); it != d->parameters.end(); ++it) {
        nlohmann::json p;
        p["name"] = it->second.name;
        p["expression"] = it->second.expression;
        if (!it->second.unit.empty())
            p["unit"] = it->second.unit;
        if (!it->second.comment.empty())
            p["comment"] = it->second.comment;
        if (!it->second.isUserParam)
            p["isUserParam"] = false;
        if (it->second.isReference) {
            p["isReference"] = true;
            if (!it->second.referenceSource.empty())
                p["referenceSource"] = it->second.referenceSource;
        }
        params.push_back(p);
    }

    nlohmann::json obj;
    obj["parameters"] = params;
    return obj;
}

bool ParameterEngine::fromJson(const nlohmann::json& json, std::string* errorMsg)
{
    clear();

    if (!json.contains("parameters")) {
        return true;  // Empty is valid
    }

    for (const auto& p : json["parameters"]) {
        Parameter param;
        param.name = p.value("name", std::string{});
        param.expression = p.value("expression", std::string{});
        param.unit = p.value("unit", std::string{});
        param.comment = p.value("comment", std::string{});
        param.isUserParam = p.value("isUserParam", true);
        param.isReference = p.value("isReference", false);
        param.referenceSource = p.value("referenceSource", std::string{});

        if (param.name.empty()) {
            if (errorMsg) *errorMsg = "Parameter missing name";
            return false;
        }

        d->parameters[param.name] = param;
    }

    d->buildDependencyGraph();
    return true;
}

#endif  // HOBBYCAD_HAS_QT

// ---- Standalone expression evaluation ----

bool evaluateExpression(const std::string& expression, double& result,
                        const std::map<std::string, double>& params,
                        std::string* errorMsg)
{
    std::string error;
    ExpressionEvaluator eval(params);
    bool ok = eval.evaluate(expression, result, error);

    if (errorMsg && !ok) {
        *errorMsg = error;
    }

    return ok;
}

bool expressionUsesParameters(const std::string& expression,
                              const std::map<std::string, double>& params)
{
    double withParams = 0.0, without = 0.0;
    const bool okWith = evaluateExpression(expression, withParams, params);
    const bool okBare = evaluateExpression(expression, without, {});
    return okWith && (!okBare || withParams != without);
}

}  // namespace hobbycad
