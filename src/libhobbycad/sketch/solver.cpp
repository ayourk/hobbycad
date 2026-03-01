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

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace hobbycad {
namespace sketch {

// =====================================================================
//  Solver Implementation Class
// =====================================================================

class Solver::Impl {
public:
#ifdef HAVE_SLVS
    // Map HobbyCAD entity IDs to solver handles
    std::map<int, Slvs_hEntity> entityHandles;
    std::map<int, Slvs_hParam> paramHandles;
    std::map<int, Slvs_hConstraint> constraintHandles;

    static constexpr Slvs_hGroup workplaneGroupId = 1;  // Group for workplane definition
    static constexpr Slvs_hGroup sketchGroupId = 2;    // Group for sketch entities/constraints
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

    Slvs_hParam addParam(std::vector<Slvs_Param>& params, double value,
                         Slvs_hGroup group = 0) {
        Slvs_hParam h = nextParamHandle++;
        params.push_back(Slvs_MakeParam(h, group ? group : sketchGroupId, value));
        return h;
    }

    Slvs_hEntity addPoint2d(std::vector<Slvs_Param>& params, std::vector<Slvs_Entity>& entities,
                            const Point2D& pt, int entityId) {
        Slvs_hEntity h = nextEntityHandle++;
        entityHandles[entityId] = h;

        Slvs_hParam u = addParam(params, pt.x, sketchGroupId);
        Slvs_hParam v = addParam(params, pt.y, sketchGroupId);

        paramHandles[entityId * 10 + 0] = u;
        paramHandles[entityId * 10 + 1] = v;

        entities.push_back(Slvs_MakePoint2d(h, sketchGroupId, workplaneHandle, u, v));
        return h;
    }

    Slvs_hEntity addLineSegment(std::vector<Slvs_Entity>& entities,
                                Slvs_hEntity p1, Slvs_hEntity p2, int entityId) {
        Slvs_hEntity h = nextEntityHandle++;
        entityHandles[entityId] = h;
        entities.push_back(Slvs_MakeLineSegment(h, sketchGroupId, workplaneHandle, p1, p2));
        return h;
    }

    Slvs_hEntity addCircle(std::vector<Slvs_Param>& params, std::vector<Slvs_Entity>& entities,
                           const Point2D& center, double radius, int entityId,
                           Slvs_hEntity normalHandle) {
        Slvs_hEntity h = nextEntityHandle++;
        entityHandles[entityId] = h;

        // Create center point
        Slvs_hEntity centerHandle = addPoint2d(params, entities, center, entityId * 1000);

        // Create radius as a distance entity (SolveSpace requires SLVS_E_DISTANCE,
        // not a raw parameter, for the circle's distance sub-entity)
        Slvs_hParam radiusParam = addParam(params, radius, sketchGroupId);
        Slvs_hEntity distHandle = nextEntityHandle++;
        entities.push_back(Slvs_MakeDistance(distHandle, sketchGroupId, workplaneHandle, radiusParam));

        entities.push_back(Slvs_MakeCircle(h, sketchGroupId, workplaneHandle, centerHandle, normalHandle, distHandle));
        return h;
    }

    Slvs_hEntity getPointHandle(int entityId, int pointIndex) {
        // First try compound key (line/arc/circle endpoint or center).
        // Lines register endpoints as entityId*1000+0 and entityId*1000+1.
        // Circles/arcs register centers as entityId*1000.
        int pointId = entityId * 1000 + pointIndex;
        if (entityHandles.count(pointId) > 0) {
            return entityHandles[pointId];
        }

        // Fall back to direct handle (point entities are registered directly).
        if (entityHandles.count(entityId) > 0) {
            return entityHandles[entityId];
        }

        return 0;
    }

    void buildSolverSystem(
        Slvs_System& sys,
        std::vector<Slvs_Param>& params,
        std::vector<Slvs_Entity>& slvsEntities,
        std::vector<Slvs_Constraint>& slvsConstraints,
        const std::vector<Entity>& entities,
        const std::vector<Constraint>& constraints);

    void extractSolution(
        const Slvs_System& sys,
        std::vector<Entity>& entities);

    void addConstraintToSolver(
        const Constraint& constraint,
        std::vector<Slvs_Param>& params,
        std::vector<Slvs_Entity>& slvsEntities,
        std::vector<Slvs_Constraint>& slvsConstraints,
        const std::vector<Entity>& entities);
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
    std::vector<Entity>& entities,
    const std::vector<Constraint>& constraints)
{
#ifndef HAVE_SLVS
    SolveResult result;
    result.success = false;
    result.errorMessage = "Solver not available (libslvs not compiled)";
    return result;
#else
    m_impl->reset();

    // Prepare solver data structures
    std::vector<Slvs_Param> params;
    std::vector<Slvs_Entity> slvsEntities;
    std::vector<Slvs_Constraint> slvsConstraints;

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
    std::vector<Slvs_hConstraint> failed(std::max(1, static_cast<int>(slvsConstraints.size())));
    sys.failed = failed.data();
    sys.faileds = failed.size();
    sys.calculateFaileds = 1;

    // Solve
    Slvs_Solve(&sys, m_impl->sketchGroupId);

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
                if (it->second == sys.failed[i]) {
                    result.failedConstraintIds.push_back(it->first);
                }
            }
        }

        result.errorMessage = solveResultName(result.resultCode);
    }

    return result;
