// SPDX-License-Identifier: GPL-3.0-only
// HobbyCAD - screenmath.h
// Screen-space helpers the canvas and the tool handlers all need: the angle
// between two screen points in the two conventions the code uses, and the
// small center-marker cross. Each was hand-written dozens of times.

#pragma once

#include <hobbycad/units.h>

#include <QPainter>
#include <QPointF>

#include <cmath>

namespace hobbycad {

/// Angle in degrees of the direction from `from` to `to`, screen axes as they
/// are (y down). atan2 in degrees, nothing more.
inline double screenAngleDeg(const QPointF& from, const QPointF& to)
{
    return radiansToDegrees(std::atan2(to.y() - from.y(), to.x() - from.x()));
}

/// The same angle with the y axis flipped, which is QPainter's arc convention
/// (counter-clockwise positive, as on paper).
inline double painterAngleDeg(const QPointF& from, const QPointF& to)
{
    return radiansToDegrees(std::atan2(-(to.y() - from.y()), to.x() - from.x()));
}

/// A small "+" marker of half-size `size` pixels centered on `c`.
inline void paintCenterCross(QPainter& painter, const QPoint& c, int size)
{
    painter.drawLine(c.x() - size, c.y(), c.x() + size, c.y());
    painter.drawLine(c.x(), c.y() - size, c.x(), c.y() + size);
}
inline void paintCenterCross(QPainter& painter, const QPointF& c, int size)
{
    // Integer lines, as every caller drew them before.
    painter.drawLine(static_cast<int>(c.x() - size), static_cast<int>(c.y()),
                     static_cast<int>(c.x() + size), static_cast<int>(c.y()));
    painter.drawLine(static_cast<int>(c.x()), static_cast<int>(c.y() - size),
                     static_cast<int>(c.x()), static_cast<int>(c.y() + size));
}

}  // namespace hobbycad
