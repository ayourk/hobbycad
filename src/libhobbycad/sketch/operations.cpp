// =====================================================================
//  src/libhobbycad/sketch/operations.cpp — Sketch operations implementation
// =====================================================================
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include <hobbycad/sketch/operations.h>
#include <hobbycad/geometry/intersections.h>
#include <hobbycad/geometry/utils.h>

#include <queue>
#include <unordered_set>
#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

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
        result.success = true;
    }
    else if (entity.type == EntityType::Circle && !entity.points.empty()) {
        // Determine if offset is inward or outward
        double distToCenter = std::hypot(clickPos.x - entity.points[0].x, clickPos.y - entity.points[0].y);
        double newRadius = (distToCenter > entity.radius)
            ? entity.radius + distance
            : entity.radius - distance;

        if (newRadius < 0.1) {
            result.errorMessage = "Offset would create invalid radius";
            return result;
        }

        result.entity = createCircle(newId, entity.points[0], newRadius);
        result.entity.isConstruction = entity.isConstruction;
        result.success = true;
    }
    else if (entity.type == EntityType::Arc && !entity.points.empty()) {
        double distToCenter = std::hypot(clickPos.x - entity.points[0].x, clickPos.y - entity.points[0].y);
        double newRadius = (distToCenter > entity.radius)
            ? entity.radius + distance
            : entity.radius - distance;

        if (newRadius < 0.1) {
            result.errorMessage = "Offset would create invalid radius";
            return result;
        }

        result.entity = createArc(newId, entity.points[0], newRadius,
            entity.startAngle, entity.sweepAngle);
        result.entity.isConstruction = entity.isConstruction;
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
    double startAngle = std::atan2(
        tangent1.y - arcCenter.y,
        tangent1.x - arcCenter.x) * 180.0 / M_PI;
    double endAngle = std::atan2(
        tangent2.y - arcCenter.y,
        tangent2.x - arcCenter.x) * 180.0 / M_PI;

    double sweep = endAngle - startAngle;
    while (sweep > 180.0) sweep -= 360.0;
    while (sweep < -180.0) sweep += 360.0;

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
            double angle = std::atan2(
                pt.y - entity.points[0].y,
                pt.x - entity.points[0].x) * 180.0 / M_PI;
            angles.push_back(normalizeAngle(angle));
        }

        std::sort(angles.begin(), angles.end());

        // Find which arc segment the click is in
        double clickAngle = normalizeAngle(std::atan2(
            clickPos.y - entity.points[0].y,
            clickPos.x - entity.points[0].x) * 180.0 / M_PI);

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
        // Split circle into two arcs
        double angle = normalizeAngle(std::atan2(
            splitPoint.y - entity.points[0].y,
            splitPoint.x - entity.points[0].x) * 180.0 / M_PI);

        Entity arc1 = createArc(nextId(), entity.points[0], entity.radius, angle, 180.0);
        arc1.isConstruction = entity.isConstruction;

        Entity arc2 = createArc(nextId(), entity.points[0], entity.radius, angle + 180.0, 180.0);
        arc2.isConstruction = entity.isConstruction;

        result.newEntities.push_back(arc1);
        result.newEntities.push_back(arc2);
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
    else {
        // For other types, split at each intersection sequentially
        // This is a simplified approach
        result.errorMessage = "Multi-point split only fully supported for lines";
    }

    return result;
}

// =====================================================================
//  Chain Selection
// =====================================================================

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

        // Find connected entities
        for (const Entity& other : entities) {
            if (visited.count(other.id) > 0) continue;
            if (entitiesConnected(*current, other, tolerance)) {
                queue.push(other.id);
            }
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
    if (edgeLen < 1e-6) {
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

    double newStartAngle = std::atan2(
        tanPt.y - newCenter.y,
        tanPt.x - newCenter.x) * 180.0 / M_PI;

    double sweepAngle = arc.sweepAngle;
    double endRad = (newStartAngle + sweepAngle) * M_PI / 180.0;

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
    if (refLen < 1e-9) {
        result.errorMessage = "Selected line has zero length.";
        return result;
    }
    refDir = Point2D(refDir.x / refLen, refDir.y / refLen);

    // Validate collinearity
    for (size_t i = 1; i < entities.size(); ++i) {
        Point2D d = entities[i].points[1] - entities[i].points[0];
        double len = length(d);
        if (len < 1e-9) continue;
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
        int id;
        double t0, t1;
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

}  // namespace sketch
}  // namespace hobbycad
