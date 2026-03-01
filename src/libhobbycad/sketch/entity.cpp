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

#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace hobbycad {
namespace sketch {

using namespace geometry;

// =====================================================================
//  Entity Methods
// =====================================================================

BoundingBox Entity::boundingBox() const
{
    BoundingBox bbox;

    for (const Point2D& p : points) {
        bbox.include(p);
    }

    // Expand for circles and arcs
    if (type == EntityType::Circle && !points.empty()) {
        bbox.include(Point2D(points[0].x - radius, points[0].y - radius));
        bbox.include(Point2D(points[0].x + radius, points[0].y + radius));
    } else if (type == EntityType::Arc && !points.empty()) {
        // For arcs, include the center and endpoints
        bbox.include(Point2D(points[0].x - radius, points[0].y - radius));
        bbox.include(Point2D(points[0].x + radius, points[0].y + radius));
    } else if (type == EntityType::Polygon && !points.empty()) {
        bbox.include(Point2D(points[0].x - radius, points[0].y - radius));
        bbox.include(Point2D(points[0].x + radius, points[0].y + radius));
    } else if (type == EntityType::Slot && points.size() >= 2) {
        // Include slot width
        for (const Point2D& p : points) {
            bbox.include(Point2D(p.x - radius, p.y - radius));
            bbox.include(Point2D(p.x + radius, p.y + radius));
        }
    } else if (type == EntityType::Ellipse && !points.empty()) {
        bbox.include(Point2D(points[0].x - majorRadius, points[0].y - minorRadius));
        bbox.include(Point2D(points[0].x + majorRadius, points[0].y + minorRadius));
    }

    return bbox;
}

std::vector<Point2D> Entity::endpoints() const
{
    std::vector<Point2D> result;

    switch (type) {
    case EntityType::Line:
        if (points.size() >= 2) {
            result.push_back(points[0]);
            result.push_back(points[1]);
        }
        break;
    case EntityType::Arc:
        if (!points.empty()) {
            Arc arc;
            arc.center = points[0];
            arc.radius = radius;
            arc.startAngle = startAngle;
            arc.sweepAngle = sweepAngle;
            result.push_back(arc.startPoint());
            result.push_back(arc.endPoint());
        }
        break;
    case EntityType::Spline:
        if (points.size() >= 2) {
            result.push_back(points.front());
            result.push_back(points.back());
        }
        break;
    case EntityType::Slot:
        if (points.size() >= 3) {
            // Arc slot: points[0] = arc center, points[1] = start, points[2] = end
            result.push_back(points[1]);
            result.push_back(points[2]);
        } else if (points.size() >= 2) {
            // Linear slot: points[0] and points[1] are arc centers (endpoints)
            result.push_back(points[0]);
            result.push_back(points[1]);
        }
        break;
    default:
        break;
    }

    return result;
}

bool Entity::containsPoint(const Point2D& point, double tolerance) const
{
    return distanceTo(point) < tolerance;
}

Point2D Entity::closestPoint(const Point2D& point) const
{
    switch (type) {
    case EntityType::Point:
        if (!points.empty()) {
            return points[0];
        }
        break;

    case EntityType::Line:
        if (points.size() >= 2) {
            return closestPointOnLine(point, points[0], points[1]);
        }
        break;

    case EntityType::Circle:
        if (!points.empty()) {
            return closestPointOnCircle(point, points[0], radius);
        }
        break;

    case EntityType::Arc:
        if (!points.empty()) {
            Arc arc;
            arc.center = points[0];
            arc.radius = radius;
            arc.startAngle = startAngle;
            arc.sweepAngle = sweepAngle;
            return closestPointOnArc(point, arc);
        }
        break;

    case EntityType::Rectangle:
        if (points.size() >= 4) {
            // 4-point rotated rectangle: find closest point on any edge
            Point2D closest = points[0];
            double minDist = std::hypot(point.x - points[0].x, point.y - points[0].y);
            for (int i = 0; i < 4; ++i) {
                int j = (i + 1) % 4;
                Point2D cp = closestPointOnLine(point, points[i], points[j]);
                double d = std::hypot(point.x - cp.x, point.y - cp.y);
                if (d < minDist) { minDist = d; closest = cp; }
            }
            return closest;
        } else if (points.size() >= 2) {
            // Axis-aligned rectangle from 2 corner points
            Point2D corners[4] = {
                points[0],
                Point2D(points[1].x, points[0].y),
                points[1],
                Point2D(points[0].x, points[1].y)
            };
            Point2D closest = corners[0];
            double minDist = std::hypot(point.x - corners[0].x, point.y - corners[0].y);

            for (int i = 0; i < 4; ++i) {
                int j = (i + 1) % 4;
                Point2D cp = closestPointOnLine(point, corners[i], corners[j]);
                double d = std::hypot(point.x - cp.x, point.y - cp.y);
                if (d < minDist) {
                    minDist = d;
                    closest = cp;
                }
            }
            return closest;
        }
        break;

    case EntityType::Parallelogram:
        if (points.size() >= 4) {
            Point2D closest = points[0];
            double minDist = std::hypot(point.x - points[0].x, point.y - points[0].y);
            for (int i = 0; i < 4; ++i) {
                int j = (i + 1) % 4;
                Point2D cp = closestPointOnLine(point, points[i], points[j]);
                double d = std::hypot(point.x - cp.x, point.y - cp.y);
                if (d < minDist) { minDist = d; closest = cp; }
            }
            return closest;
        }
        break;

    case EntityType::Ellipse:
        if (!points.empty()) {
            double a = majorRadius;
            double b = minorRadius;
            if (a < 0.001 || b < 0.001) break;
            double dx = point.x - points[0].x;
            double dy = point.y - points[0].y;
            double angle = std::atan2(dy, dx);
            // Approximate: point on ellipse at same angle from center
            return Point2D(points[0].x + a * std::cos(angle),
                           points[0].y + b * std::sin(angle));
        }
        break;

    case EntityType::Spline:
        if (points.size() >= 2) {
            if (points.size() == 2) {
                return closestPointOnLine(point, points[0], points[1]);
            }
            // Sample Catmull-Rom spline and find closest point on sub-segments
            Point2D bestPoint = points[0];
            double minDist = std::hypot(point.x - points[0].x, point.y - points[0].y);
            const int samplesPerSegment = 20;

            for (int i = 0; i < static_cast<int>(points.size()) - 1; ++i) {
                Point2D cp0 = (i == 0) ? points[i] : points[i - 1];
                Point2D cp1 = points[i];
                Point2D cp2 = points[i + 1];
                Point2D cp3 = (i == static_cast<int>(points.size()) - 2) ? points[i + 1] : points[i + 2];

                Point2D b0 = cp1;
                Point2D b1 = cp1 + (cp2 - cp0) / 6.0;
                Point2D b2 = cp2 - (cp3 - cp1) / 6.0;
                Point2D b3 = cp2;

                Point2D prev = b0;
                for (int s = 1; s <= samplesPerSegment; ++s) {
                    double st = static_cast<double>(s) / samplesPerSegment;
                    double u = 1.0 - st;
                    Point2D cur = u*u*u*b0 + 3.0*u*u*st*b1 + 3.0*u*st*st*b2 + st*st*st*b3;
                    Point2D cp = closestPointOnLine(point, prev, cur);
                    double d = std::hypot(point.x - cp.x, point.y - cp.y);
                    if (d < minDist) { minDist = d; bestPoint = cp; }
                    prev = cur;
                }
            }
            return bestPoint;
        }
        break;

    case EntityType::Slot:
        if (points.size() >= 3) {
            // Arc slot: points[0] = arc center, points[1] = start, points[2] = end
            Point2D arcCenter = points[0];
            Point2D start = points[1];
            Point2D end = points[2];
            double halfWidth = radius;

            double arcRadius = std::hypot(start.x - arcCenter.x, start.y - arcCenter.y);
            double distToCenter = std::hypot(point.x - arcCenter.x, point.y - arcCenter.y);

            // Compute angles
            double startAngle = std::atan2(start.y - arcCenter.y, start.x - arcCenter.x);
            double endAngle = std::atan2(end.y - arcCenter.y, end.x - arcCenter.x);
            double pointAngle = std::atan2(point.y - arcCenter.y, point.x - arcCenter.x);

            // Normalize sweep angle
            double sweep = endAngle - startAngle;
            while (sweep > M_PI) sweep -= 2 * M_PI;
            while (sweep < -M_PI) sweep += 2 * M_PI;
            if (arcFlipped) {
                sweep = (sweep > 0) ? sweep - 2 * M_PI : sweep + 2 * M_PI;
            }

            double relAngle = pointAngle - startAngle;
            while (relAngle > M_PI) relAngle -= 2 * M_PI;
            while (relAngle < -M_PI) relAngle += 2 * M_PI;

            bool inSweep = (sweep >= 0) ? (relAngle >= 0 && relAngle <= sweep) : (relAngle <= 0 && relAngle >= sweep);

            if (inSweep) {
                // Point is in angular range - return closest point on inner or outer arc
                if (distToCenter < arcRadius) {
                    // Inside arc - closest is on inner edge
                    double innerR = arcRadius - halfWidth;
                    return arcCenter + (point - arcCenter) * (innerR / distToCenter);
                } else {
                    // Outside arc - closest is on outer edge
                    double outerR = arcRadius + halfWidth;
                    return arcCenter + (point - arcCenter) * (outerR / distToCenter);
                }
            } else {
                // Point is outside angular range - closest to one of the endpoint semicircles
                double distToStart = std::hypot(point.x - start.x, point.y - start.y);
                double distToEnd = std::hypot(point.x - end.x, point.y - end.y);
                if (distToStart < distToEnd) {
                    return start + (point - start) * (halfWidth / distToStart);
                } else {
                    return end + (point - end) * (halfWidth / distToEnd);
                }
            }
        } else if (points.size() >= 2) {
            // Linear slot: points[0] and points[1] are arc centers
            Point2D p1 = points[0];
            Point2D p2 = points[1];
            double halfWidth = radius;

            double len = std::hypot(p2.x - p1.x, p2.y - p1.y);
            if (len < 0.001) return p1;

            Point2D d = p2 - p1;
            Point2D dp = point - p1;
            double t = (dp.x * d.x + dp.y * d.y) / (len * len);

            if (t >= 0.0 && t <= 1.0) {
                // Project onto center line
                Point2D onLine = p1 + t * d;
                double dist = std::hypot(point.x - onLine.x, point.y - onLine.y);
                if (dist < 0.001) return onLine;  // On center line
                return onLine + (point - onLine) * (halfWidth / dist);
            } else if (t < 0.0) {
                // Beyond p1 - closest on p1's semicircle
                double dist = std::hypot(point.x - p1.x, point.y - p1.y);
                if (dist < 0.001) return p1;
                return p1 + (point - p1) * (halfWidth / dist);
            } else {
                // Beyond p2 - closest on p2's semicircle
                double dist = std::hypot(point.x - p2.x, point.y - p2.y);
                if (dist < 0.001) return p2;
                return p2 + (point - p2) * (halfWidth / dist);
            }
        }
        break;

    default:
        break;
    }

    return point;  // Fallback
}

double Entity::distanceTo(const Point2D& point) const
{
    switch (type) {
    case EntityType::Point:
        if (!points.empty()) {
            return std::hypot(point.x - points[0].x, point.y - points[0].y);
        }
        break;

    case EntityType::Line:
        if (points.size() >= 2) {
            return pointToLineDistance(point, points[0], points[1]);
        }
        break;

    case EntityType::Circle:
        if (!points.empty()) {
            return pointToCircleDistance(point, points[0], radius);
        }
        break;

    case EntityType::Arc:
        if (!points.empty()) {
            Arc arc;
            arc.center = points[0];
            arc.radius = radius;
            arc.startAngle = startAngle;
            arc.sweepAngle = sweepAngle;
            return pointToArcDistance(point, arc);
        }
        break;

    case EntityType::Rectangle:
        if (points.size() >= 4) {
            // 4-point rotated rectangle: test all 4 edges
            double minDist = std::numeric_limits<double>::max();
            for (int i = 0; i < 4; ++i) {
                int j = (i + 1) % 4;
                double d = pointToLineDistance(point, points[i], points[j]);
                if (d < minDist) minDist = d;
            }
            return minDist;
        } else if (points.size() >= 2) {
            Point2D cp = closestPoint(point);
            return std::hypot(point.x - cp.x, point.y - cp.y);
        }
        break;

    case EntityType::Parallelogram:
        if (points.size() >= 4) {
            // Test distance to all 4 edges
            double minDist = std::numeric_limits<double>::max();
            for (int i = 0; i < 4; ++i) {
                int j = (i + 1) % 4;
                double d = pointToLineDistance(point, points[i], points[j]);
                if (d < minDist) minDist = d;
            }
            return minDist;
        }
        break;

    case EntityType::Ellipse:
        if (!points.empty()) {
            double a = majorRadius;
            double b = minorRadius;
            if (a < 0.001 || b < 0.001) break;
            double dx = point.x - points[0].x;
            double dy = point.y - points[0].y;
            // Normalized ellipse equation: (dx/a)^2 + (dy/b)^2 = 1 on the outline
            double normalized = (dx * dx) / (a * a) + (dy * dy) / (b * b);
            // Approximate distance: |normalized - 1| * min(a,b)
            // This matches the GUI hit-testing tolerance calculation
            return std::abs(normalized - 1.0) * std::min(a, b);
        }
        break;

    case EntityType::Spline:
        if (points.size() >= 2) {
            if (points.size() == 2) {
                // Just two points - distance to line segment
                return pointToLineDistance(point, points[0], points[1]);
            }
            // Sample Catmull-Rom spline and find minimum distance to sub-segments
            const int samplesPerSegment = 20;
            double minDist = std::numeric_limits<double>::max();
            for (int i = 0; i < static_cast<int>(points.size()) - 1; ++i) {
                Point2D cp0 = (i == 0) ? points[i] : points[i - 1];
                Point2D cp1 = points[i];
                Point2D cp2 = points[i + 1];
                Point2D cp3 = (i == static_cast<int>(points.size()) - 2) ? points[i + 1] : points[i + 2];

                // Convert Catmull-Rom to cubic Bezier control points (tension = 0.5)
                Point2D b0 = cp1;
                Point2D b1 = cp1 + (cp2 - cp0) / 6.0;
                Point2D b2 = cp2 - (cp3 - cp1) / 6.0;
                Point2D b3 = cp2;

                // Sample the cubic Bezier and test each sub-segment
                Point2D prev = b0;
                for (int s = 1; s <= samplesPerSegment; ++s) {
                    double st = static_cast<double>(s) / samplesPerSegment;
                    double u = 1.0 - st;
                    Point2D cur = u*u*u*b0 + 3.0*u*u*st*b1 + 3.0*u*st*st*b2 + st*st*st*b3;
                    double d = pointToLineDistance(point, prev, cur);
                    if (d < minDist) minDist = d;
                    prev = cur;
                }
            }
            return minDist;
        }
        break;

    case EntityType::Slot:
        if (points.size() >= 3) {
            // Arc slot: points[0] = arc center, points[1] = start, points[2] = end
            Point2D arcCenter = points[0];
            Point2D start = points[1];
            Point2D end = points[2];
            double halfWidth = radius;

            double arcRadius = std::hypot(start.x - arcCenter.x, start.y - arcCenter.y);
            double distToCenter = std::hypot(point.x - arcCenter.x, point.y - arcCenter.y);

            // Compute angles
            double startAngle = std::atan2(start.y - arcCenter.y, start.x - arcCenter.x);
            double endAngle = std::atan2(end.y - arcCenter.y, end.x - arcCenter.x);
            double pointAngle = std::atan2(point.y - arcCenter.y, point.x - arcCenter.x);

            // Normalize sweep angle
            double sweep = endAngle - startAngle;
            while (sweep > M_PI) sweep -= 2 * M_PI;
            while (sweep < -M_PI) sweep += 2 * M_PI;
            if (arcFlipped) {
                sweep = (sweep > 0) ? sweep - 2 * M_PI : sweep + 2 * M_PI;
            }

            double relAngle = pointAngle - startAngle;
            while (relAngle > M_PI) relAngle -= 2 * M_PI;
            while (relAngle < -M_PI) relAngle += 2 * M_PI;

            bool inSweep = (sweep >= 0) ? (relAngle >= 0 && relAngle <= sweep) : (relAngle <= 0 && relAngle >= sweep);

            if (inSweep) {
                // Point is in angular range - check radial distance
                double innerRadius = arcRadius - halfWidth;
                double outerRadius = arcRadius + halfWidth;
                if (distToCenter >= innerRadius && distToCenter <= outerRadius) {
                    return 0.0;  // Inside the slot
                } else if (distToCenter < innerRadius) {
                    return innerRadius - distToCenter;
                } else {
                    return distToCenter - outerRadius;
                }
            } else {
                // Point is outside angular range - check endpoint semicircles
                double distToStart = std::hypot(point.x - start.x, point.y - start.y);
                double distToEnd = std::hypot(point.x - end.x, point.y - end.y);
                double minDist = std::min(distToStart, distToEnd);
                if (minDist <= halfWidth) {
                    return 0.0;  // Inside endpoint semicircle
                }
                return minDist - halfWidth;
            }
        } else if (points.size() >= 2) {
            // Linear slot: points[0] and points[1] are arc centers
            Point2D p1 = points[0];
            Point2D p2 = points[1];
            double halfWidth = radius;

            double len = std::hypot(p2.x - p1.x, p2.y - p1.y);
            if (len < 0.001) return std::hypot(point.x - p1.x, point.y - p1.y);

            Point2D d = p2 - p1;
            Point2D dp = point - p1;
            double t = (dp.x * d.x + dp.y * d.y) / (len * len);

            if (t >= 0.0 && t <= 1.0) {
                // Project onto center line
                Point2D onLine = p1 + t * d;
                double dist = std::hypot(point.x - onLine.x, point.y - onLine.y);
                if (dist <= halfWidth) {
                    return 0.0;  // Inside the slot
                }
                return dist - halfWidth;
            } else if (t < 0.0) {
                // Beyond p1 - check p1's semicircle
                double dist = std::hypot(point.x - p1.x, point.y - p1.y);
                if (dist <= halfWidth) return 0.0;
                return dist - halfWidth;
            } else {
                // Beyond p2 - check p2's semicircle
                double dist = std::hypot(point.x - p2.x, point.y - p2.y);
                if (dist <= halfWidth) return 0.0;
                return dist - halfWidth;
            }
        }
        break;

    default:
        break;
    }

    return std::numeric_limits<double>::max();
}

void Entity::transform(const Transform2D& t)
{
    for (Point2D& p : points) {
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

Entity createPoint(int id, const Point2D& position)
{
    Entity e;
    e.id = id;
    e.type = EntityType::Point;
    e.points.push_back(position);
    return e;
}

Entity createLine(int id, const Point2D& start, const Point2D& end)
{
    Entity e;
    e.id = id;
    e.type = EntityType::Line;
    e.points.push_back(start);
    e.points.push_back(end);
    return e;
}

Entity createRectangle(int id, const Point2D& corner1, const Point2D& corner2)
{
    Entity e;
    e.id = id;
    e.type = EntityType::Rectangle;
    e.points.push_back(corner1);
    e.points.push_back(corner2);
    return e;
}

Entity createCircle(int id, const Point2D& center, double radius)
{
    Entity e;
    e.id = id;
    e.type = EntityType::Circle;
    e.points.push_back(center);
    e.radius = radius;
    return e;
}

Entity createArc(int id, const Point2D& center, double radius,
                 double startAngle, double sweepAngle)
{
    Entity e;
    e.id = id;
    e.type = EntityType::Arc;
    e.points.push_back(center);
    e.radius = radius;
    e.startAngle = startAngle;
    e.sweepAngle = sweepAngle;
    return e;
}

Entity createArcFromThreePoints(int id, const Point2D& start,
                                const Point2D& mid, const Point2D& end)
{
    auto arc = arcFromThreePoints(start, mid, end);
    if (arc) {
        return createArc(id, arc->center, arc->radius,
                         arc->startAngle, arc->sweepAngle);
    }
    // Fallback to line if collinear
    return createLine(id, start, end);
}

Entity createSpline(int id, const std::vector<Point2D>& controlPoints)
{
    Entity e;
    e.id = id;
    e.type = EntityType::Spline;
    e.points = controlPoints;
    return e;
}

Entity createPolygon(int id, const Point2D& center, double radius, int sides)
{
    Entity e;
    e.id = id;
    e.type = EntityType::Polygon;
    e.points.push_back(center);
    e.radius = radius;
    e.sides = std::max(3, sides);
    return e;
}

Entity createSlot(int id, const Point2D& center1, const Point2D& center2, double radius)
{
    Entity e;
    e.id = id;
    e.type = EntityType::Slot;
    e.points.push_back(center1);
    e.points.push_back(center2);
    e.radius = radius;
    return e;
}

Entity createArcSlot(int id, const Point2D& arcCenter, const Point2D& start,
                     const Point2D& end, double radius, bool flipped)
{
    Entity e;
    e.id = id;
    e.type = EntityType::Slot;
    e.points.push_back(arcCenter);   // points[0] = arc center
    e.points.push_back(start);       // points[1] = start endpoint
    e.points.push_back(end);         // points[2] = end endpoint
    e.radius = radius;            // Half-width of the slot
    e.arcFlipped = flipped;       // True for >180 degree arcs
    return e;
}

Entity createEllipse(int id, const Point2D& center, double majorRadius, double minorRadius)
{
    Entity e;
    e.id = id;
    e.type = EntityType::Ellipse;
    e.points.push_back(center);
    e.majorRadius = majorRadius;
    e.minorRadius = minorRadius;
    return e;
}

Entity createText(int id, const Point2D& position, const std::string& text,
                  const std::string& fontFamily, double fontSize, bool bold,
                  bool italic, double rotation)
{
    Entity e;
    e.id = id;
    e.type = EntityType::Text;
    e.points.push_back(position);
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

std::optional<Point2D> connectionPoint(const Entity& e1, const Entity& e2, double tolerance)
{
    std::vector<Point2D> ep1 = e1.endpoints();
    std::vector<Point2D> ep2 = e2.endpoints();

    for (const Point2D& p1 : ep1) {
        for (const Point2D& p2 : ep2) {
            if (pointsCoincident(p1, p2, tolerance)) {
                return (p1 + p2) / 2.0;
            }
        }
    }

    return std::nullopt;
}

bool entityIntersectsRect(const Entity& entity, const Rect2D& rect)
{
    switch (entity.type) {
    case EntityType::Point:
        if (!entity.points.empty()) {
            return rect.contains(entity.points[0]);
        }
        break;

    case EntityType::Line:
        if (entity.points.size() >= 2) {
            // Check endpoint containment first (faster)
            if (rect.contains(entity.points[0]) || rect.contains(entity.points[1]))
                return true;
            // Then check edge crossing using manual line-segment intersection
            Point2D tl = rect.topLeft();
            Point2D tr = rect.topRight();
            Point2D br = rect.bottomRight();
            Point2D bl = rect.bottomLeft();
            auto checkIntersect = [&](const Point2D& a, const Point2D& b) {
                auto lli = lineLineIntersection(entity.points[0], entity.points[1], a, b);
                return lli.intersects && lli.withinSegment1 && lli.withinSegment2;
            };
            if (checkIntersect(tl, tr)) return true;
            if (checkIntersect(tr, br)) return true;
            if (checkIntersect(br, bl)) return true;
            if (checkIntersect(bl, tl)) return true;
        }
        break;

    case EntityType::Circle:
        if (!entity.points.empty()) {
            Point2D center = entity.points[0];
            double r = entity.radius;
            // Quick reject: expand rect by radius
            Rect2D expanded(rect.x - r, rect.y - r,
                            rect.width + 2*r, rect.height + 2*r);
            if (!expanded.contains(center)) return false;
            // Precise: closest point on rect to center
            double cx = std::clamp(center.x, rect.left(), rect.right());
            double cy = std::clamp(center.y, rect.top(), rect.bottom());
            double dist = std::hypot(center.x - cx, center.y - cy);
            return dist <= r || rect.contains(center);
        }
        break;

    case EntityType::Rectangle:
        if (entity.points.size() >= 4) {
            // 4-point rotated rectangle: check any vertex or any edge crossing
            for (int i = 0; i < 4; ++i) {
                if (rect.contains(entity.points[i])) return true;
                Point2D edgeStart = entity.points[i];
                Point2D edgeEnd = entity.points[(i + 1) % 4];
                Point2D tl = rect.topLeft();
                Point2D tr = rect.topRight();
                Point2D br = rect.bottomRight();
                Point2D bl = rect.bottomLeft();
                auto checkIntersect = [&](const Point2D& a, const Point2D& b) {
                    auto lli = lineLineIntersection(edgeStart, edgeEnd, a, b);
                    return lli.intersects && lli.withinSegment1 && lli.withinSegment2;
                };
                if (checkIntersect(tl, tr)) return true;
                if (checkIntersect(tr, br)) return true;
                if (checkIntersect(br, bl)) return true;
                if (checkIntersect(bl, tl)) return true;
            }
        } else if (entity.points.size() >= 2) {
            Rect2D entityRect = Rect2D::fromPoints(entity.points[0], entity.points[1]);
            return !(rect.right() < entityRect.left() || entityRect.right() < rect.left() ||
                     rect.bottom() < entityRect.top() || entityRect.bottom() < rect.top());
        }
        break;

    case EntityType::Arc:
        if (!entity.points.empty()) {
            Point2D center = entity.points[0];
            double r = entity.radius;
            double startRad = entity.startAngle * M_PI / 180.0;
            double endRad   = (entity.startAngle + entity.sweepAngle) * M_PI / 180.0;
            Point2D startPt = center + Point2D(r * std::cos(startRad), r * std::sin(startRad));
            Point2D endPt   = center + Point2D(r * std::cos(endRad),   r * std::sin(endRad));
            if (rect.contains(startPt) || rect.contains(endPt)) return true;
            // Also check midpoint of arc
            double midRad = (entity.startAngle + entity.sweepAngle / 2) * M_PI / 180.0;
            Point2D midPt = center + Point2D(r * std::cos(midRad), r * std::sin(midRad));
            return rect.contains(midPt);
        }
        break;

    case EntityType::Spline:
        for (const Point2D& pt : entity.points) {
            if (rect.contains(pt)) return true;
        }
        break;

    default:
        // For other types, check all points
        for (const Point2D& pt : entity.points) {
            if (rect.contains(pt)) return true;
        }
        break;
    }

    return false;
}

bool entityEnclosedByRect(const Entity& entity, const Rect2D& rect)
{
    switch (entity.type) {
    case EntityType::Point:
        if (!entity.points.empty()) {
            return rect.contains(entity.points[0]);
        }
        break;

    case EntityType::Line:
        if (entity.points.size() >= 2) {
            return rect.contains(entity.points[0]) && rect.contains(entity.points[1]);
        }
        break;

    case EntityType::Circle:
        if (!entity.points.empty()) {
            Point2D center = entity.points[0];
            double r = entity.radius;
            Rect2D circleRect(center.x - r, center.y - r, r * 2, r * 2);
            return rect.contains(circleRect);
        }
        break;

    case EntityType::Rectangle:
        if (entity.points.size() >= 4) {
            // 4-point rotated rectangle: all corners must be enclosed
            return rect.contains(entity.points[0]) && rect.contains(entity.points[1]) &&
                   rect.contains(entity.points[2]) && rect.contains(entity.points[3]);
        } else if (entity.points.size() >= 2) {
            return rect.contains(entity.points[0]) && rect.contains(entity.points[1]);
        }
        break;

    case EntityType::Arc:
        if (!entity.points.empty()) {
            Point2D center = entity.points[0];
            double r = entity.radius;
            double startRad = entity.startAngle * M_PI / 180.0;
            double endRad   = (entity.startAngle + entity.sweepAngle) * M_PI / 180.0;
            Point2D startPt = center + Point2D(r * std::cos(startRad), r * std::sin(startRad));
            Point2D endPt   = center + Point2D(r * std::cos(endRad),   r * std::sin(endRad));
            if (!rect.contains(startPt) || !rect.contains(endPt)) return false;
            // Also check midpoint
            double midRad = (entity.startAngle + entity.sweepAngle / 2) * M_PI / 180.0;
            Point2D midPt = center + Point2D(r * std::cos(midRad), r * std::sin(midRad));
            return rect.contains(midPt);
        }
        break;

    case EntityType::Spline:
    default:
        // All control points must be enclosed
        for (const Point2D& p : entity.points) {
            if (!rect.contains(p)) return false;
        }
        return !entity.points.empty();
    }

    return false;
}

int nearestPointIndex(const Entity& entity, const Point2D& point)
{
    if (entity.points.empty()) {
        return -1;
    }

    int nearestIdx = 0;
    double minDist = std::numeric_limits<double>::max();

    for (int i = 0; i < static_cast<int>(entity.points.size()); ++i) {
        double dist = std::hypot(entity.points[i].x - point.x, entity.points[i].y - point.y);
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

    Point2D delta = entity.points[1] - entity.points[0];
    double angle = std::atan2(delta.y, delta.x) * 180.0 / M_PI;

    // Normalize to 0-360
    if (angle < 0) {
        angle += 360.0;
    }
    return angle;
}

std::vector<Point2D> entityToPolygon(const Entity& entity, int segments)
{
    std::vector<Point2D> result;

    switch (entity.type) {
    case EntityType::Point:
        if (!entity.points.empty()) {
            result.push_back(entity.points[0]);
        }
        break;

    case EntityType::Line:
        if (entity.points.size() >= 2) {
            result.push_back(entity.points[0]);
            result.push_back(entity.points[1]);
        }
        break;

    case EntityType::Rectangle:
        if (entity.points.size() >= 2) {
            Rect2D rect = Rect2D::fromPoints(entity.points[0], entity.points[1]);
            result.push_back(rect.topLeft());
            result.push_back(rect.topRight());
            result.push_back(rect.bottomRight());
            result.push_back(rect.bottomLeft());
            result.push_back(rect.topLeft());  // Close
        }
        break;

    case EntityType::Circle:
        if (!entity.points.empty()) {
            Point2D center = entity.points[0];
            for (int i = 0; i <= segments; ++i) {
                double angle = 2.0 * M_PI * i / segments;
                result.push_back(Point2D(
                    center.x + entity.radius * std::cos(angle),
                    center.y + entity.radius * std::sin(angle)));
            }
        }
        break;

    case EntityType::Arc:
        if (!entity.points.empty()) {
            Point2D center = entity.points[0];
            double startRad = entity.startAngle * M_PI / 180.0;
            double sweepRad = entity.sweepAngle * M_PI / 180.0;
            int arcSegments = std::max(1, static_cast<int>(segments * std::abs(entity.sweepAngle) / 360.0));
            for (int i = 0; i <= arcSegments; ++i) {
                double angle = startRad + sweepRad * i / arcSegments;
                result.push_back(Point2D(
                    center.x + entity.radius * std::cos(angle),
                    center.y + entity.radius * std::sin(angle)));
            }
        }
        break;

    case EntityType::Ellipse:
        if (!entity.points.empty()) {
            Point2D center = entity.points[0];
            for (int i = 0; i <= segments; ++i) {
                double angle = 2.0 * M_PI * i / segments;
                result.push_back(Point2D(
                    center.x + entity.majorRadius * std::cos(angle),
                    center.y + entity.minorRadius * std::sin(angle)));
            }
        }
        break;

    case EntityType::Polygon:
        if (!entity.points.empty()) {
            Point2D center = entity.points[0];
            for (int i = 0; i <= entity.sides; ++i) {
                double angle = 2.0 * M_PI * i / entity.sides - M_PI / 2.0;
                result.push_back(Point2D(
                    center.x + entity.radius * std::cos(angle),
                    center.y + entity.radius * std::sin(angle)));
            }
        }
        break;

    case EntityType::Slot:
        if (entity.points.size() >= 3) {
            // Arc slot: points[0] = arc center, points[1] = start, points[2] = end
            Point2D arcCenter = entity.points[0];
            Point2D start = entity.points[1];
            Point2D end = entity.points[2];
            double halfWidth = entity.radius;

            double arcRadius = std::hypot(start.x - arcCenter.x, start.y - arcCenter.y);
            double innerRadius = arcRadius - halfWidth;
            double outerRadius = arcRadius + halfWidth;

            // Calculate angles
            double startAngle = std::atan2(start.y - arcCenter.y, start.x - arcCenter.x);
            double endAngle = std::atan2(end.y - arcCenter.y, end.x - arcCenter.x);
            double sweepAngle = endAngle - startAngle;

            // Normalize sweep
            while (sweepAngle > M_PI) sweepAngle -= 2 * M_PI;
            while (sweepAngle < -M_PI) sweepAngle += 2 * M_PI;
            if (entity.arcFlipped) {
                sweepAngle = (sweepAngle > 0) ? sweepAngle - 2 * M_PI : sweepAngle + 2 * M_PI;
            }

            int arcSegs = std::max(8, static_cast<int>(segments * std::abs(sweepAngle) / (2 * M_PI)));
            int capSegs = segments / 4;

            // Outer arc
            for (int i = 0; i <= arcSegs; ++i) {
                double angle = startAngle + sweepAngle * i / arcSegs;
                result.push_back(Point2D(
                    arcCenter.x + outerRadius * std::cos(angle),
                    arcCenter.y + outerRadius * std::sin(angle)));
            }

            // End cap (semicircle)
            Point2D endOuter = arcCenter + Point2D(outerRadius * std::cos(endAngle), outerRadius * std::sin(endAngle));
            Point2D endInner = arcCenter + Point2D(innerRadius * std::cos(endAngle), innerRadius * std::sin(endAngle));
            Point2D endCapCenter = (endOuter + endInner) / 2.0;
            double capDir = (sweepAngle >= 0) ? 1.0 : -1.0;
            for (int i = 1; i <= capSegs; ++i) {
                double capAngle = endAngle + capDir * M_PI * i / capSegs;
                result.push_back(Point2D(
                    endCapCenter.x + halfWidth * std::cos(capAngle),
                    endCapCenter.y + halfWidth * std::sin(capAngle)));
            }

            // Inner arc (reverse)
            for (int i = arcSegs; i >= 0; --i) {
                double angle = startAngle + sweepAngle * i / arcSegs;
                result.push_back(Point2D(
                    arcCenter.x + innerRadius * std::cos(angle),
                    arcCenter.y + innerRadius * std::sin(angle)));
            }

            // Start cap (semicircle)
            Point2D startOuter = arcCenter + Point2D(outerRadius * std::cos(startAngle), outerRadius * std::sin(startAngle));
            Point2D startInner = arcCenter + Point2D(innerRadius * std::cos(startAngle), innerRadius * std::sin(startAngle));
            Point2D startCapCenter = (startOuter + startInner) / 2.0;
            for (int i = 1; i < capSegs; ++i) {
                double capAngle = startAngle + M_PI + capDir * M_PI * i / capSegs;
                result.push_back(Point2D(
                    startCapCenter.x + halfWidth * std::cos(capAngle),
                    startCapCenter.y + halfWidth * std::sin(capAngle)));
            }

            // Close
            if (!result.empty()) {
                result.push_back(result.front());
            }
        } else if (entity.points.size() >= 2) {
            // Linear slot: two semicircles connected by lines
            Point2D c1 = entity.points[0];
            Point2D c2 = entity.points[1];
            Point2D dir = c2 - c1;
            double len = std::hypot(dir.x, dir.y);
            if (len > 0) {
                Point2D norm(-dir.y / len, dir.x / len);
                int halfSegs = segments / 2;

                // First semicircle
                for (int i = 0; i <= halfSegs; ++i) {
                    double angle = M_PI / 2.0 + M_PI * i / halfSegs;
                    double dx = dir.x / len, dy = dir.y / len;
                    double ca = std::cos(angle), sa = std::sin(angle);
                    result.push_back(Point2D(
                        c1.x + entity.radius * (dx * ca - dy * sa),
                        c1.y + entity.radius * (dy * ca + dx * sa)));
                }
                // Second semicircle
                for (int i = 0; i <= halfSegs; ++i) {
                    double angle = -M_PI / 2.0 + M_PI * i / halfSegs;
                    double dx = dir.x / len, dy = dir.y / len;
                    double ca = std::cos(angle), sa = std::sin(angle);
                    result.push_back(Point2D(
                        c2.x + entity.radius * (dx * ca - dy * sa),
                        c2.y + entity.radius * (dy * ca + dx * sa)));
                }
                // Close
                if (!result.empty()) {
                    result.push_back(result.front());
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
        if (!entity.points.empty()) {
            result.push_back(entity.points[0]);
        }
        break;
    }

    return result;
}

}  // namespace sketch
}  // namespace hobbycad
