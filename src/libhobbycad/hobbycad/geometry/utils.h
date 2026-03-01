// =====================================================================
//  src/libhobbycad/hobbycad/geometry/utils.h — Geometry utility functions
// =====================================================================
//
//  General geometry utility functions for bounding boxes, connectivity,
//  and other common operations.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_GEOMETRY_UTILS_H
#define HOBBYCAD_GEOMETRY_UTILS_H

#include "types.h"

namespace hobbycad {
namespace geometry {

// =====================================================================
//  Vector Operations
// =====================================================================

/// Compute the dot product of two vectors (as Point2D)
HOBBYCAD_EXPORT double dot(const Point2D& a, const Point2D& b);

/// Compute the cross product (z-component) of two 2D vectors
HOBBYCAD_EXPORT double cross(const Point2D& a, const Point2D& b);

/// Compute the length of a vector
HOBBYCAD_EXPORT double length(const Point2D& v);

/// Compute the squared length of a vector (faster, no sqrt)
HOBBYCAD_EXPORT double lengthSquared(const Point2D& v);

/// Normalize a vector to unit length
HOBBYCAD_EXPORT Point2D normalize(const Point2D& v);

/// Compute perpendicular vector (90° CCW rotation)
HOBBYCAD_EXPORT Point2D perpendicular(const Point2D& v);

/// Compute perpendicular vector (90° CW rotation)
HOBBYCAD_EXPORT Point2D perpendicularCW(const Point2D& v);

/// Linear interpolation between two points
HOBBYCAD_EXPORT Point2D lerp(const Point2D& a, const Point2D& b, double t);

// =====================================================================
//  Angle Operations
// =====================================================================

/// Compute angle of a vector in degrees (0 = +X axis, CCW positive)
HOBBYCAD_EXPORT double vectorAngle(const Point2D& v);

/// Compute angle between two vectors in degrees
HOBBYCAD_EXPORT double angleBetween(const Point2D& a, const Point2D& b);

/// Compute signed angle from vector a to vector b in degrees (CCW positive)
HOBBYCAD_EXPORT double signedAngleBetween(const Point2D& a, const Point2D& b);

/// Rotate a point around the origin by angle (degrees)
HOBBYCAD_EXPORT Point2D rotatePoint(const Point2D& point, double angleDegrees);

/// Rotate a point around a center by angle (degrees)
HOBBYCAD_EXPORT Point2D rotatePointAround(
    const Point2D& point, const Point2D& center, double angleDegrees);

// =====================================================================
//  Line Operations
// =====================================================================

/// Compute the length of a line segment
HOBBYCAD_EXPORT double lineLength(const Point2D& p1, const Point2D& p2);

/// Compute the midpoint of a line segment
HOBBYCAD_EXPORT Point2D lineMidpoint(const Point2D& p1, const Point2D& p2);

/// Compute the direction vector of a line (normalized)
HOBBYCAD_EXPORT Point2D lineDirection(const Point2D& p1, const Point2D& p2);

/// Compute point on line at parameter t (0 = p1, 1 = p2)
HOBBYCAD_EXPORT Point2D pointOnLine(const Point2D& p1, const Point2D& p2, double t);

/// Project a point onto a line, returning the parameter t
HOBBYCAD_EXPORT double projectPointOnLine(
    const Point2D& point,
    const Point2D& lineStart, const Point2D& lineEnd);

/// Check if two line segments are parallel
HOBBYCAD_EXPORT bool linesParallel(
    const Point2D& p1, const Point2D& p2,
    const Point2D& p3, const Point2D& p4,
    double tolerance = DEFAULT_TOLERANCE);

/// Check if two line segments are perpendicular
HOBBYCAD_EXPORT bool linesPerpendicular(
    const Point2D& p1, const Point2D& p2,
    const Point2D& p3, const Point2D& p4,
    double tolerance = DEFAULT_TOLERANCE);

// =====================================================================
//  Ray Operations
// =====================================================================

/// Project a point onto an infinite ray (origin + direction)
/// @param point The point to project
/// @param rayOrigin Start point of the ray
/// @param rayDirection Direction vector of the ray (will be normalized internally)
/// @return The projected point on the ray
HOBBYCAD_EXPORT Point2D projectPointOntoRay(
    const Point2D& point,
    const Point2D& rayOrigin,
    const Point2D& rayDirection);

/// Calculate the distance from a point to an infinite ray
/// @param point The point to measure from
/// @param rayOrigin Start point of the ray
/// @param rayDirection Direction vector of the ray (will be normalized internally)
/// @return Distance from point to the ray
HOBBYCAD_EXPORT double distanceFromRay(
    const Point2D& point,
    const Point2D& rayOrigin,
    const Point2D& rayDirection);

/// Check if a point lies on a ray within tolerance
/// @param point The point to check
/// @param rayOrigin Start point of the ray
/// @param rayDirection Direction vector of the ray (will be normalized internally)
/// @param tolerance Distance tolerance (default: essentially exact)
/// @return true if point is on the ray within tolerance
HOBBYCAD_EXPORT bool pointOnRay(
    const Point2D& point,
    const Point2D& rayOrigin,
    const Point2D& rayDirection,
    double tolerance = 0.001);

/// Snap a point to the nearest angle increment from an origin
/// @param origin The origin point
/// @param target The point to snap
/// @param incrementDegrees The angle increment in degrees (default: 45°)
/// @return The snapped point at the same distance from origin but at a snapped angle
HOBBYCAD_EXPORT Point2D snapToAngleIncrement(
    const Point2D& origin,
    const Point2D& target,
    double incrementDegrees = 45.0);

/// Snap a point to the nearest angle increment and return the snapped angle
/// @param origin The origin point
/// @param target The point to snap
/// @param incrementDegrees The angle increment in degrees (default: 45°)
/// @param[out] snappedAngle The resulting snapped angle in degrees
/// @return The snapped point at the same distance from origin but at a snapped angle
HOBBYCAD_EXPORT Point2D snapToAngleIncrementWithAngle(
    const Point2D& origin,
    const Point2D& target,
    double incrementDegrees,
    double& snappedAngle);

// =====================================================================
//  Arc Operations
// =====================================================================

/// Create arc from three points (start, mid, end)
/// Returns nullopt if points are collinear
HOBBYCAD_EXPORT std::optional<Arc> arcFromThreePoints(
    const Point2D& start, const Point2D& mid, const Point2D& end);

/// Create arc from center and two endpoints
/// Sweep direction determined by sweepCCW parameter
HOBBYCAD_EXPORT Arc arcFromCenterAndEndpoints(
    const Point2D& center,
    const Point2D& start, const Point2D& end,
    bool sweepCCW = true);

/// Compute arc length
HOBBYCAD_EXPORT double arcLength(const Arc& arc);

/// Split an arc at a point, returning two arcs
/// Returns empty vector if point is not on arc
HOBBYCAD_EXPORT std::vector<Arc> splitArc(const Arc& arc, const Point2D& point);

// =====================================================================
//  Polygon Operations
// =====================================================================

/// Compute the signed area of a polygon (positive = CCW, negative = CW)
HOBBYCAD_EXPORT double polygonArea(const std::vector<Point2D>& polygon);

/// Check if a polygon is wound counter-clockwise
HOBBYCAD_EXPORT bool polygonIsCCW(const std::vector<Point2D>& polygon);

/// Reverse the winding order of a polygon
HOBBYCAD_EXPORT std::vector<Point2D> reversePolygon(const std::vector<Point2D>& polygon);

/// Check if a point is inside a polygon (using ray casting)
HOBBYCAD_EXPORT bool pointInPolygon(
    const Point2D& point,
    const std::vector<Point2D>& polygon);

/// Compute the centroid of a polygon
HOBBYCAD_EXPORT Point2D polygonCentroid(const std::vector<Point2D>& polygon);

/// Compute the bounding box of a polygon
HOBBYCAD_EXPORT BoundingBox polygonBounds(const std::vector<Point2D>& polygon);

// =====================================================================
//  Tangent Circle/Arc Construction
// =====================================================================

/// Result of tangent circle calculation
struct TangentCircleResult {
    bool valid = false;           ///< Whether a valid circle was found
    Point2D center;               ///< Circle center
    double radius = 0.0;          ///< Circle radius
};

/// Result of tangent arc calculation
struct TangentArcResult {
    bool valid = false;           ///< Whether a valid arc was found
    Point2D center;               ///< Arc center
    double radius = 0.0;          ///< Arc radius
    double startAngle = 0.0;      ///< Start angle in degrees
    double sweepAngle = 0.0;      ///< Sweep angle in degrees
};

/// Calculate circle tangent to two lines
/// Uses angle bisector method to find the circle center
/// @param line1Start, line1End First line segment
/// @param line2Start, line2End Second line segment
/// @param radius Desired circle radius
/// @param hint Point near desired tangent location (for selecting which of multiple solutions)
/// @return Tangent circle result
HOBBYCAD_EXPORT TangentCircleResult circleTangentToTwoLines(
    const Point2D& line1Start, const Point2D& line1End,
    const Point2D& line2Start, const Point2D& line2End,
    double radius,
    const Point2D& hint = Point2D());

/// Calculate circle tangent to three lines (incircle)
/// Uses incenter calculation (intersection of angle bisectors)
/// @param line1Start, line1End First line segment
/// @param line2Start, line2End Second line segment
/// @param line3Start, line3End Third line segment
/// @return Tangent circle result (incircle of the triangle formed by lines)
HOBBYCAD_EXPORT TangentCircleResult circleTangentToThreeLines(
    const Point2D& line1Start, const Point2D& line1End,
    const Point2D& line2Start, const Point2D& line2End,
    const Point2D& line3Start, const Point2D& line3End);

/// Calculate arc tangent to a line at a specific point, ending at another point
/// @param lineStart, lineEnd Line segment
/// @param tangentPoint Point on line where arc is tangent
/// @param endPoint End point of the arc
/// @return Tangent arc result
HOBBYCAD_EXPORT TangentArcResult arcTangentToLine(
    const Point2D& lineStart, const Point2D& lineEnd,
    const Point2D& tangentPoint,
    const Point2D& endPoint);

/// Calculate fillet arc between two lines
/// @param line1Start, line1End First line segment
/// @param line2Start, line2End Second line segment
/// @param radius Fillet radius
/// @return Tangent arc result, or invalid if lines don't meet or radius too large
HOBBYCAD_EXPORT TangentArcResult filletArc(
    const Point2D& line1Start, const Point2D& line1End,
    const Point2D& line2Start, const Point2D& line2End,
    double radius);

// =====================================================================
//  Rectangle Operations
// =====================================================================

/// Check if a point is inside a rectangle
HOBBYCAD_EXPORT bool pointInRect(const Point2D& point, const Rect2D& rect);

/// Check if a line segment intersects a rectangle
HOBBYCAD_EXPORT bool lineIntersectsRect(
    const Point2D& p1, const Point2D& p2,
    const Rect2D& rect);

/// Check if a circle intersects a rectangle
HOBBYCAD_EXPORT bool circleIntersectsRect(
    const Point2D& center, double radius,
    const Rect2D& rect);

/// Check if a line segment is fully enclosed by a rectangle
HOBBYCAD_EXPORT bool lineEnclosedByRect(
    const Point2D& p1, const Point2D& p2,
    const Rect2D& rect);

/// Check if a circle is fully enclosed by a rectangle
HOBBYCAD_EXPORT bool circleEnclosedByRect(
    const Point2D& center, double radius,
    const Rect2D& rect);

// =====================================================================
//  Axis Constraint
// =====================================================================

/// Axis selector for constrained movement
enum class Axis { X, Y };

/// Constrain a point to move only along one axis relative to a reference point.
///   Axis::X -> keep point.x, use reference.y
///   Axis::Y -> use reference.x, keep point.y
inline Point2D constrainToAxis(const Point2D& point, const Point2D& reference, Axis axis)
{
    return (axis == Axis::X)
        ? Point2D(point.x, reference.y)
        : Point2D(reference.x, point.y);
}

}  // namespace geometry
}  // namespace hobbycad

#endif  // HOBBYCAD_GEOMETRY_UTILS_H
