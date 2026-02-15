// =====================================================================
//  src/libhobbycad/hobbycad/geometry/intersections.h — Intersection functions
// =====================================================================
//
//  Functions for computing intersections between geometric primitives.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_GEOMETRY_INTERSECTIONS_H
#define HOBBYCAD_GEOMETRY_INTERSECTIONS_H

#include "types.h"

namespace hobbycad {
namespace geometry {

// =====================================================================
//  Line Intersections
// =====================================================================

/// Compute intersection of two line segments
/// @param p1, p2 First line segment endpoints
/// @param p3, p4 Second line segment endpoints
/// @return Intersection result with point and parameters
HOBBYCAD_EXPORT LineLineIntersection lineLineIntersection(
    const QPointF& p1, const QPointF& p2,
    const QPointF& p3, const QPointF& p4);

/// Compute intersection of two infinite lines (defined by two points each)
/// @param p1, p2 Points defining first line
/// @param p3, p4 Points defining second line
/// @return Intersection result (withinSegment flags ignored)
HOBBYCAD_EXPORT LineLineIntersection infiniteLineIntersection(
    const QPointF& p1, const QPointF& p2,
    const QPointF& p3, const QPointF& p4);

// =====================================================================
//  Line-Circle Intersections
// =====================================================================

/// Compute intersection of a line segment with a circle
/// @param lineStart, lineEnd Line segment endpoints
/// @param center Circle center
/// @param radius Circle radius
/// @return Intersection result with up to 2 points
HOBBYCAD_EXPORT LineCircleIntersection lineCircleIntersection(
    const QPointF& lineStart, const QPointF& lineEnd,
    const QPointF& center, double radius);

/// Compute intersection of an infinite line with a circle
/// @param linePoint1, linePoint2 Points defining the line
/// @param center Circle center
/// @param radius Circle radius
/// @return Intersection result (segment flags ignored)
HOBBYCAD_EXPORT LineCircleIntersection infiniteLineCircleIntersection(
    const QPointF& linePoint1, const QPointF& linePoint2,
    const QPointF& center, double radius);

// =====================================================================
//  Circle-Circle Intersections
// =====================================================================

/// Compute intersection of two circles
/// @param center1, radius1 First circle
/// @param center2, radius2 Second circle
/// @return Intersection result with up to 2 points
HOBBYCAD_EXPORT CircleCircleIntersection circleCircleIntersection(
    const QPointF& center1, double radius1,
    const QPointF& center2, double radius2);

// =====================================================================
//  Line-Arc Intersections
// =====================================================================

/// Compute intersection of a line segment with an arc
/// @param lineStart, lineEnd Line segment endpoints
/// @param arc Arc definition
/// @return Intersection result with up to 2 points
HOBBYCAD_EXPORT LineArcIntersection lineArcIntersection(
    const QPointF& lineStart, const QPointF& lineEnd,
    const Arc& arc);

// =====================================================================
//  Arc-Arc Intersections
// =====================================================================

/// Compute intersection of two arcs
/// Uses circle-circle intersection and filters by arc sweeps
/// @param arc1 First arc
/// @param arc2 Second arc
/// @return Intersection result (uses CircleCircleIntersection, filtered)
HOBBYCAD_EXPORT CircleCircleIntersection arcArcIntersection(
    const Arc& arc1, const Arc& arc2);

// =====================================================================
//  Closest Point Functions
// =====================================================================

/// Find the closest point on a line segment to a given point
/// @param point The query point
/// @param lineStart, lineEnd Line segment endpoints
/// @return Closest point on the segment
HOBBYCAD_EXPORT QPointF closestPointOnLine(
    const QPointF& point,
    const QPointF& lineStart, const QPointF& lineEnd);

/// Find the closest point on a circle to a given point
/// @param point The query point
/// @param center Circle center
/// @param radius Circle radius
/// @return Closest point on the circle
HOBBYCAD_EXPORT QPointF closestPointOnCircle(
    const QPointF& point,
    const QPointF& center, double radius);

/// Find the closest point on an arc to a given point
/// @param point The query point
/// @param arc Arc definition
/// @return Closest point on the arc
HOBBYCAD_EXPORT QPointF closestPointOnArc(
    const QPointF& point,
    const Arc& arc);

// =====================================================================
//  Distance Functions
// =====================================================================

/// Distance from a point to a line segment
HOBBYCAD_EXPORT double pointToLineDistance(
    const QPointF& point,
    const QPointF& lineStart, const QPointF& lineEnd);

/// Distance from a point to an infinite line
HOBBYCAD_EXPORT double pointToInfiniteLineDistance(
    const QPointF& point,
    const QPointF& linePoint1, const QPointF& linePoint2);

/// Distance from a point to a circle (to the circumference)
HOBBYCAD_EXPORT double pointToCircleDistance(
    const QPointF& point,
    const QPointF& center, double radius);

/// Distance from a point to an arc
HOBBYCAD_EXPORT double pointToArcDistance(
    const QPointF& point,
    const Arc& arc);

// =====================================================================
//  Utility Functions
// =====================================================================

/// Check if two points are coincident within tolerance
HOBBYCAD_EXPORT bool pointsCoincident(
    const QPointF& p1, const QPointF& p2,
    double tolerance = POINT_TOLERANCE);

/// Check if a point lies on a line segment within tolerance
HOBBYCAD_EXPORT bool pointOnLine(
    const QPointF& point,
    const QPointF& lineStart, const QPointF& lineEnd,
    double tolerance = DEFAULT_TOLERANCE);

/// Check if a point lies on a circle within tolerance
HOBBYCAD_EXPORT bool pointOnCircle(
    const QPointF& point,
    const QPointF& center, double radius,
    double tolerance = DEFAULT_TOLERANCE);

/// Check if a point lies on an arc within tolerance
HOBBYCAD_EXPORT bool pointOnArc(
    const QPointF& point,
    const Arc& arc,
    double tolerance = DEFAULT_TOLERANCE);

/// Normalize an angle to [0, 360) degrees
HOBBYCAD_EXPORT double normalizeAngle(double degrees);

/// Normalize an angle to [-180, 180) degrees
HOBBYCAD_EXPORT double normalizeAngleSigned(double degrees);

}  // namespace geometry
}  // namespace hobbycad

#endif  // HOBBYCAD_GEOMETRY_INTERSECTIONS_H
