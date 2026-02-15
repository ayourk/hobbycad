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

#include <QQueue>
#include <QSet>
#include <algorithm>

namespace hobbycad {
namespace sketch {

using namespace geometry;

// =====================================================================
//  Intersection Detection
// =====================================================================

QVector<Intersection> findIntersection(const Entity& e1, const Entity& e2)
{
    QVector<Intersection> results;

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
                results.append(inter);
            }
        }
    }
    // Line-Circle
    else if (e1.type == EntityType::Line &&
             (e2.type == EntityType::Circle || e2.type == EntityType::Arc)) {
        if (e1.points.size() >= 2 && !e2.points.isEmpty()) {
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
                    results.append(inter);
                }
                if (lci.count >= 2 && lci.point2InSegment) {
                    Intersection inter;
                    inter.entityId1 = e1.id;
                    inter.entityId2 = e2.id;
                    inter.point = lci.point2;
                    inter.param1 = lci.t2;
                    results.append(inter);
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
                    results.append(inter);
                }
                if (lai.count >= 2 && lai.point2InSegment && lai.point2OnArc) {
                    Intersection inter;
                    inter.entityId1 = e1.id;
                    inter.entityId2 = e2.id;
                    inter.point = lai.point2;
                    results.append(inter);
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
        results.append(swapped);
    }
    // Circle-Circle
    else if ((e1.type == EntityType::Circle || e1.type == EntityType::Arc) &&
             (e2.type == EntityType::Circle || e2.type == EntityType::Arc)) {
        if (!e1.points.isEmpty() && !e2.points.isEmpty()) {
            if (e1.type == EntityType::Circle && e2.type == EntityType::Circle) {
                CircleCircleIntersection cci = circleCircleIntersection(
                    e1.points[0], e1.radius,
                    e2.points[0], e2.radius);

                if (cci.count >= 1) {
                    Intersection inter;
                    inter.entityId1 = e1.id;
                    inter.entityId2 = e2.id;
                    inter.point = cci.point1;
                    results.append(inter);
                }
                if (cci.count >= 2) {
                    Intersection inter;
                    inter.entityId1 = e1.id;
                    inter.entityId2 = e2.id;
                    inter.point = cci.point2;
                    results.append(inter);
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
                    results.append(inter);
                }
                if (cci.count >= 2) {
                    Intersection inter;
                    inter.entityId1 = e1.id;
                    inter.entityId2 = e2.id;
                    inter.point = cci.point2;
                    results.append(inter);
                }
            }
        }
    }

    return results;
}

QVector<Intersection> findIntersections(
    const Entity& entity,
    const QVector<Entity>& others)
{
    QVector<Intersection> results;

    for (const Entity& other : others) {
        if (other.id == entity.id) continue;
        results.append(findIntersection(entity, other));
    }

    return results;
}

