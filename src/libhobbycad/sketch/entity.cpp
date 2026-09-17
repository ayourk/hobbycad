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
#include <hobbycad/units.h>

#include <cmath>
#include <algorithm>
#include <array>

#include <hobbycad/math_constants.h>

namespace hobbycad {
namespace sketch {

namespace {
inline Point3 at3(const Point2D& p) { return Point3{p.x, p.y, 0.0}; }

/// Clamp an ellipse parameter (radians, the ellipse's own frame) into the
/// entity's stored arc range. A full ellipse returns the angle untouched.
/// Outside the range, the nearer of the two ends wins, so a hit test against
/// an elliptical arc reports the arc and not the ellipse it was cut from.
inline double clampEllipseParam(const Entity& e, double angleRad)
{
    if (isFullEllipse(e)) return angleRad;
    const double s0 = degreesToRadians(e.ellipseStart);
    const double sw = degreesToRadians(e.ellipseSweep);
    // Offset of the query from the start, wrapped into [0, 2pi).
    double rel = std::fmod(angleRad - s0, 2.0 * M_PI);
    if (rel < 0.0) rel += 2.0 * M_PI;
    const double span = std::abs(sw);
    if (rel <= span) return s0 + (sw < 0.0 ? -rel : rel);
    // Past the end: pick whichever end the query sits nearer to.
    const double pastEnd = rel - span;
    const double beforeStart = 2.0 * M_PI - rel;
    return (pastEnd <= beforeStart) ? s0 + sw : s0;
}
}  // namespace

using namespace geometry;

namespace {

// Cubic Bezier control quads describing a spline's curve, for sampling and
// picking. When bezier is true the control points ARE the Bezier control
// polygon (3k+1 points, one cubic per 3-point step); otherwise the polyline
// is treated as Catmull-Rom (tension 0.5) and converted to Bezier segments.
std::vector<std::array<Point2D, 4>>
splineBezierSegments(const std::vector<Point3>& points, bool bezier)
{
    std::vector<std::array<Point2D, 4>> segs;
    const int n = static_cast<int>(points.size());
    if (n < 2) return segs;
    if (bezier && n >= 4 && (n - 1) % 3 == 0) {
        for (int i = 0; i + 3 < n; i += 3)
            segs.push_back({{points[i], points[i + 1], points[i + 2], points[i + 3]}});
        return segs;
    }
    for (int i = 0; i < n - 1; ++i) {
        Point2D cp0 = (i == 0) ? points[i] : points[i - 1];
        Point2D cp1 = points[i];
        Point2D cp2 = points[i + 1];
        Point2D cp3 = (i == n - 2) ? points[i + 1] : points[i + 2];
        segs.push_back({{cp1, cp1 + (cp2 - cp0) / 6.0, cp2 - (cp3 - cp1) / 6.0, cp2}});
    }
    return segs;
}

} // namespace

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
        // Exact axis-aligned bound of a rotated ellipse: the extreme reached in
        // x (and y) over the parametric ellipse is hypot of the two axes'
        // projections onto that world axis.
        const double th = degreesToRadians(ellipseRotation);
        const double c = std::cos(th), s = std::sin(th);
        const double hx = std::sqrt(majorRadius * majorRadius * c * c +
                                    minorRadius * minorRadius * s * s);
        const double hy = std::sqrt(majorRadius * majorRadius * s * s +
                                    minorRadius * minorRadius * c * c);
        bbox.include(Point2D(points[0].x - hx, points[0].y - hy));
        bbox.include(Point2D(points[0].x + hx, points[0].y + hy));
    } else if (type == EntityType::Spline && points.size() >= 2) {
        // Sample the actual curve so the bound covers Catmull-Rom overshoot
        // between control points (the control points alone under-bound it).
        // Bezier stays a valid superset: its control-point hull already
        // contains the curve; this only ever grows the box, never clips.
        for (const auto& b : splineBezierSegments(points, splineBezier)) {
            const int N = 24;
            for (int s = 0; s <= N; ++s) {
                double t = static_cast<double>(s) / N, u = 1.0 - t;
                bbox.include(u*u*u*b[0] + 3.0*u*u*t*b[1] + 3.0*u*t*t*b[2] + t*t*t*b[3]);
            }
        }
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
            Arc arc = toArc();
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

void setAnchorHandle(BezierAnchor& a, BezierHandleSide side, double angleDeg, double length,
                     bool canIn, bool canOut)
{
    const double rad = degreesToRadians(angleDeg);
    const double dx = length * std::cos(rad), dy = length * std::sin(rad);
    if ((side == BezierHandleSide::Out || side == BezierHandleSide::Tangent) && canOut) {
        a.hasOut = true;
        a.outHandle = {a.pos.x + dx, a.pos.y + dy};
    }
    if (side == BezierHandleSide::In && canIn) {
        a.hasIn = true;
        a.inHandle = {a.pos.x + dx, a.pos.y + dy};
    }
    if (side == BezierHandleSide::Tangent && canIn) {
        a.hasIn = true;
        a.inHandle = {a.pos.x - dx, a.pos.y - dy};
    }
}

bool anchorHandlePolar(const BezierAnchor& a, BezierHandleSide side, double& angleDeg, double& length)
{
    const Point2D* h = nullptr;
    if (side == BezierHandleSide::In && a.hasIn) h = &a.inHandle;
    else if (side == BezierHandleSide::Out && a.hasOut) h = &a.outHandle;
    if (!h) return false;
    length = geometry::lineLength(a.pos, *h);
    if (length < geometry::kZeroEps) return false;            // coincident: a corner
    angleDeg = atan2Degrees(h->y - a.pos.y, h->x - a.pos.x);
    return true;
}

AnchorContinuity anchorContinuity(const BezierAnchor& a)
{
    if (!a.hasIn || !a.hasOut) return AnchorContinuity::Corner;
    const Point2D o(a.outHandle.x - a.pos.x, a.outHandle.y - a.pos.y);
    const Point2D i(a.pos.x - a.inHandle.x, a.pos.y - a.inHandle.y);
    const double lo = geometry::length(o), li = geometry::length(i);
    if (lo < geometry::kZeroEps || li < geometry::kZeroEps) return AnchorContinuity::Corner;
    constexpr double kCollinearCos = 1.0 - geometry::kDegenerateLen;   // cos of ~0.08 degrees
    const double cosine = geometry::dot(o, i) / (lo * li);
    if (cosine <= kCollinearCos) return AnchorContinuity::Corner;
    return (std::fabs(lo - li) < geometry::kDegenerateLen) ? AnchorContinuity::Smooth
                                                           : AnchorContinuity::Asymmetric;
}

std::vector<Point2D> Entity::connectionPoints() const
{
    if (type == EntityType::Point) {
        if (points.empty()) return {};
        return {Point2D(points[0])};
    }
    if (type == EntityType::Rectangle) {
        Point2D c[4];
        if (!rectangleCorners(*this, c)) return {};
        return {c[0], c[1], c[2], c[3]};
    }
    return endpoints();
}

geometry::Arc Entity::toArc() const
{
    Arc arc;
    if (!points.empty()) {
        arc.center = points[0];
    }
    arc.radius     = radius;
    arc.startAngle = startAngle;
    arc.sweepAngle = sweepAngle;
    return arc;
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
            Arc arc = toArc();
            return closestPointOnArc(point, arc);
        }
        break;

    case EntityType::Rectangle: {
        // Stored corners (4-point) or the two diagonal corners expanded: the
        // closest point on any of the four edges.
        Point2D corners[4];
        if (rectangleCorners(*this, corners)) {
            Point2D closest = corners[0];
            double minDist = std::hypot(point.x - corners[0].x, point.y - corners[0].y);
            for (int i = 0; i < 4; ++i) {
                int j = (i + 1) % 4;
                Point2D cp = closestPointOnLine(point, corners[i], corners[j]);
                double d = std::hypot(point.x - cp.x, point.y - cp.y);
                if (d < minDist) { minDist = d; closest = cp; }
            }
            return closest;
        }
        break;
    }

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
            const double th = degreesToRadians(ellipseRotation);
            const double ct = std::cos(th), st = std::sin(th);
            const double dx = point.x - points[0].x;
            const double dy = point.y - points[0].y;
            // Into local frame, take the same-angle point, then rotate back.
            const double lxq =  dx * ct + dy * st;
            const double lyq = -dx * st + dy * ct;
            // Clamped, so an elliptical arc answers with a point ON the arc.
            const double angle = clampEllipseParam(*this, std::atan2(lyq, lxq));
            const double lx = a * std::cos(angle);
            const double ly = b * std::sin(angle);
            return Point2D(points[0].x + lx * ct - ly * st,
                           points[0].y + lx * st + ly * ct);
        }
        break;

