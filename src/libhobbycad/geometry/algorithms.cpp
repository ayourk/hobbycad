// =====================================================================
//  src/libhobbycad/geometry/algorithms.cpp — Geometry algorithms
// =====================================================================

#include <hobbycad/geometry/algorithms.h>
#include <hobbycad/geometry/utils.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <random>
#include <stack>

namespace hobbycad {
namespace geometry {

// =====================================================================
//  Convex Hull - Andrew's Monotone Chain Algorithm
// =====================================================================

QVector<QPointF> convexHull(const QVector<QPointF>& points)
{
    if (points.size() < 3) {
        return points;
    }

    // Sort points lexicographically
    QVector<QPointF> sorted = points;
    std::sort(sorted.begin(), sorted.end(), [](const QPointF& a, const QPointF& b) {
        return a.x() < b.x() || (a.x() == b.x() && a.y() < b.y());
    });

    // Remove duplicates
    sorted.erase(std::unique(sorted.begin(), sorted.end(),
        [](const QPointF& a, const QPointF& b) {
            return qFuzzyCompare(a.x(), b.x()) && qFuzzyCompare(a.y(), b.y());
        }), sorted.end());

    if (sorted.size() < 3) {
        return sorted;
    }

    QVector<QPointF> hull;
    hull.reserve(sorted.size() * 2);

    // Build lower hull
    for (const QPointF& p : sorted) {
        while (hull.size() >= 2 &&
               cross(hull[hull.size()-1] - hull[hull.size()-2],
                     p - hull[hull.size()-2]) <= 0) {
            hull.removeLast();
        }
        hull.append(p);
    }

    // Build upper hull
    int lowerSize = hull.size();
    for (int i = sorted.size() - 2; i >= 0; --i) {
        const QPointF& p = sorted[i];
        while (hull.size() > lowerSize &&
               cross(hull[hull.size()-1] - hull[hull.size()-2],
                     p - hull[hull.size()-2]) <= 0) {
            hull.removeLast();
        }
        hull.append(p);
    }

    hull.removeLast();  // Remove duplicate of first point
    return hull;
}

bool isConvex(const QVector<QPointF>& polygon)
{
    if (polygon.size() < 3) return true;

    bool hasPositive = false;
    bool hasNegative = false;

    for (int i = 0; i < polygon.size(); ++i) {
        const QPointF& p0 = polygon[i];
        const QPointF& p1 = polygon[(i + 1) % polygon.size()];
        const QPointF& p2 = polygon[(i + 2) % polygon.size()];

        double crossProduct = cross(p1 - p0, p2 - p1);

        if (crossProduct > DEFAULT_TOLERANCE) hasPositive = true;
        if (crossProduct < -DEFAULT_TOLERANCE) hasNegative = true;

        if (hasPositive && hasNegative) return false;
    }

    return true;
}

// =====================================================================
//  Polygon Simplification - Ramer-Douglas-Peucker Algorithm
// =====================================================================

namespace {

// Helper for Douglas-Peucker: find point with max distance from line
double perpendicularDistance(const QPointF& point, const QPointF& lineStart, const QPointF& lineEnd)
{
    double dx = lineEnd.x() - lineStart.x();
    double dy = lineEnd.y() - lineStart.y();

    double lineLengthSq = dx * dx + dy * dy;
    if (lineLengthSq < DEFAULT_TOLERANCE * DEFAULT_TOLERANCE) {
        return length(point - lineStart);
    }

    double t = ((point.x() - lineStart.x()) * dx + (point.y() - lineStart.y()) * dy) / lineLengthSq;
    t = qBound(0.0, t, 1.0);

    QPointF projection(lineStart.x() + t * dx, lineStart.y() + t * dy);
    return length(point - projection);
}

void douglasPeuckerRecursive(
    const QVector<QPointF>& points,
    int start, int end,
    double epsilon,
    QVector<bool>& keep)
{
    if (end <= start + 1) return;

    double maxDist = 0;
    int maxIndex = start;

    for (int i = start + 1; i < end; ++i) {
        double dist = perpendicularDistance(points[i], points[start], points[end]);
        if (dist > maxDist) {
            maxDist = dist;
            maxIndex = i;
        }
    }

    if (maxDist > epsilon) {
        keep[maxIndex] = true;
        douglasPeuckerRecursive(points, start, maxIndex, epsilon, keep);
        douglasPeuckerRecursive(points, maxIndex, end, epsilon, keep);
    }
}

}  // anonymous namespace

QVector<QPointF> simplifyPolyline(const QVector<QPointF>& points, double epsilon)
{
    if (points.size() < 3) return points;
    if (epsilon <= 0) return points;

    QVector<bool> keep(points.size(), false);
    keep[0] = true;
    keep[points.size() - 1] = true;

    douglasPeuckerRecursive(points, 0, points.size() - 1, epsilon, keep);

    QVector<QPointF> result;
    for (int i = 0; i < points.size(); ++i) {
        if (keep[i]) result.append(points[i]);
    }

    return result;
}

QVector<QPointF> simplifyPolygon(const QVector<QPointF>& polygon, double epsilon)
{
    if (polygon.size() < 4) return polygon;

    // For polygon, we need to handle the wrap-around
    // Double the polygon, simplify, then take the relevant portion
    QVector<QPointF> doubled;
    doubled.reserve(polygon.size() * 2);
    doubled.append(polygon);
    doubled.append(polygon);

    QVector<bool> keep(doubled.size(), false);

    // Find the point farthest from opposite point to use as anchor
    int anchor = 0;
    double maxDist = 0;
    int n = polygon.size();
    for (int i = 0; i < n; ++i) {
        double dist = length(polygon[i] - polygon[(i + n/2) % n]);
        if (dist > maxDist) {
            maxDist = dist;
            anchor = i;
        }
    }

    keep[anchor] = true;
    keep[anchor + n] = true;

    // Simplify from anchor to anchor+n
    douglasPeuckerRecursive(doubled, anchor, anchor + n, epsilon, keep);

    QVector<QPointF> result;
    for (int i = anchor; i <= anchor + n; ++i) {
        if (keep[i]) {
            result.append(doubled[i % n]);
        }
    }

    // Remove duplicate last point if present
    if (!result.isEmpty() && result.first() == result.last()) {
        result.removeLast();
    }

    return result;
}

QVector<QPointF> simplifyByArea(const QVector<QPointF>& points, double minArea)
{
    if (points.size() < 3) return points;

    // Visvalingam-Whyatt: iteratively remove point forming smallest triangle
    struct PointData {
        int index;
        double area;
        bool removed = false;
    };

    QVector<PointData> data(points.size());
    for (int i = 0; i < points.size(); ++i) {
        data[i].index = i;
        data[i].removed = false;
    }

    auto calcArea = [&](int idx) -> double {
        if (idx <= 0 || idx >= points.size() - 1) return std::numeric_limits<double>::max();

        // Find prev and next non-removed points
        int prev = idx - 1;
        while (prev >= 0 && data[prev].removed) --prev;
        int next = idx + 1;
        while (next < points.size() && data[next].removed) ++next;

        if (prev < 0 || next >= points.size()) return std::numeric_limits<double>::max();

        // Triangle area
        double area = 0.5 * qAbs(cross(points[idx] - points[prev], points[next] - points[prev]));
        return area;
    };

    // Initialize areas
    for (int i = 1; i < points.size() - 1; ++i) {
        data[i].area = calcArea(i);
    }
    data[0].area = std::numeric_limits<double>::max();
    data[points.size()-1].area = std::numeric_limits<double>::max();

    // Use priority queue for efficient minimum finding
    auto cmp = [](const PointData* a, const PointData* b) { return a->area > b->area; };
    std::priority_queue<PointData*, QVector<PointData*>, decltype(cmp)> pq(cmp);

    for (int i = 1; i < points.size() - 1; ++i) {
        pq.push(&data[i]);
    }

    while (!pq.empty()) {
        PointData* pd = pq.top();
        pq.pop();

        if (pd->removed) continue;
        if (pd->area >= minArea) break;

        pd->removed = true;

        // Update neighbors
        int prev = pd->index - 1;
        while (prev >= 0 && data[prev].removed) --prev;
        int next = pd->index + 1;
        while (next < points.size() && data[next].removed) ++next;

        if (prev > 0) {
            data[prev].area = calcArea(prev);
            pq.push(&data[prev]);
        }
        if (next < points.size() - 1) {
            data[next].area = calcArea(next);
            pq.push(&data[next]);
        }
    }

    QVector<QPointF> result;
    for (int i = 0; i < points.size(); ++i) {
        if (!data[i].removed) result.append(points[i]);
    }

    return result;
}

// =====================================================================
//  Minimal Bounding Circle - Welzl's Algorithm
// =====================================================================

namespace {

MinimalBoundingCircle circleFromThreePoints(const QPointF& a, const QPointF& b, const QPointF& c)
{
    double ax = a.x(), ay = a.y();
    double bx = b.x(), by = b.y();
    double cx = c.x(), cy = c.y();

    double d = 2.0 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by));
    if (qAbs(d) < DEFAULT_TOLERANCE) {
        // Collinear - return circle through two farthest points
        double ab = length(b - a);
        double bc = length(c - b);
        double ca = length(a - c);
        if (ab >= bc && ab >= ca) {
            return {lineMidpoint(a, b), ab / 2.0};
        } else if (bc >= ca) {
            return {lineMidpoint(b, c), bc / 2.0};
        } else {
            return {lineMidpoint(c, a), ca / 2.0};
        }
    }

