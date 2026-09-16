// =====================================================================
//  src/libhobbycad/sketch/decomposition.cpp — Entity decomposition
// =====================================================================
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include <hobbycad/sketch/decomposition.h>
#include <hobbycad/units.h>

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
    // Freeform polygons have no radius DOF (constrained by vertex positions)
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

    // No automatic Fixed Point. A fresh polygon floats, the way it does in
    // Fusion and Onshape: nothing is anchored unless the user applies Fix.
    // (Until 2026-09-03 the first vertex and, for regular polygons, the
    // construction-circle center were pinned here; that hid two degrees of
    // freedom and made a corner drag rotate the shape about the pin.)

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
        // Stored corners (rotated / 3-point) or the two diagonal corners expanded.
        if (!rectangleCorners(compound, c)) return result;
        axisAligned = compound.points.size() < 4;
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
                continue;  // Unknown angle label: skip
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
            continue;  // Unknown label: skip
        }

        result.constraints.push_back(dc);
        constraintIds.push_back(dc.id);
    }

    // No automatic Fixed Point on the first corner: a fresh rectangle has
    // four degrees of freedom (position, width, height) as in Fusion and
    // Onshape, and a corner drag resizes it instead of rotating it about
    // an anchor the user never asked for. Fix is a tool, not a default.

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

// =====================================================================
//  2D Sweep decomposition
// =====================================================================

