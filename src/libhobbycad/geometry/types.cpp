// =====================================================================
//  src/libhobbycad/geometry/types.cpp — Basic geometry types implementation
// =====================================================================
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include <hobbycad/geometry/types.h>

namespace hobbycad {
namespace geometry {

// =====================================================================
//  Arc Implementation
// =====================================================================

bool Arc::containsAngle(double angle) const
{
    // Normalize angles to [0, 360)
    double normStart = startAngle;
    while (normStart < 0) normStart += 360.0;
    while (normStart >= 360.0) normStart -= 360.0;

    double normAngle = angle;
    while (normAngle < 0) normAngle += 360.0;
    while (normAngle >= 360.0) normAngle -= 360.0;

    if (qFuzzyCompare(qAbs(sweepAngle), 360.0)) {
        return true;  // Full circle
    }

    double sweep = sweepAngle;
    if (sweep < 0) {
        // Negative sweep: going clockwise
        double endAngle = normStart + sweep;
        while (endAngle < 0) endAngle += 360.0;

        if (normStart > endAngle) {
            // Arc crosses 0°
            return normAngle <= normStart && normAngle >= endAngle;
        } else {
            return normAngle >= endAngle && normAngle <= normStart;
        }
    } else {
        // Positive sweep: going counter-clockwise
        double endAngle = normStart + sweep;
        while (endAngle >= 360.0) endAngle -= 360.0;

        if (normStart < endAngle) {
            return normAngle >= normStart && normAngle <= endAngle;
        } else {
            // Arc crosses 360°
            return normAngle >= normStart || normAngle <= endAngle;
        }
    }
}

QPointF Arc::startPoint() const
{
    double rad = qDegreesToRadians(startAngle);
    return center + QPointF(radius * qCos(rad), radius * qSin(rad));
}

QPointF Arc::endPoint() const
{
    double rad = qDegreesToRadians(startAngle + sweepAngle);
    return center + QPointF(radius * qCos(rad), radius * qSin(rad));
}

QPointF Arc::pointAt(double t) const
{
    double angle = startAngle + t * sweepAngle;
    double rad = qDegreesToRadians(angle);
    return center + QPointF(radius * qCos(rad), radius * qSin(rad));
}

// =====================================================================
//  BoundingBox Implementation
// =====================================================================

BoundingBox::BoundingBox(double x1, double y1, double x2, double y2)
    : minX(qMin(x1, x2))
    , minY(qMin(y1, y2))
    , maxX(qMax(x1, x2))
    , maxY(qMax(y1, y2))
    , valid(true)
{
}

BoundingBox::BoundingBox(const QRectF& rect)
    : minX(rect.left())
    , minY(rect.top())
    , maxX(rect.right())
    , maxY(rect.bottom())
    , valid(true)
{
}

BoundingBox::BoundingBox(const QPointF& point)
    : minX(point.x())
    , minY(point.y())
    , maxX(point.x())
    , maxY(point.y())
    , valid(true)
{
}

void BoundingBox::include(const QPointF& point)
{
    if (!valid) {
        minX = maxX = point.x();
        minY = maxY = point.y();
        valid = true;
    } else {
        minX = qMin(minX, point.x());
        minY = qMin(minY, point.y());
        maxX = qMax(maxX, point.x());
        maxY = qMax(maxY, point.y());
    }
}

void BoundingBox::include(const BoundingBox& other)
{
    if (!other.valid) return;

    if (!valid) {
        *this = other;
    } else {
        minX = qMin(minX, other.minX);
        minY = qMin(minY, other.minY);
        maxX = qMax(maxX, other.maxX);
        maxY = qMax(maxY, other.maxY);
    }
}

QPointF BoundingBox::center() const
{
    return QPointF((minX + maxX) / 2.0, (minY + maxY) / 2.0);
}

QRectF BoundingBox::toRect() const
{
    return QRectF(minX, minY, maxX - minX, maxY - minY);
}

bool BoundingBox::contains(const QPointF& point) const
{
    if (!valid) return false;
    return point.x() >= minX && point.x() <= maxX &&
           point.y() >= minY && point.y() <= maxY;
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
    double rad = qDegreesToRadians(angleDegrees);
    double c = qCos(rad);
    double s = qSin(rad);

    Transform2D t;
    t.m11 = c;
    t.m12 = -s;
    t.m21 = s;
    t.m22 = c;
    return t;
}

Transform2D Transform2D::rotation(double angleDegrees, const QPointF& center)
{
    // Translate to origin, rotate, translate back
    Transform2D t1 = translation(-center.x(), -center.y());
    Transform2D r = rotation(angleDegrees);
    Transform2D t2 = translation(center.x(), center.y());
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

Transform2D Transform2D::scale(double factor, const QPointF& center)
{
    Transform2D t1 = translation(-center.x(), -center.y());
    Transform2D s = scale(factor);
    Transform2D t2 = translation(center.x(), center.y());
    return t2 * s * t1;
}

Transform2D Transform2D::mirrorHorizontal(const QPointF& center)
{
    // Mirror around horizontal axis (flip Y)
    Transform2D t1 = translation(-center.x(), -center.y());
    Transform2D m;
    m.m22 = -1.0;
    Transform2D t2 = translation(center.x(), center.y());
    return t2 * m * t1;
}

Transform2D Transform2D::mirrorVertical(const QPointF& center)
{
    // Mirror around vertical axis (flip X)
    Transform2D t1 = translation(-center.x(), -center.y());
    Transform2D m;
    m.m11 = -1.0;
    Transform2D t2 = translation(center.x(), center.y());
    return t2 * m * t1;
}

QPointF Transform2D::apply(const QPointF& point) const
{
    return QPointF(
        m11 * point.x() + m12 * point.y() + m13,
        m21 * point.x() + m22 * point.y() + m23
    );
}

QVector<QPointF> Transform2D::apply(const QVector<QPointF>& points) const
{
    QVector<QPointF> result;
    result.reserve(points.size());
    for (const QPointF& p : points) {
        result.append(apply(p));
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
