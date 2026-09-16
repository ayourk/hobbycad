// =====================================================================
//  src/hobbycad/gui/tools/rectangletoolhandler.cpp
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#include "rectangletoolhandler.h"
#include "../screenmath.h"
#include <hobbycad/units.h>
#include "../sketchcanvas.h"
#include <QCoreApplication>
#include <optional>
#include <hobbycad/geometry/utils.h>
#include <QtMath>
#include <QLineF>
#include <QPen>
#include <QPainter>
#include <QMouseEvent>
#include <QPointF>

#include <cmath>

namespace hobbycad {

using RectMode = SketchCanvas::RectMode;

namespace {
/// True for the two modes that are staged over three points; the others place
/// with two clicks through the shared path in mousePressEvent.
bool isStaged(RectMode m)
{
    return m == RectMode::ThreePoint || m == RectMode::Parallelogram;
}
}  // namespace

namespace {
// Second edge of a 3-point rectangle or a parallelogram under locked dimension
// fields: the locked length, and the locked inside angle on whichever side of
// the first edge the cursor is. Returns `toward` unchanged when degenerate.
// Stage 1 of the 3-point rectangle and the parallelogram: the first edge with
// its length and angle fields.
void drawFirstEdgeStage(SketchCanvas& canvas, QPainter& painter, const QPointF& p1, const QPointF& p2)
{
    QPoint sp1 = canvas.toScreen(p1);
    QPoint sp2 = canvas.toScreen(p2);
    painter.drawLine(sp1, sp2);

    double edgeLen = QLineF(p1, p2).length();
    if (edgeLen > 0.1) {
        if (canvas.activeDimField() >= 0 && canvas.dimFieldCount() >= 2) {
            double dx = p2.x() - p1.x();
            double dy = p2.y() - p1.y();
            double angleDeg = radiansToDegrees(std::atan2(dy, dx));
            canvas.setDimFieldValue(0, edgeLen);
            canvas.setDimFieldValue(1, angleDeg);

            QPointF midPt = (QPointF(sp1) + QPointF(sp2)) / 2.0;
            double screenAngle = screenAngleDeg(sp1, sp2);
            bool flipped = (screenAngle > 90 || screenAngle < -90);
            if (flipped) screenAngle += 180;

            canvas.paintDimInputField(painter, midPt + QPointF(0, 18), 0, screenAngle);
            canvas.paintDimInputField(painter, midPt + QPointF(0, 38), 1, screenAngle);
        } else {
            canvas.paintPreviewDimension(painter, sp1, sp2, edgeLen);
        }
    }
}

// Orange markers on the corners placed so far.
void drawPlacedCorners(SketchCanvas& canvas, QPainter& painter)
{
    painter.save();
    painter.setPen(QPen(QColor(255, 140, 0), 1));
    painter.setBrush(QColor(255, 140, 0));
    for (int i = 0; i < canvas.previewPointCount(); ++i) {
        QPoint sp = canvas.toScreen(canvas.previewPoints()[i]);
        painter.drawEllipse(sp, 4, 4);
    }
    painter.restore();
}
}  // namespace

void RectangleToolHandler::constrainThreePoint(const SketchCanvas& canvas, QPointF& snapped)
{

    // Apply locked dimension constraints
    int pStage = canvas.previewPointCount();
    if (pStage == 1) {
        QPointF p1 = canvas.previewPoint(0);
        double lockedLen = canvas.lockedDim(0);
        double lockedAng = canvas.lockedDim(1);
        if (lockedLen > 0 || lockedAng != -1.0) {
            // Locked length/angle placement now lives in libhobbycad, so a
            // second front-end need not reimplement it.
            snapped = geometry::applyPolarLock(p1, snapped, lockedLen, lockedAng);
        }
    } else if (pStage >= 2) {
        double lockedW = canvas.lockedDim(0);
        if (lockedW > 0) {
            QPointF p1 = canvas.previewPoint(0);
            QPointF p2 = canvas.previewPoint(1);
            QPointF edge = p2 - p1;
            double edgeLen = geometry::length(edge);
            if (edgeLen > 0.001) {
                QPointF edgeDir = geometry::normalize(edge);
                QPointF perpDir = geometry::perpendicular(edgeDir);
                QPointF toMouse = snapped - p1;
                double perpDot = toMouse.x() * perpDir.x() + toMouse.y() * perpDir.y();
                double sign = (perpDot >= 0) ? 1.0 : -1.0;
                double edgeDot = toMouse.x() * edgeDir.x() + toMouse.y() * edgeDir.y();
                snapped = p1 + edgeDir * edgeDot + perpDir * sign * lockedW;
            }
        }
    }
}

void RectangleToolHandler::constrainParallelogram(const SketchCanvas& canvas, QPointF& snapped)
{

    // Apply locked dimension constraints
    int pStage = canvas.previewPointCount();
    if (pStage == 1) {
        QPointF p1 = canvas.previewPoint(0);
        double lockedLen = canvas.lockedDim(0);
        double lockedAng = canvas.lockedDim(1);
        if (lockedLen > 0 || lockedAng != -1.0) {
            // Locked length/angle placement now lives in libhobbycad, so a
            // second front-end need not reimplement it.
            snapped = geometry::applyPolarLock(p1, snapped, lockedLen, lockedAng);
        }
    } else if (pStage >= 2) {
        QPointF p1 = canvas.previewPoint(0);
        QPointF p2 = canvas.previewPoint(1);
        double lockedLen = canvas.lockedDim(0);
        double lockedAng = canvas.lockedDim(1);
        if (lockedLen > 0 || lockedAng != -1.0) {
            snapped = geometry::applyInsideAngleLock(p1, p2, snapped, lockedLen, lockedAng);
        }
    }
}

bool RectangleToolHandler::mousePress(SketchCanvas& canvas, QMouseEvent* event, const QPointF& world)
{
    const RectMode mode = canvas.rectMode();
    if (!isStaged(mode) || !canvas.isDrawing()) return false;

    canvas.beginDragDetection(event->pos());   // per STAGE, not per entity

    QPointF snapped = canvas.snapToGeometry(world);
    if (mode == RectMode::ThreePoint) constrainThreePoint(canvas, snapped);
    else                              constrainParallelogram(canvas, snapped);

    canvas.appendPlacementPoint(snapped);
    if (canvas.pendingEntity().points.size() >= 3) canvas.commitEntity();
    else                                           canvas.refreshDimFields();
    return true;
}

bool RectangleToolHandler::mouseRelease(SketchCanvas& canvas, QMouseEvent*, const QPointF& world)
{
    const RectMode mode = canvas.rectMode();
    if (!isStaged(mode) || !canvas.isDrawing()) return false;

    if (!canvas.wasDragged()) {
        // Click placement: press already appended the point.
        if (canvas.pendingEntity().points.size() >= 3) canvas.commitEntity();
        return true;
    }

    QPointF snapped = canvas.snapToGeometry(world);
    if (mode == RectMode::ThreePoint) constrainThreePoint(canvas, snapped);
    else                              constrainParallelogram(canvas, snapped);

    canvas.appendPlacementPoint(snapped);
    if (canvas.pendingEntity().points.size() >= 3) canvas.commitEntity();
    else                                           canvas.refreshDimFields();
    return true;
}


bool RectangleToolHandler::initDimFields(SketchCanvas& canvas)
{
    const int stage = canvas.previewPointCount();

    switch (canvas.rectMode()) {
    case RectMode::Corner:
    case RectMode::Center:
        if (stage >= 1) {
            canvas.addDimField(tr("Width"), false);
            canvas.addDimField(tr("Height"), false);
        }
        break;
    case RectMode::ThreePoint:
        if (stage == 1) {
            canvas.addDimField(tr("Edge Length"), false);
            canvas.addDimField(tr("Edge Angle"), true);
        } else if (stage >= 2) {
            canvas.addDimField(tr("Width"), false);
        }
        break;
    case RectMode::Parallelogram:
        if (stage == 1) {
            canvas.addDimField(tr("Edge1"), false);
            canvas.addDimField(tr("Edge1 Angle"), true);
        } else if (stage >= 2) {
            canvas.addDimField(tr("Edge2"), false);
            canvas.addDimField(tr("Edge2 Angle"), true);
        }
        break;
    }
    return true;
}

QString RectangleToolHandler::hint(const SketchCanvas& canvas) const
{
    const int stage = canvas.previewPointCount();

    switch (canvas.rectMode()) {
    case RectMode::Corner:
        return stage < 1 ? tr("Rectangle: click the first corner")
                         : tr("Rectangle: click the opposite corner, or type a width");
    case RectMode::Center:
        return stage < 1 ? tr("Rectangle: click the center")
                         : tr("Rectangle: click a corner, or type a width");
    case RectMode::ThreePoint:
        if (stage < 1) return tr("Rectangle (3-point): click the first corner");
        if (stage < 2) return tr("Rectangle (3-point): click the end of the first edge");
        return tr("Rectangle (3-point): click to set the width, or type one");
    case RectMode::Parallelogram:
        if (stage < 1) return tr("Parallelogram: click the first corner");
        if (stage < 2) return tr("Parallelogram: click the end of the first edge");
        return tr("Parallelogram: click the end of the second edge");
    }
    return {};
}


// drawPreview, 3-point (angled) rectangle mode.
void RectangleToolHandler::drawThreePointPreview(SketchCanvas& canvas, QPainter& painter)
{
    // 3-Point mode: draw angled rectangle
    // Point 1: first corner, Point 2: second corner (defines first edge)
    // Point 3 (or mouse): perpendicular offset (defines width)
    QPointF p1 = canvas.previewPoint(0);
    QPointF p2 = (canvas.previewPointCount() >= 2) ? canvas.previewPoint(1) : canvas.currentMouseWorld();
    QPointF p3 = canvas.currentMouseWorld();

    if (canvas.previewPointCount() < 2) {
        drawFirstEdgeStage(canvas, painter, p1, p2);
    } else {
        // Have two corners, now defining width
        // Calculate the direction of the first edge
        QPointF edge = p2 - p1;
        double edgeLen = QLineF(p1, p2).length();
        if (edgeLen > 0.01) {
            // Normalize edge direction
            QPointF edgeDir = geometry::normalize(edge);
            // Perpendicular direction (rotate 90 degrees CCW)
            QPointF perpDir = geometry::perpendicular(edgeDir);

            // Project mouse position onto perpendicular direction
            QPointF toMouse = p3 - p1;
            double perpDist = toMouse.x() * perpDir.x() + toMouse.y() * perpDir.y();

            // Calculate all four corners
            QPointF c1 = p1;
            QPointF c2 = p2;
            QPointF c3 = p2 + perpDir * perpDist;
            QPointF c4 = p1 + perpDir * perpDist;

            // Draw the rectangle
            QPoint sc1 = canvas.toScreen(c1);
            QPoint sc2 = canvas.toScreen(c2);
            QPoint sc3 = canvas.toScreen(c3);
            QPoint sc4 = canvas.toScreen(c4);

            painter.drawLine(sc1, sc2);
            painter.drawLine(sc2, sc3);
            painter.drawLine(sc3, sc4);
            painter.drawLine(sc4, sc1);

            // Draw dimensions
            canvas.paintPreviewDimension(painter, sc1, sc2, edgeLen);  // Fixed edge from stage 1
            double width = std::abs(perpDist);
            if (width > 0.1) {
                if (canvas.activeDimField() >= 0 && canvas.dimFieldCount() >= 1) {
                    canvas.setDimFieldValue(0, width);
                    QPointF midEdge2 = (QPointF(sc2) + QPointF(sc3)) / 2.0;
                    double screenAngle2 = screenAngleDeg(sc2, sc3);
                    bool flipped2 = (screenAngle2 > 90 || screenAngle2 < -90);
                    if (flipped2) screenAngle2 += 180;
                    canvas.paintDimInputField(painter, midEdge2 + QPointF(0, 18), 0, screenAngle2);
                } else {
                    canvas.paintPreviewDimension(painter, sc2, sc3, width);
                }
            }
        }
    }

    drawPlacedCorners(canvas, painter);
}

// drawPreview, parallelogram mode.
void RectangleToolHandler::drawParallelogramPreview(SketchCanvas& canvas, QPainter& painter)
{
    // Parallelogram mode: p1 -> p2 -> p3 -> p4, where p4 = p1 + (p3 - p2)
    QPointF p1 = canvas.previewPoint(0);
    QPointF p2 = (canvas.previewPointCount() >= 2) ? canvas.previewPoint(1) : canvas.currentMouseWorld();
    QPointF p3 = canvas.currentMouseWorld();

    if (canvas.previewPointCount() < 2) {
        drawFirstEdgeStage(canvas, painter, p1, p2);
    } else {
        // Have two corners, now defining third (and computing fourth)
        QPointF p4 = p1 + (p3 - p2);  // Complete the parallelogram

        double edge1Len = QLineF(p1, p2).length();
        double edge2Len = QLineF(p2, p3).length();

        // Calculate the INSIDE angle at p2 (angle between rays p2->p1 and p2->p3)
        QPointF toP1 = p1 - p2;  // Vector from p2 to p1
        QPointF toP3 = p3 - p2;  // Vector from p2 to p3
        double insideAngleDeg = 0.0;
        if (edge1Len > 0.001 && edge2Len > 0.001) {
            insideAngleDeg = geometry::angleBetween(toP1, toP3);
        }

        // Draw the parallelogram
        QPoint sp1 = canvas.toScreen(p1);
        QPoint sp2 = canvas.toScreen(p2);
        QPoint sp3 = canvas.toScreen(p3);
        QPoint sp4 = canvas.toScreen(p4);

        painter.drawLine(sp1, sp2);
        painter.drawLine(sp2, sp3);
        painter.drawLine(sp3, sp4);
        painter.drawLine(sp4, sp1);

        // Draw edge1 dimension (already placed, not editable in stage 2)
        canvas.paintPreviewDimension(painter, sp1, sp2, edge1Len);

        // Draw edge2 dimension and angle via dim input fields
        if (edge2Len > 0.1 && canvas.activeDimField() >= 0 && canvas.dimFieldCount() >= 2) {
            // Update current values
            canvas.setDimFieldValue(0, edge2Len);
            canvas.setDimFieldValue(1, insideAngleDeg);

            // Edge2 length field along the sp2-sp3 edge
            QPointF midEdge2 = (QPointF(sp2) + QPointF(sp3)) / 2.0;
            double screenAngle2 = screenAngleDeg(sp2, sp3);
            bool flipped2 = (screenAngle2 > 90 || screenAngle2 < -90);
            if (flipped2) screenAngle2 += 180;
            canvas.paintDimInputField(painter, midEdge2 + QPointF(0, 18), 0, screenAngle2);

            // Draw angle arc (decorative) at p2
            if (edge1Len > 0.1) {
                painter.save();
                painter.setPen(QPen(QColor(255, 140, 0), 1));
                double arcRadius = qMin(30.0, qMin(edge1Len, edge2Len) * canvas.zoomFactor() * 0.3);
                double angleStart = painterAngleDeg(sp2, sp1);
                double angleEnd = painterAngleDeg(sp2, sp3);
                double angleSweep = angleEnd - angleStart;
                angleSweep = hobbycad::geometry::wrapSweepDeg(angleSweep);
                QRectF arcRect(sp2.x() - arcRadius, sp2.y() - arcRadius,
                               arcRadius * 2, arcRadius * 2);
                painter.drawArc(arcRect, static_cast<int>(angleStart * 16),
                               static_cast<int>(angleSweep * 16));
                painter.restore();

                // Angle dim input field at label position
                double labelAngle = degreesToRadians(angleStart + angleSweep / 2.0);
                QPointF labelPos(sp2.x() + (arcRadius + 15) * std::cos(-labelAngle),
                                 sp2.y() + (arcRadius + 15) * std::sin(-labelAngle));
                canvas.paintDimInputField(painter, labelPos, 1, 0.0);
            }
        } else if (edge2Len > 0.1) {
            // Fallback: no dim fields available
            canvas.paintPreviewDimension(painter, sp2, sp3, edge2Len);
            // Draw inside angle indicator at p2
            if (edge1Len > 0.1) {
                painter.save();
                painter.setPen(QPen(QColor(255, 140, 0), 1));
                double arcRadius = qMin(30.0, qMin(edge1Len, edge2Len) * canvas.zoomFactor() * 0.3);
                double startAngle = painterAngleDeg(sp2, sp1);
                double endAngle = painterAngleDeg(sp2, sp3);
                double sweepAngle = endAngle - startAngle;
                sweepAngle = hobbycad::geometry::wrapSweepDeg(sweepAngle);
                QRectF arcRect(sp2.x() - arcRadius, sp2.y() - arcRadius,
                               arcRadius * 2, arcRadius * 2);
                painter.drawArc(arcRect, static_cast<int>(startAngle * 16),
                               static_cast<int>(sweepAngle * 16));
                QString angleText = QString::fromStdString(formatAngle(insideAngleDeg));
                QFont font = painter.font();
                font.setPointSize(9);
                painter.setFont(font);
                QFontMetrics fm(font);
                QRect textRect = fm.boundingRect(angleText);
                double labelAngle = degreesToRadians(startAngle + sweepAngle / 2.0);
                QPointF labelPos(sp2.x() + (arcRadius + 15) * std::cos(-labelAngle),
                                 sp2.y() + (arcRadius + 15) * std::sin(-labelAngle));
                QRectF bgRect(labelPos.x() - textRect.width() / 2 - 2,
                              labelPos.y() - textRect.height() / 2 - 1,
                              textRect.width() + 4, textRect.height() + 2);
                painter.fillRect(bgRect, QColor(255, 255, 255, 200));
                painter.drawText(bgRect, Qt::AlignCenter, angleText);
                painter.restore();
            }
        }
    }

    drawPlacedCorners(canvas, painter);
}

// drawPreview, Corner and Center modes.
void RectangleToolHandler::drawCornerOrCenterPreview(SketchCanvas& canvas, QPainter& painter)
{
    // Corner or Center mode - use constrained corners from updateEntity
    QPointF corner1, corner2;
    if (canvas.rectMode() == SketchCanvas::RectMode::Center) {
        // Center mode: pendingEntity stores [center, corner1, corner2]
        if (canvas.pendingEntity().points.size() >= 3) {
            corner1 = canvas.pendingEntity().points[1];
            corner2 = canvas.pendingEntity().points[2];
        } else {
            QPointF center = canvas.previewPoint(0);
            QPointF delta = canvas.currentMouseWorld() - center;
            corner1 = center - delta;
            corner2 = canvas.currentMouseWorld();
        }
    } else if (canvas.pendingEntity().points.size() >= 4) {
        // Corner mode, rotated (both W+H locked): 4 corners
        QPointF c0 = canvas.pendingEntity().points[0];
        QPointF c1 = canvas.pendingEntity().points[1];
        QPointF c2 = canvas.pendingEntity().points[2];
        QPointF c3 = canvas.pendingEntity().points[3];
        QPoint s0 = canvas.toScreen(c0);
        QPoint s1 = canvas.toScreen(c1);
        QPoint s2 = canvas.toScreen(c2);
        QPoint s3 = canvas.toScreen(c3);

        QPolygon poly;
        poly << s0 << s1 << s2 << s3 << s0;
        painter.drawPolyline(poly);

        // Width and height from locked dims
        double width = canvas.lockedDim(0);
        double height = canvas.lockedDim(1);
        if (width > 0 && height > 0 && canvas.activeDimField() >= 0 && canvas.dimFieldCount() >= 2) {
            canvas.setDimFieldValue(0, width);
            canvas.setDimFieldValue(1, height);
            // Width label along edge c0→c1
            QPointF wMid = (QPointF(s0) + QPointF(s1)) / 2.0;
            QPointF wDir = QPointF(s1) - QPointF(s0);
            double wAngle = geometry::vectorAngle(wDir);
            if (wAngle > 90.0)  wAngle -= 180.0;
            if (wAngle < -90.0) wAngle += 180.0;
            // Offset perpendicular to the edge (outward)
            QPointF wPerp = geometry::perpendicular(wDir);
            double wLen = geometry::length(wPerp);
            if (wLen > geometry::kDegenerateLen) wPerp /= wLen;
            canvas.paintDimInputField(painter, wMid - wPerp * 18, 0, wAngle);
            // Height label along edge c0→c3
            QPointF hMid = (QPointF(s0) + QPointF(s3)) / 2.0;
            QPointF hDir = QPointF(s3) - QPointF(s0);
            double hAngle = geometry::vectorAngle(hDir);
            if (hAngle > 90.0)  hAngle -= 180.0;
            if (hAngle < -90.0) hAngle += 180.0;
            QPointF hPerp = geometry::perpendicular(hDir);
            double hLen = geometry::length(hPerp);
            if (hLen > geometry::kDegenerateLen) hPerp /= hLen;
            canvas.paintDimInputField(painter, hMid - hPerp * 40, 1, hAngle);
        }
    } else {
        // Corner mode, axis-aligned: 2 points
        corner1 = canvas.previewPoint(0);
        corner2 = (canvas.pendingEntity().points.size() >= 2)
            ? QPointF(canvas.pendingEntity().points[1]) : canvas.currentMouseWorld();
    }

    // Draw axis-aligned rectangle (2-point case and Center mode)
    if (canvas.pendingEntity().points.size() < 4 || canvas.rectMode() == SketchCanvas::RectMode::Center) {
        QPoint p1 = canvas.toScreen(corner1);
        QPoint p2 = canvas.toScreen(corner2);
        QRect rect = QRect(p1, p2).normalized();
        painter.drawRect(rect);

        // Draw center marker in center mode
        if (canvas.rectMode() == SketchCanvas::RectMode::Center) {
            QPoint centerScreen = canvas.toScreen(canvas.previewPoint(0));
            painter.save();
            painter.setPen(QPen(QColor(255, 140, 0), 1));
            paintCenterCross(painter, centerScreen, 5);
            painter.restore();
        }

        // Draw width and height dimensions
        double width = std::abs(corner2.x() - corner1.x());
        double height = std::abs(corner2.y() - corner1.y());
        if (width > 0.1 || height > 0.1) {
            if (canvas.activeDimField() >= 0 && canvas.dimFieldCount() >= 2) {
                canvas.setDimFieldValue(0, width);
                canvas.setDimFieldValue(1, height);
                // Width below bottom edge
                QPointF bottomMid((rect.left() + rect.right()) / 2.0, rect.bottom() + 18);
                canvas.paintDimInputField(painter, bottomMid, 0);
                // Height along right edge
                QPointF rightMid(rect.right() + 40, (rect.top() + rect.bottom()) / 2.0);
                canvas.paintDimInputField(painter, rightMid, 1);
            } else {
                if (width > 0.1) {
                    QPoint bottomLeft(rect.left(), rect.bottom());
                    QPoint bottomRight(rect.right(), rect.bottom());
                    canvas.paintPreviewDimension(painter, bottomLeft, bottomRight, width);
                }
                if (height > 0.1) {
                    QPoint topRight(rect.right(), rect.top());
                    QPoint bottomRight2(rect.right(), rect.bottom());
                    canvas.paintPreviewDimension(painter, topRight, bottomRight2, height);
                }
            }
        }
    }
}

bool RectangleToolHandler::drawPreview(SketchCanvas& canvas, QPainter& painter)
{
    if (canvas.previewPointCount() == 0) return true;
    if (canvas.rectMode() == SketchCanvas::RectMode::ThreePoint) {
        drawThreePointPreview(canvas, painter);
    } else if (canvas.rectMode() == SketchCanvas::RectMode::Parallelogram) {
        drawParallelogramPreview(canvas, painter);
    } else {
        drawCornerOrCenterPreview(canvas, painter);
    }
    return true;
}


bool RectangleToolHandler::updateEntity(SketchCanvas& canvas, const QPointF& pos)
{
    if (canvas.rectMode() == SketchCanvas::RectMode::Center) {
        // Center mode: point[0] is center, compute opposite corners
        if (!canvas.pendingEntityRef().points.empty()) {
            QPointF center = canvas.pendingEntityRef().points[0];
            // Compute delta from center to mouse position
            QPointF delta = pos - center;
            // Apply locked dimensions
            double lockedW = canvas.lockedDim(0);
            double lockedH = canvas.lockedDim(1);
            if (lockedW > 0) delta.setX(delta.x() >= 0 ? lockedW / 2.0 : -lockedW / 2.0);
            if (lockedH > 0) delta.setY(delta.y() >= 0 ? lockedH / 2.0 : -lockedH / 2.0);
            // Opposite corner mirrors across center
            QPointF corner1 = center - delta;
            QPointF corner2 = center + delta;
            // Store as corner-to-corner (points[0] and points[1] are opposite corners)
            if (canvas.pendingEntityRef().points.size() > 2) {
                canvas.pendingEntityRef().points[1] = corner1;
                canvas.pendingEntityRef().points[2] = corner2;
            } else if (canvas.pendingEntityRef().points.size() > 1) {
                canvas.pendingEntityRef().points[1] = corner1;
                canvas.pendingEntityRef().points.push_back(corner2);
            } else {
                canvas.pendingEntityRef().points.push_back(corner1);
                canvas.pendingEntityRef().points.push_back(corner2);
            }
        }
    } else if (canvas.rectMode() == SketchCanvas::RectMode::ThreePoint) {
        // 3-Point mode: apply locked dimension constraints to preview position.
        // Override canvas.currentMouseWorld() so the preview draws at the constrained position.
        int stage = canvas.previewPointCount();
        if (stage == 1) {
            // Stage 1: defining edge (p1 → p2). Same math as Line/Parallelogram.
            QPointF p1 = canvas.previewPoint(0);
            double lockedLen = canvas.lockedDim(0);   // Edge Length
            double lockedAng = canvas.lockedDim(1);   // Edge Angle
            if (lockedLen > 0 || lockedAng != -1.0) {
                // Locked length/angle placement now lives in libhobbycad.
                canvas.currentMouseWorld() = geometry::applyPolarLock(p1, pos, lockedLen, lockedAng);
            }
        } else if (stage >= 2) {
            // Stage 2: defining width (perpendicular offset from edge).
            // Preview projects canvas.currentMouseWorld() onto perpendicular of p1-p2.
            // Locked width → set canvas.currentMouseWorld() so projection gives locked width.
            double lockedW = canvas.lockedDim(0);  // Width
            if (lockedW > 0) {
                QPointF p1 = canvas.previewPoint(0);
                QPointF p2 = canvas.previewPoint(1);
                QPointF edge = p2 - p1;
                double edgeLen = geometry::length(edge);
                if (edgeLen > 0.001) {
                    QPointF edgeDir = geometry::normalize(edge);
                    QPointF perpDir = geometry::perpendicular(edgeDir);
                    QPointF toMouse = pos - p1;
                    double perpDot = toMouse.x() * perpDir.x() + toMouse.y() * perpDir.y();
                    double sign = (perpDot >= 0) ? 1.0 : -1.0;
                    // Keep edge-parallel position from mouse
                    double edgeDot = toMouse.x() * edgeDir.x() + toMouse.y() * edgeDir.y();
                    canvas.currentMouseWorld() = p1 + edgeDir * edgeDot + perpDir * sign * lockedW;
                }
            }
        }
    } else if (canvas.rectMode() == SketchCanvas::RectMode::Parallelogram) {
        // Parallelogram mode: apply locked dimension constraints to preview position.
        // Override canvas.currentMouseWorld() so the preview draws at the constrained position.
        // Don't modify canvas.pendingEntityRef().points here; that breaks click-click mode.
        int stage = canvas.previewPointCount();
        if (stage == 1) {
            // Stage 1: defining edge1 (p1 → p2)
            QPointF p1 = canvas.previewPoint(0);
            double lockedLen = canvas.lockedDim(0);   // Edge1
            double lockedAng = canvas.lockedDim(1);   // Edge1 Angle
            if (lockedLen > 0 || lockedAng != -1.0) {
                // Locked length/angle placement now lives in libhobbycad.
                canvas.currentMouseWorld() = geometry::applyPolarLock(p1, pos, lockedLen, lockedAng);
            }
        } else if (stage >= 2) {
            // Stage 2: defining edge2 (p2 → p3)
            QPointF p1 = canvas.previewPoint(0);
            QPointF p2 = canvas.previewPoint(1);
            double lockedLen = canvas.lockedDim(0);   // Edge2
            double lockedAng = canvas.lockedDim(1);   // Edge2 Angle (inside angle at p2)
            if (lockedLen > 0 || lockedAng != -1.0) {
                canvas.currentMouseWorld() = geometry::applyInsideAngleLock(p1, p2, pos, lockedLen, lockedAng);
            }
        }
    } else {
        // Corner mode: standard corner-to-corner
        double lockedW = canvas.lockedDim(0);  // field 0 = Width
        double lockedH = canvas.lockedDim(1);  // field 1 = Height

        if (lockedW > 0 && lockedH > 0 && m_bothLocked) {
            // Both locked: user rotates the rectangle around the first corner.
            // Rotation is relative to the axis-aligned state at lock time,
            // so the rectangle starts axis-aligned and rotates as the mouse moves.
            QPointF origin = canvas.previewPoint(0);
            QPointF delta = pos - origin;
            double currentAngle = std::atan2(delta.y(), delta.x());
            double rotation = currentAngle - m_lockRefAngle;
            double wAngle = m_lockWidthAngle + rotation;
            double hAngle = m_lockHeightAngle + rotation;
            QPointF wDir(std::cos(wAngle), std::sin(wAngle));
            QPointF hDir(std::cos(hAngle), std::sin(hAngle));
            // 4 corners: origin, along width, diagonal, along height
            QPointF p0 = origin;
            QPointF p1 = origin + wDir * lockedW;
            QPointF p2 = p1 + hDir * lockedH;
            QPointF p3 = origin + hDir * lockedH;
            // Store all 4 corners for rotated rectangle
            while (canvas.pendingEntityRef().points.size() < 4)
                canvas.pendingEntityRef().points.push_back(QPointF());
            canvas.pendingEntityRef().points[0] = p0;
            canvas.pendingEntityRef().points[1] = p1;
            canvas.pendingEntityRef().points[2] = p2;
            canvas.pendingEntityRef().points[3] = p3;
        } else {
            // One or no dim locked: axis-aligned (2-point) rectangle
            QPointF corner = pos;
            if (lockedW > 0 || lockedH > 0) {
                QPointF origin = canvas.pendingEntityRef().points[0];
                double dx = pos.x() - origin.x();
                double dy = pos.y() - origin.y();
                if (lockedW > 0) dx = (dx >= 0 ? lockedW : -lockedW);
                if (lockedH > 0) dy = (dy >= 0 ? lockedH : -lockedH);
                corner = origin + QPointF(dx, dy);
            }
            // Keep only 2 points for axis-aligned mode
            while (canvas.pendingEntityRef().points.size() > 2)
                canvas.pendingEntityRef().points.pop_back();
            if (canvas.pendingEntityRef().points.size() > 1) {
                canvas.pendingEntityRef().points[1] = corner;
            } else {
                canvas.pendingEntityRef().points.push_back(corner);
            }
        }
    }
    return true;
}


bool RectangleToolHandler::normalize(SketchCanvas& canvas, SketchEntity& entity, bool& valid)
{
    (void)canvas;
    if (entity.type == SketchEntityType::Rectangle) {
    // For center mode, we have 3 points: [center, corner1, corner2]
    // Convert to standard 2-point corner format [corner1, corner2]
    if (canvas.rectMode() == SketchCanvas::RectMode::Center && entity.points.size() >= 3) {
        QPointF corner1 = entity.points[1];
        QPointF corner2 = entity.points[2];
        entity.points.clear();
        entity.points.push_back(corner1);
        entity.points.push_back(corner2);
    } else if (canvas.rectMode() == SketchCanvas::RectMode::ThreePoint && entity.points.size() >= 3) {
        // 3-point angled rectangle: [p1, p2, p3] where p1-p2 is the first
        // edge and p3 sets the width. Stored as the four corners (a rotated
        // rectangle); the corner construction is the library's.
        Point2D corners[4];
        if (sketch::rectangleFromThreePoints(entity.points[0], entity.points[1],
                                             entity.points[2], corners)) {
            entity.points.assign(corners, corners + 4);
        }
    }
    // Validate: need at least 2 points with some distance
    // For 3-point mode, we now have 4 points (all corners)
    if (entity.points.size() == 4) {
        // 4-point rotated rectangle
        valid = QLineF(entity.points[0], entity.points[1]).length() > 0.1;
    } else {
        valid = entity.points.size() >= 2 &&
                QLineF(entity.points[0], entity.points[1]).length() > 0.1;
    }
        return true;
    }
    if (entity.type == SketchEntityType::Parallelogram) {
    // Parallelogram: 3 points clicked (p1, p2, p3), 4th is computed
    // p1-p2 is first edge, p2-p3 is second edge, p4 = p1 + (p3 - p2)
    if (entity.points.size() >= 3) {
        QPointF p1 = entity.points[0];
        QPointF p2 = entity.points[1];
        QPointF p3 = entity.points[2];

        const QPointF p4 = sketch::parallelogramFourthCorner(p1, p2, p3);

        // Store all 4 corners
        entity.points.clear();
        entity.points.push_back(p1);
        entity.points.push_back(p2);
        entity.points.push_back(p3);
        entity.points.push_back(p4);

        valid = QLineF(p1, p2).length() > 0.1 && QLineF(p2, p3).length() > 0.1;
    }
        return true;
    }
    // --- additional entity types handled by this tool ---
    return false;
}


bool RectangleToolHandler::beginEntity(SketchCanvas& canvas, SketchEntity& entity)
{
    m_bothLocked = false;   // rotation reference is per rectangle
    (void)canvas;
    // Parallelogram mode produces a different ENTITY type from the other
    // three rectangle modes.
    entity.type = (canvas.rectMode() == SketchCanvas::RectMode::Parallelogram)
                      ? SketchEntityType::Parallelogram
                      : SketchEntityType::Rectangle;
    return true;
}

void RectangleToolHandler::dimFieldsChanged(SketchCanvas& canvas)
{
    // Only the corner-to-corner rectangle rotates once both dimensions are
    // pinned, and the reference is captured once.
    if (canvas.rectMode() != SketchCanvas::RectMode::Corner || m_bothLocked) {
        return;
    }
    if (canvas.dimFieldCount() < 2 || !canvas.allDimFieldsLocked()
        || canvas.previewPointCount() < 1) {
        return;
    }
    const QPointF origin = canvas.previewPoint(0);
    const QPointF delta  = canvas.currentMouseWorld() - origin;
    m_lockRefAngle    = std::atan2(delta.y(), delta.x());
    m_lockWidthAngle  = (delta.x() >= 0) ? 0.0 : M_PI;
    m_lockHeightAngle = (delta.y() >= 0) ? M_PI / 2.0 : -M_PI / 2.0;
    m_bothLocked      = true;
}

bool RectangleToolHandler::supportsAngleSnap(const SketchCanvas&) const
{
    return true;
}


bool RectangleToolHandler::applyCreationMode(SketchCanvas& canvas, int modeValue)
{
    // Rectangle modes: 0=Corner, 1=Center, 2=ThreePoint, 3=Parallelogram.
    // The mapping lives with the tool that acts on it, not in the canvas.
    switch (modeValue) {
    case 1:  canvas.setRectMode(SketchCanvas::RectMode::Center); break;
    case 2:  canvas.setRectMode(SketchCanvas::RectMode::ThreePoint); break;
    case 3:  canvas.setRectMode(SketchCanvas::RectMode::Parallelogram); break;
    default: canvas.setRectMode(SketchCanvas::RectMode::Corner); break;
    }
    return true;
}

}  // namespace hobbycad