    double aSq = ax * ax + ay * ay;
    double bSq = bx * bx + by * by;
    double cSq = cx * cx + cy * cy;

    double ux = (aSq * (by - cy) + bSq * (cy - ay) + cSq * (ay - by)) / d;
    double uy = (aSq * (cx - bx) + bSq * (ax - cx) + cSq * (bx - ax)) / d;

    QPointF center(ux, uy);
    return {center, length(a - center)};
}

MinimalBoundingCircle circleFromTwoPoints(const QPointF& a, const QPointF& b)
{
    return {lineMidpoint(a, b), length(b - a) / 2.0};
}

MinimalBoundingCircle welzlRecursive(
    QVector<QPointF>& points, int n,
    QVector<QPointF>& boundary, int b)
{
    if (n == 0 || b == 3) {
        switch (b) {
            case 0: return {QPointF(0, 0), 0};
            case 1: return {boundary[0], 0};
            case 2: return circleFromTwoPoints(boundary[0], boundary[1]);
            case 3: return circleFromThreePoints(boundary[0], boundary[1], boundary[2]);
        }
    }

    // Pick random point
    int idx = n - 1;  // Could randomize for better average case
    QPointF p = points[idx];

    MinimalBoundingCircle circle = welzlRecursive(points, n - 1, boundary, b);

    if (length(p - circle.center) <= circle.radius + DEFAULT_TOLERANCE) {
        return circle;
    }

    // Point is outside, must be on boundary
    boundary[b] = p;
    return welzlRecursive(points, n - 1, boundary, b + 1);
}

}  // anonymous namespace

MinimalBoundingCircle minimalBoundingCircle(const QVector<QPointF>& points)
{
    if (points.isEmpty()) return {QPointF(0, 0), 0};
    if (points.size() == 1) return {points[0], 0};
    if (points.size() == 2) return circleFromTwoPoints(points[0], points[1]);

    // Shuffle for expected O(n) performance
    QVector<QPointF> shuffled = points;
    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(shuffled.begin(), shuffled.end(), g);

    QVector<QPointF> boundary(3);
    return welzlRecursive(shuffled, shuffled.size(), boundary, 0);
}

// =====================================================================
//  Oriented Bounding Box - Rotating Calipers
// =====================================================================

QVector<QPointF> OrientedBoundingBox::corners() const
{
    QVector<QPointF> result(4);
    double c = qCos(qDegreesToRadians(angle));
    double s = qSin(qDegreesToRadians(angle));

    QPointF xAxis(c * halfExtents.x(), s * halfExtents.x());
    QPointF yAxis(-s * halfExtents.y(), c * halfExtents.y());

    result[0] = center - xAxis - yAxis;
    result[1] = center + xAxis - yAxis;
    result[2] = center + xAxis + yAxis;
    result[3] = center - xAxis + yAxis;

    return result;
}

double OrientedBoundingBox::area() const
{
    return 4.0 * halfExtents.x() * halfExtents.y();
}

bool OrientedBoundingBox::contains(const QPointF& point) const
{
    // Transform point to local coordinates
    QPointF local = point - center;
    double c = qCos(qDegreesToRadians(-angle));
    double s = qSin(qDegreesToRadians(-angle));
    QPointF rotated(local.x() * c - local.y() * s, local.x() * s + local.y() * c);

    return qAbs(rotated.x()) <= halfExtents.x() + DEFAULT_TOLERANCE &&
           qAbs(rotated.y()) <= halfExtents.y() + DEFAULT_TOLERANCE;
}

