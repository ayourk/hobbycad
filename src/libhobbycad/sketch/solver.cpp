// =====================================================================
//  src/libhobbycad/sketch/solver.cpp — Constraint solver implementation
// =====================================================================
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include <hobbycad/sketch/solver.h>

#ifdef HAVE_SLVS
#include <slvs.h>
#endif

#include <QDebug>

namespace hobbycad {
namespace sketch {

// =====================================================================
//  Solver Implementation Class
// =====================================================================

class Solver::Impl {
public:
#ifdef HAVE_SLVS
    // Map HobbyCAD entity IDs to solver handles
    QMap<int, Slvs_hEntity> entityHandles;
    QMap<int, Slvs_hParam> paramHandles;
    QMap<int, Slvs_hConstraint> constraintHandles;

    Slvs_hGroup groupId = 1;
    Slvs_hEntity workplaneHandle = 0;
    int nextParamHandle = 1;
    int nextEntityHandle = 1;
    int nextConstraintHandle = 1;

    void reset() {
        entityHandles.clear();
        paramHandles.clear();
        constraintHandles.clear();
        nextParamHandle = 1;
        nextEntityHandle = 1;
        nextConstraintHandle = 1;
    }

    Slvs_hParam addParam(QVector<Slvs_Param>& params, double value) {
        Slvs_hParam h = nextParamHandle++;
        params.append(Slvs_MakeParam(h, groupId, value));
        return h;
    }

    Slvs_hEntity addPoint2d(QVector<Slvs_Param>& params, QVector<Slvs_Entity>& entities,
                            const QPointF& pt, int entityId) {
        Slvs_hEntity h = nextEntityHandle++;
        entityHandles[entityId] = h;

        Slvs_hParam u = addParam(params, pt.x());
        Slvs_hParam v = addParam(params, pt.y());

        paramHandles[entityId * 10 + 0] = u;
        paramHandles[entityId * 10 + 1] = v;

        entities.append(Slvs_MakePoint2d(h, groupId, workplaneHandle, u, v));
        return h;
    }

    Slvs_hEntity addLineSegment(QVector<Slvs_Entity>& entities,
                                Slvs_hEntity p1, Slvs_hEntity p2, int entityId) {
        Slvs_hEntity h = nextEntityHandle++;
        entityHandles[entityId] = h;
        entities.append(Slvs_MakeLineSegment(h, groupId, workplaneHandle, p1, p2));
        return h;
    }

    Slvs_hEntity addCircle(QVector<Slvs_Param>& params, QVector<Slvs_Entity>& entities,
                           const QPointF& center, double radius, int entityId,
                           Slvs_hEntity normalHandle) {
        Slvs_hEntity h = nextEntityHandle++;
        entityHandles[entityId] = h;

        // Create center point
        Slvs_hEntity centerHandle = addPoint2d(params, entities, center, entityId * 1000);

        // Create radius parameter
        Slvs_hParam radiusParam = addParam(params, radius);

        entities.append(Slvs_MakeCircle(h, groupId, workplaneHandle, centerHandle, normalHandle, radiusParam));
        return h;
    }

    Slvs_hEntity getPointHandle(int entityId, int pointIndex) {
        // For point entities, use the entity handle directly
        if (entityHandles.contains(entityId)) {
            return entityHandles[entityId];
        }

        // For line entities, use endpoint handles
        int pointId = entityId * 1000 + pointIndex;
        if (entityHandles.contains(pointId)) {
            return entityHandles[pointId];
        }

        return 0;
    }

    void buildSolverSystem(
        Slvs_System& sys,
        QVector<Slvs_Param>& params,
        QVector<Slvs_Entity>& slvsEntities,
        QVector<Slvs_Constraint>& slvsConstraints,
        const QVector<Entity>& entities,
        const QVector<Constraint>& constraints);

    void extractSolution(
        const Slvs_System& sys,
        QVector<Entity>& entities);

