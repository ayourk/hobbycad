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

#include <functional>
#include <string>
#include <vector>
#include <utility>

namespace hobbycad {
namespace sketch {

// =====================================================================
//  Solver Result Types
// =====================================================================

/// What a sketch's constraint system is, as ONE value.
///
/// This exists because a bare `int dof` cannot carry it. That int was being
/// used for two incompatible jobs at once: a measurement ("2 degrees of
/// freedom remain") and a status ("-1, no idea"). So callers had to know
/// which meaning was in play, and -1 stood in for three different situations.
/// The number and the state are now separate values.
///
/// Note there is deliberately NO "over-constrained by N". libslvs reports
/// redundancy as a yes/no (SLVS_RESULT_REDUNDANT_OKAY) and gives no count:
/// one redundant constraint and three produce identical output. Inventing a
/// count would mean re-solving once per constraint.
enum class SketchState {
    Empty,              ///< No geometry. Vacuously constrained; dof is 0.
    UnderConstrained,   ///< Solvable, dof > 0 ways left to move. The normal
                        ///< state of a sketch being built.
    FullyConstrained,   ///< Solvable and dof == 0. The goal state.
    OverConstrained,    ///< Solvable, but redundant constraints are present.
                        ///< Still a valid sketch, NOT an error.
    Inconsistent,       ///< Constraints contradict each other; no solution.
    Failed,             ///< The solver could not produce an answer
                        ///< (did not converge, too many unknowns, internal
                        ///< fault). Distinct from Inconsistent: the sketch
                        ///< may well be satisfiable.
    Unknown             ///< No solver available, or nothing solved yet.
};

/// Human-readable name for a sketch state (untranslated; for logs and tests).
HOBBYCAD_EXPORT const char* sketchStateName(SketchState state);

/// True when `dof` carries a real measurement for this state. Only the three
/// solvable states have a meaningful degree-of-freedom count.
HOBBYCAD_EXPORT bool sketchStateHasDof(SketchState state);

/// Result of constraint solving
struct SolveResult {
    bool success = false;
    int dof = 0;                           ///< Degrees of freedom remaining
    std::string errorMessage;
    std::vector<int> failedConstraintIds;  ///< IDs of constraints that couldn't be satisfied

    /// Points the solver reports as still FREE (under-constrained): (entityId,
    /// pointIndex). Populated only when the linked libslvs supports free-param
    /// reporting (SLVS_HAS_FREE_PARAMS); empty otherwise. Solver truth, unlike
    /// the old topological heuristic. Both coincidence-substituted endpoints
    /// are included.
    std::vector<std::pair<int, int>> freePoints;
    bool freePointsValid = false;          ///< true iff this libslvs build reports free points

    /// Solver result codes (matches libslvs)
    enum ResultCode {
        Okay = 0,               ///< Solved successfully
        Inconsistent = 1,       ///< Constraints conflict with each other
        DidntConverge = 2,      ///< Solver failed to find a solution
        TooManyUnknowns = 3,    ///< System too complex
        RedundantOkay = 4,      ///< Redundant but solvable (over-constrained)
        InternalError = 5       ///< Solver hit a fault and was recovered;
                                ///< the system must be rebuilt before reuse
    };
    ResultCode resultCode = Okay;

    /// The system's state as one value. Prefer this over reading `success`
    /// and `dof` and re-deriving the same conclusion at each call site.
    SketchState state = SketchState::Unknown;

    /// True when `dof` is a real count rather than a placeholder.
    bool dofIsKnown() const { return sketchStateHasDof(state); }
};

/// Result of over-constraint check
struct OverConstraintInfo {
    bool wouldOverConstrain = false;
    /// True when the constraint is REDUNDANT (a solution still exists, the
    /// constraint just adds nothing) rather than CONTRADICTORY (no solution).
    /// The two need different wording: "conflicts with" is wrong for a
    /// duplicate that agrees with everything.
    bool isRedundant = false;
    /// IDs of existing constraints causing the conflict. Populated only for
    /// the contradictory case: libslvs does not reliably identify which
    /// constraint is the redundant one.
    std::vector<int> conflictingConstraintIds;
    std::string reason;                          ///< Human-readable explanation
};

// =====================================================================
//  3D sketch solving (first-class 3D points)
// =====================================================================
//  A 2D sketch is the special case where every point is pinned to the sketch
//  plane (SLVS_C_PT_IN_PLANE, 2 DOF); dropping that pin gives a free 3D point
//  (3 DOF). This is the substrate the 3D sketcher builds on. It is a separate
//  path from solve() above; the mature 2D path is left untouched.
//  Uses hobbycad::Point3 (the unified sketch point type from types.h).

/// One point in a 3D-sketch solve. `onPlane` pins it to the plane passed to
/// solve3D (a 2D-mode point: 2 DOF); `fixed` holds all three coordinates as a
/// reference (0 DOF, not moved by the solve).
struct Sketch3DPoint {
    int    id = 0;
    Point3 pos;
    bool   onPlane = false;
    bool   fixed = false;
};

/// A minimal 3D constraint set to begin with; widened as the 3D sketcher grows.
struct Sketch3DConstraint {
    enum Kind { Coincident, Distance } kind = Coincident;
    int    a = 0;          ///< first point id
    int    b = 0;          ///< second point id
    double value = 0.0;    ///< target distance (Distance only)
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
///     std::vector<Entity> entities = { ... };
///     std::vector<Constraint> constraints = { ... };
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
        std::vector<Entity>& entities,
        const std::vector<Constraint>& constraints
    );
    /// Points the user is dragging, for the NEXT solve() only. libslvs
    /// keeps these parameters as close as it can to the values handed in
    /// and moves everything else as little as the constraints require, so
    /// a drag lands where the cursor is instead of anywhere that satisfies
    /// the constraints. Each pair is (entityId, pointIndex). Cleared after
    /// the solve.
    void setDraggedPoints(const std::vector<std::pair<int, int>>& points);