OrientedBoundingBox minimalOrientedBoundingBox(const QVector<QPointF>& points)
{
    if (points.isEmpty()) return {};

    QVector<QPointF> hull = convexHull(points);
    if (hull.size() < 3) {
        // Degenerate case - single point or line
        BoundingBox aabb = polygonBounds(points);
        OrientedBoundingBox result;
        result.center = QPointF((aabb.minX + aabb.maxX) / 2,
                                (aabb.minY + aabb.maxY) / 2);
        result.halfExtents = QPointF((aabb.maxX - aabb.minX) / 2,
                                     (aabb.maxY - aabb.minY) / 2);
        result.angle = 0;
        return result;
    }

    double minArea = std::numeric_limits<double>::max();
    OrientedBoundingBox best;

    // For each edge of the hull, compute the bounding box aligned to that edge
    for (int i = 0; i < hull.size(); ++i) {
        QPointF edge = hull[(i + 1) % hull.size()] - hull[i];
        double edgeAngle = qRadiansToDegrees(qAtan2(edge.y(), edge.x()));

        // Rotate all points to align this edge with x-axis
        double c = qCos(qDegreesToRadians(-edgeAngle));
        double s = qSin(qDegreesToRadians(-edgeAngle));

        double minX = std::numeric_limits<double>::max();
        double maxX = std::numeric_limits<double>::lowest();
        double minY = std::numeric_limits<double>::max();
        double maxY = std::numeric_limits<double>::lowest();

        for (const QPointF& p : hull) {
            double rx = p.x() * c - p.y() * s;
            double ry = p.x() * s + p.y() * c;
            minX = qMin(minX, rx);
            maxX = qMax(maxX, rx);
            minY = qMin(minY, ry);
            maxY = qMax(maxY, ry);
        }

        double area = (maxX - minX) * (maxY - minY);
        if (area < minArea) {
            minArea = area;

            // Compute center in original coordinates
            double cx = (minX + maxX) / 2;
            double cy = (minY + maxY) / 2;
            double cr = qCos(qDegreesToRadians(edgeAngle));
            double sr = qSin(qDegreesToRadians(edgeAngle));

            best.center = QPointF(cx * cr - cy * sr, cx * sr + cy * cr);
            best.halfExtents = QPointF((maxX - minX) / 2, (maxY - minY) / 2);
            best.angle = edgeAngle;
        }
    }

    return best;
}

BoundingBox obbToAABB(const OrientedBoundingBox& obb)
{
    QVector<QPointF> corners = obb.corners();
    return polygonBounds(corners);
}

// =====================================================================
//  2D Boolean Operations
// =====================================================================
//  Uses Sutherland-Hodgman for intersection and Weiler-Atherton style
//  edge-walking for union/difference/XOR operations.

namespace {

// Sutherland-Hodgman polygon clipping (for convex clipping polygons)
QVector<QPointF> clipPolygonByEdge(
    const QVector<QPointF>& polygon,
    const QPointF& edgeStart, const QPointF& edgeEnd)
{
    if (polygon.isEmpty()) return {};

    QVector<QPointF> output;
    QPointF edgeDir = edgeEnd - edgeStart;

    auto inside = [&](const QPointF& p) {
        return cross(edgeDir, p - edgeStart) >= 0;
    };

    auto intersect = [&](const QPointF& a, const QPointF& b) -> QPointF {
        QPointF dir = b - a;
        double denom = cross(dir, edgeDir);
        if (qAbs(denom) < DEFAULT_TOLERANCE) return a;
        double t = cross(edgeDir, a - edgeStart) / denom;
        return a - t * dir;
    };

    for (int i = 0; i < polygon.size(); ++i) {
        const QPointF& current = polygon[i];
        const QPointF& next = polygon[(i + 1) % polygon.size()];

        bool currentInside = inside(current);
        bool nextInside = inside(next);

        if (currentInside) {
            output.append(current);
            if (!nextInside) {
                output.append(intersect(current, next));
            }
        } else if (nextInside) {
            output.append(intersect(current, next));
        }
    }

    return output;
}

// Find all intersection points between two polygon edges
struct EdgeIntersection {
    QPointF point;
    int edge1;      // Edge index in poly1
    double t1;      // Parameter on edge1
    int edge2;      // Edge index in poly2
    double t2;      // Parameter on edge2
    bool entering;  // True if entering poly2 from outside
};

QVector<EdgeIntersection> findPolygonIntersections(
    const QVector<QPointF>& poly1,
    const QVector<QPointF>& poly2)
{
    QVector<EdgeIntersection> intersections;

    for (int i = 0; i < poly1.size(); ++i) {
        const QPointF& a1 = poly1[i];
        const QPointF& b1 = poly1[(i + 1) % poly1.size()];
        QPointF d1 = b1 - a1;

        for (int j = 0; j < poly2.size(); ++j) {
            const QPointF& a2 = poly2[j];
            const QPointF& b2 = poly2[(j + 1) % poly2.size()];
            QPointF d2 = b2 - a2;

            double denom = cross(d1, d2);
            if (qAbs(denom) < DEFAULT_TOLERANCE) continue;

            QPointF diff = a2 - a1;
            double t1 = cross(diff, d2) / denom;
            double t2 = cross(diff, d1) / denom;

            if (t1 > DEFAULT_TOLERANCE && t1 < 1.0 - DEFAULT_TOLERANCE &&
                t2 > DEFAULT_TOLERANCE && t2 < 1.0 - DEFAULT_TOLERANCE) {
                QPointF pt = a1 + t1 * d1;
                // Determine if entering or leaving poly2
                // Cross product of edge direction with poly2 edge normal
                QPointF n2 = perpendicular(d2);
                bool entering = dot(d1, n2) > 0;
                intersections.append({pt, i, t1, j, t2, entering});
            }
        }
    }

    return intersections;
}

// Build result polygon by walking edges (Weiler-Atherton style)
QVector<QPointF> walkPolygonBoundary(
    const QVector<QPointF>& poly1,
    const QVector<QPointF>& poly2,
    const QVector<EdgeIntersection>& intersections,
    bool walkInside)  // true for intersection, false for union exterior
{
    if (intersections.isEmpty()) return {};

    QVector<QPointF> result;
    QVector<bool> visited(intersections.size(), false);

    // Find first unvisited entering intersection
    int startIdx = -1;
    for (int i = 0; i < intersections.size(); ++i) {
        if (!visited[i] && intersections[i].entering == walkInside) {
            startIdx = i;
            break;
        }
    }
    if (startIdx < 0) return {};

    int currentIdx = startIdx;
    bool onPoly1 = true;

    do {
        visited[currentIdx] = true;
        result.append(intersections[currentIdx].point);

        const EdgeIntersection& curr = intersections[currentIdx];

        // Walk along current polygon to next intersection
        const QVector<QPointF>& currentPoly = onPoly1 ? poly1 : poly2;
        int edge = onPoly1 ? curr.edge1 : curr.edge2;
        double t = onPoly1 ? curr.t1 : curr.t2;

        // Find next intersection on this polygon
        double nextT = 2.0;
        int nextIdx = -1;
        int nextEdge = edge;

        // Check remaining intersections on current edge
        for (int i = 0; i < intersections.size(); ++i) {
            if (i == currentIdx) continue;
            const EdgeIntersection& other = intersections[i];
            int otherEdge = onPoly1 ? other.edge1 : other.edge2;
            double otherT = onPoly1 ? other.t1 : other.t2;

            if (otherEdge == edge && otherT > t && otherT < nextT) {
                nextT = otherT;
                nextIdx = i;
            }
        }

        if (nextIdx < 0) {
            // Walk to next edges
            for (int step = 1; step <= currentPoly.size(); ++step) {
                int checkEdge = (edge + step) % currentPoly.size();
                result.append(currentPoly[(edge + step) % currentPoly.size()]);

                // Find first intersection on this edge
                double minT = 2.0;
                for (int i = 0; i < intersections.size(); ++i) {
                    const EdgeIntersection& other = intersections[i];
                    int otherEdge = onPoly1 ? other.edge1 : other.edge2;
                    double otherT = onPoly1 ? other.t1 : other.t2;

                    if (otherEdge == checkEdge && otherT < minT) {
                        minT = otherT;
                        nextIdx = i;
                    }
                }
                if (nextIdx >= 0) break;
            }
        }

        if (nextIdx < 0 || visited[nextIdx]) break;

        currentIdx = nextIdx;
        onPoly1 = !onPoly1;

    } while (currentIdx != startIdx && result.size() < 1000);

    return result;
}

}  // anonymous namespace

