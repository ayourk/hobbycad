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
#include "../types.h"
#include "../units.h"
#include "constraint.h"

#include <array>
#include <cctype>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace hobbycad {
namespace sketch {

// =====================================================================
//  Value Parsing
// =====================================================================

/// Result of parsing a value that may be a number, parameter, or expression
struct ParsedValue {
    bool valid = false;          ///< Whether parsing succeeded
    double numericValue = 0.0;   ///< Numeric value (if directly parseable)
    std::string expression;      ///< Original expression string
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
HOBBYCAD_EXPORT ParsedValue parseValue(const std::string& str);

/// Parse a value string into either a number or a deferred formula.
/// @param str        Input string to parse
/// @param value      Output numeric value: set ONLY for a plain number;
///                   left 0 for an expression or parameter (not evaluated here)
/// @param expression Output formula: EMPTY for a plain number, the formula
///                   string for an expression or parameter name
/// @return True if parsing succeeded
/// @note If `expression` comes back non-empty the input is a formula the caller
///       must resolve; `value` is meaningful only when `expression` is empty.
HOBBYCAD_EXPORT bool parseValue(const std::string& str, double& value, std::string& expression);

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
HOBBYCAD_EXPORT std::vector<std::string> splitCoordinate(const std::string& str);

/// Parse a coordinate string like "x,y" or "(expr1),(expr2)"
/// @param str Input coordinate string
/// @return ParsedCoordinate with x and y values
HOBBYCAD_EXPORT ParsedCoordinate parseCoordinate(const std::string& str);

/// Parse a coordinate string and extract numeric values
/// @param str Input coordinate string
/// @param x Output X value
/// @param y Output Y value
/// @param xExpr Output X expression (optional)
/// @param yExpr Output Y expression (optional)
/// @return True if parsing succeeded
HOBBYCAD_EXPORT bool parseCoordinate(const std::string& str, double& x, double& y,
                                      std::string* xExpr = nullptr, std::string* yExpr = nullptr);

/// Parse a coordinate string and return as Point2D
/// @param str Input coordinate string
/// @return Point if parsing succeeded, nullopt otherwise
HOBBYCAD_EXPORT std::optional<Point2D> parsePoint(const std::string& str);

// =====================================================================
//  Resolving against a document
// =====================================================================

/// Named coordinates ("origin", "corner") as a front end has evaluated them.
using NamedPoints = std::map<std::string, std::array<double, 3>>;

/// Resolve a value token to a number: a literal (unit suffix applied), a
/// parameter name, or a formula, evaluated against `params`. `expression`,
/// when given, receives the formula text (empty for a literal) so a caller
/// can store the parametric form.
HOBBYCAD_EXPORT bool resolveValue(const std::string& str,
                                  const std::map<std::string, double>& params,
                                  double& value, std::string* expression = nullptr);

/// Resolve "x,y" to numbers. A lone named-coordinate identifier expands to
/// its x and y (named points are tried before the coordinate grammar, so a
/// parameter called "origin" cannot shadow the point). Each half may be a
/// literal, a parameter, a parenthesized formula, or a bare formula ("w/2").
/// `xExpr` / `yExpr` receive the halves' formula text when the coordinate
/// grammar classified them (empty for literals).
HOBBYCAD_EXPORT bool resolveCoordinate(const std::string& str,
                                       const std::map<std::string, double>& params,
                                       const NamedPoints& named,
                                       double& x, double& y,
                                       std::string* xExpr = nullptr, std::string* yExpr = nullptr);

/// Resolve "x,y,z" (or a named point) to numbers; each component a literal,
/// parameter or formula. Bracket-aware: a function's internal commas are
/// respected.
HOBBYCAD_EXPORT bool resolveCoordinate3(const std::string& str,
                                        const std::map<std::string, double>& params,
                                        const NamedPoints& named,
                                        double& x, double& y, double& z);

/// Why resolveConstraintValue() refused.
enum class ConstraintValueProblem {
    None,
    AngleForLength,   ///< an angle suffix on a length constraint
    LengthForAngle,   ///< a length suffix on an angular constraint
    NotANumber,       ///< an angle suffix on something that is not a number (see badPart)
    Invalid           ///< not a number, parameter or expression
};

/// Resolve a constraint's value token, honoring and CHECKING its unit: an
/// angle suffix ("45deg", "0.5rad") is converted to degrees and only
/// accepted on an angular constraint; a length suffix is refused on one.
/// Otherwise the token resolves like any value (number, parameter,
/// formula) through resolveValue(), lengths in millimeters. `text`
/// receives the token's parametric form. `badPart`, when given, receives
/// the offending fragment for NotANumber.
HOBBYCAD_EXPORT ConstraintValueProblem resolveConstraintValue(const std::string& token,
                                                              ConstraintType type,
                                                              const std::map<std::string, double>& params,
                                                              double& value, std::string& text,
                                                              std::string* badPart = nullptr);

// =====================================================================
//  Measurements typed into a field
// =====================================================================

/// What a field measures, which decides its unit handling.
enum class MeasureKind {
    Length,   ///< millimeters out; a unit suffix converts, none means `defaultUnit`
    Angle,    ///< degrees out; "deg" / "°" / "rad" accepted, a length unit refused
    Count     ///< a plain number (sides), no unit
};

/// Read a field the way the Parameters dialog reads an expression: the text
/// is an expression over the document's parameters (bare formulas allowed,
/// "width/2", "2*r + 1"), optionally followed by a unit ("10 mm", "2 in",
/// "45 deg", "90°"). Lengths come back in millimeters, a length with no unit
/// being in `defaultUnit` (what the field displays). Angles come back in
/// degrees. A length unit on an angle, or an angle unit on a length, is
/// refused, as is anything that does not evaluate.
HOBBYCAD_EXPORT bool resolveMeasurement(const std::string& text, MeasureKind kind,
                                        LengthUnit defaultUnit,
                                        const std::map<std::string, double>& params,
                                        double& out);

/// "(x, y) mm", "x, y" or "(x, y)": two lengths read as above, one unit for
/// both when it trails the pair. Millimeters out.
HOBBYCAD_EXPORT bool resolveMeasuredPoint(const std::string& text, LengthUnit defaultUnit,
                                          const std::map<std::string, double>& params,
                                          Point2D& out);

/// Why parsePointRef() refused.
enum class PointRefProblem { None, BadEntityId, BadPointIndex };

/// Parse "<entityId>" or "<entityId>.<pointIndex>" (the CLI's way of naming
/// a point on an entity). pointIndex is 0 when absent and must not be
/// negative; `hadPoint`, when given, says whether one was written.
HOBBYCAD_EXPORT PointRefProblem parsePointRef(const std::string& text,
                                              int& entityId, int& pointIndex,
                                              bool* hadPoint = nullptr);

// =====================================================================
//  Identifier Validation
// =====================================================================

/// Check if a string is a valid parameter/identifier name
/// Must start with letter, contain only letters/digits/underscore
/// @param str String to check
/// @return True if valid identifier
HOBBYCAD_EXPORT bool isValidIdentifier(const std::string& str);

/// Check if a string looks like a numeric value
/// @param str String to check
/// @return True if starts with digit, minus, or decimal point
HOBBYCAD_EXPORT bool looksNumeric(const std::string& str);

/// Check if a string is a parenthesized expression
/// @param str String to check
/// @return True if starts with '(' and ends with ')'
HOBBYCAD_EXPORT bool isParenthesizedExpression(const std::string& str);

/// Measure a reference-parameter source string against solved geometry.
/// The only form understood today is "distance <ptA> <ptB>", each point written
/// "<entityId>" or "<entityId>.<pointIndex>". @p resolve maps an
/// (entityId, pointIndex) to its solved Point2D (returning false when it does
/// not exist); passing the resolver keeps this helper free of any entity-
/// container type, so the CLI and the GUI share one implementation. Returns
/// true and sets @p out (the measured distance) on success.
HOBBYCAD_EXPORT bool measureReferenceSource(
    const std::string& source,
    const std::function<bool(int entityId, int pointIndex, Point2D& out)>& resolve,
    double& out);

// =====================================================================
//  Command Tokenization
// =====================================================================

/// Tokenize a command line while respecting parenthesized expressions.
/// Splits on whitespace but keeps parenthesized sub-expressions intact.
///
/// Example: "circle (a + b),(c * d) radius (r * 2)"
///       -> ["circle", "(a + b),(c * d)", "radius", "(r * 2)"]
///
/// @param line Input command string
/// @return List of tokens
inline std::vector<std::string> tokenizeLine(const std::string& line)
{
    std::vector<std::string> tokens;
    std::string current;
    int parenDepth = 0;
    bool inQuote = false;
    bool inToken = false;

    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];

