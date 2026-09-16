// =====================================================================
//  src/libhobbycad/geometry/types.cpp — Basic geometry types implementation
// =====================================================================
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include <hobbycad/geometry/types.h>
#include <hobbycad/units.h>

#include <algorithm>
#include <cmath>

#include <hobbycad/math_constants.h>

namespace hobbycad {
namespace geometry {

// =====================================================================
//  Arc Implementation
// =====================================================================

bool Arc::containsAngle(double angle) const
{
    if (std::abs(sweepAngle) >= 360.0 - kExactEps) {
        return true;  // Full circle
    }
    auto norm = [](double a) {
        a = std::fmod(a, 360.0);
        if (a < 0) a += 360.0;
        return a;
    };
    const double start = norm(startAngle);
    const double a = norm(angle);
    // Offset of `a` from the start, measured IN the sweep direction, in [0,360).
    // This handles the 0/360 seam uniformly for both sweep signs. The old
    // split-by-"which side of 0 does it cross" logic got the negative-sweep case
    // wrong: an arc with sweep < 0 that spans across 0 (one hugging the +X axis)
    // reported a dead-on click as "not on the arc", so it could not be selected.
    const double eps = 1e-9;
    if (sweepAngle >= 0.0) {
        const double off = norm(a - start);          // CCW distance from the start
        return off <= sweepAngle + eps;
    }
    const double off = norm(start - a);              // CW distance from the start
    return off <= -sweepAngle + eps;
}

Point2D Arc::startPoint() const
{
    double rad = degreesToRadians(startAngle);
    return center + Point2D(radius * std::cos(rad), radius * std::sin(rad));
}

Point2D Arc::endPoint() const
{
    double rad = degreesToRadians(startAngle + sweepAngle);
    return center + Point2D(radius * std::cos(rad), radius * std::sin(rad));
}

Point2D Arc::pointAt(double t) const
{
    double angle = startAngle + t * sweepAngle;
    double rad = degreesToRadians(angle);
    return center + Point2D(radius * std::cos(rad), radius * std::sin(rad));
}

// =====================================================================
//  BoundingBox Implementation
// =====================================================================

BoundingBox::BoundingBox(double x1, double y1, double x2, double y2)
    : minX(std::min(x1, x2))
    , minY(std::min(y1, y2))
    , maxX(std::max(x1, x2))
    , maxY(std::max(y1, y2))
    , valid(true)
{
}

BoundingBox::BoundingBox(const Rect2D& rect)
    : minX(rect.left())
    , minY(rect.top())
    , maxX(rect.right())
    , maxY(rect.bottom())
    , valid(true)
{
}

BoundingBox::BoundingBox(const Point2D& point)
    : minX(point.x)
    , minY(point.y)
    , maxX(point.x)
    , maxY(point.y)
    , valid(true)
{
}

void BoundingBox::include(const Point2D& point)
{
    if (!valid) {
        minX = maxX = point.x;
        minY = maxY = point.y;
        valid = true;
    } else {
        minX = std::min(minX, point.x);
        minY = std::min(minY, point.y);
        maxX = std::max(maxX, point.x);
        maxY = std::max(maxY, point.y);
    }
}

void BoundingBox::include(const BoundingBox& other)
{
    if (!other.valid) return;

    if (!valid) {
        *this = other;
    } else {
        minX = std::min(minX, other.minX);
        minY = std::min(minY, other.minY);
        maxX = std::max(maxX, other.maxX);
        maxY = std::max(maxY, other.maxY);
    }
}

Point2D BoundingBox::center() const
{
    return Point2D((minX + maxX) / 2.0, (minY + maxY) / 2.0);
}

Rect2D BoundingBox::toRect() const
{
    return Rect2D(minX, minY, maxX - minX, maxY - minY);
}

bool BoundingBox::contains(const Point2D& point) const
{
    if (!valid) return false;
    return point.x >= minX && point.x <= maxX &&
           point.y >= minY && point.y <= maxY;
}

bool BoundingBox::intersects(const BoundingBox& other) const
{
    if (!valid || !other.valid) return false;
    return !(maxX < other.minX || other.maxX < minX ||
             maxY < other.minY || other.maxY < minY);
}

// =====================================================================
//  Transform2D Implementation
// =====================================================================

Transform2D Transform2D::identity()
{
    return Transform2D();
}

Transform2D Transform2D::translation(double dx, double dy)
{
    Transform2D t;
    t.m13 = dx;
    t.m23 = dy;
    return t;
}

Transform2D Transform2D::rotation(double angleDegrees)
{
    double rad = degreesToRadians(angleDegrees);
    double c = std::cos(rad);
    double s = std::sin(rad);

    Transform2D t;
    t.m11 = c;
    t.m12 = -s;
    t.m21 = s;
    t.m22 = c;
    return t;
}

Transform2D Transform2D::rotation(double angleDegrees, const Point2D& center)
{
    // Translate to origin, rotate, translate back
    Transform2D t1 = translation(-center.x, -center.y);
    Transform2D r = rotation(angleDegrees);
    Transform2D t2 = translation(center.x, center.y);
    return t2 * r * t1;
}

Transform2D Transform2D::scale(double factor)
{
    return scale(factor, factor);
}

Transform2D Transform2D::scale(double sx, double sy)
{
    Transform2D t;
    t.m11 = sx;
    t.m22 = sy;
    return t;
}

Transform2D Transform2D::scale(double factor, const Point2D& center)
{
    Transform2D t1 = translation(-center.x, -center.y);
    Transform2D s = scale(factor);
    Transform2D t2 = translation(center.x, center.y);
    return t2 * s * t1;
}

Transform2D Transform2D::mirrorHorizontal(const Point2D& center)
{
    // Mirror around horizontal axis (flip Y)
    Transform2D t1 = translation(-center.x, -center.y);
    Transform2D m;
    m.m22 = -1.0;
    Transform2D t2 = translation(center.x, center.y);
    return t2 * m * t1;
}

Transform2D Transform2D::mirrorVertical(const Point2D& center)
{
    // Mirror around vertical axis (flip X)
    Transform2D t1 = translation(-center.x, -center.y);
    Transform2D m;
    m.m11 = -1.0;
    Transform2D t2 = translation(center.x, center.y);
    return t2 * m * t1;
}

Point2D Transform2D::apply(const Point2D& point) const
{
    return Point2D(
        m11 * point.x + m12 * point.y + m13,
        m21 * point.x + m22 * point.y + m23
    );
}

std::vector<Point2D> Transform2D::apply(const std::vector<Point2D>& points) const
{
    std::vector<Point2D> result;
    result.reserve(points.size());
    for (const Point2D& p : points) {
        result.push_back(apply(p));
    }
    return result;
}

Transform2D Transform2D::operator*(const Transform2D& other) const
{
    Transform2D result;
    result.m11 = m11 * other.m11 + m12 * other.m21;
    result.m12 = m11 * other.m12 + m12 * other.m22;
    result.m13 = m11 * other.m13 + m12 * other.m23 + m13;
    result.m21 = m21 * other.m11 + m22 * other.m21;
    result.m22 = m21 * other.m12 + m22 * other.m22;
    result.m23 = m21 * other.m13 + m22 * other.m23 + m23;
    return result;
}

}  // namespace geometry
}  // namespace hobbycad
