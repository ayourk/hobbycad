// =====================================================================
//  src/libhobbycad/sketch/decomposition.cpp — Entity decomposition
// =====================================================================
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include <hobbycad/sketch/decomposition.h>

#include <QLineF>
#include <QtMath>

namespace hobbycad {
namespace sketch {

// =====================================================================
//  Helper: create a constraint with common defaults
// =====================================================================

static Constraint makeConstraint(int id, ConstraintType type,
                                  const QVector<int>& entityIds,
                                  const QVector<int>& pointIndices = {})
{
    Constraint c;
    c.id = id;
    c.type = type;
    c.entityIds = entityIds;
    c.pointIndices = pointIndices;
    c.isDriving = true;
    c.enabled = true;
    c.satisfied = true;
    c.labelVisible = false;
    return c;
}

// =====================================================================
//  Polygon decomposition
// =====================================================================

static DecompositionResult decomposePolygon(
    const Entity& compound,
    const QVector<QPair<QString, double>>& lockedDims,
    std::function<int()> nextEntityId,
    std::function<int()> nextConstraintId,
    int groupId,
    const QVector<Group>& existingGroups,
    const QString& typeName,
    bool isFreeform)
{
    DecompositionResult result;
    bool isRegular = !isFreeform;
    int sides = compound.sides;

    // --- Extract vertex positions ---
    QVector<QPointF> vertices;
    QPointF center;

    if (isFreeform) {
        // Freeform: all points are vertices (no center stored)
        for (int i = 0; i < compound.points.size(); ++i) {
            vertices.append(compound.points[i]);
        }
        sides = vertices.size();
        if (sides < 3) return result;
    } else {
        // Regular: points[0] = center, points[1..N] = vertices
        if (compound.points.size() < 1 + sides) return result;
        center = compound.points[0];
        for (int i = 1; i <= sides; ++i) {
            vertices.append(compound.points[i]);
        }
    }

    // Check if the radius is locked (determines full-constrained status)
    bool radiusLocked = false;
    if (isRegular) {
        for (const auto& [label, value] : lockedDims) {
            if (label == QStringLiteral("Radius")) {
                radiusLocked = true;
                break;
            }
        }
    }
    // Freeform polygons have no radius DOF — constrained by vertex positions
    bool fullyConstrained = isFreeform || radiusLocked;

    // --- Create N Line entities ---
    QVector<Entity> lines(sides);
    QVector<int> lineIds;
    for (int i = 0; i < sides; ++i) {
        lines[i].id = nextEntityId();
        lines[i].type = EntityType::Line;
        lines[i].points.append(vertices[i]);
        lines[i].points.append(vertices[(i + 1) % sides]);
        lines[i].isConstruction = compound.isConstruction;
        lines[i].constrained = fullyConstrained;
        lineIds.append(lines[i].id);
    }
    result.entities = lines;

    QVector<int> allEntityIds = lineIds;

    // --- Create Construction Circle (regular polygons only) ---
    Entity circleEnt;
    int circleId = -1;
    if (isRegular) {
        circleEnt.id = nextEntityId();
        circleEnt.type = EntityType::Circle;
        circleEnt.points.append(center);
        circleEnt.radius = compound.radius;  // Original circle radius
        circleEnt.isConstruction = true;
        circleEnt.constrained = fullyConstrained;
        circleId = circleEnt.id;
        allEntityIds.append(circleId);
        result.entities.append(circleEnt);
    }

    QVector<int> constraintIds;

    // --- Create N Coincident constraints (vertex connections) ---
    for (int i = 0; i < sides; ++i) {
        int nextI = (i + 1) % sides;
        Constraint cc = makeConstraint(nextConstraintId(), ConstraintType::Coincident,
                                        {lineIds[i], lineIds[nextI]}, {1, 0});
        result.constraints.append(cc);
        constraintIds.append(cc.id);
    }

    // --- Create (N-1) Equal constraints (regular polygons only) ---
    if (isRegular && sides > 1) {
        for (int i = 1; i < sides; ++i) {
            Constraint eq = makeConstraint(nextConstraintId(), ConstraintType::Equal,
                                            {lineIds[0], lineIds[i]});
            result.constraints.append(eq);
            constraintIds.append(eq.id);
        }
    }

    // --- Create Radius constraint from locked dim (regular only) ---
    if (isRegular && circleId >= 0) {
        for (const auto& [label, value] : lockedDims) {
            if (label == QStringLiteral("Radius")) {
                Constraint rc = makeConstraint(nextConstraintId(), ConstraintType::Radius,
                                                {circleId});
                rc.value = value;
                rc.labelVisible = true;
                // Place label at the midpoint between center and first
                // vertex so it sits along the radius line, between the
                // center and the circle edge (standard CAD convention).
                rc.labelPosition = !vertices.isEmpty()
                    ? (center + vertices[0]) / 2.0
                    : center + QPointF(value / 2.0, 0);
                result.constraints.append(rc);
                constraintIds.append(rc.id);
            }
        }
    }

    // --- Create FixedPoint constraint on the first vertex ---
    // Anchors the polygon so the decomposed shape stays where the user
    // placed it.  For regular polygons the construction-circle center is
    // also pinned (it carries the positional DOF).
    {
        Constraint fp = makeConstraint(nextConstraintId(), ConstraintType::FixedPoint,
                                        {lineIds[0]}, {0});
        fp.labelVisible = false;
        result.constraints.append(fp);
        constraintIds.append(fp.id);
    }
    if (isRegular && circleId >= 0) {
        // Pin the construction circle center as well
        Constraint fpCenter = makeConstraint(nextConstraintId(), ConstraintType::FixedPoint,
                                              {circleId}, {0});
        fpCenter.labelVisible = false;
        result.constraints.append(fpCenter);
        constraintIds.append(fpCenter.id);
    }

    // --- Create named group ---
    int serial = 1;
    for (const Group& g : existingGroups) {
        if (g.name.startsWith(typeName))
            serial++;
    }

    result.group.id = groupId;
    result.group.name = QStringLiteral("%1%2").arg(typeName).arg(serial, 4, 10, QLatin1Char('0'));
    result.group.entityIds = allEntityIds;
    result.group.constraintIds = constraintIds;
    result.group.locked = false;
    result.group.expanded = true;

    // Set groupId on all entities
    for (Entity& e : result.entities) {
        e.groupId = groupId;
    }

    result.success = true;
    return result;
}

// =====================================================================
//  Rectangle / Parallelogram decomposition
// =====================================================================

static DecompositionResult decomposeRectParallelogram(
    const Entity& compound,
    const QVector<QPair<QString, double>>& lockedDims,
    std::function<int()> nextEntityId,
    std::function<int()> nextConstraintId,
    int groupId,
    const QVector<Group>& existingGroups,
    const QString& typeName)
{
    DecompositionResult result;
    const auto type = compound.type;

    // --- Compute 4 corners ---
    QPointF c[4];
    bool axisAligned = false;

    if (type == EntityType::Rectangle) {
        if (compound.points.size() >= 4) {
            // Rotated / 3-point rectangle (already has 4 corners)
            c[0] = compound.points[0];
            c[1] = compound.points[1];
            c[2] = compound.points[2];
            c[3] = compound.points[3];
        } else if (compound.points.size() >= 2) {
            // Axis-aligned 2-point rectangle
            const QPointF& p0 = compound.points[0];
            const QPointF& p1 = compound.points[1];
            c[0] = p0;
            c[1] = QPointF(p1.x(), p0.y());
            c[2] = p1;
            c[3] = QPointF(p0.x(), p1.y());
            axisAligned = true;
        } else {
            return result;
        }
    } else {
        // Parallelogram: already has 4 points
        if (compound.points.size() < 4) return result;
        for (int i = 0; i < 4; ++i) c[i] = compound.points[i];
    }

    // --- Create 4 Line entities ---
    Entity lines[4];
    QVector<int> lineIds;
    for (int i = 0; i < 4; ++i) {
        lines[i].id = nextEntityId();
        lines[i].type = EntityType::Line;
        lines[i].points.append(c[i]);
        lines[i].points.append(c[(i + 1) % 4]);
        lines[i].isConstruction = compound.isConstruction;
        lines[i].constrained = true;
        lineIds.append(lines[i].id);
    }

    QVector<int> constraintIds;

    // --- Create 4 Coincident constraints ---
    for (int i = 0; i < 4; ++i) {
        int nextI = (i + 1) % 4;
        Constraint cc = makeConstraint(nextConstraintId(), ConstraintType::Coincident,
                                        {lineIds[i], lineIds[nextI]}, {1, 0});
        result.constraints.append(cc);
        constraintIds.append(cc.id);
    }

    // --- Create geometric constraints by type ---
    if (type == EntityType::Rectangle && axisAligned) {
        // 2 Horizontal (lines 0, 2) + 2 Vertical (lines 1, 3)
        for (int i = 0; i < 4; ++i) {
            ConstraintType ct = (i % 2 == 0) ? ConstraintType::Horizontal : ConstraintType::Vertical;
            Constraint gc = makeConstraint(nextConstraintId(), ct, {lineIds[i]});
            result.constraints.append(gc);
            constraintIds.append(gc.id);
        }
    } else if (type == EntityType::Rectangle && !axisAligned) {
        // Rotated rectangle: 1 Perpendicular (line0 ⊥ line1) + 2 Parallel
        Constraint perp = makeConstraint(nextConstraintId(), ConstraintType::Perpendicular,
                                          {lineIds[0], lineIds[1]});
        result.constraints.append(perp);
        constraintIds.append(perp.id);

        for (int pair = 0; pair < 2; ++pair) {
            Constraint par = makeConstraint(nextConstraintId(), ConstraintType::Parallel,
                                             {lineIds[pair], lineIds[pair + 2]});
            result.constraints.append(par);
            constraintIds.append(par.id);
        }
    } else {
        // Parallelogram: 2 Parallel (line0 ‖ line2, line1 ‖ line3)
        for (int pair = 0; pair < 2; ++pair) {
            Constraint par = makeConstraint(nextConstraintId(), ConstraintType::Parallel,
                                             {lineIds[pair], lineIds[pair + 2]});
            result.constraints.append(par);
            constraintIds.append(par.id);
        }
    }

    // --- Create Distance constraints from locked dimension fields ---
    for (const auto& [label, value] : lockedDims) {
        if (label.contains(QStringLiteral("Angle")))
            continue;  // Angle constraints not yet supported

        Constraint dc;
        dc.id = nextConstraintId();
        dc.type = ConstraintType::Distance;
        dc.isDriving = true;
        dc.enabled = true;
        dc.satisfied = true;
        dc.labelVisible = true;
        dc.value = value;

        if (label == QStringLiteral("Width")) {
            dc.entityIds = {lineIds[0], lineIds[0]};
            dc.pointIndices = {0, 1};
            dc.labelPosition = (c[0] + c[1]) / 2.0 + QPointF(0, -10);
        } else if (label == QStringLiteral("Height")) {
            dc.entityIds = {lineIds[1], lineIds[1]};
            dc.pointIndices = {0, 1};
            dc.labelPosition = (c[1] + c[2]) / 2.0 + QPointF(10, 0);
        } else if (label == QStringLiteral("Edge Length") || label == QStringLiteral("Edge1")) {
            dc.entityIds = {lineIds[0], lineIds[0]};
            dc.pointIndices = {0, 1};
            dc.labelPosition = (c[0] + c[1]) / 2.0 + QPointF(0, -10);
        } else if (label == QStringLiteral("Edge2")) {
            dc.entityIds = {lineIds[1], lineIds[1]};
            dc.pointIndices = {0, 1};
            dc.labelPosition = (c[1] + c[2]) / 2.0 + QPointF(10, 0);
        } else {
            continue;  // Unknown label — skip
        }

        result.constraints.append(dc);
        constraintIds.append(dc.id);
    }

    // --- Create FixedPoint constraint on the pivot corner (c[0]) ---
    // This anchors the first corner so the decomposed shape stays where
    // the user placed it and can still pivot around that point.
    {
        Constraint fp = makeConstraint(nextConstraintId(), ConstraintType::FixedPoint,
                                        {lineIds[0]}, {0});
        fp.labelVisible = false;
        result.constraints.append(fp);
        constraintIds.append(fp.id);
    }

    // --- Create named group ---
    int serial = 1;
    for (const Group& g : existingGroups) {
        if (g.name.startsWith(typeName))
            serial++;
    }

    result.group.id = groupId;
    result.group.name = QStringLiteral("%1%2").arg(typeName).arg(serial, 4, 10, QLatin1Char('0'));
    result.group.entityIds = lineIds;
    result.group.constraintIds = constraintIds;
    result.group.locked = false;
    result.group.expanded = true;

    // Add line entities to result (with groupId set)
    for (int i = 0; i < 4; ++i) {
        lines[i].groupId = groupId;
        result.entities.append(lines[i]);
    }

    result.success = true;
    return result;
}

// =====================================================================
//  Public API
// =====================================================================

DecompositionResult decomposeEntity(
    const Entity& compound,
    const QVector<QPair<QString, double>>& lockedDims,
    std::function<int()> nextEntityId,
    std::function<int()> nextConstraintId,
    int groupId,
    const QVector<Group>& existingGroups,
    const QString& typeName,
    bool isFreeform)
{
    switch (compound.type) {
    case EntityType::Polygon:
        return decomposePolygon(compound, lockedDims, nextEntityId, nextConstraintId,
                                 groupId, existingGroups, typeName, isFreeform);

    case EntityType::Rectangle:
    case EntityType::Parallelogram:
        return decomposeRectParallelogram(compound, lockedDims, nextEntityId, nextConstraintId,
                                           groupId, existingGroups, typeName);

    default:
        return DecompositionResult{};  // Not a compound entity
    }
}

}  // namespace sketch
}  // namespace hobbycad
