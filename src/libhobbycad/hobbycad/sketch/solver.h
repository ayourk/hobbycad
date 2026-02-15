// =====================================================================
//  src/libhobbycad/hobbycad/sketch/solver.h — Constraint solver wrapper
// =====================================================================
//
//  Wrapper around libslvs for parametric constraint solving.
//  This allows sketches to maintain geometric relationships as
//  entities are modified.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_SKETCH_SOLVER_H
#define HOBBYCAD_SKETCH_SOLVER_H

#include "entity.h"
#include "constraint.h"
#include "../core.h"

#include <QVector>
#include <QPointF>
#include <QMap>
#include <QString>
#include <functional>

namespace hobbycad {
namespace sketch {

// =====================================================================
//  Solver Result Types
// =====================================================================

/// Result of constraint solving
struct SolveResult {
    bool success = false;
    int dof = 0;                           ///< Degrees of freedom remaining
    QString errorMessage;
    QVector<int> failedConstraintIds;      ///< IDs of constraints that couldn't be satisfied

    /// Solver result codes (matches libslvs)
    enum ResultCode {
        Okay = 0,               ///< Solved successfully
        Inconsistent = 1,       ///< Constraints conflict with each other
        DidntConverge = 2,      ///< Solver failed to find a solution
        TooManyUnknowns = 3,    ///< System too complex
        RedundantOkay = 4       ///< Redundant but solvable (over-constrained)
    };
    ResultCode resultCode = Okay;
};

/// Result of over-constraint check
struct OverConstraintInfo {
    bool wouldOverConstrain = false;
    QVector<int> conflictingConstraintIds;  ///< IDs of existing constraints causing the conflict
    QString reason;                          ///< Human-readable explanation
};

// =====================================================================
//  Solver Class
// =====================================================================

/// Wrapper around libslvs constraint solver
///
/// The solver takes a set of entities and constraints, and adjusts
/// entity geometry to satisfy all constraints while minimizing
/// deviation from the original positions.
///
/// Example usage:
/// @code
///     Solver solver;
///     QVector<Entity> entities = { ... };
///     QVector<Constraint> constraints = { ... };
///
///     SolveResult result = solver.solve(entities, constraints);
///     if (result.success) {
///         // entities have been modified to satisfy constraints
///     } else {
///         // handle failure, check result.failedConstraintIds
///     }
/// @endcode
class HOBBYCAD_EXPORT Solver {
public:
    Solver();
    ~Solver();

    /// Solve constraints and update entity geometry
    /// @param entities Entities to solve (modified in place on success)
    /// @param constraints Constraints to satisfy
    /// @return Solve result with success status and diagnostic info
    SolveResult solve(
        QVector<Entity>& entities,
        const QVector<Constraint>& constraints
    );

    /// Test if adding a constraint would over-constrain the sketch
    /// @param entities Current entities
    /// @param existingConstraints Current constraints
    /// @param newConstraint Proposed new constraint
    /// @return True if the new constraint would cause over-constraint
    bool wouldOverConstrain(
        const QVector<Entity>& entities,
        const QVector<Constraint>& existingConstraints,
        const Constraint& newConstraint
    );

    /// Check for over-constraint with detailed conflict information
    /// @param entities Current entities
    /// @param existingConstraints Current constraints
    /// @param newConstraint Proposed new constraint
    /// @return Detailed information about potential conflicts
    OverConstraintInfo checkOverConstrain(
        const QVector<Entity>& entities,
        const QVector<Constraint>& existingConstraints,
        const Constraint& newConstraint
    );

    /// Calculate degrees of freedom for a sketch
    /// @param entities Current entities
    /// @param constraints Current constraints
    /// @return Number of remaining degrees of freedom (0 = fully constrained)
    int degreesOfFreedom(
        const QVector<Entity>& entities,
        const QVector<Constraint>& constraints
    );

    /// Check if solver is available (libslvs compiled in)
    /// @return True if constraint solving is supported
    static bool isAvailable();

private:
    class Impl;
    Impl* m_impl;
};

// =====================================================================
//  Utility Functions
// =====================================================================

/// Get human-readable name for a solve result code
HOBBYCAD_EXPORT QString solveResultName(SolveResult::ResultCode code);

/// Check if a constraint type is supported by the solver
HOBBYCAD_EXPORT bool constraintSupported(ConstraintType type);

/// Get list of all solver-supported constraint types
HOBBYCAD_EXPORT QVector<ConstraintType> supportedConstraintTypes();

}  // namespace sketch
}  // namespace hobbycad

#endif  // HOBBYCAD_SKETCH_SOLVER_H
