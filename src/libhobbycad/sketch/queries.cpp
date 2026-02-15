// =====================================================================
//  src/libhobbycad/sketch/queries.cpp — Sketch query implementation
// =====================================================================
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include <hobbycad/sketch/queries.h>
#include <hobbycad/sketch/profiles.h>
#include <hobbycad/sketch/solver.h>
#include <hobbycad/geometry/utils.h>
#include <hobbycad/geometry/intersections.h>

#include <QLineF>
#include <QSet>
#include <QMap>
#include <QtMath>
#include <algorithm>

namespace hobbycad {
namespace sketch {

using namespace geometry;

// =====================================================================
//  Helper Functions
// =====================================================================

namespace {

/// Calculate distance from point to entity
double distanceToEntity(const QPointF& point, const Entity& entity)
{
    switch (entity.type) {
    case EntityType::Point:
        if (!entity.points.isEmpty()) {
            return QLineF(point, entity.points[0]).length();
        }
        break;

    case EntityType::Line:
        if (entity.points.size() >= 2) {
            return pointToLineDistance(point, entity.points[0], entity.points[1]);
        }
        break;

    case EntityType::Circle:
        if (!entity.points.isEmpty()) {
            double distToCenter = QLineF(point, entity.points[0]).length();
            return qAbs(distToCenter - entity.radius);
        }
        break;

    case EntityType::Arc:
        if (!entity.points.isEmpty()) {
            return pointToArcDistance(point, Arc{
                entity.points[0], entity.radius, entity.startAngle, entity.sweepAngle
            });
        }
        break;

    case EntityType::Rectangle:
        if (entity.points.size() >= 2) {
            QPointF p1 = entity.points[0];
            QPointF p2 = entity.points[1];
            QPointF corners[4] = {
                p1,
                QPointF(p2.x(), p1.y()),
                p2,
                QPointF(p1.x(), p2.y())
            };
            double minDist = std::numeric_limits<double>::max();
            for (int i = 0; i < 4; ++i) {
                double d = pointToLineDistance(point, corners[i], corners[(i+1)%4]);
                minDist = qMin(minDist, d);
            }
            return minDist;
        }
        break;

    case EntityType::Polygon:
        if (entity.points.size() >= 2) {
            double minDist = std::numeric_limits<double>::max();
            for (int i = 0; i < entity.points.size(); ++i) {
                int j = (i + 1) % entity.points.size();
                double d = pointToLineDistance(point, entity.points[i], entity.points[j]);
                minDist = qMin(minDist, d);
            }
            return minDist;
        }
        break;

    case EntityType::Ellipse:
        if (!entity.points.isEmpty()) {
            // Approximate: use distance to closest point on ellipse
            // This is a simplified calculation
            QPointF center = entity.points[0];
            QPointF rel = point - center;
            double angle = qAtan2(rel.y(), rel.x());
            QPointF ellipsePoint(
                center.x() + entity.majorRadius * qCos(angle),
                center.y() + entity.minorRadius * qSin(angle)
            );
            return QLineF(point, ellipsePoint).length();
        }
        break;

    case EntityType::Slot:
        if (entity.points.size() >= 2) {
            // Distance to capsule shape
            double lineDist = pointToLineDistance(point, entity.points[0], entity.points[1]);
            return qMax(0.0, lineDist - entity.radius);
        }
        break;

    case EntityType::Spline:
        if (entity.points.size() >= 2) {
            // Approximate by checking control polygon
            double minDist = std::numeric_limits<double>::max();
            for (int i = 0; i < entity.points.size() - 1; ++i) {
                double d = pointToLineDistance(point, entity.points[i], entity.points[i+1]);
                minDist = qMin(minDist, d);
            }
            return minDist;
        }
        break;

    case EntityType::Text:
        // Text hit testing would use bounding box
        if (!entity.points.isEmpty()) {
            return QLineF(point, entity.points[0]).length();
        }
        break;
    }

    return std::numeric_limits<double>::max();
}

/// Get closest point on entity to a query point
QPointF closestPointOnEntity(const QPointF& point, const Entity& entity)
{
    switch (entity.type) {
    case EntityType::Point:
        if (!entity.points.isEmpty()) {
            return entity.points[0];
        }
        break;

    case EntityType::Line:
        if (entity.points.size() >= 2) {
            return closestPointOnLine(point, entity.points[0], entity.points[1]);
        }
        break;

    case EntityType::Circle:
        if (!entity.points.isEmpty()) {
            return closestPointOnCircle(point, entity.points[0], entity.radius);
        }
        break;

    case EntityType::Arc:
        if (!entity.points.isEmpty()) {
            return closestPointOnArc(point, Arc{
                entity.points[0], entity.radius, entity.startAngle, entity.sweepAngle
            });
        }
        break;

    default:
        // For complex entities, return nearest control point
        if (!entity.points.isEmpty()) {
            double minDist = std::numeric_limits<double>::max();
            QPointF nearest = entity.points[0];
            for (const QPointF& p : entity.points) {
                double d = QLineF(point, p).length();
                if (d < minDist) {
                    minDist = d;
                    nearest = p;
                }
            }
            return nearest;
        }
        break;
    }

    return point;
}

}  // anonymous namespace

// =====================================================================
//  Hit Testing
// =====================================================================

QVector<int> findEntitiesAtPoint(
    const QVector<Entity>& entities,
    const QPointF& point,
    double tolerance)
{
    QVector<QPair<int, double>> hits;

    for (const Entity& entity : entities) {
        double dist = distanceToEntity(point, entity);
        if (dist <= tolerance) {
            hits.append({entity.id, dist});
        }
    }

    // Sort by distance
    std::sort(hits.begin(), hits.end(),
              [](const QPair<int,double>& a, const QPair<int,double>& b) {
                  return a.second < b.second;
              });

    QVector<int> result;
    for (const auto& hit : hits) {
        result.append(hit.first);
    }
    return result;
}

HitTestResult findNearestEntity(
    const QVector<Entity>& entities,
    const QPointF& point)
{
    HitTestResult result;
    result.distance = std::numeric_limits<double>::max();

    for (const Entity& entity : entities) {
        double dist = distanceToEntity(point, entity);
        if (dist < result.distance) {
            result.entityId = entity.id;
            result.distance = dist;
            result.closestPoint = closestPointOnEntity(point, entity);

            // Check if we hit a control point
            result.pointIndex = -1;
            for (int i = 0; i < entity.points.size(); ++i) {
                if (QLineF(point, entity.points[i]).length() < POINT_TOLERANCE) {
                    result.pointIndex = i;
                    break;
                }
            }
        }
    }

    return result;
}

QVector<int> findEntitiesInRect(
    const QVector<Entity>& entities,
    const QRectF& rect,
    bool mustBeFullyInside)
{
    QVector<int> result;

    for (const Entity& entity : entities) {
        BoundingBox bounds = entity.boundingBox();

        if (mustBeFullyInside) {
            // Entity must be fully contained
            if (rect.contains(QPointF(bounds.minX, bounds.minY)) &&
                rect.contains(QPointF(bounds.maxX, bounds.maxY))) {
                result.append(entity.id);
            }
        } else {
            // Any intersection counts
            QRectF entityRect(bounds.minX, bounds.minY,
                              bounds.maxX - bounds.minX,
                              bounds.maxY - bounds.minY);
            if (rect.intersects(entityRect)) {
                result.append(entity.id);
            }
        }
    }

    return result;
}

QVector<QPair<int, int>> findControlPointsAtPoint(
    const QVector<Entity>& entities,
    const QPointF& point,
    double tolerance)
{
    QVector<QPair<int, int>> result;

    for (const Entity& entity : entities) {
        for (int i = 0; i < entity.points.size(); ++i) {
            if (QLineF(point, entity.points[i]).length() <= tolerance) {
                result.append({entity.id, i});
            }
        }
    }

    return result;
}

// =====================================================================
//  Sketch Validation
// =====================================================================

ValidationResult validateSketch(
    const QVector<Entity>& entities,
    const QVector<Constraint>& constraints)
{
    ValidationResult result;

    // Check for duplicate entity IDs
    QSet<int> entityIds;
    for (const Entity& e : entities) {
        if (entityIds.contains(e.id)) {
            result.errors.append(QString("Duplicate entity ID: %1").arg(e.id));
            result.valid = false;
        }
        entityIds.insert(e.id);
    }

    // Check for duplicate constraint IDs
    QSet<int> constraintIds;
    for (const Constraint& c : constraints) {
        if (constraintIds.contains(c.id)) {
            result.errors.append(QString("Duplicate constraint ID: %1").arg(c.id));
            result.valid = false;
        }
        constraintIds.insert(c.id);
    }

    // Check constraints reference valid entities
    for (const Constraint& c : constraints) {
        for (int entityId : c.entityIds) {
            if (!entityIds.contains(entityId)) {
                result.errors.append(QString("Constraint %1 references non-existent entity %2")
                                     .arg(c.id).arg(entityId));
                result.valid = false;
            }
        }
    }

    // Check for degenerate entities
    for (const Entity& e : entities) {
        switch (e.type) {
        case EntityType::Line:
            if (e.points.size() >= 2 &&
                QLineF(e.points[0], e.points[1]).length() < POINT_TOLERANCE) {
                result.warnings.append(QString("Line %1 has zero length").arg(e.id));
            }
            break;
        case EntityType::Circle:
        case EntityType::Arc:
            if (e.radius < POINT_TOLERANCE) {
                result.warnings.append(QString("Circle/Arc %1 has zero radius").arg(e.id));
            }
            break;
        default:
            break;
        }
    }

    return result;
}

bool isSketchFullyConstrained(
    const QVector<Entity>& entities,
    const QVector<Constraint>& constraints)
{
    if (!Solver::isAvailable()) {
        return false;
    }

    Solver solver;
    return solver.degreesOfFreedom(entities, constraints) == 0;
}

QVector<int> findUnconstrainedEntities(
    const QVector<Entity>& entities,
    const QVector<Constraint>& constraints)
{
    // Collect all entity IDs referenced by constraints
    QSet<int> constrainedIds;
    for (const Constraint& c : constraints) {
        for (int id : c.entityIds) {
            constrainedIds.insert(id);
        }
    }

    // Find entities not referenced by any constraint
    QVector<int> unconstrained;
    for (const Entity& e : entities) {
        if (!constrainedIds.contains(e.id)) {
            unconstrained.append(e.id);
        }
    }

    return unconstrained;
}

QVector<int> findUnderconstrainedEntities(
    const QVector<Entity>& entities,
    const QVector<Constraint>& constraints)
{
    // Heuristic: count constraints per entity and compare to expected DOF
    QMap<int, int> constraintCount;
    for (const Constraint& c : constraints) {
        for (int id : c.entityIds) {
            constraintCount[id]++;
        }
    }

    QVector<int> underconstrained;
    for (const Entity& e : entities) {
        int count = constraintCount.value(e.id, 0);
        int expectedDOF = 0;

        switch (e.type) {
        case EntityType::Point:
            expectedDOF = 2;  // x, y
            break;
        case EntityType::Line:
            expectedDOF = 4;  // x1, y1, x2, y2
            break;
        case EntityType::Circle:
            expectedDOF = 3;  // cx, cy, r
            break;
        case EntityType::Arc:
            expectedDOF = 5;  // cx, cy, r, start, sweep
            break;
        default:
            expectedDOF = 2 * e.points.size();
            break;
        }

        // This is a rough heuristic - real DOF analysis needs the solver
        if (count > 0 && count < expectedDOF / 2) {
            underconstrained.append(e.id);
        }
    }

    return underconstrained;
}

// =====================================================================
//  Sketch Analysis
// =====================================================================

double sketchArea(const QVector<Entity>& entities)
{
    ProfileDetectionOptions options;
    QVector<Profile> profiles = detectProfiles(entities, options);

    double totalArea = 0.0;
    for (const Profile& p : profiles) {
        totalArea += qAbs(p.area);
    }

    return totalArea;
}

double sketchLength(const QVector<Entity>& entities)
{
    double total = 0.0;
    for (const Entity& e : entities) {
        total += entityLength(e);
    }
    return total;
}

BoundingBox sketchBounds(const QVector<Entity>& entities)
{
    BoundingBox bounds;
    for (const Entity& e : entities) {
        bounds.include(e.boundingBox());
    }
    return bounds;
}

QMap<EntityType, int> countEntitiesByType(const QVector<Entity>& entities)
{
    QMap<EntityType, int> counts;
    for (const Entity& e : entities) {
        counts[e.type]++;
    }
    return counts;
}

// =====================================================================
//  Curve Utilities
// =====================================================================

double entityLength(const Entity& entity)
{
    switch (entity.type) {
    case EntityType::Point:
        return 0.0;

    case EntityType::Line:
        if (entity.points.size() >= 2) {
            return QLineF(entity.points[0], entity.points[1]).length();
        }
        break;

    case EntityType::Circle:
        return 2.0 * M_PI * entity.radius;

    case EntityType::Arc:
        return qAbs(qDegreesToRadians(entity.sweepAngle)) * entity.radius;

    case EntityType::Rectangle:
        if (entity.points.size() >= 2) {
            double w = qAbs(entity.points[1].x() - entity.points[0].x());
            double h = qAbs(entity.points[1].y() - entity.points[0].y());
            return 2.0 * (w + h);
        }
        break;

    case EntityType::Polygon:
        {
            double len = 0.0;
            for (int i = 0; i < entity.points.size(); ++i) {
                int j = (i + 1) % entity.points.size();
                len += QLineF(entity.points[i], entity.points[j]).length();
            }
            return len;
        }

    case EntityType::Ellipse:
        // Ramanujan approximation for ellipse circumference
        {
            double a = entity.majorRadius;
            double b = entity.minorRadius;
            double h = qPow((a - b) / (a + b), 2);
            return M_PI * (a + b) * (1.0 + 3.0 * h / (10.0 + qSqrt(4.0 - 3.0 * h)));
        }

    case EntityType::Slot:
        if (entity.points.size() >= 2) {
            double lineLen = QLineF(entity.points[0], entity.points[1]).length();
            return 2.0 * lineLen + 2.0 * M_PI * entity.radius;
        }
        break;

    case EntityType::Spline:
        // Approximate by summing control polygon segments
        {
            double len = 0.0;
            for (int i = 0; i < entity.points.size() - 1; ++i) {
                len += QLineF(entity.points[i], entity.points[i+1]).length();
            }
            return len;
        }

    case EntityType::Text:
        return 0.0;
    }

    return 0.0;
}

QPointF pointAtParameter(const Entity& entity, double t)
{
    t = qBound(0.0, t, 1.0);

    switch (entity.type) {
    case EntityType::Point:
        if (!entity.points.isEmpty()) {
            return entity.points[0];
        }
        break;

    case EntityType::Line:
        if (entity.points.size() >= 2) {
            return lerp(entity.points[0], entity.points[1], t);
        }
        break;

    case EntityType::Circle:
        if (!entity.points.isEmpty()) {
            double angle = 2.0 * M_PI * t;
            return entity.points[0] + QPointF(
                entity.radius * qCos(angle),
                entity.radius * qSin(angle)
            );
        }
        break;

    case EntityType::Arc:
        if (!entity.points.isEmpty()) {
            double startRad = qDegreesToRadians(entity.startAngle);
            double sweepRad = qDegreesToRadians(entity.sweepAngle);
            double angle = startRad + t * sweepRad;
            return entity.points[0] + QPointF(
                entity.radius * qCos(angle),
                entity.radius * qSin(angle)
            );
        }
        break;

    case EntityType::Rectangle:
        if (entity.points.size() >= 2) {
            // Traverse rectangle perimeter
            QPointF p1 = entity.points[0];
            QPointF p2 = entity.points[1];
            QPointF corners[4] = {
                p1,
                QPointF(p2.x(), p1.y()),
                p2,
                QPointF(p1.x(), p2.y())
            };
            double perimeter = 2.0 * (qAbs(p2.x() - p1.x()) + qAbs(p2.y() - p1.y()));
            double dist = t * perimeter;
            double accumulated = 0.0;
            for (int i = 0; i < 4; ++i) {
                int j = (i + 1) % 4;
                double segLen = QLineF(corners[i], corners[j]).length();
                if (accumulated + segLen >= dist) {
                    double segT = (dist - accumulated) / segLen;
                    return lerp(corners[i], corners[j], segT);
                }
                accumulated += segLen;
            }
            return corners[0];
        }
        break;

    case EntityType::Spline:
        // Simple linear interpolation along control polygon
        if (entity.points.size() >= 2) {
            double totalLen = 0.0;
            for (int i = 0; i < entity.points.size() - 1; ++i) {
                totalLen += QLineF(entity.points[i], entity.points[i+1]).length();
            }
            double targetLen = t * totalLen;
            double accumulated = 0.0;
            for (int i = 0; i < entity.points.size() - 1; ++i) {
                double segLen = QLineF(entity.points[i], entity.points[i+1]).length();
                if (accumulated + segLen >= targetLen) {
                    double segT = (targetLen - accumulated) / segLen;
                    return lerp(entity.points[i], entity.points[i+1], segT);
                }
                accumulated += segLen;
            }
            return entity.points.last();
        }
        break;

    default:
        if (!entity.points.isEmpty()) {
            return entity.points[0];
        }
        break;
    }

    return QPointF();
}

double parameterAtPoint(const Entity& entity, const QPointF& point)
{
    switch (entity.type) {
    case EntityType::Point:
        return 0.0;

    case EntityType::Line:
        if (entity.points.size() >= 2) {
            double t = projectPointOnLine(point, entity.points[0], entity.points[1]);
            return qBound(0.0, t, 1.0);
        }
        break;

    case EntityType::Circle:
        if (!entity.points.isEmpty()) {
            QPointF rel = point - entity.points[0];
            double angle = qAtan2(rel.y(), rel.x());
            if (angle < 0) angle += 2.0 * M_PI;
            return angle / (2.0 * M_PI);
        }
        break;

    case EntityType::Arc:
        if (!entity.points.isEmpty()) {
            QPointF rel = point - entity.points[0];
            double angle = qRadiansToDegrees(qAtan2(rel.y(), rel.x()));
            double startAngle = entity.startAngle;
            double sweepAngle = entity.sweepAngle;

            // Normalize angle relative to arc
            double relAngle = angle - startAngle;
            while (relAngle < 0) relAngle += 360.0;
            while (relAngle > 360) relAngle -= 360.0;

            if (sweepAngle < 0) {
                relAngle = 360.0 - relAngle;
                sweepAngle = -sweepAngle;
            }

            return qBound(0.0, relAngle / sweepAngle, 1.0);
        }
        break;

    default:
        break;
    }

    return -1.0;
}

QPointF tangentAtParameter(const Entity& entity, double t)
{
    t = qBound(0.0, t, 1.0);

    switch (entity.type) {
    case EntityType::Point:
        return QPointF(1, 0);  // Arbitrary

    case EntityType::Line:
        if (entity.points.size() >= 2) {
            return normalize(entity.points[1] - entity.points[0]);
        }
        break;

    case EntityType::Circle:
        if (!entity.points.isEmpty()) {
            double angle = 2.0 * M_PI * t;
            return QPointF(-qSin(angle), qCos(angle));
        }
        break;

    case EntityType::Arc:
        if (!entity.points.isEmpty()) {
            double startRad = qDegreesToRadians(entity.startAngle);
            double sweepRad = qDegreesToRadians(entity.sweepAngle);
            double angle = startRad + t * sweepRad;
            double sign = (sweepRad >= 0) ? 1.0 : -1.0;
            return QPointF(-sign * qSin(angle), sign * qCos(angle));
        }
        break;

    default:
        // Approximate by finite difference
        {
            double dt = 0.001;
            QPointF p1 = pointAtParameter(entity, qMax(0.0, t - dt));
            QPointF p2 = pointAtParameter(entity, qMin(1.0, t + dt));
            QPointF diff = p2 - p1;
            double len = length(diff);
            if (len > POINT_TOLERANCE) {
                return diff / len;
            }
        }
        break;
    }

    return QPointF(1, 0);
}

QPointF normalAtParameter(const Entity& entity, double t)
{
    QPointF tan = tangentAtParameter(entity, t);
    return perpendicular(tan);
}

// =====================================================================
//  Tessellation
// =====================================================================

QVector<QPointF> tessellate(const Entity& entity, double tolerance)
{
    QVector<QPointF> points;

    switch (entity.type) {
    case EntityType::Point:
        if (!entity.points.isEmpty()) {
            points.append(entity.points[0]);
        }
        break;

    case EntityType::Line:
        if (entity.points.size() >= 2) {
            points.append(entity.points[0]);
            points.append(entity.points[1]);
        }
        break;

    case EntityType::Circle:
    case EntityType::Arc:
        {
            double sweepRad = (entity.type == EntityType::Circle)
                              ? 2.0 * M_PI
                              : qDegreesToRadians(qAbs(entity.sweepAngle));
            // Number of segments based on tolerance
            int segments = qMax(8, static_cast<int>(
                qCeil(sweepRad * entity.radius / tolerance)
            ));
            for (int i = 0; i <= segments; ++i) {
                double t = static_cast<double>(i) / segments;
                points.append(pointAtParameter(entity, t));
            }
        }
        break;

    case EntityType::Rectangle:
        if (entity.points.size() >= 2) {
            QPointF p1 = entity.points[0];
            QPointF p2 = entity.points[1];
            points.append(p1);
            points.append(QPointF(p2.x(), p1.y()));
            points.append(p2);
            points.append(QPointF(p1.x(), p2.y()));
            points.append(p1);  // Close
        }
        break;

    case EntityType::Polygon:
        points = entity.points;
        if (!points.isEmpty() && points.first() != points.last()) {
            points.append(points.first());
        }
        break;

    case EntityType::Ellipse:
        {
            // Approximate ellipse circumference for segment count
            double a = entity.majorRadius;
            double b = entity.minorRadius;
            double approxCircum = M_PI * (a + b);
            int segments = qMax(16, static_cast<int>(qCeil(approxCircum / tolerance)));
            for (int i = 0; i <= segments; ++i) {
                double angle = 2.0 * M_PI * i / segments;
                if (!entity.points.isEmpty()) {
                    points.append(entity.points[0] + QPointF(
                        a * qCos(angle),
                        b * qSin(angle)
                    ));
                }
            }
        }
        break;

    case EntityType::Slot:
        if (entity.points.size() >= 2) {
            // Two semicircles connected by lines
            QPointF p1 = entity.points[0];
            QPointF p2 = entity.points[1];
            QPointF dir = normalize(p2 - p1);
            QPointF perp = perpendicular(dir);

            int arcSegments = qMax(8, static_cast<int>(
                qCeil(M_PI * entity.radius / tolerance)
            ));

            // First semicircle
            double baseAngle = qAtan2(perp.y(), perp.x());
            for (int i = 0; i <= arcSegments; ++i) {
                double angle = baseAngle + M_PI * i / arcSegments;
                points.append(p1 + entity.radius * QPointF(qCos(angle), qSin(angle)));
            }

            // Second semicircle
            for (int i = 0; i <= arcSegments; ++i) {
                double angle = baseAngle + M_PI + M_PI * i / arcSegments;
                points.append(p2 + entity.radius * QPointF(qCos(angle), qSin(angle)));
            }

            points.append(points.first());  // Close
        }
        break;

    case EntityType::Spline:
        // For now, just use control points
        // TODO: Proper spline evaluation
        points = entity.points;
        break;

    case EntityType::Text:
        // Text is not tessellated
        break;
    }

    return points;
}

QVector<QLineF> tessellateToLines(const QVector<Entity>& entities, double tolerance)
{
    QVector<QLineF> lines;

    for (const Entity& entity : entities) {
        QVector<QPointF> points = tessellate(entity, tolerance);
        for (int i = 0; i < points.size() - 1; ++i) {
            lines.append(QLineF(points[i], points[i+1]));
        }
    }

    return lines;
}

}  // namespace sketch
}  // namespace hobbycad