namespace hobbycad {
namespace sketch {

namespace {

/// A serial name like "Sweep 3", counting the ones already there.
std::string sweepName(const std::vector<Group>& existing)
{
    int n = 0;
    for (const auto& g : existing) {
        if (g.name.rfind("Sweep", 0) == 0) ++n;
    }
    return "Sweep " + std::to_string(n + 1);
}

Constraint pair(int id, ConstraintType t, int a, int b)
{
    Constraint c;
    c.id = id;
    c.type = t;
    c.entityIds = { a, b };
    c.enabled = true;
    return c;
}

Constraint coincident(int id, int a, int aPt, int b, int bPt)
{
    Constraint c = pair(id, ConstraintType::Coincident, a, b);
    c.pointIndices = { aPt, bPt };
    return c;
}

}  // namespace

// Straight round-ended slot as the mainstream "racetrack": two side lines +
// two outward cap arcs joined by endpoint coincidences, tangencies and one
// equal-radius, with the path kept as the construction centerline (no
// Parallel; ~5 DOF). Extracted from decomposeSweep for readability. (Aaron)
static DecompositionResult decomposeStraightRacetrack(
        const Entity& path, double halfWidth,
        const std::function<int()>& nextEntityId,
        const std::function<int()>& nextConstraintId,
        int groupId, const std::vector<Group>& existingGroups)
{
    DecompositionResult out;
        const Point2D a = path.points[0], b = path.points[1];
        const double dx = b.x - a.x, dy = b.y - a.y;
        const double len = std::hypot(dx, dy);
        if (len < geometry::kZeroEps) return out;
        const Point2D n{ -dy / len * halfWidth, dx / len * halfWidth };  // +offset
        const Point2D pdir{ dx / len, dy / len };

        auto pt = [](double x, double y) { return Point2D{ x, y }; };

        Entity side1;   // +n side
        side1.id = nextEntityId();
        side1.type = EntityType::Line;
        side1.points = { pt(a.x + n.x, a.y + n.y), pt(b.x + n.x, b.y + n.y) };

        Entity side2;   // -n side
        side2.id = nextEntityId();
        side2.type = EntityType::Line;
        side2.points = { pt(a.x - n.x, a.y - n.y), pt(b.x - n.x, b.y - n.y) };

        // Two cap arcs centered on the path ends, each bulging OUTWARD (away
        // from the other end). Point layout: {center, e1, e2}.
        auto capArc = [&](const Point2D& c, const Point2D& e1, const Point2D& e2,
                          const Point2D& outward) {
            Entity e;
            e.id = nextEntityId();
            e.type = EntityType::Arc;
            e.radius = halfWidth;
            const double sA = std::atan2(e1.y - c.y, e1.x - c.x);
            auto midOutward = [&](double sw) {
                const double m = sA + hobbycad::degreesToRadians(sw) / 2.0;
                return std::cos(m) * outward.x + std::sin(m) * outward.y;
            };
            e.startAngle = hobbycad::radiansToDegrees(sA);
            e.sweepAngle = (midOutward(180.0) >= midOutward(-180.0)) ? 180.0 : -180.0;
            e.points = { c, e1, e2 };
            return e;
        };
        Entity capA = capArc(a, pt(a.x + n.x, a.y + n.y), pt(a.x - n.x, a.y - n.y),
                             pt(-pdir.x, -pdir.y));
        Entity capB = capArc(b, pt(b.x + n.x, b.y + n.y), pt(b.x - n.x, b.y - n.y),
                             pt(pdir.x, pdir.y));

        out.entities = { side1, side2, capA, capB };

        // 4 endpoint coincidences (line end <-> arc end).
        out.constraints.push_back(coincident(nextConstraintId(), side1.id, 0, capA.id, 1));
        out.constraints.push_back(coincident(nextConstraintId(), side2.id, 0, capA.id, 2));
        out.constraints.push_back(coincident(nextConstraintId(), side1.id, 1, capB.id, 1));
        out.constraints.push_back(coincident(nextConstraintId(), side2.id, 1, capB.id, 2));
        // 4 tangencies at those junctions (arc <-> side).
        out.constraints.push_back(pair(nextConstraintId(), ConstraintType::Tangent, capA.id, side1.id));
        out.constraints.push_back(pair(nextConstraintId(), ConstraintType::Tangent, capA.id, side2.id));
        out.constraints.push_back(pair(nextConstraintId(), ConstraintType::Tangent, capB.id, side1.id));
        out.constraints.push_back(pair(nextConstraintId(), ConstraintType::Tangent, capB.id, side2.id));
        // Both caps one radius (the width). No Parallel (implied).
        out.constraints.push_back(pair(nextConstraintId(), ConstraintType::Equal, capA.id, capB.id));
        // Keep the path as the construction centerline: tie its ends to the arc
        // centers (a line's ends are points 0/1).
        out.constraints.push_back(coincident(nextConstraintId(), path.id, 0, capA.id, 0));
        out.constraints.push_back(coincident(nextConstraintId(), path.id, 1, capB.id, 0));

        out.group.id = groupId;
        out.group.name = sweepName(existingGroups);
        for (const auto& e : out.entities) out.group.entityIds.push_back(e.id);
        for (const auto& c : out.constraints) out.group.constraintIds.push_back(c.id);
        out.group.locked = false;
        out.success = true;
        return out;
}

DecompositionResult decomposeSweep(const Entity& path,
                                   double halfWidth,
                                   SweepEndStyle ends,
                                   std::function<int()> nextEntityId,
                                   std::function<int()> nextConstraintId,
                                   int groupId,
                                   const std::vector<Group>& existingGroups)
{
    DecompositionResult out;
    if (!slotWidthIsPositive(2.0 * halfWidth)) return out;

    // Straight round-ended slot uses the mainstream racetrack model; the
    // arc-path and flat-end cases fall through to the construction below.
    if (path.type == EntityType::Line && ends == SweepEndStyle::Round
            && path.points.size() >= 2) {
        return decomposeStraightRacetrack(path, halfWidth, nextEntityId,
                                          nextConstraintId, groupId,
                                          existingGroups);
    }

    std::vector<Entity> sides;   // the two offset elements
    Point2D capA0, capA1, capB0, capB1;   // where each cap starts and ends

    if (path.type == EntityType::Line) {
        if (path.points.size() < 2) return out;
        const Point2D a = path.points[0], b = path.points[1];
        const double dx = b.x - a.x, dy = b.y - a.y;
        const double len = std::hypot(dx, dy);
        if (len < geometry::kZeroEps) return out;
        const Point2D n{ -dy / len * halfWidth, dx / len * halfWidth };

        Entity left;
        left.id = nextEntityId();
        left.type = EntityType::Line;
        left.points = { { a.x + n.x, a.y + n.y }, { b.x + n.x, b.y + n.y } };

        Entity right;
        right.id = nextEntityId();
        right.type = EntityType::Line;
        right.points = { { a.x - n.x, a.y - n.y }, { b.x - n.x, b.y - n.y } };

        sides = { left, right };
        capA0 = right.points[0]; capA1 = left.points[0];   // cap at a
        capB0 = left.points[1];  capB1 = right.points[1];  // cap at b

    } else if (path.type == EntityType::Arc) {
        // Aaron's rule (2026-08-28): "slot width/2 is less than or equal to
        // the arc radius". Only PAST equality does the inner edge turn inside
        // out; AT equality the inner side is the arc's center itself.
        if (path.points.empty() || path.radius < halfWidth - geometry::kDegenerateLen) return out;
        const bool innerIsCenter = (path.radius - halfWidth) <= geometry::kDegenerateLen;
        const Point2D c = path.points[0];
        const double a0 = hobbycad::degreesToRadians(path.startAngle);
        const double a1 = hobbycad::degreesToRadians(path.startAngle + path.sweepAngle);

        Entity outer;
        outer.id = nextEntityId();
        outer.type = EntityType::Arc;
        outer.points = { c };
        outer.radius = path.radius + halfWidth;
        outer.startAngle = path.startAngle;
        outer.sweepAngle = path.sweepAngle;

        auto at = [&](const Entity& e, double ang) {
            return Point2D{ c.x + e.radius * std::cos(ang),
                            c.y + e.radius * std::sin(ang) };
        };
        outer.points = { c, at(outer, a0), at(outer, a1) };

        Entity inner;
        inner.id = nextEntityId();
        if (innerIsCenter) {
            // The limit case: the inner edge has collapsed onto the center,
            // so the inner side is a Point there and both caps meet at it
            // (a 180-degree slot, the shape the rule keeps).
            inner.type = EntityType::Point;
            inner.points = { c };
            capA0 = c; capB1 = c;
        } else {
            inner = outer;
            inner.id = nextEntityId();
            inner.radius = path.radius - halfWidth;
            inner.points = { c, at(inner, a0), at(inner, a1) };
            capA0 = inner.points[1]; capB1 = inner.points[2];
        }
        sides = { outer, inner };
        capA1 = outer.points[1];
        capB0 = outer.points[2];

    } else {
        // Longer or curvier paths need the offsets trimmed where elements
        // meet, which is the classic offset-join problem. Not attempted
        // rather than attempted badly.
        return out;
    }

    // ---- The ends ---------------------------------------------------
    //
    // A FLAT end is TWO segments meeting at the path's endpoint, not one
    // line across. Aaron, 2026-08-28: "Maybe using 2 line segments per end
    // makes that easier?" and "midpoint becomes important for line based
    // ends so that resizing of one end shrinks or grows from the midpoint."
    //
    // It is easier, and it earns its keep three times over:
    //
    //   * each half's LENGTH is the half-width, so Equal between halves
    //     states the width directly instead of implying it;
    //   * the two halves sharing their inner point IS the midpoint
    //     behavior: the end stays centered on the path, so growing it
    //     grows both ways;
    //   * chained across every half, Equal says "one width throughout" in
    //     a form that works for one end, two, or a tree of them.
    //
    // Measured: four halves starting at 6, 3, 4 and 7 solve to 5.000000
    // each, UNDER-constrained, with no Parallel and no Equal(sides) needed
    // at all. The single-line cap needed both of those and still reported
    // Equal(caps) as redundant.
    const int s0 = sides[0].id, s1 = sides[1].id;
    const bool sideIsArc = (path.type == EntityType::Arc);
    const int sideStart = sideIsArc ? 1 : 0;
    const int sideEnd   = sideIsArc ? 2 : 1;
    // An inner side collapsed to the center (the width limit) is a Point:
    // both of its "ends" are its one point.
    const bool innerIsPoint = (sides[1].type == EntityType::Point);
    const int s1Start = innerIsPoint ? 0 : sideStart;
    const int s1End   = innerIsPoint ? 0 : sideEnd;

    // Where each end sits on the path, and which way "outward" points there
    // (along the path, away from the body). The round caps must bulge that
    // way, not back into the slot.
    Point2D endA, endB, outwardA, outwardB;
    if (path.type == EntityType::Line) {
        endA = path.points[0];
        endB = path.points[1];
        double pdx = endB.x - endA.x, pdy = endB.y - endA.y;
        double pl = std::hypot(pdx, pdy);
        if (pl < geometry::kZeroEps) pl = 1.0;
        outwardB = { pdx / pl, pdy / pl };
        outwardA = { -outwardB.x, -outwardB.y };
    } else {
        const Point2D c = path.points[0];
        const geometry::Arc arc = path.toArc();
        const double a0 = hobbycad::degreesToRadians(path.startAngle);
        const double a1 = hobbycad::degreesToRadians(path.startAngle + path.sweepAngle);
        endA = arc.startPoint();
        endB = arc.endPoint();
        // Forward tangent along the arc; outward is backward at the start and
        // forward at the end.
        const double sgn = (path.sweepAngle >= 0.0) ? 1.0 : -1.0;
        outwardA = { sgn * std::sin(a0), -sgn * std::cos(a0) };   // -tangent(a0)
        outwardB = { -sgn * std::sin(a1),  sgn * std::cos(a1) };  // +tangent(a1)
    }

    std::vector<int> halfIds;   // every half-cap, for the Equal chain

    if (ends == SweepEndStyle::Flat) {
        auto half = [&](const Point2D& inner, const Point2D& outer) {
            Entity e;
            e.id = nextEntityId();
            e.type = EntityType::Line;
            e.points = { inner, outer };   // point 0 is always the path end
            return e;
        };
        Entity a0 = half(endA, capA1);
        Entity a1 = half(endA, capA0);
        Entity b0 = half(endB, capB0);
        Entity b1 = half(endB, capB1);
        out.entities = { sides[0], sides[1], a0, a1, b0, b1 };
        halfIds = { a0.id, a1.id, b0.id, b1.id };

        // Corners, then the shared inner point at each end.
        out.constraints.push_back(coincident(nextConstraintId(), a0.id, 1, s0, sideStart));
        out.constraints.push_back(coincident(nextConstraintId(), a1.id, 1, s1, s1Start));
        out.constraints.push_back(coincident(nextConstraintId(), b0.id, 1, s0, sideEnd));
        out.constraints.push_back(coincident(nextConstraintId(), b1.id, 1, s1, s1End));
        out.constraints.push_back(coincident(nextConstraintId(), a0.id, 0, a1.id, 0));
        out.constraints.push_back(coincident(nextConstraintId(), b0.id, 0, b1.id, 0));

    } else {
        // A round end gets the SAME two half-segments, as construction,
        // plus the arc that rides on them. One rule for both styles.
        // Aaron: "equal ends should be the case for slot constraints
        // throughout. simpler code path."
        //
        // Without them there is nothing on a round end whose LENGTH is the
        // half-width: an arc cap's radius is the right quantity but the
        // solver reports Equal between two cap arcs as redundant given the
        // corner coincidences, so it removes no freedom and tips the
        // sketch over-constrained. The half-segments carry it instead, and
        // the arc simply spans them.
        auto cap = [&](const Point2D& center, const Point2D& from,
                       const Point2D& to, const Point2D& outward) {
            Entity e;
            e.id = nextEntityId();
            e.type = EntityType::Arc;
            const double sA = std::atan2(from.y - center.y, from.x - center.x);
            // from and to are diametrically opposite, so both semicircles are
            // 180 degrees and differ only in which way they bulge. Take the
            // one whose midpoint lies on the OUTWARD side, so the cap rounds
            // away from the body rather than into it.
            auto midOutward = [&](double sweepDeg) {
                const double m = sA + hobbycad::degreesToRadians(sweepDeg) / 2.0;
                return std::cos(m) * outward.x + std::sin(m) * outward.y;
            };
            const double sweep = (midOutward(180.0) >= midOutward(-180.0))
                                     ? 180.0 : -180.0;
            e.radius = halfWidth;
            e.startAngle = hobbycad::radiansToDegrees(sA);
            e.sweepAngle = sweep;
            e.points = { center, from, to };
            return e;
        };
        auto half = [&](const Point2D& inner, const Point2D& outer) {
            Entity e;
            e.id = nextEntityId();
            e.type = EntityType::Line;
            e.points = { inner, outer };
            e.isConstruction = true;   // a guide, not an edge of the profile
            return e;
        };
        Entity ha0 = half(endA, capA1);
        Entity ha1 = half(endA, capA0);
        Entity hb0 = half(endB, capB0);
        Entity hb1 = half(endB, capB1);

        Entity ca = cap(endA, capA0, capA1, outwardA);
        Entity cb = cap(endB, capB0, capB1, outwardB);
        out.entities = { sides[0], sides[1], ca, cb, ha0, ha1, hb0, hb1 };
        halfIds = { ha0.id, ha1.id, hb0.id, hb1.id };

        // The half-segments meet the sides and share the path's end, the
        // same way they do for a flat end.
        out.constraints.push_back(coincident(nextConstraintId(), ha0.id, 1, s0, sideStart));
        out.constraints.push_back(coincident(nextConstraintId(), ha1.id, 1, s1, s1Start));
        out.constraints.push_back(coincident(nextConstraintId(), hb0.id, 1, s0, sideEnd));
        out.constraints.push_back(coincident(nextConstraintId(), hb1.id, 1, s1, s1End));
        out.constraints.push_back(coincident(nextConstraintId(), ha0.id, 0, ha1.id, 0));
        out.constraints.push_back(coincident(nextConstraintId(), hb0.id, 0, hb1.id, 0));

        // The arc's own ends ride on the corners the half-segments already
        // fixed, so it needs tying to them and nothing more.
        out.constraints.push_back(coincident(nextConstraintId(), ca.id, 1, s1, s1Start));
        out.constraints.push_back(coincident(nextConstraintId(), ca.id, 2, s0, sideStart));
        out.constraints.push_back(coincident(nextConstraintId(), cb.id, 1, s0, sideEnd));
        out.constraints.push_back(coincident(nextConstraintId(), cb.id, 2, s1, s1End));
    }

    // ONE width, said once, however many ends there are. Chained rather
    // than all-to-first so it reads the same for two ends or twenty.
    // Aaron: "I still think equal ends should be the case for slot
    // constraints throughout. simpler code path."
    for (size_t i = 0; i + 1 < halfIds.size(); ++i) {
        out.constraints.push_back(pair(nextConstraintId(),
                                       ConstraintType::Equal,
                                       halfIds[i], halfIds[i + 1]));
    }

    // Tie the path to the profile so it stays the CENTERLINE and travels with
    // the sweep instead of detaching (Aaron). Each free end's junction (a
    // half-segment's shared inner point, sitting exactly on the path end) is
    // coincident to the path's own endpoint. A line's ends are points 0 and 1;
    // an arc's are points 1 and 2 (point 0 is the center). halfIds[0] was made
    // at endA, halfIds[2] at endB. This is the one tie between path and profile;
    // everything else stays as-drawn, so the sweep is under-constrained by
    // design and the caller/user adds dimensions as desired.
    if (halfIds.size() >= 3) {
        const int endIdxA = (path.type == EntityType::Line) ? 0 : 1;
        const int endIdxB = (path.type == EntityType::Line) ? 1 : 2;
        out.constraints.push_back(
            coincident(nextConstraintId(), path.id, endIdxA, halfIds[0], 0));
        out.constraints.push_back(
            coincident(nextConstraintId(), path.id, endIdxB, halfIds[2], 0));
    }

    // Round caps come in a matched pair: keep both ends the SAME size with a
    // single EQUAL_RADIUS on the two arcs (Aaron: "both arc ends should be
    // equal"). Clean and robust: no touch-point helpers, no over-constraint.
    // It does not by itself pin the ABSOLUTE radius (both caps can still resize
    // together, one under-constrained DOF that libslvs cannot pin cleanly here:
    // arc-vs-line Equal is unsupported and any 2-equation pin on an arc center
    // over-constrains), but it stops one end ballooning independently of the
    // other. Flat ends have no arcs, so this simply does nothing there.
    {
        std::vector<int> arcIds;    // the two round caps
        std::vector<int> sideIds;   // the two profile sides (offset lines)
        for (const auto& e : out.entities) {
            if (e.type == EntityType::Arc) arcIds.push_back(e.id);
            else if (e.type == EntityType::Line && !e.isConstruction) sideIds.push_back(e.id);
        }
        if (arcIds.size() == 2)
            out.constraints.push_back(pair(nextConstraintId(),
                                           ConstraintType::Equal,
                                           arcIds[0], arcIds[1]));

        // Straight (line-path) slot only: hold the two sides Parallel so the
        // slot cannot skew. (Aaron) Arc-path sweeps have concentric-arc sides,
        // not parallel lines, so they are left as-is. No cap Tangent: it
        // couples the radius to the offset but Aaron prefers the group stay
        // freely resizeable; the caps are kept equal to each other by the
        // Equal above.
        if (path.type == EntityType::Line && sideIds.size() == 2) {
            out.constraints.push_back(pair(nextConstraintId(),
                                           ConstraintType::Parallel,
                                           sideIds[0], sideIds[1]));
            out.constraints.push_back(pair(nextConstraintId(),
                                           ConstraintType::Equal,
                                           sideIds[0], sideIds[1]));
        }
    }

    out.group.id = groupId;
    out.group.name = sweepName(existingGroups);
    for (const auto& e : out.entities) out.group.entityIds.push_back(e.id);
    for (const auto& c : out.constraints) out.group.constraintIds.push_back(c.id);
    out.group.locked = false;

    out.success = true;
    return out;
}

SolveResult solveReportWithDecomposedCompounds(const std::vector<Entity>& entities,
                                               const std::vector<Constraint>& constraints,
                                               const SolveResult& solved)
{
    std::vector<Entity> de;
    std::vector<Constraint> dc;
    int nextE = nextFreeEntityId(entities);
    int nextC = nextFreeConstraintId(constraints);
    bool anyDecomposed = false;
    for (const Entity& e : entities) {
        const bool compound = e.type == EntityType::Rectangle ||
                              e.type == EntityType::Parallelogram ||
                              e.type == EntityType::Polygon;
        if (!compound) { de.push_back(e); continue; }
        const bool isFreeform = (e.type == EntityType::Polygon && e.radius < 0.001);
        const DecompositionResult d = decomposeEntity(
            e, {}, [&nextE] { return nextE++; }, [&nextC] { return nextC++; },
            0, {}, "solve", isFreeform);
        if (!d.success) { de.push_back(e); continue; }   // count it as-is
        anyDecomposed = true;
        for (const Entity& le : d.entities) de.push_back(le);
        for (const Constraint& lc : d.constraints) dc.push_back(lc);
    }
    if (!anyDecomposed) return solved;
    for (const Constraint& c : constraints) dc.push_back(c);
    Solver dofSolver;
    return dofSolver.solve(de, dc);
}

}  // namespace sketch
}  // namespace hobbycad