QVector<Intersection> findAllIntersections(const QVector<Entity>& entities)
{
    QVector<Intersection> results;

    for (int i = 0; i < entities.size(); ++i) {
        for (int j = i + 1; j < entities.size(); ++j) {
            results.append(findIntersection(entities[i], entities[j]));
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
    const QPointF& clickPos,
    int newId)
{
    OffsetResult result;

    if (entity.type == EntityType::Line && entity.points.size() >= 2) {
        // Calculate perpendicular direction
        QPointF dir = entity.points[1] - entity.points[0];
        QPointF perp = normalize(perpendicular(dir));

        // Determine which side based on click position
        QPointF mid = lineMidpoint(entity.points[0], entity.points[1]);
        QPointF toClick = clickPos - mid;
        int side = (dot(toClick, perp) > 0) ? 1 : -1;

        QPointF offset = perp * (distance * side);

        result.entity = createLine(newId,
            entity.points[0] + offset,
            entity.points[1] + offset);
        result.entity.isConstruction = entity.isConstruction;
        result.success = true;
    }
    else if (entity.type == EntityType::Circle && !entity.points.isEmpty()) {
        // Determine if offset is inward or outward
        double distToCenter = QLineF(clickPos, entity.points[0]).length();
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
    else if (entity.type == EntityType::Arc && !entity.points.isEmpty()) {
        double distToCenter = QLineF(clickPos, entity.points[0]).length();
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
    QPointF clickPos;

    if (entity.type == EntityType::Line && entity.points.size() >= 2) {
        QPointF mid = lineMidpoint(entity.points[0], entity.points[1]);
        QPointF dir = entity.points[1] - entity.points[0];
        QPointF perp = normalize(perpendicular(dir));
        clickPos = mid + perp * (side > 0 ? 1.0 : -1.0);
    }
    else if ((entity.type == EntityType::Circle || entity.type == EntityType::Arc) &&
             !entity.points.isEmpty()) {
        clickPos = entity.points[0] + QPointF(entity.radius * side * 1.1, 0);
    }

    return offsetEntity(entity, distance, clickPos, newId);
}

// =====================================================================
//  Fillet Operation
// =====================================================================

std::optional<QPointF> findCornerPoint(
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

    QPointF corner = *cornerOpt;

    // Find which endpoints are at the corner
    int idx1 = pointsCoincident(line1.points[0], corner) ? 0 : 1;
    int idx2 = pointsCoincident(line2.points[0], corner) ? 0 : 1;

    QPointF other1 = line1.points[1 - idx1];
    QPointF other2 = line2.points[1 - idx2];

    // Direction vectors from corner
    QPointF dir1 = normalize(other1 - corner);
    QPointF dir2 = normalize(other2 - corner);

    double len1 = lineLength(corner, other1);
    double len2 = lineLength(corner, other2);

    // Calculate angle between lines
    double dotProd = dot(dir1, dir2);
    double angle = qAcos(qBound(-1.0, dotProd, 1.0));

    // Calculate tangent distance from corner
    double tanHalfAngle = qTan((M_PI - angle) / 2.0);
    if (qAbs(tanHalfAngle) < 0.001) {
        result.errorMessage = "Lines are nearly parallel";
        return result;
    }

    double tangentDist = radius / tanHalfAngle;

    if (tangentDist > len1 || tangentDist > len2) {
        result.errorMessage = "Fillet radius too large for these lines";
        return result;
    }

    // Calculate tangent points
    QPointF tangent1 = corner + dir1 * tangentDist;
    QPointF tangent2 = corner + dir2 * tangentDist;

    // Calculate arc center
    QPointF bisector = normalize(dir1 + dir2);
    double centerDist = radius / qSin((M_PI - angle) / 2.0);
    QPointF arcCenter = corner + bisector * centerDist;

    // Calculate arc angles
    double startAngle = qRadiansToDegrees(qAtan2(
        tangent1.y() - arcCenter.y(),
        tangent1.x() - arcCenter.x()));
    double endAngle = qRadiansToDegrees(qAtan2(
        tangent2.y() - arcCenter.y(),
        tangent2.x() - arcCenter.x()));

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

    QPointF corner = *cornerOpt;

    // Find which endpoints are at the corner
    int idx1 = pointsCoincident(line1.points[0], corner) ? 0 : 1;
    int idx2 = pointsCoincident(line2.points[0], corner) ? 0 : 1;

    QPointF other1 = line1.points[1 - idx1];
    QPointF other2 = line2.points[1 - idx2];

    double len1 = lineLength(corner, other1);
    double len2 = lineLength(corner, other2);

    if (distance1 > len1 || distance2 > len2) {
        result.errorMessage = "Chamfer distance too large for these lines";
        return result;
    }

    // Calculate chamfer points
    QPointF dir1 = normalize(other1 - corner);
    QPointF dir2 = normalize(other2 - corner);

    QPointF chamferPt1 = corner + dir1 * distance1;
    QPointF chamferPt2 = corner + dir2 * distance2;

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
    const QVector<QPointF>& intersections,
    const QPointF& clickPos,
    std::function<int()> nextId)
{
    TrimResult result;

    if (intersections.isEmpty()) {
        result.errorMessage = "No intersections found to trim at";
        return result;
    }

    if (entity.type == EntityType::Line && entity.points.size() >= 2) {
        // Sort intersections by parameter along line
        QVector<double> params;
        for (const QPointF& pt : intersections) {
            double t = projectPointOnLine(pt, entity.points[0], entity.points[1]);
            if (t > 0.001 && t < 0.999) {
                params.append(t);
            }
        }

        if (params.isEmpty()) {
            result.errorMessage = "No valid trim points on this segment";
            return result;
        }

        std::sort(params.begin(), params.end());

        // Find which segment the click is in
        double clickT = projectPointOnLine(clickPos, entity.points[0], entity.points[1]);

        // Add endpoints
        params.prepend(0.0);
        params.append(1.0);

        // Create segments except the one containing clickT
        result.removedEntityId = entity.id;

        for (int i = 0; i < params.size() - 1; ++i) {
            if (clickT >= params[i] && clickT <= params[i + 1]) {
                continue;  // Skip this segment
            }

            QPointF p1 = pointOnLine(entity.points[0], entity.points[1], params[i]);
            QPointF p2 = pointOnLine(entity.points[0], entity.points[1], params[i + 1]);

            Entity newLine = createLine(nextId(), p1, p2);
            newLine.isConstruction = entity.isConstruction;
            result.newEntities.append(newLine);
        }

        result.success = true;
    }
    else if (entity.type == EntityType::Circle && !entity.points.isEmpty()) {
        // Convert circle to arcs
        // For now, just handle the simple case of 2 intersections
        if (intersections.size() < 2) {
            result.errorMessage = "Circle requires at least 2 intersections to trim";
            return result;
        }

        // Calculate angles for each intersection
        QVector<double> angles;
        for (const QPointF& pt : intersections) {
            double angle = qRadiansToDegrees(qAtan2(
                pt.y() - entity.points[0].y(),
                pt.x() - entity.points[0].x()));
            angles.append(normalizeAngle(angle));
        }

        std::sort(angles.begin(), angles.end());

        // Find which arc segment the click is in
        double clickAngle = normalizeAngle(qRadiansToDegrees(qAtan2(
            clickPos.y() - entity.points[0].y(),
            clickPos.x() - entity.points[0].x())));

        result.removedEntityId = entity.id;

        // Create arcs for each segment except the one containing the click
        for (int i = 0; i < angles.size(); ++i) {
            int j = (i + 1) % angles.size();
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
            result.newEntities.append(arc);
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
    const QVector<Entity>& boundaries,
    int extendEnd,
    const QPointF& clickPos)
{
    ExtendResult result;

    if (entity.type != EntityType::Line || entity.points.size() < 2) {
        result.errorMessage = "Extend only supports lines";
        return result;
    }

    // Determine which end to extend
    int endIdx = extendEnd;
    if (endIdx < 0) {
        double d0 = QLineF(clickPos, entity.points[0]).length();
        double d1 = QLineF(clickPos, entity.points[1]).length();
        endIdx = (d0 < d1) ? 0 : 1;
    }

    QPointF extendPoint = entity.points[endIdx];
    QPointF anchorPoint = entity.points[1 - endIdx];
    QPointF dir = normalize(extendPoint - anchorPoint);

    // Find intersections with extension ray
    double bestDist = std::numeric_limits<double>::max();
    QPointF bestPoint = extendPoint;

    for (const Entity& boundary : boundaries) {
        if (boundary.id == entity.id) continue;

        // Create extended line (project far)
        QPointF farPoint = extendPoint + dir * 10000.0;

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
    const QPointF& splitPoint,
    std::function<int()> nextId)
{
    SplitResult result;

    if (entity.type == EntityType::Line && entity.points.size() >= 2) {
        double t = projectPointOnLine(splitPoint, entity.points[0], entity.points[1]);
        t = qBound(0.01, t, 0.99);

        QPointF midPoint = pointOnLine(entity.points[0], entity.points[1], t);

        Entity line1 = createLine(nextId(), entity.points[0], midPoint);
        line1.isConstruction = entity.isConstruction;

        Entity line2 = createLine(nextId(), midPoint, entity.points[1]);
        line2.isConstruction = entity.isConstruction;

        result.newEntities.append(line1);
        result.newEntities.append(line2);
        result.removedEntityId = entity.id;
        result.success = true;
    }
    else if (entity.type == EntityType::Circle && !entity.points.isEmpty()) {
        // Split circle into two arcs
        double angle = normalizeAngle(qRadiansToDegrees(qAtan2(
            splitPoint.y() - entity.points[0].y(),
            splitPoint.x() - entity.points[0].x())));

        Entity arc1 = createArc(nextId(), entity.points[0], entity.radius, angle, 180.0);
        arc1.isConstruction = entity.isConstruction;

        Entity arc2 = createArc(nextId(), entity.points[0], entity.radius, angle + 180.0, 180.0);
        arc2.isConstruction = entity.isConstruction;

        result.newEntities.append(arc1);
        result.newEntities.append(arc2);
        result.removedEntityId = entity.id;
        result.success = true;
    }
    else if (entity.type == EntityType::Arc && !entity.points.isEmpty()) {
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

            result.newEntities.append(arc1);
            result.newEntities.append(arc2);
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
    const QVector<QPointF>& intersections,
    std::function<int()> nextId)
{
    SplitResult result;

    if (intersections.isEmpty()) {
        result.errorMessage = "No intersection points to split at";
        return result;
    }

    if (entity.type == EntityType::Line && entity.points.size() >= 2) {
        // Sort intersections by parameter
        QVector<double> params;
        params.append(0.0);
        for (const QPointF& pt : intersections) {
            double t = projectPointOnLine(pt, entity.points[0], entity.points[1]);
            if (t > 0.001 && t < 0.999) {
                params.append(t);
            }
        }
        params.append(1.0);

        std::sort(params.begin(), params.end());

        // Remove duplicates
        auto last = std::unique(params.begin(), params.end(),
            [](double a, double b) { return qAbs(a - b) < 0.001; });
        params.erase(last, params.end());

        if (params.size() <= 2) {
            result.errorMessage = "No valid split points on segment";
            return result;
        }

        result.removedEntityId = entity.id;

        for (int i = 0; i < params.size() - 1; ++i) {
            QPointF p1 = pointOnLine(entity.points[0], entity.points[1], params[i]);
            QPointF p2 = pointOnLine(entity.points[0], entity.points[1], params[i + 1]);

            Entity newLine = createLine(nextId(), p1, p2);
            newLine.isConstruction = entity.isConstruction;
            result.newEntities.append(newLine);
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

QVector<int> findConnectedChain(
    int startId,
    const QVector<Entity>& entities,
    double tolerance)
{
    QSet<int> visited;
    QVector<int> result;
    QQueue<int> queue;

    queue.enqueue(startId);

    while (!queue.isEmpty()) {
        int currentId = queue.dequeue();
        if (visited.contains(currentId)) continue;
        visited.insert(currentId);
        result.append(currentId);

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
            if (visited.contains(other.id)) continue;
            if (entitiesConnected(*current, other, tolerance)) {
                queue.enqueue(other.id);
            }
        }
    }

    return result;
}

int findConnectedLineAtCorner(
    const Entity& lineEntity,
    const QVector<Entity>& allEntities,
    const QPointF& cornerHint,
    double tolerance)
{
    if (lineEntity.type != EntityType::Line || lineEntity.points.size() < 2) {
        return -1;
    }

    // Determine which endpoint is closer to the hint
    double d0 = QLineF(lineEntity.points[0], cornerHint).length();
    double d1 = QLineF(lineEntity.points[1], cornerHint).length();
    QPointF targetEndpoint = (d0 < d1) ? lineEntity.points[0] : lineEntity.points[1];

    // Find another line connected at this endpoint
    for (const Entity& other : allEntities) {
        if (other.id == lineEntity.id) continue;
        if (other.type != EntityType::Line) continue;
        if (other.points.size() < 2) continue;

        // Check if either endpoint matches
        if (QLineF(other.points[0], targetEndpoint).length() < tolerance ||
            QLineF(other.points[1], targetEndpoint).length() < tolerance) {
            return other.id;
        }
    }

    return -1;
}

}  // namespace sketch
}  // namespace hobbycad
