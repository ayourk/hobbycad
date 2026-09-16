// =====================================================================
//  src/libhobbycad/hobbycad/geometry/types.h — Basic geometry types
// =====================================================================
//
//  Fundamental geometric types used throughout libhobbycad.
//  These are lightweight value types for points, vectors, and transforms.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_GEOMETRY_TYPES_H
#define HOBBYCAD_GEOMETRY_TYPES_H

#include "../core.h"
#include "../types.h"

#include <vector>
#include <cmath>
#include <optional>

namespace hobbycad {
namespace geometry {

// =====================================================================
//  Constants
// =====================================================================

/// Default tolerance for geometric comparisons (in mm)
constexpr double DEFAULT_TOLERANCE = 1e-6;

/// Named thresholds shared by the library and the front ends. Each value is
/// the literal it replaced across the code base; the name records what the
/// threshold means so a reader does not have to infer it from the comparison.
/// A magnitude (length, radius, vector norm, denominator, radian angle
/// difference) below this is treated as zero.
constexpr double kZeroEps = 1e-9;
/// Near-exact equality of doubles, and squared lengths that must be non-zero.
constexpr double kExactEps = 1e-12;
/// A length, distance or radius below this is degenerate geometry (mm).
constexpr double kDegenerateLen = 1e-6;
/// Angle differences in degrees below this are the same angle.
constexpr double kAngleEpsDeg = 1e-6;
/// Points closer than this coincide (mm; also used on squared distances).
constexpr double kCoincidentTol = 1e-4;

/// "Greater than zero" as the model means it. Aaron: a width, radius or
/// length has to be greater than zero, and how much greater is dictated by
/// the precision. A length at or below kDegenerateLen is zero for every
/// purpose; an angle in degrees at or below kAngleEpsDeg likewise. Every
/// validation of a size or a sweep goes through these, never a bare "> 0".
constexpr bool isPositiveLength(double v) { return v > kDegenerateLen; }
constexpr bool isZeroAngleDeg(double deg) { return (deg < 0.0 ? -deg : deg) <= kAngleEpsDeg; }
constexpr bool isPositiveAngleDeg(double deg) { return deg > kAngleEpsDeg; }

/// Tolerance for point coincidence checks (in mm)
constexpr double POINT_TOLERANCE = 0.5;

// =====================================================================
//  Intersection Results
// =====================================================================

/// Result of a line-line intersection
struct LineLineIntersection {
    bool intersects = false;      ///< Whether lines intersect
    bool parallel = false;        ///< Whether lines are parallel
    bool coincident = false;      ///< Whether lines are coincident (overlapping)
    Point2D point;                ///< Intersection point (if intersects)
    double t1 = 0.0;              ///< Parameter on first line [0,1] if within segment
    double t2 = 0.0;              ///< Parameter on second line [0,1] if within segment
    bool withinSegment1 = false;  ///< Whether intersection is within first segment
    bool withinSegment2 = false;  ///< Whether intersection is within second segment
};

/// Result of a line-circle intersection
struct LineCircleIntersection {
    int count = 0;                ///< Number of intersections (0, 1, or 2)
    Point2D point1;               ///< First intersection point
    Point2D point2;               ///< Second intersection point
    double t1 = 0.0;              ///< Parameter on line for first intersection
    double t2 = 0.0;              ///< Parameter on line for second intersection
    bool point1InSegment = false; ///< Whether first point is within line segment
    bool point2InSegment = false; ///< Whether second point is within line segment
};

/// Result of a circle-circle intersection
struct CircleCircleIntersection {
    int count = 0;                ///< Number of intersections (0, 1, or 2)
    bool coincident = false;      ///< Whether circles are coincident
    bool internal = false;        ///< Whether one circle is inside the other
    Point2D point1;               ///< First intersection point
    Point2D point2;               ///< Second intersection point
};

/// Result of a line-arc intersection
struct LineArcIntersection {
    int count = 0;                ///< Number of intersections (0, 1, or 2)
    Point2D point1;               ///< First intersection point
    Point2D point2;               ///< Second intersection point
    double t1 = 0.0;              ///< Parameter on line for first intersection [0,1]
    double t2 = 0.0;              ///< Parameter on line for second intersection [0,1]
    bool point1InSegment = false; ///< Whether first point is within line segment
    bool point2InSegment = false; ///< Whether second point is within line segment
    bool point1OnArc = false;     ///< Whether first point is on arc sweep
    bool point2OnArc = false;     ///< Whether second point is on arc sweep
};

// =====================================================================
//  Arc Representation
// =====================================================================

/// Arc defined by center, radius, and angles
struct Arc {
    Point2D center;
    double radius = 0.0;
    double startAngle = 0.0;   ///< Start angle in degrees
    double sweepAngle = 360.0; ///< Sweep angle in degrees (positive = CCW)