#endif
}

bool Solver::wouldOverConstrain(
    const std::vector<Entity>& entities,
    const std::vector<Constraint>& existingConstraints,
    const Constraint& newConstraint)
{
    OverConstraintInfo info = checkOverConstrain(entities, existingConstraints, newConstraint);
    return info.wouldOverConstrain;
}

OverConstraintInfo Solver::checkOverConstrain(
    const std::vector<Entity>& entities,
    const std::vector<Constraint>& existingConstraints,
    const Constraint& newConstraint)
{
    OverConstraintInfo info;

#ifndef HAVE_SLVS
    info.wouldOverConstrain = false;
    return info;
#else
    // Create a temporary list with the new constraint added
    std::vector<Constraint> testConstraints = existingConstraints;
    testConstraints.push_back(newConstraint);

    // Make a copy of entities (solve modifies them)
    std::vector<Entity> testEntities = entities;

    m_impl->reset();

    // Prepare solver data structures
    std::vector<Slvs_Param> params;
    std::vector<Slvs_Entity> slvsEntities;
    std::vector<Slvs_Constraint> slvsConstraints;

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
    std::vector<Slvs_hConstraint> failed(std::max(1, static_cast<int>(slvsConstraints.size())));
    sys.failed = failed.data();
    sys.faileds = failed.size();
    sys.calculateFaileds = 1;

    // Solve
    Slvs_Solve(&sys, m_impl->sketchGroupId);

    // Check if it would fail
    info.wouldOverConstrain = (sys.result == SLVS_RESULT_INCONSISTENT ||
                               sys.result == SLVS_RESULT_DIDNT_CONVERGE ||
                               sys.result == SLVS_RESULT_TOO_MANY_UNKNOWNS);

    if (info.wouldOverConstrain) {
        // Map failed constraint handles back to IDs (excluding the new one)
        for (int i = 0; i < sys.faileds; ++i) {
            Slvs_hConstraint failedHandle = sys.failed[i];
            for (auto it = m_impl->constraintHandles.begin(); it != m_impl->constraintHandles.end(); ++it) {
                if (it->second == failedHandle && it->first != newConstraint.id) {
                    info.conflictingConstraintIds.push_back(it->first);
                }
            }
        }

        info.reason = solveResultName(static_cast<SolveResult::ResultCode>(sys.result));
    }

    return info;
#endif
}

