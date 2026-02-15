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

/// Compute the dot product of two vectors (as QPointF)
HOBBYCAD_EXPORT double dot(const QPointF& a, const QPointF& b);

/// Compute the cross product (z-component) of two 2D vectors
HOBBYCAD_EXPORT double cross(const QPointF& a, const QPointF& b);

/// Compute the length of a vector
HOBBYCAD_EXPORT double length(const QPointF& v);

/// Compute the squared length of a vector (faster, no sqrt)
HOBBYCAD_EXPORT double lengthSquared(const QPointF& v);

/// Normalize a vector to unit length
HOBBYCAD_EXPORT QPointF normalize(const QPointF& v);

/// Compute perpendicular vector (90° CCW rotation)
HOBBYCAD_EXPORT QPointF perpendicular(const QPointF& v);

/// Compute perpendicular vector (90° CW rotation)
HOBBYCAD_EXPORT QPointF perpendicularCW(const QPointF& v);

/// Linear interpolation between two points
HOBBYCAD_EXPORT QPointF lerp(const QPointF& a, const QPointF& b, double t);

// =====================================================================
//  Angle Operations
// =====================================================================

/// Compute angle of a vector in degrees (0 = +X axis, CCW positive)
HOBBYCAD_EXPORT double vectorAngle(const QPointF& v);

/// Compute angle between two vectors in degrees
HOBBYCAD_EXPORT double angleBetween(const QPointF& a, const QPointF& b);

/// Compute signed angle from vector a to vector b in degrees (CCW positive)
HOBBYCAD_EXPORT double signedAngleBetween(const QPointF& a, const QPointF& b);

/// Rotate a point around the origin by angle (degrees)
HOBBYCAD_EXPORT QPointF rotatePoint(const QPointF& point, double angleDegrees);

/// Rotate a point around a center by angle (degrees)
HOBBYCAD_EXPORT QPointF rotatePointAround(
    const QPointF& point, const QPointF& center, double angleDegrees);

// =====================================================================
//  Line Operations
// =====================================================================

/// Compute the length of a line segment
HOBBYCAD_EXPORT double lineLength(const QPointF& p1, const QPointF& p2);

/// Compute the midpoint of a line segment
HOBBYCAD_EXPORT QPointF lineMidpoint(const QPointF& p1, const QPointF& p2);

/// Compute the direction vector of a line (normalized)
HOBBYCAD_EXPORT QPointF lineDirection(const QPointF& p1, const QPointF& p2);

/// Compute point on line at parameter t (0 = p1, 1 = p2)
HOBBYCAD_EXPORT QPointF pointOnLine(const QPointF& p1, const QPointF& p2, double t);

/// Project a point onto a line, returning the parameter t
HOBBYCAD_EXPORT double projectPointOnLine(
    const QPointF& point,
    const QPointF& lineStart, const QPointF& lineEnd);

/// Check if two line segments are parallel
HOBBYCAD_EXPORT bool linesParallel(
    const QPointF& p1, const QPointF& p2,
    const QPointF& p3, const QPointF& p4,
    double tolerance = DEFAULT_TOLERANCE);

/// Check if two line segments are perpendicular
HOBBYCAD_EXPORT bool linesPerpendicular(
    const QPointF& p1, const QPointF& p2,
    const QPointF& p3, const QPointF& p4,
    double tolerance = DEFAULT_TOLERANCE);

// =====================================================================
//  Arc Operations
// =====================================================================

/// Create arc from three points (start, mid, end)
/// Returns nullopt if points are collinear
HOBBYCAD_EXPORT std::optional<Arc> arcFromThreePoints(
    const QPointF& start, const QPointF& mid, const QPointF& end);

/// Create arc from center and two endpoints
/// Sweep direction determined by sweepCCW parameter
HOBBYCAD_EXPORT Arc arcFromCenterAndEndpoints(
    const QPointF& center,
    const QPointF& start, const QPointF& end,
    bool sweepCCW = true);

/// Compute arc length
HOBBYCAD_EXPORT double arcLength(const Arc& arc);

/// Split an arc at a point, returning two arcs
/// Returns empty vector if point is not on arc
HOBBYCAD_EXPORT QVector<Arc> splitArc(const Arc& arc, const QPointF& point);

