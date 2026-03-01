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

#include <cmath>
#include <algorithm>
#include <unordered_set>
#include <map>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace hobbycad {
namespace sketch {

using namespace geometry;

// =====================================================================
//  Helper Functions
// =====================================================================

namespace {

/// Calculate distance from point to entity
double distanceToEntity(const Point2D& point, const Entity& entity)
{
    switch (entity.type) {
    case EntityType::Point:
        if (!entity.points.empty()) {
            return std::hypot(point.x - entity.points[0].x, point.y - entity.points[0].y);
        }
        break;

    case EntityType::Line:
        if (entity.points.size() >= 2) {
            return pointToLineDistance(point, entity.points[0], entity.points[1]);
        }
        break;

    case EntityType::Circle:
        if (!entity.points.empty()) {
            double distToCenter = std::hypot(point.x - entity.points[0].x, point.y - entity.points[0].y);
            return std::abs(distToCenter - entity.radius);
        }
        break;

    case EntityType::Arc:
        if (!entity.points.empty()) {
            return pointToArcDistance(point, Arc{
                entity.points[0], entity.radius, entity.startAngle, entity.sweepAngle
            });
        }
        break;

    case EntityType::Rectangle:
        if (entity.points.size() >= 2) {
            Point2D p1 = entity.points[0];
            Point2D p2 = entity.points[1];
            Point2D corners[4] = {
                p1,
                Point2D(p2.x, p1.y),
                p2,
                Point2D(p1.x, p2.y)
            };
            double minDist = std::numeric_limits<double>::max();
            for (int i = 0; i < 4; ++i) {
                double d = pointToLineDistance(point, corners[i], corners[(i+1)%4]);
                minDist = std::min(minDist, d);
            }
            return minDist;
        }
        break;

    case EntityType::Polygon:
        if (entity.points.size() >= 2) {
            double minDist = std::numeric_limits<double>::max();
            for (int i = 0; i < static_cast<int>(entity.points.size()); ++i) {
                int j = (i + 1) % static_cast<int>(entity.points.size());
                double d = pointToLineDistance(point, entity.points[i], entity.points[j]);
                minDist = std::min(minDist, d);
            }
            return minDist;
        }
        break;

    case EntityType::Ellipse:
        if (!entity.points.empty()) {
            // Approximate: use distance to closest point on ellipse
            // This is a simplified calculation
            Point2D center = entity.points[0];
            Point2D rel = point - center;
            double angle = std::atan2(rel.y, rel.x);
            Point2D ellipsePoint(
                center.x + entity.majorRadius * std::cos(angle),
                center.y + entity.minorRadius * std::sin(angle)
            );
            return std::hypot(point.x - ellipsePoint.x, point.y - ellipsePoint.y);
        }
        break;

    case EntityType::Slot:
        if (entity.points.size() >= 2) {
            // Distance to capsule shape
            double lineDist = pointToLineDistance(point, entity.points[0], entity.points[1]);
            return std::max(0.0, lineDist - entity.radius);
        }
        break;

    case EntityType::Spline:
        if (entity.points.size() >= 2) {
            // Approximate by checking control polygon
            double minDist = std::numeric_limits<double>::max();
            for (int i = 0; i < static_cast<int>(entity.points.size()) - 1; ++i) {
                double d = pointToLineDistance(point, entity.points[i], entity.points[i+1]);
                minDist = std::min(minDist, d);
            }
            return minDist;
        }
        break;

    case EntityType::Text:
        // Text hit testing would use bounding box
        if (!entity.points.empty()) {
            return std::hypot(point.x - entity.points[0].x, point.y - entity.points[0].y);
        }
        break;
    }

    return std::numeric_limits<double>::max();
}

/// Get closest point on entity to a query point
Point2D closestPointOnEntity(const Point2D& point, const Entity& entity)
{
    switch (entity.type) {
    case EntityType::Point:
        if (!entity.points.empty()) {
            return entity.points[0];
        }
        break;

    case EntityType::Line:
        if (entity.points.size() >= 2) {
            return closestPointOnLine(point, entity.points[0], entity.points[1]);
        }
        break;

    case EntityType::Circle:
        if (!entity.points.empty()) {
            return closestPointOnCircle(point, entity.points[0], entity.radius);
        }
        break;

    case EntityType::Arc:
        if (!entity.points.empty()) {
            return closestPointOnArc(point, Arc{
                entity.points[0], entity.radius, entity.startAngle, entity.sweepAngle
            });
        }
        break;

    default:
        // For complex entities, return nearest control point
        if (!entity.points.empty()) {
            double minDist = std::numeric_limits<double>::max();
            Point2D nearest = entity.points[0];
            for (const Point2D& p : entity.points) {
                double d = std::hypot(point.x - p.x, point.y - p.y);
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

std::vector<int> findEntitiesAtPoint(
    const std::vector<Entity>& entities,
    const Point2D& point,
    double tolerance)
{
    std::vector<std::pair<int, double>> hits;

    for (const Entity& entity : entities) {
        double dist = distanceToEntity(point, entity);
        if (dist <= tolerance) {
            hits.push_back({entity.id, dist});
        }
    }

    // Sort by distance
    std::sort(hits.begin(), hits.end(),
              [](const std::pair<int,double>& a, const std::pair<int,double>& b) {
                  return a.second < b.second;
              });

    std::vector<int> result;
    for (const auto& hit : hits) {
        result.push_back(hit.first);
    }
    return result;
}

HitTestResult findNearestEntity(
    const std::vector<Entity>& entities,
    const Point2D& point)
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
            for (int i = 0; i < static_cast<int>(entity.points.size()); ++i) {
                if (std::hypot(point.x - entity.points[i].x, point.y - entity.points[i].y) < POINT_TOLERANCE) {
                    result.pointIndex = i;
                    break;
                }
            }
        }
    }

    return result;
}

std::vector<int> findEntitiesInRect(
    const std::vector<Entity>& entities,
    const Rect2D& rect,
    bool mustBeFullyInside)
{
    std::vector<int> result;

    for (const Entity& entity : entities) {
        bool match = mustBeFullyInside
            ? entityEnclosedByRect(entity, rect)
            : entityIntersectsRect(entity, rect);
        if (match)
            result.push_back(entity.id);
    }

    return result;
}

std::vector<std::pair<int, int>> findControlPointsAtPoint(
    const std::vector<Entity>& entities,
    const Point2D& point,
    double tolerance)
{
    std::vector<std::pair<int, int>> result;

    for (const Entity& entity : entities) {
        for (int i = 0; i < static_cast<int>(entity.points.size()); ++i) {
            if (std::hypot(point.x - entity.points[i].x, point.y - entity.points[i].y) <= tolerance) {
                result.push_back({entity.id, i});
            }
        }
    }

    return result;
}

// =====================================================================
//  Sketch Validation
// =====================================================================

ValidationResult validateSketch(
    const std::vector<Entity>& entities,
    const std::vector<Constraint>& constraints)
{
    ValidationResult result;

    // Check for duplicate entity IDs
    std::unordered_set<int> entityIds;
    for (const Entity& e : entities) {
        if (entityIds.count(e.id) > 0) {
            result.errors.push_back("Duplicate entity ID: " + std::to_string(e.id));
            result.valid = false;
        }
        entityIds.insert(e.id);
    }

    // Check for duplicate constraint IDs
    std::unordered_set<int> constraintIds;
    for (const Constraint& c : constraints) {
        if (constraintIds.count(c.id) > 0) {
            result.errors.push_back("Duplicate constraint ID: " + std::to_string(c.id));
            result.valid = false;
        }
        constraintIds.insert(c.id);
    }

    // Check constraints reference valid entities
    for (const Constraint& c : constraints) {
        for (int entityId : c.entityIds) {
            if (entityIds.count(entityId) == 0) {
                result.errors.push_back("Constraint " + std::to_string(c.id) +
                    " references non-existent entity " + std::to_string(entityId));
                result.valid = false;
            }
        }
    }

    // Check for degenerate entities
    for (const Entity& e : entities) {
        switch (e.type) {
        case EntityType::Line:
            if (e.points.size() >= 2 &&
                std::hypot(e.points[0].x - e.points[1].x, e.points[0].y - e.points[1].y) < POINT_TOLERANCE) {
                result.warnings.push_back("Line " + std::to_string(e.id) + " has zero length");
            }
            break;
        case EntityType::Circle:
        case EntityType::Arc:
            if (e.radius < POINT_TOLERANCE) {
                result.warnings.push_back("Circle/Arc " + std::to_string(e.id) + " has zero radius");
            }
            break;
        default:
            break;
        }
    }

    return result;
}

bool isSketchFullyConstrained(
    const std::vector<Entity>& entities,
    const std::vector<Constraint>& constraints)
{
    if (!Solver::isAvailable()) {
        return false;
    }

    Solver solver;
    return solver.degreesOfFreedom(entities, constraints) == 0;
}

std::vector<int> findUnconstrainedEntities(
    const std::vector<Entity>& entities,
    const std::vector<Constraint>& constraints)
{
    // Collect all entity IDs referenced by constraints
    std::unordered_set<int> constrainedIds;
    for (const Constraint& c : constraints) {
        for (int id : c.entityIds) {
            constrainedIds.insert(id);
        }
    }

    // Find entities not referenced by any constraint
    std::vector<int> unconstrained;
    for (const Entity& e : entities) {
        if (constrainedIds.count(e.id) == 0) {
            unconstrained.push_back(e.id);
        }
    }

    return unconstrained;
}

std::vector<int> findUnderconstrainedEntities(
    const std::vector<Entity>& entities,
    const std::vector<Constraint>& constraints)
{
    // Heuristic: count constraints per entity and compare to expected DOF
    std::map<int, int> constraintCount;
    for (const Constraint& c : constraints) {
        for (int id : c.entityIds) {
            constraintCount[id]++;
        }
    }

    std::vector<int> underconstrained;
    for (const Entity& e : entities) {
        int count = constraintCount.count(e.id) ? constraintCount.at(e.id) : 0;
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
            expectedDOF = 2 * static_cast<int>(e.points.size());
            break;
        }

        // This is a rough heuristic - real DOF analysis needs the solver
        if (count > 0 && count < expectedDOF / 2) {
            underconstrained.push_back(e.id);
        }
    }

    return underconstrained;
}

// =====================================================================
//  Sketch Analysis
// =====================================================================

double sketchArea(const std::vector<Entity>& entities)
{
    ProfileDetectionOptions options;
    std::vector<Profile> profiles = detectProfiles(entities, options);

    double totalArea = 0.0;
    for (const Profile& p : profiles) {
        totalArea += std::abs(p.area);
    }

    return totalArea;
}

double sketchLength(const std::vector<Entity>& entities)
{
    double total = 0.0;
    for (const Entity& e : entities) {
        total += entityLength(e);
    }
    return total;
}

BoundingBox sketchBounds(const std::vector<Entity>& entities)
{
    BoundingBox bounds;
    for (const Entity& e : entities) {
        bounds.include(e.boundingBox());
    }
    return bounds;
}

std::map<EntityType, int> countEntitiesByType(const std::vector<Entity>& entities)
{
    std::map<EntityType, int> counts;
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
            return std::hypot(entity.points[0].x - entity.points[1].x, entity.points[0].y - entity.points[1].y);
        }
        break;

