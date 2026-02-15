// =====================================================================
//  src/libhobbycad/sketch/profiles.cpp — Profile detection implementation
// =====================================================================
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include <hobbycad/sketch/profiles.h>
#include <hobbycad/geometry/utils.h>

#include <QMap>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace hobbycad {
namespace sketch {

using namespace geometry;

// =====================================================================
//  Helper Functions
// =====================================================================

namespace {

/// Find entity by ID in a vector
const Entity* findEntityById(const QVector<Entity>& entities, int id)
{
    for (const Entity& e : entities) {
        if (e.id == id) {
            return &e;
        }
    }
    return nullptr;
}

/// Get endpoints of an entity (returns 0, 1, or 2 points)
QVector<QPointF> getEndpoints(const Entity& entity)
{
    return entity.endpoints();
}

/// Discretize an entity into a series of points
QVector<QPointF> discretizeEntity(const Entity& entity, int segments)
{
    QVector<QPointF> points;

    switch (entity.type) {
    case EntityType::Point:
        if (!entity.points.isEmpty()) {
            points.append(entity.points[0]);
        }
        break;

    case EntityType::Line:
        if (entity.points.size() >= 2) {
            points.append(entity.points[0]);
            points.append(entity.points[1]);
        }
        break;

    case EntityType::Rectangle:
        if (entity.points.size() >= 2) {
            QPointF p1 = entity.points[0];
            QPointF p2 = entity.points[1];
            points.append(p1);
            points.append(QPointF(p2.x(), p1.y()));
            points.append(p2);
            points.append(QPointF(p1.x(), p2.y()));
        }
        break;

    case EntityType::Circle:
        if (!entity.points.isEmpty()) {
            for (int i = 0; i <= segments; ++i) {
                double angle = 2.0 * M_PI * i / segments;
                double x = entity.points[0].x() + entity.radius * qCos(angle);
                double y = entity.points[0].y() + entity.radius * qSin(angle);
                points.append(QPointF(x, y));
            }
        }
        break;

    case EntityType::Arc:
        if (!entity.points.isEmpty()) {
            double startRad = qDegreesToRadians(entity.startAngle);
            double sweepRad = qDegreesToRadians(entity.sweepAngle);
            for (int i = 0; i <= segments; ++i) {
                double t = static_cast<double>(i) / segments;
                double angle = startRad + t * sweepRad;
                double x = entity.points[0].x() + entity.radius * qCos(angle);
                double y = entity.points[0].y() + entity.radius * qSin(angle);
                points.append(QPointF(x, y));
            }
        }
        break;

    case EntityType::Polygon:
        points = entity.points;
        if (!points.isEmpty() && points.first() != points.last()) {
            points.append(points.first());
        }
        break;

    case EntityType::Spline:
        // For splines, use the control points as approximation
        // A proper implementation would evaluate the spline
        points = entity.points;
        break;

    case EntityType::Ellipse:
        if (!entity.points.isEmpty()) {
            for (int i = 0; i <= segments; ++i) {
                double angle = 2.0 * M_PI * i / segments;
                double x = entity.points[0].x() + entity.majorRadius * qCos(angle);
                double y = entity.points[0].y() + entity.minorRadius * qSin(angle);
                points.append(QPointF(x, y));
            }
        }
        break;

    case EntityType::Slot:
        // Slot is two semicircles connected by lines
        if (entity.points.size() >= 2) {
            QPointF p1 = entity.points[0];
            QPointF p2 = entity.points[1];
            QPointF dir = p2 - p1;
            double len = length(dir);
            if (len > DEFAULT_TOLERANCE) {
                dir = dir / len;
                QPointF perp(-dir.y(), dir.x());

                // First semicircle around p1
                double baseAngle = qRadiansToDegrees(qAtan2(perp.y(), perp.x()));
                for (int i = 0; i <= segments / 2; ++i) {
                    double t = static_cast<double>(i) / (segments / 2);
                    double angle = qDegreesToRadians(baseAngle + 180 * t);
                    double x = p1.x() + entity.radius * qCos(angle);
                    double y = p1.y() + entity.radius * qSin(angle);
                    points.append(QPointF(x, y));
                }

                // Second semicircle around p2
                for (int i = 0; i <= segments / 2; ++i) {
                    double t = static_cast<double>(i) / (segments / 2);
                    double angle = qDegreesToRadians(baseAngle + 180 + 180 * t);
                    double x = p2.x() + entity.radius * qCos(angle);
                    double y = p2.y() + entity.radius * qSin(angle);
                    points.append(QPointF(x, y));
                }
            }
        }
        break;

    case EntityType::Text:
        // Text doesn't contribute to profiles
        break;
    }

    return points;
}

/// Check if two points are the same within tolerance
bool pointsEqual(const QPointF& a, const QPointF& b, double tolerance)
{
    return QLineF(a, b).length() < tolerance;
}

/// DFS to find cycles in the connectivity graph
void findCyclesDFS(
    const ConnectivityGraph& graph,
    int currentNode,
    int startNode,
    QVector<int>& currentPath,
    QSet<QPair<int, int>>& usedEdges,
    QVector<QVector<int>>& cycles,
    int maxCycles,
    int depth)
{
    if (cycles.size() >= maxCycles || depth > 50) {
        return;  // Limit search
    }

    const QVector<int>& adjacentEdges = graph.adjacency[currentNode];

    for (int edgeIdx : adjacentEdges) {
        const ConnectivityEdge& edge = graph.edges[edgeIdx];

        // Determine the other node
        int otherNode = (edge.startNode == currentNode) ? edge.endNode : edge.startNode;

        // Check if this edge was already used in current path
        QPair<int, int> edgePair = qMakePair(qMin(currentNode, otherNode),
                                              qMax(currentNode, otherNode));
        if (usedEdges.contains(edgePair)) {
            continue;
        }

        if (otherNode == startNode && currentPath.size() >= 2) {
            // Found a cycle
            QVector<int> cycle = currentPath;
            cycle.append(startNode);
            cycles.append(cycle);
            continue;
        }

        // Check if we've already visited this node in current path
        if (currentPath.contains(otherNode)) {
            continue;
        }

        // Continue DFS
        currentPath.append(otherNode);
        usedEdges.insert(edgePair);

        findCyclesDFS(graph, otherNode, startNode, currentPath, usedEdges,
                      cycles, maxCycles, depth + 1);

        currentPath.removeLast();
        usedEdges.remove(edgePair);
    }
}

}  // anonymous namespace

// =====================================================================
//  Profile Methods
// =====================================================================

bool Profile::contains(const Profile& other) const
{
    // Check if any point of other is inside this profile
    if (other.polygon.isEmpty()) {
        return false;
    }

    // First check bounding box
    if (!bounds.valid || !other.bounds.valid) {
        return false;
    }

    if (other.bounds.minX < bounds.minX || other.bounds.maxX > bounds.maxX ||
        other.bounds.minY < bounds.minY || other.bounds.maxY > bounds.maxY) {
        return false;
    }

    // Check if centroid of other is inside this polygon
    QPointF centroid = other.polygon.boundingRect().center();
    return polygon.containsPoint(centroid, Qt::OddEvenFill);
}

bool Profile::containsPoint(const QPointF& point) const
{
    return polygon.containsPoint(point, Qt::OddEvenFill);
}

// =====================================================================
//  Connectivity Graph
// =====================================================================

ConnectivityGraph buildConnectivityGraph(
    const QVector<Entity>& entities,
    double tolerance)
{
    ConnectivityGraph graph;

    // Map from position hash to node index
    QMap<QString, int> positionToNode;

    auto positionKey = [tolerance](const QPointF& p) {
        // Round to tolerance to group nearby points
        int xi = static_cast<int>(p.x() / tolerance + 0.5);
        int yi = static_cast<int>(p.y() / tolerance + 0.5);
        return QString("%1,%2").arg(xi).arg(yi);
    };

    auto getOrCreateNode = [&](const QPointF& pos, int entityId, int pointIdx) -> int {
        QString key = positionKey(pos);

        if (positionToNode.contains(key)) {
            return positionToNode[key];
        }

        int nodeIdx = graph.nodes.size();
        ConnectivityNode node;
        node.entityId = entityId;
        node.pointIndex = pointIdx;
        node.position = pos;
        graph.nodes.append(node);
        positionToNode[key] = nodeIdx;
        return nodeIdx;
    };

    // Build nodes and edges
    for (const Entity& entity : entities) {
        // Skip construction geometry
        if (entity.isConstruction) {
            continue;
        }

        QVector<QPointF> endpoints = getEndpoints(entity);

        if (endpoints.size() == 2) {
            // Entity with two endpoints (line, arc, etc.)
            int startNode = getOrCreateNode(endpoints[0], entity.id, 0);
            int endNode = getOrCreateNode(endpoints[1], entity.id, 1);

            ConnectivityEdge edge;
            edge.entityId = entity.id;
            edge.startNode = startNode;
            edge.endNode = endNode;
            edge.length = QLineF(endpoints[0], endpoints[1]).length();
            edge.isConstruction = entity.isConstruction;

            graph.edges.append(edge);
        } else if (endpoints.size() == 1) {
            // Point entity - just create node
            getOrCreateNode(endpoints[0], entity.id, 0);
        }
        // Closed shapes (circle, ellipse, polygon) create their own single node
        // that connects to itself - handled specially in profile detection
    }

    // Build adjacency list
    graph.adjacency.resize(graph.nodes.size());
    for (int i = 0; i < graph.edges.size(); ++i) {
        const ConnectivityEdge& edge = graph.edges[i];
        graph.adjacency[edge.startNode].append(i);
        if (edge.startNode != edge.endNode) {
            graph.adjacency[edge.endNode].append(i);
        }
    }

    return graph;
}

QVector<QVector<int>> findCycles(
    const ConnectivityGraph& graph,
    int maxCycles)
{
    QVector<QVector<int>> cycles;

    if (graph.nodes.isEmpty()) {
        return cycles;
    }

    // Try starting from each node
    for (int startNode = 0; startNode < graph.nodes.size() && cycles.size() < maxCycles; ++startNode) {
        QVector<int> currentPath;
        currentPath.append(startNode);
        QSet<QPair<int, int>> usedEdges;

        findCyclesDFS(graph, startNode, startNode, currentPath, usedEdges,
                      cycles, maxCycles, 0);
    }

    // Remove duplicate cycles (same cycle starting at different points)
    QVector<QVector<int>> uniqueCycles;
    QSet<QString> seenCycles;

    for (const QVector<int>& cycle : cycles) {
        if (cycle.size() < 3) continue;

        // Create canonical representation (start from minimum node, in sorted direction)
        QVector<int> normalized = cycle;
        normalized.removeLast();  // Remove duplicate end

        // Find minimum element
        int minIdx = 0;
        for (int i = 1; i < normalized.size(); ++i) {
            if (normalized[i] < normalized[minIdx]) {
                minIdx = i;
            }
        }

        // Rotate to start from minimum
        QVector<int> rotated;
        for (int i = 0; i < normalized.size(); ++i) {
            rotated.append(normalized[(minIdx + i) % normalized.size()]);
        }

        // Check direction and reverse if needed for canonical form
        if (rotated.size() >= 2 && rotated[1] > rotated.last()) {
            QVector<int> reversed;
            reversed.append(rotated[0]);
            for (int i = rotated.size() - 1; i >= 1; --i) {
                reversed.append(rotated[i]);
            }
            rotated = reversed;
        }

        // Create key
        QString key;
        for (int n : rotated) {
            key += QString::number(n) + ",";
        }

        if (!seenCycles.contains(key)) {
            seenCycles.insert(key);
            rotated.append(rotated.first());  // Re-add closing node
            uniqueCycles.append(rotated);
        }
    }

    return uniqueCycles;
}

// =====================================================================
//  Profile Detection
// =====================================================================

QVector<Profile> detectProfiles(
    const QVector<Entity>& entities,
    const ProfileDetectionOptions& options)
{
    QVector<Profile> profiles;

    // Filter entities
    QVector<Entity> filteredEntities;
    for (const Entity& e : entities) {
        if (options.excludeConstruction && e.isConstruction) {
            continue;
        }
        filteredEntities.append(e);
    }

    // Handle closed entities (circles, ellipses, closed polygons) as profiles
    int profileId = 1;
    for (const Entity& entity : filteredEntities) {
        bool isClosed = false;

        switch (entity.type) {
        case EntityType::Circle:
        case EntityType::Ellipse:
            isClosed = true;
            break;

        case EntityType::Polygon:
            // Check if polygon is closed
            if (entity.points.size() >= 3) {
                isClosed = pointsEqual(entity.points.first(), entity.points.last(),
                                       options.tolerance);
            }
            break;

        case EntityType::Rectangle:
            isClosed = true;
            break;

        default:
            break;
        }

        if (isClosed) {
            Profile profile;
            profile.id = profileId++;
            profile.entityIds.append(entity.id);
            profile.reversed.append(false);
            profile.polygon = QPolygonF(discretizeEntity(entity, options.polygonSegments));
            profile.area = polygonArea(profile.polygon.toList().toVector());
            profile.isOuter = true;
            profile.bounds = entity.boundingBox();

            profiles.append(profile);

            if (profiles.size() >= options.maxProfiles) {
                return profiles;
            }
        }
    }

    // Build connectivity graph for open entities
    ConnectivityGraph graph = buildConnectivityGraph(filteredEntities, options.tolerance);

    // Find cycles
    QVector<QVector<int>> cycles = findCycles(graph, options.maxProfiles - profiles.size());

    // Convert cycles to profiles
    for (const QVector<int>& cycle : cycles) {
        if (profiles.size() >= options.maxProfiles) {
            break;
        }

        Profile profile;
        profile.id = profileId++;

        // Convert node cycle to entity IDs
        QVector<QPointF> polygonPoints;

        for (int i = 0; i < cycle.size() - 1; ++i) {
            int nodeA = cycle[i];
            int nodeB = cycle[i + 1];

            // Find edge between these nodes
            for (int edgeIdx : graph.adjacency[nodeA]) {
                const ConnectivityEdge& edge = graph.edges[edgeIdx];
                if ((edge.startNode == nodeA && edge.endNode == nodeB) ||
                    (edge.startNode == nodeB && edge.endNode == nodeA)) {

                    profile.entityIds.append(edge.entityId);
                    bool reversed = (edge.endNode == nodeA);
                    profile.reversed.append(reversed);

                    // Add discretized points
                    const Entity* entity = findEntityById(filteredEntities, edge.entityId);
                    if (entity) {
                        QVector<QPointF> pts = discretizeEntity(*entity, options.polygonSegments);
                        if (reversed) {
                            std::reverse(pts.begin(), pts.end());
                        }
                        for (const QPointF& p : pts) {
                            if (polygonPoints.isEmpty() ||
                                !pointsEqual(polygonPoints.last(), p, options.tolerance)) {
                                polygonPoints.append(p);
                            }
                        }
                    }
                    break;
                }
            }
        }

        if (!polygonPoints.isEmpty()) {
            profile.polygon = QPolygonF(polygonPoints);
            profile.area = polygonArea(polygonPoints);
            profile.isOuter = true;

            // Calculate bounds
            for (const QPointF& p : polygonPoints) {
                profile.bounds.include(p);
            }

            profiles.append(profile);
        }
    }

    return profiles;
}

QVector<Profile> detectProfilesWithHoles(
    const QVector<Entity>& entities,
    const ProfileDetectionOptions& options)
{
    QVector<Profile> profiles = detectProfiles(entities, options);

    if (profiles.size() < 2) {
        return profiles;
    }

    // Sort profiles by area (largest first)
    std::sort(profiles.begin(), profiles.end(), [](const Profile& a, const Profile& b) {
        return qAbs(a.area) > qAbs(b.area);
    });

    // Determine containment
    for (int i = 0; i < profiles.size(); ++i) {
        for (int j = i + 1; j < profiles.size(); ++j) {
            if (profiles[i].contains(profiles[j])) {
                // j is inside i
                // If i is outer, j is inner. If i is inner, j is outer again.
                profiles[j].isOuter = !profiles[i].isOuter;
            }
        }
    }

    return profiles;
}

// =====================================================================
//  Profile Utilities
// =====================================================================

QPolygonF profileToPolygon(
    const Profile& profile,
    const QVector<Entity>& entities,
    int segments)
{
    QVector<QPointF> points;

    for (int i = 0; i < profile.entityIds.size(); ++i) {
        int entityId = profile.entityIds[i];
        bool reversed = profile.reversed.value(i, false);

        const Entity* entity = findEntityById(entities, entityId);
        if (!entity) continue;

        QVector<QPointF> entityPoints = discretizeEntity(*entity, segments);
        if (reversed) {
            std::reverse(entityPoints.begin(), entityPoints.end());
        }

        for (const QPointF& p : entityPoints) {
            if (points.isEmpty() ||
                !pointsEqual(points.last(), p, POINT_TOLERANCE)) {
                points.append(p);
            }
        }
    }

    return QPolygonF(points);
}

double profileArea(
    const Profile& profile,
    const QVector<Entity>& entities)
{
    QPolygonF polygon = profileToPolygon(profile, entities, 32);
    return polygonArea(polygon.toList().toVector());
}

bool profilesShareEdge(const Profile& p1, const Profile& p2)
{
    for (int id1 : p1.entityIds) {
        if (p2.entityIds.contains(id1)) {
            return true;
        }
    }
    return false;
}

QPointF profileCentroid(
    const Profile& profile,
    const QVector<Entity>& entities)
{
    QPolygonF polygon = profileToPolygon(profile, entities, 32);
    return polygonCentroid(polygon.toList().toVector());
}

bool profileIsCCW(const Profile& profile)
{
    return profile.area > 0;
}

Profile reverseProfile(const Profile& profile)
{
    Profile reversed = profile;

    // Reverse entity order
    std::reverse(reversed.entityIds.begin(), reversed.entityIds.end());

    // Flip all reversed flags
    for (bool& r : reversed.reversed) {
        r = !r;
    }
    std::reverse(reversed.reversed.begin(), reversed.reversed.end());

    // Reverse polygon
    QVector<QPointF> points = reversed.polygon.toList().toVector();
    std::reverse(points.begin(), points.end());
    reversed.polygon = QPolygonF(points);

    // Negate area
    reversed.area = -reversed.area;

    return reversed;
}

}  // namespace sketch
}  // namespace hobbycad
