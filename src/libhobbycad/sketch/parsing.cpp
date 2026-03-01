// =====================================================================
//  src/libhobbycad/sketch/parsing.cpp — Text parsing utilities
// =====================================================================
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include <hobbycad/sketch/parsing.h>

#include <cctype>
#include <string>

namespace hobbycad {
namespace sketch {

// =====================================================================
//  Helper: trim whitespace from both ends of a string
// =====================================================================

static std::string trim(const std::string& s)
{
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return {};
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// =====================================================================
//  Identifier Validation
// =====================================================================

bool isValidIdentifier(const std::string& str)
{
    if (str.empty()) return false;

    char first = str[0];
    if (!std::isalpha(static_cast<unsigned char>(first)) && first != '_') return false;

    for (char c : str) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') {
            return false;
        }
    }

    return true;
}

bool looksNumeric(const std::string& str)
{
    if (str.empty()) return false;
    char first = str[0];
    return std::isdigit(static_cast<unsigned char>(first)) || first == '-' || first == '.';
}

bool isParenthesizedExpression(const std::string& str)
{
    return str.length() >= 2 && str.front() == '(' && str.back() == ')';
}

// =====================================================================
//  Value Parsing
// =====================================================================

ParsedValue parseValue(const std::string& str)
{
    ParsedValue result;
    std::string expr = trim(str);

    if (expr.empty()) {
        return result;
    }

    result.expression = expr;

    // If it starts with a digit, minus, or decimal point, try to parse as number
    if (looksNumeric(expr)) {
        try {
            size_t pos = 0;
            result.numericValue = std::stod(expr, &pos);
            if (pos == expr.size()) {
                result.valid = true;
                result.isNumeric = true;
            }
        } catch (...) {
            // Not a valid number
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

bool parseValue(const std::string& str, double& value, std::string& expression)
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

std::vector<std::string> splitCoordinate(const std::string& str)
{
    std::vector<std::string> parts;
    std::string current;
    int parenDepth = 0;

    for (char c : str) {
        if (c == '(') {
            parenDepth++;
            current += c;
        } else if (c == ')') {
            parenDepth--;
            current += c;
        } else if (c == ',' && parenDepth == 0) {
            parts.push_back(trim(current));
            current.clear();
        } else {
            current += c;
        }
    }

    if (!current.empty()) {
        parts.push_back(trim(current));
    }

    return parts;
}

ParsedCoordinate parseCoordinate(const std::string& str)
{
    ParsedCoordinate result;

    std::vector<std::string> parts = splitCoordinate(str);
    if (parts.size() != 2) {
        return result;
    }

    result.x = parseValue(parts[0]);
    result.y = parseValue(parts[1]);
    result.valid = result.x.valid && result.y.valid;

    return result;
}

bool parseCoordinate(const std::string& str, double& x, double& y,
                     std::string* xExpr, std::string* yExpr)
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

std::optional<Point2D> parsePoint(const std::string& str)
{
    ParsedCoordinate parsed = parseCoordinate(str);
    if (!parsed.valid) {
        return std::nullopt;
    }

    // Only return a point if both values are numeric
    if (parsed.x.isNumeric && parsed.y.isNumeric) {
        return Point2D(parsed.x.numericValue, parsed.y.numericValue);
    }

    return std::nullopt;
}

}  // namespace sketch
}  // namespace hobbycad