    case EntityType::Circle:
        return 2.0 * M_PI * entity.radius;

    case EntityType::Arc:
        return std::abs(entity.sweepAngle * M_PI / 180.0) * entity.radius;

    case EntityType::Rectangle:
        if (entity.points.size() >= 2) {
            double w = std::abs(entity.points[1].x - entity.points[0].x);
            double h = std::abs(entity.points[1].y - entity.points[0].y);
            return 2.0 * (w + h);
        }
        break;

    case EntityType::Polygon:
        {
            double len = 0.0;
            for (int i = 0; i < static_cast<int>(entity.points.size()); ++i) {
                int j = (i + 1) % static_cast<int>(entity.points.size());
                len += std::hypot(entity.points[i].x - entity.points[j].x, entity.points[i].y - entity.points[j].y);
            }
            return len;
        }

    case EntityType::Ellipse:
        // Ramanujan approximation for ellipse circumference
        {
            double a = entity.majorRadius;
            double b = entity.minorRadius;
            double h = std::pow((a - b) / (a + b), 2);
            return M_PI * (a + b) * (1.0 + 3.0 * h / (10.0 + std::sqrt(4.0 - 3.0 * h)));
        }

    case EntityType::Slot:
        if (entity.points.size() >= 2) {
            double lineLen = std::hypot(entity.points[0].x - entity.points[1].x, entity.points[0].y - entity.points[1].y);
            return 2.0 * lineLen + 2.0 * M_PI * entity.radius;
        }
        break;

