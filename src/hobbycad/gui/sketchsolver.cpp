// =====================================================================
//  src/hobbycad/gui/sketchsolver.cpp — Constraint solver wrapper (GUI)
// =====================================================================
//
//  Part of HobbyCAD GUI.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include "sketchsolver.h"
#include "sketchcanvas.h"
#include "sketchutils.h"

namespace hobbycad {

// =====================================================================
//  Constraint Conversion Utilities
// =====================================================================

// Constraint conversion functions (toLibraryConstraint, toLibraryConstraints)
// are provided by sketchutils.h

namespace {

/// Update GUI entities from solved library entities
void updateGuiEntitiesFromSolution(
    QVector<SketchEntity>& guiEntities,
    const QVector<sketch::Entity>& libEntities)
{
    // Create a map for quick lookup
    QMap<int, const sketch::Entity*> libMap;
    for (const sketch::Entity& lib : libEntities) {
        libMap[lib.id] = &lib;
    }

    // Update GUI entities with solved positions
    for (SketchEntity& gui : guiEntities) {
        if (libMap.contains(gui.id)) {
            const sketch::Entity* lib = libMap[gui.id];
            gui.points = lib->points;
            gui.radius = lib->radius;
            gui.startAngle = lib->startAngle;
            gui.sweepAngle = lib->sweepAngle;
        }
    }
}

}  // anonymous namespace

// =====================================================================
//  SketchSolver Implementation
// =====================================================================

SketchSolver::SketchSolver()
{
}

SketchSolver::~SketchSolver()
{
}

bool SketchSolver::isAvailable()
{
    return sketch::Solver::isAvailable();
}

SolveResult SketchSolver::solve(
    QVector<SketchEntity>& entities,
    const QVector<SketchConstraint>& constraints)
{
    // Convert to library types
    QVector<sketch::Entity> libEntities = toLibraryEntities(entities);
    QVector<sketch::Constraint> libConstraints = toLibraryConstraints(constraints);

    // Solve using library solver
    SolveResult result = m_solver.solve(libEntities, libConstraints);

    // If successful, update GUI entities with solved positions
    if (result.success) {
        updateGuiEntitiesFromSolution(entities, libEntities);
    }

    return result;
}

bool SketchSolver::wouldOverConstrain(
    const QVector<SketchEntity>& entities,
    const QVector<SketchConstraint>& existingConstraints,
    const SketchConstraint& newConstraint)
{
    QVector<sketch::Entity> libEntities = toLibraryEntities(entities);
    QVector<sketch::Constraint> libConstraints = toLibraryConstraints(existingConstraints);
    sketch::Constraint libNewConstraint = toLibraryConstraint(newConstraint);

    return m_solver.wouldOverConstrain(libEntities, libConstraints, libNewConstraint);
}

OverConstraintInfo SketchSolver::checkOverConstrain(
    const QVector<SketchEntity>& entities,
    const QVector<SketchConstraint>& existingConstraints,
    const SketchConstraint& newConstraint)
{
    QVector<sketch::Entity> libEntities = toLibraryEntities(entities);
    QVector<sketch::Constraint> libConstraints = toLibraryConstraints(existingConstraints);
    sketch::Constraint libNewConstraint = toLibraryConstraint(newConstraint);

    return m_solver.checkOverConstrain(libEntities, libConstraints, libNewConstraint);
}

int SketchSolver::degreesOfFreedom(
    const QVector<SketchEntity>& entities,
    const QVector<SketchConstraint>& constraints)
{
    QVector<sketch::Entity> libEntities = toLibraryEntities(entities);
    QVector<sketch::Constraint> libConstraints = toLibraryConstraints(constraints);

    return m_solver.degreesOfFreedom(libEntities, libConstraints);
}

}  // namespace hobbycad
