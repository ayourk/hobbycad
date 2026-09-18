// =====================================================================
//  src/libhobbycad/geometry/utils.cpp — Geometry utility functions
// =====================================================================
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include <hobbycad/geometry/utils.h>
#include <hobbycad/units.h>
#include <hobbycad/geometry/intersections.h>

#include <algorithm>
#include <cmath>

#include <hobbycad/math_constants.h>

namespace hobbycad {
namespace geometry {

// =====================================================================
//  Vector Operations
// =====================================================================

double dot(const Point2D& a, const Point2D& b)
{
    return a.x * b.x + a.y * b.y;
}

double cross(const Point2D& a, const Point2D& b)
{
    return a.x * b.y - a.y * b.x;
}

double length(const Point2D& v)
{
    return std::sqrt(v.x * v.x + v.y * v.y);
}

double lengthSquared(const Point2D& v)
{
    return v.x * v.x + v.y * v.y;
}

Point2D normalize(const Point2D& v)
{
    double len = length(v);
    if (len < DEFAULT_TOLERANCE) {
        return Point2D(0, 0);
    }
    return Point2D(v.x / len, v.y / len);
}

Point2D perpendicular(const Point2D& v)
{
    return Point2D(-v.y, v.x);
}

Point2D perpendicularCW(const Point2D& v)
{
    return Point2D(v.y, -v.x);
}

Point2D lerp(const Point2D& a, const Point2D& b, double t)
{
    return Point2D(
        a.x + t * (b.x - a.x),
        a.y + t * (b.y - a.y)
    );
}

// =====================================================================
//  Angle Operations
// =====================================================================

double vectorAngle(const Point2D& v)
{
    return radiansToDegrees(std::atan2(v.y, v.x));
}

double angleBetween(const Point2D& a, const Point2D& b)
{
    double lenA = length(a);
    double lenB = length(b);

    if (lenA < DEFAULT_TOLERANCE || lenB < DEFAULT_TOLERANCE) {
        return 0.0;
    }

    double cosAngle = dot(a, b) / (lenA * lenB);
    cosAngle = std::clamp(cosAngle, -1.0, 1.0);

    return radiansToDegrees(std::acos(cosAngle));
}

double signedAngleBetween(const Point2D& a, const Point2D& b)
{
    double angle = radiansToDegrees(std::atan2(b.y, b.x) - std::atan2(a.y, a.x));
    return normalizeAngleSigned(angle);
}

Point2D rotatePoint(const Point2D& point, double angleDegrees)
{
    double rad = degreesToRadians(angleDegrees);
    double c = std::cos(rad);
    double s = std::sin(rad);
    return Point2D(
        point.x * c - point.y * s,
        point.x * s + point.y * c
    );
}

Point2D rotatePointAround(const Point2D& point, const Point2D& center, double angleDegrees)
{
    Point2D rel = point - center;
    Point2D rotated = rotatePoint(rel, angleDegrees);
    return center + rotated;
}

// =====================================================================
//  Line Operations
// =====================================================================

double lineLength(const Point2D& p1, const Point2D& p2)
{
    return std::hypot(p2.x - p1.x, p2.y - p1.y);
}

Point2D snapToGrid(const Point2D& p, double spacing)
{
    if (spacing <= 0.0) return p;
    return Point2D(std::round(p.x / spacing) * spacing, std::round(p.y / spacing) * spacing);
}

Point2D polarPoint(const Point2D& center, double distance, double angleRad)
{
    return Point2D(center.x + distance * std::cos(angleRad),
                   center.y + distance * std::sin(angleRad));
}

Point2D lineMidpoint(const Point2D& p1, const Point2D& p2)
{
    return Point2D((p1.x + p2.x) / 2.0, (p1.y + p2.y) / 2.0);
}

double ellipseCircumference(double a, double b)
{
    if (!isPositiveLength(a + b)) return 0.0;
    const double h = std::pow((a - b) / (a + b), 2);
    return M_PI * (a + b) * (1.0 + 3.0 * h / (10.0 + std::sqrt(4.0 - 3.0 * h)));
}

Point2D lineDirection(const Point2D& p1, const Point2D& p2)
{
    return normalize(p2 - p1);
}

Point2D pointOnLine(const Point2D& p1, const Point2D& p2, double t)
{
    return lerp(p1, p2, t);
}

double projectPointOnLine(
    const Point2D& point,
    const Point2D& lineStart, const Point2D& lineEnd)
{
    Point2D d = lineEnd - lineStart;
    double lenSq = lengthSquared(d);

    if (lenSq < DEFAULT_TOLERANCE * DEFAULT_TOLERANCE) {
        return 0.0;
    }

    return dot(point - lineStart, d) / lenSq;
}

bool linesParallel(
    const Point2D& p1, const Point2D& p2,
    const Point2D& p3, const Point2D& p4,
    double tolerance)
{
    Point2D d1 = p2 - p1;
    Point2D d2 = p4 - p3;

    double crossProd = cross(d1, d2);
    double len1 = length(d1);
    double len2 = length(d2);

    if (len1 < DEFAULT_TOLERANCE || len2 < DEFAULT_TOLERANCE) {
        return false;
    }

    return std::abs(crossProd) < tolerance * len1 * len2;
}

bool linesPerpendicular(
    const Point2D& p1, const Point2D& p2,
    const Point2D& p3, const Point2D& p4,
    double tolerance)
{
    Point2D d1 = p2 - p1;
    Point2D d2 = p4 - p3;

    double len1 = length(d1);
    double len2 = length(d2);

    if (len1 < DEFAULT_TOLERANCE || len2 < DEFAULT_TOLERANCE) {
        return false;
    }

    double dotProd = dot(d1, d2);
    return std::abs(dotProd) < tolerance * len1 * len2;
}

// =====================================================================
//  Ray Operations
// =====================================================================

Point2D projectPointOntoRay(
    const Point2D& point,
    const Point2D& rayOrigin,
    const Point2D& rayDirection)
{
    Point2D dir = normalize(rayDirection);
    if (lengthSquared(dir) < DEFAULT_TOLERANCE) {
        return rayOrigin;
    }

    Point2D toPoint = point - rayOrigin;
    double projection = dot(toPoint, dir);
    return rayOrigin + dir * projection;
}

double distanceFromRay(
    const Point2D& point,
    const Point2D& rayOrigin,
    const Point2D& rayDirection)
{
    Point2D projected = projectPointOntoRay(point, rayOrigin, rayDirection);
    return length(point - projected);
}

bool pointOnRay(
    const Point2D& point,
    const Point2D& rayOrigin,
    const Point2D& rayDirection,
    double tolerance)
{
    return distanceFromRay(point, rayOrigin, rayDirection) < tolerance;
}

Point2D snapToAngleIncrement(
    const Point2D& origin,
    const Point2D& target,
    double incrementDegrees)
{
    double snappedAngle;
    return snapToAngleIncrementWithAngle(origin, target, incrementDegrees, snappedAngle);
}

Point2D snapToAngleIncrementWithAngle(
    const Point2D& origin,
    const Point2D& target,
    double incrementDegrees,
    double& snappedAngle)
{
    Point2D delta = target - origin;
    double distance = length(delta);

    if (distance < DEFAULT_TOLERANCE) {
        snappedAngle = 0.0;
        return target;
    }

    // Calculate current angle in degrees
    double angle = radiansToDegrees(std::atan2(delta.y, delta.x));

    // Snap to nearest increment
    snappedAngle = std::round(angle / incrementDegrees) * incrementDegrees;

    // Calculate new position at snapped angle
    double snappedRad = degreesToRadians(snappedAngle);
    return origin + Point2D(distance * std::cos(snappedRad), distance * std::sin(snappedRad));
}

// =====================================================================
//  Arc Operations
// =====================================================================

std::optional<Arc> arcFromThreePoints(
    const Point2D& start, const Point2D& mid, const Point2D& end)
{
    // Find circumcenter of triangle formed by three points
    // Using perpendicular bisectors of two sides

    double ax = start.x, ay = start.y;
    double bx = mid.x, by = mid.y;
    double cx = end.x, cy = end.y;

    double d = 2.0 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by));

