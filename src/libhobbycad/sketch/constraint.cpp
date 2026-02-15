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
        return "°";
    default:
        return "";
    }
}

// =====================================================================
//  Constraint Detection
// =====================================================================

QVector<ConstraintType> suggestConstraints(const Entity& e1, const Entity& e2)
{
    QVector<ConstraintType> suggestions;

    // Line-Line constraints
    if (e1.type == EntityType::Line && e2.type == EntityType::Line) {
        suggestions.append(ConstraintType::Parallel);
        suggestions.append(ConstraintType::Perpendicular);
        suggestions.append(ConstraintType::Equal);
        suggestions.append(ConstraintType::Collinear);
        suggestions.append(ConstraintType::Angle);

        // Check if they share an endpoint
        if (entitiesConnected(e1, e2)) {
            suggestions.append(ConstraintType::Coincident);
        }
    }
    // Point-Line constraints
    else if ((e1.type == EntityType::Point && e2.type == EntityType::Line) ||
             (e1.type == EntityType::Line && e2.type == EntityType::Point)) {
        suggestions.append(ConstraintType::PointOnLine);
        suggestions.append(ConstraintType::Distance);
        suggestions.append(ConstraintType::Midpoint);
    }
    // Point-Point constraints
    else if (e1.type == EntityType::Point && e2.type == EntityType::Point) {
        suggestions.append(ConstraintType::Coincident);
        suggestions.append(ConstraintType::Distance);
    }
    // Circle/Arc constraints
    else if ((e1.type == EntityType::Circle || e1.type == EntityType::Arc) &&
             (e2.type == EntityType::Circle || e2.type == EntityType::Arc)) {
        suggestions.append(ConstraintType::Concentric);
        suggestions.append(ConstraintType::Equal);
        suggestions.append(ConstraintType::Tangent);
    }
    // Line-Circle constraints
    else if ((e1.type == EntityType::Line &&
              (e2.type == EntityType::Circle || e2.type == EntityType::Arc)) ||
             ((e1.type == EntityType::Circle || e1.type == EntityType::Arc) &&
              e2.type == EntityType::Line)) {
        suggestions.append(ConstraintType::Tangent);
        suggestions.append(ConstraintType::Distance);
    }
    // Point-Circle constraints
    else if ((e1.type == EntityType::Point &&
              (e2.type == EntityType::Circle || e2.type == EntityType::Arc)) ||
             ((e1.type == EntityType::Circle || e1.type == EntityType::Arc) &&
              e2.type == EntityType::Point)) {
        suggestions.append(ConstraintType::PointOnCircle);
        suggestions.append(ConstraintType::Coincident);  // Coincident with center
    }

    // Always suggest distance as fallback
    if (!suggestions.contains(ConstraintType::Distance)) {
        suggestions.append(ConstraintType::Distance);
    }

    return suggestions;
}

QVector<ConstraintType> suggestConstraints(const Entity& entity)
{
    QVector<ConstraintType> suggestions;

    switch (entity.type) {
    case EntityType::Line:
        suggestions.append(ConstraintType::Horizontal);
        suggestions.append(ConstraintType::Vertical);
        suggestions.append(ConstraintType::FixedAngle);
        suggestions.append(ConstraintType::Distance);  // Length
        break;

    case EntityType::Circle:
    case EntityType::Arc:
        suggestions.append(ConstraintType::Radius);
        suggestions.append(ConstraintType::Diameter);
        break;

    case EntityType::Point:
        suggestions.append(ConstraintType::FixedPoint);
        break;

    default:
        break;
    }

    return suggestions;
}

