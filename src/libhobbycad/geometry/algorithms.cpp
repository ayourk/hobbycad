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

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace hobbycad {
namespace geometry {

// =====================================================================
//  Convex Hull - Andrew's Monotone Chain Algorithm
// =====================================================================

std::vector<Point2D> convexHull(const std::vector<Point2D>& points)
{
    if (points.size() < 3) {
        return points;
    }

    // Sort points lexicographically
    std::vector<Point2D> sorted = points;
    std::sort(sorted.begin(), sorted.end(), [](const Point2D& a, const Point2D& b) {
        return a.x < b.x || (a.x == b.x && a.y < b.y);
    });

    // Remove duplicates
    sorted.erase(std::unique(sorted.begin(), sorted.end(),
        [](const Point2D& a, const Point2D& b) {
            return std::abs(a.x - b.x) < 1e-12 && std::abs(a.y - b.y) < 1e-12;
        }), sorted.end());

    if (sorted.size() < 3) {
        return sorted;
    }

    std::vector<Point2D> hull;
    hull.reserve(sorted.size() * 2);

    // Build lower hull
    for (const Point2D& p : sorted) {
        while (hull.size() >= 2 &&
               cross(hull[hull.size()-1] - hull[hull.size()-2],
                     p - hull[hull.size()-2]) <= 0) {
            hull.pop_back();
        }
        hull.push_back(p);
    }

    // Build upper hull
    int lowerSize = hull.size();
    for (int i = sorted.size() - 2; i >= 0; --i) {
        const Point2D& p = sorted[i];
        while (hull.size() > lowerSize &&
               cross(hull[hull.size()-1] - hull[hull.size()-2],
                     p - hull[hull.size()-2]) <= 0) {
            hull.pop_back();
        }
        hull.push_back(p);
    }

    hull.pop_back();  // Remove duplicate of first point
    return hull;
}

bool isConvex(const std::vector<Point2D>& polygon)
{
    if (polygon.size() < 3) return true;

    bool hasPositive = false;
    bool hasNegative = false;

    for (size_t i = 0; i < polygon.size(); ++i) {
        const Point2D& p0 = polygon[i];
        const Point2D& p1 = polygon[(i + 1) % polygon.size()];
        const Point2D& p2 = polygon[(i + 2) % polygon.size()];

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
double perpendicularDistance(const Point2D& point, const Point2D& lineStart, const Point2D& lineEnd)
{
    double dx = lineEnd.x - lineStart.x;
    double dy = lineEnd.y - lineStart.y;

    double lenSq = dx * dx + dy * dy;
    if (lenSq < DEFAULT_TOLERANCE * DEFAULT_TOLERANCE) {
        return length(point - lineStart);
    }

    double t = ((point.x - lineStart.x) * dx + (point.y - lineStart.y) * dy) / lenSq;
    t = std::clamp(t, 0.0, 1.0);

    Point2D projection(lineStart.x + t * dx, lineStart.y + t * dy);
    return length(point - projection);
}

void douglasPeuckerRecursive(
    const std::vector<Point2D>& points,
    int start, int end,
    double epsilon,
    std::vector<bool>& keep)
{
    double maxDist = 0;
    int maxIdx = start;

    for (int i = start + 1; i < end; ++i) {
        double dist = perpendicularDistance(points[i], points[start], points[end]);
        if (dist > maxDist) {
            maxDist = dist;
            maxIdx = i;
        }
    }

    if (maxDist > epsilon) {
        keep[maxIdx] = true;
        douglasPeuckerRecursive(points, start, maxIdx, epsilon, keep);
        douglasPeuckerRecursive(points, maxIdx, end, epsilon, keep);
    }
}

}  // anonymous namespace

std::vector<Point2D> simplifyPolyline(const std::vector<Point2D>& points, double epsilon)
{
    if (points.size() < 3) return points;
    if (epsilon <= 0) return points;

    std::vector<bool> keep(points.size(), false);
    keep[0] = true;
    keep[points.size() - 1] = true;

    douglasPeuckerRecursive(points, 0, points.size() - 1, epsilon, keep);

    std::vector<Point2D> result;
    for (size_t i = 0; i < points.size(); ++i) {
        if (keep[i]) result.push_back(points[i]);
    }

    return result;
}

std::vector<Point2D> simplifyPolygon(const std::vector<Point2D>& polygon, double epsilon)
{
    if (polygon.size() < 4) return polygon;

    // For polygon, we need to handle the wrap-around
    // Double the polygon, simplify, then take the relevant portion
    std::vector<Point2D> doubled;
    doubled.reserve(polygon.size() * 2);
    doubled.insert(doubled.end(), polygon.begin(), polygon.end());
    doubled.insert(doubled.end(), polygon.begin(), polygon.end());

    std::vector<bool> keep(doubled.size(), false);

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

    std::vector<Point2D> result;
    for (int i = anchor; i <= anchor + n; ++i) {
        if (keep[i]) {
            result.push_back(doubled[i % n]);
        }
    }

    // Remove duplicate last point if present
    if (result.size() >= 2 &&
        std::abs(result.front().x - result.back().x) < DEFAULT_TOLERANCE &&
        std::abs(result.front().y - result.back().y) < DEFAULT_TOLERANCE) {
        result.pop_back();
    }

    return result;
}

// =====================================================================
//  Visvalingam-Whyatt Simplification
// =====================================================================

std::vector<Point2D> simplifyByArea(const std::vector<Point2D>& points, double minArea)
{
    if (points.size() < 3) return points;

    // Visvalingam-Whyatt: iteratively remove point forming smallest triangle
    struct PointData {
        int index;
        double area;
        bool removed = false;
    };

    std::vector<PointData> data(points.size());
    for (size_t i = 0; i < points.size(); ++i) {
        data[i].index = i;
        data[i].removed = false;
    }

    auto calcArea = [&](int idx) -> double {
        if (idx <= 0 || idx >= static_cast<int>(points.size()) - 1) return std::numeric_limits<double>::max();

        // Find prev and next non-removed points
        int prev = idx - 1;
        while (prev >= 0 && data[prev].removed) --prev;
        int next = idx + 1;
        while (next < static_cast<int>(points.size()) && data[next].removed) ++next;

        if (prev < 0 || next >= static_cast<int>(points.size())) return std::numeric_limits<double>::max();

        // Triangle area
        return 0.5 * std::abs(cross(points[idx] - points[prev], points[next] - points[prev]));
    };

    // Initialize areas
    for (size_t i = 1; i < points.size() - 1; ++i) {
        data[i].area = calcArea(i);
    }
    data[0].area = std::numeric_limits<double>::max();
    data[points.size()-1].area = std::numeric_limits<double>::max();

    // Use priority queue for efficient minimum finding
    auto cmp = [](const PointData* a, const PointData* b) { return a->area > b->area; };
    std::priority_queue<PointData*, std::vector<PointData*>, decltype(cmp)> pq(cmp);

    for (size_t i = 1; i < points.size() - 1; ++i) {
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
        while (next < static_cast<int>(points.size()) && data[next].removed) ++next;

        if (prev > 0) {
            data[prev].area = calcArea(prev);
            pq.push(&data[prev]);
        }
        if (next < static_cast<int>(points.size()) - 1) {
            data[next].area = calcArea(next);
            pq.push(&data[next]);
        }
    }

    std::vector<Point2D> result;
    for (size_t i = 0; i < points.size(); ++i) {
        if (!data[i].removed) result.push_back(points[i]);
    }

    return result;
}

// =====================================================================
//  Minimal Bounding Circle - Welzl's Algorithm
// =====================================================================

namespace {

MinimalBoundingCircle circleFromThreePoints(const Point2D& a, const Point2D& b, const Point2D& c)
{
    double ax = a.x, ay = a.y;
    double bx = b.x, by = b.y;
    double cx = c.x, cy = c.y;

    double d = 2.0 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by));
    if (std::abs(d) < DEFAULT_TOLERANCE) {
        // Degenerate: return circle from two farthest points
        double d1 = lengthSquared(b - a);
        double d2 = lengthSquared(c - b);
        double d3 = lengthSquared(a - c);
        if (d1 >= d2 && d1 >= d3) return {Point2D((ax+bx)/2, (ay+by)/2), std::sqrt(d1)/2};
        if (d2 >= d3) return {Point2D((bx+cx)/2, (by+cy)/2), std::sqrt(d2)/2};
        return {Point2D((ax+cx)/2, (ay+cy)/2), std::sqrt(d3)/2};
    }

    double ux = ((ax*ax + ay*ay) * (by - cy) + (bx*bx + by*by) * (cy - ay) + (cx*cx + cy*cy) * (ay - by)) / d;
    double uy = ((ax*ax + ay*ay) * (cx - bx) + (bx*bx + by*by) * (ax - cx) + (cx*cx + cy*cy) * (bx - ax)) / d;

    Point2D center(ux, uy);
    return {center, length(a - center)};
}

MinimalBoundingCircle circleFromTwoPoints(const Point2D& a, const Point2D& b)
{
    return {Point2D((a.x + b.x) / 2, (a.y + b.y) / 2), length(b - a) / 2};
}

MinimalBoundingCircle welzlRecursive(
    const std::vector<Point2D>& points, int n,
    std::vector<Point2D>& boundary, int b)
{
    if (n == 0 || b == 3) {
        if (b == 0) return {Point2D(0, 0), 0};
        if (b == 1) return {boundary[0], 0};
        if (b == 2) return circleFromTwoPoints(boundary[0], boundary[1]);
        return circleFromThreePoints(boundary[0], boundary[1], boundary[2]);
    }

    // Pick random point
    int idx = n - 1;  // Could randomize for better average case
    Point2D p = points[idx];

    MinimalBoundingCircle circle = welzlRecursive(points, n - 1, boundary, b);

    if (length(p - circle.center) <= circle.radius + DEFAULT_TOLERANCE) {
        return circle;
    }

    // Point is outside, must be on boundary
    boundary[b] = p;
    return welzlRecursive(points, n - 1, boundary, b + 1);
}

}  // anonymous namespace

MinimalBoundingCircle minimalBoundingCircle(const std::vector<Point2D>& points)
{
    if (points.empty()) return {Point2D(0, 0), 0};
    if (points.size() == 1) return {points[0], 0};
    if (points.size() == 2) return circleFromTwoPoints(points[0], points[1]);

    // Shuffle for expected O(n) performance
    std::vector<Point2D> shuffled = points;
    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(shuffled.begin(), shuffled.end(), g);

    std::vector<Point2D> boundary(3);
    return welzlRecursive(shuffled, shuffled.size(), boundary, 0);
}

// =====================================================================
//  Oriented Bounding Box - Rotating Calipers
// =====================================================================

std::vector<Point2D> OrientedBoundingBox::corners() const
{
    std::vector<Point2D> result(4);
    double c = std::cos(angle * M_PI / 180.0);
    double s = std::sin(angle * M_PI / 180.0);

    Point2D xAxis(c * halfExtents.x, s * halfExtents.x);
    Point2D yAxis(-s * halfExtents.y, c * halfExtents.y);

    result[0] = center - xAxis - yAxis;
    result[1] = center + xAxis - yAxis;
    result[2] = center + xAxis + yAxis;
    result[3] = center - xAxis + yAxis;

    return result;
}

double OrientedBoundingBox::area() const
{
    return 4.0 * halfExtents.x * halfExtents.y;
}

bool OrientedBoundingBox::contains(const Point2D& point) const
{
    // Transform point to local coordinates
    Point2D local = point - center;
    double c = std::cos(-angle * M_PI / 180.0);
    double s = std::sin(-angle * M_PI / 180.0);
    Point2D rotated(local.x * c - local.y * s, local.x * s + local.y * c);

    return std::abs(rotated.x) <= halfExtents.x + DEFAULT_TOLERANCE &&
           std::abs(rotated.y) <= halfExtents.y + DEFAULT_TOLERANCE;
}

OrientedBoundingBox minimalOrientedBoundingBox(const std::vector<Point2D>& points)
{
    if (points.empty()) return {};

    std::vector<Point2D> hull = convexHull(points);
    if (hull.size() < 3) {
        // Degenerate case - single point or line
        BoundingBox aabb = polygonBounds(points);
        OrientedBoundingBox result;
        result.center = Point2D((aabb.minX + aabb.maxX) / 2,
                                (aabb.minY + aabb.maxY) / 2);
        result.halfExtents = Point2D((aabb.maxX - aabb.minX) / 2,
                                     (aabb.maxY - aabb.minY) / 2);
        result.angle = 0;
        return result;
    }

    double minArea = std::numeric_limits<double>::max();
    OrientedBoundingBox best;

    // For each edge of the hull, compute the bounding box aligned to that edge
    for (size_t i = 0; i < hull.size(); ++i) {
        Point2D edge = hull[(i + 1) % hull.size()] - hull[i];
        double edgeAngle = std::atan2(edge.y, edge.x) * 180.0 / M_PI;

        // Rotate all points to align this edge with x-axis
        double c = std::cos(-edgeAngle * M_PI / 180.0);
        double s = std::sin(-edgeAngle * M_PI / 180.0);

        double minX = std::numeric_limits<double>::max();
        double maxX = std::numeric_limits<double>::lowest();
        double minY = std::numeric_limits<double>::max();
        double maxY = std::numeric_limits<double>::lowest();

        for (const Point2D& p : hull) {
            double rx = p.x * c - p.y * s;
            double ry = p.x * s + p.y * c;
            minX = std::min(minX, rx);
            maxX = std::max(maxX, rx);
            minY = std::min(minY, ry);
            maxY = std::max(maxY, ry);
        }

        double area = (maxX - minX) * (maxY - minY);
        if (area < minArea) {
            minArea = area;

            // Compute center in original coordinates
            double cx = (minX + maxX) / 2;
            double cy = (minY + maxY) / 2;
            double cr = std::cos(edgeAngle * M_PI / 180.0);
            double sr = std::sin(edgeAngle * M_PI / 180.0);

            best.center = Point2D(cx * cr - cy * sr, cx * sr + cy * cr);
            best.halfExtents = Point2D((maxX - minX) / 2, (maxY - minY) / 2);
            best.angle = edgeAngle;
        }
    }

    return best;
}

BoundingBox obbToAABB(const OrientedBoundingBox& obb)
{
    std::vector<Point2D> corners = obb.corners();
    return polygonBounds(corners);
}

// =====================================================================
//  2D Boolean Operations
// =====================================================================
//  Uses Sutherland-Hodgman for intersection and Weiler-Atherton style
//  edge-walking for union/difference/XOR operations.

namespace {

// Sutherland-Hodgman polygon clipping (for convex clipping polygons)
std::vector<Point2D> clipPolygonByEdge(
    const std::vector<Point2D>& polygon,
    const Point2D& edgeStart, const Point2D& edgeEnd)
{
    if (polygon.empty()) return {};

    std::vector<Point2D> output;
    Point2D edgeDir = edgeEnd - edgeStart;

    auto inside = [&](const Point2D& p) {
        return cross(edgeDir, p - edgeStart) >= 0;
    };

    auto intersect = [&](const Point2D& a, const Point2D& b) -> Point2D {
        Point2D dir = b - a;
        double denom = cross(dir, edgeDir);
        if (std::abs(denom) < DEFAULT_TOLERANCE) return a;
        double t = cross(edgeDir, a - edgeStart) / denom;
        return a - t * dir;
    };

    for (size_t i = 0; i < polygon.size(); ++i) {
        const Point2D& current = polygon[i];
        const Point2D& next = polygon[(i + 1) % polygon.size()];

        bool currentInside = inside(current);
        bool nextInside = inside(next);

        if (currentInside) {
            output.push_back(current);
            if (!nextInside) {
                output.push_back(intersect(current, next));
            }
        } else if (nextInside) {
            output.push_back(intersect(current, next));
        }
    }

    return output;
}

// Find all intersection points between two polygon edges
struct EdgeIntersection {
    Point2D point;
    int edge1;      // Edge index in poly1
    double t1;      // Parameter on edge1
    int edge2;      // Edge index in poly2
    double t2;      // Parameter on edge2
    bool entering;  // True if entering poly2 from outside
};

std::vector<EdgeIntersection> findPolygonIntersections(
    const std::vector<Point2D>& poly1,
    const std::vector<Point2D>& poly2)
{
    std::vector<EdgeIntersection> intersections;

    for (size_t i = 0; i < poly1.size(); ++i) {
        const Point2D& a1 = poly1[i];
        const Point2D& b1 = poly1[(i + 1) % poly1.size()];
        Point2D d1 = b1 - a1;

        for (size_t j = 0; j < poly2.size(); ++j) {
            const Point2D& a2 = poly2[j];
            const Point2D& b2 = poly2[(j + 1) % poly2.size()];
            Point2D d2 = b2 - a2;

            double denom = cross(d1, d2);
            if (std::abs(denom) < DEFAULT_TOLERANCE) continue;

            Point2D diff = a2 - a1;
            double t1 = cross(diff, d2) / denom;
            double t2 = cross(diff, d1) / denom;

            if (t1 > DEFAULT_TOLERANCE && t1 < 1.0 - DEFAULT_TOLERANCE &&
                t2 > DEFAULT_TOLERANCE && t2 < 1.0 - DEFAULT_TOLERANCE) {
                Point2D pt = a1 + t1 * d1;
                // Determine if entering or leaving poly2
                // Cross product of edge direction with poly2 edge normal
                Point2D n2 = perpendicular(d2);
                bool entering = dot(d1, n2) > 0;
                intersections.push_back({pt, static_cast<int>(i), t1, static_cast<int>(j), t2, entering});
            }
        }
    }

    return intersections;
}

// Build result polygon by walking edges (Weiler-Atherton style)
std::vector<Point2D> walkPolygonBoundary(
    const std::vector<Point2D>& poly1,
    const std::vector<Point2D>& poly2,
    const std::vector<EdgeIntersection>& intersections,
    bool walkInside)  // true for intersection, false for union exterior
{
    if (intersections.empty()) return {};

    std::vector<Point2D> result;
    std::vector<bool> visited(intersections.size(), false);

    // Find first unvisited entering intersection
    int startIdx = -1;
    for (size_t i = 0; i < intersections.size(); ++i) {
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
        result.push_back(intersections[currentIdx].point);

        const EdgeIntersection& curr = intersections[currentIdx];

        // Walk along current polygon to next intersection
        const std::vector<Point2D>& currentPoly = onPoly1 ? poly1 : poly2;
        int edge = onPoly1 ? curr.edge1 : curr.edge2;
        double t = onPoly1 ? curr.t1 : curr.t2;

        // Find next intersection on this polygon
        double nextT = 2.0;
        int nextIdx = -1;
        int nextEdge = edge;

        // Check remaining intersections on current edge
        for (size_t i = 0; i < intersections.size(); ++i) {
            if (static_cast<int>(i) == currentIdx) continue;
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
            for (size_t step = 1; step <= currentPoly.size(); ++step) {
                int checkEdge = (edge + step) % currentPoly.size();
                result.push_back(currentPoly[(edge + step) % currentPoly.size()]);

                // Find first intersection on this edge
                double minT = 2.0;
                for (size_t i = 0; i < intersections.size(); ++i) {
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

BooleanResult polygonIntersection(const std::vector<Point2D>& poly1, const std::vector<Point2D>& poly2)
{
    BooleanResult result;
    if (poly1.size() < 3 || poly2.size() < 3) {
        result.error = "Polygons must have at least 3 vertices";
        return result;
    }

    // Check containment first
    bool allPoly1InPoly2 = true;
    bool allPoly2InPoly1 = true;
    for (const Point2D& p : poly1) {
        if (!pointInPolygon(p, poly2)) {
            allPoly1InPoly2 = false;
            break;
        }
    }
    for (const Point2D& p : poly2) {
        if (!pointInPolygon(p, poly1)) {
            allPoly2InPoly1 = false;
            break;
        }
    }

    if (allPoly1InPoly2) {
        PolygonWithHoles pwh;
        pwh.outer = poly1;
        result.polygons.push_back(pwh);
        result.success = true;
        return result;
    }
    if (allPoly2InPoly1) {
        PolygonWithHoles pwh;
        pwh.outer = poly2;
        result.polygons.push_back(pwh);
        result.success = true;
        return result;
    }

    // Use Sutherland-Hodgman clipping
    std::vector<Point2D> clipped = poly1;
    for (size_t i = 0; i < poly2.size(); ++i) {
        clipped = clipPolygonByEdge(clipped, poly2[i], poly2[(i + 1) % poly2.size()]);
        if (clipped.empty()) {
            result.success = true;  // No intersection is a valid result
            return result;
        }
    }

    if (clipped.size() >= 3) {
        PolygonWithHoles pwh;
        pwh.outer = clipped;
        result.polygons.push_back(pwh);
    }

    result.success = true;
    return result;
}

BooleanResult polygonUnion(const std::vector<Point2D>& poly1, const std::vector<Point2D>& poly2)
{
    BooleanResult result;
    if (poly1.size() < 3 || poly2.size() < 3) {
        result.error = "Polygons must have at least 3 vertices";
        return result;
    }

    // Check containment
    bool allPoly2InPoly1 = true;
    for (const Point2D& p : poly2) {
        if (!pointInPolygon(p, poly1)) {
            allPoly2InPoly1 = false;
            break;
        }
    }
    if (allPoly2InPoly1) {
        PolygonWithHoles pwh;
        pwh.outer = poly1;
        result.polygons.push_back(pwh);
        result.success = true;
        return result;
    }

    bool allPoly1InPoly2 = true;
    for (const Point2D& p : poly1) {
        if (!pointInPolygon(p, poly2)) {
            allPoly1InPoly2 = false;
            break;
        }
    }
    if (allPoly1InPoly2) {
        PolygonWithHoles pwh;
        pwh.outer = poly2;
        result.polygons.push_back(pwh);
        result.success = true;
        return result;
    }

    // Find intersections
    auto intersections = findPolygonIntersections(poly1, poly2);

    if (intersections.empty()) {
        // No intersections and no containment - disjoint polygons
        PolygonWithHoles pwh1, pwh2;
        pwh1.outer = poly1;
        pwh2.outer = poly2;
        result.polygons.push_back(pwh1);
        result.polygons.push_back(pwh2);
        result.success = true;
        return result;
    }

    // Build union boundary
    std::vector<Point2D> unionBoundary = walkPolygonBoundary(poly1, poly2, intersections, false);

    if (unionBoundary.size() >= 3) {
        PolygonWithHoles pwh;
        pwh.outer = unionBoundary;
        result.polygons.push_back(pwh);
        result.success = true;
    } else {
        // Fallback: return convex hull of both polygons
        std::vector<Point2D> allPoints;
        allPoints.insert(allPoints.end(), poly1.begin(), poly1.end());
        allPoints.insert(allPoints.end(), poly2.begin(), poly2.end());
        PolygonWithHoles pwh;
        pwh.outer = convexHull(allPoints);
        result.polygons.push_back(pwh);
        result.success = true;
    }

    return result;
}

BooleanResult polygonDifference(const std::vector<Point2D>& poly1, const std::vector<Point2D>& poly2)
{
    BooleanResult result;
    if (poly1.size() < 3 || poly2.size() < 3) {
        result.error = "Polygons must have at least 3 vertices";
        return result;
    }

    // Check if poly2 is fully outside poly1
    bool anyPoly2InPoly1 = false;
    for (const Point2D& p : poly2) {
        if (pointInPolygon(p, poly1)) {
            anyPoly2InPoly1 = true;
            break;
        }
    }
    // Also check if any poly2 edges intersect poly1
    auto intersections = findPolygonIntersections(poly1, poly2);

    if (!anyPoly2InPoly1 && intersections.empty()) {
        // poly2 is completely outside - return poly1 unchanged
        PolygonWithHoles pwh;
        pwh.outer = poly1;
        result.polygons.push_back(pwh);
        result.success = true;
        return result;
    }

    // Check if poly2 fully contains poly1
    bool allPoly1InPoly2 = true;
    for (const Point2D& p : poly1) {
        if (!pointInPolygon(p, poly2)) {
            allPoly1InPoly2 = false;
            break;
        }
    }
    if (allPoly1InPoly2 && intersections.empty()) {
        // poly1 - poly2 = empty
        result.success = true;
        return result;
    }

    // Check if poly2 is fully inside poly1 (creates a hole)
    bool allPoly2InPoly1 = true;
    for (const Point2D& p : poly2) {
        if (!pointInPolygon(p, poly1)) {
            allPoly2InPoly1 = false;
            break;
        }
    }
    if (allPoly2InPoly1 && intersections.empty()) {
        PolygonWithHoles pwh;
        pwh.outer = poly1;
        // Reverse poly2 to make it a hole (CW)
        std::vector<Point2D> hole = poly2;
        if (polygonIsCCW(hole)) {
            std::reverse(hole.begin(), hole.end());
        }
        pwh.holes.push_back(hole);
        result.polygons.push_back(pwh);
        result.success = true;
        return result;
    }

    // Complex case: use clipping
    // Clip poly1 by the exterior of poly2 (reverse each edge of poly2)
    std::vector<Point2D> clipped = poly1;
    for (size_t i = 0; i < poly2.size(); ++i) {
        // Clip by reversed edge (exterior of poly2)
        clipped = clipPolygonByEdge(clipped,
            poly2[(i + 1) % poly2.size()], poly2[i]);
        if (clipped.empty()) {
            result.success = true;  // poly1 fully consumed
            return result;
        }
    }

    if (clipped.size() >= 3) {
        PolygonWithHoles pwh;
        pwh.outer = clipped;
        result.polygons.push_back(pwh);
    }
    result.success = true;
    return result;
}

BooleanResult polygonXOR(const std::vector<Point2D>& poly1, const std::vector<Point2D>& poly2)
{
    BooleanResult result;

    // XOR = (A - B) + (B - A) = Union - Intersection
    auto diff1 = polygonDifference(poly1, poly2);
    auto diff2 = polygonDifference(poly2, poly1);

    if (diff1.success) {
        result.polygons.insert(result.polygons.end(), diff1.polygons.begin(), diff1.polygons.end());
    }
    if (diff2.success) {
        result.polygons.insert(result.polygons.end(), diff2.polygons.begin(), diff2.polygons.end());
    }

    result.success = diff1.success || diff2.success;
    return result;
}

// =====================================================================
//  Polygon Offset (Simplified)
// =====================================================================

std::vector<std::vector<Point2D>> offsetPolygon(
    const std::vector<Point2D>& polygon,
    double distance,
    int joinType,
    double miterLimit)
{
    if (polygon.size() < 3 || std::abs(distance) < DEFAULT_TOLERANCE) {
        return {polygon};
    }

    std::vector<Point2D> result;
    result.reserve(polygon.size() * (joinType == 1 ? 8 : 1));

    for (size_t i = 0; i < polygon.size(); ++i) {
        const Point2D& prev = polygon[(i + polygon.size() - 1) % polygon.size()];
        const Point2D& curr = polygon[i];
        const Point2D& next = polygon[(i + 1) % polygon.size()];

        // Edge directions
        Point2D dir1 = normalize(curr - prev);
        Point2D dir2 = normalize(next - curr);

        // Normals (perpendicular, pointing outward for CCW polygon)
        Point2D n1 = perpendicular(dir1);
        Point2D n2 = perpendicular(dir2);

        // Offset points along normals
        Point2D p1 = curr + n1 * distance;
        Point2D p2 = curr + n2 * distance;

        double crossVal = cross(dir1, dir2);

        if (std::abs(crossVal) < DEFAULT_TOLERANCE) {
            // Parallel edges
            result.push_back(p1);
        } else if (joinType == 0) {
            // Miter join
            double miterDist = distance / std::sin(std::acos(std::clamp(dot(n1, n2), -1.0, 1.0)) / 2.0);
            if (std::abs(miterDist) < miterLimit * std::abs(distance)) {
                Point2D miterDir = normalize(n1 + n2);
                result.push_back(curr + miterDir * miterDist);
            } else {
                result.push_back(p1);
                result.push_back(p2);
            }
        } else if (joinType == 1) {
            // Round join
            double angle1 = vectorAngle(n1);
            double angle2 = vectorAngle(n2);
            double sweep = angle2 - angle1;
            if (sweep > 180) sweep -= 360;
            if (sweep < -180) sweep += 360;

            int segments = std::max(2, static_cast<int>(std::abs(sweep) / 15.0));
            for (int j = 0; j <= segments; ++j) {
                double t = static_cast<double>(j) / segments;
                double a = (angle1 + t * sweep) * M_PI / 180.0;
                result.push_back(curr + Point2D(std::cos(a), std::sin(a)) * std::abs(distance));
            }
        } else {
            // Square join
            result.push_back(p1);
            result.push_back(p2);
        }
    }

    return {result};
}

std::vector<std::vector<Point2D>> offsetPolyline(
    const std::vector<Point2D>& polyline,
    double distance,
    int endType,
    int joinType)
{
    if (polyline.size() < 2) return {};

    // Create closed polygon by going forward on left side, backward on right
    std::vector<Point2D> closed;

    // Left side (forward)
    for (size_t i = 0; i < polyline.size() - 1; ++i) {
        Point2D dir = normalize(polyline[i + 1] - polyline[i]);
        Point2D normal = perpendicular(dir);
        closed.push_back(polyline[i] + normal * distance);
    }

    // End cap
    Point2D lastDir = normalize(polyline.back() - polyline[polyline.size() - 2]);
    Point2D lastNormal = perpendicular(lastDir);

    if (endType == 1) {
        // Round end
        for (int j = 0; j <= 8; ++j) {
            double angle = (90.0 - 180.0 * j / 8) * M_PI / 180.0;
            Point2D offset = rotatePoint(lastNormal * distance, -j * 180.0 / 8);
            closed.push_back(polyline.back() + offset);
        }
    } else if (endType == 2) {
        // Square end
        closed.push_back(polyline.back() + lastNormal * distance + lastDir * std::abs(distance));
        closed.push_back(polyline.back() - lastNormal * distance + lastDir * std::abs(distance));
    } else {
        // Butt end
        closed.push_back(polyline.back() + lastNormal * distance);
        closed.push_back(polyline.back() - lastNormal * distance);
    }

    // Right side (backward)
    for (int i = polyline.size() - 1; i > 0; --i) {
        Point2D dir = normalize(polyline[i - 1] - polyline[i]);
        Point2D normal = perpendicular(dir);
        closed.push_back(polyline[i] + normal * distance);
    }

    // Start cap
    Point2D firstDir = normalize(polyline[1] - polyline[0]);
    Point2D firstNormal = perpendicular(firstDir);

    if (endType == 1) {
        for (int j = 0; j <= 8; ++j) {
            double angle = (-90.0 + 180.0 * j / 8) * M_PI / 180.0;
            closed.push_back(polyline[0] - rotatePoint(firstNormal * distance, j * 180.0 / 8));
        }
    } else if (endType == 2) {
        closed.push_back(polyline[0] - firstNormal * distance - firstDir * std::abs(distance));
        closed.push_back(polyline[0] + firstNormal * distance - firstDir * std::abs(distance));
    }

    return {closed};
}

// =====================================================================
//  Triangulation - Ear Clipping
// =====================================================================

namespace {

bool isEar(const std::vector<Point2D>& polygon, int i, const std::vector<bool>& removed)
{
    int n = polygon.size();

    // Find prev and next non-removed vertices
    int prev = (i + n - 1) % n;
    while (removed[prev]) prev = (prev + n - 1) % n;
    int next = (i + 1) % n;
    while (removed[next]) next = (next + 1) % n;

    const Point2D& a = polygon[prev];
    const Point2D& b = polygon[i];
    const Point2D& c = polygon[next];

    // Check if convex (CCW)
    if (cross(b - a, c - b) <= 0) return false;

    // Check that no other vertex is inside this triangle
    for (int j = 0; j < n; ++j) {
        if (removed[j] || j == prev || j == i || j == next) continue;

        const Point2D& p = polygon[j];

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

std::vector<Triangle> triangulatePolygon(const std::vector<Point2D>& polygon)
{
    std::vector<Triangle> triangles;
    if (polygon.size() < 3) return triangles;

    // Ensure CCW winding
    std::vector<Point2D> poly = polygon;
    if (!polygonIsCCW(poly)) {
        std::reverse(poly.begin(), poly.end());
    }

    std::vector<bool> removed(poly.size(), false);
    int remaining = poly.size();

    while (remaining > 3) {
        bool foundEar = false;
        for (size_t i = 0; i < poly.size(); ++i) {
            if (removed[i]) continue;
            if (isEar(poly, i, removed)) {
                // Find prev and next
                int prev = (i + poly.size() - 1) % poly.size();
                while (removed[prev]) prev = (prev + poly.size() - 1) % poly.size();
                int next = (i + 1) % poly.size();
                while (removed[next]) next = (next + 1) % poly.size();

                triangles.push_back({prev, static_cast<int>(i), next});
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
        std::vector<int> indices;
        for (size_t i = 0; i < poly.size(); ++i) {
            if (!removed[i]) indices.push_back(i);
        }
        if (indices.size() == 3) {
            triangles.push_back({indices[0], indices[1], indices[2]});
        }
    }

    return triangles;
}

std::pair<std::vector<Point2D>, std::vector<Triangle>> triangulatePolygonWithHoles(
    const std::vector<Point2D>& outer,
    const std::vector<std::vector<Point2D>>& holes)
{
    if (holes.empty()) {
        return {outer, triangulatePolygon(outer)};
    }

    // Bridge holes to outer boundary using horizontal cuts
    // Algorithm: For each hole, find the rightmost point, then find a visible
    // point on the outer boundary or another hole, and insert a bridge (two edges)

    std::vector<Point2D> combined;
    combined.reserve(outer.size() + holes.size() * 2);

    // Ensure outer is CCW
    std::vector<Point2D> outerCCW = outer;
    if (!polygonIsCCW(outerCCW)) {
        std::reverse(outerCCW.begin(), outerCCW.end());
    }

    // Ensure holes are CW (so when combined they work correctly)
    std::vector<std::vector<Point2D>> holesCW;
    for (const auto& hole : holes) {
        std::vector<Point2D> h = hole;
        if (polygonIsCCW(h)) {
            std::reverse(h.begin(), h.end());
        }
        holesCW.push_back(h);
    }

    // Sort holes by rightmost x-coordinate (process right to left)
    std::vector<int> holeOrder(holesCW.size());
    for (size_t i = 0; i < holesCW.size(); ++i) holeOrder[i] = i;
    std::sort(holeOrder.begin(), holeOrder.end(), [&](int a, int b) {
        double maxXa = std::numeric_limits<double>::lowest();
        double maxXb = std::numeric_limits<double>::lowest();
        for (const auto& p : holesCW[a]) maxXa = std::max(maxXa, p.x);
        for (const auto& p : holesCW[b]) maxXb = std::max(maxXb, p.x);
        return maxXa > maxXb;
    });

    // Start with outer boundary
    combined = outerCCW;

    // Insert each hole with a bridge
    for (int hi : holeOrder) {
        const std::vector<Point2D>& hole = holesCW[hi];
        if (hole.empty()) continue;

        // Find rightmost point of hole
        int rightmostIdx = 0;
        for (size_t i = 1; i < hole.size(); ++i) {
            if (hole[i].x > hole[rightmostIdx].x) {
                rightmostIdx = i;
            }
        }
        Point2D holePoint = hole[rightmostIdx];

        // Find visible point on combined polygon (shoot ray to the right)
        int bestIdx = -1;
        double bestX = std::numeric_limits<double>::max();

        for (size_t i = 0; i < combined.size(); ++i) {
            const Point2D& p1 = combined[i];
            const Point2D& p2 = combined[(i + 1) % combined.size()];

            // Check if edge crosses the horizontal ray from holePoint
            if ((p1.y <= holePoint.y && p2.y > holePoint.y) ||
                (p2.y <= holePoint.y && p1.y > holePoint.y)) {
                // Find intersection x
                double t = (holePoint.y - p1.y) / (p2.y - p1.y);
                double x = p1.x + t * (p2.x - p1.x);
                if (x > holePoint.x && x < bestX) {
                    bestX = x;
                    // Choose the endpoint that's to the right
                    bestIdx = (p1.x > p2.x) ? i : (i + 1) % combined.size();
                }
            }
        }

        // If no intersection found, use closest point
        if (bestIdx < 0) {
            double minDist = std::numeric_limits<double>::max();
            for (size_t i = 0; i < combined.size(); ++i) {
                if (combined[i].x > holePoint.x) {
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
        std::vector<Point2D> newCombined;
        newCombined.reserve(combined.size() + hole.size() + 2);

        for (int i = 0; i <= bestIdx; ++i) {
            newCombined.push_back(combined[i]);
        }

        // Insert hole starting from rightmost point
        for (size_t i = 0; i < hole.size(); ++i) {
            newCombined.push_back(hole[(rightmostIdx + i) % hole.size()]);
        }
        // Close the bridge back to hole start
        newCombined.push_back(hole[rightmostIdx]);
        // Back to outer
        newCombined.push_back(combined[bestIdx]);

        for (size_t i = bestIdx + 1; i < combined.size(); ++i) {
            newCombined.push_back(combined[i]);
        }

        combined = newCombined;
    }

    // Now triangulate the combined polygon
    std::vector<Triangle> triangles = triangulatePolygon(combined);

    return {combined, triangles};
}

// =====================================================================
//  Delaunay Triangulation - Bowyer-Watson Algorithm
// =====================================================================

namespace {

// Check if point is inside circumcircle of triangle
bool inCircumcircle(const Point2D& p, const Point2D& a, const Point2D& b, const Point2D& c)
{
    double ax = a.x - p.x;
    double ay = a.y - p.y;
    double bx = b.x - p.x;
    double by = b.y - p.y;
    double cx = c.x - p.x;
    double cy = c.y - p.y;

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

std::vector<Triangle> delaunayTriangulation(const std::vector<Point2D>& points)
{
    if (points.size() < 3) return {};

    // Create super-triangle that contains all points
    BoundingBox bb = polygonBounds(points);
    double dx = bb.maxX - bb.minX;
    double dy = bb.maxY - bb.minY;
    double dmax = std::max(dx, dy) * 2;
    double midX = (bb.minX + bb.maxX) / 2;
    double midY = (bb.minY + bb.maxY) / 2;

    // Super-triangle vertices (indices -3, -2, -1 conceptually, stored at end)
    std::vector<Point2D> allPoints = points;
    int superIdx = allPoints.size();
    allPoints.push_back(Point2D(midX - dmax, midY - dmax));      // super vertex 0
    allPoints.push_back(Point2D(midX, midY + dmax * 2));          // super vertex 1
    allPoints.push_back(Point2D(midX + dmax * 2, midY - dmax));  // super vertex 2

    std::vector<DelaunayTriangle> triangles;
    triangles.push_back({{superIdx, superIdx + 1, superIdx + 2}, false});

    // Insert points one at a time
    for (size_t pi = 0; pi < points.size(); ++pi) {
        const Point2D& p = points[pi];

        // Find triangles whose circumcircle contains the point
        for (auto& tri : triangles) {
            if (tri.bad) continue;
            if (inCircumcircle(p, allPoints[tri.v[0]], allPoints[tri.v[1]], allPoints[tri.v[2]])) {
                tri.bad = true;
            }
        }

        // Find boundary of polygonal hole (edges of bad triangles not shared)
        std::vector<Edge> polygon;
        for (size_t i = 0; i < triangles.size(); ++i) {
            if (!triangles[i].bad) continue;

            for (int e = 0; e < 3; ++e) {
                Edge edge = {triangles[i].v[e], triangles[i].v[(e + 1) % 3]};
                bool shared = false;

                for (size_t j = 0; j < triangles.size(); ++j) {
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
                    polygon.push_back(edge);
                }
            }
        }

        // Remove bad triangles
        triangles.erase(std::remove_if(triangles.begin(), triangles.end(),
            [](const DelaunayTriangle& t) { return t.bad; }), triangles.end());

        // Re-triangulate the hole with new triangles connecting to point
        for (const Edge& edge : polygon) {
            triangles.push_back({{edge.i0, edge.i1, static_cast<int>(pi)}, false});
        }
    }

    // Remove triangles that share vertices with super-triangle
    std::vector<Triangle> result;
    for (const auto& tri : triangles) {
        if (!tri.hasVertex(superIdx) && !tri.hasVertex(superIdx + 1) && !tri.hasVertex(superIdx + 2)) {
            result.push_back({tri.v[0], tri.v[1], tri.v[2]});
        }
    }

    return result;
}

std::vector<Triangle> constrainedDelaunay(
    const std::vector<Point2D>& points,
    const std::vector<Edge>& constrainedEdges)
{
    // Start with regular Delaunay
    std::vector<Triangle> triangles = delaunayTriangulation(points);

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

std::vector<std::vector<Point2D>> voronoiDiagram(
    const std::vector<Point2D>& points,
    const Rect2D& bounds)
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
    std::vector<Triangle> delaunay = delaunayTriangulation(points);

    // For each input point, collect circumcenters of adjacent triangles
    std::vector<std::vector<Point2D>> cells(points.size());

    // Compute circumcenters
    std::vector<Point2D> circumcenters;
    circumcenters.reserve(delaunay.size());

    for (const Triangle& tri : delaunay) {
        const Point2D& a = points[tri.i0];
        const Point2D& b = points[tri.i1];
        const Point2D& c = points[tri.i2];

        // Circumcenter calculation
        double d = 2.0 * (a.x * (b.y - c.y) + b.x * (c.y - a.y) + c.x * (a.y - b.y));
        if (std::abs(d) < DEFAULT_TOLERANCE) {
            circumcenters.push_back(lineMidpoint(a, b));
            continue;
        }

        double aSq = a.x * a.x + a.y * a.y;
        double bSq = b.x * b.x + b.y * b.y;
        double cSq = c.x * c.x + c.y * c.y;

        double ux = (aSq * (b.y - c.y) + bSq * (c.y - a.y) + cSq * (a.y - b.y)) / d;
        double uy = (aSq * (c.x - b.x) + bSq * (a.x - c.x) + cSq * (b.x - a.x)) / d;

        circumcenters.push_back(Point2D(ux, uy));
    }

    // For each point, find its Voronoi cell
    for (size_t pi = 0; pi < points.size(); ++pi) {
        std::vector<Point2D> cellPoints;

        // Find all triangles containing this point
        for (size_t ti = 0; ti < delaunay.size(); ++ti) {
            const Triangle& tri = delaunay[ti];
            if (tri.i0 == static_cast<int>(pi) || tri.i1 == static_cast<int>(pi) || tri.i2 == static_cast<int>(pi)) {
                cellPoints.push_back(circumcenters[ti]);
            }
        }

        if (cellPoints.size() >= 3) {
            // Sort points around centroid
            Point2D centroid(0, 0);
            for (const Point2D& p : cellPoints) centroid = centroid + p;
            centroid = centroid / static_cast<double>(cellPoints.size());

            std::sort(cellPoints.begin(), cellPoints.end(), [&](const Point2D& a, const Point2D& b) {
                return std::atan2(a.y - centroid.y, a.x - centroid.x) <
                       std::atan2(b.y - centroid.y, b.x - centroid.x);
            });

            cells[pi] = cellPoints;
        }
    }

    return cells;
}

// =====================================================================
//  Point Set Analysis
// =====================================================================

std::pair<int, int> findDiameter(const std::vector<Point2D>& points)
{
    if (points.size() < 2) return {0, 0};

    // Use rotating calipers on convex hull
    std::vector<Point2D> hull = convexHull(points);
    if (hull.size() < 2) return {0, 0};

    double maxDist = 0;
    int best1 = 0, best2 = 0;

    // For small hulls, just check all pairs
    for (size_t i = 0; i < hull.size(); ++i) {
        for (size_t j = i + 1; j < hull.size(); ++j) {
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
    for (size_t i = 0; i < points.size(); ++i) {
        if (points[i] == hull[best1]) idx1 = i;
        if (points[i] == hull[best2]) idx2 = i;
    }

    return {idx1, idx2};
}

std::pair<int, int> findClosestPair(const std::vector<Point2D>& points)
{
    if (points.size() < 2) return {0, 0};

    // Simple O(n^2) for now - could use divide-and-conquer
    double minDist = std::numeric_limits<double>::max();
    int best1 = 0, best2 = 1;

    for (size_t i = 0; i < points.size(); ++i) {
        for (size_t j = i + 1; j < points.size(); ++j) {
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

double hausdorffDistance(const std::vector<Point2D>& set1, const std::vector<Point2D>& set2)
{
    if (set1.empty() || set2.empty()) return 0;

    auto maxMinDist = [](const std::vector<Point2D>& from, const std::vector<Point2D>& to) {
        double maxDist = 0;
        for (const Point2D& p : from) {
            double minDist = std::numeric_limits<double>::max();
            for (const Point2D& q : to) {
                minDist = std::min(minDist, lengthSquared(q - p));
            }
            maxDist = std::max(maxDist, minDist);
        }
        return std::sqrt(maxDist);
    };

    return std::max(maxMinDist(set1, set2), maxMinDist(set2, set1));
}

// =====================================================================
//  Curve Analysis
// =====================================================================

std::vector<double> polylineCurvature(const std::vector<Point2D>& points)
{
    std::vector<double> curvature(points.size(), 0.0);
    if (points.size() < 3) return curvature;

    for (size_t i = 1; i < points.size() - 1; ++i) {
        const Point2D& prev = points[i - 1];
        const Point2D& curr = points[i];
        const Point2D& next = points[i + 1];

        // Menger curvature: 4 * triangle_area / (|a| * |b| * |c|)
        double a = length(curr - prev);
        double b = length(next - curr);
        double c = length(next - prev);

        double area = 0.5 * std::abs(cross(curr - prev, next - prev));

        if (a > DEFAULT_TOLERANCE && b > DEFAULT_TOLERANCE && c > DEFAULT_TOLERANCE) {
            curvature[i] = 4.0 * area / (a * b * c);
        }
    }

    return curvature;
}

std::vector<int> findCorners(const std::vector<Point2D>& points, double angleThreshold)
{
    std::vector<int> corners;
    if (points.size() < 3) return corners;

    for (size_t i = 1; i < points.size() - 1; ++i) {
        Point2D v1 = points[i] - points[i - 1];
        Point2D v2 = points[i + 1] - points[i];

        double angle = std::abs(signedAngleBetween(v1, v2));
        if (angle > angleThreshold) {
            corners.push_back(i);
        }
    }

    return corners;
}

std::vector<Point2D> smoothPolyline(const std::vector<Point2D>& points, int iterations)
{
    if (points.size() < 3 || iterations <= 0) return points;

    std::vector<Point2D> result = points;

    for (int iter = 0; iter < iterations; ++iter) {
        std::vector<Point2D> newPoints;
        newPoints.reserve(result.size() * 2);

        for (size_t i = 0; i < result.size() - 1; ++i) {
            Point2D q = result[i] * 0.75 + result[i + 1] * 0.25;
            Point2D r = result[i] * 0.25 + result[i + 1] * 0.75;
            newPoints.push_back(q);
            newPoints.push_back(r);
        }

        result = newPoints;
    }

    return result;
}

// =====================================================================
//  Path Operations
// =====================================================================

double pathLength(const std::vector<Point2D>& points)
{
    double len = 0;
    for (size_t i = 1; i < points.size(); ++i) {
        len += length(points[i] - points[i - 1]);
    }
    return len;
}

std::vector<Point2D> resamplePath(const std::vector<Point2D>& points, double spacing)
{
    if (points.size() < 2 || spacing <= 0) return points;

    double totalLen = pathLength(points);
    int numPoints = std::max(2, static_cast<int>(totalLen / spacing) + 1);

    return resamplePathByCount(points, numPoints);
}

std::vector<Point2D> resamplePathByCount(const std::vector<Point2D>& points, int numPoints)
{
    if (points.size() < 2 || numPoints < 2) return points;

    double totalLen = pathLength(points);
    double spacing = totalLen / (numPoints - 1);

    std::vector<Point2D> result;
    result.reserve(numPoints);
    result.push_back(points.front());

    double accumulated = 0;
    double nextTarget = spacing;
    int segIndex = 0;
    double segStart = 0;

    while (static_cast<int>(result.size()) < numPoints - 1 && segIndex < static_cast<int>(points.size()) - 1) {
        double segLen = length(points[segIndex + 1] - points[segIndex]);

        while (segStart + segLen >= nextTarget && static_cast<int>(result.size()) < numPoints - 1) {
            double t = (nextTarget - segStart) / segLen;
            result.push_back(lerp(points[segIndex], points[segIndex + 1], t));
            nextTarget += spacing;
        }

        segStart += segLen;
        ++segIndex;
    }

    result.push_back(points.back());
    return result;
}

Point2D pointAtArcLength(const std::vector<Point2D>& points, double arcLength)
{
    if (points.empty()) return Point2D();
    if (points.size() == 1 || arcLength <= 0) return points.front();

    double accumulated = 0;
    for (size_t i = 0; i < points.size() - 1; ++i) {
        double segLen = length(points[i + 1] - points[i]);
        if (accumulated + segLen >= arcLength) {
            double t = (arcLength - accumulated) / segLen;
            return lerp(points[i], points[i + 1], t);
        }
        accumulated += segLen;
    }

    return points.back();
}

Point2D tangentAtArcLength(const std::vector<Point2D>& points, double arcLength)
{
    if (points.size() < 2) return Point2D(1, 0);

    double accumulated = 0;
    for (size_t i = 0; i < points.size() - 1; ++i) {
        double segLen = length(points[i + 1] - points[i]);
        if (accumulated + segLen >= arcLength || i == points.size() - 2) {
            return normalize(points[i + 1] - points[i]);
        }
        accumulated += segLen;
    }

    return normalize(points.back() - points[points.size() - 2]);
}

}  // namespace geometry
}  // namespace hobbycad
