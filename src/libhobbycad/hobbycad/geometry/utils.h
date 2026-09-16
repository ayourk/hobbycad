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
#include "../math_constants.h"

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

/// The point `distance` from `center` in direction `angleRad` (polar to
/// Cartesian): a radial dimension label, a handle on a circle, an arc end.
HOBBYCAD_EXPORT Point2D polarPoint(const Point2D& center, double distance, double angleRad);

/// The nearest grid intersection to `p` for a square grid of `spacing`.
HOBBYCAD_EXPORT Point2D snapToGrid(const Point2D& p, double spacing);

// =====================================================================
//  Sketch placement helpers
// =====================================================================
//
//  These were hand-rolled inside the sketch canvas (repeatedly, and with no
//  library equivalent), which meant a second front-end would have to
//  reimplement them. They are pure functions of their inputs and carry no UI
//  state.

/// The frame of a chord: its midpoint, unit direction and unit perpendicular.
/// Used for arc/circle construction from two points.
struct ChordFrame {
    bool    valid = false;   ///< false when the two points coincide
    Point2D midpoint;
    Point2D direction;       ///< unit vector from start to end
    Point2D normal;          ///< unit perpendicular (direction rotated +90 deg)
    double  length = 0.0;    ///< full chord length (not half)
};

/// Build the chord frame for a segment. `valid` is false when the points are
/// closer together than `tolerance`, in which case direction/normal are unset.
HOBBYCAD_EXPORT ChordFrame chordFrame(const Point2D& start,
                                      const Point2D& end,
                                      double tolerance = 1e-3);

/// An arc center placed on the perpendicular bisector of a chord.
struct ArcCenterFromChord {
    bool    valid = false;      ///< false when the chord endpoints coincide
    Point2D center;
    double  radius = 0.0;
    double  projection = 0.0;   ///< signed distance of the center along the chord normal
};

/// The distance floor arcCenterOnBisector keeps a center off its chord
/// (ChordFloor::MinPerpDistance), in millimeters. A rule for geometry PLACED
/// by a tool, so a dragged center cannot land on the chord and lose the
/// arc's side; the slot tool's Ends mode uses half of it (Aaron) and then
/// its own end-separation rule. Geometry PROJECTED from another sketch is
/// exempt: it arrives with a rotation, and its restriction lives with the
/// source sketch.
constexpr double kChordPerpFloor = 0.1;

/// How arcCenterOnBisector keeps the arc from degenerating.
enum class ChordFloor {
    MinRadius,        ///< push the center out until radius >= 1.01x the half-chord
    MinPerpDistance,  ///< keep |center-offset from the chord| >= minPerpDistance
};

/// Place an arc center on the perpendicular bisector of chord (start,end) at the
/// projection of `target` onto that bisector. `semicircle` forces the center to
/// the chord midpoint (an exact 180-degree arc); `flip` mirrors it to the far
/// side. The `floor` keeps the arc non-degenerate: MinRadius (the default, unless
/// `semicircle`) pushes the center out to 1.01x the half-chord; MinPerpDistance
/// only keeps the center at least `minPerpDistance` off the chord (used by the
/// endpoint drag, which resizes rather than places).
HOBBYCAD_EXPORT ArcCenterFromChord arcCenterOnBisector(
    const Point2D& start, const Point2D& end, const Point2D& target,
    bool semicircle, bool flip,
    ChordFloor floor = ChordFloor::MinRadius, double minPerpDistance = kChordPerpFloor);

/// The two circle centers of a given radius passing through both points.
/// A radius smaller than half the chord admits no solution, and the two
/// centers coincide when the radius is exactly half the chord.
struct ChordCenters {
    bool    valid = false;   ///< false when radius is too small, or points coincide
    Point2D first;           ///< midpoint + normal * offset
    Point2D second;          ///< midpoint - normal * offset
};

HOBBYCAD_EXPORT ChordCenters circleCentersThroughPoints(const Point2D& a,
                                                        const Point2D& b,
                                                        double radius,
                                                        double tolerance = 1e-9);

