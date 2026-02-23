// =====================================================================
//  src/libhobbycad/sketch/snap.cpp — Snap point detection
// =====================================================================

#include "../hobbycad/sketch/snap.h"
#include "../hobbycad/geometry/intersections.h"

#include <QLineF>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace hobbycad {
namespace sketch {

// =====================================================================
//  defaultSnapWeight
// =====================================================================

double defaultSnapWeight(SnapType type)
{
    switch (type) {
    case SnapType::Origin:       return 4.0;   // Strongest
    case SnapType::Point:        return 3.0;   // Sketch point marker
    case SnapType::Endpoint:     return 2.5;
    case SnapType::Center:       return 2.5;
    case SnapType::Intersection: return 2.2;
    case SnapType::Midpoint:     return 2.0;
    case SnapType::Quadrant:     return 2.0;
    case SnapType::ArcEndCenter: return 2.0;
    case SnapType::AxisX:        return 1.25;
    case SnapType::AxisY:        return 1.25;
    case SnapType::Nearest:      return 1.0;   // Weakest
    }
    return 1.0;
}

// =====================================================================
//  collectSnapPoints  (single entity)
// =====================================================================

QVector<SnapPoint> collectSnapPoints(const Entity& entity)
{
    QVector<SnapPoint> points;

    switch (entity.type) {
    case EntityType::Point:
        if (!entity.points.isEmpty()) {
            points.append({entity.points[0], SnapType::Point, entity.id});
        }
        break;

    case EntityType::Line:
        if (entity.points.size() >= 2) {
            // Endpoints
            points.append({entity.points[0], SnapType::Endpoint, entity.id});
            points.append({entity.points[1], SnapType::Endpoint, entity.id});
            // Midpoint
            QPointF mid = (entity.points[0] + entity.points[1]) / 2.0;
            points.append({mid, SnapType::Midpoint, entity.id});
        }
        break;

    case EntityType::Rectangle:
        if (entity.points.size() >= 4) {
            // 4-point rotated rectangle
            QPointF c0 = entity.points[0];
            QPointF c1 = entity.points[1];
            QPointF c2 = entity.points[2];
            QPointF c3 = entity.points[3];
            // Four corners (endpoints)
            points.append({c0, SnapType::Endpoint, entity.id});
            points.append({c1, SnapType::Endpoint, entity.id});
            points.append({c2, SnapType::Endpoint, entity.id});
            points.append({c3, SnapType::Endpoint, entity.id});
            // Four edge midpoints
            points.append({(c0 + c1) / 2.0, SnapType::Midpoint, entity.id});
            points.append({(c1 + c2) / 2.0, SnapType::Midpoint, entity.id});
            points.append({(c2 + c3) / 2.0, SnapType::Midpoint, entity.id});
            points.append({(c3 + c0) / 2.0, SnapType::Midpoint, entity.id});
            // Center
            QPointF center = (c0 + c1 + c2 + c3) / 4.0;
            points.append({center, SnapType::Center, entity.id});
        } else if (entity.points.size() >= 2) {
            // Axis-aligned rectangle (2 opposite corners)
            QPointF p0 = entity.points[0];
            QPointF p1 = entity.points[1];
            // Four corners (endpoints)
            points.append({p0, SnapType::Endpoint, entity.id});
            points.append({QPointF(p1.x(), p0.y()), SnapType::Endpoint, entity.id});
            points.append({p1, SnapType::Endpoint, entity.id});
            points.append({QPointF(p0.x(), p1.y()), SnapType::Endpoint, entity.id});
            // Four edge midpoints
            points.append({QPointF((p0.x() + p1.x()) / 2, p0.y()), SnapType::Midpoint, entity.id});
            points.append({QPointF(p1.x(), (p0.y() + p1.y()) / 2), SnapType::Midpoint, entity.id});
            points.append({QPointF((p0.x() + p1.x()) / 2, p1.y()), SnapType::Midpoint, entity.id});
            points.append({QPointF(p0.x(), (p0.y() + p1.y()) / 2), SnapType::Midpoint, entity.id});
            // Center
            QPointF center = (p0 + p1) / 2.0;
            points.append({center, SnapType::Center, entity.id});
        }
        break;

    case EntityType::Parallelogram:
        if (entity.points.size() >= 4) {
            QPointF c0 = entity.points[0];
            QPointF c1 = entity.points[1];
            QPointF c2 = entity.points[2];
            QPointF c3 = entity.points[3];
            // Four corners (endpoints)
            points.append({c0, SnapType::Endpoint, entity.id});
            points.append({c1, SnapType::Endpoint, entity.id});
            points.append({c2, SnapType::Endpoint, entity.id});
            points.append({c3, SnapType::Endpoint, entity.id});
            // Four edge midpoints
            points.append({(c0 + c1) / 2.0, SnapType::Midpoint, entity.id});
            points.append({(c1 + c2) / 2.0, SnapType::Midpoint, entity.id});
            points.append({(c2 + c3) / 2.0, SnapType::Midpoint, entity.id});
            points.append({(c3 + c0) / 2.0, SnapType::Midpoint, entity.id});
            // Center
            QPointF center = (c0 + c1 + c2 + c3) / 4.0;
            points.append({center, SnapType::Center, entity.id});
        }
        break;

    case EntityType::Circle:
        if (!entity.points.isEmpty()) {
            QPointF center = entity.points[0];
            double r = entity.radius;
            // Center
            points.append({center, SnapType::Center, entity.id});
            // Quadrant points
            points.append({center + QPointF(r, 0), SnapType::Quadrant, entity.id});
            points.append({center + QPointF(-r, 0), SnapType::Quadrant, entity.id});
            points.append({center + QPointF(0, r), SnapType::Quadrant, entity.id});
            points.append({center + QPointF(0, -r), SnapType::Quadrant, entity.id});
        }
        break;

    case EntityType::Arc:
        if (!entity.points.isEmpty()) {
            QPointF center = entity.points[0];
            double r = entity.radius;
            // Center
            points.append({center, SnapType::Center, entity.id});
            // Arc endpoints
            double startRad = entity.startAngle * M_PI / 180.0;
            double endRad = (entity.startAngle + entity.sweepAngle) * M_PI / 180.0;
            QPointF start = center + QPointF(r * std::cos(startRad), r * std::sin(startRad));
            QPointF end = center + QPointF(r * std::cos(endRad), r * std::sin(endRad));
            points.append({start, SnapType::Endpoint, entity.id});
            points.append({end, SnapType::Endpoint, entity.id});
            // Arc midpoint
            double midRad = (startRad + endRad) / 2.0;
            QPointF mid = center + QPointF(r * std::cos(midRad), r * std::sin(midRad));
            points.append({mid, SnapType::Midpoint, entity.id});
        }
        break;

    case EntityType::Slot:
        if (entity.points.size() >= 3) {
            // Arc slot: points[0] = arc center, points[1] = start, points[2] = end
            QPointF arcCenter = entity.points[0];
            QPointF start = entity.points[1];
            QPointF end = entity.points[2];
            double halfWidth = entity.radius;

            // Arc center (for the centerline arc)
            points.append({arcCenter, SnapType::Center, entity.id});

            // Slot endpoint centers (where the semicircular ends are centered)
            points.append({start, SnapType::ArcEndCenter, entity.id});
            points.append({end, SnapType::ArcEndCenter, entity.id});

            // Midpoint of the centerline arc
            double arcRadius = QLineF(arcCenter, start).length();
            double startAngle = std::atan2(start.y() - arcCenter.y(), start.x() - arcCenter.x());
            double endAngle = std::atan2(end.y() - arcCenter.y(), end.x() - arcCenter.x());
            double sweep = endAngle - startAngle;
            while (sweep > M_PI) sweep -= 2 * M_PI;
            while (sweep < -M_PI) sweep += 2 * M_PI;
            if (entity.arcFlipped) {
                sweep = (sweep > 0) ? sweep - 2 * M_PI : sweep + 2 * M_PI;
            }
            double midAngle = startAngle + sweep / 2.0;
            QPointF midArc = arcCenter + QPointF(arcRadius * std::cos(midAngle), arcRadius * std::sin(midAngle));
            points.append({midArc, SnapType::Midpoint, entity.id});

            // Outer edge endpoints (extreme tips of the slot)
            QPointF startDir = (start - arcCenter);
            if (QLineF(arcCenter, start).length() > 0.001) {
                startDir = startDir / QLineF(arcCenter, start).length();
            }
            QPointF endDir = (end - arcCenter);
            if (QLineF(arcCenter, end).length() > 0.001) {
                endDir = endDir / QLineF(arcCenter, end).length();
            }
            QPointF startOuter = start + startDir * halfWidth;
            QPointF endOuter = end + endDir * halfWidth;
            points.append({startOuter, SnapType::Endpoint, entity.id});
            points.append({endOuter, SnapType::Endpoint, entity.id});
        } else if (entity.points.size() >= 2) {
            // Linear slot: points[0] and points[1] are arc centers
            QPointF p1 = entity.points[0];
            QPointF p2 = entity.points[1];
            double halfWidth = entity.radius;

            // Arc centers (slot end centers)
            points.append({p1, SnapType::ArcEndCenter, entity.id});
            points.append({p2, SnapType::ArcEndCenter, entity.id});

            // Centerline midpoint
            QPointF mid = (p1 + p2) / 2.0;
            points.append({mid, SnapType::Midpoint, entity.id});

            // Slot extreme endpoints
            double len = QLineF(p1, p2).length();
            if (len > 0.001) {
                QPointF dir = (p2 - p1) / len;
                QPointF end1 = p1 - dir * halfWidth;
                QPointF end2 = p2 + dir * halfWidth;
                points.append({end1, SnapType::Endpoint, entity.id});
                points.append({end2, SnapType::Endpoint, entity.id});
            }
        }
        break;

    case EntityType::Polygon:
        if (!entity.points.isEmpty()) {
            QPointF center = entity.points[0];
            double r = entity.radius;
            int sides = entity.sides > 0 ? entity.sides : 6;

            // Center
            points.append({center, SnapType::Center, entity.id});

            // Vertices and edge midpoints
            double angleStep = 2.0 * M_PI / sides;
            for (int i = 0; i < sides; ++i) {
                double angle = i * angleStep - M_PI / 2;  // Start at top
                QPointF vertex = center + QPointF(r * std::cos(angle), r * std::sin(angle));
                points.append({vertex, SnapType::Endpoint, entity.id});

                // Edge midpoint (between this vertex and next)
                double nextAngle = (i + 1) * angleStep - M_PI / 2;
                QPointF nextVertex = center + QPointF(r * std::cos(nextAngle), r * std::sin(nextAngle));
                QPointF edgeMid = (vertex + nextVertex) / 2.0;
                points.append({edgeMid, SnapType::Midpoint, entity.id});
            }
        }
        break;

    case EntityType::Ellipse:
        if (!entity.points.isEmpty()) {
            QPointF center = entity.points[0];
            double major = entity.majorRadius;
            double minor = entity.minorRadius;

            // Center
            points.append({center, SnapType::Center, entity.id});

            // Quadrant points (major and minor axis endpoints)
            points.append({center + QPointF(major, 0), SnapType::Quadrant, entity.id});
            points.append({center + QPointF(-major, 0), SnapType::Quadrant, entity.id});
            points.append({center + QPointF(0, minor), SnapType::Quadrant, entity.id});
            points.append({center + QPointF(0, -minor), SnapType::Quadrant, entity.id});
        }
        break;

    case EntityType::Spline:
        if (entity.points.size() >= 2) {
            // Endpoints
            points.append({entity.points.first(), SnapType::Endpoint, entity.id});
            points.append({entity.points.last(), SnapType::Endpoint, entity.id});

            // Control points as endpoints (useful for editing)
            for (int i = 1; i < entity.points.size() - 1; ++i) {
                points.append({entity.points[i], SnapType::Endpoint, entity.id});
            }
        }
        break;

    case EntityType::Text:
        if (!entity.points.isEmpty()) {
            // Text anchor point
            points.append({entity.points[0], SnapType::Endpoint, entity.id});
        }
        break;

    default:
        break;
    }

    return points;
}

// =====================================================================
//  Intersection helpers
// =====================================================================

/// Compute polygon vertices from center, radius, and side count.
static QVector<QPointF> computePolygonVertices(const Entity& entity)
{
    QVector<QPointF> verts;
    if (entity.type != EntityType::Polygon || entity.points.isEmpty())
        return verts;
    QPointF center = entity.points[0];
    double r = entity.radius;
    int sides = entity.sides > 0 ? entity.sides : 6;
    double angleStep = 2.0 * M_PI / sides;
    for (int i = 0; i < sides; ++i) {
        double angle = i * angleStep - M_PI / 2;  // Start at top
        verts.append(center + QPointF(r * std::cos(angle), r * std::sin(angle)));
    }
    return verts;
}

/// Build a geometry::Arc from a sketch Arc entity.
static geometry::Arc entityToArc(const Entity& entity)
{
    geometry::Arc arc;
    arc.center = entity.points[0];
    arc.radius = entity.radius;
    arc.startAngle = entity.startAngle;
    arc.sweepAngle = entity.sweepAngle;
    return arc;
}

/// Check if an entity type is edge-based (Rectangle 4-pt, Parallelogram, Polygon).
static bool isEdgeBasedEntity(EntityType type)
{
    return type == EntityType::Rectangle
        || type == EntityType::Parallelogram
        || type == EntityType::Polygon;
}

/// Get ordered edge list for edge-based entities.
static QVector<std::pair<QPointF, QPointF>> entityEdges(const Entity& entity)
{
    QVector<std::pair<QPointF, QPointF>> edges;
    if ((entity.type == EntityType::Rectangle || entity.type == EntityType::Parallelogram)
        && entity.points.size() >= 4) {
        for (int i = 0; i < 4; ++i)
            edges.append({entity.points[i], entity.points[(i + 1) % 4]});
    } else if (entity.type == EntityType::Polygon) {
        QVector<QPointF> verts = computePolygonVertices(entity);
        int n = verts.size();
        for (int i = 0; i < n; ++i)
            edges.append({verts[i], verts[(i + 1) % n]});
    }
    return edges;
}

// =====================================================================
//  computeEntityIntersectionPoints
// =====================================================================

QVector<QPointF> computeEntityIntersectionPoints(const Entity& e1, const Entity& e2)
{
    QVector<QPointF> result;

    // Line-Line intersection
    if (e1.type == EntityType::Line && e2.type == EntityType::Line) {
        if (e1.points.size() >= 2 && e2.points.size() >= 2) {
            auto isect = geometry::lineLineIntersection(
                e1.points[0], e1.points[1],
                e2.points[0], e2.points[1]);
            if (isect.intersects && isect.withinSegment1 && isect.withinSegment2) {
                result.append(isect.point);
            }
        }
    }
    // Line-Circle intersection
    else if (e1.type == EntityType::Line && e2.type == EntityType::Circle) {
        if (e1.points.size() >= 2 && !e2.points.isEmpty()) {
            auto isect = geometry::lineCircleIntersection(
                e1.points[0], e1.points[1],
                e2.points[0], e2.radius);
            if (isect.count >= 1 && isect.point1InSegment) {
                result.append(isect.point1);
            }
            if (isect.count >= 2 && isect.point2InSegment) {
                result.append(isect.point2);
            }
        }
    }
    else if (e1.type == EntityType::Circle && e2.type == EntityType::Line) {
        if (!e1.points.isEmpty() && e2.points.size() >= 2) {
            auto isect = geometry::lineCircleIntersection(
                e2.points[0], e2.points[1],
                e1.points[0], e1.radius);
            if (isect.count >= 1 && isect.point1InSegment) {
                result.append(isect.point1);
            }
            if (isect.count >= 2 && isect.point2InSegment) {
                result.append(isect.point2);
            }
        }
    }
    // Circle-Circle intersection
    else if (e1.type == EntityType::Circle && e2.type == EntityType::Circle) {
        if (!e1.points.isEmpty() && !e2.points.isEmpty()) {
            auto isect = geometry::circleCircleIntersection(
                e1.points[0], e1.radius,
                e2.points[0], e2.radius);
            if (isect.count >= 1) {
                result.append(isect.point1);
            }
            if (isect.count >= 2) {
                result.append(isect.point2);
            }
        }
    }
    // Line-Arc intersection
    else if (e1.type == EntityType::Line && e2.type == EntityType::Arc) {
        if (e1.points.size() >= 2 && !e2.points.isEmpty()) {
            geometry::Arc arc;
            arc.center = e2.points[0];
            arc.radius = e2.radius;
            arc.startAngle = e2.startAngle;
            arc.sweepAngle = e2.sweepAngle;
            auto isect = geometry::lineArcIntersection(e1.points[0], e1.points[1], arc);
            if (isect.count >= 1 && isect.point1InSegment && isect.point1OnArc) {
                result.append(isect.point1);
            }
            if (isect.count >= 2 && isect.point2InSegment && isect.point2OnArc) {
                result.append(isect.point2);
            }
        }
    }
    else if (e1.type == EntityType::Arc && e2.type == EntityType::Line) {
        if (!e1.points.isEmpty() && e2.points.size() >= 2) {
            geometry::Arc arc;
            arc.center = e1.points[0];
            arc.radius = e1.radius;
            arc.startAngle = e1.startAngle;
            arc.sweepAngle = e1.sweepAngle;
            auto isect = geometry::lineArcIntersection(e2.points[0], e2.points[1], arc);
            if (isect.count >= 1 && isect.point1InSegment && isect.point1OnArc) {
                result.append(isect.point1);
            }
            if (isect.count >= 2 && isect.point2InSegment && isect.point2OnArc) {
                result.append(isect.point2);
            }
        }
    }
    // Line-Rectangle/Parallelogram intersection (treat as 4 edges)
    else if (e1.type == EntityType::Line &&
             (e2.type == EntityType::Rectangle || e2.type == EntityType::Parallelogram)) {
        if (e1.points.size() >= 2 && e2.points.size() >= 4) {
            for (int edge = 0; edge < 4; ++edge) {
                QPointF p1 = e2.points[edge];
                QPointF p2 = e2.points[(edge + 1) % 4];
                auto isect = geometry::lineLineIntersection(
                    e1.points[0], e1.points[1], p1, p2);
                if (isect.intersects && isect.withinSegment1 && isect.withinSegment2) {
                    result.append(isect.point);
                }
            }
        }
    }
    else if ((e1.type == EntityType::Rectangle || e1.type == EntityType::Parallelogram) &&
             e2.type == EntityType::Line) {
        if (e1.points.size() >= 4 && e2.points.size() >= 2) {
            for (int edge = 0; edge < 4; ++edge) {
                QPointF p1 = e1.points[edge];
                QPointF p2 = e1.points[(edge + 1) % 4];
                auto isect = geometry::lineLineIntersection(
                    p1, p2, e2.points[0], e2.points[1]);
                if (isect.intersects && isect.withinSegment1 && isect.withinSegment2) {
                    result.append(isect.point);
                }
            }
        }
    }
    // Circle-Rectangle/Parallelogram intersection (treat edges as lines)
    else if (e1.type == EntityType::Circle &&
             (e2.type == EntityType::Rectangle || e2.type == EntityType::Parallelogram)) {
        if (!e1.points.isEmpty() && e2.points.size() >= 4) {
            for (int edge = 0; edge < 4; ++edge) {
                QPointF p1 = e2.points[edge];
                QPointF p2 = e2.points[(edge + 1) % 4];
                auto isect = geometry::lineCircleIntersection(p1, p2, e1.points[0], e1.radius);
                if (isect.count >= 1 && isect.point1InSegment) {
                    result.append(isect.point1);
                }
                if (isect.count >= 2 && isect.point2InSegment) {
                    result.append(isect.point2);
                }
            }
        }
    }
    else if ((e1.type == EntityType::Rectangle || e1.type == EntityType::Parallelogram) &&
             e2.type == EntityType::Circle) {
        if (e1.points.size() >= 4 && !e2.points.isEmpty()) {
            for (int edge = 0; edge < 4; ++edge) {
                QPointF p1 = e1.points[edge];
                QPointF p2 = e1.points[(edge + 1) % 4];
                auto isect = geometry::lineCircleIntersection(p1, p2, e2.points[0], e2.radius);
                if (isect.count >= 1 && isect.point1InSegment) {
                    result.append(isect.point1);
                }
                if (isect.count >= 2 && isect.point2InSegment) {
                    result.append(isect.point2);
                }
            }
        }
    }
    // Arc-Circle intersection (circle-circle filtered by arc sweep)
    else if (e1.type == EntityType::Arc && e2.type == EntityType::Circle) {
        if (!e1.points.isEmpty() && !e2.points.isEmpty()) {
            geometry::Arc arc = entityToArc(e1);
            auto isect = geometry::circleCircleIntersection(
                e1.points[0], e1.radius, e2.points[0], e2.radius);
            if (isect.count >= 1) {
                double angle = std::atan2(isect.point1.y() - arc.center.y(),
                                          isect.point1.x() - arc.center.x()) * 180.0 / M_PI;
                if (arc.containsAngle(angle))
                    result.append(isect.point1);
            }
            if (isect.count >= 2) {
                double angle = std::atan2(isect.point2.y() - arc.center.y(),
                                          isect.point2.x() - arc.center.x()) * 180.0 / M_PI;
                if (arc.containsAngle(angle))
                    result.append(isect.point2);
            }
        }
    }
    else if (e1.type == EntityType::Circle && e2.type == EntityType::Arc) {
        if (!e1.points.isEmpty() && !e2.points.isEmpty()) {
            geometry::Arc arc = entityToArc(e2);
            auto isect = geometry::circleCircleIntersection(
                e1.points[0], e1.radius, e2.points[0], e2.radius);
            if (isect.count >= 1) {
                double angle = std::atan2(isect.point1.y() - arc.center.y(),
                                          isect.point1.x() - arc.center.x()) * 180.0 / M_PI;
                if (arc.containsAngle(angle))
                    result.append(isect.point1);
            }
            if (isect.count >= 2) {
                double angle = std::atan2(isect.point2.y() - arc.center.y(),
                                          isect.point2.x() - arc.center.x()) * 180.0 / M_PI;
                if (arc.containsAngle(angle))
                    result.append(isect.point2);
            }
        }
    }
    // Arc-Arc intersection
    else if (e1.type == EntityType::Arc && e2.type == EntityType::Arc) {
        if (!e1.points.isEmpty() && !e2.points.isEmpty()) {
            geometry::Arc arc1 = entityToArc(e1);
            geometry::Arc arc2 = entityToArc(e2);
            auto isect = geometry::arcArcIntersection(arc1, arc2);
            if (isect.count >= 1)
                result.append(isect.point1);
            if (isect.count >= 2)
                result.append(isect.point2);
        }
    }
    // Arc × edge-based (handles Arc-Rect/Para, Arc-Polygon)
    else if (e1.type == EntityType::Arc && isEdgeBasedEntity(e2.type)) {
        if (!e1.points.isEmpty()) {
            geometry::Arc arc = entityToArc(e1);
            auto edges = entityEdges(e2);
            for (const auto& [p1, p2] : edges) {
                auto isect = geometry::lineArcIntersection(p1, p2, arc);
                if (isect.count >= 1 && isect.point1InSegment && isect.point1OnArc)
                    result.append(isect.point1);
                if (isect.count >= 2 && isect.point2InSegment && isect.point2OnArc)
                    result.append(isect.point2);
            }
        }
    }
    else if (isEdgeBasedEntity(e1.type) && e2.type == EntityType::Arc) {
        if (!e2.points.isEmpty()) {
            geometry::Arc arc = entityToArc(e2);
            auto edges = entityEdges(e1);
            for (const auto& [p1, p2] : edges) {
                auto isect = geometry::lineArcIntersection(p1, p2, arc);
                if (isect.count >= 1 && isect.point1InSegment && isect.point1OnArc)
                    result.append(isect.point1);
                if (isect.count >= 2 && isect.point2InSegment && isect.point2OnArc)
                    result.append(isect.point2);
            }
        }
    }
    // Line × Polygon (Rect/Para already caught above)
    else if (e1.type == EntityType::Line && isEdgeBasedEntity(e2.type)) {
        if (e1.points.size() >= 2) {
            auto edges = entityEdges(e2);
            for (const auto& [p1, p2] : edges) {
                auto isect = geometry::lineLineIntersection(e1.points[0], e1.points[1], p1, p2);
                if (isect.intersects && isect.withinSegment1 && isect.withinSegment2)
                    result.append(isect.point);
            }
        }
    }
    else if (isEdgeBasedEntity(e1.type) && e2.type == EntityType::Line) {
        if (e2.points.size() >= 2) {
            auto edges = entityEdges(e1);
            for (const auto& [p1, p2] : edges) {
                auto isect = geometry::lineLineIntersection(p1, p2, e2.points[0], e2.points[1]);
                if (isect.intersects && isect.withinSegment1 && isect.withinSegment2)
                    result.append(isect.point);
            }
        }
    }
    // Circle × Polygon (Rect/Para already caught above)
    else if (e1.type == EntityType::Circle && isEdgeBasedEntity(e2.type)) {
        if (!e1.points.isEmpty()) {
            auto edges = entityEdges(e2);
            for (const auto& [p1, p2] : edges) {
                auto isect = geometry::lineCircleIntersection(p1, p2, e1.points[0], e1.radius);
                if (isect.count >= 1 && isect.point1InSegment)
                    result.append(isect.point1);
                if (isect.count >= 2 && isect.point2InSegment)
                    result.append(isect.point2);
            }
        }
    }
    else if (isEdgeBasedEntity(e1.type) && e2.type == EntityType::Circle) {
        if (!e2.points.isEmpty()) {
            auto edges = entityEdges(e1);
            for (const auto& [p1, p2] : edges) {
                auto isect = geometry::lineCircleIntersection(p1, p2, e2.points[0], e2.radius);
                if (isect.count >= 1 && isect.point1InSegment)
                    result.append(isect.point1);
                if (isect.count >= 2 && isect.point2InSegment)
                    result.append(isect.point2);
            }
        }
    }
    // Edge-based × edge-based (Rect-Rect, Rect-Polygon, Polygon-Polygon, etc.)
    else if (isEdgeBasedEntity(e1.type) && isEdgeBasedEntity(e2.type)) {
        auto edges1 = entityEdges(e1);
        auto edges2 = entityEdges(e2);
        for (const auto& [a1, a2] : edges1) {
            for (const auto& [b1, b2] : edges2) {
                auto isect = geometry::lineLineIntersection(a1, a2, b1, b2);
                if (isect.intersects && isect.withinSegment1 && isect.withinSegment2)
                    result.append(isect.point);
            }
        }
    }

    return result;
}

// =====================================================================
//  collectAxisCrossingSnapPoints
// =====================================================================

QVector<SnapPoint> collectAxisCrossingSnapPoints(
    const QVector<Entity>& entities,
    int excludeEntityId)
{
    QVector<SnapPoint> points;
    constexpr double kEps = 1e-9;

    for (const Entity& entity : entities) {
        if (entity.id == excludeEntityId) continue;

        auto addAxisPoint = [&](const QPointF& pt) {
            // Skip if at origin — Origin snap already covers (0,0)
            if (std::abs(pt.x()) < kEps && std::abs(pt.y()) < kEps)
                return;
            points.append({pt, SnapType::Intersection, entity.id});
        };

        // Helper: compute where a line segment crosses X=0 and Y=0
        auto lineAxisCrossings = [&](const QPointF& p1, const QPointF& p2) {
            double dx = p2.x() - p1.x();
            double dy = p2.y() - p1.y();

            // Crossing with Y axis (X = 0)
            if (std::abs(dx) > kEps) {
                double t = -p1.x() / dx;
                if (t > kEps && t < 1.0 - kEps) {  // Exclude endpoints
                    addAxisPoint(QPointF(0.0, p1.y() + t * dy));
                }
            }

            // Crossing with X axis (Y = 0)
            if (std::abs(dy) > kEps) {
                double t = -p1.y() / dy;
                if (t > kEps && t < 1.0 - kEps) {  // Exclude endpoints
                    addAxisPoint(QPointF(p1.x() + t * dx, 0.0));
                }
            }
        };

        switch (entity.type) {
        case EntityType::Line:
            if (entity.points.size() >= 2) {
                lineAxisCrossings(entity.points[0], entity.points[1]);
            }
            break;

        case EntityType::Rectangle:
        case EntityType::Parallelogram:
            if (entity.points.size() >= 4) {
                for (int edge = 0; edge < 4; ++edge) {
                    lineAxisCrossings(entity.points[edge],
                                      entity.points[(edge + 1) % 4]);
                }
            }
            break;

        case EntityType::Circle:
            if (!entity.points.isEmpty()) {
                QPointF c = entity.points[0];
                double r = entity.radius;
                // Circle crosses Y axis (X=0) when |c.x| <= r
                if (std::abs(c.x()) <= r + kEps) {
                    double disc = r * r - c.x() * c.x();
                    if (disc >= 0.0) {
                        double sq = std::sqrt(disc);
                        addAxisPoint(QPointF(0.0, c.y() + sq));
                        if (sq > kEps) {
                            addAxisPoint(QPointF(0.0, c.y() - sq));
                        }
                    }
                }
                // Circle crosses X axis (Y=0) when |c.y| <= r
                if (std::abs(c.y()) <= r + kEps) {
                    double disc = r * r - c.y() * c.y();
                    if (disc >= 0.0) {
                        double sq = std::sqrt(disc);
                        addAxisPoint(QPointF(c.x() + sq, 0.0));
                        if (sq > kEps) {
                            addAxisPoint(QPointF(c.x() - sq, 0.0));
                        }
                    }
                }
            }
            break;

        case EntityType::Arc:
            if (!entity.points.isEmpty()) {
                QPointF c = entity.points[0];
                double r = entity.radius;
                double startDeg = entity.startAngle;
                double sweepDeg = entity.sweepAngle;

                // Helper: check if angle (degrees) is within the arc span
                auto angleOnArc = [startDeg, sweepDeg](double angleDeg) -> bool {
                    // Normalize to [0, 360)
                    double s = std::fmod(startDeg, 360.0);
                    if (s < 0) s += 360.0;
                    double a = std::fmod(angleDeg, 360.0);
                    if (a < 0) a += 360.0;
                    double sweep = std::abs(sweepDeg);
                    double offset = std::fmod(a - s + 720.0, 360.0);
                    if (sweepDeg < 0) offset = std::fmod(s - a + 720.0, 360.0);
                    return offset <= sweep + 1e-6;
                };

                // Arc crosses Y axis at x=0
                if (std::abs(c.x()) <= r + kEps) {
                    double disc = r * r - c.x() * c.x();
                    if (disc >= 0.0) {
                        double sq = std::sqrt(disc);
                        // Two candidate points
                        double angle1 = std::atan2(sq, -c.x()) * 180.0 / M_PI;
                        double angle2 = std::atan2(-sq, -c.x()) * 180.0 / M_PI;
                        if (angleOnArc(angle1)) addAxisPoint(QPointF(0.0, c.y() + sq));
                        if (sq > kEps && angleOnArc(angle2)) addAxisPoint(QPointF(0.0, c.y() - sq));
                    }
                }
                // Arc crosses X axis at y=0
                if (std::abs(c.y()) <= r + kEps) {
                    double disc = r * r - c.y() * c.y();
                    if (disc >= 0.0) {
                        double sq = std::sqrt(disc);
                        double angle1 = std::atan2(-c.y(), sq) * 180.0 / M_PI;
                        double angle2 = std::atan2(-c.y(), -sq) * 180.0 / M_PI;
                        if (angleOnArc(angle1)) addAxisPoint(QPointF(c.x() + sq, 0.0));
                        if (sq > kEps && angleOnArc(angle2)) addAxisPoint(QPointF(c.x() - sq, 0.0));
                    }
                }
            }
            break;

        case EntityType::Polygon:
            if (!entity.points.isEmpty()) {
                QVector<QPointF> verts = computePolygonVertices(entity);
                int n = verts.size();
                for (int i = 0; i < n; ++i) {
                    lineAxisCrossings(verts[i], verts[(i + 1) % n]);
                }
            }
            break;

        case EntityType::Ellipse:
            if (!entity.points.isEmpty()) {
                QPointF c = entity.points[0];
                double a = entity.majorRadius;
                double b = entity.minorRadius;

                // Ellipse: (x-cx)²/a² + (y-cy)²/b² = 1
                // Crosses Y axis (x=0): y = cy ± b*sqrt(1 - cx²/a²)
                if (a > kEps && std::abs(c.x()) <= a + kEps) {
                    double disc = 1.0 - (c.x() * c.x()) / (a * a);
                    if (disc >= 0.0) {
                        double sq = b * std::sqrt(disc);
                        addAxisPoint(QPointF(0.0, c.y() + sq));
                        if (sq > kEps) {
                            addAxisPoint(QPointF(0.0, c.y() - sq));
                        }
                    }
                }
                // Crosses X axis (y=0): x = cx ± a*sqrt(1 - cy²/b²)
                if (b > kEps && std::abs(c.y()) <= b + kEps) {
                    double disc = 1.0 - (c.y() * c.y()) / (b * b);
                    if (disc >= 0.0) {
                        double sq = a * std::sqrt(disc);
                        addAxisPoint(QPointF(c.x() + sq, 0.0));
                        if (sq > kEps) {
                            addAxisPoint(QPointF(c.x() - sq, 0.0));
                        }
                    }
                }
            }
            break;

        default:
            // Slots, splines — can be extended later
            break;
        }
    }

    return points;
}

// =====================================================================
//  collectIntersectionSnapPoints
// =====================================================================

QVector<SnapPoint> collectIntersectionSnapPoints(
    const QVector<Entity>& entities,
    int excludeEntityId)
{
    QVector<SnapPoint> points;

    // Compute intersections between all pairs of entities
    for (int i = 0; i < entities.size(); ++i) {
        const Entity& e1 = entities[i];
        if (e1.id == excludeEntityId) continue;

        for (int j = i + 1; j < entities.size(); ++j) {
            const Entity& e2 = entities[j];
            if (e2.id == excludeEntityId) continue;

            // Get intersection points between e1 and e2
            QVector<QPointF> intersections = computeEntityIntersectionPoints(e1, e2);
            for (const QPointF& pt : intersections) {
                // Use first entity's id for the snap point
                points.append({pt, SnapType::Intersection, e1.id});
            }
        }
    }

    // Compute intersections of entities with the X and Y axes
    points.append(collectAxisCrossingSnapPoints(entities, excludeEntityId));

    return points;
}

// =====================================================================
//  collectAllSnapPoints
// =====================================================================

QVector<SnapPoint> collectAllSnapPoints(
    const QVector<Entity>& entities,
    int excludeEntityId)
{
    QVector<SnapPoint> points;

    for (const Entity& entity : entities) {
        if (entity.id == excludeEntityId) continue;
        points.append(collectSnapPoints(entity));
    }

    // Collect intersection points between all entity pairs
    points.append(collectIntersectionSnapPoints(entities, excludeEntityId));

    return points;
}

// =====================================================================
//  findNearestOnPerimeter
// =====================================================================

SnapPoint findNearestOnPerimeter(
    const QVector<Entity>& entities,
    const QPointF& point,
    double tolerance,
    int excludeEntityId)
{
    SnapPoint result;
    result.type = SnapType::Endpoint;  // Will be set to Nearest if found
    result.entityId = -1;
    double bestDist = tolerance;

    for (const Entity& entity : entities) {
        if (entity.id == excludeEntityId) continue;

        QPointF nearest;
        double dist = tolerance + 1;  // Initialize beyond tolerance

        switch (entity.type) {
        case EntityType::Line:
            if (entity.points.size() >= 2) {
                nearest = geometry::closestPointOnLine(point, entity.points[0], entity.points[1]);
                dist = QLineF(point, nearest).length();
            }
            break;

        case EntityType::Circle:
            if (!entity.points.isEmpty() && entity.radius > 0) {
                nearest = geometry::closestPointOnCircle(point, entity.points[0], entity.radius);
                dist = QLineF(point, nearest).length();
            }
            break;

        case EntityType::Arc:
            if (!entity.points.isEmpty() && entity.radius > 0) {
                geometry::Arc arc;
                arc.center = entity.points[0];
                arc.radius = entity.radius;
                arc.startAngle = entity.startAngle;
                arc.sweepAngle = entity.sweepAngle;
                nearest = geometry::closestPointOnArc(point, arc);
                dist = QLineF(point, nearest).length();
            }
            break;

        case EntityType::Rectangle:
        case EntityType::Parallelogram:
            if (entity.points.size() >= 4) {
                // Check each edge
                for (int edge = 0; edge < 4; ++edge) {
                    QPointF p1 = entity.points[edge];
                    QPointF p2 = entity.points[(edge + 1) % 4];
                    QPointF edgeNearest = geometry::closestPointOnLine(point, p1, p2);
                    double edgeDist = QLineF(point, edgeNearest).length();
                    if (edgeDist < dist) {
                        dist = edgeDist;
                        nearest = edgeNearest;
                    }
                }
            }
            break;

        case EntityType::Polygon:
            if (!entity.points.isEmpty()) {
                QVector<QPointF> verts = computePolygonVertices(entity);
                int n = verts.size();
                for (int i = 0; i < n; ++i) {
                    QPointF edgeNearest = geometry::closestPointOnLine(
                        point, verts[i], verts[(i + 1) % n]);
                    double edgeDist = QLineF(point, edgeNearest).length();
                    if (edgeDist < dist) {
                        dist = edgeDist;
                        nearest = edgeNearest;
                    }
                }
            }
            break;

        case EntityType::Slot:
            // Use Entity::closestPoint() for slot perimeter (arcs + lines)
            nearest = entity.closestPoint(point);
            dist = QLineF(point, nearest).length();
            break;

        default:
            // Ellipses, splines — can be extended later
            break;
        }

        if (dist < bestDist) {
            bestDist = dist;
            result.position = nearest;
            result.type = SnapType::Nearest;
            result.entityId = entity.id;
        }
    }

    return result;
}

// =====================================================================
//  findBestSnap
// =====================================================================

SnapResult findBestSnap(
    const QVector<Entity>& entities,
    const QPointF& worldPos,
    double worldTolerance,
    int excludeEntityId)
{
    SnapResult snapResult;
    double bestWeightedDist = worldTolerance;

    // Helper: consider a snap candidate
    auto consider = [&](const SnapPoint& sp, double rawDist) {
        // Must be within raw tolerance to be valid at all
        if (rawDist > worldTolerance) return;
        double wd = rawDist / defaultSnapWeight(sp.type);
        if (wd < bestWeightedDist) {
            bestWeightedDist = wd;
            snapResult.found = true;
            snapResult.snap = sp;
        }
    };

    // Check origin snap
    double originDist = QLineF(worldPos, QPointF(0, 0)).length();
    consider(SnapPoint{QPointF(0, 0), SnapType::Origin, -1}, originDist);

    // Check explicit entity snap points (includes axis-crossing intersections)
    QVector<SnapPoint> snapPoints = collectAllSnapPoints(entities, excludeEntityId);
    for (const SnapPoint& sp : snapPoints) {
        double dist = QLineF(worldPos, sp.position).length();
        consider(sp, dist);
    }

    // Check nearest point on entity perimeter
    SnapPoint nearestSnap = findNearestOnPerimeter(entities, worldPos, worldTolerance, excludeEntityId);
    if (nearestSnap.type == SnapType::Nearest) {
        double dist = QLineF(worldPos, nearestSnap.position).length();
        consider(nearestSnap, dist);
    }

    // Check axis snaps (Y=0 for X axis, X=0 for Y axis)
    if (std::abs(worldPos.y()) < worldTolerance) {
        double dist = std::abs(worldPos.y());
        QPointF pos(worldPos.x(), 0);
        consider(SnapPoint{pos, SnapType::AxisX, -1}, dist);
    }
    if (std::abs(worldPos.x()) < worldTolerance) {
        double dist = std::abs(worldPos.x());
        QPointF pos(0, worldPos.y());
        consider(SnapPoint{pos, SnapType::AxisY, -1}, dist);
    }

    return snapResult;
}

}  // namespace sketch
}  // namespace hobbycad
