// =====================================================================
//  src/libhobbycad/sketch/edit_session.cpp — editing a sketch with its own
//  undo history
// =====================================================================
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include <hobbycad/sketch/edit_session.h>

#include <hobbycad/sketch/solver.h>

#include <cmath>

namespace hobbycad {
namespace sketch {

// ---- Equality ---------------------------------------------------------

namespace {

bool sameNumber(double a, double b)
{
    return a == b || (std::isnan(a) && std::isnan(b));
}

}  // namespace

bool sameEntity(const Entity& a, const Entity& b)
{
    return a.id == b.id && a.type == b.type && a.points == b.points
        && a.radius == b.radius && a.startAngle == b.startAngle
        && a.sweepAngle == b.sweepAngle && a.sides == b.sides
        && a.majorRadius == b.majorRadius && a.minorRadius == b.minorRadius
        && a.ellipseRotation == b.ellipseRotation && a.ellipseStart == b.ellipseStart
        && a.ellipseSweep == b.ellipseSweep && a.text == b.text
        && a.fontFamily == b.fontFamily && a.fontSize == b.fontSize
        && a.fontBold == b.fontBold && a.fontItalic == b.fontItalic
        && a.textRotation == b.textRotation && a.arcFlipped == b.arcFlipped
        && a.splineBezier == b.splineBezier && a.splineClosed == b.splineClosed
        && a.splineRational == b.splineRational && a.weights == b.weights
        && a.conicRho == b.conicRho
        && a.pathEntityIds == b.pathEntityIds && a.offsetParentId == b.offsetParentId
        && a.offsetDistance == b.offsetDistance && a.offsetSide == b.offsetSide
        && a.projectionSourceId == b.projectionSourceId
        && a.projectionSourceSketchId == b.projectionSourceSketchId
        && a.isConstruction == b.isConstruction && a.isCenterline == b.isCenterline
        && a.color == b.color && a.constrained == b.constrained
        && a.groupId == b.groupId;
}

bool sameConstraint(const Constraint& a, const Constraint& b)
{
    return a.id == b.id && a.type == b.type && a.entityIds == b.entityIds
        && a.pointIndices == b.pointIndices && sameNumber(a.value, b.value)
        && a.expression == b.expression && a.isDriving == b.isDriving
        && a.enabled == b.enabled && a.labelPosition == b.labelPosition
        && a.labelVisible == b.labelVisible && sameNumber(a.labelAngle, b.labelAngle)
        && a.supplementary == b.supplementary;
}

bool sameGroup(const Group& a, const Group& b)
{
    return a.id == b.id && a.name == b.name && a.kind == b.kind
        && a.entityIds == b.entityIds && a.constraintIds == b.constraintIds
        && a.childGroupIds == b.childGroupIds && a.parentGroupId == b.parentGroupId
        && a.locked == b.locked && a.hasPivot == b.hasPivot && a.pivot == b.pivot;
}

// ---- Listener ---------------------------------------------------------

EditListener::~EditListener() = default;

void EditListener::entityRemoved(int /*entityId*/)
{
}

void EditListener::constraintRemoved(int /*constraintId*/)
{
}

EditCallbacks::EditCallbacks(std::function<void(int)> onEntityRemoved,
                             std::function<void(int)> onConstraintRemoved)
    : m_entity(std::move(onEntityRemoved))
    , m_constraint(std::move(onConstraintRemoved))
{
}

void EditCallbacks::entityRemoved(int entityId)
{
    if (m_entity) m_entity(entityId);
}

void EditCallbacks::constraintRemoved(int constraintId)
{
    if (m_constraint) m_constraint(constraintId);
}

// ---- Checking a new constraint ----------------------------------------

ConstraintCheck checkNewConstraint(const std::vector<Entity>& entities,
                                   const std::vector<Constraint>& constraints,
                                   const Constraint& constraint,
                                   const ConstraintCheckOptions& options)
{
    ConstraintCheck check;
    const auto refuse = [&check](ConstraintProblem problem, int entityId = -1) {
        check.problem = problem;
        check.entityId = entityId;
        return check;
    };

    // Every operand exists, and none is named twice. The same entity twice
    // is a relation only between different points of it ("coincident 1.0
    // 1.1" closes a line onto itself).
    std::vector<EntityType> kinds;
    const auto pointOf = [&constraint](std::size_t k) {
        return k < constraint.pointIndices.size() ? constraint.pointIndices[k] : 0;
    };
    for (std::size_t k = 0; k < constraint.entityIds.size(); ++k) {
        const int id = constraint.entityIds[k];
        const Entity* e = findEntityById(entities, id);
        if (!e) return refuse(ConstraintProblem::UnknownEntity, id);
        for (std::size_t j = 0; j < k; ++j) {
            if (constraint.entityIds[j] == id && pointOf(j) == pointOf(k)) {
                return refuse(ConstraintProblem::RepeatedOperand, id);
            }
        }
        kinds.push_back(e->type);
    }

    // The right kind of operands: libslvs aborts on some wrong pairings.
    if (options.operands) {
        const std::string why = constraintOperandError(constraint.type, kinds);
        if (!why.empty()) {
            check.reason = why;
            return refuse(ConstraintProblem::WrongOperands);
        }
    }

    // A radius or distance of zero collapses geometry.
    if (options.value && !isValidConstraintValue(constraint.type, constraint.value)) {
        return refuse(ConstraintProblem::BadValue);
    }

    // Redundancy is caught when the constraint is added: once a redundant
    // set is in place, the solver cannot say which member is the extra one.
    if (options.solver && constraint.isDriving && Solver::isAvailable()) {
        Solver solver;
        const OverConstraintInfo info =
            solver.checkOverConstrain(entities, constraints, constraint);
        if (info.wouldOverConstrain) {
            check.reason = info.reason;
            check.conflictingIds = info.conflictingConstraintIds;
            return refuse(info.isRedundant ? ConstraintProblem::Redundant
                                           : ConstraintProblem::OverConstrains);
        }
    }
    return check;
}

// ---- The session ------------------------------------------------------

EditSession::EditSession(int depth)
    : m_history(depth)
{
}

void EditSession::record(const UndoCommand& command)
{
    m_history.push(command);
}

UndoCommand EditSession::withDescription(UndoCommand cmd, const std::string& description)
{
    if (!description.empty()) cmd.description = description;
    return cmd;
}

}  // namespace sketch
}  // namespace hobbycad
