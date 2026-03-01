// =====================================================================
//  src/libhobbycad/sketch/decomposition.cpp — Entity decomposition
// =====================================================================
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include <hobbycad/sketch/decomposition.h>

#include <cmath>
#include <algorithm>
#include <string>

namespace hobbycad {
namespace sketch {

// =====================================================================
//  Helper: create a constraint with common defaults
// =====================================================================

static Constraint makeConstraint(int id, ConstraintType type,
                                  const std::vector<int>& entityIds,
                                  const std::vector<int>& pointIndices = {})
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
    const std::vector<std::pair<std::string, double>>& lockedDims,
    std::function<int()> nextEntityId,
    std::function<int()> nextConstraintId,
    int groupId,
    const std::vector<Group>& existingGroups,
    const std::string& typeName,
    bool isFreeform)
{
    DecompositionResult result;
    bool isRegular = !isFreeform;
    int sides = compound.sides;

    // --- Extract vertex positions ---
    std::vector<Point2D> vertices;
    Point2D center;

    if (isFreeform) {
        // Freeform: all points are vertices (no center stored)
        for (int i = 0; i < static_cast<int>(compound.points.size()); ++i) {
            vertices.push_back(compound.points[i]);
        }
        sides = static_cast<int>(vertices.size());
        if (sides < 3) return result;
    } else {
        // Regular: points[0] = center, points[1..N] = vertices
        if (static_cast<int>(compound.points.size()) < 1 + sides) return result;
        center = compound.points[0];
        for (int i = 1; i <= sides; ++i) {
            vertices.push_back(compound.points[i]);
        }
    }

    // Check if the radius is locked (determines full-constrained status)
    bool radiusLocked = false;
    if (isRegular) {
        for (const auto& [label, value] : lockedDims) {
            if (label == "Radius") {
                radiusLocked = true;
                break;
            }
        }
    }
    // Freeform polygons have no radius DOF — constrained by vertex positions
    bool fullyConstrained = isFreeform || radiusLocked;

    // --- Create N Line entities ---
    std::vector<Entity> lines(sides);
    std::vector<int> lineIds;
    for (int i = 0; i < sides; ++i) {
        lines[i].id = nextEntityId();
        lines[i].type = EntityType::Line;
        lines[i].points.push_back(vertices[i]);
        lines[i].points.push_back(vertices[(i + 1) % sides]);
        lines[i].isConstruction = compound.isConstruction;
        lines[i].constrained = fullyConstrained;
        lineIds.push_back(lines[i].id);
    }
    result.entities = lines;

    std::vector<int> allEntityIds = lineIds;

    // --- Create Construction Circle (regular polygons only) ---
    Entity circleEnt;
    int circleId = -1;
    if (isRegular) {
        circleEnt.id = nextEntityId();
        circleEnt.type = EntityType::Circle;
        circleEnt.points.push_back(center);
        circleEnt.radius = compound.radius;  // Original circle radius
        circleEnt.isConstruction = true;
        circleEnt.constrained = fullyConstrained;
        circleId = circleEnt.id;
        allEntityIds.push_back(circleId);
        result.entities.push_back(circleEnt);
    }

    std::vector<int> constraintIds;

    // --- Create N Coincident constraints (vertex connections) ---
    for (int i = 0; i < sides; ++i) {
        int nextI = (i + 1) % sides;
        Constraint cc = makeConstraint(nextConstraintId(), ConstraintType::Coincident,
                                        {lineIds[i], lineIds[nextI]}, {1, 0});
        result.constraints.push_back(cc);
        constraintIds.push_back(cc.id);
    }

    // --- Create (N-1) Equal constraints (regular polygons only) ---
    if (isRegular && sides > 1) {
        for (int i = 1; i < sides; ++i) {
            Constraint eq = makeConstraint(nextConstraintId(), ConstraintType::Equal,
                                            {lineIds[0], lineIds[i]});
            result.constraints.push_back(eq);
            constraintIds.push_back(eq.id);
        }
    }

    // --- Create Radius constraint from locked dim (regular only) ---
    if (isRegular && circleId >= 0) {
        for (const auto& [label, value] : lockedDims) {
            if (label == "Radius") {
                Constraint rc = makeConstraint(nextConstraintId(), ConstraintType::Radius,
                                                {circleId});
                rc.value = value;
                rc.labelVisible = true;
                // Place label at the midpoint between center and first
                // vertex so it sits along the radius line, between the
                // center and the circle edge (standard CAD convention).
                rc.labelPosition = !vertices.empty()
                    ? (center + vertices[0]) / 2.0
                    : center + Point2D(value / 2.0, 0);
                result.constraints.push_back(rc);
                constraintIds.push_back(rc.id);
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
        result.constraints.push_back(fp);
        constraintIds.push_back(fp.id);
    }
    if (isRegular && circleId >= 0) {
        // Pin the construction circle center as well
        Constraint fpCenter = makeConstraint(nextConstraintId(), ConstraintType::FixedPoint,
                                              {circleId}, {0});
        fpCenter.labelVisible = false;
        result.constraints.push_back(fpCenter);
        constraintIds.push_back(fpCenter.id);
    }

    // --- Create named group ---
    int serial = 1;
    for (const Group& g : existingGroups) {
        if (g.name.rfind(typeName, 0) == 0)
            serial++;
    }

    result.group.id = groupId;
    // Format: typeName + 4-digit serial
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d", serial);
    result.group.name = typeName + buf;
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
    const std::vector<std::pair<std::string, double>>& lockedDims,
    std::function<int()> nextEntityId,
    std::function<int()> nextConstraintId,
    int groupId,
    const std::vector<Group>& existingGroups,
    const std::string& typeName)
{
    DecompositionResult result;
    const auto type = compound.type;

    // --- Compute 4 corners ---
    Point2D c[4];
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
            const Point2D& p0 = compound.points[0];
            const Point2D& p1 = compound.points[1];
            c[0] = p0;
            c[1] = Point2D(p1.x, p0.y);
            c[2] = p1;
            c[3] = Point2D(p0.x, p1.y);
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
    std::vector<int> lineIds;
    for (int i = 0; i < 4; ++i) {
        lines[i].id = nextEntityId();
        lines[i].type = EntityType::Line;
        lines[i].points.push_back(c[i]);
        lines[i].points.push_back(c[(i + 1) % 4]);
        lines[i].isConstruction = compound.isConstruction;
        lines[i].constrained = true;
        lineIds.push_back(lines[i].id);
    }

    std::vector<int> constraintIds;

    // --- Create 4 Coincident constraints ---
    for (int i = 0; i < 4; ++i) {
        int nextI = (i + 1) % 4;
        Constraint cc = makeConstraint(nextConstraintId(), ConstraintType::Coincident,
                                        {lineIds[i], lineIds[nextI]}, {1, 0});
        result.constraints.push_back(cc);
        constraintIds.push_back(cc.id);
    }

    // --- Create geometric constraints by type ---
    if (type == EntityType::Rectangle && axisAligned) {
        // 2 Horizontal (lines 0, 2) + 2 Vertical (lines 1, 3)
        for (int i = 0; i < 4; ++i) {
            ConstraintType ct = (i % 2 == 0) ? ConstraintType::Horizontal : ConstraintType::Vertical;
            Constraint gc = makeConstraint(nextConstraintId(), ct, {lineIds[i]});
            result.constraints.push_back(gc);
            constraintIds.push_back(gc.id);
        }
    } else if (type == EntityType::Rectangle && !axisAligned) {
        // Rotated rectangle: 1 Perpendicular (line0 ⊥ line1) + 2 Parallel
        Constraint perp = makeConstraint(nextConstraintId(), ConstraintType::Perpendicular,
                                          {lineIds[0], lineIds[1]});
        result.constraints.push_back(perp);
        constraintIds.push_back(perp.id);

        for (int pair = 0; pair < 2; ++pair) {
            Constraint par = makeConstraint(nextConstraintId(), ConstraintType::Parallel,
                                             {lineIds[pair], lineIds[pair + 2]});
            result.constraints.push_back(par);
            constraintIds.push_back(par.id);
        }
    } else {
        // Parallelogram: 2 Parallel (line0 || line2, line1 || line3)
        for (int pair = 0; pair < 2; ++pair) {
            Constraint par = makeConstraint(nextConstraintId(), ConstraintType::Parallel,
                                             {lineIds[pair], lineIds[pair + 2]});
            result.constraints.push_back(par);
            constraintIds.push_back(par.id);
        }
    }

    // --- Create constraints from locked dimension fields ---
    for (const auto& [label, value] : lockedDims) {
        // --- Angle constraints ---
        if (label.find("Angle") != std::string::npos) {
            Constraint ac;
            ac.id = nextConstraintId();
            ac.isDriving = true;
            ac.enabled = true;
            ac.satisfied = true;
            ac.labelVisible = true;

            if (label == "Edge2 Angle") {
                // Inside angle at vertex c[1] between edge1 (line0) and edge2 (line1).
                ac.type = ConstraintType::Angle;
                ac.value = 180.0 - value;
                ac.entityIds = {lineIds[0], lineIds[1]};
                ac.anchorPoint = c[1];
                ac.supplementary = false;
                ac.labelPosition = c[1] + Point2D(15, -15);
            } else if (label == "Edge1 Angle" || label == "Edge Angle") {
                // Absolute orientation of edge1 from horizontal (degrees)
                ac.type = ConstraintType::FixedAngle;
                ac.value = value;
                ac.entityIds = {lineIds[0]};
                ac.anchorPoint = c[0];
                Point2D mid = (c[0] + c[1]) / 2.0;
                ac.labelPosition = mid + Point2D(0, 15);
            } else {
                continue;  // Unknown angle label -- skip
            }

            result.constraints.push_back(ac);
            constraintIds.push_back(ac.id);
            continue;  // Don't fall through to Distance creation below
        }

        // --- Distance constraints ---
        Constraint dc;
        dc.id = nextConstraintId();
        dc.type = ConstraintType::Distance;
        dc.isDriving = true;
        dc.enabled = true;
        dc.satisfied = true;
        dc.labelVisible = true;
        dc.value = value;

        if (label == "Width") {
            dc.entityIds = {lineIds[0], lineIds[0]};
            dc.pointIndices = {0, 1};
            dc.labelPosition = (c[0] + c[1]) / 2.0 + Point2D(0, -10);
        } else if (label == "Height") {
            dc.entityIds = {lineIds[1], lineIds[1]};
            dc.pointIndices = {0, 1};
            dc.labelPosition = (c[1] + c[2]) / 2.0 + Point2D(10, 0);
        } else if (label == "Edge Length" || label == "Edge1") {
            dc.entityIds = {lineIds[0], lineIds[0]};
            dc.pointIndices = {0, 1};
            dc.labelPosition = (c[0] + c[1]) / 2.0 + Point2D(0, -10);
        } else if (label == "Edge2") {
            dc.entityIds = {lineIds[1], lineIds[1]};
            dc.pointIndices = {0, 1};
            dc.labelPosition = (c[1] + c[2]) / 2.0 + Point2D(10, 0);
        } else {
            continue;  // Unknown label -- skip
        }

        result.constraints.push_back(dc);
        constraintIds.push_back(dc.id);
    }

    // --- Create FixedPoint constraint on the pivot corner (c[0]) ---
    {
        Constraint fp = makeConstraint(nextConstraintId(), ConstraintType::FixedPoint,
                                        {lineIds[0]}, {0});
        fp.labelVisible = false;
        result.constraints.push_back(fp);
        constraintIds.push_back(fp.id);
    }

    // --- Create named group ---
    int serial = 1;
    for (const Group& g : existingGroups) {
        if (g.name.rfind(typeName, 0) == 0)
            serial++;
    }

    result.group.id = groupId;
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d", serial);
    result.group.name = typeName + buf;
    result.group.entityIds = lineIds;
    result.group.constraintIds = constraintIds;
    result.group.locked = false;
    result.group.expanded = true;

    // Add line entities to result (with groupId set)
    for (int i = 0; i < 4; ++i) {
        lines[i].groupId = groupId;
        result.entities.push_back(lines[i]);
    }

    result.success = true;
    return result;
}

// =====================================================================
//  Public API
// =====================================================================

DecompositionResult decomposeEntity(
    const Entity& compound,
    const std::vector<std::pair<std::string, double>>& lockedDims,
    std::function<int()> nextEntityId,
    std::function<int()> nextConstraintId,
    int groupId,
    const std::vector<Group>& existingGroups,
    const std::string& typeName,
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