BooleanResult polygonIntersection(const QVector<QPointF>& poly1, const QVector<QPointF>& poly2)
{
    BooleanResult result;
    if (poly1.size() < 3 || poly2.size() < 3) {
        result.error = "Polygons must have at least 3 vertices";
        return result;
    }

    // Check containment first
    bool allPoly1InPoly2 = true;
    bool allPoly2InPoly1 = true;
    for (const QPointF& p : poly1) {
        if (!pointInPolygon(p, poly2)) {
            allPoly1InPoly2 = false;
            break;
        }
    }
    for (const QPointF& p : poly2) {
        if (!pointInPolygon(p, poly1)) {
            allPoly2InPoly1 = false;
            break;
        }
    }

    if (allPoly1InPoly2) {
        PolygonWithHoles pwh;
        pwh.outer = poly1;
        result.polygons.append(pwh);
        result.success = true;
        return result;
    }
    if (allPoly2InPoly1) {
        PolygonWithHoles pwh;
        pwh.outer = poly2;
        result.polygons.append(pwh);
        result.success = true;
        return result;
    }

    // Use Sutherland-Hodgman clipping
    QVector<QPointF> clipped = poly1;
    for (int i = 0; i < poly2.size(); ++i) {
        clipped = clipPolygonByEdge(clipped, poly2[i], poly2[(i + 1) % poly2.size()]);
        if (clipped.isEmpty()) {
            result.success = true;  // No intersection is a valid result
            return result;
        }
    }

    if (clipped.size() >= 3) {
        PolygonWithHoles pwh;
        pwh.outer = clipped;
        result.polygons.append(pwh);
    }

    result.success = true;
    return result;
}

BooleanResult polygonUnion(const QVector<QPointF>& poly1, const QVector<QPointF>& poly2)
{
    BooleanResult result;
    if (poly1.size() < 3 || poly2.size() < 3) {
        result.error = "Polygons must have at least 3 vertices";
        return result;
    }

    // Check containment
    bool allPoly2InPoly1 = true;
    for (const QPointF& p : poly2) {
        if (!pointInPolygon(p, poly1)) {
            allPoly2InPoly1 = false;
            break;
        }
    }
    if (allPoly2InPoly1) {
        PolygonWithHoles pwh;
        pwh.outer = poly1;
        result.polygons.append(pwh);
        result.success = true;
        return result;
    }

    bool allPoly1InPoly2 = true;
    for (const QPointF& p : poly1) {
        if (!pointInPolygon(p, poly2)) {
            allPoly1InPoly2 = false;
            break;
        }
    }
    if (allPoly1InPoly2) {
        PolygonWithHoles pwh;
        pwh.outer = poly2;
        result.polygons.append(pwh);
        result.success = true;
        return result;
    }

    // Find intersections
    auto intersections = findPolygonIntersections(poly1, poly2);

    if (intersections.isEmpty()) {
        // No intersections and no containment - disjoint polygons
        PolygonWithHoles pwh1, pwh2;
        pwh1.outer = poly1;
        pwh2.outer = poly2;
        result.polygons.append(pwh1);
        result.polygons.append(pwh2);
        result.success = true;
        return result;
    }

    // Build union boundary
    QVector<QPointF> unionBoundary = walkPolygonBoundary(poly1, poly2, intersections, false);

    if (unionBoundary.size() >= 3) {
        PolygonWithHoles pwh;
        pwh.outer = unionBoundary;
        result.polygons.append(pwh);
        result.success = true;
    } else {
        // Fallback: return convex hull of both polygons
        QVector<QPointF> allPoints;
        allPoints.append(poly1);
        allPoints.append(poly2);
        PolygonWithHoles pwh;
        pwh.outer = convexHull(allPoints);
        result.polygons.append(pwh);
        result.success = true;
    }

    return result;
}

BooleanResult polygonDifference(const QVector<QPointF>& poly1, const QVector<QPointF>& poly2)
{
    BooleanResult result;
    if (poly1.size() < 3 || poly2.size() < 3) {
        result.error = "Polygons must have at least 3 vertices";
        return result;
    }

    // Check if poly2 is fully outside poly1
    bool anyPoly2InPoly1 = false;
    for (const QPointF& p : poly2) {
        if (pointInPolygon(p, poly1)) {
            anyPoly2InPoly1 = true;
            break;
        }
    }
    // Also check if any poly2 edges intersect poly1
    auto intersections = findPolygonIntersections(poly1, poly2);

    if (!anyPoly2InPoly1 && intersections.isEmpty()) {
        // poly2 is completely outside - return poly1 unchanged
        PolygonWithHoles pwh;
        pwh.outer = poly1;
        result.polygons.append(pwh);
        result.success = true;
        return result;
    }

    // Check if poly2 fully contains poly1
    bool allPoly1InPoly2 = true;
    for (const QPointF& p : poly1) {
        if (!pointInPolygon(p, poly2)) {
            allPoly1InPoly2 = false;
            break;
        }
    }
    if (allPoly1InPoly2 && intersections.isEmpty()) {
        // poly1 - poly2 = empty
        result.success = true;
        return result;
    }

    // Check if poly2 is fully inside poly1 (creates a hole)
    bool allPoly2InPoly1 = true;
    for (const QPointF& p : poly2) {
        if (!pointInPolygon(p, poly1)) {
            allPoly2InPoly1 = false;
            break;
        }
    }
    if (allPoly2InPoly1 && intersections.isEmpty()) {
        PolygonWithHoles pwh;
        pwh.outer = poly1;
        // Reverse poly2 to make it a hole (CW)
        QVector<QPointF> hole = poly2;
        if (polygonIsCCW(hole)) {
            std::reverse(hole.begin(), hole.end());
        }
        pwh.holes.append(hole);
        result.polygons.append(pwh);
        result.success = true;
        return result;
    }

    // Complex case: use clipping
    // Clip poly1 by the exterior of poly2 (reverse each edge of poly2)
    QVector<QPointF> clipped = poly1;
    for (int i = 0; i < poly2.size(); ++i) {
        // Clip by reversed edge (exterior of poly2)
        clipped = clipPolygonByEdge(clipped,
            poly2[(i + 1) % poly2.size()], poly2[i]);
        if (clipped.isEmpty()) {
            result.success = true;  // poly1 fully consumed
            return result;
        }
    }

    if (clipped.size() >= 3) {
        PolygonWithHoles pwh;
        pwh.outer = clipped;
        result.polygons.append(pwh);
    }
    result.success = true;
    return result;
}