    case EntityType::Spline:
        if (points.size() >= 2) {
            if (points.size() == 2) {
                return closestPointOnLine(point, points[0], points[1]);
            }
            // Sample the spline's cubic segments (Bezier control polygon when
            // splineBezier, else Catmull-Rom) and find the closest sampled point.
            Point2D bestPoint = points[0];
            double minDist = std::hypot(point.x - points[0].x, point.y - points[0].y);
            const int samplesPerSegment = 20;
            for (const auto& b : splineBezierSegments(points, splineBezier)) {
                Point2D prev = b[0];
                for (int s = 1; s <= samplesPerSegment; ++s) {
                    double st = static_cast<double>(s) / samplesPerSegment;
                    double u = 1.0 - st;
                    Point2D cur = u*u*u*b[0] + 3.0*u*u*st*b[1] + 3.0*u*st*st*b[2] + st*st*st*b[3];
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
            sweep = geometry::wrapSweepRad(sweep);
            if (arcFlipped) {
                sweep = (sweep > 0) ? sweep - 2 * M_PI : sweep + 2 * M_PI;
            }

            double relAngle = pointAngle - startAngle;
            relAngle = geometry::wrapSweepRad(relAngle);

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
            Arc arc = toArc();
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
            // Rotate the query into the ellipse's local (unrotated) frame.
            const double th = degreesToRadians(ellipseRotation);
            const double ct = std::cos(th), st = std::sin(th);
            const double dx = point.x - points[0].x;
            const double dy = point.y - points[0].y;
            const double lx =  dx * ct + dy * st;
            const double ly = -dx * st + dy * ct;
            if (!isFullEllipse(*this)) {
                // Elliptical arc: measure to the nearest point ON the arc,
                // because the closed-form below answers for the whole
                // ellipse and would report a hit off the end of the arc.
                const double pr = clampEllipseParam(*this, std::atan2(ly, lx));
                return lineLength(Point2D(lx, ly),
                                  Point2D(a * std::cos(pr), b * std::sin(pr)));
            }
            // Normalized ellipse equation: (lx/a)^2 + (ly/b)^2 = 1 on the outline
            double normalized = (lx * lx) / (a * a) + (ly * ly) / (b * b);
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
            // Sample the spline's cubic segments (Bezier control polygon when
            // splineBezier, else Catmull-Rom) and find the minimum distance.
            const int samplesPerSegment = 20;
            double minDist = std::numeric_limits<double>::max();
            for (const auto& b : splineBezierSegments(points, splineBezier)) {
                Point2D prev = b[0];
                for (int s = 1; s <= samplesPerSegment; ++s) {
                    double st = static_cast<double>(s) / samplesPerSegment;
                    double u = 1.0 - st;
                    Point2D cur = u*u*u*b[0] + 3.0*u*u*st*b[1] + 3.0*u*st*st*b[2] + st*st*st*b[3];
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
            sweep = geometry::wrapSweepRad(sweep);
            if (arcFlipped) {
                sweep = (sweep > 0) ? sweep - 2 * M_PI : sweep + 2 * M_PI;
            }

            double relAngle = pointAngle - startAngle;
            relAngle = geometry::wrapSweepRad(relAngle);

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
    for (Point3& p : points) {
        const Point2D xy = t.apply(p.xy());   // 2D transform is in-plane
        p.x = xy.x; p.y = xy.y;               // z (off-plane) preserved
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

    // Store the endpoints as well, so a parametric arc has the SAME point
    // layout as one built from three points: [center, start, end].
    //
    // The parametric fields stay authoritative: rendering draws from
    // center/radius/startAngle/sweepAngle, and the solver re-derives all
    // three of those from the solved points on readback. The endpoints are a
    // derived second view, needed because everything that addresses an arc by
    // its ends works through points[1] and points[2]: solver registration
    // (without them an arc degrades to a plain circle and its endpoints
    // cannot be constrained at all), endpoint snapping, and trim/extend.
    const geometry::Arc a = e.toArc();
    e.points.push_back(a.startPoint());
    e.points.push_back(a.endPoint());
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
    e.points.assign(controlPoints.begin(), controlPoints.end());
    return e;
}

Entity createBezierSpline(int id, const std::vector<Point2D>& controlPoints)
{
    Entity e = createSpline(id, controlPoints);
    e.splineBezier = true;
    return e;
}

Entity createRationalBezierSpline(int id, const std::vector<Point2D>& controlPoints,
                                  const std::vector<double>& weights)
{
    Entity e = createBezierSpline(id, controlPoints);
    e.splineRational = true;
    e.weights = weights;
    // A missing/short weight list defaults to 1.0 (non-rational for that point).
    e.weights.resize(controlPoints.size(), 1.0);
    return e;
}

std::vector<Point2D> bezierControlPolygon(const std::vector<BezierAnchor>& anchors)
{
    std::vector<Point2D> poly;
    const size_t n = anchors.size();
    if (n < 2) return poly;                 // need at least one segment
    poly.reserve(3 * (n - 1) + 1);
    poly.push_back(anchors[0].pos);
    for (size_t k = 0; k + 1 < n; ++k) {
        const BezierAnchor& a = anchors[k];
        const BezierAnchor& b = anchors[k + 1];
        // segment k: [P_k, out_k, in_{k+1}, P_{k+1}]; a missing handle collapses
        // onto its anchor (a corner on that side).
        poly.push_back(a.hasOut ? a.outHandle : a.pos);
        poly.push_back(b.hasIn  ? b.inHandle  : b.pos);
        poly.push_back(b.pos);
    }
    return poly;
}

std::vector<double> bezierControlPolygonWeights(const std::vector<BezierAnchor>& anchors)
{
    std::vector<double> w;
    const int n = static_cast<int>(anchors.size());
    if (n < 2) return w;
    const int np = 3 * (n - 1) + 1;
    w.reserve(np);
    // control point j belongs to anchor (j+1)/3 (integer div): P_k and its two
    // handles (out_k, in_k) all carry anchor k's weight.
    for (int j = 0; j < np; ++j)
        w.push_back(anchors[static_cast<std::size_t>((j + 1) / 3)].weight);
    return w;
}

std::vector<BezierAnchor> bezierAnchorsFromControlPolygon(const std::vector<Point2D>& poly)
{
    std::vector<BezierAnchor> anchors;
    const int n = static_cast<int>(poly.size());
    if (n < 4 || (n - 1) % 3 != 0) return anchors;   // not a Bezier control polygon
    const int N = (n - 1) / 3;                        // segment count
    for (int k = 0; k <= N; ++k) {
        BezierAnchor a;
        a.pos = poly[3 * k];
        if (k > 0) { a.hasIn  = true; a.inHandle  = poly[3 * k - 1]; }
        if (k < N) { a.hasOut = true; a.outHandle = poly[3 * k + 1]; }
        anchors.push_back(a);
    }
    return anchors;
}

bool isRegularPolygon(const Entity& entity)
{
    return entity.type == EntityType::Polygon && entity.radius >= 0.001;
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

double maxArcSlotSweepDegrees(double pathRadius, double halfWidth)
{
    // A slot needs a centerline to sweep along and caps that fit inside it.
    if (pathRadius <= 0.0 || !slotWidthIsPositive(2.0 * halfWidth)) return 0.0;

    // Half the width may EQUAL the radius, and that case is real rather
    // than degenerate: the inner edge collapses onto the arc center, the
    // two caps have radius R and centers a full diameter apart, and they
    // meet at the center itself. asin(1) is a half turn, so the limit
    // comes out at 180 degrees. Only a width past that has no shape.
    if (halfWidth > pathRadius) return 0.0;

    // The caps touch when the STRAIGHT-LINE distance between their centers
    // is two cap radii. That distance is a chord of the centerline circle,
    // so the angle it subtends is 2*asin(halfWidth / pathRadius).
    //
    // Measuring the gap as an arc length instead (2*halfWidth/pathRadius,
    // the obvious reading) is close but leaves the caps slightly
    // OVERLAPPING, because an arc is longer than the chord it spans. At
    // r=30, width=8 it is off by 0.024mm. Small, but the whole point of
    // this value is that the ends meet exactly.
    const double limit = 360.0 - arcSlotCuspSeparationDegrees(pathRadius, halfWidth);
    return limit > 0.0 ? limit : 0.0;
}

bool updateSlotFromPath(Entity& slot, const Entity& path)
{
    if (slot.type != EntityType::Slot) return false;

    if (path.type == EntityType::Line) {
        if (path.points.size() < 2) return false;
        // A circle swept along a segment puts its center at each end.
        slot.points = { path.points[0], path.points[1] };
        slot.arcFlipped = false;
        return true;
    }

    if (path.type == EntityType::Arc) {
        if (path.points.empty() || !geometry::isPositiveLength(path.radius)) return false;
        const geometry::Arc arc = path.toArc();
        slot.points = { arc.center, arc.startPoint(), arc.endPoint() };
        // Two end centers cannot say which way round the arc runs; the
        // arc's own sweep can, so it has to be carried across or the slot
        // silently takes the short way.
        slot.arcFlipped = std::abs(path.sweepAngle) > 180.0;
        return true;
    }

    return false;
}

double arcSlotGapDegrees(double pathRadius, double halfWidth,
                         double capRadiiApart)
{
    if (pathRadius <= 0.0 || !slotWidthIsPositive(2.0 * halfWidth)) return 0.0;
    if (halfWidth > pathRadius) return 0.0;

    // Separation is a CHORD of the centerline circle: 2*R*sin(gap/2).
    const double sep = capRadiiApart * halfWidth;
    const double ratio = sep / (2.0 * pathRadius);
    if (ratio >= 1.0) return 180.0;          // ends diametrically opposite
    return radiansToDegrees(2.0 * std::asin(ratio));
}

double arcSlotCuspSeparationDegrees(double pathRadius, double halfWidth)
{
    return arcSlotGapDegrees(pathRadius, halfWidth, 2.0);
}

double arcSlotFloorSeparationDegrees(double pathRadius, double halfWidth)
{
    return arcSlotGapDegrees(pathRadius, halfWidth, 1.0);
}

SlotArcCenter enforceSlotArcSeparation(
    const Point2D& start, const Point2D& end,
    const Point2D& center, double slotHalfWidth)
{
    SlotArcCenter r;
    r.center = center;
    r.radius = std::hypot(center.x - start.x, center.y - start.y);

    const double slotR = (slotHalfWidth < 0.1) ? 5.0 : slotHalfWidth;   // UI default
    const double minSep = (r.radius > 0.001)
        ? degreesToRadians(arcSlotFloorSeparationDegrees(r.radius, slotR))
        : 0.1;

    double sA = std::atan2(start.y - center.y, start.x - center.x);
    double eA = std::atan2(end.y - center.y, end.x - center.x);
    double diff = eA - sA;
    diff = geometry::wrapSweepRad(diff);

    const double chordLen = std::hypot(end.x - start.x, end.y - start.y);
    if (std::abs(diff) < minSep && r.radius > 0.001 && chordLen > 0.001) {
        const double halfChord = chordLen / 2.0;
        const double required = halfChord / std::sin(minSep / 2.0);
        if (required > r.radius) {
            const double midx = (start.x + end.x) / 2.0;
            const double midy = (start.y + end.y) / 2.0;
            const double tcx = center.x - midx, tcy = center.y - midy;
            const double toLen = std::hypot(tcx, tcy);
            if (toLen > 0.001) {
                const double newDist =
                    std::sqrt(required * required - halfChord * halfChord);
                r.center = Point2D(midx + tcx * (newDist / toLen),
                                   midy + tcy * (newDist / toLen));
                r.radius = required;
            }
        }
    }
    return r;
}

void resyncArcEndpoints(Entity& arc)
{
    if (arc.type != EntityType::Arc || arc.points.size() < 3) return;
    const double cx = arc.points[0].x, cy = arc.points[0].y;
    const double r = arc.radius;
    const double s = degreesToRadians(arc.startAngle);
    const double e = degreesToRadians(arc.startAngle + arc.sweepAngle);
    arc.points[1] = Point3{cx + r * std::cos(s), cy + r * std::sin(s), 0.0};
    arc.points[2] = Point3{cx + r * std::cos(e), cy + r * std::sin(e), 0.0};
}

void setArcFromAngles(Entity& arc, const Point2D& center, double radius,
                      double startAngleDeg, double sweepAngleDeg)
{
    arc.type = EntityType::Arc;
    arc.points.assign(3, Point3{center.x, center.y, 0.0});
    arc.radius = radius;
    arc.startAngle = startAngleDeg;
    arc.sweepAngle = sweepAngleDeg;
    resyncArcEndpoints(arc);
}

void resyncTextHandle(Entity& text)
{
    if (text.type != EntityType::Text || text.points.size() < 2) return;
    const double dist = std::max(text.fontSize * 2.0,
                                 text.fontSize * static_cast<double>(text.text.length()) * 0.6);
    const Point2D anchor(text.points[0]);
    text.points[1] = at3(geometry::polarPoint(anchor, dist, degreesToRadians(text.textRotation)));
}

int nearestArcEndIndex(const Entity& arc, const Point2D& p)
{
    if (arc.points.size() < 3) return 1;
    return (geometry::lineLength(arc.points[1], p) <= geometry::lineLength(arc.points[2], p)) ? 1 : 2;
}

void rescaleCircleToRadius(Entity& circle, double radius)
{
    if (circle.type != EntityType::Circle || circle.points.empty()) return;
    circle.radius = radius;
    const double cx = circle.points[0].x, cy = circle.points[0].y;
    for (size_t i = 1; i < circle.points.size(); ++i) {
        const double dx = circle.points[i].x - cx, dy = circle.points[i].y - cy;
        const double len = std::sqrt(dx * dx + dy * dy);
        if (len > geometry::kDegenerateLen)
            circle.points[i] = Point3{cx + dx * (radius / len),
                                      cy + dy * (radius / len), 0.0};
    }
}

double absoluteMaxArcSlotSweepDegrees(double pathRadius, double halfWidth)
{
    const double gap = arcSlotGapDegrees(pathRadius, halfWidth, 1.0);
    if (gap <= 0.0) return 0.0;
    const double limit = 360.0 - gap;
    return limit > 0.0 ? limit : 0.0;
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

Entity createEllipse(int id, const Point2D& center, double majorRadius, double minorRadius,
                     double rotationDeg)
{
    Entity e;
    e.id = id;
    e.type = EntityType::Ellipse;
    e.points.push_back(center);
    e.majorRadius = majorRadius;
    e.minorRadius = minorRadius;
    e.ellipseRotation = rotationDeg;
    syncEllipseAxisPoints(e);
    return e;
}

void syncEllipseAxisPoints(Entity& e)
{
    if (e.type != EntityType::Ellipse || e.points.empty()) return;
    // +major axis, then +minor axis a quarter turn from it.
    const Point2D major = ellipsePointAtParamDeg(e, 0.0);
    const Point2D minor = ellipsePointAtParamDeg(e, 90.0);
    if (e.points.size() < 2) e.points.push_back(at3(major));
    else                     e.points[1] = at3(major);
    if (e.points.size() < 3) e.points.push_back(at3(minor));
    else                     e.points[2] = at3(minor);
}

bool ensureEllipseAxisPoints(Entity& e)
{
    if (e.type != EntityType::Ellipse || e.points.empty()) return false;
    if (e.points.size() >= 3) return false;
    syncEllipseAxisPoints(e);
    return true;
}

namespace {

/// The second radius of an ellipse centered at `c` whose first axis runs
/// along the unit vector `u` with radius `a`, chosen so the curve passes
/// through `p`: in the axis frame (x' along u, y' across it) that radius is
/// |y'| / sqrt(1 - (x'/a)^2). Beyond the first axis's extent (|x'| >= a) no
/// such ellipse exists and the perpendicular distance |y'| stands in.
/// `side` receives y' itself, whose sign says which side of the axis `p`
/// lies on.
double radiusThrough(const Point2D& c, const Point2D& u, double a, const Point2D& p,
                     double& side)
{
    const Point2D r = p - c;
    const double along = dot(r, u);
    side = cross(u, r);
    const double k = 1.0 - (along * along) / (a * a);
    return k > kZeroEps ? std::abs(side) / std::sqrt(k) : std::abs(side);
}

/// Store an arc range from p1 to p2 on `e`: the shorter way by default, the
/// long way on request (the circular arc tools' Shift), always as a positive
/// counter-clockwise sweep in (0, 360]. Measured only counter-clockwise from
/// p1, a cursor moving clockwise drew the long complement and the arc looked
/// subtracted from the ellipse (Aaron, 2026-09-16), so the short clockwise
/// arc is stored from p2 instead. The same point twice is a full turn.
void applyArcRange(Entity& e, const Point2D& p1, const Point2D& p2, bool longWay)
{
    const double s = ellipseParamDeg(e, p1);
    double sweep = std::fmod(ellipseParamDeg(e, p2) - s, 360.0);
    if (sweep <= 0.0) sweep += 360.0;
    e.ellipseStart = s;
    if (sweep < 360.0) {
        const bool isLong = sweep > 180.0;
        if (isLong != longWay) {
            e.ellipseStart = ellipseParamDeg(e, p2);
            sweep = 360.0 - sweep;
        }
    }
    e.ellipseSweep = sweep;
}

}  // namespace

bool ellipseFromClicks(bool threePoint, const std::vector<Point2D>& clicks,
                       Entity& out, bool longWay)
{
    if (clicks.size() < 2) return false;

    // Center: given by the first click, or the midpoint of the two rim
    // clicks when the major axis was drawn end to end.
    const Point2D c = threePoint ? lineMidpoint(clicks[0], clicks[1]) : clicks[0];
    const Point2D axis = clicks[1] - c;
    const double major = length(axis);
    if (!isPositiveLength(major)) return false;

    out.type = EntityType::Ellipse;
    out.majorRadius = major;
    out.ellipseRotation = radiansToDegrees(std::atan2(axis.y, axis.x));

    double minor = major * 0.5;          // interrupted placement default
    if (clicks.size() >= 3) {
        // The third click is a point the ellipse PASSES THROUGH, as in
        // Fusion and Onshape: in the ellipse's own frame (x' along the major
        // axis, y' across it) the minor radius is |y'| / sqrt(1 - (x'/a)^2),
        // so the curve stays under the cursor wherever it goes. It used to
        // be the perpendicular distance alone, which put the minor-axis end
        // at the foot of the cursor rather than the curve under it, and read
        // as "the minor dot does not follow the cursor" (Aaron, 2026-09-16).
        // radiusThrough() also covers a click beyond the major extent.
        double side = 0.0;
        const double d = radiusThrough(c, axis / major, major, clicks[2], side);
        if (isPositiveLength(d)) minor = d;
    }
    out.minorRadius = minor;

    out.points.clear();
    out.points.push_back(at3(c));
    syncEllipseAxisPoints(out);          // writes +major and +minor ends

    // An arc range, when the placement went on to choose one: click four is
    // the start and click five the end (applyArcRange). Four clicks means
    // the end is still being placed, so the sweep stays a full turn.
    out.ellipseStart = 0.0;
    out.ellipseSweep = 360.0;
    if (clicks.size() >= 5) {
        applyArcRange(out, clicks[3], clicks[4], longWay);
    } else if (clicks.size() == 4) {
        out.ellipseStart = ellipseParamDeg(out, clicks[3]);
    }
    return true;
}

namespace {

/// The first `n` clicks (fewer when there are fewer).
std::vector<Point2D> firstClicks(const std::vector<Point2D>& clicks, std::size_t n)
{
    return std::vector<Point2D>(clicks.begin(), clicks.begin() + std::min(n, clicks.size()));
}

/// Write the scalar fields and canonical points of an ellipse from its
/// center, semi-axes and rotation, keeping the longer axis the major one.
void setEllipse(Entity& out, const Point2D& c, double a, double b, double rotDeg)
{
    if (b > a) { std::swap(a, b); rotDeg += 90.0; }
    out.type = EntityType::Ellipse;
    out.majorRadius = a;
    out.minorRadius = b;
    out.ellipseRotation = normalizeAngle360(rotDeg);
    out.points.clear();
    out.points.push_back(Point3(c.x, c.y, 0.0));
    syncEllipseAxisPoints(out);
    out.ellipseStart = 0.0;
    out.ellipseSweep = 360.0;
}

}  // namespace

int ellipsePlacementClicks(EllipsePlacement mode)
{
    switch (mode) {
    case EllipsePlacement::Arc:       return 5;
    case EllipsePlacement::Endpoints: return 4;
    default:                          return 3;
    }
}

bool ellipseFromPlacement(EllipsePlacement mode, const std::vector<Point2D>& clicks,
                          bool longWay, Entity& out)
{
    switch (mode) {
    case EllipsePlacement::CenterAxes:
    case EllipsePlacement::ThreePoint:
        return ellipseFromClicks(mode == EllipsePlacement::ThreePoint, firstClicks(clicks, 3),
                                 out, longWay);
    case EllipsePlacement::Arc:
        return ellipseFromClicks(false, clicks, out, longWay);

    case EllipsePlacement::SpanRise: {
        // The span is one axis end to end (3-Point rule: center = midpoint),
        // the apex a point the curve passes through, and the arc is the half
        // on the apex's side. Without the apex yet the half is the counter-
        // clockwise one from the first end.
        if (!ellipseFromClicks(true, firstClicks(clicks, 3), out)) return false;
        const double s1 = ellipseParamDeg(out, clicks[0]);
        const double s2 = ellipseParamDeg(out, clicks[1]);
        out.ellipseStart = s1;
        out.ellipseSweep = 180.0;
        if (clicks.size() >= 3
            && normalizeAngle360(ellipseParamDeg(out, clicks[2]) - s1) > 180.0) {
            out.ellipseStart = s2;   // the apex is on the other half
        }
        return true;
    }

    case EllipsePlacement::Corner: {
        // Corner = center; the second click ends one axis (it sets the axis
        // direction and the first radius); the third click is a point the
        // curve PASSES THROUGH: the second radius is solved from it (the
        // pass-through rule, as for a whole ellipse), and the arc stays the
        // full quarter from the first leg point to the second axis end, on
        // the click's side. Passing through a point does not mean ending on
        // it (Aaron, 2026-09-16). It used to keep only the click's
        // perpendicular distance, which left the curve "just shy" of a point
        // clicked slightly off the perpendicular. Beyond the first axis's
        // extent no such ellipse exists; the perpendicular distance stands in.
        if (clicks.size() < 2) return false;
        const Point2D c = clicks[0];
        const Point2D axis = clicks[1] - c;
        const double a = length(axis);
        if (!isPositiveLength(a)) return false;
        double b = a * 0.5;
        bool minorSide = true;   // the +90 side until a third click says otherwise
        if (clicks.size() >= 3) {
            double side = 0.0;
            const double d = radiusThrough(c, axis / a, a, clicks[2], side);
            if (isPositiveLength(d)) b = d;
            minorSide = side >= 0.0;
        }
        setEllipse(out, c, a, b, radiansToDegrees(std::atan2(axis.y, axis.x)));
        // The quarter from the first leg point (read back: setEllipse may have
        // swapped the axes when b > a) toward the click's side.
        const double s1 = ellipseParamDeg(out, clicks[1]);
        out.ellipseStart = minorSide ? s1 : normalizeAngle360(s1 + 270.0);
        out.ellipseSweep = 90.0;
        return true;
    }

    case EllipsePlacement::Endpoints: {
        // Two points ON the curve, the center, then a point giving the axis
        // direction. In that frame each point gives  x'^2/a^2 + y'^2/b^2 = 1,
        // two equations linear in 1/a^2 and 1/b^2; no solution (or a
        // negative one) means no ellipse with that center and axis passes
        // through both points, and the placement is refused rather than
        // guessed. Two points mirror-symmetric about an axis (equal x'^2 and
        // y'^2) leave one radius free; refused too.
        if (clicks.size() < 3) return false;
        const Point2D p1 = clicks[0], p2 = clicks[1], c = clicks[2];
        double th = 0.0;
        if (clicks.size() >= 4 && isPositiveLength(lineLength(c, clicks[3]))) {
            th = std::atan2(clicks[3].y - c.y, clicks[3].x - c.x);
        }
        const double ux = std::cos(th), uy = std::sin(th);
        auto frame = [&](const Point2D& p, double& x, double& y) {
            const double rx = p.x - c.x, ry = p.y - c.y;
            x = rx * ux + ry * uy;
            y = -rx * uy + ry * ux;
        };
        double x1, y1, x2, y2;
        frame(p1, x1, y1);
        frame(p2, x2, y2);
        const double X1 = x1 * x1, Y1 = y1 * y1, X2 = x2 * x2, Y2 = y2 * y2;
        const double det = X1 * Y2 - X2 * Y1;
        // Relative test: the squares scale with the sketch's units. The
        // floor only keeps an all-zero set from comparing against zero.
        const double scale = std::max({X1, Y1, X2, Y2, std::numeric_limits<double>::min()});
        if (std::abs(det) <= kZeroEps * scale * scale) return false;
        const double alpha = (Y2 - Y1) / det;   // 1/a^2
        const double beta  = (X1 - X2) / det;   // 1/b^2
        if (alpha <= 0.0 || beta <= 0.0) return false;
        const double a = 1.0 / std::sqrt(alpha), b = 1.0 / std::sqrt(beta);
        if (!isPositiveLength(a) || !isPositiveLength(b)) return false;
        setEllipse(out, c, a, b, radiansToDegrees(th));
        applyArcRange(out, p1, p2, longWay);
        return true;
    }
    }
    return false;
}

Point2D conicShoulder(const Point2D& start, const Point2D& end, const Point2D& apex, double rho)
{
    return lerp(lineMidpoint(start, end), apex, rho);
}

double conicRhoFromPoint(const Point2D& start, const Point2D& end, const Point2D& apex,
                         const Point2D& p)
{
    const Point2D m = lineMidpoint(start, end);
    const Point2D toApex = apex - m;
    if (!isPositiveLength(length(toApex))) return 0.5;
    const double t = dot(p - m, toApex) / lengthSquared(toApex);
    return std::min(0.98, std::max(0.02, t));
}

bool conicFromRho(int id, const Point2D& start, const Point2D& end, const Point2D& apex,
                  double rho, Entity& out)
{
    const Point2D chordVec = end - start;
    const double chord = length(chordVec);
    if (!isPositiveLength(chord)) return false;
    // The apex must be off the chord's line, or there is no curve to bend.
    if (!isPositiveLength(std::abs(cross(chordVec, apex - start)) / chord)) return false;
    rho = std::min(0.999, std::max(0.001, rho));

    // Rational quadratic with weights (1, w, 1), w = rho / (1 - rho), then
    // exact degree elevation in homogeneous coordinates:
    //   Q1h = (R0 + 2 R1) / 3,  Q2h = (2 R1 + R2) / 3
    // so the inner cubic weights are (1 + 2w) / 3 and the inner points are
    // (P0 + 2w A) / (1 + 2w) and (2w A + P2) / (1 + 2w).
    const double w = rho / (1.0 - rho);
    const double d = 1.0 + 2.0 * w;
    const Point2D q1{(start.x + 2.0 * w * apex.x) / d, (start.y + 2.0 * w * apex.y) / d};
    const Point2D q2{(2.0 * w * apex.x + end.x) / d, (2.0 * w * apex.y + end.y) / d};
    const double W = d / 3.0;
    out = createRationalBezierSpline(id, {start, q1, q2, end}, {1.0, W, W, 1.0});
    out.conicRho = rho;
    return true;
}

bool conicApex(const Entity& e, Point2D& apex)
{
    if (e.type != EntityType::Spline || !e.splineBezier || e.points.size() != 4) return false;
    const Point2D p0(e.points[0]), p1(e.points[1]), p2(e.points[2]), p3(e.points[3]);
    if (!isPositiveLength(lineLength(p0, p1)) || !isPositiveLength(lineLength(p3, p2))) {
        return false;
    }
    const LineLineIntersection x = infiniteLineIntersection(p0, p1, p3, p2);
    if (!x.intersects || x.parallel) return false;
    apex = x.point;
    return true;
}

const char* conicKindName(double rho)
{
    if (!(rho > 0.0) || !(rho < 1.0)) return "";
    if (std::abs(rho - 0.5) < kZeroEps) return "parabolic";
    return rho < 0.5 ? "elliptical" : "hyperbolic";
}

bool setConicRho(Entity& e, double rho)
{
    if (!(e.conicRho > 0.0)) return false;
    Point2D apex;
    if (!conicApex(e, apex)) return false;
    Entity fresh;
    if (!conicFromRho(e.id, Point2D(e.points.front()), Point2D(e.points.back()), apex, rho,
                      fresh)) {
        return false;
    }
    e.points = fresh.points;
    e.weights = fresh.weights;
    e.splineRational = true;
    e.conicRho = fresh.conicRho;
    return true;
}

double ellipseParamDeg(const Entity& e, const Point2D& p)
{
    if (e.points.empty() || !isPositiveLength(e.majorRadius)
        || !isPositiveLength(e.minorRadius)) {
        return 0.0;
    }
    const double th = degreesToRadians(e.ellipseRotation);
    const double ct = std::cos(th), st = std::sin(th);
    const double dx = p.x - e.points[0].x, dy = p.y - e.points[0].y;
    // Into the ellipse's own frame, then undo the axis scaling so the
    // angle is the curve's parameter rather than a polar angle.
    const double lx =  dx * ct + dy * st;
    const double ly = -dx * st + dy * ct;
    return normalizeAngle360(
        radiansToDegrees(std::atan2(ly / e.minorRadius, lx / e.majorRadius)));
}

Point2D ellipsePointAtParamDeg(const Entity& e, double paramDeg)
{
    if (e.points.empty()) return Point2D();
    const double t = degreesToRadians(paramDeg);
    const Point2D local(e.majorRadius * std::cos(t), e.minorRadius * std::sin(t));
    return Point2D(e.points[0]) + rotatePoint(local, e.ellipseRotation);
}

bool isFullEllipse(const Entity& e)
{
    // A sweep goes through text and the solver, so a whole turn can come
    // back a hair short of 360.
    constexpr double FULL_TURN_TOLERANCE_DEG = 1e-3;
    return std::abs(e.ellipseSweep) >= 360.0 - FULL_TURN_TOLERANCE_DEG;
}

bool syncEllipseFields(Entity& e)
{
    if (e.type != EntityType::Ellipse || e.points.size() < 3) return false;
    const Point2D c{e.points[0].x, e.points[0].y};

    // Whichever axis came out longer IS the major one. Onshape and BricsCAD
    // both say so outright, and OCCT refuses an ellipse built the other way.
    // Swapping the two points turns the frame a quarter turn, so the stored
    // arc range moves with it or the drawn arc would jump.
    if (lineLength(c, Point2D(e.points[2])) > lineLength(c, Point2D(e.points[1]))) {
        std::swap(e.points[1], e.points[2]);
        e.ellipseStart -= 90.0;
    }

    const Point2D p1(e.points[1]);
    e.majorRadius = lineLength(c, p1);
    e.minorRadius = lineLength(c, Point2D(e.points[2]));
    e.ellipseRotation = normalizeAngle360(radiansToDegrees(std::atan2(p1.y - c.y, p1.x - c.x)));
    return true;
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
    case EntityType::Parallelogram:
        if (entity.points.size() >= 4) {
            // Four stored corners (a rotated rectangle or a parallelogram):
            // check any vertex or any edge crossing
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
            const geometry::Arc arc = entity.toArc();
            const Point2D startPt = arc.startPoint();
            const Point2D endPt   = arc.endPoint();
            if (rect.contains(startPt) || rect.contains(endPt)) return true;
            // Also check midpoint of arc
            const Point2D midPt = arc.pointAt(0.5);
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
    case EntityType::Parallelogram:
        if (entity.points.size() >= 4) {
            // Four stored corners (a rotated rectangle or a parallelogram):
            // all of them must be enclosed
            return rect.contains(entity.points[0]) && rect.contains(entity.points[1]) &&
                   rect.contains(entity.points[2]) && rect.contains(entity.points[3]);
        } else if (entity.points.size() >= 2) {
            return rect.contains(entity.points[0]) && rect.contains(entity.points[1]);
        }
        break;

    case EntityType::Arc:
        if (!entity.points.empty()) {
            const geometry::Arc arc = entity.toArc();
            const Point2D startPt = arc.startPoint();
            const Point2D endPt   = arc.endPoint();
            if (!rect.contains(startPt) || !rect.contains(endPt)) return false;
            // Also check midpoint
            const Point2D midPt = arc.pointAt(0.5);
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
    double angle = radiansToDegrees(std::atan2(delta.y, delta.x));

    // Normalize to 0-360
    if (angle < 0) {
        angle += 360.0;
    }
    return angle;
}

Entity makeSlotCenterline(const Entity& slot, double pathRadius, double startAngleDeg, double sweepDeg)
{
    Entity path;
    path.isConstruction = true;
    if (slot.points.size() >= 3) {
        path.type = EntityType::Arc;
        path.points = { slot.points[0], slot.points[1], slot.points[2] };
        const Point2D& c = slot.points[0];
        const Point2D& sp0 = slot.points[1];
        const Point2D& ep0 = slot.points[2];
        path.radius = pathRadius > 0.0 ? pathRadius : std::hypot(sp0.x - c.x, sp0.y - c.y);
        if (std::isnan(startAngleDeg) || std::isnan(sweepDeg)) {
            // From the geometry plus arcFlipped, NOT slot.startAngle/sweepAngle: arc
            // slots carry the >180 direction in arcFlipped and leave sweepAngle
            // unset, so copying it would give the centerline a short sweep and the
            // slot would not follow past 180 degrees.
            const double a0 = std::atan2(sp0.y - c.y, sp0.x - c.x);
            const double a1 = std::atan2(ep0.y - c.y, ep0.x - c.x);
            double sweep = a1 - a0;
            sweep = geometry::wrapSweepRad(sweep);
            if (slot.arcFlipped) sweep = (sweep > 0) ? sweep - 2.0 * M_PI : sweep + 2.0 * M_PI;
            path.startAngle = radiansToDegrees(a0);
            path.sweepAngle = radiansToDegrees(sweep);
        } else {
            path.startAngle = startAngleDeg;
            path.sweepAngle = sweepDeg;
        }
    } else if (slot.points.size() >= 2) {
        path.type = EntityType::Line;
        path.points = { slot.points[0], slot.points[1] };
    }
    return path;
}

bool rectangleFromThreePoints(const Point2D& p1, const Point2D& p2, const Point2D& p3, Point2D out[4])
{
    const Point2D edge = p2 - p1;
    if (geometry::length(edge) <= 0.01) return false;
    const Point2D perpDir = geometry::perpendicular(geometry::normalize(edge));
    const double perpDist = geometry::dot(p3 - p1, perpDir);
    out[0] = p1;
    out[1] = p2;
    out[2] = p2 + perpDir * perpDist;
    out[3] = p1 + perpDir * perpDist;
    return true;
}

bool rectangleCorners(const Entity& rect, Point2D out[4])
{
    if (rect.type != EntityType::Rectangle || rect.points.size() < 2) return false;
    if (rect.points.size() >= 4) {
        for (int i = 0; i < 4; ++i) out[i] = rect.points[i];
    } else {
        const Point2D p0 = rect.points[0], p1 = rect.points[1];
        out[0] = p0;
        out[1] = Point2D(p1.x, p0.y);
        out[2] = p1;
        out[3] = Point2D(p0.x, p1.y);
    }
    return true;
}

bool quadCorners(const Entity& entity, Point2D out[4])
{
    if (entity.type == EntityType::Rectangle) return rectangleCorners(entity, out);
    if (entity.type != EntityType::Parallelogram || entity.points.size() < 4) return false;
    for (int i = 0; i < 4; ++i) out[i] = entity.points[i];
    return true;
}

const Entity* findEntityById(const std::vector<Entity>& entities, int id)
{
    for (const Entity& e : entities) {
        if (e.id == id) return &e;
    }
    return nullptr;
}

Entity* findEntityById(std::vector<Entity>& entities, int id)
{
    for (Entity& e : entities) {
        if (e.id == id) return &e;
    }
    return nullptr;
}

int nextFreeEntityId(const std::vector<Entity>& entities)
{
    int next = 1;
    for (const Entity& e : entities) {
        if (e.id >= next) next = e.id + 1;
    }
    return next;
}

}  // namespace sketch
}  // namespace hobbycad
