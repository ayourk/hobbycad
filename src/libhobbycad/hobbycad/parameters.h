// =====================================================================
//  src/libhobbycad/hobbycad/parameters.h — Parametric value engine
// =====================================================================
//
//  Provides expression evaluation for parameter-driven design.
//  Parameters can reference other parameters, enabling formulas like:
//    width = 100
//    height = width * 0.5
//    depth = min(width, height) / 2
//
//  Features:
//  - Mathematical operators: + - * / ^ ( )
//  - Built-in functions: sin, cos, tan, sqrt, abs, floor, ceil, round,
//                        min, max, pow, log, exp, asin, acos, atan, atan2
//  - Constants: pi, e
//  - Trigonometric functions use degrees
//  - Dependency tracking and topological sorting
//  - Circular dependency detection
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_PARAMETERS_H
#define HOBBYCAD_PARAMETERS_H

#include "core.h"
#include "types.h"
#include "units.h"

#include <map>
#include <string>
#include <unordered_set>
#include <vector>

#if HOBBYCAD_HAS_QT
#include <QJsonObject>
#else
#include <nlohmann/json.hpp>
#endif

namespace hobbycad {

// =====================================================================
//  ParametricValue — a value that may be a number, parameter, or formula
// =====================================================================

/// Represents a parametric value that can be a number, parameter, or formula.
/// Pure computation: no GUI dependencies.
/// Rewrite whole-identifier occurrences of @p from to @p to in @p expr.
///
/// Used when a parameter is renamed: every expression that referenced it
/// must follow, or the rename silently breaks them or, worse, repoints
/// them at a different parameter that later takes the freed name.
///
/// Matching is on identifier boundaries, not substrings. Renaming "w" to
/// "wide" turns `w * 2` into `wide * 2` while leaving `width` and `w2`
/// untouched. A naive find-and-replace corrupts those silently, producing
/// an expression that still parses and evaluates to the wrong thing.
HOBBYCAD_EXPORT std::string renameIdentifierInExpression(
    const std::string& expr,
    const std::string& from,
    const std::string& to);

class HOBBYCAD_EXPORT ParametricValue {
public:
    enum class Type {
        Number,      // Plain numeric value
        Parameter,   // Named parameter reference
        Formula      // Mathematical expression
    };

    ParametricValue() = default;
    explicit ParametricValue(double value);
    explicit ParametricValue(const std::string& expression);

    /// Get the type of this value
    Type type() const { return m_type; }

    /// Get the raw expression string
    std::string expression() const { return m_expression; }

    /// Get the numeric value (evaluates if formula/parameter)
    double value() const { return m_value; }

    /// Check if the expression is valid
    bool isValid() const { return m_valid; }

    /// Get error message if invalid
    std::string errorMessage() const { return m_errorMessage; }

    /// Set the expression and re-evaluate
    void setExpression(const std::string& expr);

    /// Evaluate the expression with given parameter values
    bool evaluate(const std::map<std::string, double>& parameters);

    /// Check if expression contains any parameters
    bool containsParameters() const;

    /// Get list of parameter names used in the expression
    std::vector<std::string> usedParameters() const;

private:
    void parse();

    Type m_type = Type::Number;
    std::string m_expression;
    double m_value = 0.0;
    bool m_valid = true;
    std::string m_errorMessage;
    std::vector<std::string> m_usedParams;
};

// =====================================================================
//  Parameter — a named parameter in the engine
// =====================================================================

/// A single parameter definition
struct Parameter {
    std::string name;                   ///< Parameter name (identifier)
    std::string expression;             ///< Expression string (number or formula)
    double value = 0.0;                 ///< Evaluated numeric value
    std::string unit;                   ///< Unit type (mm, deg, etc.)
    std::string comment;                ///< User description
    bool isUserParam = true;            ///< User vs model-generated parameter
    bool isValid = true;                ///< Expression evaluated successfully
    std::string errorMessage;           ///< Error description if invalid
    std::vector<std::string> dependencies;  ///< Parameters this depends on
    bool isReference = false;            ///< Value is MEASURED from the solved
                                         ///< model (set via setReferenceValue
                                         ///< after each solve), not authored.
                                         ///< Read-only; usable in other
                                         ///< parameters' expressions.
    std::string referenceSource;         ///< App-defined descriptor of what
                                         ///< geometry it measures (opaque here).
};

/// Result of parameter evaluation
struct EvaluationResult {
    bool success = false;               ///< All parameters evaluated successfully
    int errorCount = 0;                 ///< Number of parameters with errors
    std::vector<std::string> errorMessages;      ///< Detailed error messages
    std::vector<std::string> evaluationOrder;    ///< Order parameters were evaluated
};

/// Parameter engine for expression evaluation and dependency tracking
class HOBBYCAD_EXPORT ParameterEngine {
public:
    ParameterEngine();
    ~ParameterEngine();

    // ---- Parameter management ----