BooleanResult polygonXOR(const QVector<QPointF>& poly1, const QVector<QPointF>& poly2)
{
    BooleanResult result;

    // XOR = (A - B) + (B - A) = Union - Intersection
    auto diff1 = polygonDifference(poly1, poly2);
    auto diff2 = polygonDifference(poly2, poly1);

    if (diff1.success) {
        result.polygons.append(diff1.polygons);
    }
    if (diff2.success) {
        result.polygons.append(diff2.polygons);
    }

    result.success = diff1.success || diff2.success;
    return result;
}

// =====================================================================
//  Polygon Offset (Simplified)
// =====================================================================

QVector<QVector<QPointF>> offsetPolygon(
    const QVector<QPointF>& polygon,
    double distance,
    int joinType,
    double miterLimit)
{
    if (polygon.size() < 3 || qAbs(distance) < DEFAULT_TOLERANCE) {
        return {polygon};
    }

    QVector<QPointF> result;
    result.reserve(polygon.size() * (joinType == 1 ? 8 : 1));

    for (int i = 0; i < polygon.size(); ++i) {
        const QPointF& prev = polygon[(i + polygon.size() - 1) % polygon.size()];
        const QPointF& curr = polygon[i];
        const QPointF& next = polygon[(i + 1) % polygon.size()];

        // Edge directions
        QPointF dir1 = normalize(curr - prev);
        QPointF dir2 = normalize(next - curr);

        // Normals (perpendicular, pointing outward for CCW polygon)
        QPointF n1 = perpendicular(dir1);
        QPointF n2 = perpendicular(dir2);

        // Offset points along normals
        QPointF p1 = curr + n1 * distance;
        QPointF p2 = curr + n2 * distance;

        double crossVal = cross(dir1, dir2);

        if (qAbs(crossVal) < DEFAULT_TOLERANCE) {
            // Parallel edges
            result.append(p1);
        } else if (joinType == 0) {
            // Miter join
            double miterDist = distance / qSin(qAcos(qBound(-1.0, dot(n1, n2), 1.0)) / 2.0);
            if (qAbs(miterDist) < miterLimit * qAbs(distance)) {
                QPointF miterDir = normalize(n1 + n2);
                result.append(curr + miterDir * miterDist);
            } else {
                result.append(p1);
                result.append(p2);
            }
        } else if (joinType == 1) {
            // Round join
            double angle1 = vectorAngle(n1);
            double angle2 = vectorAngle(n2);
            double sweep = angle2 - angle1;
            if (sweep > 180) sweep -= 360;
            if (sweep < -180) sweep += 360;

            int segments = qMax(2, static_cast<int>(qAbs(sweep) / 15.0));
            for (int j = 0; j <= segments; ++j) {
                double t = static_cast<double>(j) / segments;
                double a = qDegreesToRadians(angle1 + t * sweep);
                result.append(curr + QPointF(qCos(a), qSin(a)) * qAbs(distance));
            }
        } else {
            // Square join
            result.append(p1);
            result.append(p2);
        }
    }

    return {result};
}

QVector<QVector<QPointF>> offsetPolyline(
    const QVector<QPointF>& polyline,
    double distance,
    int endType,
    int joinType)
{
    if (polyline.size() < 2) return {};

    // Create closed polygon by going forward on left side, backward on right
    QVector<QPointF> closed;

    // Left side (forward)
    for (int i = 0; i < polyline.size() - 1; ++i) {
        QPointF dir = normalize(polyline[i + 1] - polyline[i]);
        QPointF normal = perpendicular(dir);
        closed.append(polyline[i] + normal * distance);
    }

    // End cap
    QPointF lastDir = normalize(polyline.last() - polyline[polyline.size() - 2]);
    QPointF lastNormal = perpendicular(lastDir);

    if (endType == 1) {
        // Round end
        for (int j = 0; j <= 8; ++j) {
            double angle = qDegreesToRadians(90.0 - 180.0 * j / 8);
            QPointF offset = rotatePoint(lastNormal * distance, -j * 180.0 / 8);
            closed.append(polyline.last() + offset);
        }
    } else if (endType == 2) {
        // Square end
        closed.append(polyline.last() + lastNormal * distance + lastDir * qAbs(distance));
        closed.append(polyline.last() - lastNormal * distance + lastDir * qAbs(distance));
    } else {
        // Butt end
        closed.append(polyline.last() + lastNormal * distance);
        closed.append(polyline.last() - lastNormal * distance);
    }

    // Right side (backward)
    for (int i = polyline.size() - 1; i > 0; --i) {
        QPointF dir = normalize(polyline[i - 1] - polyline[i]);
        QPointF normal = perpendicular(dir);
        closed.append(polyline[i] + normal * distance);
    }

    // Start cap
    QPointF firstDir = normalize(polyline[1] - polyline[0]);
    QPointF firstNormal = perpendicular(firstDir);

    if (endType == 1) {
        for (int j = 0; j <= 8; ++j) {
            double angle = qDegreesToRadians(-90.0 + 180.0 * j / 8);
            closed.append(polyline[0] - rotatePoint(firstNormal * distance, j * 180.0 / 8));
        }
    } else if (endType == 2) {
        closed.append(polyline[0] - firstNormal * distance - firstDir * qAbs(distance));
        closed.append(polyline[0] + firstNormal * distance - firstDir * qAbs(distance));
    }

    return {closed};
}

// =====================================================================
//  Triangulation - Ear Clipping
// =====================================================================

namespace {

bool isEar(const QVector<QPointF>& polygon, int i, const QVector<bool>& removed)
{
    int n = polygon.size();

    // Find prev and next non-removed vertices
    int prev = (i + n - 1) % n;
    while (removed[prev]) prev = (prev + n - 1) % n;
    int next = (i + 1) % n;
    while (removed[next]) next = (next + 1) % n;

    const QPointF& a = polygon[prev];
    const QPointF& b = polygon[i];
    const QPointF& c = polygon[next];

    // Check if convex (CCW)
    if (cross(b - a, c - b) <= 0) return false;

    // Check that no other vertex is inside this triangle
    for (int j = 0; j < n; ++j) {
        if (removed[j] || j == prev || j == i || j == next) continue;

        const QPointF& p = polygon[j];

        // Point-in-triangle test
        double d1 = cross(b - a, p - a);
        double d2 = cross(c - b, p - b);
        double d3 = cross(a - c, p - c);

        bool hasNeg = (d1 < 0) || (d2 < 0) || (d3 < 0);
        bool hasPos = (d1 > 0) || (d2 > 0) || (d3 > 0);

        if (!(hasNeg && hasPos)) return false;  // Point is inside
    }

    return true;
}

}  // anonymous namespace