    if (std::abs(d) < DEFAULT_TOLERANCE) {
        // Points are collinear
        return std::nullopt;
    }

    double ux = ((ax * ax + ay * ay) * (by - cy) +
                 (bx * bx + by * by) * (cy - ay) +
                 (cx * cx + cy * cy) * (ay - by)) / d;

    double uy = ((ax * ax + ay * ay) * (cx - bx) +
                 (bx * bx + by * by) * (ax - cx) +
                 (cx * cx + cy * cy) * (bx - ax)) / d;

    Point2D center(ux, uy);
    double radius = std::hypot(start.x - ux, start.y - uy);

    // Calculate start and end angles
    double startAngle = radiansToDegrees(std::atan2(ay - uy, ax - ux));
    double midAngle = radiansToDegrees(std::atan2(by - uy, bx - ux));
    double endAngle = radiansToDegrees(std::atan2(cy - uy, cx - ux));

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
    const Point2D& center,
    const Point2D& start, const Point2D& end,
    bool sweepCCW)
{
    Arc arc;
    arc.center = center;
    arc.radius = std::hypot(start.x - center.x, start.y - center.y);

    double startAngle = radiansToDegrees(std::atan2(
        start.y - center.y, start.x - center.x));
    double endAngle = radiansToDegrees(std::atan2(
        end.y - center.y, end.x - center.x));

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
    return std::abs(arc.radius * (degreesToRadians(arc.sweepAngle)));
}

