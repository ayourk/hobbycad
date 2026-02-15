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

#include <QPointF>
#include <QLineF>
#include <QRectF>
#include <QVector>
#include <QtMath>

#include <optional>

namespace hobbycad {
namespace geometry {

// =====================================================================
//  Constants
// =====================================================================

/// Default tolerance for geometric comparisons (in mm)
constexpr double DEFAULT_TOLERANCE = 1e-6;

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
    QPointF point;                ///< Intersection point (if intersects)
    double t1 = 0.0;              ///< Parameter on first line [0,1] if within segment
    double t2 = 0.0;              ///< Parameter on second line [0,1] if within segment
    bool withinSegment1 = false;  ///< Whether intersection is within first segment
    bool withinSegment2 = false;  ///< Whether intersection is within second segment
};

/// Result of a line-circle intersection
struct LineCircleIntersection {
    int count = 0;                ///< Number of intersections (0, 1, or 2)
    QPointF point1;               ///< First intersection point
    QPointF point2;               ///< Second intersection point
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
    QPointF point1;               ///< First intersection point
    QPointF point2;               ///< Second intersection point
};

/// Result of a line-arc intersection
struct LineArcIntersection {
    int count = 0;                ///< Number of intersections (0, 1, or 2)
    QPointF point1;               ///< First intersection point
    QPointF point2;               ///< Second intersection point
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
    QPointF center;
    double radius = 0.0;
    double startAngle = 0.0;   ///< Start angle in degrees
    double sweepAngle = 360.0; ///< Sweep angle in degrees (positive = CCW)

    /// Check if an angle (in degrees) is within the arc sweep
    bool containsAngle(double angle) const;

    /// Get the start point of the arc
    QPointF startPoint() const;

    /// Get the end point of the arc
    QPointF endPoint() const;

    /// Get point at parameter t (0 = start, 1 = end)
    QPointF pointAt(double t) const;
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
    explicit BoundingBox(const QRectF& rect);
    explicit BoundingBox(const QPointF& point);

    /// Expand to include a point
    void include(const QPointF& point);

    /// Expand to include another bounding box
    void include(const BoundingBox& other);

    /// Get center point
    QPointF center() const;

    /// Get width
    double width() const { return maxX - minX; }

    /// Get height
    double height() const { return maxY - minY; }

    /// Convert to QRectF
    QRectF toRect() const;

    /// Check if point is inside (inclusive)
    bool contains(const QPointF& point) const;

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
    static Transform2D rotation(double angleDegrees, const QPointF& center);

    /// Scale transform (uniform, around origin)
    static Transform2D scale(double factor);

    /// Scale transform (non-uniform, around origin)
    static Transform2D scale(double sx, double sy);

    /// Scale transform (around center point)
    static Transform2D scale(double factor, const QPointF& center);

    /// Mirror transform (horizontal around X axis through center)
    static Transform2D mirrorHorizontal(const QPointF& center);

    /// Mirror transform (vertical around Y axis through center)
    static Transform2D mirrorVertical(const QPointF& center);

    /// Apply transform to a point
    QPointF apply(const QPointF& point) const;

    /// Apply transform to multiple points
    QVector<QPointF> apply(const QVector<QPointF>& points) const;

    /// Combine with another transform (this * other)
    Transform2D operator*(const Transform2D& other) const;
};

}  // namespace geometry
}  // namespace hobbycad

#endif  // HOBBYCAD_GEOMETRY_TYPES_H