QVector<Triangle> triangulatePolygon(const QVector<QPointF>& polygon)
{
    QVector<Triangle> triangles;
    if (polygon.size() < 3) return triangles;

    // Ensure CCW winding
    QVector<QPointF> poly = polygon;
    if (!polygonIsCCW(poly)) {
        std::reverse(poly.begin(), poly.end());
    }

    QVector<bool> removed(poly.size(), false);
    int remaining = poly.size();

    while (remaining > 3) {
        bool foundEar = false;
        for (int i = 0; i < poly.size(); ++i) {
            if (removed[i]) continue;
            if (isEar(poly, i, removed)) {
                // Find prev and next
                int prev = (i + poly.size() - 1) % poly.size();
                while (removed[prev]) prev = (prev + poly.size() - 1) % poly.size();
                int next = (i + 1) % poly.size();
                while (removed[next]) next = (next + 1) % poly.size();

                triangles.append({prev, i, next});
                removed[i] = true;
                --remaining;
                foundEar = true;
                break;
            }
        }
        if (!foundEar) break;  // Degenerate polygon
    }

    // Add final triangle
    if (remaining == 3) {
        QVector<int> indices;
        for (int i = 0; i < poly.size(); ++i) {
            if (!removed[i]) indices.append(i);
        }
        if (indices.size() == 3) {
            triangles.append({indices[0], indices[1], indices[2]});
        }
    }

    return triangles;
}

QPair<QVector<QPointF>, QVector<Triangle>> triangulatePolygonWithHoles(
    const QVector<QPointF>& outer,
    const QVector<QVector<QPointF>>& holes)
{
    if (holes.isEmpty()) {
        return {outer, triangulatePolygon(outer)};
    }

    // Bridge holes to outer boundary using horizontal cuts
    // Algorithm: For each hole, find the rightmost point, then find a visible
    // point on the outer boundary or another hole, and insert a bridge (two edges)

    QVector<QPointF> combined;
    combined.reserve(outer.size() + holes.size() * 2);

    // Ensure outer is CCW
    QVector<QPointF> outerCCW = outer;
    if (!polygonIsCCW(outerCCW)) {
        std::reverse(outerCCW.begin(), outerCCW.end());
    }

    // Ensure holes are CW (so when combined they work correctly)
    QVector<QVector<QPointF>> holesCW;
    for (const auto& hole : holes) {
        QVector<QPointF> h = hole;
        if (polygonIsCCW(h)) {
            std::reverse(h.begin(), h.end());
        }
        holesCW.append(h);
    }

    // Sort holes by rightmost x-coordinate (process right to left)
    QVector<int> holeOrder(holesCW.size());
    for (int i = 0; i < holesCW.size(); ++i) holeOrder[i] = i;
    std::sort(holeOrder.begin(), holeOrder.end(), [&](int a, int b) {
        double maxXa = std::numeric_limits<double>::lowest();
        double maxXb = std::numeric_limits<double>::lowest();
        for (const auto& p : holesCW[a]) maxXa = qMax(maxXa, p.x());
        for (const auto& p : holesCW[b]) maxXb = qMax(maxXb, p.x());
        return maxXa > maxXb;
    });

    // Start with outer boundary
    combined = outerCCW;

    // Insert each hole with a bridge
    for (int hi : holeOrder) {
        const QVector<QPointF>& hole = holesCW[hi];
        if (hole.isEmpty()) continue;

        // Find rightmost point of hole
        int rightmostIdx = 0;
        for (int i = 1; i < hole.size(); ++i) {
            if (hole[i].x() > hole[rightmostIdx].x()) {
                rightmostIdx = i;
            }
        }
        QPointF holePoint = hole[rightmostIdx];

        // Find visible point on combined polygon (shoot ray to the right)
        int bestIdx = -1;
        double bestX = std::numeric_limits<double>::max();

        for (int i = 0; i < combined.size(); ++i) {
            const QPointF& p1 = combined[i];
            const QPointF& p2 = combined[(i + 1) % combined.size()];

            // Check if edge crosses the horizontal ray from holePoint
            if ((p1.y() <= holePoint.y() && p2.y() > holePoint.y()) ||
                (p2.y() <= holePoint.y() && p1.y() > holePoint.y())) {
                // Find intersection x
                double t = (holePoint.y() - p1.y()) / (p2.y() - p1.y());
                double x = p1.x() + t * (p2.x() - p1.x());
                if (x > holePoint.x() && x < bestX) {
                    bestX = x;
                    // Choose the endpoint that's to the right
                    bestIdx = (p1.x() > p2.x()) ? i : (i + 1) % combined.size();
                }
            }
        }

        // If no intersection found, use closest point
        if (bestIdx < 0) {
            double minDist = std::numeric_limits<double>::max();
            for (int i = 0; i < combined.size(); ++i) {
                if (combined[i].x() > holePoint.x()) {
                    double dist = lengthSquared(combined[i] - holePoint);
                    if (dist < minDist) {
                        minDist = dist;
                        bestIdx = i;
                    }
                }
            }
        }
        if (bestIdx < 0) bestIdx = 0;

        // Insert hole into combined polygon at bestIdx
        // The bridge goes from combined[bestIdx] to hole[rightmostIdx] and back
        QVector<QPointF> newCombined;
        newCombined.reserve(combined.size() + hole.size() + 2);

        for (int i = 0; i <= bestIdx; ++i) {
            newCombined.append(combined[i]);
        }

        // Insert hole starting from rightmost point
        for (int i = 0; i < hole.size(); ++i) {
            newCombined.append(hole[(rightmostIdx + i) % hole.size()]);
        }
        // Close the bridge back to hole start
        newCombined.append(hole[rightmostIdx]);
        // Back to outer
        newCombined.append(combined[bestIdx]);

        for (int i = bestIdx + 1; i < combined.size(); ++i) {
            newCombined.append(combined[i]);
        }

        combined = newCombined;
    }

    // Now triangulate the combined polygon
    QVector<Triangle> triangles = triangulatePolygon(combined);

    return {combined, triangles};
}

// =====================================================================
//  Delaunay Triangulation - Bowyer-Watson Algorithm
// =====================================================================

namespace {

// Check if point is inside circumcircle of triangle
bool inCircumcircle(const QPointF& p, const QPointF& a, const QPointF& b, const QPointF& c)
{
    double ax = a.x() - p.x();
    double ay = a.y() - p.y();
    double bx = b.x() - p.x();
    double by = b.y() - p.y();
    double cx = c.x() - p.x();
    double cy = c.y() - p.y();

    double det = (ax * ax + ay * ay) * (bx * cy - cx * by) -
                 (bx * bx + by * by) * (ax * cy - cx * ay) +
                 (cx * cx + cy * cy) * (ax * by - bx * ay);

    // For CCW triangle, point is inside if det > 0
    return det > 0;
}

struct DelaunayTriangle {
    int v[3];
    bool bad = false;

    bool hasVertex(int idx) const {
        return v[0] == idx || v[1] == idx || v[2] == idx;
    }

    bool sharesEdge(const DelaunayTriangle& other) const {
        int shared = 0;
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                if (v[i] == other.v[j]) ++shared;
            }
        }
        return shared == 2;
    }
};

}  // anonymous namespace