/// Place a point relative to `from`, honoring optionally locked polar values.
///
/// Sentinels match the sketch UI's dimension fields: a `lockedLength` of zero
/// or less means "use the length implied by `to`", and a `lockedAngleDegrees`
/// of exactly -1.0 means "use the angle implied by `to`". With neither locked
/// this returns `to` unchanged.
HOBBYCAD_EXPORT Point2D applyPolarLock(const Point2D& from,
                                       const Point2D& to,
                                       double lockedLength,
                                       double lockedAngleDegrees);

/// Place the end of a second edge p2 -> p3 from `toward` (the cursor),
/// honoring a locked length and a locked INSIDE angle between the edges
/// p2 -> p1 and p2 -> p3, on whichever side of the first edge the cursor
/// is. Sentinels as applyPolarLock: length <= 0 and angle == -1.0 mean
/// "from the cursor". A degenerate length returns `toward` unchanged.
HOBBYCAD_EXPORT Point2D applyInsideAngleLock(const Point2D& p1, const Point2D& p2,
                                             const Point2D& toward,
                                             double lockedLength, double lockedAngleDegrees);

/// Of the two circle centers of `radius` through `a` and `b`, the one on
/// `toward`'s side. False when the radius is too small for the chord (or
/// the points coincide).
HOBBYCAD_EXPORT bool lockedRadiusCenterToward(const Point2D& a, const Point2D& b, double radius,
                                              const Point2D& toward, Point2D& center);

/// End point of a locked-sweep arc: on the circle through `start` about
/// `center`, `lockedSweepDeg` away from `start` toward the side `toward` lies
/// on (the short way round, or the long way when `flip`).
HOBBYCAD_EXPORT Point2D pointAtLockedSweep(const Point2D& center, const Point2D& start,
                                           const Point2D& toward, double lockedSweepDeg, bool flip);

/// Center of the arc through `start` and `end` whose sweep is `lockedSweepDeg`:
/// on the perpendicular bisector, on the side of the chord where `toward` lies.
/// A degenerate chord returns `toward` unchanged.
HOBBYCAD_EXPORT Point2D arcCenterFromChordAndSweep(const Point2D& start, const Point2D& end,
                                                   const Point2D& toward, double lockedSweepDeg);

/// Wrap a sweep to [-180, 180] degrees / [-pi, pi] radians, ends inclusive.
/// Not normalizeAngle180(): that maps an exact half turn to -180, and the two
/// half circles of +180 and -180 are different arcs, so a sweep keeps its sign.
inline double wrapSweepDeg(double sweep)
{
    while (sweep > 180.0) sweep -= 360.0;
    while (sweep < -180.0) sweep += 360.0;
    return sweep;
}
inline double wrapSweepRad(double sweep)
{
    while (sweep > M_PI) sweep -= 2.0 * M_PI;
    while (sweep < -M_PI) sweep += 2.0 * M_PI;
    return sweep;
}

/// The same two endpoints the long way round: a sweep and its complement.
inline double oppositeSweepDeg(double sweep) { return sweep > 0 ? sweep - 360.0 : sweep + 360.0; }
inline double oppositeSweepRad(double sweep) { return sweep > 0 ? sweep - 2.0 * M_PI : sweep + 2.0 * M_PI; }

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

/// Closest point on the SEGMENT [a,b] to `point`: the projection parameter
/// clamped to [0,1]. Unlike projectPointOnLine (infinite line, unclamped t),
/// this never returns a point beyond the endpoints.
HOBBYCAD_EXPORT Point2D closestPointOnSegment(
    const Point2D& point, const Point2D& a, const Point2D& b);

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

/// Vertices of a regular N-gon, in order. When `circumscribed` is false the
/// polygon is inscribed (vertices sit on the circle of the given `radius`);
/// when true, `radius` is the apothem (edge midpoints on the circle) and the
/// polygon is rotated a half-step so an edge midpoint faces `startAngle`.
/// `startAngle` (radians) is the direction of the first vertex. Returns empty
/// for sides < 3 or radius <= 0.
HOBBYCAD_EXPORT std::vector<Point2D> regularPolygonVertices(
    const Point2D& center, double radius, int sides,
    double startAngle, bool circumscribed);

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
