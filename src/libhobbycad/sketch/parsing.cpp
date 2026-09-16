// =====================================================================
//  src/libhobbycad/sketch/parsing.cpp — Text parsing utilities
// =====================================================================
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include "hobbycad/units.h"
#include "hobbycad/parameters.h"
#include <hobbycad/sketch/parsing.h>

#include <cstdlib>

#include <cctype>
#include <cmath>
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
        // Reject hexadecimal before stod sees it. strtod accepts "0xDD" and
        // returns 221, so a hex-looking value was already being converted
        // SILENTLY, and "0x10" meant 16, not 10. A dimension written in
        // hex is a typo or a paste from the wrong place; a bit pattern
        // belongs wherever bit patterns are set, not in a length.
        if (expr.size() > 1 && expr[0] == '0'
            && (expr[1] == 'x' || expr[1] == 'X')) {
            return result;   // invalid
        }

        // Reject a leading zero followed by another digit: "000", "050",
        // "007". Nobody writes a coordinate that way, but it is exactly
        // how the second group of a thousands-separated number looks, and
        // the comma is the coordinate separator, so "1,000" was parsing
        // SILENTLY as the point (1, 0) when the user meant one thousand.
        //
        // "2,500" stays valid: 500 has no leading zero and is a perfectly
        // ordinary y coordinate. Only the ambiguous shape is refused, and
        // it is refused rather than guessed at.
        // Underscore as a digit-group separator: 1_000_000. Chosen over a
        // comma because a comma already separates x from y; a character
        // meaning "digit group" in one place and "next coordinate" in
        // another is what made "1,000" parse silently as the point (1, 0).
        // Python, Rust, Java and Ada all use '_'; C++ uses '\''.
        if (expr.find('_') != std::string::npos) {
            std::string stripped;
            for (size_t i = 0; i < expr.size(); ++i) {
                if (expr[i] != '_') { stripped += expr[i]; continue; }
                // Only BETWEEN digits, so "_5" and "5_" stay errors.
                const bool between =
                    i > 0 && i + 1 < expr.size()
                    && std::isdigit(static_cast<unsigned char>(expr[i - 1]))
                    && std::isdigit(static_cast<unsigned char>(expr[i + 1]));
                if (!between) { return result; }
            }
            expr = stripped;
            result.expression = expr;
        }

        {
            const size_t first = (expr[0] == '-' || expr[0] == '+') ? 1 : 0;
            if (expr.size() > first + 1
                && expr[first] == '0'
                && std::isdigit(static_cast<unsigned char>(expr[first + 1]))) {
                return result;   // invalid
            }
        }

        try {
            size_t pos = 0;
            result.numericValue = std::stod(expr, &pos);

            if (pos == expr.size()) {
                result.valid = true;
                result.isNumeric = true;
                return result;
            }

            // Everything after the number may be a unit. This is what lets
            // "10mm", "1in" and "2.5ft" be written directly rather than
            // converted by hand; the value is stored in mm either way,
            // which is the project's storage convention.
            const std::string suffix = expr.substr(pos);
            if (isKnownUnitSuffix(suffix)) {
                result.numericValue =
                    unitToMm(result.numericValue, parseUnitSuffix(suffix));
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

    // Contract: a plain number returns its value with an EMPTY expression; an
    // expression or a parameter name returns the formula in `expression` with
    // `value` left 0 because it has NOT been evaluated here; the caller must
    // resolve it (e.g. through the parameter engine). Returning a non-empty
    // expression is the signal; this is what stops a symbolic input from
    // silently reading back as 0. [maintainability audit 2026-09-09]
    if (parsed.isExpression || parsed.isParameter) {
        expression = parsed.expression;
        value = 0.0;
    } else {
        expression.clear();
        value = parsed.numericValue;
    }
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

/// Strip one layer of [ ] from an explicit point, if present.
///
/// Aaron, 2026-08-27: *"essentially [ vs ("*. Brackets mark a POINT and
/// parentheses group MATH, so "[(width*2), 0]" is unambiguous where
/// "(width*2), 0" needs the reader to know which comma is which.
///
/// Accepted alongside the bare form rather than replacing it: every CAD
/// format and command line writes a point as "x,y", and requiring brackets
/// would make the most common operation longer for no gain in the common
/// case.
static std::string stripPointBrackets(const std::string& str)
{
    const std::string t = trim(str);
    if (t.size() >= 2 && t.front() == '[' && t.back() == ']') {
        return trim(t.substr(1, t.size() - 2));
    }
    return t;
}

ParsedCoordinate parseCoordinate(const std::string& str)
{
    const std::string bracketless = stripPointBrackets(str);
    ParsedCoordinate result;

    std::vector<std::string> parts = splitCoordinate(bracketless);
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


// =====================================================================
//  Reference-parameter measurement
// =====================================================================

bool measureReferenceSource(
    const std::string& source,
    const std::function<bool(int, int, Point2D&)>& resolve,
    double& out)
{
    // Tokenize on runs of whitespace.
    std::vector<std::string> tok;
    std::string cur;
    for (char c : source) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!cur.empty()) { tok.push_back(cur); cur.clear(); }
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) tok.push_back(cur);

    // Only "distance <ptA> <ptB>" is understood today.
    if (tok.size() != 3) return false;
    std::string kw = tok[0];
    for (char& ch : kw)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if (kw != "distance") return false;

    // "<entityId>" or "<entityId>.<pointIndex>" -> a solved point via resolve.
    auto parseRef = [&resolve](const std::string& t, Point2D& p) -> bool {
        int id = 0, idx = 0;
        if (parsePointRef(t, id, idx) != PointRefProblem::None) return false;
        return resolve(id, idx, p);
    };

    Point2D a, b;
    if (!parseRef(tok[1], a) || !parseRef(tok[2], b)) return false;
    out = std::hypot(a.x - b.x, a.y - b.y);
    return true;
}

// =====================================================================
//  Resolving against a document
// =====================================================================

namespace {

std::string trimmed(const std::string& s)
{
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

/// Whole-string integer parse (optional sign, digits, nothing else).
bool parseWholeInt(const std::string& s, int& out)
{
    if (s.empty()) return false;
    char* end = nullptr;
    const long v = std::strtol(s.c_str(), &end, 10);
    if (!end || *end != '\0') return false;
    out = static_cast<int>(v);
    return true;
}

}  // namespace

bool resolveValue(const std::string& str,
                  const std::map<std::string, double>& params,
                  double& value, std::string* expression)
{
    const ParsedValue parsed = parseValue(str);
    if (!parsed.valid) return false;
    if (expression) *expression = parsed.expression;
    if (parsed.isNumeric) {
        value = parsed.numericValue;   // units already applied
        return true;
    }
    return evaluateExpression(parsed.expression, value, params);
}

bool resolveCoordinate(const std::string& str,
                       const std::map<std::string, double>& params,
                       const NamedPoints& named,
                       double& x, double& y,
                       std::string* xExpr, std::string* yExpr)
{
    // A lone named-coordinate identifier expands to its point; a 2D
    // context uses its x and y.
    const auto it = named.find(trimmed(str));
    if (it != named.end()) { x = it->second[0]; y = it->second[1]; return true; }

    const ParsedCoordinate parsed = parseCoordinate(str);
    if (!parsed.valid) {
        // Halves that are BARE formulas ("w/2", "3+5"): parseCoordinate only
        // accepts a parenthesized formula or a bare parameter, so evaluate
        // the halves directly. Bracket-aware.
        const std::vector<std::string> parts = splitCoordinate(str);
        if (parts.size() != 2) return false;
        return evaluateExpression(parts[0], x, params) && evaluateExpression(parts[1], y, params);
    }

    // Each half may be a literal or a formula; evaluate the ones that are
    // not already numbers.
    if (parsed.x.isNumeric) x = parsed.x.numericValue;
    else if (!evaluateExpression(parsed.x.expression, x, params)) return false;
    if (parsed.y.isNumeric) y = parsed.y.numericValue;
    else if (!evaluateExpression(parsed.y.expression, y, params)) return false;

    if (xExpr) *xExpr = parsed.x.expression;
    if (yExpr) *yExpr = parsed.y.expression;
    return true;
}

bool resolveCoordinate3(const std::string& str,
                        const std::map<std::string, double>& params,
                        const NamedPoints& named,
                        double& x, double& y, double& z)
{
    const auto it = named.find(trimmed(str));
    if (it != named.end()) {
        x = it->second[0]; y = it->second[1]; z = it->second[2];
        return true;
    }
    const std::vector<std::string> parts = splitCoordinate(str);
    if (parts.size() != 3) return false;
    return evaluateExpression(parts[0], x, params)
        && evaluateExpression(parts[1], y, params)
        && evaluateExpression(parts[2], z, params);
}

ConstraintValueProblem resolveConstraintValue(const std::string& token, ConstraintType type,
                                              const std::map<std::string, double>& params,
                                              double& value, std::string& text,
                                              std::string* badPart)
{
    const bool angular = isAngularConstraint(type);

    // Split a trailing suffix off a leading number, if that is the shape.
    size_t split = std::string::npos;
    for (size_t i = 0; i < token.size(); ++i) {
        const char c = token[i];
        if (!std::isdigit(static_cast<unsigned char>(c)) && c != '.' && c != '-' && c != '+') {
            split = i;
            break;
        }
    }

    if (split != std::string::npos && split > 0) {
        const std::string suffix = token.substr(split);
        if (isKnownAngleSuffix(suffix)) {
            if (!angular) return ConstraintValueProblem::AngleForLength;
            const std::string numberPart = token.substr(0, split);
            char* end = nullptr;
            const double n = std::strtod(numberPart.c_str(), &end);
            if (!end || *end != '\0') {
                if (badPart) *badPart = numberPart;
                return ConstraintValueProblem::NotANumber;
            }
            value = angleSuffixToDegrees(n, suffix);
            text = token;
            return ConstraintValueProblem::None;
        }
        if (isKnownUnitSuffix(suffix) && angular) return ConstraintValueProblem::LengthForAngle;
    }

    if (!resolveValue(token, params, value, &text)) return ConstraintValueProblem::Invalid;
    return ConstraintValueProblem::None;
}

namespace {

/// Split a trailing unit or angle word (letters, or the degree sign) off an
/// expression: "10 mm" -> ("10", "mm"), "w/2" -> ("w/2", ""). A trailing word
/// that is not a known unit stays part of the expression (a parameter name).
void splitUnitSuffix(const std::string& text, std::string& expr, std::string& suffix)
{
    std::string t = trimmed(text);
    expr = t; suffix.clear();
    // The degree sign is two bytes in UTF-8 (0xC2 0xB0).
    if (t.size() >= 2 && static_cast<unsigned char>(t[t.size() - 2]) == 0xC2
        && static_cast<unsigned char>(t[t.size() - 1]) == 0xB0) {
        expr = trimmed(t.substr(0, t.size() - 2));
        suffix = "deg";
        return;
    }
    size_t i = t.size();
    while (i > 0 && std::isalpha(static_cast<unsigned char>(t[i - 1]))) --i;
    if (i == 0 || i == t.size()) return;                 // all letters, or no letters at the end
    const std::string word = t.substr(i);
    if (!isKnownUnitSuffix(word) && !isKnownAngleSuffix(word)) return;
    expr = trimmed(t.substr(0, i));
    suffix = word;
}

}  // namespace

bool resolveMeasurement(const std::string& text, MeasureKind kind, LengthUnit defaultUnit,
                        const std::map<std::string, double>& params, double& out)
{
    std::string expr, suffix;
    splitUnitSuffix(text, expr, suffix);
    if (expr.empty()) return false;
    double v = 0.0;
    if (!evaluateExpression(expr, v, params) || !std::isfinite(v)) return false;

    switch (kind) {
    case MeasureKind::Length:
        if (!suffix.empty() && isKnownAngleSuffix(suffix)) return false;   // an angle in a length field
        out = v * unitScale(suffix.empty() ? defaultUnit : parseUnitSuffix(suffix));
        return true;
    case MeasureKind::Angle:
        if (!suffix.empty() && !isKnownAngleSuffix(suffix)) return false;  // a length in an angle field
        out = suffix.empty() ? v : angleSuffixToDegrees(v, suffix);
        return true;
    case MeasureKind::Count:
        if (!suffix.empty()) return false;
        out = v;
        return true;
    }
    return false;
}

bool resolveMeasuredPoint(const std::string& text, LengthUnit defaultUnit,
                          const std::map<std::string, double>& params, Point2D& out)
{
    // A unit trailing the pair applies to both halves: "(10, 20) mm".
    std::string body, suffix;
    splitUnitSuffix(text, body, suffix);
    if (!suffix.empty() && !isKnownUnitSuffix(suffix)) return false;
    if (!body.empty() && body.front() == '(' && body.back() == ')')
        body = body.substr(1, body.size() - 2);
    const std::vector<std::string> parts = splitCoordinate(body);
    if (parts.size() != 2) return false;
    const LengthUnit unit = suffix.empty() ? defaultUnit : parseUnitSuffix(suffix);
    double x = 0.0, y = 0.0;
    if (!resolveMeasurement(parts[0], MeasureKind::Length, unit, params, x)) return false;
    if (!resolveMeasurement(parts[1], MeasureKind::Length, unit, params, y)) return false;
    out = Point2D(x, y);
    return true;
}

PointRefProblem parsePointRef(const std::string& text, int& entityId, int& pointIndex,
                              bool* hadPoint)
{
    std::string e = text;
    int idx = 0;
    const auto dot = text.find('.');
    if (hadPoint) *hadPoint = (dot != std::string::npos);
    if (dot != std::string::npos) {
        e = text.substr(0, dot);
        if (!parseWholeInt(text.substr(dot + 1), idx) || idx < 0)
            return PointRefProblem::BadPointIndex;
    }
    int id = 0;
    if (!parseWholeInt(e, id)) return PointRefProblem::BadEntityId;
    entityId = id;
    pointIndex = idx;
    return PointRefProblem::None;
}

}  // namespace sketch
}  // namespace hobbycad
