// =====================================================================
//  src/libhobbycad/sketch/parsing.cpp — Text parsing utilities
// =====================================================================
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include <hobbycad/sketch/parsing.h>

namespace hobbycad {
namespace sketch {

// =====================================================================
//  Identifier Validation
// =====================================================================

bool isValidIdentifier(const QString& str)
{
    if (str.isEmpty()) return false;

    QChar first = str[0];
    if (!first.isLetter() && first != '_') return false;

    for (const QChar& c : str) {
        if (!c.isLetterOrNumber() && c != '_') {
            return false;
        }
    }

    return true;
}

bool looksNumeric(const QString& str)
{
    if (str.isEmpty()) return false;
    QChar first = str[0];
    return first.isDigit() || first == '-' || first == '.';
}

bool isParenthesizedExpression(const QString& str)
{
    return str.length() >= 2 && str.startsWith('(') && str.endsWith(')');
}

// =====================================================================
//  Value Parsing
// =====================================================================

ParsedValue parseValue(const QString& str)
{
    ParsedValue result;
    QString expr = str.trimmed();

    if (expr.isEmpty()) {
        return result;
    }

    result.expression = expr;

    // If it starts with a digit, minus, or decimal point, try to parse as number
    if (looksNumeric(expr)) {
        bool ok = false;
        result.numericValue = expr.toDouble(&ok);
        if (ok) {
            result.valid = true;
            result.isNumeric = true;
        }
        return result;
    }

    // If it's a parenthesized expression, accept it
    if (isParenthesizedExpression(expr)) {
        result.valid = true;
        result.isExpression = true;
        result.numericValue = 0;  // Placeholder - will be evaluated later
        return result;
    }

    // Must be a parameter name
    if (isValidIdentifier(expr)) {
        result.valid = true;
        result.isParameter = true;
        result.numericValue = 0;  // Placeholder - will be resolved later
        return result;
    }

    return result;
}

bool parseValue(const QString& str, double& value, QString& expression)
{
    ParsedValue parsed = parseValue(str);
    if (!parsed.valid) {
        return false;
    }

    expression = parsed.expression;
    value = parsed.numericValue;
    return true;
}

// =====================================================================
//  Coordinate Parsing
// =====================================================================

QStringList splitCoordinate(const QString& str)
{
    QStringList parts;
    QString current;
    int parenDepth = 0;

    for (const QChar& c : str) {
        if (c == '(') {
            parenDepth++;
            current += c;
        } else if (c == ')') {
            parenDepth--;
            current += c;
        } else if (c == ',' && parenDepth == 0) {
            parts.append(current.trimmed());
            current.clear();
        } else {
            current += c;
        }
    }

    if (!current.isEmpty()) {
        parts.append(current.trimmed());
    }

    return parts;
}

ParsedCoordinate parseCoordinate(const QString& str)
{
    ParsedCoordinate result;

    QStringList parts = splitCoordinate(str);
    if (parts.size() != 2) {
        return result;
    }

    result.x = parseValue(parts[0]);
    result.y = parseValue(parts[1]);
    result.valid = result.x.valid && result.y.valid;

    return result;
}

bool parseCoordinate(const QString& str, double& x, double& y,
                     QString* xExpr, QString* yExpr)
{
    ParsedCoordinate parsed = parseCoordinate(str);
    if (!parsed.valid) {
        return false;
    }

    x = parsed.x.numericValue;
    y = parsed.y.numericValue;

    if (xExpr) *xExpr = parsed.x.expression;
    if (yExpr) *yExpr = parsed.y.expression;

    return true;
}

std::optional<QPointF> parsePoint(const QString& str)
{
    ParsedCoordinate parsed = parseCoordinate(str);
    if (!parsed.valid) {
        return std::nullopt;
    }

    // Only return a point if both values are numeric
    if (parsed.x.isNumeric && parsed.y.isNumeric) {
        return QPointF(parsed.x.numericValue, parsed.y.numericValue);
    }

    return std::nullopt;
}

}  // namespace sketch
}  // namespace hobbycad