    /// [0009] Give these points a drag STIFFNESS (resistance) for the next
    /// solve: > 1 holds them that many times harder, so a body-drag deforms the
    /// grabbed element while distant geometry stays put. A no-op unless the
    /// linked libslvs advertises SLVS_HAS_DRAG_WEIGHTS. Each pair is
    /// (entityId, pointIndex); cleared after the solve.
    void setPointWeights(const std::vector<std::pair<int, int>>& points, double stiffness);

    /// Solve a 3D sketch: 3D points constrained in space, with `onPlane` points
    /// pinned to `plane`. Solved positions are written back into `points`, and
    /// SolveResult carries dof/state and (when the lib supports it) the free
    /// points by id. Independent of solve(): the 2D path is unchanged.
    SolveResult solve3D(std::vector<Sketch3DPoint>& points,
                        const std::vector<Sketch3DConstraint>& constraints,
                        const PlaneBasis& plane);

    /// Test if adding a constraint would over-constrain the sketch
    /// @param entities Current entities
    /// @param existingConstraints Current constraints
    /// @param newConstraint Proposed new constraint
    /// @return True if the new constraint would cause over-constraint
    bool wouldOverConstrain(
        const std::vector<Entity>& entities,
        const std::vector<Constraint>& existingConstraints,
        const Constraint& newConstraint
    );

    /// Check for over-constraint with detailed conflict information
    /// @param entities Current entities
    /// @param existingConstraints Current constraints
    /// @param newConstraint Proposed new constraint
    /// @return Detailed information about potential conflicts
    OverConstraintInfo checkOverConstrain(
        const std::vector<Entity>& entities,
        const std::vector<Constraint>& existingConstraints,
        const Constraint& newConstraint
    );

    /// Calculate degrees of freedom for a sketch
    /// @param entities Current entities
    /// @param constraints Current constraints
    /// @return Number of remaining degrees of freedom (0 = fully constrained)
    /// Which constraints could be removed to clear a redundancy.
    ///
    /// Returns CANDIDATES, never "the culprit". In a mutually dependent set
    /// removing ANY member clears the redundancy, so every member is a valid
    /// answer and all of them are returned. Do not present the result as a
    /// diagnosis; present it as "removing any one of these would help".
    ///
    /// Method: re-solve the system once per constraint, each time EXCLUDING
    /// that constraint. A constraint whose removal makes the system solve
    /// cleanly was participating in the redundancy.
    ///
    /// COST IS O(n) SOLVES. Call it on demand (from a "find them" action),
    /// never on every edit. Returns empty immediately when the system is not
    /// actually over-constrained, so the common case is one solve.
    ///
    /// The solver's own `failed` list cannot be used for this: measured
    /// against libslvs, one redundant constraint makes it name four
    /// constraints and three redundant constraints make it name none.
    std::vector<int> findRedundantConstraints(
        const std::vector<Entity>& entities,
        const std::vector<Constraint>& constraints);

    int degreesOfFreedom(
        const std::vector<Entity>& entities,
        const std::vector<Constraint>& constraints
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
HOBBYCAD_EXPORT std::string solveResultName(SolveResult::ResultCode code);

/// Check if a constraint type is supported by the solver
HOBBYCAD_EXPORT bool constraintSupported(ConstraintType type);

/// Get list of all solver-supported constraint types
HOBBYCAD_EXPORT std::vector<ConstraintType> supportedConstraintTypes();

/// Install a fatal-error handler with the constraint solver library.
///
/// libslvs terminates the process when it reaches a condition it cannot
/// continue from, and the diagnostic it writes goes to the library's own
/// stderr where the application never sees it.  Where the library supports
/// it, this routes that diagnostic into the crash log and triggers an
/// emergency save first, so a solver fault costs the user a session rather
/// than their work.
///
/// Safe to call more than once.  A no-op when built against a libslvs
/// without the handler API, in which case a solver fault still aborts.
/// Call once during startup, before any solving begins.
HOBBYCAD_EXPORT void installSolverFatalHandler();

/// True when the solver library supports a fatal-error handler.
HOBBYCAD_EXPORT bool solverFatalHandlerAvailable();

/// True when the solver library can recover from an internal fault and
/// return control instead of ending the process.  When false, a solver
/// fault is still fatal: the handler can save the user's work, but the
/// application will not survive it.
HOBBYCAD_EXPORT bool solverCanRecoverFromFaults();

/// What the constraint solver reports itself as: "3.2p3" for a
/// HobbyCAD-patched libslvs, "stock (unversioned)" for an unpatched one
/// (upstream exposes no version macro at all, so that case cannot be named
/// more precisely without inventing a number).
///
/// For display and bug reports ONLY; never branch on it. Use
/// solverFatalHandlerAvailable() / solverCanRecoverFromFaults() to decide
/// what the library can do; those cannot disagree with what was compiled in.
HOBBYCAD_EXPORT const char* solverVersionString();

}  // namespace sketch
}  // namespace hobbycad

#endif  // HOBBYCAD_SKETCH_SOLVER_H