        if (c == '"' && parenDepth == 0) {
            // Toggle quote mode: quotes are stripped, content kept as one token
            inQuote = !inQuote;
            inToken = true;
        } else if (inQuote) {
            // Inside quotes: everything is part of the current token
            current += c;
        } else if (c == '(') {
            parenDepth++;
            current += c;
            inToken = true;
        } else if (c == ')') {
            parenDepth--;
            current += c;
            inToken = true;
        } else if (std::isspace(static_cast<unsigned char>(c)) && parenDepth == 0) {
            // End of token (unless inside parentheses)
            if (inToken && !current.empty()) {
                tokens.push_back(current);
                current.clear();
                inToken = false;
            }
        } else {
            current += c;
            inToken = true;
        }
    }

    // Don't forget the last token
    if (!current.empty()) {
        tokens.push_back(current);
    }

    return tokens;
}

/// Rejoin coordinate fragments that tokenizeLine split at spaces around
/// commas, so every coordinate parser is space-tolerant without per-command
/// code: ["3,","4,","5"] and ["3",",","4"] each merge to one token.
/// Two tokens join when the boundary between them is a comma (left ends
/// with ',' or right begins with ','). splitCoordinate handles the rest,
/// bracket-aware.
inline std::vector<std::string> mergeCoordinateTokens(std::vector<std::string> tokens)
{
    std::vector<std::string> merged;
    merged.reserve(tokens.size());
    for (size_t k = 0; k < tokens.size(); ++k) {
        std::string cur = std::move(tokens[k]);
        while (k + 1 < tokens.size()) {
            const std::string& nxt = tokens[k + 1];
            const bool curEndsComma = !cur.empty() && cur.back() == ',';
            const bool nxtStartsComma = !nxt.empty() && nxt.front() == ',';
            if (!curEndsComma && !nxtStartsComma) break;
            cur += nxt;
            ++k;
        }
        merged.push_back(std::move(cur));
    }
    return merged;
}

}  // namespace sketch
}  // namespace hobbycad

#endif  // HOBBYCAD_SKETCH_PARSING_H
