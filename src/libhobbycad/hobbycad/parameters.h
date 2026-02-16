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

#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>

namespace hobbycad {

/// A single parameter definition
struct Parameter {
    QString name;                   ///< Parameter name (identifier)
    QString expression;             ///< Expression string (number or formula)
    double value = 0.0;             ///< Evaluated numeric value
    QString unit;                   ///< Unit type (mm, deg, etc.)
    QString comment;                ///< User description
    bool isUserParam = true;        ///< User vs model-generated parameter
    bool isValid = true;            ///< Expression evaluated successfully
    QString errorMessage;           ///< Error description if invalid
    QStringList dependencies;       ///< Parameters this depends on
};

/// Result of parameter evaluation
struct EvaluationResult {
    bool success = false;           ///< All parameters evaluated successfully
    int errorCount = 0;             ///< Number of parameters with errors
    QStringList errorMessages;      ///< Detailed error messages
    QStringList evaluationOrder;    ///< Order parameters were evaluated
};

/// Parameter engine for expression evaluation and dependency tracking
class HOBBYCAD_EXPORT ParameterEngine {
public:
    ParameterEngine();
    ~ParameterEngine();

    // ---- Parameter management ----

    /// Set all parameters (replaces existing)
    void setParameters(const QList<Parameter>& params);

    /// Get all parameters
    QList<Parameter> parameters() const;

    /// Add or update a parameter
    void setParameter(const QString& name, const QString& expression,
                      const QString& unit = QString(),
                      const QString& comment = QString());

    /// Remove a parameter
    void removeParameter(const QString& name);

    /// Clear all parameters
    void clear();

    /// Check if a parameter exists
    bool hasParameter(const QString& name) const;

    /// Get a parameter by name (returns nullptr if not found)
    const Parameter* parameter(const QString& name) const;

    /// Get parameter value by name (returns 0 if not found)
    double value(const QString& name) const;

    // ---- Evaluation ----

    /// Evaluate all parameters
    /// Returns evaluation result with success/error info
    EvaluationResult evaluate();

    /// Evaluate a single expression with current parameter values
    /// @param expression The expression to evaluate
    /// @param result Output: the numeric result
    /// @param errorMsg Output: error message if evaluation fails
    /// @return true if evaluation succeeded
    bool evaluateExpression(const QString& expression, double& result,
                            QString* errorMsg = nullptr) const;

    /// Get the evaluation order (topologically sorted)
    QStringList evaluationOrder() const;

    // ---- Dependency analysis ----

    /// Get parameters that the given parameter depends on
    QStringList dependenciesOf(const QString& name) const;

    /// Get parameters that depend on the given parameter
    QStringList dependentsOf(const QString& name) const;

    /// Check if there are any circular dependencies
    bool hasCircularDependencies() const;

    /// Get circular dependency chain (if any)
    QStringList circularDependencyChain() const;

    // ---- Validation ----

    /// Check if a name is a valid parameter name
    static bool isValidName(const QString& name);

    /// Check if expression syntax is valid (without evaluating)
    bool isValidSyntax(const QString& expression, QString* errorMsg = nullptr) const;

    /// Get list of parameter names used in an expression
    static QStringList usedParameters(const QString& expression);

    // ---- Import/Export ----

    /// Export parameters to JSON
    QJsonObject toJson() const;

    /// Import parameters from JSON
    bool fromJson(const QJsonObject& json, QString* errorMsg = nullptr);

private:
    class Impl;
    Impl* d;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_PARAMETERS_H
