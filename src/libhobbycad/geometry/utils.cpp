// =====================================================================
//  src/libhobbycad/geometry/utils.cpp — Geometry utility functions
// =====================================================================
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include <hobbycad/geometry/utils.h>
#include <hobbycad/geometry/intersections.h>

namespace hobbycad {
namespace geometry {

// =====================================================================
//  Vector Operations
// =====================================================================

double dot(const QPointF& a, const QPointF& b)
{
    return a.x() * b.x() + a.y() * b.y();
}

double cross(const QPointF& a, const QPointF& b)
{
    return a.x() * b.y() - a.y() * b.x();
}

double length(const QPointF& v)
{
    return qSqrt(v.x() * v.x() + v.y() * v.y());
}

double lengthSquared(const QPointF& v)
{
    return v.x() * v.x() + v.y() * v.y();
}

QPointF normalize(const QPointF& v)
{
    double len = length(v);
    if (len < DEFAULT_TOLERANCE) {
        return QPointF(0, 0);
    }
    return QPointF(v.x() / len, v.y() / len);
}

QPointF perpendicular(const QPointF& v)
{
    return QPointF(-v.y(), v.x());
}

QPointF perpendicularCW(const QPointF& v)
{
    return QPointF(v.y(), -v.x());
}

QPointF lerp(const QPointF& a, const QPointF& b, double t)
{
    return QPointF(
        a.x() + t * (b.x() - a.x()),
        a.y() + t * (b.y() - a.y())
    );
}

// =====================================================================
//  Angle Operations
// =====================================================================

double vectorAngle(const QPointF& v)
{
    return qRadiansToDegrees(qAtan2(v.y(), v.x()));
}

double angleBetween(const QPointF& a, const QPointF& b)
{
    double lenA = length(a);
    double lenB = length(b);

    if (lenA < DEFAULT_TOLERANCE || lenB < DEFAULT_TOLERANCE) {
        return 0.0;
    }

    double cosAngle = dot(a, b) / (lenA * lenB);
    cosAngle = qBound(-1.0, cosAngle, 1.0);

    return qRadiansToDegrees(qAcos(cosAngle));
}

double signedAngleBetween(const QPointF& a, const QPointF& b)
{
    double angle = qRadiansToDegrees(qAtan2(b.y(), b.x()) - qAtan2(a.y(), a.x()));
    return normalizeAngleSigned(angle);
}

QPointF rotatePoint(const QPointF& point, double angleDegrees)
{
    double rad = qDegreesToRadians(angleDegrees);
    double c = qCos(rad);
    double s = qSin(rad);
    return QPointF(
        point.x() * c - point.y() * s,
        point.x() * s + point.y() * c
    );
}

QPointF rotatePointAround(const QPointF& point, const QPointF& center, double angleDegrees)
{
    QPointF rel = point - center;
    QPointF rotated = rotatePoint(rel, angleDegrees);
    return center + rotated;
}

// =====================================================================
//  Line Operations
// =====================================================================

double lineLength(const QPointF& p1, const QPointF& p2)
{
    return QLineF(p1, p2).length();
}

QPointF lineMidpoint(const QPointF& p1, const QPointF& p2)
{
    return QPointF((p1.x() + p2.x()) / 2.0, (p1.y() + p2.y()) / 2.0);
}

QPointF lineDirection(const QPointF& p1, const QPointF& p2)
{
    return normalize(p2 - p1);
}

QPointF pointOnLine(const QPointF& p1, const QPointF& p2, double t)
{
    return lerp(p1, p2, t);
}

double projectPointOnLine(
    const QPointF& point,
    const QPointF& lineStart, const QPointF& lineEnd)
{
    QPointF d = lineEnd - lineStart;
    double lenSq = lengthSquared(d);

    if (lenSq < DEFAULT_TOLERANCE * DEFAULT_TOLERANCE) {
        return 0.0;
    }

    return dot(point - lineStart, d) / lenSq;
}

bool linesParallel(
    const QPointF& p1, const QPointF& p2,
    const QPointF& p3, const QPointF& p4,
    double tolerance)
{
    QPointF d1 = p2 - p1;
    QPointF d2 = p4 - p3;

    double crossProd = cross(d1, d2);
    double len1 = length(d1);
    double len2 = length(d2);

    if (len1 < DEFAULT_TOLERANCE || len2 < DEFAULT_TOLERANCE) {
        return false;
    }

    return qAbs(crossProd) < tolerance * len1 * len2;
}

bool linesPerpendicular(
    const QPointF& p1, const QPointF& p2,
    const QPointF& p3, const QPointF& p4,
    double tolerance)
{
    QPointF d1 = p2 - p1;
    QPointF d2 = p4 - p3;

    double len1 = length(d1);
    double len2 = length(d2);

    if (len1 < DEFAULT_TOLERANCE || len2 < DEFAULT_TOLERANCE) {
        return false;
    }

    double dotProd = dot(d1, d2);
    return qAbs(dotProd) < tolerance * len1 * len2;
}

// =====================================================================
//  Arc Operations
// =====================================================================

std::optional<Arc> arcFromThreePoints(
    const QPointF& start, const QPointF& mid, const QPointF& end)
{
    // Find circumcenter of triangle formed by three points
    // Using perpendicular bisectors of two sides

    double ax = start.x(), ay = start.y();
    double bx = mid.x(), by = mid.y();
    double cx = end.x(), cy = end.y();

    double d = 2.0 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by));