    void addConstraintToSolver(
        const Constraint& constraint,
        QVector<Slvs_Constraint>& slvsConstraints,
        const QVector<Entity>& entities);
#endif
};

// =====================================================================
//  Solver Implementation
// =====================================================================

Solver::Solver()
    : m_impl(new Impl())
{
}

Solver::~Solver()
{
    delete m_impl;
}

bool Solver::isAvailable()
{
#ifdef HAVE_SLVS
    return true;
#else
    return false;
#endif
}

SolveResult Solver::solve(
    QVector<Entity>& entities,
    const QVector<Constraint>& constraints)
{
#ifndef HAVE_SLVS
    SolveResult result;
    result.success = false;
    result.errorMessage = QStringLiteral("Solver not available (libslvs not compiled)");
    return result;
#else
    m_impl->reset();

    // Prepare solver data structures
    QVector<Slvs_Param> params;
    QVector<Slvs_Entity> slvsEntities;
    QVector<Slvs_Constraint> slvsConstraints;

    // Build solver system
    Slvs_System sys = {};
    m_impl->buildSolverSystem(sys, params, slvsEntities, slvsConstraints, entities, constraints);

    // Set up Slvs_System pointers
    sys.param = params.data();
    sys.params = params.size();
    sys.entity = slvsEntities.data();
    sys.entities = slvsEntities.size();
    sys.constraint = slvsConstraints.data();
    sys.constraints = slvsConstraints.size();

    // Allocate space for failed constraints
    QVector<Slvs_hConstraint> failed(qMax(1, slvsConstraints.size()));
    sys.failed = failed.data();
    sys.faileds = failed.size();
    sys.calculateFaileds = 1;

    // Solve
    Slvs_Solve(&sys, m_impl->groupId);

    SolveResult result;
    result.dof = sys.dof;
    result.resultCode = static_cast<SolveResult::ResultCode>(sys.result);

    if (sys.result == SLVS_RESULT_OKAY || sys.result == SLVS_RESULT_REDUNDANT_OKAY) {
        m_impl->extractSolution(sys, entities);
        result.success = true;
    } else {
        result.success = false;

        // Map failed constraint handles back to HobbyCAD IDs
        for (int i = 0; i < sys.faileds; ++i) {
            for (auto it = m_impl->constraintHandles.begin(); it != m_impl->constraintHandles.end(); ++it) {
                if (it.value() == sys.failed[i]) {
                    result.failedConstraintIds.append(it.key());
                }
            }
        }

        result.errorMessage = solveResultName(result.resultCode);
    }

    return result;
#endif
}

bool Solver::wouldOverConstrain(
    const QVector<Entity>& entities,
    const QVector<Constraint>& existingConstraints,
    const Constraint& newConstraint)
{
    OverConstraintInfo info = checkOverConstrain(entities, existingConstraints, newConstraint);
    return info.wouldOverConstrain;
}

OverConstraintInfo Solver::checkOverConstrain(
    const QVector<Entity>& entities,
    const QVector<Constraint>& existingConstraints,
    const Constraint& newConstraint)
{
    OverConstraintInfo info;

#ifndef HAVE_SLVS
    info.wouldOverConstrain = false;
    return info;
#else
    // Create a temporary list with the new constraint added
    QVector<Constraint> testConstraints = existingConstraints;
    testConstraints.append(newConstraint);

    // Make a copy of entities (solve modifies them)
    QVector<Entity> testEntities = entities;

    m_impl->reset();

    // Prepare solver data structures
    QVector<Slvs_Param> params;
    QVector<Slvs_Entity> slvsEntities;
    QVector<Slvs_Constraint> slvsConstraints;

    // Build solver system
    Slvs_System sys = {};
    m_impl->buildSolverSystem(sys, params, slvsEntities, slvsConstraints, testEntities, testConstraints);

    // Set up Slvs_System pointers
    sys.param = params.data();
    sys.params = params.size();
    sys.entity = slvsEntities.data();
    sys.entities = slvsEntities.size();
    sys.constraint = slvsConstraints.data();
    sys.constraints = slvsConstraints.size();

    // Allocate space for failed constraints
    QVector<Slvs_hConstraint> failed(qMax(1, slvsConstraints.size()));
    sys.failed = failed.data();
    sys.faileds = failed.size();
    sys.calculateFaileds = 1;

    // Solve
    Slvs_Solve(&sys, m_impl->groupId);

    // Check if it would fail
    info.wouldOverConstrain = (sys.result == SLVS_RESULT_INCONSISTENT ||
                               sys.result == SLVS_RESULT_DIDNT_CONVERGE ||
                               sys.result == SLVS_RESULT_TOO_MANY_UNKNOWNS);

    if (info.wouldOverConstrain) {
        // Map failed constraint handles back to IDs (excluding the new one)
        for (int i = 0; i < sys.faileds; ++i) {
            Slvs_hConstraint failedHandle = sys.failed[i];
            for (auto it = m_impl->constraintHandles.begin(); it != m_impl->constraintHandles.end(); ++it) {
                if (it.value() == failedHandle && it.key() != newConstraint.id) {
                    info.conflictingConstraintIds.append(it.key());
                }
            }
        }

        info.reason = solveResultName(static_cast<SolveResult::ResultCode>(sys.result));
    }

    return info;
#endif
}

int Solver::degreesOfFreedom(
    const QVector<Entity>& entities,
    const QVector<Constraint>& constraints)
{
#ifndef HAVE_SLVS
    return -1;  // Unknown
#else
    // Make a copy of entities (solve may modify them)
    QVector<Entity> testEntities = entities;

    m_impl->reset();

    QVector<Slvs_Param> params;
    QVector<Slvs_Entity> slvsEntities;
    QVector<Slvs_Constraint> slvsConstraints;

    Slvs_System sys = {};
    m_impl->buildSolverSystem(sys, params, slvsEntities, slvsConstraints, testEntities, constraints);

    sys.param = params.data();
    sys.params = params.size();
    sys.entity = slvsEntities.data();
    sys.entities = slvsEntities.size();
    sys.constraint = slvsConstraints.data();
    sys.constraints = slvsConstraints.size();

    QVector<Slvs_hConstraint> failed(qMax(1, slvsConstraints.size()));
    sys.failed = failed.data();
    sys.faileds = failed.size();

    Slvs_Solve(&sys, m_impl->groupId);

    return sys.dof;
#endif
}

// =====================================================================
//  libslvs System Building (only when HAVE_SLVS is defined)
// =====================================================================

#ifdef HAVE_SLVS

void Solver::Impl::buildSolverSystem(
    Slvs_System& sys,
    QVector<Slvs_Param>& params,
    QVector<Slvs_Entity>& slvsEntities,
    QVector<Slvs_Constraint>& slvsConstraints,
    const QVector<Entity>& entities,
    const QVector<Constraint>& constraints)
{
    // Create 2D workplane (fixed XY plane)
    Slvs_hEntity normalHandle = nextEntityHandle++;
    Slvs_hEntity originHandle = nextEntityHandle++;
    workplaneHandle = nextEntityHandle++;

    // Origin point (0, 0, 0) - fixed
    Slvs_hParam originX = addParam(params, 0.0);
    Slvs_hParam originY = addParam(params, 0.0);
    Slvs_hParam originZ = addParam(params, 0.0);

    slvsEntities.append(Slvs_MakePoint3d(originHandle, groupId, originX, originY, originZ));

    // Normal vector (0, 0, 1) - Z axis, fixed
    Slvs_hParam normalW = addParam(params, 1.0);
    Slvs_hParam normalXP = addParam(params, 0.0);
    Slvs_hParam normalYP = addParam(params, 0.0);
    Slvs_hParam normalZP = addParam(params, 1.0);

    slvsEntities.append(Slvs_MakeNormal3d(normalHandle, groupId, normalW, normalXP, normalYP, normalZP));

    // Workplane
    slvsEntities.append(Slvs_MakeWorkplane(workplaneHandle, groupId, originHandle, normalHandle));

    // Fix the origin and normal (they define the workplane)
    params[originX - 1].group = 0;  // Group 0 = fixed
    params[originY - 1].group = 0;
    params[originZ - 1].group = 0;
    params[normalW - 1].group = 0;
    params[normalXP - 1].group = 0;
    params[normalYP - 1].group = 0;
    params[normalZP - 1].group = 0;

    // Create solver entities from library entities
    for (const Entity& entity : entities) {
        switch (entity.type) {
        case EntityType::Point:
            if (!entity.points.isEmpty()) {
                addPoint2d(params, slvsEntities, entity.points[0], entity.id);
            }
            break;

        case EntityType::Line:
            if (entity.points.size() >= 2) {
                Slvs_hEntity p1 = addPoint2d(params, slvsEntities, entity.points[0], entity.id * 1000 + 0);
                Slvs_hEntity p2 = addPoint2d(params, slvsEntities, entity.points[1], entity.id * 1000 + 1);
                addLineSegment(slvsEntities, p1, p2, entity.id);
            }
            break;

        case EntityType::Circle:
        case EntityType::Arc:
            if (!entity.points.isEmpty()) {
                addCircle(params, slvsEntities, entity.points[0], entity.radius, entity.id, normalHandle);
            }
            break;

        // Other entity types not yet supported in solver
        default:
            break;
        }
    }

    // Add constraints
    for (const Constraint& constraint : constraints) {
        if (!constraint.enabled) continue;
        addConstraintToSolver(constraint, slvsConstraints, entities);
    }
}

void Solver::Impl::extractSolution(
    const Slvs_System& sys,
    QVector<Entity>& entities)
{
    for (Entity& entity : entities) {
        switch (entity.type) {
        case EntityType::Point:
            if (!entity.points.isEmpty() && entityHandles.contains(entity.id)) {
                Slvs_hEntity pointHandle = entityHandles[entity.id];
                for (int i = 0; i < sys.entities; ++i) {
                    if (sys.entity[i].h == pointHandle) {
                        Slvs_hParam uParam = sys.entity[i].param[0];
                        Slvs_hParam vParam = sys.entity[i].param[1];
                        for (int j = 0; j < sys.params; ++j) {
                            if (sys.param[j].h == uParam) {
                                entity.points[0].setX(sys.param[j].val);
                            }
                            if (sys.param[j].h == vParam) {
                                entity.points[0].setY(sys.param[j].val);
                            }
                        }
                        break;
                    }
                }
            }
            break;

        case EntityType::Line:
            if (entity.points.size() >= 2) {
                for (int idx = 0; idx < 2; ++idx) {
                    int pointId = entity.id * 1000 + idx;
                    if (entityHandles.contains(pointId)) {
                        Slvs_hEntity pointHandle = entityHandles[pointId];
                        for (int i = 0; i < sys.entities; ++i) {
                            if (sys.entity[i].h == pointHandle) {
                                Slvs_hParam uParam = sys.entity[i].param[0];
                                Slvs_hParam vParam = sys.entity[i].param[1];
                                for (int j = 0; j < sys.params; ++j) {
                                    if (sys.param[j].h == uParam) {
                                        entity.points[idx].setX(sys.param[j].val);
                                    }
                                    if (sys.param[j].h == vParam) {
                                        entity.points[idx].setY(sys.param[j].val);
                                    }
                                }
                                break;
                            }
                        }
                    }
                }
            }
            break;

        case EntityType::Circle:
        case EntityType::Arc:
            if (!entity.points.isEmpty() && entityHandles.contains(entity.id)) {
                Slvs_hEntity circleHandle = entityHandles[entity.id];
                for (int i = 0; i < sys.entities; ++i) {
                    if (sys.entity[i].h == circleHandle) {
                        Slvs_hEntity centerHandle = sys.entity[i].point[0];
                        Slvs_hEntity radiusHandle = sys.entity[i].distance;

                        // Extract center point
                        for (int j = 0; j < sys.entities; ++j) {
                            if (sys.entity[j].h == centerHandle) {
                                Slvs_hParam uParam = sys.entity[j].param[0];
                                Slvs_hParam vParam = sys.entity[j].param[1];
                                for (int k = 0; k < sys.params; ++k) {
                                    if (sys.param[k].h == uParam) {
                                        entity.points[0].setX(sys.param[k].val);
                                    }
                                    if (sys.param[k].h == vParam) {
                                        entity.points[0].setY(sys.param[k].val);
                                    }
                                }
                                break;
                            }
                        }

                        // Extract radius
                        for (int k = 0; k < sys.params; ++k) {
                            if (sys.param[k].h == radiusHandle) {
                                entity.radius = sys.param[k].val;
                                break;
                            }
                        }
                        break;
                    }
                }
            }
            break;

        default:
            break;
        }
    }
}

void Solver::Impl::addConstraintToSolver(
    const Constraint& constraint,
    QVector<Slvs_Constraint>& slvsConstraints,
    const QVector<Entity>& entities)
{
    Slvs_hConstraint ch = nextConstraintHandle++;
    constraintHandles[constraint.id] = ch;

    switch (constraint.type) {
    case ConstraintType::Distance:
        if (constraint.entityIds.size() >= 2) {
            Slvs_hEntity pt1 = getPointHandle(constraint.entityIds[0], constraint.pointIndices.value(0, 0));
            Slvs_hEntity pt2 = getPointHandle(constraint.entityIds[1], constraint.pointIndices.value(1, 0));

            if (pt1 && pt2) {
                slvsConstraints.append(
                    Slvs_MakeConstraint(
                        ch, groupId,
                        SLVS_C_PT_PT_DISTANCE,
                        workplaneHandle,
                        constraint.value,
                        pt1, pt2,
                        0, 0
                    )
                );
            }
        }
        break;

    case ConstraintType::Radius:
    case ConstraintType::Diameter:
        if (!constraint.entityIds.isEmpty() && entityHandles.contains(constraint.entityIds[0])) {
            Slvs_hEntity circleEntity = entityHandles[constraint.entityIds[0]];
            double diameterValue = (constraint.type == ConstraintType::Diameter)
                                   ? constraint.value
                                   : constraint.value * 2.0;

            slvsConstraints.append(
                Slvs_MakeConstraint(
                    ch, groupId,
                    SLVS_C_DIAMETER,
                    workplaneHandle,
                    diameterValue,
                    0, 0,
                    circleEntity, 0
                )
            );
        }
        break;

    case ConstraintType::Angle:
        if (constraint.entityIds.size() >= 2 &&
            entityHandles.contains(constraint.entityIds[0]) &&
            entityHandles.contains(constraint.entityIds[1])) {
            Slvs_hEntity line1 = entityHandles[constraint.entityIds[0]];
            Slvs_hEntity line2 = entityHandles[constraint.entityIds[1]];

            slvsConstraints.append(
                Slvs_MakeConstraint(
                    ch, groupId,
                    SLVS_C_ANGLE,
                    workplaneHandle,
                    constraint.value,
                    0, 0,
                    line1, line2
                )
            );
        }
        break;

    case ConstraintType::Horizontal:
        if (!constraint.entityIds.isEmpty() && entityHandles.contains(constraint.entityIds[0])) {
            Slvs_hEntity line = entityHandles[constraint.entityIds[0]];
            slvsConstraints.append(
                Slvs_MakeConstraint(
                    ch, groupId,
                    SLVS_C_HORIZONTAL,
                    workplaneHandle,
                    0.0,
                    0, 0,
                    line, 0
                )
            );
        }
        break;

    case ConstraintType::Vertical:
        if (!constraint.entityIds.isEmpty() && entityHandles.contains(constraint.entityIds[0])) {
            Slvs_hEntity line = entityHandles[constraint.entityIds[0]];
            slvsConstraints.append(
                Slvs_MakeConstraint(
                    ch, groupId,
                    SLVS_C_VERTICAL,
                    workplaneHandle,
                    0.0,
                    0, 0,
                    line, 0
                )
            );
        }
        break;

    case ConstraintType::Parallel:
        if (constraint.entityIds.size() >= 2 &&
            entityHandles.contains(constraint.entityIds[0]) &&
            entityHandles.contains(constraint.entityIds[1])) {
            Slvs_hEntity line1 = entityHandles[constraint.entityIds[0]];
            Slvs_hEntity line2 = entityHandles[constraint.entityIds[1]];
            slvsConstraints.append(
                Slvs_MakeConstraint(
                    ch, groupId,
                    SLVS_C_PARALLEL,
                    workplaneHandle,
                    0.0,
                    0, 0,
                    line1, line2
                )
            );
        }
        break;

    case ConstraintType::Perpendicular:
        if (constraint.entityIds.size() >= 2 &&
            entityHandles.contains(constraint.entityIds[0]) &&
            entityHandles.contains(constraint.entityIds[1])) {
            Slvs_hEntity line1 = entityHandles[constraint.entityIds[0]];
            Slvs_hEntity line2 = entityHandles[constraint.entityIds[1]];
            slvsConstraints.append(
                Slvs_MakeConstraint(
                    ch, groupId,
                    SLVS_C_PERPENDICULAR,
                    workplaneHandle,
                    0.0,
                    0, 0,
                    line1, line2
                )
            );
        }
        break;

    case ConstraintType::Coincident:
        if (constraint.entityIds.size() >= 2) {
            Slvs_hEntity pt1 = getPointHandle(constraint.entityIds[0], constraint.pointIndices.value(0, 0));
            Slvs_hEntity pt2 = getPointHandle(constraint.entityIds[1], constraint.pointIndices.value(1, 0));
            if (pt1 && pt2) {
                slvsConstraints.append(
                    Slvs_MakeConstraint(
                        ch, groupId,
                        SLVS_C_POINTS_COINCIDENT,
                        workplaneHandle,
                        0.0,
                        pt1, pt2,
                        0, 0
                    )
                );
            }
        }
        break;

    case ConstraintType::Equal:
        if (constraint.entityIds.size() >= 2 &&
            entityHandles.contains(constraint.entityIds[0]) &&
            entityHandles.contains(constraint.entityIds[1])) {
            Slvs_hEntity e1 = entityHandles[constraint.entityIds[0]];
            Slvs_hEntity e2 = entityHandles[constraint.entityIds[1]];
            slvsConstraints.append(
                Slvs_MakeConstraint(
                    ch, groupId,
                    SLVS_C_EQUAL_LENGTH_LINES,
                    workplaneHandle,
                    0.0,
                    0, 0,
                    e1, e2
                )
            );
        }
        break;

    case ConstraintType::Tangent:
        if (constraint.entityIds.size() >= 2 &&
            entityHandles.contains(constraint.entityIds[0]) &&
            entityHandles.contains(constraint.entityIds[1])) {
            Slvs_hEntity e1 = entityHandles[constraint.entityIds[0]];
            Slvs_hEntity e2 = entityHandles[constraint.entityIds[1]];
            slvsConstraints.append(
                Slvs_MakeConstraint(
                    ch, groupId,
                    SLVS_C_CURVE_CURVE_TANGENT,
                    workplaneHandle,
                    0.0,
                    0, 0,
                    e1, e2
                )
            );
        }
        break;

    case ConstraintType::Midpoint:
        if (constraint.entityIds.size() >= 2) {
            Slvs_hEntity pt = getPointHandle(constraint.entityIds[0], 0);
            Slvs_hEntity line = 0;
            if (entityHandles.contains(constraint.entityIds[1])) {
                line = entityHandles[constraint.entityIds[1]];
            }
            if (pt && line) {
                slvsConstraints.append(
                    Slvs_MakeConstraint(
                        ch, groupId,
                        SLVS_C_AT_MIDPOINT,
                        workplaneHandle,
                        0.0,
                        pt, 0,
                        line, 0
                    )
                );
            }
        }
        break;

    case ConstraintType::Symmetric:
        if (constraint.entityIds.size() >= 3) {
            Slvs_hEntity pt1 = getPointHandle(constraint.entityIds[0], constraint.pointIndices.value(0, 0));
            Slvs_hEntity pt2 = getPointHandle(constraint.entityIds[1], constraint.pointIndices.value(1, 0));
            Slvs_hEntity line = 0;
            if (entityHandles.contains(constraint.entityIds[2])) {
                line = entityHandles[constraint.entityIds[2]];
            }
            if (pt1 && pt2 && line) {
                slvsConstraints.append(
                    Slvs_MakeConstraint(
                        ch, groupId,
                        SLVS_C_SYMMETRIC_LINE,
                        workplaneHandle,
                        0.0,
                        pt1, pt2,
                        line, 0
                    )
                );
            }
        }
        break;

    default:
        // Unsupported constraint type
        break;
    }
}

#endif  // HAVE_SLVS

// =====================================================================
//  Utility Functions
// =====================================================================

QString solveResultName(SolveResult::ResultCode code)
{
    switch (code) {
    case SolveResult::Okay:
        return QStringLiteral("Solved successfully");
    case SolveResult::Inconsistent:
        return QStringLiteral("Constraints are inconsistent (conflicting)");
    case SolveResult::DidntConverge:
        return QStringLiteral("Solver didn't converge (too complex or ill-conditioned)");
    case SolveResult::TooManyUnknowns:
        return QStringLiteral("Too many unknowns for solver");
    case SolveResult::RedundantOkay:
        return QStringLiteral("Solved with redundant constraints");
    default:
        return QStringLiteral("Unknown solver result");
    }
}

bool constraintSupported(ConstraintType type)
{
    switch (type) {
    case ConstraintType::Distance:
    case ConstraintType::Radius:
    case ConstraintType::Diameter:
    case ConstraintType::Angle:
    case ConstraintType::Horizontal:
    case ConstraintType::Vertical:
    case ConstraintType::Parallel:
    case ConstraintType::Perpendicular:
    case ConstraintType::Coincident:
    case ConstraintType::Equal:
    case ConstraintType::Tangent:
    case ConstraintType::Midpoint:
    case ConstraintType::Symmetric:
        return true;
    default:
        return false;
    }
}

QVector<ConstraintType> supportedConstraintTypes()
{
    return {
        ConstraintType::Distance,
        ConstraintType::Radius,
        ConstraintType::Diameter,
        ConstraintType::Angle,
        ConstraintType::Horizontal,
        ConstraintType::Vertical,
        ConstraintType::Parallel,
        ConstraintType::Perpendicular,
        ConstraintType::Coincident,
        ConstraintType::Equal,
        ConstraintType::Tangent,
        ConstraintType::Midpoint,
        ConstraintType::Symmetric
    };
}

}  // namespace sketch
}  // namespace hobbycad