    case EntityType::Spline:
        // Approximate by summing control polygon segments
        {
            double len = 0.0;
            for (int i = 0; i < static_cast<int>(entity.points.size()) - 1; ++i) {
                len += std::hypot(entity.points[i].x - entity.points[i+1].x, entity.points[i].y - entity.points[i+1].y);
            }
            return len;
        }

    case EntityType::Text:
        return 0.0;
    }

    return 0.0;
}

Point2D pointAtParameter(const Entity& entity, double t)
{
    t = std::clamp(t, 0.0, 1.0);

    switch (entity.type) {
    case EntityType::Point:
        if (!entity.points.empty()) {
            return entity.points[0];
        }
        break;

    case EntityType::Line:
        if (entity.points.size() >= 2) {
            return lerp(entity.points[0], entity.points[1], t);
        }
        break;

    case EntityType::Circle:
        if (!entity.points.empty()) {
            double angle = 2.0 * M_PI * t;
            return entity.points[0] + Point2D(
                entity.radius * std::cos(angle),
                entity.radius * std::sin(angle)
            );
        }
        break;

    case EntityType::Arc:
        if (!entity.points.empty()) {
            double startRad = entity.startAngle * M_PI / 180.0;
            double sweepRad = entity.sweepAngle * M_PI / 180.0;
            double angle = startRad + t * sweepRad;
            return entity.points[0] + Point2D(
                entity.radius * std::cos(angle),
                entity.radius * std::sin(angle)
            );
        }
        break;

    case EntityType::Rectangle:
        if (entity.points.size() >= 2) {
            // Traverse rectangle perimeter
            Point2D p1 = entity.points[0];
            Point2D p2 = entity.points[1];
            Point2D corners[4] = {
                p1,
                Point2D(p2.x, p1.y),
                p2,
                Point2D(p1.x, p2.y)
            };
            double perimeter = 2.0 * (std::abs(p2.x - p1.x) + std::abs(p2.y - p1.y));
            double dist = t * perimeter;
            double accumulated = 0.0;
            for (int i = 0; i < 4; ++i) {
                int j = (i + 1) % 4;
                double segLen = std::hypot(corners[i].x - corners[j].x, corners[i].y - corners[j].y);
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
            for (int i = 0; i < static_cast<int>(entity.points.size()) - 1; ++i) {
                totalLen += std::hypot(entity.points[i].x - entity.points[i+1].x, entity.points[i].y - entity.points[i+1].y);
            }
            double targetLen = t * totalLen;
            double accumulated = 0.0;
            for (int i = 0; i < static_cast<int>(entity.points.size()) - 1; ++i) {
                double segLen = std::hypot(entity.points[i].x - entity.points[i+1].x, entity.points[i].y - entity.points[i+1].y);
                if (accumulated + segLen >= targetLen) {
                    double segT = (targetLen - accumulated) / segLen;
                    return lerp(entity.points[i], entity.points[i+1], segT);
                }
                accumulated += segLen;
            }
            return entity.points.back();
        }
        break;

    default:
        if (!entity.points.empty()) {
            return entity.points[0];
        }
        break;
    }

    return Point2D();
}

double parameterAtPoint(const Entity& entity, const Point2D& point)
{
    switch (entity.type) {
    case EntityType::Point:
        return 0.0;

    case EntityType::Line:
        if (entity.points.size() >= 2) {
            double t = projectPointOnLine(point, entity.points[0], entity.points[1]);
            return std::clamp(t, 0.0, 1.0);
        }
        break;

    case EntityType::Circle:
        if (!entity.points.empty()) {
            Point2D rel = point - entity.points[0];
            double angle = std::atan2(rel.y, rel.x);
            if (angle < 0) angle += 2.0 * M_PI;
            return angle / (2.0 * M_PI);
        }
        break;

    case EntityType::Arc:
        if (!entity.points.empty()) {
            Point2D rel = point - entity.points[0];
            double angle = std::atan2(rel.y, rel.x) * 180.0 / M_PI;
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

            return std::clamp(relAngle / sweepAngle, 0.0, 1.0);
        }
        break;

    default:
        break;
    }

    return -1.0;
}

Point2D tangentAtParameter(const Entity& entity, double t)
{
    t = std::clamp(t, 0.0, 1.0);

    switch (entity.type) {
    case EntityType::Point:
        return Point2D(1, 0);  // Arbitrary

    case EntityType::Line:
        if (entity.points.size() >= 2) {
            return normalize(entity.points[1] - entity.points[0]);
        }
        break;

    case EntityType::Circle:
        if (!entity.points.empty()) {
            double angle = 2.0 * M_PI * t;
            return Point2D(-std::sin(angle), std::cos(angle));
        }
        break;

    case EntityType::Arc:
        if (!entity.points.empty()) {
            double startRad = entity.startAngle * M_PI / 180.0;
            double sweepRad = entity.sweepAngle * M_PI / 180.0;
            double angle = startRad + t * sweepRad;
            double sign = (sweepRad >= 0) ? 1.0 : -1.0;
            return Point2D(-sign * std::sin(angle), sign * std::cos(angle));
        }
        break;

    default:
        // Approximate by finite difference
        {
            double dt = 0.001;
            Point2D p1 = pointAtParameter(entity, std::max(0.0, t - dt));
            Point2D p2 = pointAtParameter(entity, std::min(1.0, t + dt));
            Point2D diff = p2 - p1;
            double len = length(diff);
            if (len > POINT_TOLERANCE) {
                return diff / len;
            }
        }
        break;
    }

    return Point2D(1, 0);
}

Point2D normalAtParameter(const Entity& entity, double t)
{
    Point2D tan = tangentAtParameter(entity, t);
    return perpendicular(tan);
}

// =====================================================================
//  Tessellation
// =====================================================================

std::vector<Point2D> tessellate(const Entity& entity, double tolerance)
{
    std::vector<Point2D> points;

    switch (entity.type) {
    case EntityType::Point:
        if (!entity.points.empty()) {
            points.push_back(entity.points[0]);
        }
        break;

    case EntityType::Line:
        if (entity.points.size() >= 2) {
            points.push_back(entity.points[0]);
            points.push_back(entity.points[1]);
        }
        break;

    case EntityType::Circle:
    case EntityType::Arc:
        {
            double sweepRad = (entity.type == EntityType::Circle)
                              ? 2.0 * M_PI
                              : std::abs(entity.sweepAngle) * M_PI / 180.0;
            // Number of segments based on tolerance
            int segments = std::max(8, static_cast<int>(
                std::ceil(sweepRad * entity.radius / tolerance)
            ));
            for (int i = 0; i <= segments; ++i) {
                double t = static_cast<double>(i) / segments;
                points.push_back(pointAtParameter(entity, t));
            }
        }
        break;

    case EntityType::Rectangle:
        if (entity.points.size() >= 2) {
            Point2D p1 = entity.points[0];
            Point2D p2 = entity.points[1];
            points.push_back(p1);
            points.push_back(Point2D(p2.x, p1.y));
            points.push_back(p2);
            points.push_back(Point2D(p1.x, p2.y));
            points.push_back(p1);  // Close
        }
        break;

    case EntityType::Polygon:
        points = entity.points;
        if (!points.empty() && !(points.front().x == points.back().x && points.front().y == points.back().y)) {
            points.push_back(points.front());
        }
        break;

    case EntityType::Ellipse:
        {
            // Approximate ellipse circumference for segment count
            double a = entity.majorRadius;
            double b = entity.minorRadius;
            double approxCircum = M_PI * (a + b);
            int segments = std::max(16, static_cast<int>(std::ceil(approxCircum / tolerance)));
            for (int i = 0; i <= segments; ++i) {
                double angle = 2.0 * M_PI * i / segments;
                if (!entity.points.empty()) {
                    points.push_back(entity.points[0] + Point2D(
                        a * std::cos(angle),
                        b * std::sin(angle)
                    ));
                }
            }
        }
        break;

    case EntityType::Slot:
        if (entity.points.size() >= 2) {
            // Two semicircles connected by lines
            Point2D p1 = entity.points[0];
            Point2D p2 = entity.points[1];
            Point2D dir = normalize(p2 - p1);
            Point2D perp = perpendicular(dir);

            int arcSegments = std::max(8, static_cast<int>(
                std::ceil(M_PI * entity.radius / tolerance)
            ));

            // First semicircle
            double baseAngle = std::atan2(perp.y, perp.x);
            for (int i = 0; i <= arcSegments; ++i) {
                double angle = baseAngle + M_PI * i / arcSegments;
                points.push_back(p1 + entity.radius * Point2D(std::cos(angle), std::sin(angle)));
            }

            // Second semicircle
            for (int i = 0; i <= arcSegments; ++i) {
                double angle = baseAngle + M_PI + M_PI * i / arcSegments;
                points.push_back(p2 + entity.radius * Point2D(std::cos(angle), std::sin(angle)));
            }

            points.push_back(points.front());  // Close
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

std::vector<std::pair<Point2D, Point2D>> tessellateToLines(const std::vector<Entity>& entities, double tolerance)
{
    std::vector<std::pair<Point2D, Point2D>> lines;

    for (const Entity& entity : entities) {
        std::vector<Point2D> points = tessellate(entity, tolerance);
        for (int i = 0; i < static_cast<int>(points.size()) - 1; ++i) {
            lines.push_back({points[i], points[i+1]});
        }
    }

    return lines;
}

}  // namespace sketch
}  // namespace hobbycad