// =====================================================================
//  Polygon Operations
// =====================================================================

/// Compute the signed area of a polygon (positive = CCW, negative = CW)
HOBBYCAD_EXPORT double polygonArea(const QVector<QPointF>& polygon);

/// Check if a polygon is wound counter-clockwise
HOBBYCAD_EXPORT bool polygonIsCCW(const QVector<QPointF>& polygon);

/// Reverse the winding order of a polygon
HOBBYCAD_EXPORT QVector<QPointF> reversePolygon(const QVector<QPointF>& polygon);

/// Check if a point is inside a polygon (using ray casting)
HOBBYCAD_EXPORT bool pointInPolygon(
    const QPointF& point,
    const QVector<QPointF>& polygon);

/// Compute the centroid of a polygon
HOBBYCAD_EXPORT QPointF polygonCentroid(const QVector<QPointF>& polygon);

/// Compute the bounding box of a polygon
HOBBYCAD_EXPORT BoundingBox polygonBounds(const QVector<QPointF>& polygon);

// =====================================================================
//  Tangent Circle/Arc Construction
// =====================================================================

/// Result of tangent circle calculation
struct TangentCircleResult {
    bool valid = false;           ///< Whether a valid circle was found
    QPointF center;               ///< Circle center
    double radius = 0.0;          ///< Circle radius
};

/// Result of tangent arc calculation
struct TangentArcResult {
    bool valid = false;           ///< Whether a valid arc was found
    QPointF center;               ///< Arc center
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
    const QPointF& line1Start, const QPointF& line1End,
    const QPointF& line2Start, const QPointF& line2End,
    double radius,
    const QPointF& hint = QPointF());

/// Calculate circle tangent to three lines (incircle)
/// Uses incenter calculation (intersection of angle bisectors)
/// @param line1Start, line1End First line segment
/// @param line2Start, line2End Second line segment
/// @param line3Start, line3End Third line segment
/// @return Tangent circle result (incircle of the triangle formed by lines)
HOBBYCAD_EXPORT TangentCircleResult circleTangentToThreeLines(
    const QPointF& line1Start, const QPointF& line1End,
    const QPointF& line2Start, const QPointF& line2End,
    const QPointF& line3Start, const QPointF& line3End);

/// Calculate arc tangent to a line at a specific point, ending at another point
/// @param lineStart, lineEnd Line segment
/// @param tangentPoint Point on line where arc is tangent
/// @param endPoint End point of the arc
/// @return Tangent arc result
HOBBYCAD_EXPORT TangentArcResult arcTangentToLine(
    const QPointF& lineStart, const QPointF& lineEnd,
    const QPointF& tangentPoint,
    const QPointF& endPoint);

/// Calculate fillet arc between two lines
/// @param line1Start, line1End First line segment
/// @param line2Start, line2End Second line segment
/// @param radius Fillet radius
/// @return Tangent arc result, or invalid if lines don't meet or radius too large
HOBBYCAD_EXPORT TangentArcResult filletArc(
    const QPointF& line1Start, const QPointF& line1End,
    const QPointF& line2Start, const QPointF& line2End,
    double radius);

// =====================================================================
//  Rectangle Operations
// =====================================================================

/// Check if a point is inside a rectangle
HOBBYCAD_EXPORT bool pointInRect(const QPointF& point, const QRectF& rect);

/// Check if a line segment intersects a rectangle
HOBBYCAD_EXPORT bool lineIntersectsRect(
    const QPointF& p1, const QPointF& p2,
    const QRectF& rect);

/// Check if a circle intersects a rectangle
HOBBYCAD_EXPORT bool circleIntersectsRect(
    const QPointF& center, double radius,
    const QRectF& rect);

/// Check if a line segment is fully enclosed by a rectangle
HOBBYCAD_EXPORT bool lineEnclosedByRect(
    const QPointF& p1, const QPointF& p2,
    const QRectF& rect);

/// Check if a circle is fully enclosed by a rectangle
HOBBYCAD_EXPORT bool circleEnclosedByRect(
    const QPointF& center, double radius,
    const QRectF& rect);

}  // namespace geometry
}  // namespace hobbycad

#endif  // HOBBYCAD_GEOMETRY_UTILS_H