QVector<Triangle> delaunayTriangulation(const QVector<QPointF>& points)
{
    if (points.size() < 3) return {};

    // Create super-triangle that contains all points
    BoundingBox bb = polygonBounds(points);
    double dx = bb.maxX - bb.minX;
    double dy = bb.maxY - bb.minY;
    double dmax = qMax(dx, dy) * 2;
    double midX = (bb.minX + bb.maxX) / 2;
    double midY = (bb.minY + bb.maxY) / 2;

    // Super-triangle vertices (indices -3, -2, -1 conceptually, stored at end)
    QVector<QPointF> allPoints = points;
    int superIdx = allPoints.size();
    allPoints.append(QPointF(midX - dmax, midY - dmax));      // super vertex 0
    allPoints.append(QPointF(midX, midY + dmax * 2));          // super vertex 1
    allPoints.append(QPointF(midX + dmax * 2, midY - dmax));  // super vertex 2

    QVector<DelaunayTriangle> triangles;
    triangles.append({{superIdx, superIdx + 1, superIdx + 2}, false});

    // Insert points one at a time
    for (int pi = 0; pi < points.size(); ++pi) {
        const QPointF& p = points[pi];

        // Find triangles whose circumcircle contains the point
        for (auto& tri : triangles) {
            if (tri.bad) continue;
            if (inCircumcircle(p, allPoints[tri.v[0]], allPoints[tri.v[1]], allPoints[tri.v[2]])) {
                tri.bad = true;
            }
        }

        // Find boundary of polygonal hole (edges of bad triangles not shared)
        QVector<Edge> polygon;
        for (int i = 0; i < triangles.size(); ++i) {
            if (!triangles[i].bad) continue;

            for (int e = 0; e < 3; ++e) {
                Edge edge = {triangles[i].v[e], triangles[i].v[(e + 1) % 3]};
                bool shared = false;

                for (int j = 0; j < triangles.size(); ++j) {
                    if (i == j || !triangles[j].bad) continue;
                    for (int e2 = 0; e2 < 3; ++e2) {
                        Edge other = {triangles[j].v[e2], triangles[j].v[(e2 + 1) % 3]};
                        if (edge == other) {
                            shared = true;
                            break;
                        }
                    }
                    if (shared) break;
                }

                if (!shared) {
                    polygon.append(edge);
                }
            }
        }

        // Remove bad triangles
        triangles.erase(std::remove_if(triangles.begin(), triangles.end(),
            [](const DelaunayTriangle& t) { return t.bad; }), triangles.end());

        // Re-triangulate the hole with new triangles connecting to point
        for (const Edge& edge : polygon) {
            triangles.append({{edge.i0, edge.i1, pi}, false});
        }
    }

    // Remove triangles that share vertices with super-triangle
    QVector<Triangle> result;
    for (const auto& tri : triangles) {
        if (!tri.hasVertex(superIdx) && !tri.hasVertex(superIdx + 1) && !tri.hasVertex(superIdx + 2)) {
            result.append({tri.v[0], tri.v[1], tri.v[2]});
        }
    }

    return result;
}

QVector<Triangle> constrainedDelaunay(
    const QVector<QPointF>& points,
    const QVector<Edge>& constrainedEdges)
{
    // Start with regular Delaunay
    QVector<Triangle> triangles = delaunayTriangulation(points);

    // For each constrained edge, ensure it exists in triangulation
    // by flipping edges that cross it
    for (const Edge& ce : constrainedEdges) {
        // Check if edge already exists
        bool exists = false;
        for (const Triangle& tri : triangles) {
            for (int e = 0; e < 3; ++e) {
                int v0 = (e == 0) ? tri.i0 : (e == 1) ? tri.i1 : tri.i2;
                int v1 = (e == 0) ? tri.i1 : (e == 1) ? tri.i2 : tri.i0;
                if ((v0 == ce.i0 && v1 == ce.i1) || (v0 == ce.i1 && v1 == ce.i0)) {
                    exists = true;
                    break;
                }
            }
            if (exists) break;
        }

        if (!exists) {
            // Edge doesn't exist - would need to flip edges to insert it
            // This is complex; for now, we accept the unconstrained result
            // A full implementation would use edge-flip operations
        }
    }

    return triangles;
}

QVector<QVector<QPointF>> voronoiDiagram(
    const QVector<QPointF>& points,
    const QRectF& bounds)
{
    if (points.size() < 2) {
        if (points.size() == 1) {
            // Single point: entire bounds is its cell
            return {{
                bounds.topLeft(),
                bounds.topRight(),
                bounds.bottomRight(),
                bounds.bottomLeft()
            }};
        }
        return {};
    }

    // Compute Delaunay triangulation
    QVector<Triangle> delaunay = delaunayTriangulation(points);

    // For each input point, collect circumcenters of adjacent triangles
    QVector<QVector<QPointF>> cells(points.size());

    // Compute circumcenters
    QVector<QPointF> circumcenters;
    circumcenters.reserve(delaunay.size());

    for (const Triangle& tri : delaunay) {
        const QPointF& a = points[tri.i0];
        const QPointF& b = points[tri.i1];
        const QPointF& c = points[tri.i2];

        // Circumcenter calculation
        double d = 2.0 * (a.x() * (b.y() - c.y()) + b.x() * (c.y() - a.y()) + c.x() * (a.y() - b.y()));
        if (qAbs(d) < DEFAULT_TOLERANCE) {
            circumcenters.append(lineMidpoint(a, b));
            continue;
        }

        double aSq = a.x() * a.x() + a.y() * a.y();
        double bSq = b.x() * b.x() + b.y() * b.y();
        double cSq = c.x() * c.x() + c.y() * c.y();

        double ux = (aSq * (b.y() - c.y()) + bSq * (c.y() - a.y()) + cSq * (a.y() - b.y())) / d;
        double uy = (aSq * (c.x() - b.x()) + bSq * (a.x() - c.x()) + cSq * (b.x() - a.x())) / d;

        circumcenters.append(QPointF(ux, uy));
    }

    // For each point, find its Voronoi cell
    for (int pi = 0; pi < points.size(); ++pi) {
        QVector<QPointF> cellPoints;

        // Find all triangles containing this point
        for (int ti = 0; ti < delaunay.size(); ++ti) {
            const Triangle& tri = delaunay[ti];
            if (tri.i0 == pi || tri.i1 == pi || tri.i2 == pi) {
                cellPoints.append(circumcenters[ti]);
            }
        }

        if (cellPoints.size() >= 3) {
            // Sort points around centroid
            QPointF centroid(0, 0);
            for (const QPointF& p : cellPoints) centroid += p;
            centroid /= cellPoints.size();

            std::sort(cellPoints.begin(), cellPoints.end(), [&](const QPointF& a, const QPointF& b) {
                return qAtan2(a.y() - centroid.y(), a.x() - centroid.x()) <
                       qAtan2(b.y() - centroid.y(), b.x() - centroid.x());
            });

            cells[pi] = cellPoints;
        }
    }

    return cells;
}

