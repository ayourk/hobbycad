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
    const Point2D& p1, const Point2D& p2,
    const Point2D& p3, const Point2D& p4);

/// Compute intersection of two infinite lines (defined by two points each)
/// @param p1, p2 Points defining first line
/// @param p3, p4 Points defining second line
/// @return Intersection result (withinSegment flags ignored)
HOBBYCAD_EXPORT LineLineIntersection infiniteLineIntersection(
    const Point2D& p1, const Point2D& p2,
    const Point2D& p3, const Point2D& p4);

// =====================================================================
//  Line-Circle Intersections
// =====================================================================

/// Compute intersection of a line segment with a circle
/// @param lineStart, lineEnd Line segment endpoints
/// @param center Circle center
/// @param radius Circle radius
/// @return Intersection result with up to 2 points
HOBBYCAD_EXPORT LineCircleIntersection lineCircleIntersection(
    const Point2D& lineStart, const Point2D& lineEnd,
    const Point2D& center, double radius);

/// Compute intersection of an infinite line with a circle
/// @param linePoint1, linePoint2 Points defining the line
/// @param center Circle center
/// @param radius Circle radius
/// @return Intersection result (segment flags ignored)
HOBBYCAD_EXPORT LineCircleIntersection infiniteLineCircleIntersection(
    const Point2D& linePoint1, const Point2D& linePoint2,
    const Point2D& center, double radius);

// =====================================================================
//  Circle-Circle Intersections
// =====================================================================

/// Compute intersection of two circles
/// @param center1, radius1 First circle
/// @param center2, radius2 Second circle
/// @return Intersection result with up to 2 points
HOBBYCAD_EXPORT CircleCircleIntersection circleCircleIntersection(
    const Point2D& center1, double radius1,
    const Point2D& center2, double radius2);

// =====================================================================
//  Line-Arc Intersections
// =====================================================================

/// Compute intersection of a line segment with an arc
/// @param lineStart, lineEnd Line segment endpoints
/// @param arc Arc definition
/// @return Intersection result with up to 2 points
HOBBYCAD_EXPORT LineArcIntersection lineArcIntersection(
    const Point2D& lineStart, const Point2D& lineEnd,
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
HOBBYCAD_EXPORT Point2D closestPointOnLine(
    const Point2D& point,
    const Point2D& lineStart, const Point2D& lineEnd);

/// Find the closest point on a circle to a given point
/// @param point The query point
/// @param center Circle center
/// @param radius Circle radius
/// @return Closest point on the circle
HOBBYCAD_EXPORT Point2D closestPointOnCircle(
    const Point2D& point,
    const Point2D& center, double radius);

/// Find the closest point on an arc to a given point
/// @param point The query point
/// @param arc Arc definition
/// @return Closest point on the arc
HOBBYCAD_EXPORT Point2D closestPointOnArc(
    const Point2D& point,
    const Arc& arc);

// =====================================================================
//  Distance Functions
// =====================================================================

/// Distance from a point to a line segment
HOBBYCAD_EXPORT double pointToLineDistance(
    const Point2D& point,
    const Point2D& lineStart, const Point2D& lineEnd);

/// Distance from a point to an infinite line
HOBBYCAD_EXPORT double pointToInfiniteLineDistance(
    const Point2D& point,
    const Point2D& linePoint1, const Point2D& linePoint2);

/// Distance from a point to a circle (to the circumference)
HOBBYCAD_EXPORT double pointToCircleDistance(
    const Point2D& point,
    const Point2D& center, double radius);

/// Distance from a point to an arc
HOBBYCAD_EXPORT double pointToArcDistance(
    const Point2D& point,
    const Arc& arc);

// =====================================================================
//  Utility Functions
// =====================================================================

/// Check if two points are coincident within tolerance
HOBBYCAD_EXPORT bool pointsCoincident(
    const Point2D& p1, const Point2D& p2,
    double tolerance = POINT_TOLERANCE);

/// Check if a point lies on a line segment within tolerance
HOBBYCAD_EXPORT bool pointOnLine(
    const Point2D& point,
    const Point2D& lineStart, const Point2D& lineEnd,
    double tolerance = DEFAULT_TOLERANCE);

/// Check if a point lies on a circle within tolerance
HOBBYCAD_EXPORT bool pointOnCircle(
    const Point2D& point,
    const Point2D& center, double radius,
    double tolerance = DEFAULT_TOLERANCE);

/// Check if a point lies on an arc within tolerance
HOBBYCAD_EXPORT bool pointOnArc(
    const Point2D& point,
    const Arc& arc,
    double tolerance = DEFAULT_TOLERANCE);

/// Normalize an angle to [0, 360) degrees
HOBBYCAD_EXPORT double normalizeAngle(double degrees);

/// Normalize an angle to [-180, 180) degrees
HOBBYCAD_EXPORT double normalizeAngleSigned(double degrees);

}  // namespace geometry
}  // namespace hobbycad

#endif  // HOBBYCAD_GEOMETRY_INTERSECTIONS_H
