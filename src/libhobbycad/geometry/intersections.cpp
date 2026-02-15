// =====================================================================
//  src/libhobbycad/geometry/intersections.cpp — Intersection functions
// =====================================================================
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include <hobbycad/geometry/intersections.h>

namespace hobbycad {
namespace geometry {

// =====================================================================
//  Line-Line Intersection
// =====================================================================

LineLineIntersection lineLineIntersection(
    const QPointF& p1, const QPointF& p2,
    const QPointF& p3, const QPointF& p4)
{
    LineLineIntersection result;

    // Direction vectors
    double d1x = p2.x() - p1.x();
    double d1y = p2.y() - p1.y();
    double d2x = p4.x() - p3.x();
    double d2y = p4.y() - p3.y();

    // Cross product of directions (determinant)
    double cross = d1x * d2y - d1y * d2x;

    // Vector from p1 to p3
    double dx = p3.x() - p1.x();
    double dy = p3.y() - p1.y();

    // Check for parallel lines
    if (qAbs(cross) < DEFAULT_TOLERANCE) {
        result.parallel = true;

        // Check if coincident (p3 lies on line through p1, p2)
        double crossCheck = dx * d1y - dy * d1x;
        result.coincident = (qAbs(crossCheck) < DEFAULT_TOLERANCE);

        return result;
    }

    // Compute parameters
    result.t1 = (dx * d2y - dy * d2x) / cross;
    result.t2 = (dx * d1y - dy * d1x) / cross;

    // Compute intersection point
    result.point = QPointF(p1.x() + result.t1 * d1x, p1.y() + result.t1 * d1y);
    result.intersects = true;

    // Check if within segments
    result.withinSegment1 = (result.t1 >= 0.0 && result.t1 <= 1.0);
    result.withinSegment2 = (result.t2 >= 0.0 && result.t2 <= 1.0);

    return result;
}

LineLineIntersection infiniteLineIntersection(
    const QPointF& p1, const QPointF& p2,
    const QPointF& p3, const QPointF& p4)
{
    // Same as line-line but don't care about segment bounds
    return lineLineIntersection(p1, p2, p3, p4);
}

// =====================================================================
//  Line-Circle Intersection
// =====================================================================

LineCircleIntersection lineCircleIntersection(
    const QPointF& lineStart, const QPointF& lineEnd,
    const QPointF& center, double radius)
{
    LineCircleIntersection result;

    // Direction vector
    double dx = lineEnd.x() - lineStart.x();
    double dy = lineEnd.y() - lineStart.y();
    double len = qSqrt(dx * dx + dy * dy);

    if (len < DEFAULT_TOLERANCE) {
        // Degenerate line
        return result;
    }

    // Vector from line start to circle center
    double fx = lineStart.x() - center.x();
    double fy = lineStart.y() - center.y();

    // Quadratic coefficients: at² + bt + c = 0
    double a = dx * dx + dy * dy;
    double b = 2.0 * (fx * dx + fy * dy);
    double c = fx * fx + fy * fy - radius * radius;

    double discriminant = b * b - 4.0 * a * c;

    if (discriminant < 0) {
        // No intersection
        return result;
    }

    discriminant = qSqrt(discriminant);

    // Two solutions (may be the same if tangent)
    double t1 = (-b - discriminant) / (2.0 * a);
    double t2 = (-b + discriminant) / (2.0 * a);

    result.t1 = t1;
    result.t2 = t2;

    result.point1 = QPointF(lineStart.x() + t1 * dx, lineStart.y() + t1 * dy);
    result.point1InSegment = (t1 >= 0.0 && t1 <= 1.0);

    if (qAbs(discriminant) < DEFAULT_TOLERANCE) {
        // Tangent - single intersection
        result.count = 1;
    } else {
        result.count = 2;
        result.point2 = QPointF(lineStart.x() + t2 * dx, lineStart.y() + t2 * dy);
        result.point2InSegment = (t2 >= 0.0 && t2 <= 1.0);
    }

    return result;
}

LineCircleIntersection infiniteLineCircleIntersection(
    const QPointF& linePoint1, const QPointF& linePoint2,
    const QPointF& center, double radius)
{
    // Same calculation, ignore segment flags
    return lineCircleIntersection(linePoint1, linePoint2, center, radius);
}

// =====================================================================
//  Circle-Circle Intersection
// =====================================================================

CircleCircleIntersection circleCircleIntersection(
    const QPointF& center1, double radius1,
    const QPointF& center2, double radius2)
{
    CircleCircleIntersection result;

    // Distance between centers
    double dx = center2.x() - center1.x();
    double dy = center2.y() - center1.y();
    double d = qSqrt(dx * dx + dy * dy);

    // Check for coincident circles
    if (d < DEFAULT_TOLERANCE && qAbs(radius1 - radius2) < DEFAULT_TOLERANCE) {
        result.coincident = true;
        return result;
    }

    // Check for no intersection (too far apart)
    if (d > radius1 + radius2 + DEFAULT_TOLERANCE) {
        return result;
    }

    // Check for no intersection (one inside the other)
    if (d < qAbs(radius1 - radius2) - DEFAULT_TOLERANCE) {
        result.internal = true;
        return result;
    }

    // Calculate intersection points
    double a = (radius1 * radius1 - radius2 * radius2 + d * d) / (2.0 * d);
    double h2 = radius1 * radius1 - a * a;

    if (h2 < 0) h2 = 0;  // Numerical stability
    double h = qSqrt(h2);

    // Point on line between centers
    double px = center1.x() + a * dx / d;
    double py = center1.y() + a * dy / d;

    // Perpendicular offset
    double offX = h * dy / d;
    double offY = h * dx / d;

    result.point1 = QPointF(px + offX, py - offY);

    if (h < DEFAULT_TOLERANCE) {
        // Tangent - single intersection
        result.count = 1;
    } else {
        result.count = 2;
        result.point2 = QPointF(px - offX, py + offY);
    }

    return result;
}

// =====================================================================
//  Line-Arc Intersection
// =====================================================================

LineArcIntersection lineArcIntersection(
    const QPointF& lineStart, const QPointF& lineEnd,
    const Arc& arc)
{
    LineArcIntersection result;

    // First get line-circle intersections
    LineCircleIntersection lci = lineCircleIntersection(
        lineStart, lineEnd, arc.center, arc.radius);

    if (lci.count == 0) {
        return result;
    }

    // Check if intersection points are on the arc
    auto checkPoint = [&arc](const QPointF& point) -> bool {
        double angle = qRadiansToDegrees(qAtan2(
            point.y() - arc.center.y(),
            point.x() - arc.center.x()));
        return arc.containsAngle(angle);
    };

    if (lci.count >= 1) {
        result.point1 = lci.point1;
        result.t1 = lci.t1;
        result.point1InSegment = lci.point1InSegment;
        result.point1OnArc = checkPoint(lci.point1);
        if (result.point1OnArc) {
            result.count = 1;
        }
    }

    if (lci.count >= 2) {
        result.point2 = lci.point2;
        result.t2 = lci.t2;
        result.point2InSegment = lci.point2InSegment;
        result.point2OnArc = checkPoint(lci.point2);
        if (result.point2OnArc) {
            if (result.count == 0) {
                // Swap to make point1 the valid one
                std::swap(result.point1, result.point2);
                std::swap(result.t1, result.t2);
                std::swap(result.point1InSegment, result.point2InSegment);
                std::swap(result.point1OnArc, result.point2OnArc);
            }
            result.count++;
        }
    }

    return result;
}

// =====================================================================
//  Arc-Arc Intersection
// =====================================================================

CircleCircleIntersection arcArcIntersection(const Arc& arc1, const Arc& arc2)
{
    CircleCircleIntersection result = circleCircleIntersection(
        arc1.center, arc1.radius, arc2.center, arc2.radius);

    if (result.count == 0 || result.coincident) {
        return result;
    }

    // Filter by arc sweeps
    auto checkPoint = [](const Arc& arc, const QPointF& point) -> bool {
        double angle = qRadiansToDegrees(qAtan2(
            point.y() - arc.center.y(),
            point.x() - arc.center.x()));
        return arc.containsAngle(angle);
    };

    bool p1Valid = checkPoint(arc1, result.point1) && checkPoint(arc2, result.point1);
    bool p2Valid = (result.count >= 2) &&
                   checkPoint(arc1, result.point2) && checkPoint(arc2, result.point2);

    if (!p1Valid && !p2Valid) {
        result.count = 0;
    } else if (!p1Valid) {
        result.point1 = result.point2;
        result.count = 1;
    } else if (!p2Valid) {
        result.count = 1;
    }

    return result;
}

// =====================================================================
//  Closest Point Functions
// =====================================================================

QPointF closestPointOnLine(
    const QPointF& point,
    const QPointF& lineStart, const QPointF& lineEnd)
{
    double dx = lineEnd.x() - lineStart.x();
    double dy = lineEnd.y() - lineStart.y();
    double lenSq = dx * dx + dy * dy;

    if (lenSq < DEFAULT_TOLERANCE * DEFAULT_TOLERANCE) {
        return lineStart;  // Degenerate line
    }

    // Project point onto line
    double t = ((point.x() - lineStart.x()) * dx +
                (point.y() - lineStart.y()) * dy) / lenSq;

    // Clamp to segment
    t = qBound(0.0, t, 1.0);

    return QPointF(lineStart.x() + t * dx, lineStart.y() + t * dy);
}

QPointF closestPointOnCircle(
    const QPointF& point,
    const QPointF& center, double radius)
{
    double dx = point.x() - center.x();
    double dy = point.y() - center.y();
    double len = qSqrt(dx * dx + dy * dy);

    if (len < DEFAULT_TOLERANCE) {
        // Point at center - return arbitrary point on circle
        return QPointF(center.x() + radius, center.y());
    }

    return QPointF(
        center.x() + radius * dx / len,
        center.y() + radius * dy / len
    );
}

QPointF closestPointOnArc(const QPointF& point, const Arc& arc)
{
    // Get angle to point
    double angle = qRadiansToDegrees(qAtan2(
        point.y() - arc.center.y(),
        point.x() - arc.center.x()));

    if (arc.containsAngle(angle)) {
        // Point projects onto arc
        return closestPointOnCircle(point, arc.center, arc.radius);
    }

    // Point doesn't project onto arc - return nearest endpoint
    QPointF startPt = arc.startPoint();
    QPointF endPt = arc.endPoint();

    double distStart = QLineF(point, startPt).length();
    double distEnd = QLineF(point, endPt).length();

    return (distStart < distEnd) ? startPt : endPt;
}

// =====================================================================
//  Distance Functions
// =====================================================================

double pointToLineDistance(
    const QPointF& point,
    const QPointF& lineStart, const QPointF& lineEnd)
{
    QPointF closest = closestPointOnLine(point, lineStart, lineEnd);
    return QLineF(point, closest).length();
}

double pointToInfiniteLineDistance(
    const QPointF& point,
    const QPointF& linePoint1, const QPointF& linePoint2)
{
    double dx = linePoint2.x() - linePoint1.x();
    double dy = linePoint2.y() - linePoint1.y();
    double len = qSqrt(dx * dx + dy * dy);

    if (len < DEFAULT_TOLERANCE) {
        return QLineF(point, linePoint1).length();
    }

    // Cross product gives signed area of parallelogram
    double cross = (point.x() - linePoint1.x()) * dy -
                   (point.y() - linePoint1.y()) * dx;

    return qAbs(cross) / len;
}

double pointToCircleDistance(
    const QPointF& point,
    const QPointF& center, double radius)
{
    double distToCenter = QLineF(point, center).length();
    return qAbs(distToCenter - radius);
}

double pointToArcDistance(const QPointF& point, const Arc& arc)
{
    QPointF closest = closestPointOnArc(point, arc);
    return QLineF(point, closest).length();
}

// =====================================================================
//  Utility Functions
// =====================================================================

bool pointsCoincident(const QPointF& p1, const QPointF& p2, double tolerance)
{
    return QLineF(p1, p2).length() < tolerance;
}

bool pointOnLine(
    const QPointF& point,
    const QPointF& lineStart, const QPointF& lineEnd,
    double tolerance)
{
    return pointToLineDistance(point, lineStart, lineEnd) < tolerance;
}

bool pointOnCircle(
    const QPointF& point,
    const QPointF& center, double radius,
    double tolerance)
{
    return pointToCircleDistance(point, center, radius) < tolerance;
}

bool pointOnArc(const QPointF& point, const Arc& arc, double tolerance)
{
    // Check distance to circle
    if (pointToCircleDistance(point, arc.center, arc.radius) >= tolerance) {
        return false;
    }

    // Check if angle is within sweep
    double angle = qRadiansToDegrees(qAtan2(
        point.y() - arc.center.y(),
        point.x() - arc.center.x()));

    return arc.containsAngle(angle);
}

double normalizeAngle(double degrees)
{
    while (degrees < 0) degrees += 360.0;
    while (degrees >= 360.0) degrees -= 360.0;
    return degrees;
}

double normalizeAngleSigned(double degrees)
{
    while (degrees < -180.0) degrees += 360.0;
    while (degrees >= 180.0) degrees -= 360.0;
    return degrees;
}

}  // namespace geometry
}  // namespace hobbycad