    /// Set all parameters (replaces existing)
    void setParameters(const std::vector<Parameter>& params);

    /// Get all parameters
    std::vector<Parameter> parameters() const;

    /// Add or update a parameter
    void setParameter(const std::string& name, const std::string& expression,
                      const std::string& unit = {},
                      const std::string& comment = {});

    /// Remove a parameter
    void removeParameter(const std::string& name);

    /// Add or update a REFERENCE parameter: its value is measured from the
    /// solved model (pushed via setReferenceValue after each solve), not
    /// authored. It has no expression, is read-only, and may be used in other
    /// parameters' expressions like any leaf value.
    void setReferenceParameter(const std::string& name,
                               const std::string& referenceSource = {},
                               const std::string& unit = {},
                               const std::string& comment = {});

    /// Set a reference parameter's measured value (call after each solve, then
    /// evaluate() to propagate into dependent expressions). No-op if @p name is
    /// not a reference parameter.
    void setReferenceValue(const std::string& name, double value);

    /// Clear all parameters
    void clear();

    /// Check if a parameter exists
    bool hasParameter(const std::string& name) const;

    /// Get a parameter by name (returns nullptr if not found)
    const Parameter* parameter(const std::string& name) const;

    /// Get parameter value by name (returns 0 if not found)
    double value(const std::string& name) const;

    // ---- Evaluation ----

    /// Evaluate all parameters
    /// Returns evaluation result with success/error info
    EvaluationResult evaluate();

    /// Evaluate a single expression with current parameter values
    /// @param expression The expression to evaluate
    /// @param result Output: the numeric result
    /// @param errorMsg Output: error message if evaluation fails
    /// @return true if evaluation succeeded
    bool evaluateExpression(const std::string& expression, double& result,
                            std::string* errorMsg = nullptr) const;

    /// Evaluate a single expression with unit-aware number parsing.
    /// Bare numbers are treated as being in defaultUnit (no conversion).
    /// Numbers with explicit unit suffixes (mm, cm, m, in, ft) are converted
    /// to defaultUnit so they integrate correctly in the expression.
    /// The result is in defaultUnit; the caller must convert to mm.
    /// @param expression The expression to evaluate
    /// @param result Output: the numeric result in defaultUnit
    /// @param defaultUnit Unit assumed for bare numbers (no suffix)
    /// @param errorMsg Output: error message if evaluation fails
    /// @return true if evaluation succeeded
    bool evaluateExpression(const std::string& expression, double& result,
                            LengthUnit defaultUnit,
                            std::string* errorMsg = nullptr) const;

    /// Get the evaluation order (topologically sorted)
    std::vector<std::string> evaluationOrder() const;

    // ---- Dependency analysis ----

    /// Get parameters that the given parameter depends on
    std::vector<std::string> dependenciesOf(const std::string& name) const;

    /// Get parameters that depend on the given parameter
    std::vector<std::string> dependentsOf(const std::string& name) const;

    /// Check if there are any circular dependencies
    bool hasCircularDependencies() const;

    /// Get circular dependency chain (if any)
    std::vector<std::string> circularDependencyChain() const;

    // ---- Validation ----

    /// Check if a name is a valid parameter name
    static bool isValidName(const std::string& name);

    /// Check if expression syntax is valid (without evaluating)
    bool isValidSyntax(const std::string& expression, std::string* errorMsg = nullptr) const;

    /// Get list of parameter names used in an expression
    static std::vector<std::string> usedParameters(const std::string& expression);

    // ---- Import/Export ----

#if HOBBYCAD_HAS_QT
    /// Export parameters to JSON
    QJsonObject toJson() const;

    /// Import parameters from JSON
    bool fromJson(const QJsonObject& json, std::string* errorMsg = nullptr);
#else
    /// Export parameters to JSON (non-Qt fallback using nlohmann/json)
    nlohmann::json toJson() const;

    /// Import parameters from JSON (non-Qt fallback using nlohmann/json)
    bool fromJson(const nlohmann::json& json, std::string* errorMsg = nullptr);
#endif

private:
    class Impl;
    Impl* d;
};

// ---- Standalone expression evaluation ----

/// Evaluate an expression with the given parameter values.
/// Does not require a ParameterEngine instance.
/// @param expression The expression to evaluate
/// @param result Output: the numeric result
/// @param params Parameter name->value map for variable resolution
/// @param errorMsg Output: error message if evaluation fails
/// @return true if evaluation succeeded
bool evaluateExpression(const std::string& expression, double& result,
                        const std::map<std::string, double>& params,
                        std::string* errorMsg = nullptr);

/// True when `expression` reads a parameter: it evaluates with `params` but
/// not without them, or to a different value. Decides whether the source text
/// is kept so a dimension can follow the parameter later.
bool expressionUsesParameters(const std::string& expression,
                              const std::map<std::string, double>& params);

}  // namespace hobbycad

#endif  // HOBBYCAD_PARAMETERS_H