// =====================================================================
//  Point Set Analysis
// =====================================================================

QPair<int, int> findDiameter(const QVector<QPointF>& points)
{
    if (points.size() < 2) return {0, 0};

    // Use rotating calipers on convex hull
    QVector<QPointF> hull = convexHull(points);
    if (hull.size() < 2) return {0, 0};

    double maxDist = 0;
    int best1 = 0, best2 = 0;

    // For small hulls, just check all pairs
    for (int i = 0; i < hull.size(); ++i) {
        for (int j = i + 1; j < hull.size(); ++j) {
            double dist = lengthSquared(hull[j] - hull[i]);
            if (dist > maxDist) {
                maxDist = dist;
                best1 = i;
                best2 = j;
            }
        }
    }

    // Map back to original indices
    int idx1 = 0, idx2 = 0;
    for (int i = 0; i < points.size(); ++i) {
        if (points[i] == hull[best1]) idx1 = i;
        if (points[i] == hull[best2]) idx2 = i;
    }

    return {idx1, idx2};
}

QPair<int, int> findClosestPair(const QVector<QPointF>& points)
{
    if (points.size() < 2) return {0, 0};

    // Simple O(n^2) for now - could use divide-and-conquer
    double minDist = std::numeric_limits<double>::max();
    int best1 = 0, best2 = 1;

    for (int i = 0; i < points.size(); ++i) {
        for (int j = i + 1; j < points.size(); ++j) {
            double dist = lengthSquared(points[j] - points[i]);
            if (dist < minDist) {
                minDist = dist;
                best1 = i;
                best2 = j;
            }
        }
    }

    return {best1, best2};
}

double hausdorffDistance(const QVector<QPointF>& set1, const QVector<QPointF>& set2)
{
    if (set1.isEmpty() || set2.isEmpty()) return 0;

    auto maxMinDist = [](const QVector<QPointF>& from, const QVector<QPointF>& to) {
        double maxDist = 0;
        for (const QPointF& p : from) {
            double minDist = std::numeric_limits<double>::max();
            for (const QPointF& q : to) {
                minDist = qMin(minDist, lengthSquared(q - p));
            }
            maxDist = qMax(maxDist, minDist);
        }
        return qSqrt(maxDist);
    };

    return qMax(maxMinDist(set1, set2), maxMinDist(set2, set1));
}

// =====================================================================
//  Curve Analysis
// =====================================================================

QVector<double> polylineCurvature(const QVector<QPointF>& points)
{
    QVector<double> curvature(points.size(), 0.0);
    if (points.size() < 3) return curvature;

    for (int i = 1; i < points.size() - 1; ++i) {
        const QPointF& prev = points[i - 1];
        const QPointF& curr = points[i];
        const QPointF& next = points[i + 1];

        // Menger curvature: 4 * triangle_area / (|a| * |b| * |c|)
        double a = length(curr - prev);
        double b = length(next - curr);
        double c = length(next - prev);

        double area = 0.5 * qAbs(cross(curr - prev, next - prev));

        if (a > DEFAULT_TOLERANCE && b > DEFAULT_TOLERANCE && c > DEFAULT_TOLERANCE) {
            curvature[i] = 4.0 * area / (a * b * c);
        }
    }

    return curvature;
}

QVector<int> findCorners(const QVector<QPointF>& points, double angleThreshold)
{
    QVector<int> corners;
    if (points.size() < 3) return corners;

    for (int i = 1; i < points.size() - 1; ++i) {
        QPointF v1 = points[i] - points[i - 1];
        QPointF v2 = points[i + 1] - points[i];

        double angle = qAbs(signedAngleBetween(v1, v2));
        if (angle > angleThreshold) {
            corners.append(i);
        }
    }

    return corners;
}

QVector<QPointF> smoothPolyline(const QVector<QPointF>& points, int iterations)
{
    if (points.size() < 3 || iterations <= 0) return points;

    QVector<QPointF> result = points;

    for (int iter = 0; iter < iterations; ++iter) {
        QVector<QPointF> newPoints;
        newPoints.reserve(result.size() * 2);

        for (int i = 0; i < result.size() - 1; ++i) {
            QPointF q = result[i] * 0.75 + result[i + 1] * 0.25;
            QPointF r = result[i] * 0.25 + result[i + 1] * 0.75;
            newPoints.append(q);
            newPoints.append(r);
        }

        result = newPoints;
    }

    return result;
}

// =====================================================================
//  Path Operations
// =====================================================================

double pathLength(const QVector<QPointF>& points)
{
    double len = 0;
    for (int i = 1; i < points.size(); ++i) {
        len += length(points[i] - points[i - 1]);
    }
    return len;
}

QVector<QPointF> resamplePath(const QVector<QPointF>& points, double spacing)
{
    if (points.size() < 2 || spacing <= 0) return points;

    double totalLen = pathLength(points);
    int numPoints = qMax(2, static_cast<int>(totalLen / spacing) + 1);

    return resamplePathByCount(points, numPoints);
}

QVector<QPointF> resamplePathByCount(const QVector<QPointF>& points, int numPoints)
{
    if (points.size() < 2 || numPoints < 2) return points;

    double totalLen = pathLength(points);
    double spacing = totalLen / (numPoints - 1);

    QVector<QPointF> result;
    result.reserve(numPoints);
    result.append(points.first());

    double accumulated = 0;
    double nextTarget = spacing;
    int segIndex = 0;
    double segStart = 0;

    while (result.size() < numPoints - 1 && segIndex < points.size() - 1) {
        double segLen = length(points[segIndex + 1] - points[segIndex]);

        while (segStart + segLen >= nextTarget && result.size() < numPoints - 1) {
            double t = (nextTarget - segStart) / segLen;
            result.append(lerp(points[segIndex], points[segIndex + 1], t));
            nextTarget += spacing;
        }

        segStart += segLen;
        ++segIndex;
    }

    result.append(points.last());
    return result;
}

QPointF pointAtArcLength(const QVector<QPointF>& points, double arcLength)
{
    if (points.isEmpty()) return QPointF();
    if (points.size() == 1 || arcLength <= 0) return points.first();

    double accumulated = 0;
    for (int i = 0; i < points.size() - 1; ++i) {
        double segLen = length(points[i + 1] - points[i]);
        if (accumulated + segLen >= arcLength) {
            double t = (arcLength - accumulated) / segLen;
            return lerp(points[i], points[i + 1], t);
        }
        accumulated += segLen;
    }

    return points.last();
}

QPointF tangentAtArcLength(const QVector<QPointF>& points, double arcLength)
{
    if (points.size() < 2) return QPointF(1, 0);

    double accumulated = 0;
    for (int i = 0; i < points.size() - 1; ++i) {
        double segLen = length(points[i + 1] - points[i]);
        if (accumulated + segLen >= arcLength || i == points.size() - 2) {
            return normalize(points[i + 1] - points[i]);
        }
        accumulated += segLen;
    }

    return normalize(points.last() - points[points.size() - 2]);
}

}  // namespace geometry
}  // namespace hobbycad