    /// Check if an angle (in degrees) is within the arc sweep
    bool containsAngle(double angle) const;

    /// Get the start point of the arc
    Point2D startPoint() const;

    /// Get the end point of the arc
    Point2D endPoint() const;

    /// Get point at parameter t (0 = start, 1 = end)
    Point2D pointAt(double t) const;
};

// =====================================================================
//  Bounding Box
// =====================================================================

/// Axis-aligned bounding box with utility methods
struct BoundingBox {
    double minX = 0.0;
    double minY = 0.0;
    double maxX = 0.0;
    double maxY = 0.0;
    bool valid = false;

    BoundingBox() = default;
    BoundingBox(double x1, double y1, double x2, double y2);
    explicit BoundingBox(const Rect2D& rect);
    explicit BoundingBox(const Point2D& point);

#if HOBBYCAD_HAS_QT
    explicit BoundingBox(const QRectF& rect);
    explicit BoundingBox(const QPointF& point);
#endif

    /// Expand to include a point
    void include(const Point2D& point);

    /// Expand to include another bounding box
    void include(const BoundingBox& other);

    /// Get center point
    Point2D center() const;

    /// Get width
    double width() const { return maxX - minX; }

    /// Get height
    double height() const { return maxY - minY; }

    /// Convert to Rect2D
    Rect2D toRect() const;

    /// Check if point is inside (inclusive)
    bool contains(const Point2D& point) const;

    /// Check if another box intersects
    bool intersects(const BoundingBox& other) const;
};

// =====================================================================
//  Transform Types
// =====================================================================

/// Types of transformations
enum class TransformType {
    Translate,
    Rotate,
    Scale,
    Mirror
};

/// 2D transformation matrix (3x3 affine)
struct Transform2D {
    double m11 = 1.0, m12 = 0.0, m13 = 0.0;
    double m21 = 0.0, m22 = 1.0, m23 = 0.0;
    // m31 = 0, m32 = 0, m33 = 1 (implicit for affine)

    /// Identity transform
    static Transform2D identity();

    /// Translation transform
    static Transform2D translation(double dx, double dy);

    /// Rotation transform (angle in degrees, around origin)
    static Transform2D rotation(double angleDegrees);

    /// Rotation transform (angle in degrees, around center point)
    static Transform2D rotation(double angleDegrees, const Point2D& center);

    /// Scale transform (uniform, around origin)
    static Transform2D scale(double factor);

    /// Scale transform (non-uniform, around origin)
    static Transform2D scale(double sx, double sy);

    /// Scale transform (around center point)
    static Transform2D scale(double factor, const Point2D& center);

    /// Mirror transform (horizontal around X axis through center)
    static Transform2D mirrorHorizontal(const Point2D& center);

    /// Mirror transform (vertical around Y axis through center)
    static Transform2D mirrorVertical(const Point2D& center);

    /// Apply transform to a point
    Point2D apply(const Point2D& point) const;

    /// Apply transform to multiple points
    std::vector<Point2D> apply(const std::vector<Point2D>& points) const;

    /// Combine with another transform (this * other)
    Transform2D operator*(const Transform2D& other) const;
};

}  // namespace geometry
}  // namespace hobbycad

#endif  // HOBBYCAD_GEOMETRY_TYPES_H
