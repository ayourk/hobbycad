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

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <unordered_set>

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

/// Find entity by ID in a vector
const Entity* findEntityById(const std::vector<Entity>& entities, int id)
{
    for (const Entity& e : entities) {
        if (e.id == id) {
            return &e;
        }
    }
    return nullptr;
}

/// Get endpoints of an entity (returns 0, 1, or 2 points)
std::vector<Point2D> getEndpoints(const Entity& entity)
{
    return entity.endpoints();
}

/// Discretize an entity into a series of points
std::vector<Point2D> discretizeEntity(const Entity& entity, int segments)
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

    case EntityType::Rectangle:
        if (entity.points.size() >= 2) {
            Point2D p1 = entity.points[0];
            Point2D p2 = entity.points[1];
            points.push_back(p1);
            points.push_back(Point2D(p2.x, p1.y));
            points.push_back(p2);
            points.push_back(Point2D(p1.x, p2.y));
        }
        break;

    case EntityType::Circle:
        if (!entity.points.empty()) {
            for (int i = 0; i <= segments; ++i) {
                double angle = 2.0 * M_PI * i / segments;
                double x = entity.points[0].x + entity.radius * std::cos(angle);
                double y = entity.points[0].y + entity.radius * std::sin(angle);
                points.push_back(Point2D(x, y));
            }
        }
        break;

    case EntityType::Arc:
        if (!entity.points.empty()) {
            double startRad = entity.startAngle * M_PI / 180.0;
            double sweepRad = entity.sweepAngle * M_PI / 180.0;
            for (int i = 0; i <= segments; ++i) {
                double t = static_cast<double>(i) / segments;
                double angle = startRad + t * sweepRad;
                double x = entity.points[0].x + entity.radius * std::cos(angle);
                double y = entity.points[0].y + entity.radius * std::sin(angle);
                points.push_back(Point2D(x, y));
            }
        }
        break;

    case EntityType::Polygon:
        points = entity.points;
        if (!points.empty() && !(points.front() == points.back())) {
            points.push_back(points.front());
        }
        break;

    case EntityType::Spline:
        // For splines, use the control points as approximation
        // A proper implementation would evaluate the spline
        points = entity.points;
        break;

    case EntityType::Ellipse:
        if (!entity.points.empty()) {
            for (int i = 0; i <= segments; ++i) {
                double angle = 2.0 * M_PI * i / segments;
                double x = entity.points[0].x + entity.majorRadius * std::cos(angle);
                double y = entity.points[0].y + entity.minorRadius * std::sin(angle);
                points.push_back(Point2D(x, y));
            }
        }
        break;

    case EntityType::Slot:
        // Slot is two semicircles connected by lines
        if (entity.points.size() >= 2) {
            Point2D p1 = entity.points[0];
            Point2D p2 = entity.points[1];
            Point2D dir = p2 - p1;
            double len = length(dir);
            if (len > DEFAULT_TOLERANCE) {
                dir = dir / len;
                Point2D perp(-dir.y, dir.x);

                // First semicircle around p1
                double baseAngle = std::atan2(perp.y, perp.x) * 180.0 / M_PI;
                for (int i = 0; i <= segments / 2; ++i) {
                    double t = static_cast<double>(i) / (segments / 2);
                    double angle = (baseAngle + 180 * t) * M_PI / 180.0;
                    double x = p1.x + entity.radius * std::cos(angle);
                    double y = p1.y + entity.radius * std::sin(angle);
                    points.push_back(Point2D(x, y));
                }

                // Second semicircle around p2
                for (int i = 0; i <= segments / 2; ++i) {
                    double t = static_cast<double>(i) / (segments / 2);
                    double angle = (baseAngle + 180 + 180 * t) * M_PI / 180.0;
                    double x = p2.x + entity.radius * std::cos(angle);
                    double y = p2.y + entity.radius * std::sin(angle);
                    points.push_back(Point2D(x, y));
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
bool pointsEqual(const Point2D& a, const Point2D& b, double tolerance)
{
    return std::hypot(b.x - a.x, b.y - a.y) < tolerance;
}

/// DFS to find cycles in the connectivity graph
void findCyclesDFS(
    const ConnectivityGraph& graph,
    int currentNode,
    int startNode,
    std::vector<int>& currentPath,
    std::set<std::pair<int, int>>& usedEdges,
    std::vector<std::vector<int>>& cycles,
    int maxCycles,
    int depth)
{
    if (static_cast<int>(cycles.size()) >= maxCycles || depth > 50) {
        return;  // Limit search
    }

    const std::vector<int>& adjacentEdges = graph.adjacency[currentNode];

    for (int edgeIdx : adjacentEdges) {
        const ConnectivityEdge& edge = graph.edges[edgeIdx];

        // Determine the other node
        int otherNode = (edge.startNode == currentNode) ? edge.endNode : edge.startNode;

        // Check if this edge was already used in current path
        std::pair<int, int> edgePair = {std::min(currentNode, otherNode),
                                         std::max(currentNode, otherNode)};
        if (usedEdges.count(edgePair) > 0) {
            continue;
        }

        if (otherNode == startNode && currentPath.size() >= 2) {
            // Found a cycle
            std::vector<int> cycle = currentPath;
            cycle.push_back(startNode);
            cycles.push_back(cycle);
            continue;
        }

        // Check if we've already visited this node in current path
        if (hobbycad::contains(currentPath, otherNode)) {
            continue;
        }

        // Continue DFS
        currentPath.push_back(otherNode);
        usedEdges.insert(edgePair);

        findCyclesDFS(graph, otherNode, startNode, currentPath, usedEdges,
                      cycles, maxCycles, depth + 1);

        currentPath.pop_back();
        usedEdges.erase(edgePair);
    }
}

}  // anonymous namespace

// =====================================================================
//  Profile Methods
// =====================================================================

bool Profile::contains(const Profile& other) const
{
    // Check if any point of other is inside this profile
    if (other.polygon.empty()) {
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

    // Check if centroid of other is inside this polygon using ray casting
    Point2D centroid = geometry::polygonCentroid(other.polygon);
    return geometry::pointInPolygon(centroid, polygon);
}

bool Profile::containsPoint(const Point2D& point) const
{
    return geometry::pointInPolygon(point, polygon);
}

// =====================================================================
//  Connectivity Graph
// =====================================================================

ConnectivityGraph buildConnectivityGraph(
    const std::vector<Entity>& entities,
    double tolerance)
{
    ConnectivityGraph graph;

    // Map from position hash to node index
    std::map<std::string, int> positionToNode;

    auto positionKey = [tolerance](const Point2D& p) {
        // Round to tolerance to group nearby points
        int xi = static_cast<int>(p.x / tolerance + 0.5);
        int yi = static_cast<int>(p.y / tolerance + 0.5);
        return std::to_string(xi) + "," + std::to_string(yi);
    };

    auto getOrCreateNode = [&](const Point2D& pos, int entityId, int pointIdx) -> int {
        std::string key = positionKey(pos);

        auto it = positionToNode.find(key);
        if (it != positionToNode.end()) {
            return it->second;
        }

        int nodeIdx = graph.nodes.size();
        ConnectivityNode node;
        node.entityId = entityId;
        node.pointIndex = pointIdx;
        node.position = pos;
        graph.nodes.push_back(node);
        positionToNode[key] = nodeIdx;
        return nodeIdx;
    };

    // Build nodes and edges
    for (const Entity& entity : entities) {
        // Skip construction geometry
        if (entity.isConstruction) {
            continue;
        }

        std::vector<Point2D> endpoints = getEndpoints(entity);

        if (endpoints.size() == 2) {
            // Entity with two endpoints (line, arc, etc.)
            int startNode = getOrCreateNode(endpoints[0], entity.id, 0);
            int endNode = getOrCreateNode(endpoints[1], entity.id, 1);

            ConnectivityEdge edge;
            edge.entityId = entity.id;
            edge.startNode = startNode;
            edge.endNode = endNode;
            edge.length = std::hypot(endpoints[1].x - endpoints[0].x,
                                     endpoints[1].y - endpoints[0].y);
            edge.isConstruction = entity.isConstruction;

            graph.edges.push_back(edge);
        } else if (endpoints.size() == 1) {
            // Point entity - just create node
            getOrCreateNode(endpoints[0], entity.id, 0);
        }
        // Closed shapes (circle, ellipse, polygon) create their own single node
        // that connects to itself - handled specially in profile detection
    }

    // Build adjacency list
    graph.adjacency.resize(graph.nodes.size());
    for (int i = 0; i < static_cast<int>(graph.edges.size()); ++i) {
        const ConnectivityEdge& edge = graph.edges[i];
        graph.adjacency[edge.startNode].push_back(i);
        if (edge.startNode != edge.endNode) {
            graph.adjacency[edge.endNode].push_back(i);
        }
    }

    return graph;
}

std::vector<std::vector<int>> findCycles(
    const ConnectivityGraph& graph,
    int maxCycles)
{
    std::vector<std::vector<int>> cycles;

    if (graph.nodes.empty()) {
        return cycles;
    }

    // Try starting from each node
    for (int startNode = 0; startNode < static_cast<int>(graph.nodes.size()) && static_cast<int>(cycles.size()) < maxCycles; ++startNode) {
        std::vector<int> currentPath;
        currentPath.push_back(startNode);
        std::set<std::pair<int, int>> usedEdges;

        findCyclesDFS(graph, startNode, startNode, currentPath, usedEdges,
                      cycles, maxCycles, 0);
    }

    // Remove duplicate cycles (same cycle starting at different points)
    std::vector<std::vector<int>> uniqueCycles;
    std::set<std::string> seenCycles;

    for (const std::vector<int>& cycle : cycles) {
        if (cycle.size() < 3) continue;

        // Create canonical representation (start from minimum node, in sorted direction)
        std::vector<int> normalized = cycle;
        normalized.pop_back();  // Remove duplicate end

        // Find minimum element
        int minIdx = 0;
        for (int i = 1; i < static_cast<int>(normalized.size()); ++i) {
            if (normalized[i] < normalized[minIdx]) {
                minIdx = i;
            }
        }

        // Rotate to start from minimum
        std::vector<int> rotated;
        for (int i = 0; i < static_cast<int>(normalized.size()); ++i) {
            rotated.push_back(normalized[(minIdx + i) % normalized.size()]);
        }

        // Check direction and reverse if needed for canonical form
        if (rotated.size() >= 2 && rotated[1] > rotated.back()) {
            std::vector<int> reversed;
            reversed.push_back(rotated[0]);
            for (int i = static_cast<int>(rotated.size()) - 1; i >= 1; --i) {
                reversed.push_back(rotated[i]);
            }
            rotated = reversed;
        }

        // Create key
        std::string key;
        for (int n : rotated) {
            key += std::to_string(n) + ",";
        }

        if (seenCycles.count(key) == 0) {
            seenCycles.insert(key);
            rotated.push_back(rotated.front());  // Re-add closing node
            uniqueCycles.push_back(rotated);
        }
    }

    return uniqueCycles;
}

// =====================================================================
//  Profile Detection
// =====================================================================

std::vector<Profile> detectProfiles(
    const std::vector<Entity>& entities,
    const ProfileDetectionOptions& options)
{
    std::vector<Profile> profiles;

    // Filter entities
    std::vector<Entity> filteredEntities;
    for (const Entity& e : entities) {
        if (options.excludeConstruction && e.isConstruction) {
            continue;
        }
        filteredEntities.push_back(e);
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
                isClosed = pointsEqual(entity.points.front(), entity.points.back(),
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
            profile.entityIds.push_back(entity.id);
            profile.reversed.push_back(false);
            profile.polygon = discretizeEntity(entity, options.polygonSegments);
            profile.area = polygonArea(profile.polygon);
            profile.isOuter = true;
            profile.bounds = entity.boundingBox();

            profiles.push_back(profile);

            if (static_cast<int>(profiles.size()) >= options.maxProfiles) {
                return profiles;
            }
        }
    }

    // Build connectivity graph for open entities
    ConnectivityGraph graph = buildConnectivityGraph(filteredEntities, options.tolerance);

    // Find cycles
    std::vector<std::vector<int>> cycles = findCycles(graph, options.maxProfiles - profiles.size());

    // Convert cycles to profiles
    for (const std::vector<int>& cycle : cycles) {
        if (static_cast<int>(profiles.size()) >= options.maxProfiles) {
            break;
        }

        Profile profile;
        profile.id = profileId++;

        // Convert node cycle to entity IDs
        std::vector<Point2D> polygonPoints;

        for (int i = 0; i < static_cast<int>(cycle.size()) - 1; ++i) {
            int nodeA = cycle[i];
            int nodeB = cycle[i + 1];

            // Find edge between these nodes
            for (int edgeIdx : graph.adjacency[nodeA]) {
                const ConnectivityEdge& edge = graph.edges[edgeIdx];
                if ((edge.startNode == nodeA && edge.endNode == nodeB) ||
                    (edge.startNode == nodeB && edge.endNode == nodeA)) {

                    profile.entityIds.push_back(edge.entityId);
                    bool reversed = (edge.endNode == nodeA);
                    profile.reversed.push_back(reversed);

                    // Add discretized points
                    const Entity* entity = findEntityById(filteredEntities, edge.entityId);
                    if (entity) {
                        std::vector<Point2D> pts = discretizeEntity(*entity, options.polygonSegments);
                        if (reversed) {
                            std::reverse(pts.begin(), pts.end());
                        }
                        for (const Point2D& p : pts) {
                            if (polygonPoints.empty() ||
                                !pointsEqual(polygonPoints.back(), p, options.tolerance)) {
                                polygonPoints.push_back(p);
                            }
                        }
                    }
                    break;
                }
            }
        }

        if (!polygonPoints.empty()) {
            profile.polygon = polygonPoints;
            profile.area = polygonArea(polygonPoints);
            profile.isOuter = true;

            // Calculate bounds
            for (const Point2D& p : polygonPoints) {
                profile.bounds.include(p);
            }

            profiles.push_back(profile);
        }
    }

    return profiles;
}

std::vector<Profile> detectProfilesWithHoles(
    const std::vector<Entity>& entities,
    const ProfileDetectionOptions& options)
{
    std::vector<Profile> profiles = detectProfiles(entities, options);

    if (profiles.size() < 2) {
        return profiles;
    }

    // Sort profiles by area (largest first)
    std::sort(profiles.begin(), profiles.end(), [](const Profile& a, const Profile& b) {
        return std::abs(a.area) > std::abs(b.area);
    });

    // Determine containment
    for (int i = 0; i < static_cast<int>(profiles.size()); ++i) {
        for (int j = i + 1; j < static_cast<int>(profiles.size()); ++j) {
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

std::vector<Point2D> profileToPolygon(
    const Profile& profile,
    const std::vector<Entity>& entities,
    int segments)
{
    std::vector<Point2D> points;

    for (int i = 0; i < static_cast<int>(profile.entityIds.size()); ++i) {
        int entityId = profile.entityIds[i];
        bool reversed = (i < static_cast<int>(profile.reversed.size())) ? profile.reversed[i] : false;

        const Entity* entity = findEntityById(entities, entityId);
        if (!entity) continue;

        std::vector<Point2D> entityPoints = discretizeEntity(*entity, segments);
        if (reversed) {
            std::reverse(entityPoints.begin(), entityPoints.end());
        }

        for (const Point2D& p : entityPoints) {
            if (points.empty() ||
                !pointsEqual(points.back(), p, POINT_TOLERANCE)) {
                points.push_back(p);
            }
        }
    }

    return points;
}

double profileArea(
    const Profile& profile,
    const std::vector<Entity>& entities)
{
    std::vector<Point2D> polygon = profileToPolygon(profile, entities, 32);
    return polygonArea(polygon);
}

bool profilesShareEdge(const Profile& p1, const Profile& p2)
{
    for (int id1 : p1.entityIds) {
        if (hobbycad::contains(p2.entityIds, id1)) {
            return true;
        }
    }
    return false;
}

Point2D profileCentroid(
    const Profile& profile,
    const std::vector<Entity>& entities)
{
    std::vector<Point2D> polygon = profileToPolygon(profile, entities, 32);
    return polygonCentroid(polygon);
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
    for (size_t i = 0; i < reversed.reversed.size(); ++i) {
        reversed.reversed[i] = !reversed.reversed[i];
    }
    std::reverse(reversed.reversed.begin(), reversed.reversed.end());

    // Reverse polygon
    std::vector<Point2D> points = reversed.polygon;
    std::reverse(points.begin(), points.end());
    reversed.polygon = points;

    // Negate area
    reversed.area = -reversed.area;

    return reversed;
}

}  // namespace sketch
}  // namespace hobbycad
