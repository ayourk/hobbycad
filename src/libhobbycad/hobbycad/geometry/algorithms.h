// =====================================================================
//  src/libhobbycad/hobbycad/geometry/algorithms.h — Geometry algorithms
// =====================================================================
//
//  Advanced computational geometry algorithms for analysis, optimization,
//  and manufacturing applications.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_GEOMETRY_ALGORITHMS_H
#define HOBBYCAD_GEOMETRY_ALGORITHMS_H

#include "types.h"
#include "../core.h"

#include <QPointF>
#include <QPolygonF>
#include <QVector>
#include <optional>

namespace hobbycad {
namespace geometry {

// =====================================================================
//  Convex Hull
// =====================================================================

/// Compute the convex hull of a set of points using Andrew's monotone chain algorithm
/// @param points Input points
/// @return Points forming the convex hull in counter-clockwise order
/// @note Time complexity: O(n log n)
HOBBYCAD_EXPORT QVector<QPointF> convexHull(const QVector<QPointF>& points);

/// Check if a polygon is convex
/// @param polygon Input polygon (ordered vertices)
/// @return True if the polygon is convex
HOBBYCAD_EXPORT bool isConvex(const QVector<QPointF>& polygon);

// =====================================================================
//  Polygon Simplification
// =====================================================================

/// Simplify a polyline using the Ramer-Douglas-Peucker algorithm
/// Removes points that don't contribute significantly to the shape
/// @param points Input polyline points
/// @param epsilon Maximum distance tolerance for point removal
/// @return Simplified polyline
/// @note Time complexity: O(n^2) worst case, O(n log n) average
HOBBYCAD_EXPORT QVector<QPointF> simplifyPolyline(
    const QVector<QPointF>& points,
    double epsilon);

/// Simplify a polygon (closed shape) using Douglas-Peucker
/// @param polygon Input polygon points (first != last expected, will be closed automatically)
/// @param epsilon Maximum distance tolerance
/// @return Simplified polygon
HOBBYCAD_EXPORT QVector<QPointF> simplifyPolygon(
    const QVector<QPointF>& polygon,
    double epsilon);

/// Simplify using Visvalingam-Whyatt algorithm (area-based)
/// Removes points based on the area of the triangle they form
/// Better for cartographic/visual simplification
/// @param points Input polyline points
/// @param minArea Minimum triangle area to keep a point
/// @return Simplified polyline
HOBBYCAD_EXPORT QVector<QPointF> simplifyByArea(
    const QVector<QPointF>& points,
    double minArea);

// =====================================================================
//  Bounding Shapes
// =====================================================================

/// Result of minimal bounding circle calculation
struct MinimalBoundingCircle {
    QPointF center;
    double radius = 0.0;
};

/// Compute the minimal bounding circle (smallest enclosing circle)
/// Uses Welzl's algorithm
/// @param points Input points
/// @return Minimal circle enclosing all points
/// @note Time complexity: O(n) expected
HOBBYCAD_EXPORT MinimalBoundingCircle minimalBoundingCircle(
    const QVector<QPointF>& points);

/// Result of oriented bounding box calculation
struct OrientedBoundingBox {
    QPointF center;         ///< Center of the box
    QPointF halfExtents;    ///< Half-width and half-height in local coordinates
    double angle = 0.0;     ///< Rotation angle in degrees

    /// Get the four corners of the OBB
    QVector<QPointF> corners() const;

    /// Get the area of the OBB
    double area() const;

