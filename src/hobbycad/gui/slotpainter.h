// SPDX-License-Identifier: GPL-3.0-only
// HobbyCAD - slotpainter.h
// The outline of a slot as a QPainterPath, in screen space. The entity
// renderer (final drawing) and the slot tool (preview) each had a copy, and
// the copies had already drifted apart.

#pragma once

#include "screenmath.h"

#include <QPainterPath>
#include <QPointF>
#include <QRectF>

namespace hobbycad {

/// Arc slot: outer arc, end cap, inner arc back, start cap. Angles in
/// QPainter's degrees (see painterAngleDeg); `sweepAngle` already wrapped and
/// flipped as the caller wants it drawn.
inline QPainterPath arcSlotOutlinePath(const QPointF& center, double startAngle, double endAngle,
                                       double sweepAngle, double screenInnerRadius,
                                       double screenOuterRadius, double screenHalfWidth)
{
    QPainterPath path;
    QRectF outerRect(center.x() - screenOuterRadius, center.y() - screenOuterRadius,
                     screenOuterRadius * 2, screenOuterRadius * 2);
    path.arcMoveTo(outerRect, startAngle);
    path.arcTo(outerRect, startAngle, sweepAngle);

    // End cap: a semicircle from the outer arc's end to the inner arc's end.
    QPointF outerEnd = path.currentPosition();
    QRectF innerRect(center.x() - screenInnerRadius, center.y() - screenInnerRadius,
                     screenInnerRadius * 2, screenInnerRadius * 2);
    QPainterPath tempPath;
    tempPath.arcMoveTo(innerRect, endAngle);
    QPointF innerEnd = tempPath.currentPosition();
    QPointF capCenter = (outerEnd + innerEnd) / 2;
    double capRadius = screenHalfWidth;
    QRectF capRect(capCenter.x() - capRadius, capCenter.y() - capRadius,
                   capRadius * 2, capRadius * 2);
    double capStartAngle = painterAngleDeg(capCenter, outerEnd);
    double capSweep = (sweepAngle >= 0) ? 180 : -180;   // cap direction follows the sweep
    path.arcTo(capRect, capStartAngle, capSweep);

    // Inner arc back, then the start cap.
    path.arcTo(innerRect, endAngle, -sweepAngle);
    tempPath.arcMoveTo(outerRect, startAngle);
    QPointF outerStart = tempPath.currentPosition();
    tempPath.arcMoveTo(innerRect, startAngle);
    QPointF innerStart = tempPath.currentPosition();
    capCenter = (outerStart + innerStart) / 2;
    capRect = QRectF(capCenter.x() - capRadius, capCenter.y() - capRadius,
                     capRadius * 2, capRadius * 2);
    capStartAngle = painterAngleDeg(capCenter, innerStart);
    path.arcTo(capRect, capStartAngle, capSweep);
    path.closeSubpath();
    return path;
}

/// Linear slot: two sides and two semicircular ends about the cap centers.
inline QPainterPath linearSlotOutlinePath(const QPointF& p1, const QPointF& p2, double halfWidth)
{
    const double len = QLineF(p1, p2).length();
    const double dx = (p2.x() - p1.x()) / len;
    const double dy = (p2.y() - p1.y()) / len;
    const double px = -dy * halfWidth;   // perpendicular
    const double py = dx * halfWidth;
    const QPointF c1(p1.x() + px, p1.y() + py);
    const QPointF c2(p2.x() + px, p2.y() + py);
    const QPointF c4(p1.x() - px, p1.y() - py);
    const double startAngle2 = painterAngleDeg(p2, c2);
    const double startAngle1 = painterAngleDeg(p1, c4);

    QPainterPath path;
    path.moveTo(c1);
    path.lineTo(c2);
    QRectF arcRect2(p2.x() - halfWidth, p2.y() - halfWidth, halfWidth * 2, halfWidth * 2);
    path.arcTo(arcRect2, startAngle2, 180);
    path.lineTo(c4);
    QRectF arcRect1(p1.x() - halfWidth, p1.y() - halfWidth, halfWidth * 2, halfWidth * 2);
    path.arcTo(arcRect1, startAngle1, 180);
    path.closeSubpath();
    return path;
}

}  // namespace hobbycad