    if (qAbs(d) < DEFAULT_TOLERANCE) {
        // Points are collinear
        return std::nullopt;
    }

    double ux = ((ax * ax + ay * ay) * (by - cy) +
                 (bx * bx + by * by) * (cy - ay) +
                 (cx * cx + cy * cy) * (ay - by)) / d;

    double uy = ((ax * ax + ay * ay) * (cx - bx) +
                 (bx * bx + by * by) * (ax - cx) +
                 (cx * cx + cy * cy) * (bx - ax)) / d;

    QPointF center(ux, uy);
    double radius = QLineF(center, start).length();

    // Calculate start and end angles
    double startAngle = qRadiansToDegrees(qAtan2(ay - uy, ax - ux));
    double midAngle = qRadiansToDegrees(qAtan2(by - uy, bx - ux));
    double endAngle = qRadiansToDegrees(qAtan2(cy - uy, cx - ux));

    // Determine sweep direction based on mid point
    // Normalize angles to [0, 360)
    startAngle = normalizeAngle(startAngle);
    midAngle = normalizeAngle(midAngle);
    endAngle = normalizeAngle(endAngle);

    double sweep1 = normalizeAngle(endAngle - startAngle);
    double sweep2 = sweep1 - 360.0;

    // Check which sweep contains the mid point
    double midCheck1 = normalizeAngle(midAngle - startAngle);
    double sweepAngle = (midCheck1 <= sweep1) ? sweep1 : sweep2;

    Arc arc;
    arc.center = center;
    arc.radius = radius;
    arc.startAngle = startAngle;
    arc.sweepAngle = sweepAngle;

    return arc;
}

Arc arcFromCenterAndEndpoints(
    const QPointF& center,
    const QPointF& start, const QPointF& end,
    bool sweepCCW)
{
    Arc arc;
    arc.center = center;
    arc.radius = QLineF(center, start).length();

    double startAngle = qRadiansToDegrees(qAtan2(
        start.y() - center.y(), start.x() - center.x()));
    double endAngle = qRadiansToDegrees(qAtan2(
        end.y() - center.y(), end.x() - center.x()));

    arc.startAngle = normalizeAngle(startAngle);
    double sweep = normalizeAngle(endAngle - startAngle);

    if (sweepCCW) {
        arc.sweepAngle = sweep;
    } else {
        arc.sweepAngle = sweep - 360.0;
    }

    return arc;
}

double arcLength(const Arc& arc)
{
    return qAbs(arc.radius * qDegreesToRadians(arc.sweepAngle));
}

QVector<Arc> splitArc(const Arc& arc, const QPointF& point)
{
    // Check if point is on arc
    double angle = qRadiansToDegrees(qAtan2(
        point.y() - arc.center.y(),
        point.x() - arc.center.x()));

    if (!arc.containsAngle(angle)) {
        return {};  // Point not on arc
    }

    // Create two arcs
    Arc arc1, arc2;
    arc1.center = arc2.center = arc.center;
    arc1.radius = arc2.radius = arc.radius;

    arc1.startAngle = arc.startAngle;
    arc1.sweepAngle = normalizeAngleSigned(angle - arc.startAngle);
    if (arc.sweepAngle < 0 && arc1.sweepAngle > 0) {
        arc1.sweepAngle -= 360.0;
    } else if (arc.sweepAngle > 0 && arc1.sweepAngle < 0) {
        arc1.sweepAngle += 360.0;
    }

    arc2.startAngle = angle;
    arc2.sweepAngle = arc.sweepAngle - arc1.sweepAngle;

    return {arc1, arc2};
}

// =====================================================================
//  Polygon Operations
// =====================================================================