std::vector<Arc> splitArc(const Arc& arc, const Point2D& point)
{
    // Check if point is on arc
    double angle = radiansToDegrees(std::atan2(
        point.y - arc.center.y,
        point.x - arc.center.x));

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

double polygonArea(const std::vector<Point2D>& polygon)
{
    if (polygon.size() < 3) return 0.0;

    double area = 0.0;
    int n = polygon.size();

    for (int i = 0; i < n; ++i) {
        int j = (i + 1) % n;
        area += polygon[i].x * polygon[j].y;
        area -= polygon[j].x * polygon[i].y;
    }

    return area / 2.0;
}

bool polygonIsCCW(const std::vector<Point2D>& polygon)
{
    return polygonArea(polygon) > 0;
}

std::vector<Point2D> reversePolygon(const std::vector<Point2D>& polygon)
{
    std::vector<Point2D> result = polygon;
    std::reverse(result.begin(), result.end());
    return result;
}

bool pointInPolygon(const Point2D& point, const std::vector<Point2D>& polygon)
{
    if (polygon.size() < 3) return false;

    // Ray casting algorithm
    bool inside = false;
    int n = polygon.size();

    for (int i = 0, j = n - 1; i < n; j = i++) {
        double xi = polygon[i].x, yi = polygon[i].y;
        double xj = polygon[j].x, yj = polygon[j].y;

        if (((yi > point.y) != (yj > point.y)) &&
            (point.x < (xj - xi) * (point.y - yi) / (yj - yi) + xi)) {
            inside = !inside;
        }
    }

    return inside;
}

Point2D polygonCentroid(const std::vector<Point2D>& polygon)
{
    if (polygon.empty()) return Point2D();
    if (polygon.size() == 1) return polygon[0];
    if (polygon.size() == 2) return lineMidpoint(polygon[0], polygon[1]);

    double cx = 0.0, cy = 0.0;
    double area = 0.0;
    int n = polygon.size();

    for (int i = 0; i < n; ++i) {
        int j = (i + 1) % n;
        double cross = polygon[i].x * polygon[j].y -
                       polygon[j].x * polygon[i].y;
        area += cross;
        cx += (polygon[i].x + polygon[j].x) * cross;
        cy += (polygon[i].y + polygon[j].y) * cross;
    }

    area /= 2.0;

    if (std::abs(area) < DEFAULT_TOLERANCE) {
        // Degenerate polygon - return average of points
        for (const Point2D& p : polygon) {
            cx += p.x;
            cy += p.y;
        }
        return Point2D(cx / n, cy / n);
    }

    return Point2D(cx / (6.0 * area), cy / (6.0 * area));
}

BoundingBox polygonBounds(const std::vector<Point2D>& polygon)
{
    BoundingBox bbox;
    for (const Point2D& p : polygon) {
        bbox.include(p);
    }
    return bbox;
}

// =====================================================================
//  Rectangle Operations
// =====================================================================

bool pointInRect(const Point2D& point, const Rect2D& rect)
{
    return rect.contains(point);
}

bool lineIntersectsRect(const Point2D& p1, const Point2D& p2, const Rect2D& rect)
{
    // Check if either endpoint is inside
    if (rect.contains(p1) || rect.contains(p2)) {
        return true;
    }

    // Check intersection with each edge
    Point2D corners[4] = {
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

bool circleIntersectsRect(const Point2D& center, double radius, const Rect2D& rect)
{
    // Find closest point on rectangle to circle center
    double closestX = std::clamp(center.x, rect.left(), rect.right());
    double closestY = std::clamp(center.y, rect.top(), rect.bottom());

    double dx = center.x - closestX;
    double dy = center.y - closestY;

    return (dx * dx + dy * dy) <= (radius * radius);
}

bool lineEnclosedByRect(const Point2D& p1, const Point2D& p2, const Rect2D& rect)
{
    return rect.contains(p1) && rect.contains(p2);
}

bool circleEnclosedByRect(const Point2D& center, double radius, const Rect2D& rect)
{
    return center.x - radius >= rect.left() &&
           center.x + radius <= rect.right() &&
           center.y - radius >= rect.top() &&
           center.y + radius <= rect.bottom();
}

// =====================================================================
//  Tangent Circle/Arc Construction
// =====================================================================

TangentCircleResult circleTangentToTwoLines(
    const Point2D& line1Start, const Point2D& line1End,
    const Point2D& line2Start, const Point2D& line2End,
    double radius,
    const Point2D& hint)
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

    Point2D vertex = intersection.point;

    // Get normalized direction vectors for both lines
    Point2D dir1 = normalize(line1End - line1Start);
    Point2D dir2 = normalize(line2End - line2Start);

    // Calculate the angle bisector direction
    // There are two bisectors; we'll use hint to pick the right one
    Point2D bisector1 = normalize(dir1 + dir2);
    Point2D bisector2 = perpendicular(bisector1);

    // The center lies on the angle bisector at distance r/sin(half_angle)
    // where half_angle is half the angle between the lines
    double dotProd = dot(dir1, dir2);
    dotProd = std::clamp(dotProd, -1.0, 1.0);
    double fullAngle = std::acos(dotProd);  // Angle between lines (radians)
    double halfAngle = fullAngle / 2.0;

    if (std::abs(std::sin(halfAngle)) < DEFAULT_TOLERANCE) {
        return result;  // Lines nearly parallel
    }

    double distFromVertex = radius / std::sin(halfAngle);

    // Two possible centers (on each bisector direction)
    Point2D center1 = vertex + bisector1 * distFromVertex;
    Point2D center2 = vertex - bisector1 * distFromVertex;
    Point2D center3 = vertex + bisector2 * distFromVertex;
    Point2D center4 = vertex - bisector2 * distFromVertex;

    // Choose the center closest to the hint point
    std::vector<Point2D> candidates = {center1, center2, center3, center4};
    Point2D bestCenter = center1;
    double bestDist = lengthSquared(center1 - hint);

    for (const Point2D& c : candidates) {
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
    const Point2D& line1Start, const Point2D& line1End,
    const Point2D& line2Start, const Point2D& line2End,
    const Point2D& line3Start, const Point2D& line3End)
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

    Point2D A = int12.point;  // Vertex between line1 and line2
    Point2D B = int23.point;  // Vertex between line2 and line3
    Point2D C = int31.point;  // Vertex between line3 and line1

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

    Point2D incenter(
        (a * A.x + b * B.x + c * C.x) / perimeter,
        (a * A.y + b * B.y + c * C.y) / perimeter
    );

    // Calculate inradius = area / semi-perimeter
    double area = std::abs(polygonArea({A, B, C}));
    double inradius = area / (perimeter / 2.0);

    result.valid = true;
    result.center = incenter;
    result.radius = inradius;

    return result;
}

TangentArcResult arcTangentToLine(
    const Point2D& lineStart, const Point2D& lineEnd,
    const Point2D& tangentPoint,
    const Point2D& endPoint)
{
    TangentArcResult result;

    // Direction of line
    Point2D lineDir = normalize(lineEnd - lineStart);

    // Perpendicular to line at tangent point
    Point2D perpDir = perpendicular(lineDir);

    // The center lies on the perpendicular through tangentPoint
    // The center is equidistant from tangentPoint and endPoint
    // So the center lies on the perpendicular bisector of tangentPoint-endPoint

    Point2D midpoint = lineMidpoint(tangentPoint, endPoint);
    Point2D chordDir = normalize(endPoint - tangentPoint);
    Point2D bisectorDir = perpendicular(chordDir);

    // Find intersection of:
    // 1. Line through tangentPoint with direction perpDir
    // 2. Line through midpoint with direction bisectorDir
    LineLineIntersection centerIntersection = infiniteLineIntersection(
        tangentPoint, tangentPoint + perpDir,
        midpoint, midpoint + bisectorDir);

    if (!centerIntersection.intersects) {
        return result;  // Lines are parallel (shouldn't happen normally)
    }

    Point2D center = centerIntersection.point;
    double radius = lineLength(center, tangentPoint);

    // Calculate angles
    double startAngle = radiansToDegrees(std::atan2(
        tangentPoint.y - center.y,
        tangentPoint.x - center.x));
    double endAngle = radiansToDegrees(std::atan2(
        endPoint.y - center.y,
        endPoint.x - center.x));

    // Determine sweep direction based on tangent direction
    // The arc should be tangent to the line, meaning the tangent at startAngle
    // should be parallel to lineDir
    Point2D tangentAtStart = perpendicular(normalize(tangentPoint - center));

    // Check if we need to flip the sweep direction
    double sweepAngle = normalizeAngleSigned(endAngle - startAngle);

    // Check if tangent direction matches line direction
    if (dot(tangentAtStart, lineDir) < 0) {
        // Sweep in the other direction
        sweepAngle = oppositeSweepDeg(sweepAngle);
    }

    result.valid = true;
    result.center = center;
    result.radius = radius;
    result.startAngle = normalizeAngle(startAngle);
    result.sweepAngle = sweepAngle;

    return result;
}

TangentArcResult filletArc(
    const Point2D& line1Start, const Point2D& line1End,
    const Point2D& line2Start, const Point2D& line2End,
    double radius)
{
    TangentArcResult result;

    // Find intersection point (corner vertex)
    LineLineIntersection intersection = infiniteLineIntersection(
        line1Start, line1End, line2Start, line2End);

    if (!intersection.intersects || intersection.parallel) {
        return result;
    }

    Point2D vertex = intersection.point;

    // Get direction vectors pointing away from vertex
    Point2D dir1 = normalize(line1End - line1Start);
    Point2D dir2 = normalize(line2End - line2Start);

    // Ensure directions point away from vertex
    if (dot(dir1, vertex - line1Start) < 0) dir1 = -dir1;
    if (dot(dir2, vertex - line2Start) < 0) dir2 = -dir2;

    // Invert to point away from vertex
    dir1 = -dir1;
    dir2 = -dir2;

    // Calculate half angle between lines
    double dotProd = dot(dir1, dir2);
    dotProd = std::clamp(dotProd, -1.0, 1.0);
    double halfAngle = std::acos(dotProd) / 2.0;

    if (std::abs(std::sin(halfAngle)) < DEFAULT_TOLERANCE) {
        return result;  // Lines nearly parallel
    }

    // Distance from vertex to tangent points
    double tangentDist = radius / std::tan(halfAngle);

    // Distance from vertex to center
    double centerDist = radius / std::sin(halfAngle);

    // Angle bisector direction
    Point2D bisector = normalize(dir1 + dir2);

    // Center of fillet arc
    Point2D center = vertex + bisector * centerDist;

    // Tangent points on each line
    Point2D tangent1 = vertex + dir1 * tangentDist;
    Point2D tangent2 = vertex + dir2 * tangentDist;

    // Calculate arc angles
    double startAngle = radiansToDegrees(std::atan2(
        tangent1.y - center.y,
        tangent1.x - center.x));
    double endAngle = radiansToDegrees(std::atan2(
        tangent2.y - center.y,
        tangent2.x - center.x));

    // The fillet arc should go the "short way" between tangent points
    double sweepAngle = normalizeAngleSigned(endAngle - startAngle);

    // Ensure we take the shorter path (< 180 degrees for a fillet)
    if (std::abs(sweepAngle) > 180.0) {
        sweepAngle = oppositeSweepDeg(sweepAngle);
    }

    result.valid = true;
    result.center = center;
    result.radius = radius;
    result.startAngle = normalizeAngle(startAngle);
    result.sweepAngle = sweepAngle;

    return result;
}

// =====================================================================
//  Sketch placement helpers
// =====================================================================

ChordFrame chordFrame(const Point2D& start, const Point2D& end, double tolerance)
{
    ChordFrame f;
    f.midpoint = Point2D((start.x + end.x) * 0.5, (start.y + end.y) * 0.5);

    const Point2D delta(end.x - start.x, end.y - start.y);
    f.length = length(delta);
    if (f.length <= tolerance) {
        return f;               // valid stays false; direction/normal unset
    }

    f.direction = Point2D(delta.x / f.length, delta.y / f.length);
    f.normal    = Point2D(-f.direction.y, f.direction.x);
    f.valid     = true;
    return f;
}

ChordCenters circleCentersThroughPoints(const Point2D& a, const Point2D& b,
                                        double radius, double tolerance)
{
    ChordCenters c;

    const ChordFrame f = chordFrame(a, b, tolerance);
    if (!f.valid || radius <= 0.0) {
        return c;
    }

    const double halfChord = f.length * 0.5;
    if (halfChord > radius) {
        return c;               // no circle of this radius reaches both points
    }

    // The exactly-tangent case (halfChord == radius) falls out naturally: the
    // offset is zero and both centers coincide with the midpoint.
    const double offset = std::sqrt(radius * radius - halfChord * halfChord);
    c.first  = Point2D(f.midpoint.x + f.normal.x * offset,
                       f.midpoint.y + f.normal.y * offset);
    c.second = Point2D(f.midpoint.x - f.normal.x * offset,
                       f.midpoint.y - f.normal.y * offset);
    c.valid  = true;
    return c;
}

namespace {
/// A sentinel lock (length <= 0, angle exactly -1) as an optional one.
std::optional<double> lengthLock(double v)
{
    return v > 0.0 ? std::optional<double>(v) : std::nullopt;
}
std::optional<double> angleLock(double v)
{
    return v != -1.0 ? std::optional<double>(v) : std::nullopt;
}
}  // namespace

Point2D applyInsideAngleLock(const Point2D& p1, const Point2D& p2, const Point2D& toward,
                             double lockedLength, double lockedAngleDegrees)
{
    return applyInsideAngleLock(p1, p2, toward, lengthLock(lockedLength),
                                angleLock(lockedAngleDegrees));
}

Point2D applyInsideAngleLock(const Point2D& p1, const Point2D& p2, const Point2D& toward,
                             std::optional<double> lockedLength,
                             std::optional<double> lockedAngleDegrees)
{
    const Point2D dir = toward - p2;
    const double mouseLen = length(dir);
    const double mouseAng = std::atan2(dir.y, dir.x);
    const double useLen = (lockedLength && *lockedLength > 0.0) ? *lockedLength : mouseLen;
    double useAng = mouseAng;
    if (lockedAngleDegrees) {
        // Inside angle: between p2 -> p1 and p2 -> p3, on the cursor's side.
        const double edge1Dir = std::atan2(p1.y - p2.y, p1.x - p2.x);
        const double dir1 = edge1Dir + degreesToRadians(*lockedAngleDegrees);
        const double dir2 = edge1Dir - degreesToRadians(*lockedAngleDegrees);
        const double diff1 = std::abs(std::remainder(mouseAng - dir1, 2.0 * M_PI));
        const double diff2 = std::abs(std::remainder(mouseAng - dir2, 2.0 * M_PI));
        useAng = (diff1 <= diff2) ? dir1 : dir2;
    }
    if (useLen > 0.001) return polarPoint(p2, useLen, useAng);
    return toward;
}

bool lockedRadiusCenterToward(const Point2D& a, const Point2D& b, double radius,
                              const Point2D& toward, Point2D& center)
{
    const ChordCenters cc = circleCentersThroughPoints(a, b, radius);
    if (!cc.valid) return false;
    center = (lineLength(cc.first, toward) <= lineLength(cc.second, toward)) ? cc.first : cc.second;
    return true;
}

Point2D applyPolarLock(const Point2D& from, const Point2D& to,
                       double lockedLength, double lockedAngleDegrees)
{
    return applyPolarLock(from, to, lengthLock(lockedLength), angleLock(lockedAngleDegrees));
}

Point2D applyPolarLock(const Point2D& from, const Point2D& to,
                       std::optional<double> lockedLength,
                       std::optional<double> lockedAngleDegrees)
{
    const Point2D delta(to.x - from.x, to.y - from.y);
    const double mouseLength = length(delta);
    const double mouseAngle  = std::atan2(delta.y, delta.x);

    const double useLength =
        (lockedLength && *lockedLength > 0.0) ? *lockedLength : mouseLength;
    const double useAngle =
        lockedAngleDegrees ? degreesToRadians(*lockedAngleDegrees) : mouseAngle;

    // Guard mirrors the sketch UI: a degenerate length leaves the point where
    // it was rather than collapsing it onto `from`.
    if (useLength <= 0.001) {
        return to;
    }
    return Point2D(from.x + useLength * std::cos(useAngle),
                   from.y + useLength * std::sin(useAngle));
}


std::vector<Point2D> regularPolygonVertices(
    const Point2D& center, double radius, int sides,
    double startAngle, bool circumscribed)
{
    std::vector<Point2D> verts;
    if (sides < 3 || radius <= 0.0) return verts;

    double vertexRadius = radius;
    double a0 = startAngle;
    if (circumscribed) {
        vertexRadius = radius / std::cos(M_PI / sides);  // apothem -> vertex distance
        a0 += M_PI / sides;                              // edge midpoint faces startAngle
    }
    const double step = 2.0 * M_PI / sides;
    verts.reserve(static_cast<size_t>(sides));
    for (int i = 0; i < sides; ++i) {
        const double ang = a0 + i * step;
        verts.push_back(Point2D(center.x + vertexRadius * std::cos(ang),
                                center.y + vertexRadius * std::sin(ang)));
    }
    return verts;
}


ArcCenterFromChord arcCenterOnBisector(
    const Point2D& start, const Point2D& end, const Point2D& target,
    bool semicircle, bool flip,
    ChordFloor floor, double minPerpDistance)
{
    ArcCenterFromChord r;
    const ChordFrame cf = chordFrame(start, end, 1e-3);
    if (!cf.valid) return r;   // coincident endpoints -> invalid

    double projDist = dot(target - cf.midpoint, cf.normal);
    if (semicircle) projDist = 0.0;   // exact 180-degree arc
    if (flip) projDist = -projDist;   // mirror to the far side of the chord

    if (floor == ChordFloor::MinPerpDistance && std::abs(projDist) < minPerpDistance)
        projDist = (projDist >= 0 ? 1.0 : -1.0) * minPerpDistance;

    Point2D center = cf.midpoint + cf.normal * projDist;
    double radius = length(center - start);

    const double halfChord = cf.length / 2.0;
    if (floor == ChordFloor::MinRadius && !semicircle) {
        const double minRadius = halfChord * 1.01;   // just past the degenerate chord
        if (radius < minRadius) {
            const double minDist =
                std::sqrt(minRadius * minRadius - halfChord * halfChord);
            const double sign = (projDist >= 0) ? 1.0 : -1.0;
            center = cf.midpoint + cf.normal * (sign * minDist);
            radius = minRadius;
            projDist = sign * minDist;
        }
    }
    r.valid = true; r.center = center; r.radius = radius; r.projection = projDist;
    return r;
}


Point2D closestPointOnSegment(const Point2D& point, const Point2D& a, const Point2D& b)
{
    const Point2D ab = b - a, ap = point - a;
    const double L2 = ab.x * ab.x + ab.y * ab.y;
    double t = (L2 > kExactEps) ? (ap.x * ab.x + ap.y * ab.y) / L2 : 0.0;
    t = std::max(0.0, std::min(1.0, t));
    return a + ab * t;
}

Point2D pointAtLockedSweep(const Point2D& center, const Point2D& start, const Point2D& toward,
                           double lockedSweepDeg, bool flip)
{
    const double radius = std::hypot(start.x - center.x, start.y - center.y);
    const double startAngle = std::atan2(start.y - center.y, start.x - center.x);
    const double towardAngle = std::atan2(toward.y - center.y, toward.x - center.x);
    double sweep = towardAngle - startAngle;
    sweep = wrapSweepRad(sweep);
    if (flip) sweep = (sweep > 0) ? sweep - 2.0 * M_PI : sweep + 2.0 * M_PI;
    const double sign = (sweep >= 0) ? 1.0 : -1.0;
    const double endAngle = startAngle + sign * degreesToRadians(std::abs(lockedSweepDeg));
    return Point2D(center.x + radius * std::cos(endAngle), center.y + radius * std::sin(endAngle));
}

Point2D arcCenterFromChordAndSweep(const Point2D& start, const Point2D& end, const Point2D& toward,
                                   double lockedSweepDeg)
{
    const Point2D mid((start.x + end.x) / 2.0, (start.y + end.y) / 2.0);
    const double chordLen = std::hypot(end.x - start.x, end.y - start.y);
    if (chordLen <= 0.001) return toward;
    const Point2D chordDir((end.x - start.x) / chordLen, (end.y - start.y) / chordLen);
    const Point2D perpDir = perpendicular(chordDir);
    const double tanHalf = std::tan(degreesToRadians(std::abs(lockedSweepDeg)) / 2.0);
    const double projDist = (tanHalf > 1e-6) ? (chordLen / 2.0) / tanHalf : 1e6;   // 180 degrees: center on the chord
    const double towardProj = (toward.x - mid.x) * perpDir.x + (toward.y - mid.y) * perpDir.y;
    const double sign = (towardProj >= 0) ? 1.0 : -1.0;
    return Point2D(mid.x + perpDir.x * sign * projDist, mid.y + perpDir.y * sign * projDist);
}

}  // namespace geometry
}  // namespace hobbycad
