// =====================================================================
//  src/hobbycad/gui/sketchsolver.h — Constraint solver wrapper (GUI)
// =====================================================================
//
//  Thin wrapper around hobbycad::sketch::Solver for GUI types.
//  Converts between GUI SketchEntity/SketchConstraint and library types.
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_SKETCHSOLVER_H
#define HOBBYCAD_SKETCHSOLVER_H

#include <hobbycad/sketch/solver.h>

#include <QVector>
#include <QPointF>
#include <QString>

namespace hobbycad {

// Forward declarations
struct SketchEntity;
struct SketchConstraint;

// Re-export library types for convenience
using SolveResult = sketch::SolveResult;
using OverConstraintInfo = sketch::OverConstraintInfo;

/// GUI wrapper around library Solver
///
/// Converts between GUI types (SketchEntity, SketchConstraint) and
/// library types (sketch::Entity, sketch::Constraint) for solving.
class SketchSolver {
public:
    SketchSolver();
    ~SketchSolver();

    /// Solve constraints and update entity geometry
    /// @param entities GUI entities (modified in place on success)
    /// @param constraints GUI constraints
    /// @return Solve result with success status and diagnostic info
    SolveResult solve(
        QVector<SketchEntity>& entities,
        const QVector<SketchConstraint>& constraints
    );

    /// Test if adding a constraint would over-constrain the sketch
    /// @return True if the new constraint would cause over-constraint
    bool wouldOverConstrain(
        const QVector<SketchEntity>& entities,
        const QVector<SketchConstraint>& existingConstraints,
        const SketchConstraint& newConstraint
    );

    /// Check for over-constraint with detailed conflict information
    OverConstraintInfo checkOverConstrain(
        const QVector<SketchEntity>& entities,
        const QVector<SketchConstraint>& existingConstraints,
        const SketchConstraint& newConstraint
    );

    /// Calculate degrees of freedom for a sketch
    int degreesOfFreedom(
        const QVector<SketchEntity>& entities,
        const QVector<SketchConstraint>& constraints
    );

    /// Check if solver is available (libslvs compiled in)
    static bool isAvailable();

private:
    sketch::Solver m_solver;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_SKETCHSOLVER_H