double calculateConstraintValue(
    ConstraintType type,
    const QVector<Entity*>& entities,
    const QVector<int>& pointIndices)
{
    if (entities.isEmpty()) return 0.0;

    switch (type) {
    case ConstraintType::Distance:
        if (entities.size() >= 2) {
            // Distance between two entities
            const Entity* e1 = entities[0];
            const Entity* e2 = entities[1];

            // Point to point
            if (e1->type == EntityType::Point && e2->type == EntityType::Point) {
                if (!e1->points.isEmpty() && !e2->points.isEmpty()) {
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
        if (!entities.isEmpty()) {
            const Entity* e = entities[0];
            if (e->type == EntityType::Circle || e->type == EntityType::Arc) {
                return e->radius;
            }
        }
        break;

    case ConstraintType::Diameter:
        if (!entities.isEmpty()) {
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
                    QPointF d1 = e1->points[1] - e1->points[0];
                    QPointF d2 = e2->points[1] - e2->points[0];
                    return angleBetween(d1, d2);
                }
            }
        }
        break;

    case ConstraintType::FixedAngle:
        if (!entities.isEmpty()) {
            const Entity* e = entities[0];
            if (e->type == EntityType::Line && e->points.size() >= 2) {
                QPointF d = e->points[1] - e->points[0];
                return vectorAngle(d);
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

const Entity* findEntityById(const QVector<Entity>& entities, int id)
{
    for (const Entity& e : entities) {
        if (e.id == id) {
            return &e;
        }
    }
    return nullptr;
}

bool getConstraintEndpoints(
    const Constraint& constraint,
    const QVector<Entity>& entities,
    QPointF& p1, QPointF& p2)
{
    if (constraint.entityIds.isEmpty()) {
        return false;
    }

    const Entity* e1 = findEntityById(entities, constraint.entityIds[0]);
    if (!e1) {
        return false;
    }

    switch (constraint.type) {
    case ConstraintType::Distance:
        if (constraint.entityIds.size() >= 2) {
            const Entity* e2 = findEntityById(entities, constraint.entityIds[1]);
            if (!e2) return false;

            // Point-Point
            if (e1->type == EntityType::Point && e2->type == EntityType::Point) {
                if (e1->points.isEmpty() || e2->points.isEmpty()) return false;
                p1 = e1->points[0];
                p2 = e2->points[0];
                return true;
            }

            // Point indices within entities
            if (constraint.pointIndices.size() >= 2) {
                int idx1 = constraint.pointIndices[0];
                int idx2 = constraint.pointIndices[1];
                if (idx1 < e1->points.size() && idx2 < e2->points.size()) {
                    p1 = e1->points[idx1];
                    p2 = e2->points[idx2];
                    return true;
                }
            }

            // Fallback: first points
            if (!e1->points.isEmpty() && !e2->points.isEmpty()) {
                p1 = e1->points[0];
                p2 = e2->points[0];
                return true;
            }
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
            if (e1->points.isEmpty()) return false;
            p1 = e1->points[0];  // Center
            // Point on circle at 0 degrees
            p2 = p1 + QPointF(e1->radius, 0);
            return true;
        }
        break;

    case ConstraintType::Angle:
        if (constraint.entityIds.size() >= 2) {
            const Entity* e2 = findEntityById(entities, constraint.entityIds[1]);
            if (!e2) return false;

            if (e1->type == EntityType::Line && e2->type == EntityType::Line) {
                if (e1->points.size() < 2 || e2->points.size() < 2) return false;
                // Use midpoints of lines
                p1 = (e1->points[0] + e1->points[1]) / 2.0;
                p2 = (e2->points[0] + e2->points[1]) / 2.0;
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
            const Entity* e2 = findEntityById(entities, constraint.entityIds[1]);
            if (!e2) return false;

            if (!e1->points.isEmpty() && !e2->points.isEmpty()) {
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
            const Entity* e2 = findEntityById(entities, constraint.entityIds[1]);
            if (!e2) return false;

            // Use centroids of entities
            if (!e1->points.isEmpty() && !e2->points.isEmpty()) {
                QPointF c1(0, 0), c2(0, 0);
                for (const QPointF& p : e1->points) c1 += p;
                for (const QPointF& p : e2->points) c2 += p;
                p1 = c1 / e1->points.size();
                p2 = c2 / e2->points.size();
                return true;
            }
        }
        break;

    default:
        break;
    }

    return false;
}

}  // namespace sketch
}  // namespace hobbycad
