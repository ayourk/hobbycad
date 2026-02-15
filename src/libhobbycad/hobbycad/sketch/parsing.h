// =====================================================================
//  src/libhobbycad/hobbycad/sketch/parsing.h — Text parsing utilities
// =====================================================================
//
//  Utilities for parsing sketch commands and expressions from text input.
//  Used by CLI and potential scripting interfaces.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_SKETCH_PARSING_H
#define HOBBYCAD_SKETCH_PARSING_H

#include "../core.h"

#include <QPointF>
#include <QString>
#include <QStringList>
#include <optional>

namespace hobbycad {
namespace sketch {

// =====================================================================
//  Value Parsing
// =====================================================================

/// Result of parsing a value that may be a number, parameter, or expression
struct ParsedValue {
    bool valid = false;          ///< Whether parsing succeeded
    double numericValue = 0.0;   ///< Numeric value (if directly parseable)
    QString expression;          ///< Original expression string
    bool isNumeric = false;      ///< True if value is a plain number
    bool isParameter = false;    ///< True if value is a parameter name
    bool isExpression = false;   ///< True if value is a parenthesized expression
};

/// Parse a value string that may be a number, parameter name, or expression
/// @param str Input string to parse
/// @return ParsedValue with parsing result
///
/// Examples:
/// - "25" -> { valid=true, numericValue=25, isNumeric=true }
/// - "myRadius" -> { valid=true, expression="myRadius", isParameter=true }
/// - "(width/2)" -> { valid=true, expression="(width/2)", isExpression=true }
HOBBYCAD_EXPORT ParsedValue parseValue(const QString& str);

/// Parse a value string and return numeric value if possible
/// @param str Input string to parse
/// @param value Output numeric value
/// @param expression Output expression string (for non-numeric values)
/// @return True if parsing succeeded
HOBBYCAD_EXPORT bool parseValue(const QString& str, double& value, QString& expression);

// =====================================================================
//  Coordinate Parsing
// =====================================================================

/// Result of parsing a coordinate pair
struct ParsedCoordinate {
    bool valid = false;
    ParsedValue x;
    ParsedValue y;
};

/// Split a coordinate string respecting parentheses
/// e.g., "(a+b),(c*d)" splits into ["(a+b)", "(c*d)"]
/// @param str Coordinate string like "x,y" or "(expr),(expr)"
/// @return List of parts (should have 2 elements for valid coordinates)
HOBBYCAD_EXPORT QStringList splitCoordinate(const QString& str);

/// Parse a coordinate string like "x,y" or "(expr1),(expr2)"
/// @param str Input coordinate string
/// @return ParsedCoordinate with x and y values
HOBBYCAD_EXPORT ParsedCoordinate parseCoordinate(const QString& str);

/// Parse a coordinate string and extract numeric values
/// @param str Input coordinate string
/// @param x Output X value
/// @param y Output Y value
/// @param xExpr Output X expression (optional)
/// @param yExpr Output Y expression (optional)
/// @return True if parsing succeeded
HOBBYCAD_EXPORT bool parseCoordinate(const QString& str, double& x, double& y,
                                      QString* xExpr = nullptr, QString* yExpr = nullptr);

/// Parse a coordinate string and return as QPointF
/// @param str Input coordinate string
/// @return Point if parsing succeeded, nullopt otherwise
HOBBYCAD_EXPORT std::optional<QPointF> parsePoint(const QString& str);

// =====================================================================
//  Identifier Validation
// =====================================================================

/// Check if a string is a valid parameter/identifier name
/// Must start with letter, contain only letters/digits/underscore
/// @param str String to check
/// @return True if valid identifier
HOBBYCAD_EXPORT bool isValidIdentifier(const QString& str);

/// Check if a string looks like a numeric value
/// @param str String to check
/// @return True if starts with digit, minus, or decimal point
HOBBYCAD_EXPORT bool looksNumeric(const QString& str);

/// Check if a string is a parenthesized expression
/// @param str String to check
/// @return True if starts with '(' and ends with ')'
HOBBYCAD_EXPORT bool isParenthesizedExpression(const QString& str);

}  // namespace sketch
}  // namespace hobbycad

#endif  // HOBBYCAD_SKETCH_PARSING_H
