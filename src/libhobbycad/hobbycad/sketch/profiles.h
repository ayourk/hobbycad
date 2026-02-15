// =====================================================================
//  src/libhobbycad/hobbycad/sketch/profiles.h — Profile detection
// =====================================================================
//
//  Functions for detecting closed profiles (loops) in sketch geometry.
//  Profiles are used for extrusion, revolve, and other operations.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_SKETCH_PROFILES_H
#define HOBBYCAD_SKETCH_PROFILES_H

#include "entity.h"
#include "../geometry/types.h"

#include <QVector>
#include <QPolygonF>

namespace hobbycad {
namespace sketch {

// =====================================================================
//  Profile Data Structure
// =====================================================================

/// A closed profile (loop) detected in the sketch
struct Profile {
    int id = 0;                       ///< Unique profile ID
    QVector<int> entityIds;           ///< IDs of entities forming the loop (in order)
    QVector<bool> reversed;           ///< Whether each entity is traversed in reverse
    QPolygonF polygon;                ///< Approximated polygon for the profile
    double area = 0.0;                ///< Signed area (positive = CCW, negative = CW)
    bool isOuter = true;              ///< True if outer profile, false if inner (hole)
    geometry::BoundingBox bounds;     ///< Bounding box of the profile

    /// Check if this profile contains another profile (for hole detection)
    bool contains(const Profile& other) const;

    /// Check if a point is inside this profile
    bool containsPoint(const QPointF& point) const;
};

// =====================================================================
//  Profile Detection
// =====================================================================

/// Options for profile detection
struct ProfileDetectionOptions {
    double tolerance = geometry::POINT_TOLERANCE;  ///< Endpoint connection tolerance
    bool excludeConstruction = true;   ///< Exclude construction geometry
    int maxProfiles = 100;             ///< Maximum profiles to detect
    int polygonSegments = 32;          ///< Segments per arc for polygon approximation
};

/// Detect all closed profiles in a set of entities
/// @param entities All entities in the sketch
/// @param options Detection options
/// @return List of detected profiles
HOBBYCAD_EXPORT QVector<Profile> detectProfiles(
    const QVector<Entity>& entities,
    const ProfileDetectionOptions& options = {});

/// Detect profiles and organize into outer/inner (hole) relationships
/// Inner profiles are marked with isOuter = false
/// @param entities All entities in the sketch
/// @param options Detection options
/// @return List of profiles with outer/inner classification
HOBBYCAD_EXPORT QVector<Profile> detectProfilesWithHoles(
    const QVector<Entity>& entities,
    const ProfileDetectionOptions& options = {});

// =====================================================================
//  Profile Utilities
// =====================================================================

/// Build a polygon approximation of a profile
/// @param profile The profile to polygonize
/// @param entities All entities (for looking up by ID)
/// @param segments Segments per arc/curve
/// @return Polygon approximation
HOBBYCAD_EXPORT QPolygonF profileToPolygon(
    const Profile& profile,
    const QVector<Entity>& entities,
    int segments = 32);

/// Calculate the area of a profile
/// @param profile The profile
/// @param entities All entities
/// @return Signed area (positive = CCW, negative = CW)
HOBBYCAD_EXPORT double profileArea(
    const Profile& profile,
    const QVector<Entity>& entities);

/// Check if two profiles share any edges
HOBBYCAD_EXPORT bool profilesShareEdge(
    const Profile& p1,
    const Profile& p2);

/// Get the centroid of a profile
HOBBYCAD_EXPORT QPointF profileCentroid(
    const Profile& profile,
    const QVector<Entity>& entities);

/// Check if profile winding is counter-clockwise
HOBBYCAD_EXPORT bool profileIsCCW(const Profile& profile);

/// Reverse the winding direction of a profile
HOBBYCAD_EXPORT Profile reverseProfile(const Profile& profile);

// =====================================================================
//  Connectivity Analysis
// =====================================================================

/// Node in the connectivity graph
struct ConnectivityNode {
    int entityId = 0;
    int pointIndex = 0;  ///< Which endpoint (0 = start, 1 = end)
    QPointF position;
};

/// Edge in the connectivity graph
struct ConnectivityEdge {
    int entityId = 0;
    int startNode = 0;
    int endNode = 0;
    double length = 0.0;
    bool isConstruction = false;
};

/// Graph of entity connectivity for profile detection
struct ConnectivityGraph {
    QVector<ConnectivityNode> nodes;
    QVector<ConnectivityEdge> edges;
    QVector<QVector<int>> adjacency;  ///< Node -> list of edge indices
};

/// Build a connectivity graph from entities
HOBBYCAD_EXPORT ConnectivityGraph buildConnectivityGraph(
    const QVector<Entity>& entities,
    double tolerance = geometry::POINT_TOLERANCE);

/// Find all cycles in the connectivity graph (potential profiles)
HOBBYCAD_EXPORT QVector<QVector<int>> findCycles(
    const ConnectivityGraph& graph,
    int maxCycles = 100);

}  // namespace sketch
}  // namespace hobbycad

#endif  // HOBBYCAD_SKETCH_PROFILES_H
