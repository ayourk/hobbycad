// =====================================================================
//  src/libhobbycad/sketch/operations.cpp — Sketch operations implementation
// =====================================================================
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include <hobbycad/sketch/operations.h>
#include <hobbycad/sketch/slotpath.h>
#include <hobbycad/units.h>
#include <hobbycad/sketch/constraint.h>
#include <hobbycad/sketch/solver.h>
#include <hobbycad/geometry/intersections.h>
#include <hobbycad/geometry/utils.h>

#include <queue>
#include <unordered_set>
#include <algorithm>
#include <cmath>

#include <hobbycad/math_constants.h>

namespace hobbycad {
namespace sketch {

using namespace geometry;

// =====================================================================
//  Intersection Detection
// =====================================================================

std::vector<Intersection> findIntersection(const Entity& e1, const Entity& e2)
{
    std::vector<Intersection> results;

    // Line-Line
    if (e1.type == EntityType::Line && e2.type == EntityType::Line) {
        if (e1.points.size() >= 2 && e2.points.size() >= 2) {
            LineLineIntersection lli = lineLineIntersection(
                e1.points[0], e1.points[1],
                e2.points[0], e2.points[1]);

            if (lli.intersects && lli.withinSegment1 && lli.withinSegment2) {
                Intersection inter;
                inter.entityId1 = e1.id;
                inter.entityId2 = e2.id;
                inter.point = lli.point;
                inter.param1 = lli.t1;
                inter.param2 = lli.t2;
                results.push_back(inter);
            }
        }
    }
    // Line-Circle
    else if (e1.type == EntityType::Line &&
             (e2.type == EntityType::Circle || e2.type == EntityType::Arc)) {
        if (e1.points.size() >= 2 && !e2.points.empty()) {
            if (e2.type == EntityType::Circle) {
                LineCircleIntersection lci = lineCircleIntersection(
                    e1.points[0], e1.points[1],
                    e2.points[0], e2.radius);

                if (lci.count >= 1 && lci.point1InSegment) {
                    Intersection inter;
                    inter.entityId1 = e1.id;
                    inter.entityId2 = e2.id;
                    inter.point = lci.point1;
                    inter.param1 = lci.t1;
                    results.push_back(inter);
                }
                if (lci.count >= 2 && lci.point2InSegment) {
                    Intersection inter;
                    inter.entityId1 = e1.id;
                    inter.entityId2 = e2.id;
                    inter.point = lci.point2;
                    inter.param1 = lci.t2;
                    results.push_back(inter);
                }
            } else {
                // Arc
                Arc arc;
                arc.center = e2.points[0];
                arc.radius = e2.radius;
                arc.startAngle = e2.startAngle;
                arc.sweepAngle = e2.sweepAngle;

                LineArcIntersection lai = lineArcIntersection(
                    e1.points[0], e1.points[1], arc);

                if (lai.count >= 1 && lai.point1InSegment && lai.point1OnArc) {
                    Intersection inter;
                    inter.entityId1 = e1.id;
                    inter.entityId2 = e2.id;
                    inter.point = lai.point1;
                    results.push_back(inter);
                }
                if (lai.count >= 2 && lai.point2InSegment && lai.point2OnArc) {
                    Intersection inter;
                    inter.entityId1 = e1.id;
                    inter.entityId2 = e2.id;
                    inter.point = lai.point2;
                    results.push_back(inter);
                }
            }
        }
    }
    // Circle-Line (swap arguments)
    else if ((e1.type == EntityType::Circle || e1.type == EntityType::Arc) &&
             e2.type == EntityType::Line) {
        auto swapped = findIntersection(e2, e1);
        for (auto& inter : swapped) {
            std::swap(inter.entityId1, inter.entityId2);
            std::swap(inter.param1, inter.param2);
        }
        for (auto& inter : swapped) {
            results.push_back(inter);
        }
    }
    // Circle-Circle
    else if ((e1.type == EntityType::Circle || e1.type == EntityType::Arc) &&
             (e2.type == EntityType::Circle || e2.type == EntityType::Arc)) {
        if (!e1.points.empty() && !e2.points.empty()) {
            if (e1.type == EntityType::Circle && e2.type == EntityType::Circle) {
                CircleCircleIntersection cci = circleCircleIntersection(
                    e1.points[0], e1.radius,
                    e2.points[0], e2.radius);

                if (cci.count >= 1) {
                    Intersection inter;
                    inter.entityId1 = e1.id;
                    inter.entityId2 = e2.id;
                    inter.point = cci.point1;
                    results.push_back(inter);
                }
                if (cci.count >= 2) {
                    Intersection inter;
                    inter.entityId1 = e1.id;
                    inter.entityId2 = e2.id;
                    inter.point = cci.point2;
                    results.push_back(inter);
                }
            } else {
                // At least one arc - use arc-arc intersection
                Arc arc1, arc2;
                arc1.center = e1.points[0];
                arc1.radius = e1.radius;
                arc1.startAngle = (e1.type == EntityType::Arc) ? e1.startAngle : 0;
                arc1.sweepAngle = (e1.type == EntityType::Arc) ? e1.sweepAngle : 360;

                arc2.center = e2.points[0];
                arc2.radius = e2.radius;
                arc2.startAngle = (e2.type == EntityType::Arc) ? e2.startAngle : 0;
                arc2.sweepAngle = (e2.type == EntityType::Arc) ? e2.sweepAngle : 360;

                CircleCircleIntersection cci = arcArcIntersection(arc1, arc2);

                if (cci.count >= 1) {
                    Intersection inter;
                    inter.entityId1 = e1.id;
                    inter.entityId2 = e2.id;
                    inter.point = cci.point1;
                    results.push_back(inter);
                }
                if (cci.count >= 2) {
                    Intersection inter;
                    inter.entityId1 = e1.id;
                    inter.entityId2 = e2.id;
                    inter.point = cci.point2;
                    results.push_back(inter);
                }
            }
        }
    }

    return results;
}

std::vector<Intersection> findIntersections(
    const Entity& entity,
    const std::vector<Entity>& others)
{
    std::vector<Intersection> results;

    for (const Entity& other : others) {
        if (other.id == entity.id) continue;
        auto inters = findIntersection(entity, other);
        for (auto& inter : inters) {
            results.push_back(inter);
        }
    }

    return results;
}

std::vector<Intersection> findAllIntersections(const std::vector<Entity>& entities)
{
    std::vector<Intersection> results;

    for (int i = 0; i < static_cast<int>(entities.size()); ++i) {
        for (int j = i + 1; j < static_cast<int>(entities.size()); ++j) {
            auto inters = findIntersection(entities[i], entities[j]);
            for (auto& inter : inters) {
                results.push_back(inter);
            }
        }
    }

    return results;
}

// =====================================================================
//  Offset Operation
// =====================================================================

OffsetResult offsetEntity(
    const Entity& entity,
    double distance,
    const Point2D& clickPos,
    int newId)
{
    OffsetResult result;

    if (entity.type == EntityType::Line && entity.points.size() >= 2) {
        // Calculate perpendicular direction
        Point2D dir = entity.points[1] - entity.points[0];
        Point2D perp = normalize(perpendicular(dir));

        // Determine which side based on click position
        Point2D mid = lineMidpoint(entity.points[0], entity.points[1]);
        Point2D toClick = clickPos - mid;
        int side = (dot(toClick, perp) > 0) ? 1 : -1;

        Point2D offset = perp * (distance * side);

        result.entity = createLine(newId,
            entity.points[0] + offset,
            entity.points[1] + offset);
        result.entity.isConstruction = entity.isConstruction;
        result.side = side;
        result.success = true;
    }
    else if (entity.type == EntityType::Circle && !entity.points.empty()) {
        // Determine if offset is inward or outward
        double distToCenter = std::hypot(clickPos.x - entity.points[0].x, clickPos.y - entity.points[0].y);
        int side = (distToCenter > entity.radius) ? 1 : -1;
        double newRadius = entity.radius + side * distance;

        if (newRadius < 0.1) {
            result.errorMessage = "Offset would create invalid radius";
            return result;
        }

        result.entity = createCircle(newId, entity.points[0], newRadius);
        result.entity.isConstruction = entity.isConstruction;
        result.side = side;
        result.success = true;
    }
    else if (entity.type == EntityType::Arc && !entity.points.empty()) {
        double distToCenter = std::hypot(clickPos.x - entity.points[0].x, clickPos.y - entity.points[0].y);
        int side = (distToCenter > entity.radius) ? 1 : -1;
        double newRadius = entity.radius + side * distance;

        if (newRadius < 0.1) {
            result.errorMessage = "Offset would create invalid radius";
            return result;
        }

        result.entity = createArc(newId, entity.points[0], newRadius,
            entity.startAngle, entity.sweepAngle);
        result.entity.isConstruction = entity.isConstruction;
        result.side = side;
        result.success = true;
    }
    else {
        result.errorMessage = "Offset not supported for this entity type";
    }

    return result;
}

OffsetResult offsetEntity(
    const Entity& entity,
    double distance,
    int side,
    int newId)
{
    // Create a click position on the appropriate side
    Point2D clickPos;

    if (entity.type == EntityType::Line && entity.points.size() >= 2) {
        Point2D mid = lineMidpoint(entity.points[0], entity.points[1]);
        Point2D dir = entity.points[1] - entity.points[0];
        Point2D perp = normalize(perpendicular(dir));
        clickPos = mid + perp * (side > 0 ? 1.0 : -1.0);
    }
    else if ((entity.type == EntityType::Circle || entity.type == EntityType::Arc) &&
             !entity.points.empty()) {
        clickPos = entity.points[0] + Point2D(entity.radius * side * 1.1, 0);
    }

    return offsetEntity(entity, distance, clickPos, newId);
}

bool updateOffsetFromParent(Entity& child, const Entity& parent)
{
    const double d = child.offsetDistance;
    const int side = child.offsetSide;

    if (parent.type == EntityType::Line && parent.points.size() >= 2) {
        const Point2D dir = parent.points[1] - parent.points[0];
        const Point2D perp = normalize(perpendicular(dir));
        const Point2D off = perp * (d * side);
        child.type = EntityType::Line;
        child.points = { parent.points[0] + off, parent.points[1] + off };
        return true;
    }
    if (parent.type == EntityType::Circle && !parent.points.empty()) {
        const double nr = parent.radius + side * d;
        if (nr < 0.1) return false;
        const Entity c = createCircle(child.id, parent.points[0], nr);
        child.type = EntityType::Circle;
        child.points = c.points;
        child.radius = c.radius;
        return true;
    }
    if (parent.type == EntityType::Arc && !parent.points.empty()) {
        const double nr = parent.radius + side * d;
        if (nr < 0.1) return false;
        const Entity a = createArc(child.id, parent.points[0], nr,
                                   parent.startAngle, parent.sweepAngle);
        child.type = EntityType::Arc;
        child.points = a.points;
        child.radius = a.radius;
        child.startAngle = a.startAngle;
        child.sweepAngle = a.sweepAngle;
        return true;
    }
    return false;  // parent type not offsettable
}

namespace {

// Project a source-plane conic (center-local, semi-axes a>=b at angle phiDeg in
// the source plane) onto the target plane. The two rotated semi-axes become
// conjugate semi-diameters of the projected ellipse; a 2x2 SVD of [a1 a2] gives
// its major/minor (singular values) and rotation (left-singular-vector angle).
void projectConic(const Point3& centerLocal, double a, double b, double phiDeg,
                  const PlaneBasis& src, const PlaneBasis& tgt,
                  Point2D& outCenter, double& outMajor, double& outMinor, double& outRotDeg)
{
    auto dot3 = [](const Vec3& A, const Vec3& B) {
        return double(A.x)*B.x + double(A.y)*B.y + double(A.z)*B.z;
    };
    const double ph = degreesToRadians(phiDeg), cp = std::cos(ph), sp = std::sin(ph);
    auto comb = [&](double s1, double s2) {
        return Vec3(float(s1*src.uAxis.x + s2*src.vAxis.x),
                    float(s1*src.uAxis.y + s2*src.vAxis.y),
                    float(s1*src.uAxis.z + s2*src.vAxis.z));
    };
    const Vec3 ax1 = comb(a*cp,  a*sp);    // major axis, world
    const Vec3 ax2 = comb(-b*sp, b*cp);    // minor axis, world
    const double a1x = dot3(ax1, tgt.uAxis), a1y = dot3(ax1, tgt.vAxis);
    const double a2x = dot3(ax2, tgt.uAxis), a2y = dot3(ax2, tgt.vAxis);
    const double P = a1x*a1x + a2x*a2x, R = a1y*a1y + a2y*a2y, Q = a1x*a1y + a2x*a2y;
    const double mid = (P + R) / 2.0;
    const double disc = std::sqrt(((P - R)/2.0)*((P - R)/2.0) + Q*Q);
    outMajor = std::sqrt(mid + disc > 0 ? mid + disc : 0.0);
    outMinor = std::sqrt(mid - disc > 0 ? mid - disc : 0.0);
    outRotDeg = radiansToDegrees(0.5 * std::atan2(2.0*Q, P - R));
    const Point3 ct = centerLocal.world(src).project(tgt);
    outCenter = {ct.x, ct.y};
}

// Parameter (degrees) of a target-2D point on an ellipse (center c, rotation
// rotDeg, semi-axes M,m): its angle in the ellipse's local frame.
double paramOnEllipse(const Point2D& q, const Point2D& c, double rotDeg, double M, double m)
{
    const double th = degreesToRadians(rotDeg), ct = std::cos(th), st = std::sin(th);
    const double dx = q.x - c.x, dy = q.y - c.y;
    const double lx =  dx*ct + dy*st;   // into local frame
    const double ly = -dx*st + dy*ct;
    return radiansToDegrees(std::atan2(ly / (m > geometry::kExactEps ? m : geometry::kExactEps),
                      lx / (M > geometry::kExactEps ? M : geometry::kExactEps)));
}

// Emit the projected conic into `child`: a Circle when it comes out round and
// full (the "circle feel"), otherwise an Ellipse (partial if sweep < 360).
void emitConic(Entity& child, const Point2D& c, double M, double m,
               double rotDeg, double startDeg, double sweepDeg)
{
    child.points.clear();
    child.points.push_back({c.x, c.y, 0.0});
    const bool full = std::fabs(std::fabs(sweepDeg) - 360.0) < 1e-4;
    if (full && std::fabs(M - m) <= 1e-4 * (M > 1.0 ? M : 1.0)) {
        child.type = EntityType::Circle;
        child.radius = (M + m) / 2.0;
        child.majorRadius = child.minorRadius = 0.0;
        child.ellipseRotation = child.ellipseStart = 0.0;
        child.ellipseSweep = 360.0;
    } else {
        child.type = EntityType::Ellipse;
        child.majorRadius = M;
        child.minorRadius = m;
        child.ellipseRotation = rotDeg;
        child.ellipseStart = startDeg;
        child.ellipseSweep = sweepDeg;
        child.radius = 0.0;
        // A projected ellipse gets its axis points like any other, so it is
        // the same kind of object everywhere downstream. The projection is
        // still solver-skipped; this only keeps the representation uniform.
        syncEllipseAxisPoints(child);
    }
}

}  // namespace

bool updateProjectionFromSource(Entity& child, const Entity& source,
                                const PlaneBasis& sourcePlane,
                                const PlaneBasis& targetPlane)
{
    // The conic family (circle / arc / ellipse) maps into the conic family under
    // an angled projection: a circle foreshortens to an ellipse, an arc to an
    // elliptical arc, and an ellipse can come back round (a circle) when the
    // fold undoes its foreshortening. All share projectConic() + emitConic().
    if (source.type == EntityType::Circle && !source.points.empty() && source.radius > 0) {
        Point2D c; double M, m, rot;
        projectConic(source.points[0], source.radius, source.radius, 0.0,
                     sourcePlane, targetPlane, c, M, m, rot);
        emitConic(child, c, M, m, rot, 0.0, 360.0);
        return true;
    }
    if (source.type == EntityType::Ellipse && !source.points.empty() && source.majorRadius > 0) {
        Point2D c; double M, m, rot;
        projectConic(source.points[0], source.majorRadius, source.minorRadius,
                     source.ellipseRotation, sourcePlane, targetPlane, c, M, m, rot);
        if (isFullEllipse(source)) {
            emitConic(child, c, M, m, rot, 0.0, 360.0);
        } else {
            // Partial ellipse: map its start/mid/end onto the projected ellipse.
            const double ph = degreesToRadians(source.ellipseRotation);
            const double cp = std::cos(ph), sp = std::sin(ph);
            const Point3 ctr = source.points[0];
            auto srcPt = [&](double tDeg) {
                const double t = degreesToRadians(tDeg);
                const double ex = source.majorRadius * std::cos(t);
                const double ey = source.minorRadius * std::sin(t);
                // rotate by phi into source-plane local coords, offset by center
                return Point3(ctr.x + ex*cp - ey*sp, ctr.y + ex*sp + ey*cp, ctr.z);
            };
            const double t0 = source.ellipseStart;
            const double t2 = source.ellipseStart + source.ellipseSweep;
            const double t1 = source.ellipseStart + source.ellipseSweep / 2.0;
            const Point3 q0 = srcPt(t0).world(sourcePlane).project(targetPlane);
            const Point3 q1 = srcPt(t1).world(sourcePlane).project(targetPlane);
            const Point3 q2 = srcPt(t2).world(sourcePlane).project(targetPlane);
            const double p0 = paramOnEllipse({q0.x,q0.y}, c, rot, M, m);
            const double p1 = paramOnEllipse({q1.x,q1.y}, c, rot, M, m);
            const double p2 = paramOnEllipse({q2.x,q2.y}, c, rot, M, m);
            const double ccwSweep = std::fmod(p2 - p0 + 720.0, 360.0);
            const double ccwMid   = std::fmod(p1 - p0 + 720.0, 360.0);
            double start = p0, sweep = ccwSweep;
            if (ccwMid > ccwSweep + geometry::kAngleEpsDeg) { start = p2; sweep = 360.0 - ccwSweep; }
            emitConic(child, c, M, m, rot, start, sweep);
        }
        return true;
    }
    if (source.type == EntityType::Arc && source.points.size() >= 3 && source.radius > 0) {
        Point2D c; double M, m, rot;
        projectConic(source.points[0], source.radius, source.radius, 0.0,
                     sourcePlane, targetPlane, c, M, m, rot);
        // Project the arc's start / mid / end points, find their params on the
        // projected ellipse, and pick the sweep that passes through the mid.
        const Point3 ctr = source.points[0];
        const double midA = degreesToRadians(source.startAngle + source.sweepAngle / 2.0);
        const Point3 midLocal(ctr.x + source.radius * std::cos(midA),
                              ctr.y + source.radius * std::sin(midA), ctr.z);
        const Point3 q0 = source.points[1].world(sourcePlane).project(targetPlane);
        const Point3 q1 = midLocal.world(sourcePlane).project(targetPlane);
        const Point3 q2 = source.points[2].world(sourcePlane).project(targetPlane);
        const double p0 = paramOnEllipse({q0.x,q0.y}, c, rot, M, m);
        const double p1 = paramOnEllipse({q1.x,q1.y}, c, rot, M, m);
        const double p2 = paramOnEllipse({q2.x,q2.y}, c, rot, M, m);
        const double ccwSweep = std::fmod(p2 - p0 + 720.0, 360.0);
        const double ccwMid   = std::fmod(p1 - p0 + 720.0, 360.0);
        double start = p0, sweep = ccwSweep;
        if (ccwMid > ccwSweep + geometry::kAngleEpsDeg) { start = p2; sweep = 360.0 - ccwSweep; }
        emitConic(child, c, M, m, rot, start, sweep);
        return true;
    }

    // Point-list entities project point-by-point.
    switch (source.type) {
    case EntityType::Line:
    case EntityType::Point:
    case EntityType::Spline:
    case EntityType::Polygon:
        break;
    default:
        return false;
    }

    child.type = source.type;
    child.points.clear();
    child.points.reserve(source.points.size());
    for (const Point3& s : source.points) {
        // Lift the source point to world through its plane, then project
        // orthographically onto the target plane (drop the normal component,
        // so the result lies ON the target plane: w = 0). Handles a 3D source
        // point too, since world() uses its off-plane w as well.
        const Point3 tl = s.world(sourcePlane).project(targetPlane);
        child.points.push_back({tl.x, tl.y, 0.0});
    }
    return true;
}

bool makeProjectionChild(Entity& child, const Entity& source,
                         int sourceSketchId, int newId,
                         const PlaneBasis& sourcePlane,
                         const PlaneBasis& targetPlane)
{
    child = source;                 // inherit type and point structure
    child.id = newId;
    child.isConstruction = false;   // projected geometry is reference, not
    child.isCenterline = false;     // construction/centerline of its own
    child.projectionSourceId = source.id;
    child.projectionSourceSketchId = sourceSketchId;
    // Recompute the child's points from the source through both planes.
    return updateProjectionFromSource(child, source, sourcePlane, targetPlane);
}

std::vector<Point3> tessellateSpline(const std::vector<Point3>& ctrl,
                                     int segmentsPerSpan, bool bezier, bool closed)
{
    const int n = static_cast<int>(ctrl.size());
    if (segmentsPerSpan < 1) return ctrl;
    if (bezier) {
        // Piecewise cubic Bezier: control points are the Bezier control polygon,
        // segment s = ctrl[3s..3s+3] (endpoints shared). De Casteljau per segment.
        // Closed: n = 3N points, N segments, the last wraps to ctrl[0].
        if (n < 4) return ctrl;
        const int nseg = closed ? (n / 3) : ((n - 1) / 3);
        std::vector<Point3> out;
        out.reserve(static_cast<std::size_t>(nseg * segmentsPerSpan + 1));
        for (int s = 0; s < nseg; ++s) {
            const Point3& b0 = ctrl[3*s]; const Point3& b1 = ctrl[3*s+1];
            const Point3& b2 = ctrl[3*s+2]; const Point3& b3 = ctrl[(3*s+3) % n];
            for (int j = 0; j < segmentsPerSpan; ++j) {
                const double t = static_cast<double>(j) / segmentsPerSpan, u = 1.0 - t;
                const double c0=u*u*u, c1=3*u*u*t, c2=3*u*t*t, c3=t*t*t;
                out.push_back({ c0*b0.x + c1*b1.x + c2*b2.x + c3*b3.x,
                                c0*b0.y + c1*b1.y + c2*b2.y + c3*b3.y,
                                c0*b0.z + c1*b1.z + c2*b2.z + c3*b3.z });
            }
        }
        out.push_back(closed ? ctrl[0] : ctrl[3 * ((n - 1) / 3)]);   // close/final endpoint
        return out;
    }
    if (n < 3) return ctrl;   // line/point: nothing to smooth
    std::vector<Point3> out;
    out.reserve(static_cast<std::size_t>((n - 1) * segmentsPerSpan + 1));
    auto at = [&](int i) -> const Point3& { return ctrl[std::clamp(i, 0, n - 1)]; };
    for (int i = 0; i < n - 1; ++i) {
        const Point3& p0 = at(i - 1);
        const Point3& p1 = at(i);
        const Point3& p2 = at(i + 1);
        const Point3& p3 = at(i + 2);
        for (int seg = 0; seg < segmentsPerSpan; ++seg) {
            const double t  = static_cast<double>(seg) / segmentsPerSpan;
            const double t2 = t * t, t3 = t2 * t;
            // Catmull-Rom (tension 0.5): interpolates p1..p2 using p0,p3 as tangents.
            auto comp = [&](double a, double b, double c, double d) {
                return 0.5 * ((2.0 * b) + (-a + c) * t
                              + (2.0 * a - 5.0 * b + 4.0 * c - d) * t2
                              + (-a + 3.0 * b - 3.0 * c + d) * t3);
            };
            out.push_back({ comp(p0.x, p1.x, p2.x, p3.x),
                            comp(p0.y, p1.y, p2.y, p3.y),
                            comp(p0.z, p1.z, p2.z, p3.z) });
        }
    }
    out.push_back(ctrl.back());   // include the final control point exactly
    return out;
}

std::vector<Point3> tessellateRationalSpline(const std::vector<Point3>& ctrl,
                                             const std::vector<double>& weights,
                                             int segmentsPerSpan)
{
    const int n = static_cast<int>(ctrl.size());
    if (segmentsPerSpan < 1) return ctrl;
    if (n < 4 || static_cast<int>(weights.size()) != n)
        return tessellateSpline(ctrl, segmentsPerSpan, /*bezier=*/true);  // fall back
    std::vector<Point3> out;
    out.reserve(static_cast<std::size_t>((n / 3) * segmentsPerSpan + 1));
    for (int seg = 0; 3 * seg + 3 < n; ++seg) {
        const int b = 3 * seg;
        for (int j = 0; j < segmentsPerSpan; ++j) {
            const double t = static_cast<double>(j) / segmentsPerSpan, u = 1.0 - t;
            const double B[4] = { u*u*u, 3*u*u*t, 3*u*t*t, t*t*t };
            double den = 0.0, x = 0.0, y = 0.0, z = 0.0;
            for (int k = 0; k < 4; ++k) {
                const double wb = weights[b + k] * B[k];
                den += wb;
                x += wb * ctrl[b + k].x;
                y += wb * ctrl[b + k].y;
                z += wb * ctrl[b + k].z;
            }
            if (den > geometry::kExactEps) out.push_back({ x / den, y / den, z / den });
        }
    }
    out.push_back(ctrl[3 * ((n - 1) / 3)]);  // exact final endpoint
    return out;
}

// =====================================================================
//  Fillet Operation
// =====================================================================

std::optional<Point2D> findCornerPoint(
    const Entity& line1,
    const Entity& line2,
    double tolerance)
{
    if (line1.type != EntityType::Line || line2.type != EntityType::Line) {
        return std::nullopt;
    }
    if (line1.points.size() < 2 || line2.points.size() < 2) {
        return std::nullopt;
    }

    // Check all endpoint combinations
    for (int i = 0; i < 2; ++i) {
        for (int j = 0; j < 2; ++j) {
            if (pointsCoincident(line1.points[i], line2.points[j], tolerance)) {
                return (line1.points[i] + line2.points[j]) / 2.0;
            }
        }
    }

    return std::nullopt;
}

FilletResult createFillet(
    const Entity& line1,
    const Entity& line2,
    double radius,
    int newArcId)
{
    FilletResult result;

    if (line1.type != EntityType::Line || line2.type != EntityType::Line) {
        result.errorMessage = "Fillet requires two lines";
        return result;
    }

    auto cornerOpt = findCornerPoint(line1, line2);
    if (!cornerOpt) {
        result.errorMessage = "Lines do not share a common endpoint";
        return result;
    }

    Point2D corner = *cornerOpt;

    // Find which endpoints are at the corner
    int idx1 = pointsCoincident(line1.points[0], corner) ? 0 : 1;
    int idx2 = pointsCoincident(line2.points[0], corner) ? 0 : 1;

    Point2D other1 = line1.points[1 - idx1];
    Point2D other2 = line2.points[1 - idx2];

    // Direction vectors from corner
    Point2D dir1 = normalize(other1 - corner);
    Point2D dir2 = normalize(other2 - corner);

    double len1 = lineLength(corner, other1);
    double len2 = lineLength(corner, other2);

    // Calculate angle between lines
    double dotProd = dot(dir1, dir2);
    double angle = std::acos(std::clamp(dotProd, -1.0, 1.0));

    // Calculate tangent distance from corner
    double tanHalfAngle = std::tan((M_PI - angle) / 2.0);
    if (std::abs(tanHalfAngle) < 0.001) {
        result.errorMessage = "Lines are nearly parallel";
        return result;
    }

    double tangentDist = radius / tanHalfAngle;

    if (tangentDist > len1 || tangentDist > len2) {
        result.errorMessage = "Fillet radius too large for these lines";
        return result;
    }

    // Calculate tangent points
    Point2D tangent1 = corner + dir1 * tangentDist;
    Point2D tangent2 = corner + dir2 * tangentDist;

    // Calculate arc center
    Point2D bisector = normalize(dir1 + dir2);
    double centerDist = radius / std::sin((M_PI - angle) / 2.0);
    Point2D arcCenter = corner + bisector * centerDist;

    // Calculate arc angles
    double startAngle = radiansToDegrees(std::atan2(
        tangent1.y - arcCenter.y,
        tangent1.x - arcCenter.x));
    double endAngle = radiansToDegrees(std::atan2(
        tangent2.y - arcCenter.y,
        tangent2.x - arcCenter.x));

    double sweep = endAngle - startAngle;
    sweep = geometry::wrapSweepDeg(sweep);

    // Create modified lines
    result.line1 = line1;
    result.line1.points[idx1] = tangent1;

    result.line2 = line2;
    result.line2.points[idx2] = tangent2;

    // Create fillet arc
    result.arc = createArc(newArcId, arcCenter, radius, startAngle, sweep);

    result.success = true;
    return result;
}

// =====================================================================
//  Chamfer Operation
// =====================================================================

ChamferResult createChamfer(
    const Entity& line1,
    const Entity& line2,
    double distance,
    int newLineId)
{
    return createChamfer(line1, line2, distance, distance, newLineId);
}

ChamferResult createChamfer(
    const Entity& line1,
    const Entity& line2,
    double distance1,
    double distance2,
    int newLineId)
{
    ChamferResult result;

    if (line1.type != EntityType::Line || line2.type != EntityType::Line) {
        result.errorMessage = "Chamfer requires two lines";
        return result;
    }

    auto cornerOpt = findCornerPoint(line1, line2);
    if (!cornerOpt) {
        result.errorMessage = "Lines do not share a common endpoint";
        return result;
    }

    Point2D corner = *cornerOpt;

    // Find which endpoints are at the corner
    int idx1 = pointsCoincident(line1.points[0], corner) ? 0 : 1;
    int idx2 = pointsCoincident(line2.points[0], corner) ? 0 : 1;

    Point2D other1 = line1.points[1 - idx1];
    Point2D other2 = line2.points[1 - idx2];

    double len1 = lineLength(corner, other1);
    double len2 = lineLength(corner, other2);

    if (distance1 > len1 || distance2 > len2) {
        result.errorMessage = "Chamfer distance too large for these lines";
        return result;
    }

    // Calculate chamfer points
    Point2D dir1 = normalize(other1 - corner);
    Point2D dir2 = normalize(other2 - corner);

    Point2D chamferPt1 = corner + dir1 * distance1;
    Point2D chamferPt2 = corner + dir2 * distance2;

    // Create modified lines
    result.line1 = line1;
    result.line1.points[idx1] = chamferPt1;

    result.line2 = line2;
    result.line2.points[idx2] = chamferPt2;

    // Create chamfer line
    result.chamferLine = createLine(newLineId, chamferPt1, chamferPt2);

    result.success = true;
    return result;
}

// =====================================================================
//  Trim Operation
// =====================================================================

TrimResult trimEntity(
    const Entity& entity,
    const std::vector<Point2D>& intersections,
    const Point2D& clickPos,
    std::function<int()> nextId)
{
    TrimResult result;

    if (intersections.empty()) {
        result.errorMessage = "No intersections found to trim at";
        return result;
    }

    if (entity.type == EntityType::Line && entity.points.size() >= 2) {
        // Sort intersections by parameter along line
        std::vector<double> params;
        for (const Point2D& pt : intersections) {
            double t = projectPointOnLine(pt, entity.points[0], entity.points[1]);
            if (t > 0.001 && t < 0.999) {
                params.push_back(t);
            }
        }

        if (params.empty()) {
            result.errorMessage = "No valid trim points on this segment";
            return result;
        }

        std::sort(params.begin(), params.end());

        // Find which segment the click is in
        double clickT = projectPointOnLine(clickPos, entity.points[0], entity.points[1]);

        // Add endpoints
        params.insert(params.begin(), 0.0);
        params.push_back(1.0);

        // Create segments except the one containing clickT
        result.removedEntityId = entity.id;

        for (int i = 0; i < static_cast<int>(params.size()) - 1; ++i) {
            if (clickT >= params[i] && clickT <= params[i + 1]) {
                continue;  // Skip this segment
            }

            Point2D p1 = pointOnLine(entity.points[0], entity.points[1], params[i]);
            Point2D p2 = pointOnLine(entity.points[0], entity.points[1], params[i + 1]);

            Entity newLine = createLine(nextId(), p1, p2);
            newLine.isConstruction = entity.isConstruction;
            result.newEntities.push_back(newLine);
        }

        result.success = true;
    }
    else if (entity.type == EntityType::Circle && !entity.points.empty()) {
        // Convert circle to arcs
        // For now, just handle the simple case of 2 intersections
        if (intersections.size() < 2) {
            result.errorMessage = "Circle requires at least 2 intersections to trim";
            return result;
        }

        // Calculate angles for each intersection
        std::vector<double> angles;
        for (const Point2D& pt : intersections) {
            double angle = radiansToDegrees(std::atan2(
                pt.y - entity.points[0].y,
                pt.x - entity.points[0].x));
            angles.push_back(normalizeAngle(angle));
        }

        std::sort(angles.begin(), angles.end());

        // Find which arc segment the click is in
        double clickAngle = normalizeAngle(radiansToDegrees(std::atan2(
            clickPos.y - entity.points[0].y,
            clickPos.x - entity.points[0].x)));

        result.removedEntityId = entity.id;

        // Create arcs for each segment except the one containing the click
        for (int i = 0; i < static_cast<int>(angles.size()); ++i) {
            int j = (i + 1) % static_cast<int>(angles.size());
            double startA = angles[i];
            double endA = angles[j];
            double sweep = endA - startA;
            if (sweep <= 0) sweep += 360.0;

            // Check if click is in this segment
            double relClick = normalizeAngle(clickAngle - startA);
            if (relClick >= 0 && relClick <= sweep) {
                continue;  // Skip this segment
            }

            Entity arc = createArc(nextId(), entity.points[0], entity.radius, startA, sweep);
            arc.isConstruction = entity.isConstruction;
            result.newEntities.push_back(arc);
        }

        result.success = true;
    }
    else {
        result.errorMessage = "Trim not supported for this entity type";
    }

    return result;
}

// =====================================================================
//  Extend Operation
// =====================================================================

ExtendResult extendEntity(
    const Entity& entity,
    const std::vector<Entity>& boundaries,
    int extendEnd,
    const Point2D& clickPos)
{
    ExtendResult result;

    if (entity.type != EntityType::Line || entity.points.size() < 2) {
        result.errorMessage = "Extend only supports lines";
        return result;
    }

    // Determine which end to extend
    int endIdx = extendEnd;
    if (endIdx < 0) {
        double d0 = std::hypot(clickPos.x - entity.points[0].x, clickPos.y - entity.points[0].y);
        double d1 = std::hypot(clickPos.x - entity.points[1].x, clickPos.y - entity.points[1].y);
        endIdx = (d0 < d1) ? 0 : 1;
    }

    Point2D extendPoint = entity.points[endIdx];
    Point2D anchorPoint = entity.points[1 - endIdx];
    Point2D dir = normalize(extendPoint - anchorPoint);

    // Find intersections with extension ray
    double bestDist = std::numeric_limits<double>::max();
    Point2D bestPoint = extendPoint;

    for (const Entity& boundary : boundaries) {
        if (boundary.id == entity.id) continue;

        // Create extended line (project far)
        Point2D farPoint = extendPoint + dir * 10000.0;

        auto intersections = findIntersection(
            createLine(-1, anchorPoint, farPoint), boundary);

        for (const Intersection& inter : intersections) {
            // Must be in extension direction
            double dist = dot(inter.point - extendPoint, dir);
            if (dist > 0.001 && dist < bestDist) {
                bestDist = dist;
                bestPoint = inter.point;
            }
        }
    }

    if (bestDist == std::numeric_limits<double>::max()) {
        result.errorMessage = "No intersection found in extension direction";
        return result;
    }

    result.entity = entity;
    result.entity.points[endIdx] = bestPoint;
    result.success = true;

    return result;
}

// =====================================================================
//  Split Operation
// =====================================================================

SplitResult splitEntityAt(
    const Entity& entity,
    const Point2D& splitPoint,
    std::function<int()> nextId)
{
    SplitResult result;

    if (entity.type == EntityType::Line && entity.points.size() >= 2) {
        double t = projectPointOnLine(splitPoint, entity.points[0], entity.points[1]);
        t = std::clamp(t, 0.01, 0.99);

        Point2D midPoint = pointOnLine(entity.points[0], entity.points[1], t);

        Entity line1 = createLine(nextId(), entity.points[0], midPoint);
        line1.isConstruction = entity.isConstruction;

        Entity line2 = createLine(nextId(), midPoint, entity.points[1]);
        line2.isConstruction = entity.isConstruction;

        result.newEntities.push_back(line1);
        result.newEntities.push_back(line2);
        result.removedEntityId = entity.id;
        result.success = true;
    }
    else if (entity.type == EntityType::Circle && !entity.points.empty()) {
        // A single point cannot divide a closed circle into two arcs (that would
        // force an arbitrary second cut, e.g. two 180-degree halves). It can open
        // the circle into one full-sweep (360-degree) arc that starts and ends at
        // the point; the two endpoints are deliberately left UNwelded (no
        // Coincident) so the arc can be pulled open afterwards. Cutting a circle
        // into multiple arcs needs two or more points (splitEntityAtIntersections).
        const Point2D c = entity.points[0];
        const double angle = normalizeAngle(radiansToDegrees(std::atan2(
            splitPoint.y - c.y, splitPoint.x - c.x)));
        Entity arc = createArc(nextId(), c, entity.radius, angle, 360.0);
        arc.isConstruction = entity.isConstruction;
        result.newEntities.push_back(arc);
        result.removedEntityId = entity.id;
        result.success = true;
    }
    else if (entity.type == EntityType::Arc && !entity.points.empty()) {
        Arc arc;
        arc.center = entity.points[0];
        arc.radius = entity.radius;
        arc.startAngle = entity.startAngle;
        arc.sweepAngle = entity.sweepAngle;

        auto splitArcs = geometry::splitArc(arc, splitPoint);
        if (splitArcs.size() == 2) {
            Entity arc1 = createArc(nextId(), arc.center, arc.radius,
                splitArcs[0].startAngle, splitArcs[0].sweepAngle);
            arc1.isConstruction = entity.isConstruction;

            Entity arc2 = createArc(nextId(), arc.center, arc.radius,
                splitArcs[1].startAngle, splitArcs[1].sweepAngle);
            arc2.isConstruction = entity.isConstruction;

            result.newEntities.push_back(arc1);
            result.newEntities.push_back(arc2);
            result.removedEntityId = entity.id;
            result.success = true;
        } else {
            result.errorMessage = "Could not split arc at this point";
        }
    }
    else {
        result.errorMessage = "Split not supported for this entity type";
    }

    return result;
}

SplitResult splitEntityAtIntersections(
    const Entity& entity,
    const std::vector<Point2D>& intersections,
    std::function<int()> nextId)
{
    SplitResult result;

    if (intersections.empty()) {
        result.errorMessage = "No intersection points to split at";
        return result;
    }

    if (entity.type == EntityType::Line && entity.points.size() >= 2) {
        // Sort intersections by parameter
        std::vector<double> params;
        params.push_back(0.0);
        for (const Point2D& pt : intersections) {
            double t = projectPointOnLine(pt, entity.points[0], entity.points[1]);
            if (t > 0.001 && t < 0.999) {
                params.push_back(t);
            }
        }
        params.push_back(1.0);

        std::sort(params.begin(), params.end());

        // Remove duplicates
        auto last = std::unique(params.begin(), params.end(),
            [](double a, double b) { return std::abs(a - b) < 0.001; });
        params.erase(last, params.end());

        if (params.size() <= 2) {
            result.errorMessage = "No valid split points on segment";
            return result;
        }

        result.removedEntityId = entity.id;

        for (int i = 0; i < static_cast<int>(params.size()) - 1; ++i) {
            Point2D p1 = pointOnLine(entity.points[0], entity.points[1], params[i]);
            Point2D p2 = pointOnLine(entity.points[0], entity.points[1], params[i + 1]);

            Entity newLine = createLine(nextId(), p1, p2);
            newLine.isConstruction = entity.isConstruction;
            result.newEntities.push_back(newLine);
        }

        result.success = true;
    }
    else if (entity.type == EntityType::Circle && !entity.points.empty()) {
        // A circle is cut into arcs at its crossing points. Two or more points
        // are required (one point cannot divide a closed loop); the arcs run
        // between consecutive points and their sweeps sum to 360, so the split
        // follows where the cuts fall rather than being forced into equal halves.
        const Point2D c = entity.points[0];
        std::vector<double> angles;
        for (const Point2D& pt : intersections)
            angles.push_back(normalizeAngle(
                radiansToDegrees(std::atan2(pt.y - c.y, pt.x - c.x))));
        std::sort(angles.begin(), angles.end());
        angles.erase(std::unique(angles.begin(), angles.end(),
            [](double a, double b) { return std::abs(a - b) < geometry::kAngleEpsDeg; }), angles.end());
        if (angles.size() < 2) {
            result.errorMessage = "A circle needs two or more split points";
            return result;
        }
        result.removedEntityId = entity.id;
        for (std::size_t i = 0; i < angles.size(); ++i) {
            const double a0 = angles[i];
            const double a1 = (i + 1 < angles.size()) ? angles[i + 1] : angles[0] + 360.0;
            Entity arc = createArc(nextId(), c, entity.radius, a0, a1 - a0);
            arc.isConstruction = entity.isConstruction;
            result.newEntities.push_back(arc);
        }
        result.success = true;
    }
    else if (entity.type == EntityType::Arc && !entity.points.empty()) {
        // An arc is cut into sub-arcs at its interior crossing points; the start
        // and end stay put, so the sub-arc sweeps sum to the original sweep. The
        // arc's own direction (sign of the sweep) is preserved.
        const Point2D c = entity.points[0];
        const double start = entity.startAngle;
        const double sweep = entity.sweepAngle;
        const double dir = (sweep < 0.0) ? -1.0 : 1.0;
        const double span = std::abs(sweep);
        std::vector<double> offs;   // offsets 0..span from the start, along the arc
        offs.push_back(0.0);
        for (const Point2D& pt : intersections) {
            const double a = radiansToDegrees(std::atan2(pt.y - c.y, pt.x - c.x));
            const double off = normalizeAngle((a - start) * dir);   // forward along arc
            if (off > geometry::kAngleEpsDeg && off < span - geometry::kAngleEpsDeg) offs.push_back(off);
        }
        offs.push_back(span);
        std::sort(offs.begin(), offs.end());
        offs.erase(std::unique(offs.begin(), offs.end(),
            [](double a, double b) { return std::abs(a - b) < geometry::kAngleEpsDeg; }), offs.end());
        if (offs.size() <= 2) {
            result.errorMessage = "No valid split points on arc";
            return result;
        }
        result.removedEntityId = entity.id;
        for (std::size_t i = 0; i + 1 < offs.size(); ++i) {
            const double a0 = start + dir * offs[i];
            const double segSweep = dir * (offs[i + 1] - offs[i]);
            Entity arc = createArc(nextId(), c, entity.radius, a0, segSweep);
            arc.isConstruction = entity.isConstruction;
            result.newEntities.push_back(arc);
        }
        result.success = true;
    }
    else {
        result.errorMessage = "Split not supported for this entity type";
    }

    return result;
}

// =====================================================================
//  Chain Selection
// =====================================================================

AddGroupResult addGroup(Group g, std::vector<Entity>& entities,
                        const std::vector<Constraint>& constraints, std::vector<Group>& groups)
{
    AddGroupResult res;
    if (g.entityIds.empty() && g.constraintIds.empty() && g.childGroupIds.empty()) {
        res.problem = AddGroupProblem::Empty;
        return res;
    }
    for (int id : g.entityIds) {
        if (!findEntityById(entities, id)) { res.problem = AddGroupProblem::MissingEntity; res.id = id; return res; }
    }
    for (int id : g.constraintIds) {
        if (!findConstraintById(constraints, id)) { res.problem = AddGroupProblem::MissingConstraint; res.id = id; return res; }
    }
    for (int id : g.childGroupIds) {
        if (!findGroupById(groups, id)) { res.problem = AddGroupProblem::MissingChildGroup; res.id = id; return res; }
    }
    if (const Group* other = findGroupByName(groups, g.name)) {
        res.problem = AddGroupProblem::NameInUse; res.id = other->id; return res;
    }
    if (g.id > 0) {
        if (findGroupById(groups, g.id)) { res.problem = AddGroupProblem::IdInUse; res.id = g.id; return res; }
    } else {
        g.id = nextFreeGroupId(groups);
    }
    for (int child : g.childGroupIds) {
        if (Group* c = findGroupById(groups, child)) c->parentGroupId = g.id;
    }
    for (int eid : g.entityIds) {
        if (Entity* e = findEntityById(entities, eid)) e->groupId = g.id;
    }
    res.id = g.id;
    groups.push_back(std::move(g));
    return res;
}

std::vector<Point2D> intersectionPointsTouching(const std::vector<Intersection>& all, int entityId)
{
    std::vector<Point2D> pts;
    for (const Intersection& inter : all) {
        if (inter.entityId1 == entityId || inter.entityId2 == entityId) pts.push_back(inter.point);
    }
    return pts;
}

std::vector<Point2D> bracketingSplitPoints(const Entity& line, const std::vector<Intersection>& all,
                                           const Point2D& click)
{
    std::vector<Point2D> splitPoints;
    if (line.type != EntityType::Line || line.points.size() < 2) return splitPoints;
    const Point2D p0(line.points[0]), p1(line.points[1]);

    // Parameter (0..1) along the line of each interior crossing.
    std::vector<std::pair<double, Point2D>> paramPts;
    for (const Intersection& inter : all) {
        if (inter.entityId1 != line.id && inter.entityId2 != line.id) continue;
        const double t = geometry::projectPointOnLine(inter.point, p0, p1);
        if (t > 0.001 && t < 0.999) paramPts.push_back({t, inter.point});
    }
    if (paramPts.empty()) return splitPoints;

    const double clickT = geometry::projectPointOnLine(click, p0, p1);
    double bestBefore = -1.0, bestAfter = 2.0;
    Point2D ptBefore, ptAfter;
    for (const auto& [t, pt] : paramPts) {
        if (t <= clickT && t > bestBefore) { bestBefore = t; ptBefore = pt; }
        if (t >= clickT && t < bestAfter)  { bestAfter = t;  ptAfter = pt; }
    }
    if (bestBefore >= 0.0) splitPoints.push_back(ptBefore);
    if (bestAfter <= 1.0 && std::abs(bestAfter - bestBefore) > 0.001) splitPoints.push_back(ptAfter);
    return splitPoints;
}

std::vector<EndpointPair> proximityWelds(const std::vector<Entity>& entities,
                                         const std::vector<Constraint>& constraints,
                                         int newEntityId, double tolerance)
{
    std::vector<EndpointPair> welds;
    const Entity* ne = findEntityById(entities, newEntityId);
    if (!ne || ne->type != EntityType::Line || ne->points.size() < 2) return welds;
    auto alreadyCoincident = [&](int e1, int i1, int e2, int i2) {
        for (const Constraint& c : constraints)
            if (isCoincidentBetween(c, e1, i1, e2, i2)) return true;
        return false;
    };
    for (int ni : {0, 1}) {
        const Point2D np(ne->points[ni]);
        bool done = false;
        for (const Entity& oe : entities) {
            if (done) break;
            if (oe.id == newEntityId || oe.type != EntityType::Line || oe.points.size() < 2) continue;
            for (int oi : {0, 1}) {
                if (geometry::lineLength(np, oe.points[oi]) >= tolerance) continue;
                if (!alreadyCoincident(newEntityId, ni, oe.id, oi))
                    welds.push_back({newEntityId, ni, oe.id, oi});
                done = true;
                break;
            }
        }
    }
    return welds;
}

std::vector<int> findConnectedChain(
    int startId,
    const std::vector<Entity>& entities,
    double tolerance)
{
    std::unordered_set<int> visited;
    std::vector<int> result;
    std::queue<int> queue;

    queue.push(startId);

    while (!queue.empty()) {
        int currentId = queue.front();
        queue.pop();
        if (visited.count(currentId) > 0) continue;
        visited.insert(currentId);
        result.push_back(currentId);

        // Find the current entity
        const Entity* current = nullptr;
        for (const Entity& e : entities) {
            if (e.id == currentId) {
                current = &e;
                break;
            }
        }
        if (!current) continue;
        const std::vector<Point2D> currentPts = current->connectionPoints();
        if (currentPts.empty()) continue;

        // Find connected entities
        for (const Entity& other : entities) {
            if (visited.count(other.id) > 0) continue;
            bool joined = false;
            for (const Point2D& p : other.connectionPoints()) {
                for (const Point2D& q : currentPts) {
                    if (pointsCoincident(p, q, tolerance)) { joined = true; break; }
                }
                if (joined) break;
            }
            if (joined) queue.push(other.id);
        }
    }

    return result;
}

int findConnectedLineAtCorner(
    const Entity& lineEntity,
    const std::vector<Entity>& allEntities,
    const Point2D& cornerHint,
    double tolerance)
{
    if (lineEntity.type != EntityType::Line || lineEntity.points.size() < 2) {
        return -1;
    }

    // Determine which endpoint is closer to the hint
    double d0 = std::hypot(lineEntity.points[0].x - cornerHint.x, lineEntity.points[0].y - cornerHint.y);
    double d1 = std::hypot(lineEntity.points[1].x - cornerHint.x, lineEntity.points[1].y - cornerHint.y);
    Point2D targetEndpoint = (d0 < d1) ? lineEntity.points[0] : lineEntity.points[1];

    // Find another line connected at this endpoint
    for (const Entity& other : allEntities) {
        if (other.id == lineEntity.id) continue;
        if (other.type != EntityType::Line) continue;
        if (other.points.size() < 2) continue;

        // Check if either endpoint matches
        if (std::hypot(other.points[0].x - targetEndpoint.x, other.points[0].y - targetEndpoint.y) < tolerance ||
            std::hypot(other.points[1].x - targetEndpoint.x, other.points[1].y - targetEndpoint.y) < tolerance) {
            return other.id;
        }
    }

    return -1;
}

// =====================================================================
//  Tangency Maintenance
// =====================================================================

ReestablishTangencyResult reestablishTangency(
    const Entity& arc,
    const Entity& parentEntity)
{
    ReestablishTangencyResult result;

    if (arc.type != EntityType::Arc || arc.points.size() < 3) {
        result.errorMessage = "Entity must be an arc with 3 points";
        return result;
    }

    // --- Project tangent point onto parent entity ---
    Point2D tanPt = arc.points[1];
    Point2D edgeDir(1.0, 0.0);

    if (parentEntity.type == EntityType::Line && parentEntity.points.size() >= 2) {
        tanPt = closestPointOnLine(arc.points[1],
                                   parentEntity.points[0], parentEntity.points[1]);
        edgeDir = parentEntity.points[1] - parentEntity.points[0];
    } else if (parentEntity.type == EntityType::Rectangle && parentEntity.points.size() >= 2) {
        // Expand rectangle to 4 corners
        Point2D corners[4];
        if (parentEntity.points.size() >= 4) {
            for (int i = 0; i < 4; ++i)
                corners[i] = parentEntity.points[i];
        } else {
            corners[0] = parentEntity.points[0];
            corners[1] = Point2D(parentEntity.points[1].x, parentEntity.points[0].y);
            corners[2] = parentEntity.points[1];
            corners[3] = Point2D(parentEntity.points[0].x, parentEntity.points[1].y);
        }

        // Find closest edge for projection and direction
        double minD = std::numeric_limits<double>::max();
        for (int i = 0; i < 4; ++i) {
            Point2D p = closestPointOnLine(arc.points[1],
                                           corners[i], corners[(i + 1) % 4]);
            double d = length(arc.points[1] - p);
            if (d < minD) {
                minD = d;
                tanPt = p;
                edgeDir = corners[(i + 1) % 4] - corners[i];
            }
        }
    } else {
        result.errorMessage = "Parent entity must be a Line or Rectangle";
        return result;
    }

    double edgeLen = length(edgeDir);
    if (edgeLen < geometry::kDegenerateLen) {
        result.errorMessage = "Parent entity edge has zero length";
        return result;
    }

    Point2D normal(-edgeDir.y / edgeLen, edgeDir.x / edgeLen);
    // Orient normal toward current center side
    Point2D off = arc.points[0] - tanPt;
    if (dot(off, normal) < 0)
        normal = Point2D(-normal.x, -normal.y);

    double radius = arc.radius;
    Point2D newCenter(tanPt.x + normal.x * radius,
                      tanPt.y + normal.y * radius);

    double newStartAngle = radiansToDegrees(std::atan2(
        tanPt.y - newCenter.y,
        tanPt.x - newCenter.x));

    double sweepAngle = arc.sweepAngle;
    double endRad = degreesToRadians(newStartAngle + sweepAngle);

    result.arc = arc;
    result.arc.points[0] = newCenter;
    result.arc.points[1] = tanPt;
    result.arc.points[2] = Point2D(
        newCenter.x + radius * std::cos(endRad),
        newCenter.y + radius * std::sin(endRad));
    result.arc.startAngle = newStartAngle;
    // radius and sweepAngle preserved
    result.success = true;

    return result;
}

// =====================================================================
//  Collinear Segment Rejoining
// =====================================================================

RejoinResult validateCollinearRejoin(
    const std::vector<Entity>& entities,
    double angleTolerance,
    double endpointTolerance)
{
    RejoinResult result;

    if (entities.size() < 2) {
        result.errorMessage = "At least 2 line segments are required.";
        return result;
    }

    // Validate: all must be lines with at least 2 points
    for (const Entity& e : entities) {
        if (e.type != EntityType::Line || e.points.size() < 2) {
            result.errorMessage = "All selected entities must be line segments.";
            return result;
        }
    }

    // Reference direction from first line
    Point2D refDir = entities[0].points[1] - entities[0].points[0];
    double refLen = length(refDir);
    if (refLen < geometry::kZeroEps) {
        result.errorMessage = "Selected line has zero length.";
        return result;
    }
    refDir = Point2D(refDir.x / refLen, refDir.y / refLen);

    // Validate collinearity
    for (size_t i = 1; i < entities.size(); ++i) {
        Point2D d = entities[i].points[1] - entities[i].points[0];
        double len = length(d);
        if (len < geometry::kZeroEps) continue;
        d = Point2D(d.x / len, d.y / len);
        double crossVal = std::abs(refDir.x * d.y - refDir.y * d.x);
        if (crossVal > angleTolerance) {
            result.errorMessage = "Selected lines are not collinear.";
            return result;
        }
    }

    // Project all endpoints onto reference line and sort by parameter
    Point2D refP0 = entities[0].points[0];
    struct Seg {
        int id = 0;
        double t0 = 0.0, t1 = 0.0;
        Point2D p0, p1;
    };
    std::vector<Seg> segs;
    for (const Entity& e : entities) {
        Point2D d0 = e.points[0] - refP0;
        Point2D d1 = e.points[1] - refP0;
        double t0 = d0.x * refDir.x + d0.y * refDir.y;
        double t1 = d1.x * refDir.x + d1.y * refDir.y;
        if (t0 > t1) {
            std::swap(t0, t1);
            segs.push_back({e.id, t0, t1, e.points[1], e.points[0]});
        } else {
            segs.push_back({e.id, t0, t1, e.points[0], e.points[1]});
        }
    }

    std::sort(segs.begin(), segs.end(),
              [](const Seg& a, const Seg& b) { return a.t0 < b.t0; });

    // Check contiguity
    for (size_t i = 1; i < segs.size(); ++i) {
        if (std::abs(segs[i].t0 - segs[i - 1].t1) > endpointTolerance) {
            result.errorMessage = "Selected lines do not form a contiguous chain.\n"
                                  "There is a gap between segments.";
            return result;
        }
    }

    // Collect junction points (interior endpoints between consecutive segments)
    for (size_t i = 0; i + 1 < segs.size(); ++i) {
        result.junctionPoints.push_back(segs[i].p1);
    }

    // Collect IDs of entities to remove
    for (const Seg& s : segs) {
        result.removedIds.push_back(s.id);
    }

    result.mergedStart = segs.front().p0;
    result.mergedEnd = segs.back().p1;
    result.success = true;

    return result;
}

RejoinResult validateCollinearRejoin(const std::vector<Entity>& entities,
                                     const std::vector<Entity>& all,
                                     int* attachedEntityId,
                                     double angleTolerance, double endpointTolerance)
{
    RejoinResult result = validateCollinearRejoin(entities, angleTolerance, endpointTolerance);
    if (!result.success) return result;
    const double tol = geometry::kCoincidentTol;
    for (const Point2D& jp : result.junctionPoints) {
        for (const Entity& e : all) {
            if (findEntityById(entities, e.id)) continue;
            for (const auto& ep : e.points) {
                if (geometry::lineLength(Point2D(ep), jp) < tol) {
                    result.success = false;
                    result.errorMessage = "Another entity is attached at an interior junction point.\n"
                                          "Cannot rejoin without breaking connectivity.";
                    if (attachedEntityId) *attachedEntityId = e.id;
                    return result;
                }
            }
        }
    }
    return result;
}


// ---- Geometric auto-constrain --------------------------------------------

std::vector<Constraint> autoConstrain(const std::vector<Entity>& entities,
                                      const std::vector<Constraint>& existing,
                                      int& nextConstraintId,
                                      double tolDist, double tolAngleDeg)
{
    std::vector<Constraint> added;
    std::vector<Constraint> all = existing;   // grows; used for the over-constrain guard
    const bool useSolver = Solver::isAvailable();
    Solver solver;

    auto lineDirAngle = [](const Entity& e) {   // degrees, 0..180 (undirected)
        const double dx = e.points[1].x - e.points[0].x;
        const double dy = e.points[1].y - e.points[0].y;
        double a = radiansToDegrees(std::atan2(dy, dx));
        while (a < 0) a += 180.0; while (a >= 180.0) a -= 180.0;
        return a;
    };
    auto lineLen = [](const Entity& e) {
        return std::hypot(e.points[1].x - e.points[0].x, e.points[1].y - e.points[0].y);
    };
    auto sortedPairs = [](const Constraint& c) {
        std::vector<std::pair<int,int>> v;
        for (size_t i = 0; i < c.entityIds.size(); ++i)
            v.push_back({c.entityIds[i], i < c.pointIndices.size() ? c.pointIndices[i] : -1});
        std::sort(v.begin(), v.end());
        return v;
    };
    auto sameAs = [&](const Constraint& a, const Constraint& b) {
        return a.type == b.type && sortedPairs(a) == sortedPairs(b);
    };
    auto tryAdd = [&](Constraint c) {
        for (const auto& e : all) if (sameAs(e, c)) return;   // already present
        if (useSolver && solver.checkOverConstrain(entities, all, c).wouldOverConstrain)
            return;                                           // would break the sketch
        c.id = nextConstraintId++;
        c.isDriving = true; c.enabled = true; c.satisfied = true; c.labelVisible = false;
        added.push_back(c);
        all.push_back(c);
    };
    auto mk = [](ConstraintType t, std::vector<int> ids, std::vector<int> pts = {}) {
        Constraint c; c.type = t; c.entityIds = std::move(ids); c.pointIndices = std::move(pts);
        return c;
    };

    // 1. Coincident endpoints (different entities, near-identical position).
    struct EP { int id; int idx; double x, y; };
    std::vector<EP> eps;
    for (const auto& e : entities) {
        if (e.type == EntityType::Line && e.points.size() >= 2) {
            eps.push_back({e.id, 0, e.points[0].x, e.points[0].y});
            eps.push_back({e.id, 1, e.points[1].x, e.points[1].y});
        } else if (e.type == EntityType::Arc && e.points.size() >= 3) {
            eps.push_back({e.id, 1, e.points[1].x, e.points[1].y});
            eps.push_back({e.id, 2, e.points[2].x, e.points[2].y});
        } else if (e.type == EntityType::Point && !e.points.empty()) {
            eps.push_back({e.id, 0, e.points[0].x, e.points[0].y});
        }
    }
    for (size_t i = 0; i < eps.size(); ++i)
        for (size_t j = i + 1; j < eps.size(); ++j) {
            if (eps[i].id == eps[j].id) continue;
            if (std::hypot(eps[i].x - eps[j].x, eps[i].y - eps[j].y) <= tolDist)
                tryAdd(mk(ConstraintType::Coincident, {eps[i].id, eps[j].id}, {eps[i].idx, eps[j].idx}));
        }

    // 2. Horizontal / Vertical for near-axis lines.
    for (const auto& e : entities) {
        if (e.type != EntityType::Line || e.points.size() < 2) continue;
        const double a = lineDirAngle(e);
        if (a <= tolAngleDeg || a >= 180.0 - tolAngleDeg)
            tryAdd(mk(ConstraintType::Horizontal, {e.id}));
        else if (std::fabs(a - 90.0) <= tolAngleDeg)
            tryAdd(mk(ConstraintType::Vertical, {e.id}));
    }

    // 3. Parallel / Perpendicular line pairs (redundant ones, e.g. two
    //    horizontals, are dropped by the over-constrain guard).
    std::vector<const Entity*> lines;
    for (const auto& e : entities) if (e.type == EntityType::Line && e.points.size() >= 2) lines.push_back(&e);
    for (size_t i = 0; i < lines.size(); ++i)
        for (size_t j = i + 1; j < lines.size(); ++j) {
            double d = std::fabs(lineDirAngle(*lines[i]) - lineDirAngle(*lines[j]));
            if (d > 90.0) d = 180.0 - d;   // 0..90
            if (d <= tolAngleDeg)
                tryAdd(mk(ConstraintType::Parallel, {lines[i]->id, lines[j]->id}));
            else if (std::fabs(d - 90.0) <= tolAngleDeg)
                tryAdd(mk(ConstraintType::Perpendicular, {lines[i]->id, lines[j]->id}));
        }

    // 4. Equal length (line pairs) and equal radius (circle/arc pairs).
    for (size_t i = 0; i < lines.size(); ++i)
        for (size_t j = i + 1; j < lines.size(); ++j)
            if (std::fabs(lineLen(*lines[i]) - lineLen(*lines[j])) <= tolDist)
                tryAdd(mk(ConstraintType::Equal, {lines[i]->id, lines[j]->id}));
    std::vector<const Entity*> curves;
    for (const auto& e : entities)
        if ((e.type == EntityType::Circle || e.type == EntityType::Arc) && !e.points.empty())
            curves.push_back(&e);
    for (size_t i = 0; i < curves.size(); ++i)
        for (size_t j = i + 1; j < curves.size(); ++j)
            if (std::fabs(curves[i]->radius - curves[j]->radius) <= tolDist)
                tryAdd(mk(ConstraintType::Equal, {curves[i]->id, curves[j]->id}));

    return added;
}

std::vector<Constraint> computeCutConstraints(
    const std::vector<Entity>& pieces,
    const std::vector<Entity>& others,
    const std::vector<Point2D>& junctionPoints,
    const std::function<int()>& nextConstraintId,
    double eps)
{
    std::vector<Constraint> out;

    auto pocTypeFor = [](EntityType t, ConstraintType& pt) -> bool {
        switch (t) {
            case EntityType::Line:   pt = ConstraintType::PointOnLine;   return true;
            case EntityType::Circle:
            case EntityType::Arc:    pt = ConstraintType::PointOnCircle; return true;
            case EntityType::Spline: pt = ConstraintType::PointOnSpline; return true;
            default: return false;
        }
    };

    for (const Point2D& jp : junctionPoints) {
        // Piece endpoints sitting at this junction.
        std::vector<std::pair<int, int>> hits;   // (entityId, pointIndex)
        for (const Entity& piece : pieces) {
            for (int i = 0; i < static_cast<int>(piece.points.size()); ++i) {
                const Point2D p{ piece.points[i].x, piece.points[i].y };
                if (std::hypot(p.x - jp.x, p.y - jp.y) <= eps)
                    hits.push_back({ piece.id, i });
            }
        }
        if (hits.empty()) continue;

        // Join pieces that meet here: Coincident, first-to-rest across ids.
        for (size_t k = 1; k < hits.size(); ++k) {
            if (hits[k].first == hits[0].first) continue;   // same piece is not a join
            Constraint c;
            c.id = nextConstraintId();
            c.type = ConstraintType::Coincident;
            c.entityIds = { hits[0].first, hits[k].first };
            c.pointIndices = { hits[0].second, hits[k].second };
            out.push_back(c);
        }

        // Tie a piece endpoint at this junction to any OTHER entity that also
        // meets it. Only the representative endpoint (hits[0]) is tied: where
        // several pieces meet they are already Coincident-joined above so the
        // rest follow, and where a lone piece touches itself here (a circle
        // opened into one 360-degree arc) only one of its two ends should be
        // pinned, never both. Which of the two is a free choice.
        const std::pair<int, int> rep = hits[0];
        for (const Entity& other : others) {
            // A point entity sitting exactly here ties by Coincident; a curve
            // passing through ties by point-on-object.
            if (other.type == EntityType::Point && !other.points.empty()) {
                const Point2D op{ other.points[0].x, other.points[0].y };
                if (std::hypot(op.x - jp.x, op.y - jp.y) > eps) continue;
                Constraint c;
                c.id = nextConstraintId();
                c.type = ConstraintType::Coincident;
                c.entityIds = { rep.first, other.id };
                c.pointIndices = { rep.second, 0 };
                out.push_back(c);
                continue;
            }
            ConstraintType pt;
            if (!pocTypeFor(other.type, pt)) continue;
            if (other.distanceTo(jp) > eps) continue;
            const int oIdx = nearestPointIndex(other, jp);
            if (oIdx < 0) continue;
            Constraint c;
            c.id = nextConstraintId();
            c.type = pt;
            c.entityIds = { rep.first, other.id };
            c.pointIndices = { rep.second, oIdx };
            out.push_back(c);
        }
    }
    return out;
}

std::vector<Constraint> remapCutConstraints(
    const std::vector<Constraint>& constraints,
    const Entity& original,
    const std::vector<Entity>& pieces,
    const std::function<int()>& nextConstraintId,
    double eps)
{
    std::vector<Constraint> out;
    const int oldId = original.id;

    // Position constraints anchored to a specific point of the original: the
    // reference moves to whichever piece still owns that point.
    auto isPointAnchored = [](ConstraintType t) {
        switch (t) {
            case ConstraintType::Coincident:
            case ConstraintType::PointOnLine:
            case ConstraintType::PointOnCircle:
            case ConstraintType::PointOnSpline:
            case ConstraintType::FixedPoint:
                return true;
            default:
                return false;
        }
    };

    // Direction constraints hold for every collinear line piece: replicate.
    auto isOrientation = [](ConstraintType t) {
        switch (t) {
            case ConstraintType::Horizontal:
            case ConstraintType::Vertical:
            case ConstraintType::Parallel:
            case ConstraintType::Perpendicular:
            case ConstraintType::Collinear:
            case ConstraintType::FixedAngle:
                return true;
            default:
                return false;
        }
    };

    // The piece (and its local point index) that carries a given point of the
    // original entity, or {-1,-1} when the cut removed that point.
    auto ownerOfPoint = [&](int origPtIdx) -> std::pair<int, int> {
        if (origPtIdx < 0 || origPtIdx >= static_cast<int>(original.points.size()))
            return { -1, -1 };
        const Point2D q{ original.points[origPtIdx].x, original.points[origPtIdx].y };
        for (const Entity& piece : pieces)
            for (int i = 0; i < static_cast<int>(piece.points.size()); ++i)
                if (std::hypot(piece.points[i].x - q.x, piece.points[i].y - q.y) <= eps)
                    return { piece.id, i };
        return { -1, -1 };
    };

    for (const Constraint& c : constraints) {
        bool refsOriginal = false;
        for (int eid : c.entityIds)
            if (eid == oldId) { refsOriginal = true; break; }
        if (!refsOriginal) continue;

        if (isPointAnchored(c.type)) {
            // Re-anchor every reference to the original onto its owning piece.
            // Drop the whole constraint if any anchored point was cut away.
            Constraint nc = c;
            nc.id = nextConstraintId();
            bool ok = true;
            for (std::size_t k = 0; k < nc.entityIds.size(); ++k) {
                if (nc.entityIds[k] != oldId) continue;
                const int pi = (k < nc.pointIndices.size()) ? nc.pointIndices[k] : -1;
                const std::pair<int, int> owner = ownerOfPoint(pi);
                if (owner.first < 0) { ok = false; break; }
                nc.entityIds[k] = owner.first;
                nc.pointIndices[k] = owner.second;
            }
            if (ok) out.push_back(nc);
            continue;
        }

        if (isOrientation(c.type)) {
            // Each collinear line piece keeps the original's direction.
            for (const Entity& piece : pieces) {
                if (piece.type != EntityType::Line) continue;
                Constraint nc = c;
                nc.id = nextConstraintId();
                for (std::size_t k = 0; k < nc.entityIds.size(); ++k)
                    if (nc.entityIds[k] == oldId) nc.entityIds[k] = piece.id;
                out.push_back(nc);
            }
            continue;
        }

        // Any other constraint (a length or angular dimension, Equal, Tangent,
        // Midpoint, Symmetric, Concentric, curvature) does not survive the cut:
        // the piece it would measure, or the count of pieces it would name, has
        // changed. Omit it.
    }
    return out;
}

int rederiveDependents(std::vector<Entity>& entities, const PlaneBasis& targetBasis,
                       const ProjectionSourceResolver& resolve)
{
    // Slots first: a slot stores its path's shape inline, so the solver moves
    // the centerline and leaves the slot behind. After the solve, not before:
    // the point is to follow where the path ENDED UP.
    int followed = 0;
    for (Entity& slot : entities) {
        if (slot.type != EntityType::Slot || slot.pathEntityIds.empty()) continue;
        if (slot.pathEntityIds.size() > 1) {
            if (updateSlotOutlineFromPaths(slot, entities)) ++followed;
            continue;
        }
        if (const Entity* path = findEntityById(entities, slot.pathEntityIds[0])) {
            if (updateSlotFromPath(slot, *path)) ++followed;
        }
    }

    // Associative offsets follow their parent the same way.
    for (Entity& child : entities) {
        if (child.offsetParentId < 0) continue;
        if (const Entity* parent = findEntityById(entities, child.offsetParentId))
            updateOffsetFromParent(child, *parent);
    }

    // Projections re-derive from their source through the two plane bases.
    for (Entity& child : entities) {
        if (child.projectionSourceId < 0) continue;
        Entity source;
        PlaneBasis sourceBasis = targetBasis;
        bool resolved = false;
        if (child.projectionSourceSketchId >= 0 && resolve)
            resolved = resolve(child.projectionSourceSketchId, child.projectionSourceId, source, sourceBasis);
        if (!resolved) {
            const Entity* s = findEntityById(entities, child.projectionSourceId);
            if (!s) continue;   // source unresolved: leave the projection frozen
            source = *s;
            sourceBasis = targetBasis;
        }
        updateProjectionFromSource(child, source, sourceBasis, targetBasis);
    }
    return followed;
}

}  // namespace sketch
}  // namespace hobbycad
