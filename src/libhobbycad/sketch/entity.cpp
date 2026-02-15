// =====================================================================
//  src/libhobbycad/sketch/entity.cpp — Sketch entity implementation
// =====================================================================
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include <hobbycad/sketch/entity.h>
#include <hobbycad/geometry/intersections.h>
#include <hobbycad/geometry/utils.h>

namespace hobbycad {
namespace sketch {

using namespace geometry;

// =====================================================================
//  Entity Methods
// =====================================================================

BoundingBox Entity::boundingBox() const
{
    BoundingBox bbox;

    for (const QPointF& p : points) {
        bbox.include(p);
    }

    // Expand for circles and arcs
    if (type == EntityType::Circle && !points.isEmpty()) {
        bbox.include(QPointF(points[0].x() - radius, points[0].y() - radius));
        bbox.include(QPointF(points[0].x() + radius, points[0].y() + radius));
    } else if (type == EntityType::Arc && !points.isEmpty()) {
        // For arcs, include the center and endpoints
        bbox.include(QPointF(points[0].x() - radius, points[0].y() - radius));
        bbox.include(QPointF(points[0].x() + radius, points[0].y() + radius));
    } else if (type == EntityType::Polygon && !points.isEmpty()) {
        bbox.include(QPointF(points[0].x() - radius, points[0].y() - radius));
        bbox.include(QPointF(points[0].x() + radius, points[0].y() + radius));
    } else if (type == EntityType::Slot && points.size() >= 2) {
        // Include slot width
        for (const QPointF& p : points) {
            bbox.include(QPointF(p.x() - radius, p.y() - radius));
            bbox.include(QPointF(p.x() + radius, p.y() + radius));
        }
    } else if (type == EntityType::Ellipse && !points.isEmpty()) {
        bbox.include(QPointF(points[0].x() - majorRadius, points[0].y() - minorRadius));
        bbox.include(QPointF(points[0].x() + majorRadius, points[0].y() + minorRadius));
    }

    return bbox;
}

QVector<QPointF> Entity::endpoints() const
{
    QVector<QPointF> result;

    switch (type) {
    case EntityType::Line:
        if (points.size() >= 2) {
            result.append(points[0]);
            result.append(points[1]);
        }
        break;
    case EntityType::Arc:
        if (!points.isEmpty()) {
            Arc arc;
            arc.center = points[0];
            arc.radius = radius;
            arc.startAngle = startAngle;
            arc.sweepAngle = sweepAngle;
            result.append(arc.startPoint());
            result.append(arc.endPoint());
        }
        break;
    case EntityType::Spline:
        if (points.size() >= 2) {
            result.append(points.first());
            result.append(points.last());
        }
        break;
    case EntityType::Slot:
        if (points.size() >= 2) {
            result.append(points[0]);
            result.append(points[1]);
        }
        break;
    default:
        break;
    }

    return result;
}

bool Entity::containsPoint(const QPointF& point, double tolerance) const
{
    return distanceTo(point) < tolerance;
}

QPointF Entity::closestPoint(const QPointF& point) const
{
    switch (type) {
    case EntityType::Point:
        if (!points.isEmpty()) {
            return points[0];
        }
        break;

    case EntityType::Line:
        if (points.size() >= 2) {
            return closestPointOnLine(point, points[0], points[1]);
        }
        break;

    case EntityType::Circle:
        if (!points.isEmpty()) {
            return closestPointOnCircle(point, points[0], radius);
        }
        break;

    case EntityType::Arc:
        if (!points.isEmpty()) {
            Arc arc;
            arc.center = points[0];
            arc.radius = radius;
            arc.startAngle = startAngle;
            arc.sweepAngle = sweepAngle;
            return closestPointOnArc(point, arc);
        }
        break;

    case EntityType::Rectangle:
        if (points.size() >= 2) {
            // Find closest point on rectangle edges
            QPointF corners[4] = {
                points[0],
                QPointF(points[1].x(), points[0].y()),
                points[1],
                QPointF(points[0].x(), points[1].y())
            };
            QPointF closest = corners[0];
            double minDist = QLineF(point, corners[0]).length();

            for (int i = 0; i < 4; ++i) {
                int j = (i + 1) % 4;
                QPointF cp = closestPointOnLine(point, corners[i], corners[j]);
                double d = QLineF(point, cp).length();
                if (d < minDist) {
                    minDist = d;
                    closest = cp;
                }
            }
            return closest;
        }
        break;

    default:
        break;
    }

    return point;  // Fallback
}

double Entity::distanceTo(const QPointF& point) const
{
    switch (type) {
    case EntityType::Point:
        if (!points.isEmpty()) {
            return QLineF(point, points[0]).length();
        }
        break;

    case EntityType::Line:
        if (points.size() >= 2) {
            return pointToLineDistance(point, points[0], points[1]);
        }
        break;

    case EntityType::Circle:
        if (!points.isEmpty()) {
            return pointToCircleDistance(point, points[0], radius);
        }
        break;

    case EntityType::Arc:
        if (!points.isEmpty()) {
            Arc arc;
            arc.center = points[0];
            arc.radius = radius;
            arc.startAngle = startAngle;
            arc.sweepAngle = sweepAngle;
            return pointToArcDistance(point, arc);
        }
        break;

    case EntityType::Rectangle:
        if (points.size() >= 2) {
            QPointF cp = closestPoint(point);
            return QLineF(point, cp).length();
        }
        break;

    default:
        break;
    }

    return std::numeric_limits<double>::max();
}

void Entity::transform(const Transform2D& t)
{
    for (QPointF& p : points) {
        p = t.apply(p);
    }
    // Note: radius values are not scaled here - caller should handle scaling
}

Entity Entity::transformed(const Transform2D& t) const
{
    Entity result = *this;
    result.transform(t);
    return result;
}

Entity Entity::clone(int newId) const
{
    Entity result = *this;
    result.id = newId;
    return result;
}

// =====================================================================
//  Entity Factory Functions
// =====================================================================

Entity createPoint(int id, const QPointF& position)
{
    Entity e;
    e.id = id;
    e.type = EntityType::Point;
    e.points.append(position);
    return e;
}

Entity createLine(int id, const QPointF& start, const QPointF& end)
{
    Entity e;
    e.id = id;
    e.type = EntityType::Line;
    e.points.append(start);
    e.points.append(end);
    return e;
}

Entity createRectangle(int id, const QPointF& corner1, const QPointF& corner2)
{
    Entity e;
    e.id = id;
    e.type = EntityType::Rectangle;
    e.points.append(corner1);
    e.points.append(corner2);
    return e;
}

Entity createCircle(int id, const QPointF& center, double radius)
{
    Entity e;
    e.id = id;
    e.type = EntityType::Circle;
    e.points.append(center);
    e.radius = radius;
    return e;
}

Entity createArc(int id, const QPointF& center, double radius,
                 double startAngle, double sweepAngle)
{
    Entity e;
    e.id = id;
    e.type = EntityType::Arc;
    e.points.append(center);
    e.radius = radius;
    e.startAngle = startAngle;
    e.sweepAngle = sweepAngle;
    return e;
}

Entity createArcFromThreePoints(int id, const QPointF& start,
                                const QPointF& mid, const QPointF& end)
{
    auto arc = arcFromThreePoints(start, mid, end);
    if (arc) {
        return createArc(id, arc->center, arc->radius,
                         arc->startAngle, arc->sweepAngle);
    }
    // Fallback to line if collinear
    return createLine(id, start, end);
}

Entity createSpline(int id, const QVector<QPointF>& controlPoints)
{
    Entity e;
    e.id = id;
    e.type = EntityType::Spline;
    e.points = controlPoints;
    return e;
}

Entity createPolygon(int id, const QPointF& center, double radius, int sides)
{
    Entity e;
    e.id = id;
    e.type = EntityType::Polygon;
    e.points.append(center);
    e.radius = radius;
    e.sides = qMax(3, sides);
    return e;
}

Entity createSlot(int id, const QPointF& center1, const QPointF& center2, double radius)
{
    Entity e;
    e.id = id;
    e.type = EntityType::Slot;
    e.points.append(center1);
    e.points.append(center2);
    e.radius = radius;
    return e;
}

Entity createEllipse(int id, const QPointF& center, double majorRadius, double minorRadius)
{
    Entity e;
    e.id = id;
    e.type = EntityType::Ellipse;
    e.points.append(center);
    e.majorRadius = majorRadius;
    e.minorRadius = minorRadius;
    return e;
}

Entity createText(int id, const QPointF& position, const QString& text,
                  const QString& fontFamily, double fontSize, bool bold,
                  bool italic, double rotation)
{
    Entity e;
    e.id = id;
    e.type = EntityType::Text;
    e.points.append(position);
    e.text = text;
    e.fontFamily = fontFamily;
    e.fontSize = fontSize;
    e.fontBold = bold;
    e.fontItalic = italic;
    e.textRotation = rotation;
    return e;
}

// =====================================================================
//  Entity Query Functions
// =====================================================================

bool entitiesConnected(const Entity& e1, const Entity& e2, double tolerance)
{
    return connectionPoint(e1, e2, tolerance).has_value();
}

std::optional<QPointF> connectionPoint(const Entity& e1, const Entity& e2, double tolerance)
{
    QVector<QPointF> ep1 = e1.endpoints();
    QVector<QPointF> ep2 = e2.endpoints();

    for (const QPointF& p1 : ep1) {
        for (const QPointF& p2 : ep2) {
            if (pointsCoincident(p1, p2, tolerance)) {
                return (p1 + p2) / 2.0;
            }
        }
    }

    return std::nullopt;
}

bool entityIntersectsRect(const Entity& entity, const QRectF& rect)
{
    switch (entity.type) {
    case EntityType::Point:
        if (!entity.points.isEmpty()) {
            return rect.contains(entity.points[0]);
        }
        break;

    case EntityType::Line:
        if (entity.points.size() >= 2) {
            return lineIntersectsRect(entity.points[0], entity.points[1], rect);
        }
        break;

    case EntityType::Circle:
        if (!entity.points.isEmpty()) {
            return circleIntersectsRect(entity.points[0], entity.radius, rect);
        }
        break;

    case EntityType::Rectangle:
        if (entity.points.size() >= 2) {
            QRectF entityRect(entity.points[0], entity.points[1]);
            return rect.intersects(entityRect.normalized());
        }
        break;

    default:
        // For other types, use bounding box check
        return entity.boundingBox().toRect().intersects(rect);
    }

    return false;
}

bool entityEnclosedByRect(const Entity& entity, const QRectF& rect)
{
    switch (entity.type) {
    case EntityType::Point:
        if (!entity.points.isEmpty()) {
            return rect.contains(entity.points[0]);
        }
        break;

    case EntityType::Line:
        if (entity.points.size() >= 2) {
            return lineEnclosedByRect(entity.points[0], entity.points[1], rect);
        }
        break;

    case EntityType::Circle:
        if (!entity.points.isEmpty()) {
            return circleEnclosedByRect(entity.points[0], entity.radius, rect);
        }
        break;

    case EntityType::Rectangle:
        if (entity.points.size() >= 2) {
            return rect.contains(entity.points[0]) && rect.contains(entity.points[1]);
        }
        break;

    default:
        // For other types, check if all points are enclosed
        for (const QPointF& p : entity.points) {
            if (!rect.contains(p)) {
                return false;
            }
        }
        return !entity.points.isEmpty();
    }

    return false;
}

int nearestPointIndex(const Entity& entity, const QPointF& point)
{
    if (entity.points.isEmpty()) {
        return -1;
    }

    int nearestIdx = 0;
    double minDist = std::numeric_limits<double>::max();

    for (int i = 0; i < entity.points.size(); ++i) {
        double dist = QLineF(entity.points[i], point).length();
        if (dist < minDist) {
            minDist = dist;
            nearestIdx = i;
        }
    }

    return nearestIdx;
}

double getEntityAngle(const Entity& entity)
{
    if (entity.type != EntityType::Line || entity.points.size() < 2) {
        return 0.0;
    }

    QPointF delta = entity.points[1] - entity.points[0];
    double angle = std::atan2(delta.y(), delta.x()) * 180.0 / M_PI;

    // Normalize to 0-360
    if (angle < 0) {
        angle += 360.0;
    }
    return angle;
}

QVector<QPointF> entityToPolygon(const Entity& entity, int segments)
{
    QVector<QPointF> result;

    switch (entity.type) {
    case EntityType::Point:
        if (!entity.points.isEmpty()) {
            result.append(entity.points[0]);
        }
        break;

    case EntityType::Line:
        if (entity.points.size() >= 2) {
            result.append(entity.points[0]);
            result.append(entity.points[1]);
        }
        break;

    case EntityType::Rectangle:
        if (entity.points.size() >= 2) {
            QRectF rect(entity.points[0], entity.points[1]);
            rect = rect.normalized();
            result.append(rect.topLeft());
            result.append(rect.topRight());
            result.append(rect.bottomRight());
            result.append(rect.bottomLeft());
            result.append(rect.topLeft());  // Close
        }
        break;

    case EntityType::Circle:
        if (!entity.points.isEmpty()) {
            QPointF center = entity.points[0];
            for (int i = 0; i <= segments; ++i) {
                double angle = 2.0 * M_PI * i / segments;
                result.append(QPointF(
                    center.x() + entity.radius * std::cos(angle),
                    center.y() + entity.radius * std::sin(angle)));
            }
        }
        break;

    case EntityType::Arc:
        if (!entity.points.isEmpty()) {
            QPointF center = entity.points[0];
            double startRad = entity.startAngle * M_PI / 180.0;
            double sweepRad = entity.sweepAngle * M_PI / 180.0;
            int arcSegments = qMax(1, static_cast<int>(segments * qAbs(entity.sweepAngle) / 360.0));
            for (int i = 0; i <= arcSegments; ++i) {
                double angle = startRad + sweepRad * i / arcSegments;
                result.append(QPointF(
                    center.x() + entity.radius * std::cos(angle),
                    center.y() + entity.radius * std::sin(angle)));
            }
        }
        break;

    case EntityType::Ellipse:
        if (!entity.points.isEmpty()) {
            QPointF center = entity.points[0];
            for (int i = 0; i <= segments; ++i) {
                double angle = 2.0 * M_PI * i / segments;
                result.append(QPointF(
                    center.x() + entity.majorRadius * std::cos(angle),
                    center.y() + entity.minorRadius * std::sin(angle)));
            }
        }
        break;

    case EntityType::Polygon:
        if (!entity.points.isEmpty()) {
            QPointF center = entity.points[0];
            for (int i = 0; i <= entity.sides; ++i) {
                double angle = 2.0 * M_PI * i / entity.sides - M_PI / 2.0;
                result.append(QPointF(
                    center.x() + entity.radius * std::cos(angle),
                    center.y() + entity.radius * std::sin(angle)));
            }
        }
        break;

    case EntityType::Slot:
        if (entity.points.size() >= 2) {
            // Slot is two semicircles connected by lines
            QPointF c1 = entity.points[0];
            QPointF c2 = entity.points[1];
            QPointF dir = c2 - c1;
            double len = QLineF(c1, c2).length();
            if (len > 0) {
                QPointF norm(-dir.y() / len, dir.x() / len);
                int halfSegs = segments / 2;

                // First semicircle
                for (int i = 0; i <= halfSegs; ++i) {
                    double angle = M_PI / 2.0 + M_PI * i / halfSegs;
                    double dx = dir.x() / len, dy = dir.y() / len;
                    double ca = std::cos(angle), sa = std::sin(angle);
                    result.append(QPointF(
                        c1.x() + entity.radius * (dx * ca - dy * sa),
                        c1.y() + entity.radius * (dy * ca + dx * sa)));
                }
                // Second semicircle
                for (int i = 0; i <= halfSegs; ++i) {
                    double angle = -M_PI / 2.0 + M_PI * i / halfSegs;
                    double dx = dir.x() / len, dy = dir.y() / len;
                    double ca = std::cos(angle), sa = std::sin(angle);
                    result.append(QPointF(
                        c2.x() + entity.radius * (dx * ca - dy * sa),
                        c2.y() + entity.radius * (dy * ca + dx * sa)));
                }
                // Close
                if (!result.isEmpty()) {
                    result.append(result.first());
                }
            }
        }
        break;

    case EntityType::Spline:
        // For splines, return control points (proper tessellation would need Catmull-Rom)
        result = entity.points;
        break;

    case EntityType::Text:
        // Text has no geometric representation as polygon
        if (!entity.points.isEmpty()) {
            result.append(entity.points[0]);
        }
        break;
    }

    return result;
}

}  // namespace sketch
}  // namespace hobbycad