double polygonArea(const QVector<QPointF>& polygon)
{
    if (polygon.size() < 3) return 0.0;

    double area = 0.0;
    int n = polygon.size();

    for (int i = 0; i < n; ++i) {
        int j = (i + 1) % n;
        area += polygon[i].x() * polygon[j].y();
        area -= polygon[j].x() * polygon[i].y();
    }

    return area / 2.0;
}

bool polygonIsCCW(const QVector<QPointF>& polygon)
{
    return polygonArea(polygon) > 0;
}

QVector<QPointF> reversePolygon(const QVector<QPointF>& polygon)
{
    QVector<QPointF> result = polygon;
    std::reverse(result.begin(), result.end());
    return result;
}

bool pointInPolygon(const QPointF& point, const QVector<QPointF>& polygon)
{
    if (polygon.size() < 3) return false;

    // Ray casting algorithm
    bool inside = false;
    int n = polygon.size();

    for (int i = 0, j = n - 1; i < n; j = i++) {
        double xi = polygon[i].x(), yi = polygon[i].y();
        double xj = polygon[j].x(), yj = polygon[j].y();

        if (((yi > point.y()) != (yj > point.y())) &&
            (point.x() < (xj - xi) * (point.y() - yi) / (yj - yi) + xi)) {
            inside = !inside;
        }
    }

    return inside;
}

QPointF polygonCentroid(const QVector<QPointF>& polygon)
{
    if (polygon.isEmpty()) return QPointF();
    if (polygon.size() == 1) return polygon[0];
    if (polygon.size() == 2) return lineMidpoint(polygon[0], polygon[1]);

    double cx = 0.0, cy = 0.0;
    double area = 0.0;
    int n = polygon.size();

    for (int i = 0; i < n; ++i) {
        int j = (i + 1) % n;
        double cross = polygon[i].x() * polygon[j].y() -
                       polygon[j].x() * polygon[i].y();
        area += cross;
        cx += (polygon[i].x() + polygon[j].x()) * cross;
        cy += (polygon[i].y() + polygon[j].y()) * cross;
    }

    area /= 2.0;

    if (qAbs(area) < DEFAULT_TOLERANCE) {
        // Degenerate polygon - return average of points
        for (const QPointF& p : polygon) {
            cx += p.x();
            cy += p.y();
        }
        return QPointF(cx / n, cy / n);
    }

    return QPointF(cx / (6.0 * area), cy / (6.0 * area));
}

BoundingBox polygonBounds(const QVector<QPointF>& polygon)
{
    BoundingBox bbox;
    for (const QPointF& p : polygon) {
        bbox.include(p);
    }
    return bbox;
}

// =====================================================================
//  Rectangle Operations
// =====================================================================

bool pointInRect(const QPointF& point, const QRectF& rect)
{
    return rect.contains(point);
}

bool lineIntersectsRect(const QPointF& p1, const QPointF& p2, const QRectF& rect)
{
    // Check if either endpoint is inside
    if (rect.contains(p1) || rect.contains(p2)) {
        return true;
    }

    // Check intersection with each edge
    QPointF corners[4] = {
        rect.topLeft(),
        rect.topRight(),
        rect.bottomRight(),
        rect.bottomLeft()
    };

    for (int i = 0; i < 4; ++i) {
        int j = (i + 1) % 4;
        LineLineIntersection result = lineLineIntersection(
            p1, p2, corners[i], corners[j]);
        if (result.intersects && result.withinSegment1 && result.withinSegment2) {
            return true;
        }
    }

    return false;
}

bool circleIntersectsRect(const QPointF& center, double radius, const QRectF& rect)
{
    // Find closest point on rectangle to circle center
    double closestX = qBound(rect.left(), center.x(), rect.right());
    double closestY = qBound(rect.top(), center.y(), rect.bottom());

    double dx = center.x() - closestX;
    double dy = center.y() - closestY;

    return (dx * dx + dy * dy) <= (radius * radius);
}

bool lineEnclosedByRect(const QPointF& p1, const QPointF& p2, const QRectF& rect)
{
    return rect.contains(p1) && rect.contains(p2);
}

bool circleEnclosedByRect(const QPointF& center, double radius, const QRectF& rect)
{
    return center.x() - radius >= rect.left() &&
           center.x() + radius <= rect.right() &&
           center.y() - radius >= rect.top() &&
           center.y() + radius <= rect.bottom();
}

// =====================================================================
//  Tangent Circle/Arc Construction
// =====================================================================