int Solver::degreesOfFreedom(
    const std::vector<Entity>& entities,
    const std::vector<Constraint>& constraints)
{
#ifndef HAVE_SLVS
    return -1;  // Unknown
#else
    // Make a copy of entities (solve may modify them)
    std::vector<Entity> testEntities = entities;

    m_impl->reset();

    std::vector<Slvs_Param> params;
    std::vector<Slvs_Entity> slvsEntities;
    std::vector<Slvs_Constraint> slvsConstraints;

    Slvs_System sys = {};
    m_impl->buildSolverSystem(sys, params, slvsEntities, slvsConstraints, testEntities, constraints);

    sys.param = params.data();
    sys.params = params.size();
    sys.entity = slvsEntities.data();
    sys.entities = slvsEntities.size();
    sys.constraint = slvsConstraints.data();
    sys.constraints = slvsConstraints.size();

    std::vector<Slvs_hConstraint> failed(std::max(1, static_cast<int>(slvsConstraints.size())));
    sys.failed = failed.data();
    sys.faileds = failed.size();

    Slvs_Solve(&sys, m_impl->sketchGroupId);

    return sys.dof;
#endif
}

// =====================================================================
//  libslvs System Building (only when HAVE_SLVS is defined)
// =====================================================================

#ifdef HAVE_SLVS

