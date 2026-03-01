// =====================================================================
//  src/libhobbycad/sketch/constraint.cpp — Sketch constraint implementation
// =====================================================================
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include <hobbycad/sketch/constraint.h>
#include <hobbycad/sketch/entity.h>
#include <hobbycad/geometry/utils.h>

#include <cmath>
#include <algorithm>
#include <unordered_set>

namespace hobbycad {
namespace sketch {

using namespace geometry;

// =====================================================================
//  Constraint Query Functions
// =====================================================================

bool isDimensionalConstraint(ConstraintType type)
{
    switch (type) {
    case ConstraintType::Distance:
    case ConstraintType::Radius:
    case ConstraintType::Diameter:
    case ConstraintType::Angle:
    case ConstraintType::FixedAngle:
        return true;
    default:
        return false;
    }
}

bool isGeometricConstraint(ConstraintType type)
{
    return !isDimensionalConstraint(type);
}

int requiredEntityCount(ConstraintType type)
{
    switch (type) {
    // Single entity constraints
    case ConstraintType::Horizontal:
    case ConstraintType::Vertical:
    case ConstraintType::Radius:
    case ConstraintType::Diameter:
    case ConstraintType::FixedPoint:
    case ConstraintType::FixedAngle:
        return 1;

    // Two entity constraints
    case ConstraintType::Distance:
    case ConstraintType::Angle:
    case ConstraintType::Parallel:
    case ConstraintType::Perpendicular:
    case ConstraintType::Coincident:
    case ConstraintType::Tangent:
    case ConstraintType::Equal:
    case ConstraintType::Concentric:
    case ConstraintType::Collinear:
    case ConstraintType::PointOnLine:
    case ConstraintType::PointOnCircle:
        return 2;

    // Three entity constraints
    case ConstraintType::Midpoint:
    case ConstraintType::Symmetric:
        return 3;

    default:
        return 2;
    }
}

const char* constraintTypeName(ConstraintType type)
{
    switch (type) {
    case ConstraintType::Distance:      return "Distance";
    case ConstraintType::Radius:        return "Radius";
    case ConstraintType::Diameter:      return "Diameter";
    case ConstraintType::Angle:         return "Angle";
    case ConstraintType::Horizontal:    return "Horizontal";
    case ConstraintType::Vertical:      return "Vertical";
    case ConstraintType::Parallel:      return "Parallel";
    case ConstraintType::Perpendicular: return "Perpendicular";
    case ConstraintType::Coincident:    return "Coincident";
    case ConstraintType::Tangent:       return "Tangent";
    case ConstraintType::Equal:         return "Equal";
    case ConstraintType::Midpoint:      return "Midpoint";
    case ConstraintType::Symmetric:     return "Symmetric";
    case ConstraintType::Concentric:    return "Concentric";
    case ConstraintType::Collinear:     return "Collinear";
    case ConstraintType::PointOnLine:   return "Point On Line";
    case ConstraintType::PointOnCircle: return "Point On Circle";
    case ConstraintType::FixedPoint:    return "Fixed Point";
    case ConstraintType::FixedAngle:    return "Fixed Angle";
    default:                            return "Unknown";
    }
}

const char* constraintUnit(ConstraintType type)
{
    switch (type) {
    case ConstraintType::Distance:
    case ConstraintType::Radius:
    case ConstraintType::Diameter:
        return "mm";
    case ConstraintType::Angle:
    case ConstraintType::FixedAngle:
        return "\xC2\xB0";
    default:
        return "";
    }
}

// =====================================================================
//  Constraint Detection
// =====================================================================

std::vector<ConstraintType> suggestConstraints(const Entity& e1, const Entity& e2)
{
    std::vector<ConstraintType> suggestions;

    // Line-Line constraints
    if (e1.type == EntityType::Line && e2.type == EntityType::Line) {
        suggestions.push_back(ConstraintType::Parallel);
        suggestions.push_back(ConstraintType::Perpendicular);
        suggestions.push_back(ConstraintType::Equal);
        suggestions.push_back(ConstraintType::Collinear);
        suggestions.push_back(ConstraintType::Angle);

        // Check if they share an endpoint
        if (entitiesConnected(e1, e2)) {
            suggestions.push_back(ConstraintType::Coincident);
        }
    }
    // Point-Line constraints
    else if ((e1.type == EntityType::Point && e2.type == EntityType::Line) ||
             (e1.type == EntityType::Line && e2.type == EntityType::Point)) {
        suggestions.push_back(ConstraintType::PointOnLine);
        suggestions.push_back(ConstraintType::Distance);
        suggestions.push_back(ConstraintType::Midpoint);
    }
    // Point-Point constraints
    else if (e1.type == EntityType::Point && e2.type == EntityType::Point) {
        suggestions.push_back(ConstraintType::Coincident);
        suggestions.push_back(ConstraintType::Distance);
    }
    // Circle/Arc constraints
    else if ((e1.type == EntityType::Circle || e1.type == EntityType::Arc) &&
             (e2.type == EntityType::Circle || e2.type == EntityType::Arc)) {
        suggestions.push_back(ConstraintType::Concentric);
        suggestions.push_back(ConstraintType::Equal);
        suggestions.push_back(ConstraintType::Tangent);
    }
    // Line-Circle constraints
    else if ((e1.type == EntityType::Line &&
              (e2.type == EntityType::Circle || e2.type == EntityType::Arc)) ||
             ((e1.type == EntityType::Circle || e1.type == EntityType::Arc) &&
              e2.type == EntityType::Line)) {
        suggestions.push_back(ConstraintType::Tangent);
        suggestions.push_back(ConstraintType::Distance);
    }
    // Point-Circle constraints
    else if ((e1.type == EntityType::Point &&
              (e2.type == EntityType::Circle || e2.type == EntityType::Arc)) ||
             ((e1.type == EntityType::Circle || e1.type == EntityType::Arc) &&
              e2.type == EntityType::Point)) {
        suggestions.push_back(ConstraintType::PointOnCircle);
        suggestions.push_back(ConstraintType::Coincident);  // Coincident with center
    }

    // Always suggest distance as fallback
    if (!hobbycad::contains(suggestions, ConstraintType::Distance)) {
        suggestions.push_back(ConstraintType::Distance);
    }

    return suggestions;
}

std::vector<ConstraintType> suggestConstraints(const Entity& entity)
{
    std::vector<ConstraintType> suggestions;

    switch (entity.type) {
    case EntityType::Line:
        suggestions.push_back(ConstraintType::Horizontal);
        suggestions.push_back(ConstraintType::Vertical);
        suggestions.push_back(ConstraintType::FixedAngle);
        suggestions.push_back(ConstraintType::Distance);  // Length
        break;

    case EntityType::Circle:
    case EntityType::Arc:
        suggestions.push_back(ConstraintType::Radius);
        suggestions.push_back(ConstraintType::Diameter);
        break;

    case EntityType::Point:
        suggestions.push_back(ConstraintType::FixedPoint);
        break;

    default:
        break;
    }

    return suggestions;
}

double calculateConstraintValue(
    ConstraintType type,
    const std::vector<const Entity*>& entities,
    const std::vector<int>& pointIndices)
{
    if (entities.empty()) return 0.0;

    switch (type) {
    case ConstraintType::Distance:
        if (entities.size() >= 2) {
            // Distance between two entities
            const Entity* e1 = entities[0];
            const Entity* e2 = entities[1];

            // Point to point
            if (e1->type == EntityType::Point && e2->type == EntityType::Point) {
                if (!e1->points.empty() && !e2->points.empty()) {
                    return lineLength(e1->points[0], e2->points[0]);
                }
            }
            // Line length (when both points are on same line)
            if (e1->type == EntityType::Line && e1->points.size() >= 2) {
                return lineLength(e1->points[0], e1->points[1]);
            }
        } else if (entities.size() == 1) {
            // Single entity - measure its length
            const Entity* e = entities[0];
            if (e->type == EntityType::Line && e->points.size() >= 2) {
                return lineLength(e->points[0], e->points[1]);
            }
        }
        break;

    case ConstraintType::Radius:
        if (!entities.empty()) {
            const Entity* e = entities[0];
            if (e->type == EntityType::Circle || e->type == EntityType::Arc) {
                return e->radius;
            }
        }
        break;

    case ConstraintType::Diameter:
        if (!entities.empty()) {
            const Entity* e = entities[0];
            if (e->type == EntityType::Circle || e->type == EntityType::Arc) {
                return e->radius * 2.0;
            }
        }
        break;

    case ConstraintType::Angle:
        if (entities.size() >= 2) {
            const Entity* e1 = entities[0];
            const Entity* e2 = entities[1];

            if (e1->type == EntityType::Line && e2->type == EntityType::Line) {
                if (e1->points.size() >= 2 && e2->points.size() >= 2) {
                    Point2D d1 = e1->points[1] - e1->points[0];
                    Point2D d2 = e2->points[1] - e2->points[0];
                    return angleBetween(d1, d2);
                }
            }
        }
        break;

    case ConstraintType::FixedAngle:
        if (!entities.empty()) {
            const Entity* e = entities[0];
            if (e->type == EntityType::Line && e->points.size() >= 2) {
                return getEntityAngle(*e);  // returns [0, 360)
            }
        }
        break;

    default:
        break;
    }

    return 0.0;
}

ConstraintType suggestConstraintType(const Entity& e1, const Entity& e2)
{
    // Point-Point: Distance
    if (e1.type == EntityType::Point && e2.type == EntityType::Point) {
        return ConstraintType::Distance;
    }

    // Point-Line: Distance (or PointOnLine)
    if ((e1.type == EntityType::Point && e2.type == EntityType::Line) ||
        (e1.type == EntityType::Line && e2.type == EntityType::Point)) {
        return ConstraintType::Distance;
    }

    // Line-Line: Angle (most common use case)
    if (e1.type == EntityType::Line && e2.type == EntityType::Line) {
        return ConstraintType::Angle;
    }

    // Circle/Arc: Radius for single, Concentric for pair
    if ((e1.type == EntityType::Circle || e1.type == EntityType::Arc) &&
        (e2.type == EntityType::Circle || e2.type == EntityType::Arc)) {
        return ConstraintType::Concentric;
    }

    // Line-Circle: Tangent
    if ((e1.type == EntityType::Line && (e2.type == EntityType::Circle || e2.type == EntityType::Arc)) ||
        ((e1.type == EntityType::Circle || e1.type == EntityType::Arc) && e2.type == EntityType::Line)) {
        return ConstraintType::Tangent;
    }

    // Default: Distance
    return ConstraintType::Distance;
}

const Entity* findEntityById(const std::vector<Entity>& entities, int id)
{
    for (const Entity& e : entities) {
        if (e.id == id) {
            return &e;
        }
    }
    return nullptr;
}

// ---- Helper: resolve a point index on an entity ----
// For Rectangle entities, indices 2 and 3 are virtual corners:
//   0 = points[0] = (x1,y1)   1 = points[1] = (x2,y2)
//   2 = (x1,y2)               3 = (x2,y1)
static Point2D resolveEntityPoint(const Entity* e, int idx)
{
    if (e->type == EntityType::Rectangle && e->points.size() >= 2) {
        switch (idx) {
        case 0: return e->points[0];
        case 1: return e->points[1];
        case 2: return Point2D(e->points[0].x, e->points[1].y);
        case 3: return Point2D(e->points[1].x, e->points[0].y);
        default: break;
        }
    }
    if (idx >= 0 && idx < static_cast<int>(e->points.size()))
        return e->points[idx];
    return e->points.empty() ? Point2D() : e->points[std::min(idx, static_cast<int>(e->points.size()) - 1)];
}

// ---- Helper: resolve a Distance endpoint for one entity ----
static Point2D resolveDistancePoint(const Entity* e, int pointIndex)
{
    if (e->type == EntityType::Point) {
        return e->points.empty() ? Point2D() : e->points[0];
    }
    if (e->type == EntityType::Circle || e->type == EntityType::Arc) {
        return e->points.empty() ? Point2D() : e->points[0];  // center
    }
    return resolveEntityPoint(e, pointIndex);
}

bool getConstraintEndpoints(
    const Constraint& constraint,
    EntityFinder findEntity,
    Point2D& p1, Point2D& p2)
{
    if (constraint.entityIds.empty()) {
        return false;
    }

    const Entity* e1 = findEntity(constraint.entityIds[0]);
    if (!e1) {
        return false;
    }

    switch (constraint.type) {
    case ConstraintType::Distance:
        if (constraint.entityIds.size() >= 2) {
            const Entity* e2 = findEntity(constraint.entityIds[1]);
            if (!e2) return false;

            int idx1 = (constraint.pointIndices.size() > 0) ? constraint.pointIndices[0] : 0;
            int idx2 = (constraint.pointIndices.size() > 1) ? constraint.pointIndices[1] : 0;
            p1 = resolveDistancePoint(e1, idx1);
            p2 = resolveDistancePoint(e2, idx2);
            return true;
        } else {
            // Single line: endpoints
            if (e1->type == EntityType::Line && e1->points.size() >= 2) {
                p1 = e1->points[0];
                p2 = e1->points[1];
                return true;
            }
        }
        break;

    case ConstraintType::Radius:
    case ConstraintType::Diameter:
        if (e1->type == EntityType::Circle || e1->type == EntityType::Arc) {
            if (e1->points.empty()) return false;
            p1 = e1->points[0];  // Center
            // Point on circle at 0 degrees
            p2 = p1 + Point2D(e1->radius, 0);
            return true;
        }
        break;

    case ConstraintType::Angle:
        if (constraint.entityIds.size() >= 2) {
            const Entity* e2 = findEntity(constraint.entityIds[1]);
            if (!e2) return false;

            if (e1->type == EntityType::Line && e2->type == EntityType::Line) {
                if (e1->points.size() < 2 || e2->points.size() < 2) return false;
                if (constraint.hasAnchorPoint()) {
                    // Use the explicit anchor vertex as p1, midpoint of far
                    // edges as p2 (for hit-testing / bounding box purposes)
                    p1 = constraint.anchorPoint;
                    p2 = ((e1->points[0] + e1->points[1]) / 2.0 +
                          (e2->points[0] + e2->points[1]) / 2.0) / 2.0;
                } else {
                    // Legacy: use midpoints of lines
                    p1 = (e1->points[0] + e1->points[1]) / 2.0;
                    p2 = (e2->points[0] + e2->points[1]) / 2.0;
                }
                return true;
            }
        }
        break;

    case ConstraintType::Horizontal:
    case ConstraintType::Vertical:
    case ConstraintType::FixedAngle:
        if (e1->type == EntityType::Line && e1->points.size() >= 2) {
            p1 = e1->points[0];
            p2 = e1->points[1];
            return true;
        }
        break;

    case ConstraintType::Coincident:
    case ConstraintType::Concentric:
        if (constraint.entityIds.size() >= 2) {
            const Entity* e2 = findEntity(constraint.entityIds[1]);
            if (!e2) return false;

            if (!e1->points.empty() && !e2->points.empty()) {
                p1 = e1->points[0];
                p2 = e2->points[0];
                return true;
            }
        }
        break;

    case ConstraintType::Tangent:
    case ConstraintType::Parallel:
    case ConstraintType::Perpendicular:
    case ConstraintType::Equal:
    case ConstraintType::Collinear:
        if (constraint.entityIds.size() >= 2) {
            const Entity* e2 = findEntity(constraint.entityIds[1]);
            if (!e2) return false;

            // Use centroids of entities
            if (!e1->points.empty() && !e2->points.empty()) {
                Point2D c1(0, 0), c2(0, 0);
                for (const Point2D& p : e1->points) c1 = c1 + p;
                for (const Point2D& p : e2->points) c2 = c2 + p;
                p1 = c1 / static_cast<double>(e1->points.size());
                p2 = c2 / static_cast<double>(e2->points.size());
                return true;
            }
        }
        break;

    default:
        break;
    }

    return false;
}

bool getConstraintEndpoints(
    const Constraint& constraint,
    const std::vector<Entity>& entities,
    Point2D& p1, Point2D& p2)
{
    return getConstraintEndpoints(
        constraint,
        [&entities](int id) -> const Entity* { return findEntityById(entities, id); },
        p1, p2);
}

// =====================================================================
//  Constraint Utility Functions
// =====================================================================

std::unordered_set<int> getConstrainedEntityIds(const std::vector<Constraint>& constraints)
{
    std::unordered_set<int> ids;
    for (const Constraint& c : constraints) {
        if (c.enabled && c.isDriving) {
            for (int eid : c.entityIds) {
                ids.insert(eid);
            }
        }
    }
    return ids;
}

double computeDrivenValue(const Constraint& constraint,
                          EntityFinder findEntity)
{
    switch (constraint.type) {
    case ConstraintType::Distance: {
        Point2D p1, p2;
        if (getConstraintEndpoints(constraint, findEntity, p1, p2)) {
            return std::hypot(p2.x - p1.x, p2.y - p1.y);
        }
        break;
    }
    case ConstraintType::Radius: {
        if (!constraint.entityIds.empty()) {
            const Entity* entity = findEntity(constraint.entityIds[0]);
            if (entity && (entity->type == EntityType::Circle ||
                           entity->type == EntityType::Arc)) {
                return entity->radius;
            }
        }
        break;
    }
    case ConstraintType::Diameter: {
        if (!constraint.entityIds.empty()) {
            const Entity* entity = findEntity(constraint.entityIds[0]);
            if (entity && (entity->type == EntityType::Circle ||
                           entity->type == EntityType::Arc)) {
                return entity->radius * 2.0;
            }
        }
        break;
    }
    case ConstraintType::Angle: {
        if (constraint.entityIds.size() >= 2) {
            const Entity* e1 = findEntity(constraint.entityIds[0]);
            const Entity* e2 = findEntity(constraint.entityIds[1]);
            if (e1 && e2 && e1->type == EntityType::Line &&
                e2->type == EntityType::Line &&
                e1->points.size() >= 2 && e2->points.size() >= 2) {
                Point2D d1 = e1->points[1] - e1->points[0];
                Point2D d2 = e2->points[1] - e2->points[0];
                return angleBetween(d1, d2);
            }
        }
        break;
    }
    case ConstraintType::FixedAngle: {
        if (!constraint.entityIds.empty()) {
            const Entity* e = findEntity(constraint.entityIds[0]);
            if (e && e->type == EntityType::Line && e->points.size() >= 2) {
                return getEntityAngle(*e);  // returns [0, 360)
            }
        }
        break;
    }
    default:
        break;
    }
    return constraint.value;  // Return existing value if no computation possible
}

double computeDrivenValue(const Constraint& constraint,
                          const std::vector<Entity>& entities)
{
    return computeDrivenValue(
        constraint,
        [&entities](int id) -> const Entity* { return findEntityById(entities, id); });
}

}  // namespace sketch
}  // namespace hobbycad