TangentCircleResult circleTangentToTwoLines(
    const QPointF& line1Start, const QPointF& line1End,
    const QPointF& line2Start, const QPointF& line2End,
    double radius,
    const QPointF& hint)
{
    TangentCircleResult result;

    // Find intersection of the two lines (extended to infinity)
    LineLineIntersection intersection = infiniteLineIntersection(
        line1Start, line1End, line2Start, line2End);

    if (!intersection.intersects || intersection.parallel) {
        // Lines are parallel - no inscribed circle possible with finite radius
        // (would need infinite radius)
        return result;
    }

    QPointF vertex = intersection.point;

    // Get normalized direction vectors for both lines
    QPointF dir1 = normalize(line1End - line1Start);
    QPointF dir2 = normalize(line2End - line2Start);

    // Calculate the angle bisector direction
    // There are two bisectors; we'll use hint to pick the right one
    QPointF bisector1 = normalize(dir1 + dir2);
    QPointF bisector2 = perpendicular(bisector1);

    // The center lies on the angle bisector at distance r/sin(half_angle)
    // where half_angle is half the angle between the lines
    double dotProd = dot(dir1, dir2);
    dotProd = qBound(-1.0, dotProd, 1.0);
    double fullAngle = qAcos(dotProd);  // Angle between lines (radians)
    double halfAngle = fullAngle / 2.0;

    if (qAbs(qSin(halfAngle)) < DEFAULT_TOLERANCE) {
        return result;  // Lines nearly parallel
    }

    double distFromVertex = radius / qSin(halfAngle);

    // Two possible centers (on each bisector direction)
    QPointF center1 = vertex + bisector1 * distFromVertex;
    QPointF center2 = vertex - bisector1 * distFromVertex;
    QPointF center3 = vertex + bisector2 * distFromVertex;
    QPointF center4 = vertex - bisector2 * distFromVertex;

    // Choose the center closest to the hint point
    QVector<QPointF> candidates = {center1, center2, center3, center4};
    QPointF bestCenter = center1;
    double bestDist = lengthSquared(center1 - hint);

    for (const QPointF& c : candidates) {
        double d = lengthSquared(c - hint);
        if (d < bestDist) {
            bestDist = d;
            bestCenter = c;
        }
    }

    result.valid = true;
    result.center = bestCenter;
    result.radius = radius;

    return result;
}

TangentCircleResult circleTangentToThreeLines(
    const QPointF& line1Start, const QPointF& line1End,
    const QPointF& line2Start, const QPointF& line2End,
    const QPointF& line3Start, const QPointF& line3End)
{
    TangentCircleResult result;

    // Find vertices of the triangle (intersections of line pairs)
    LineLineIntersection int12 = infiniteLineIntersection(
        line1Start, line1End, line2Start, line2End);
    LineLineIntersection int23 = infiniteLineIntersection(
        line2Start, line2End, line3Start, line3End);
    LineLineIntersection int31 = infiniteLineIntersection(
        line3Start, line3End, line1Start, line1End);

    if (!int12.intersects || !int23.intersects || !int31.intersects) {
        return result;  // Lines don't form a proper triangle
    }

    QPointF A = int12.point;  // Vertex between line1 and line2
    QPointF B = int23.point;  // Vertex between line2 and line3
    QPointF C = int31.point;  // Vertex between line3 and line1

    // Calculate incenter using angle bisector intersection
    // Incenter = (a*A + b*B + c*C) / (a + b + c)
    // where a, b, c are the lengths of opposite sides

    double a = lineLength(B, C);  // Side opposite to A
    double b = lineLength(C, A);  // Side opposite to B
    double c = lineLength(A, B);  // Side opposite to C

    double perimeter = a + b + c;
    if (perimeter < DEFAULT_TOLERANCE) {
        return result;
    }

    QPointF incenter(
        (a * A.x() + b * B.x() + c * C.x()) / perimeter,
        (a * A.y() + b * B.y() + c * C.y()) / perimeter
    );

    // Calculate inradius = area / semi-perimeter
    double area = qAbs(polygonArea({A, B, C}));
    double inradius = area / (perimeter / 2.0);

    result.valid = true;
    result.center = incenter;
    result.radius = inradius;

    return result;
}