void Solver::Impl::buildSolverSystem(
    Slvs_System& sys,
    std::vector<Slvs_Param>& params,
    std::vector<Slvs_Entity>& slvsEntities,
    std::vector<Slvs_Constraint>& slvsConstraints,
    const std::vector<Entity>& entities,
    const std::vector<Constraint>& constraints)
{
    // Create 2D workplane (fixed XY plane) in group 1.
    // Solvespace requires the workplane to be in a lower-numbered group
    // than the sketch entities/constraints (group 2) so it is treated
    // as already-solved infrastructure.
    Slvs_hEntity originHandle = nextEntityHandle++;
    Slvs_hEntity normalHandle = nextEntityHandle++;
    workplaneHandle = nextEntityHandle++;

    // Origin point (0, 0, 0)
    Slvs_hParam originX = addParam(params, 0.0, workplaneGroupId);
    Slvs_hParam originY = addParam(params, 0.0, workplaneGroupId);
    Slvs_hParam originZ = addParam(params, 0.0, workplaneGroupId);

    slvsEntities.push_back(Slvs_MakePoint3d(originHandle, workplaneGroupId, originX, originY, originZ));

    // Normal quaternion for XY plane (basis vectors (1,0,0) and (0,1,0))
    double qw, qx, qy, qz;
    Slvs_MakeQuaternion(1, 0, 0,   // unit X
                        0, 1, 0,   // unit Y
                        &qw, &qx, &qy, &qz);

    Slvs_hParam normalW  = addParam(params, qw, workplaneGroupId);
    Slvs_hParam normalXP = addParam(params, qx, workplaneGroupId);
    Slvs_hParam normalYP = addParam(params, qy, workplaneGroupId);
    Slvs_hParam normalZP = addParam(params, qz, workplaneGroupId);

    slvsEntities.push_back(Slvs_MakeNormal3d(normalHandle, workplaneGroupId, normalW, normalXP, normalYP, normalZP));

    // Workplane
    slvsEntities.push_back(Slvs_MakeWorkplane(workplaneHandle, workplaneGroupId, originHandle, normalHandle));

    // Create solver entities from library entities
    for (const Entity& entity : entities) {
        switch (entity.type) {
        case EntityType::Point:
            if (!entity.points.empty()) {
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
            if (!entity.points.empty()) {
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
        addConstraintToSolver(constraint, params, slvsEntities, slvsConstraints, entities);
    }
}

void Solver::Impl::extractSolution(
    const Slvs_System& sys,
    std::vector<Entity>& entities)
{
    for (Entity& entity : entities) {
        switch (entity.type) {
        case EntityType::Point:
            if (!entity.points.empty() && entityHandles.count(entity.id) > 0) {
                Slvs_hEntity pointHandle = entityHandles[entity.id];
                for (int i = 0; i < sys.entities; ++i) {
                    if (sys.entity[i].h == pointHandle) {
                        Slvs_hParam uParam = sys.entity[i].param[0];
                        Slvs_hParam vParam = sys.entity[i].param[1];
                        for (int j = 0; j < sys.params; ++j) {
                            if (sys.param[j].h == uParam) {
                                entity.points[0].x = sys.param[j].val;
                            }
                            if (sys.param[j].h == vParam) {
                                entity.points[0].y = sys.param[j].val;
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
                    if (entityHandles.count(pointId) > 0) {
                        Slvs_hEntity pointHandle = entityHandles[pointId];
                        for (int i = 0; i < sys.entities; ++i) {
                            if (sys.entity[i].h == pointHandle) {
                                Slvs_hParam uParam = sys.entity[i].param[0];
                                Slvs_hParam vParam = sys.entity[i].param[1];
                                for (int j = 0; j < sys.params; ++j) {
                                    if (sys.param[j].h == uParam) {
                                        entity.points[idx].x = sys.param[j].val;
                                    }
                                    if (sys.param[j].h == vParam) {
                                        entity.points[idx].y = sys.param[j].val;
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
            if (!entity.points.empty() && entityHandles.count(entity.id) > 0) {
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
                                        entity.points[0].x = sys.param[k].val;
                                    }
                                    if (sys.param[k].h == vParam) {
                                        entity.points[0].y = sys.param[k].val;
                                    }
                                }
                                break;
                            }
                        }

                        // Extract radius (distance entity → param)
                        for (int j = 0; j < sys.entities; ++j) {
                            if (sys.entity[j].h == radiusHandle) {
                                Slvs_hParam rParam = sys.entity[j].param[0];
                                for (int k = 0; k < sys.params; ++k) {
                                    if (sys.param[k].h == rParam) {
                                        entity.radius = sys.param[k].val;
                                        break;
                                    }
                                }
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
    std::vector<Slvs_Param>& params,
    std::vector<Slvs_Entity>& slvsEntities,
    std::vector<Slvs_Constraint>& slvsConstraints,
    const std::vector<Entity>& entities)
{
    Slvs_hConstraint ch = nextConstraintHandle++;
    constraintHandles[constraint.id] = ch;

    // Helper to safely get a value from a vector with a default
    auto safeGet = [](const std::vector<int>& vec, size_t index, int defaultVal) -> int {
        return (index < vec.size()) ? vec[index] : defaultVal;
    };

    switch (constraint.type) {
    case ConstraintType::Distance:
        if (constraint.entityIds.size() < 2) {
            fprintf(stderr, "Solver: Distance constraint %d has only %d entityIds (need 2), skipping\n",
                     constraint.id, static_cast<int>(constraint.entityIds.size()));
            break;
        }
        {
            Slvs_hEntity pt1 = getPointHandle(constraint.entityIds[0], safeGet(constraint.pointIndices, 0, 0));
            Slvs_hEntity pt2 = getPointHandle(constraint.entityIds[1], safeGet(constraint.pointIndices, 1, 0));

            if (!pt1 || !pt2) {
                fprintf(stderr, "Solver: Distance constraint %d: failed to resolve point handles "
                         "(entity %d -> handle %u, entity %d -> handle %u), skipping\n",
                         constraint.id,
                         constraint.entityIds[0], pt1,
                         constraint.entityIds[1], pt2);
                break;
            }
            slvsConstraints.push_back(
                Slvs_MakeConstraint(
                    ch, sketchGroupId,
                    SLVS_C_PT_PT_DISTANCE,
                    workplaneHandle,
                    constraint.value,
                    pt1, pt2,
                    0, 0
                )
            );
        }
        break;

    case ConstraintType::Radius:
    case ConstraintType::Diameter:
        if (constraint.entityIds.empty()) {
            fprintf(stderr, "Solver: Radius/Diameter constraint %d has no entityIds, skipping\n",
                     constraint.id);
            break;
        }
        if (entityHandles.count(constraint.entityIds[0]) == 0) {
            fprintf(stderr, "Solver: Radius/Diameter constraint %d: entity %d not in solver, skipping\n",
                     constraint.id, constraint.entityIds[0]);
            break;
        }
        {
            Slvs_hEntity circleEntity = entityHandles[constraint.entityIds[0]];
            double diameterValue = (constraint.type == ConstraintType::Diameter)
                                   ? constraint.value
                                   : constraint.value * 2.0;

            slvsConstraints.push_back(
                Slvs_MakeConstraint(
                    ch, sketchGroupId,
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
        if (constraint.entityIds.size() < 2) {
            fprintf(stderr, "Solver: Angle constraint %d has only %d entityIds (need 2), skipping\n",
                     constraint.id, static_cast<int>(constraint.entityIds.size()));
            break;
        }
        if (entityHandles.count(constraint.entityIds[0]) == 0 ||
            entityHandles.count(constraint.entityIds[1]) == 0) {
            fprintf(stderr, "Solver: Angle constraint %d: entity not in solver (e0=%d, e1=%d), skipping\n",
                     constraint.id, constraint.entityIds[0], constraint.entityIds[1]);
            break;
        }
        {
            Slvs_hEntity line1 = entityHandles[constraint.entityIds[0]];
            Slvs_hEntity line2 = entityHandles[constraint.entityIds[1]];

            slvsConstraints.push_back(
                Slvs_MakeConstraint(
                    ch, sketchGroupId,
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
        if (constraint.entityIds.empty() || entityHandles.count(constraint.entityIds[0]) == 0) {
            fprintf(stderr, "Solver: Horizontal constraint %d: entity not in solver, skipping\n",
                     constraint.id);
            break;
        }
        {
            Slvs_hEntity line = entityHandles[constraint.entityIds[0]];
            slvsConstraints.push_back(
                Slvs_MakeConstraint(
                    ch, sketchGroupId,
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
        if (constraint.entityIds.empty() || entityHandles.count(constraint.entityIds[0]) == 0) {
            fprintf(stderr, "Solver: Vertical constraint %d: entity not in solver, skipping\n",
                     constraint.id);
            break;
        }
        {
            Slvs_hEntity line = entityHandles[constraint.entityIds[0]];
            slvsConstraints.push_back(
                Slvs_MakeConstraint(
                    ch, sketchGroupId,
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
        if (constraint.entityIds.size() < 2 ||
            entityHandles.count(constraint.entityIds[0]) == 0 ||
            entityHandles.count(constraint.entityIds[1]) == 0) {
            fprintf(stderr, "Solver: Parallel constraint %d: missing entities, skipping\n",
                     constraint.id);
            break;
        }
        {
            Slvs_hEntity line1 = entityHandles[constraint.entityIds[0]];
            Slvs_hEntity line2 = entityHandles[constraint.entityIds[1]];
            slvsConstraints.push_back(
                Slvs_MakeConstraint(
                    ch, sketchGroupId,
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
        if (constraint.entityIds.size() < 2 ||
            entityHandles.count(constraint.entityIds[0]) == 0 ||
            entityHandles.count(constraint.entityIds[1]) == 0) {
            fprintf(stderr, "Solver: Perpendicular constraint %d: missing entities, skipping\n",
                     constraint.id);
            break;
        }
        {
            Slvs_hEntity line1 = entityHandles[constraint.entityIds[0]];
            Slvs_hEntity line2 = entityHandles[constraint.entityIds[1]];
            slvsConstraints.push_back(
                Slvs_MakeConstraint(
                    ch, sketchGroupId,
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
        if (constraint.entityIds.size() < 2) {
            fprintf(stderr, "Solver: Coincident constraint %d has only %d entityIds (need 2), skipping\n",
                     constraint.id, static_cast<int>(constraint.entityIds.size()));
            break;
        }
        {
            Slvs_hEntity pt1 = getPointHandle(constraint.entityIds[0], safeGet(constraint.pointIndices, 0, 0));
            Slvs_hEntity pt2 = getPointHandle(constraint.entityIds[1], safeGet(constraint.pointIndices, 1, 0));
            if (!pt1 || !pt2) {
                fprintf(stderr, "Solver: Coincident constraint %d: failed to resolve point handles, skipping\n",
                         constraint.id);
                break;
            }
            slvsConstraints.push_back(
                Slvs_MakeConstraint(
                    ch, sketchGroupId,
                    SLVS_C_POINTS_COINCIDENT,
                    workplaneHandle,
                    0.0,
                    pt1, pt2,
                    0, 0
                )
            );
        }
        break;

    case ConstraintType::Equal:
        if (constraint.entityIds.size() < 2 ||
            entityHandles.count(constraint.entityIds[0]) == 0 ||
            entityHandles.count(constraint.entityIds[1]) == 0) {
            fprintf(stderr, "Solver: Equal constraint %d: missing entities, skipping\n",
                     constraint.id);
            break;
        }
        {
            Slvs_hEntity e1 = entityHandles[constraint.entityIds[0]];
            Slvs_hEntity e2 = entityHandles[constraint.entityIds[1]];
            slvsConstraints.push_back(
                Slvs_MakeConstraint(
                    ch, sketchGroupId,
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
        if (constraint.entityIds.size() < 2 ||
            entityHandles.count(constraint.entityIds[0]) == 0 ||
            entityHandles.count(constraint.entityIds[1]) == 0) {
            fprintf(stderr, "Solver: Tangent constraint %d: missing entities, skipping\n",
                     constraint.id);
            break;
        }
        {
            Slvs_hEntity e1 = entityHandles[constraint.entityIds[0]];
            Slvs_hEntity e2 = entityHandles[constraint.entityIds[1]];
            slvsConstraints.push_back(
                Slvs_MakeConstraint(
                    ch, sketchGroupId,
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
        if (constraint.entityIds.size() < 2) {
            fprintf(stderr, "Solver: Midpoint constraint %d has only %d entityIds (need 2), skipping\n",
                     constraint.id, static_cast<int>(constraint.entityIds.size()));
            break;
        }
        {
            Slvs_hEntity pt = getPointHandle(constraint.entityIds[0], 0);
            Slvs_hEntity line = 0;
            if (entityHandles.count(constraint.entityIds[1]) > 0) {
                line = entityHandles[constraint.entityIds[1]];
            }
            if (!pt || !line) {
                fprintf(stderr, "Solver: Midpoint constraint %d: failed to resolve handles "
                         "(pt=%u, line=%u), skipping\n",
                         constraint.id, pt, line);
                break;
            }
            slvsConstraints.push_back(
                Slvs_MakeConstraint(
                    ch, sketchGroupId,
                    SLVS_C_AT_MIDPOINT,
                    workplaneHandle,
                    0.0,
                    pt, 0,
                    line, 0
                )
            );
        }
        break;

    case ConstraintType::Symmetric:
        if (constraint.entityIds.size() < 3) {
            fprintf(stderr, "Solver: Symmetric constraint %d has only %d entityIds (need 3), skipping\n",
                     constraint.id, static_cast<int>(constraint.entityIds.size()));
            break;
        }
        {
            Slvs_hEntity pt1 = getPointHandle(constraint.entityIds[0], safeGet(constraint.pointIndices, 0, 0));
            Slvs_hEntity pt2 = getPointHandle(constraint.entityIds[1], safeGet(constraint.pointIndices, 1, 0));
            Slvs_hEntity line = 0;
            if (entityHandles.count(constraint.entityIds[2]) > 0) {
                line = entityHandles[constraint.entityIds[2]];
            }
            if (!pt1 || !pt2 || !line) {
                fprintf(stderr, "Solver: Symmetric constraint %d: failed to resolve handles "
                         "(pt1=%u, pt2=%u, line=%u), skipping\n",
                         constraint.id, pt1, pt2, line);
                break;
            }
            slvsConstraints.push_back(
                Slvs_MakeConstraint(
                    ch, sketchGroupId,
                    SLVS_C_SYMMETRIC_LINE,
                    workplaneHandle,
                    0.0,
                    pt1, pt2,
                    line, 0
                )
            );
        }
        break;

    case ConstraintType::FixedPoint:
        // SLVS_C_WHERE_DRAGGED: tells the solver "keep this point where it is"
        if (constraint.entityIds.empty()) {
            fprintf(stderr, "Solver: FixedPoint constraint %d has no entityIds, skipping\n",
                     constraint.id);
            break;
        }
        {
            Slvs_hEntity pt = getPointHandle(constraint.entityIds[0],
                                              safeGet(constraint.pointIndices, 0, 0));
            if (!pt) {
                fprintf(stderr, "Solver: FixedPoint constraint %d: failed to resolve point handle, skipping\n",
                         constraint.id);
                break;
            }
            slvsConstraints.push_back(
                Slvs_MakeConstraint(
                    ch, sketchGroupId,
                    SLVS_C_WHERE_DRAGGED,
                    workplaneHandle,
                    0.0,
                    pt, 0,
                    0, 0
                )
            );
        }
        break;

    case ConstraintType::FixedAngle:
        // Fix a line's angle from horizontal using an internal reference line.
        // The reference is placed in workplaneGroupId (group 1) so the solver
        // treats it as frozen geometry.
        if (constraint.entityIds.empty() || entityHandles.count(constraint.entityIds[0]) == 0) {
            fprintf(stderr, "Solver: FixedAngle constraint %d: entity not in solver, skipping\n",
                     constraint.id);
            break;
        }
        {
            Slvs_hEntity targetLine = entityHandles[constraint.entityIds[0]];

            // Create internal horizontal reference line pinned at the origin
            Slvs_hParam ru1 = addParam(params, 0.0, workplaneGroupId);
            Slvs_hParam rv1 = addParam(params, 0.0, workplaneGroupId);
            Slvs_hEntity rp1 = nextEntityHandle++;
            slvsEntities.push_back(Slvs_MakePoint2d(rp1, workplaneGroupId,
                                                  workplaneHandle, ru1, rv1));

            Slvs_hParam ru2 = addParam(params, 100.0, workplaneGroupId);
            Slvs_hParam rv2 = addParam(params, 0.0, workplaneGroupId);
            Slvs_hEntity rp2 = nextEntityHandle++;
            slvsEntities.push_back(Slvs_MakePoint2d(rp2, workplaneGroupId,
                                                  workplaneHandle, ru2, rv2));

            Slvs_hEntity refLine = nextEntityHandle++;
            slvsEntities.push_back(Slvs_MakeLineSegment(refLine, workplaneGroupId,
                                                      workplaneHandle, rp1, rp2));

            slvsConstraints.push_back(
                Slvs_MakeConstraint(
                    ch, sketchGroupId,
                    SLVS_C_ANGLE,
                    workplaneHandle,
                    constraint.value,
                    0, 0,
                    refLine, targetLine
                )
            );
        }
        break;

    default:
        fprintf(stderr, "Solver: constraint %d has unsupported type %d, skipping\n",
                 constraint.id, static_cast<int>(constraint.type));
        break;
    }
}

#endif  // HAVE_SLVS

// =====================================================================
//  Utility Functions
// =====================================================================

std::string solveResultName(SolveResult::ResultCode code)
{
    switch (code) {
    case SolveResult::Okay:
        return "Solved successfully";
    case SolveResult::Inconsistent:
        return "Constraints are inconsistent (conflicting)";
    case SolveResult::DidntConverge:
        return "Solver didn't converge (too complex or ill-conditioned)";
    case SolveResult::TooManyUnknowns:
        return "Too many unknowns for solver";
    case SolveResult::RedundantOkay:
        return "Solved with redundant constraints";
    default:
        return "Unknown solver result";
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
    case ConstraintType::FixedPoint:
    case ConstraintType::FixedAngle:
        return true;
    default:
        return false;
    }
}

std::vector<ConstraintType> supportedConstraintTypes()
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
        ConstraintType::Symmetric,
        ConstraintType::FixedAngle
    };
}

}  // namespace sketch
}  // namespace hobbycad