    /// Check if a point is inside the OBB
    bool contains(const QPointF& point) const;
};

/// Compute the minimal area oriented bounding box
/// Uses rotating calipers on the convex hull
/// @param points Input points
/// @return Oriented bounding box with minimum area
/// @note Time complexity: O(n log n)
HOBBYCAD_EXPORT OrientedBoundingBox minimalOrientedBoundingBox(
    const QVector<QPointF>& points);

/// Compute an axis-aligned bounding box for a rotated rectangle
/// @param obb The oriented bounding box
/// @return Axis-aligned bounding box enclosing the OBB
HOBBYCAD_EXPORT BoundingBox obbToAABB(const OrientedBoundingBox& obb);

// =====================================================================
//  2D Boolean Operations
// =====================================================================

/// Polygon with holes representation
struct PolygonWithHoles {
    QVector<QPointF> outer;              ///< Outer boundary (CCW)
    QVector<QVector<QPointF>> holes;     ///< Holes (CW)
};

/// Result of boolean operations (may produce multiple polygons)
struct BooleanResult {
    bool success = false;
    QVector<PolygonWithHoles> polygons;  ///< Resulting polygons
    QString error;                        ///< Error message if failed
};

/// Compute union of two polygons
/// @param poly1 First polygon (simple, CCW)
/// @param poly2 Second polygon (simple, CCW)
/// @return Union result (may contain multiple disjoint regions)
HOBBYCAD_EXPORT BooleanResult polygonUnion(
    const QVector<QPointF>& poly1,
    const QVector<QPointF>& poly2);

/// Compute intersection of two polygons
/// @param poly1 First polygon (simple, CCW)
/// @param poly2 Second polygon (simple, CCW)
/// @return Intersection result (may be empty or multiple regions)
HOBBYCAD_EXPORT BooleanResult polygonIntersection(
    const QVector<QPointF>& poly1,
    const QVector<QPointF>& poly2);

/// Compute difference of two polygons (poly1 - poly2)
/// @param poly1 First polygon (simple, CCW)
/// @param poly2 Second polygon (simple, CCW)
/// @return Difference result (may contain holes)
HOBBYCAD_EXPORT BooleanResult polygonDifference(
    const QVector<QPointF>& poly1,
    const QVector<QPointF>& poly2);

/// Compute XOR (symmetric difference) of two polygons
/// @param poly1 First polygon (simple, CCW)
/// @param poly2 Second polygon (simple, CCW)
/// @return XOR result
HOBBYCAD_EXPORT BooleanResult polygonXOR(
    const QVector<QPointF>& poly1,
    const QVector<QPointF>& poly2);

// =====================================================================
//  Polygon Offset
// =====================================================================

/// Offset a polygon by a distance
/// @param polygon Input polygon (CCW for outer, CW for holes)
/// @param distance Offset distance (positive = outward, negative = inward)
/// @param joinType How to handle corners: 0=miter, 1=round, 2=square
/// @param miterLimit Maximum miter distance (for joinType=0)
/// @return Offset polygons (may produce multiple disjoint regions)
HOBBYCAD_EXPORT QVector<QVector<QPointF>> offsetPolygon(
    const QVector<QPointF>& polygon,
    double distance,
    int joinType = 1,
    double miterLimit = 2.0);

/// Offset a polyline (open path) by a distance
/// @param polyline Input polyline
/// @param distance Offset distance (positive = left side)
/// @param endType How to handle ends: 0=butt, 1=round, 2=square
/// @param joinType How to handle corners: 0=miter, 1=round, 2=square
/// @return Offset result (closed polygon around the path)
HOBBYCAD_EXPORT QVector<QVector<QPointF>> offsetPolyline(
    const QVector<QPointF>& polyline,
    double distance,
    int endType = 1,
    int joinType = 1);

// =====================================================================
//  Triangulation
// =====================================================================

/// Triangle represented by three indices into a point array
struct Triangle {
    int i0, i1, i2;
};

/// Edge in a triangulation (for Delaunay)
struct Edge {
    int i0, i1;
    bool operator==(const Edge& other) const {
        return (i0 == other.i0 && i1 == other.i1) ||
               (i0 == other.i1 && i1 == other.i0);
    }
};

/// Triangulate a simple polygon using ear clipping
/// @param polygon Input polygon (simple, CCW)
/// @return Vector of triangles (indices into original polygon)
HOBBYCAD_EXPORT QVector<Triangle> triangulatePolygon(
    const QVector<QPointF>& polygon);

/// Triangulate a polygon with holes using bridge insertion
/// @param outer Outer boundary (CCW)
/// @param holes Hole boundaries (CW each)
/// @return Vector of triangles and the combined point list
HOBBYCAD_EXPORT QPair<QVector<QPointF>, QVector<Triangle>> triangulatePolygonWithHoles(
    const QVector<QPointF>& outer,
    const QVector<QVector<QPointF>>& holes);

/// Compute Delaunay triangulation of a point set
/// Uses Bowyer-Watson incremental algorithm
/// @param points Input points
/// @return Vector of triangles (indices into input points)
/// @note Time complexity: O(n log n) expected, O(n^2) worst case
HOBBYCAD_EXPORT QVector<Triangle> delaunayTriangulation(
    const QVector<QPointF>& points);

/// Compute constrained Delaunay triangulation
/// Preserves specified edges in the triangulation
/// @param points Input points
/// @param constrainedEdges Edges that must appear in triangulation
/// @return Vector of triangles
HOBBYCAD_EXPORT QVector<Triangle> constrainedDelaunay(
    const QVector<QPointF>& points,
    const QVector<Edge>& constrainedEdges);

/// Compute Voronoi diagram (dual of Delaunay)
/// @param points Input points
/// @param bounds Bounding box to clip infinite edges
/// @return Vector of Voronoi cells (one polygon per input point)
HOBBYCAD_EXPORT QVector<QVector<QPointF>> voronoiDiagram(
    const QVector<QPointF>& points,
    const QRectF& bounds);

// =====================================================================
//  Point Set Analysis
// =====================================================================

/// Find the two points with maximum distance (diameter of point set)
/// @param points Input points
/// @return Pair of indices of the farthest points
HOBBYCAD_EXPORT QPair<int, int> findDiameter(const QVector<QPointF>& points);

/// Find the two closest points in a set
/// Uses divide-and-conquer for efficiency
/// @param points Input points
/// @return Pair of indices of the closest points
/// @note Time complexity: O(n log n)
HOBBYCAD_EXPORT QPair<int, int> findClosestPair(const QVector<QPointF>& points);

/// Compute the Hausdorff distance between two point sets
/// Maximum of minimum distances from each point to the other set
/// @param set1 First point set
/// @param set2 Second point set
/// @return Hausdorff distance
HOBBYCAD_EXPORT double hausdorffDistance(
    const QVector<QPointF>& set1,
    const QVector<QPointF>& set2);

// =====================================================================
//  Curve Analysis
// =====================================================================

/// Compute curvature at each point of a polyline
/// @param points Polyline points
/// @return Curvature at each interior point (endpoints get 0)
HOBBYCAD_EXPORT QVector<double> polylineCurvature(const QVector<QPointF>& points);

/// Find corners (high curvature points) in a polyline
/// @param points Polyline points
/// @param angleThreshold Minimum angle change (degrees) to be considered a corner
/// @return Indices of corner points
HOBBYCAD_EXPORT QVector<int> findCorners(
    const QVector<QPointF>& points,
    double angleThreshold = 30.0);

/// Smooth a polyline using Chaikin's algorithm
/// @param points Input polyline
/// @param iterations Number of smoothing iterations
/// @return Smoothed polyline (will have more points)
HOBBYCAD_EXPORT QVector<QPointF> smoothPolyline(
    const QVector<QPointF>& points,
    int iterations = 2);

// =====================================================================
//  Path Operations (for CNC/Manufacturing)
// =====================================================================

/// Compute the total length of a polyline
/// @param points Polyline points
/// @return Total path length
HOBBYCAD_EXPORT double pathLength(const QVector<QPointF>& points);

/// Resample a polyline to have uniform point spacing
/// @param points Input polyline
/// @param spacing Desired spacing between points
/// @return Resampled polyline
HOBBYCAD_EXPORT QVector<QPointF> resamplePath(
    const QVector<QPointF>& points,
    double spacing);

/// Resample a polyline to have a specific number of points
/// @param points Input polyline
/// @param numPoints Desired number of points
/// @return Resampled polyline
HOBBYCAD_EXPORT QVector<QPointF> resamplePathByCount(
    const QVector<QPointF>& points,
    int numPoints);

/// Get point at a specific arc length along a polyline
/// @param points Polyline points
/// @param arcLength Distance along the path from start
/// @return Point at the specified arc length, or last point if arcLength exceeds total
HOBBYCAD_EXPORT QPointF pointAtArcLength(
    const QVector<QPointF>& points,
    double arcLength);

/// Get tangent direction at a specific arc length
/// @param points Polyline points
/// @param arcLength Distance along the path from start
/// @return Unit tangent vector at the specified position
HOBBYCAD_EXPORT QPointF tangentAtArcLength(
    const QVector<QPointF>& points,
    double arcLength);

}  // namespace geometry
}  // namespace hobbycad

#endif  // HOBBYCAD_GEOMETRY_ALGORITHMS_H