TangentArcResult arcTangentToLine(
    const QPointF& lineStart, const QPointF& lineEnd,
    const QPointF& tangentPoint,
    const QPointF& endPoint)
{
    TangentArcResult result;

    // Direction of line
    QPointF lineDir = normalize(lineEnd - lineStart);

    // Perpendicular to line at tangent point
    QPointF perpDir = perpendicular(lineDir);

    // The center lies on the perpendicular through tangentPoint
    // The center is equidistant from tangentPoint and endPoint
    // So the center lies on the perpendicular bisector of tangentPoint-endPoint

    QPointF midpoint = lineMidpoint(tangentPoint, endPoint);
    QPointF chordDir = normalize(endPoint - tangentPoint);
    QPointF bisectorDir = perpendicular(chordDir);

    // Find intersection of:
    // 1. Line through tangentPoint with direction perpDir
    // 2. Line through midpoint with direction bisectorDir
    LineLineIntersection centerIntersection = infiniteLineIntersection(
        tangentPoint, tangentPoint + perpDir,
        midpoint, midpoint + bisectorDir);

    if (!centerIntersection.intersects) {
        return result;  // Lines are parallel (shouldn't happen normally)
    }

    QPointF center = centerIntersection.point;
    double radius = lineLength(center, tangentPoint);

    // Calculate angles
    double startAngle = qRadiansToDegrees(qAtan2(
        tangentPoint.y() - center.y(),
        tangentPoint.x() - center.x()));
    double endAngle = qRadiansToDegrees(qAtan2(
        endPoint.y() - center.y(),
        endPoint.x() - center.x()));

    // Determine sweep direction based on tangent direction
    // The arc should be tangent to the line, meaning the tangent at startAngle
    // should be parallel to lineDir
    QPointF tangentAtStart = perpendicular(normalize(tangentPoint - center));

    // Check if we need to flip the sweep direction
    double sweepAngle = normalizeAngleSigned(endAngle - startAngle);

    // Check if tangent direction matches line direction
    if (dot(tangentAtStart, lineDir) < 0) {
        // Sweep in the other direction
        if (sweepAngle > 0) {
            sweepAngle -= 360.0;
        } else {
            sweepAngle += 360.0;
        }
    }

    result.valid = true;
    result.center = center;
    result.radius = radius;
    result.startAngle = normalizeAngle(startAngle);
    result.sweepAngle = sweepAngle;

    return result;
}

TangentArcResult filletArc(
    const QPointF& line1Start, const QPointF& line1End,
    const QPointF& line2Start, const QPointF& line2End,
    double radius)
{
    TangentArcResult result;

    // Find intersection point (corner vertex)
    LineLineIntersection intersection = infiniteLineIntersection(
        line1Start, line1End, line2Start, line2End);

    if (!intersection.intersects || intersection.parallel) {
        return result;
    }

    QPointF vertex = intersection.point;

    // Get direction vectors pointing away from vertex
    QPointF dir1 = normalize(line1End - line1Start);
    QPointF dir2 = normalize(line2End - line2Start);

    // Ensure directions point away from vertex
    if (dot(dir1, vertex - line1Start) < 0) dir1 = -dir1;
    if (dot(dir2, vertex - line2Start) < 0) dir2 = -dir2;

    // Invert to point away from vertex
    dir1 = -dir1;
    dir2 = -dir2;

    // Calculate half angle between lines
    double dotProd = dot(dir1, dir2);
    dotProd = qBound(-1.0, dotProd, 1.0);
    double halfAngle = qAcos(dotProd) / 2.0;

    if (qAbs(qSin(halfAngle)) < DEFAULT_TOLERANCE) {
        return result;  // Lines nearly parallel
    }

    // Distance from vertex to tangent points
    double tangentDist = radius / qTan(halfAngle);

    // Distance from vertex to center
    double centerDist = radius / qSin(halfAngle);

    // Angle bisector direction
    QPointF bisector = normalize(dir1 + dir2);

    // Center of fillet arc
    QPointF center = vertex + bisector * centerDist;

    // Tangent points on each line
    QPointF tangent1 = vertex + dir1 * tangentDist;
    QPointF tangent2 = vertex + dir2 * tangentDist;

    // Calculate arc angles
    double startAngle = qRadiansToDegrees(qAtan2(
        tangent1.y() - center.y(),
        tangent1.x() - center.x()));
    double endAngle = qRadiansToDegrees(qAtan2(
        tangent2.y() - center.y(),
        tangent2.x() - center.x()));

    // The fillet arc should go the "short way" between tangent points
    double sweepAngle = normalizeAngleSigned(endAngle - startAngle);

    // Ensure we take the shorter path (< 180 degrees for a fillet)
    if (qAbs(sweepAngle) > 180.0) {
        if (sweepAngle > 0) {
            sweepAngle -= 360.0;
        } else {
            sweepAngle += 360.0;
        }
    }

    result.valid = true;
    result.center = center;
    result.radius = radius;
    result.startAngle = normalizeAngle(startAngle);
    result.sweepAngle = sweepAngle;

    return result;
}

}  // namespace geometry
}  // namespace hobbycad
