// =====================================================================
//  src/hobbycad/gui/tools/slottoolhandler.cpp
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#include <hobbycad/sketch/entity.h>
#include "../slotpainter.h"
#include "../screenmath.h"
#include <hobbycad/units.h>

#include <limits>
#include "slottoolhandler.h"
#include "../sketchcanvas.h"
#include <QKeyEvent>

#include <QCoreApplication>
#include <optional>
#include <hobbycad/geometry/utils.h>
#include <hobbycad/geometry/intersections.h>
#include <QPen>
#include <QPainter>
#include <QPainterPath>
#include <QLineF>
#include <QMouseEvent>
#include <QPointF>
#include <QtMath>

#include <cmath>
#include <QWheelEvent>
#include <QtGlobal>

namespace hobbycad {

using SlotMode = SketchCanvas::SlotMode;

namespace {
/// The two modes staged over three points. CenterToCenter and Overall place
/// with two clicks through the shared path in mousePressEvent.
bool isArcSlot(SlotMode m)
{
    return m == SlotMode::ArcRadius || m == SlotMode::ArcEnds;
}
}  // namespace

void SlotToolHandler::constrainArcSlot(const SketchCanvas& canvas, QPointF& snapped)
{


    // ArcRadius stage 1: locked Radius constrains start distance from arc center
    if (canvas.slotMode() == SketchCanvas::SlotMode::ArcRadius && canvas.pendingEntity().points.size() == 1) {
        double lockedR = canvas.lockedDim(0);
        if (lockedR > 0) {
            QPointF arcCenter = canvas.pendingEntity().points[0];
            if (geometry::length(snapped - arcCenter) > geometry::kDegenerateLen)
                snapped = geometry::applyPolarLock(arcCenter, snapped, lockedR, -1.0);
        }
    }

    // ArcRadius stage 2: constrain end to arc + apply locked sweep angle
    if (canvas.slotMode() == SketchCanvas::SlotMode::ArcRadius && canvas.pendingEntity().points.size() == 2) {
        QPointF arcCenterWorld = canvas.pendingEntity().points[0];
        QPointF startWorld = canvas.pendingEntity().points[1];
        double arcRadius = QLineF(arcCenterWorld, startWorld).length();
        double mouseAngle = std::atan2(snapped.y() - arcCenterWorld.y(),
                                       snapped.x() - arcCenterWorld.x());
        double startAngle = std::atan2(startWorld.y() - arcCenterWorld.y(),
                                       startWorld.x() - arcCenterWorld.x());

        // Apply locked sweep angle if present
        double lockedSweep = canvas.lockedDim(0);  // Sweep Angle (stage 2 field 0)
        if (lockedSweep != -1.0) {
            const QPointF endPt(geometry::pointAtLockedSweep(Point2D(arcCenterWorld), Point2D(startWorld),
                                                            Point2D(snapped), lockedSweep, canvas.arcSlotFlipped()));
            mouseAngle = std::atan2(endPt.y() - arcCenterWorld.y(), endPt.x() - arcCenterWorld.x());
        } else {
            // Stop at the FLOOR (one cap radius between the ends), not
            // at the tangent separation. Between the two the caps overlap,
            // which is not an error: it is how the middle of the ring is
            // actually freed, since at tangency it is still held by a
            // point. Clamping at tangency here would make that band
            // reachable from the CLI and not by dragging, which is the
            // worse kind of difference between two front ends: the same
            // model, two different sets of achievable shapes.
            //
            // This used to be computed here as 2 * slotRadius / arcRadius,
            // which reads the gap as an ARC length. An arc is longer than
            // the chord it spans, so that leaves the caps slightly
            // OVERLAPPING and cuts the cusp off: at r=30, width=8 it is
            // 0.024mm of overlap. The library form uses the chord.
            double slotRadius = canvas.pendingEntity().radius;
            if (slotRadius < 0.1) slotRadius = 5.0;

            const double minSepDeg =
                sketch::arcSlotFloorSeparationDegrees(arcRadius, slotRadius);
            const double minAngularSep = (minSepDeg > 0.0)
                ? qDegreesToRadians(minSepDeg)
                : 0.1;   // degenerate inputs: fall back to a nominal gap

            double angleDiff = mouseAngle - startAngle;
            angleDiff = hobbycad::geometry::wrapSweepRad(angleDiff);
            if (std::abs(angleDiff) < minAngularSep) {
                double sign = (angleDiff >= 0) ? 1.0 : -1.0;
                mouseAngle = startAngle + sign * minAngularSep;
            }

            // ...and the same limit from the other direction, which was
            // missing entirely: dragging the far way round could sweep past
            // the point where the caps close, overlapping them from the
            // other side. The check above only sees the SHORT separation,
            // so a flipped (long-way) arc slipped through it.
            if (canvas.arcSlotFlipped()) {
                const double maxSweepDeg =
                    sketch::absoluteMaxArcSlotSweepDegrees(arcRadius, slotRadius);
                if (maxSweepDeg > 0.0) {
                    double longSweep = angleDiff;
                    longSweep += (longSweep > 0) ? -2.0 * M_PI : 2.0 * M_PI;
                    const double maxSweep = qDegreesToRadians(maxSweepDeg);
                    if (std::abs(longSweep) > maxSweep) {
                        const double sign = (longSweep >= 0) ? 1.0 : -1.0;
                        mouseAngle = startAngle + sign * maxSweep;
                    }
                }
            }
        }

        snapped = arcCenterWorld + QPointF(arcRadius * std::cos(mouseAngle),
                                           arcRadius * std::sin(mouseAngle));
    }

    // ArcEnds stage 2: locked sweep constrains arc center on perp bisector
    if (canvas.slotMode() == SketchCanvas::SlotMode::ArcEnds && canvas.pendingEntity().points.size() == 2) {
        double lockedSweep = canvas.lockedDim(0);
        if (lockedSweep != -1.0) {
            QPointF start = canvas.pendingEntity().points[0];
            QPointF end = canvas.pendingEntity().points[1];
            snapped = QPointF(geometry::arcCenterFromChordAndSweep(Point2D(start), Point2D(end), Point2D(snapped), lockedSweep));
        }
    }


}

bool SlotToolHandler::mousePress(SketchCanvas& canvas, QMouseEvent* event, const QPointF& world)
{
    if (!isArcSlot(canvas.slotMode()) || !canvas.isDrawing()) return false;

    canvas.beginDragDetection(event->pos());   // per STAGE, not per entity

    QPointF snapped = canvas.snapToGeometry(world);
    constrainArcSlot(canvas, snapped);
    canvas.appendPlacementPoint(snapped);

    if (canvas.pendingEntity().points.size() >= 3) canvas.commitEntity();
    else                                           canvas.refreshDimFields();
    return true;
}

bool SlotToolHandler::mouseRelease(SketchCanvas& canvas, QMouseEvent*, const QPointF& world)
{
    if (!isArcSlot(canvas.slotMode()) || !canvas.isDrawing()) return false;

    if (!canvas.wasDragged()) {
        // Click placement: press already appended the point.
        if (canvas.pendingEntity().points.size() >= 3) canvas.commitEntity();
        return true;
    }

    QPointF snapped = canvas.snapToGeometry(world);
    constrainArcSlot(canvas, snapped);
    canvas.appendPlacementPoint(snapped);

    if (canvas.pendingEntity().points.size() >= 3) canvas.commitEntity();
    else                                           canvas.refreshDimFields();
    return true;
}


bool SlotToolHandler::initDimFields(SketchCanvas& canvas)
{
    const int stage = canvas.previewPointCount();

    switch (canvas.slotMode()) {
    case SlotMode::CenterToCenter:
    case SlotMode::Overall:
        if (stage >= 1) canvas.addDimField(tr("Length"), false);
        break;
    case SlotMode::ArcRadius:
        if (stage == 1)      canvas.addDimField(tr("Radius"), false);
        else if (stage >= 2) canvas.addDimField(tr("Sweep Angle"), true);
        break;
    case SlotMode::ArcEnds:
        // No field at stage 1, unlike ArcRadius: both endpoints are placed
        // first and only then is the sweep meaningful.
        if (stage >= 2) canvas.addDimField(tr("Sweep Angle"), true);
        break;
    }
    return true;
}

bool SlotToolHandler::wheel(SketchCanvas& canvas, QWheelEvent* event)
{
    if (!canvas.isDrawing()) return false;   // fall through to zoom

    const int delta = event->angleDelta().y() > 0 ? 1 : -1;
    SketchEntity& e = canvas.pendingEntityRef();

    // A 1 mm floor and NO ceiling, unchanged since at least 2026-08-23, while
    // polygons next door use qBound(3, ..., 64). For a straight slot that
    // is fine, but an arc slot's half-width cannot exceed its arc radius:
    // past that the inner edge passes through the arc center and the
    // outline turns inside out. Aaron's rule, 2026-08-28: "slot width/2 is
    // less than or equal to the arc radius." The wheel could walk straight
    // past it.
    double ceiling = std::numeric_limits<double>::max();
    const auto& pts = e.points;
    switch (canvas.slotMode()) {
    case SketchCanvas::SlotMode::ArcRadius:
        // points[0] is the arc center, points[1] the first end.
        if (pts.size() >= 2) {
            ceiling = QLineF(pts[0], pts[1]).length();
        }
        break;
    case SketchCanvas::SlotMode::ArcEnds:
        // Placement order here is start, end, center; the reorder into
        // storage form happens later.
        if (pts.size() >= 3) {
            ceiling = QLineF(pts[2], pts[0]).length();
        }
        break;
    default:
        break;   // a straight slot has no such limit
    }

    e.radius = qBound(1.0, e.radius + delta, qMax(1.0, ceiling));
    return true;
}

QString SlotToolHandler::hint(const SketchCanvas& canvas) const
{
    const int stage = canvas.previewPointCount();

    switch (canvas.slotMode()) {
    case SlotMode::CenterToCenter:
        return stage < 1 ? tr("Slot: click the first center  (scroll = thickness)")
                         : tr("Slot: click the second center, or type a length");
    case SlotMode::Overall:
        return stage < 1 ? tr("Slot: click one end  (scroll = thickness)")
                         : tr("Slot: click the other end, or type an overall length");
    case SlotMode::ArcRadius:
        if (stage < 1) return tr("Arc slot: click the arc center  (scroll = thickness)");
        if (stage < 2) return tr("Arc slot: click the start point, or type a radius");
        return tr("Arc slot: click the end point, or type a sweep angle  (Shift = long way round)");
    case SlotMode::ArcEnds:
        if (stage < 1) return tr("Arc slot: click the start point  (scroll = thickness)");
        if (stage < 2) return tr("Arc slot: click the end point");
        return tr("Arc slot: set the bulge, or type a sweep angle  (Shift = long way round)");
    }
    return {};
}

QString SlotToolHandler::cursorHint(const SketchCanvas& canvas) const
{
    // The base default strips the "  (scroll = ...)" tail from hint(); but the
    // wheel adjusts the slot's thickness at every stage of every slot mode, so
    // surface it near the cursor as its own line (Aaron). Multi-line stacks
    // downward in the canvas overlay.
    const QString base = SketchToolHandler::cursorHint(canvas);
    const QString scroll = tr("(scroll: thickness)");
    return base.isEmpty() ? scroll : base + QLatin1Char('\n') + scroll;
}


// drawPreview, arc slot modes (ArcRadius, ArcEnds).
void SlotToolHandler::drawArcSlotPreview(SketchCanvas& canvas, QPainter& painter,
                                         const QPointF& p1World, const QPointF& p2World, double radius)
{
    // Arc slot mode - needs 3 points
    // ArcRadius: arc center -> start -> end (both endpoints constrained to arc)
    // ArcEnds: start -> end -> arc center (free placement)
    if (canvas.previewPointCount() >= 2) {
        QPointF startWorld, endWorld, arcCenterWorld;

        if (canvas.slotMode() == SketchCanvas::SlotMode::ArcRadius) {
            // Have arc center and start, current mouse is end (constrained to arc)
            arcCenterWorld = canvas.previewPoint(0);
            startWorld = canvas.previewPoint(1);
            // Constrain end point to arc radius
            double arcRadius = QLineF(arcCenterWorld, startWorld).length();
            double mouseAngle = std::atan2(canvas.currentMouseWorld().y() - arcCenterWorld.y(),
                                           canvas.currentMouseWorld().x() - arcCenterWorld.x());
            double startAngle = std::atan2(startWorld.y() - arcCenterWorld.y(),
                                           startWorld.x() - arcCenterWorld.x());

            // Minimum angular separation so slot ends don't overlap
            // Arc length between centers must be >= 2 * slot radius
            double slotRadius = canvas.pendingEntity().radius;
            if (slotRadius < 0.1) slotRadius = 5.0;
            double minAngularSep = (arcRadius > 0.001)
            ? qDegreesToRadians(
                  sketch::arcSlotFloorSeparationDegrees(arcRadius, slotRadius))
            : 0.1;

            // Calculate angular difference
            double angleDiff = mouseAngle - startAngle;
            // Normalize to [-PI, PI]
            angleDiff = hobbycad::geometry::wrapSweepRad(angleDiff);

            // Clamp to minimum separation
            if (std::abs(angleDiff) < minAngularSep) {
                // Push to minimum distance in same direction
                double sign = (angleDiff >= 0) ? 1.0 : -1.0;
                mouseAngle = startAngle + sign * minAngularSep;
            }

            endWorld = arcCenterWorld + QPointF(arcRadius * std::cos(mouseAngle),
                                                arcRadius * std::sin(mouseAngle));
        } else {
            // ArcEnds: Have start and end points, current mouse is arc center
            // Both endpoints stay fixed; arc center is constrained to the perpendicular
            // bisector of the line between start and end (equidistant from both)
            startWorld = canvas.previewPoint(0);
            endWorld = canvas.previewPoint(1);

            // Find the perpendicular bisector of start-end line
            QPointF midpoint = (startWorld + endWorld) / 2.0;
            QPointF startToEnd = endWorld - startWorld;
            double chordLen = QLineF(startWorld, endWorld).length();

            if (chordLen > 0.001) {
                // Perpendicular direction (rotate 90 degrees)
                QPointF perpDir = geometry::perpendicular(geometry::normalize(startToEnd));

                // Project mouse position onto the perpendicular bisector
                QPointF mouseToMid = canvas.currentMouseWorld() - midpoint;
                double projDist = mouseToMid.x() * perpDir.x() + mouseToMid.y() * perpDir.y();

                // Arc center is on the perpendicular bisector
                arcCenterWorld = midpoint + perpDir * projDist;
            } else {
                arcCenterWorld = canvas.currentMouseWorld();
            }
        }

        // Arc center pushed out to keep the ends separated (shared with
        // the commit path via the library). [maintainability audit]
        sketch::SlotArcCenter sa = sketch::enforceSlotArcSeparation(
            startWorld, endWorld, arcCenterWorld, canvas.pendingEntity().radius);
        arcCenterWorld = QPointF(sa.center.x, sa.center.y);
        double arcRadius = sa.radius;

        double innerRadius = arcRadius - radius;
        double outerRadius = arcRadius + radius;

        // Draw the arc slot preview
        QPointF ss = canvas.toScreen(startWorld);
        QPointF se = canvas.toScreen(endWorld);  // Now projected onto arc
        QPointF sc = canvas.toScreen(arcCenterWorld);

        double screenInnerRadius = innerRadius * canvas.zoomFactor();
        double screenOuterRadius = outerRadius * canvas.zoomFactor();
        double screenHalfWidth = radius * canvas.zoomFactor();

        double startAngle = painterAngleDeg(sc, ss);
        double endAngle = painterAngleDeg(sc, se);
        double sweepAngle = endAngle - startAngle;

        // Normalize sweep angle to [-180, 180]
        sweepAngle = hobbycad::geometry::wrapSweepDeg(sweepAngle);

        // Use tracked flip state (toggled by Shift key)
        if (canvas.arcSlotFlipped()) {
            sweepAngle = hobbycad::geometry::oppositeSweepDeg(sweepAngle);
        }

        if (screenInnerRadius > 1 && screenOuterRadius > screenInnerRadius) {
            painter.drawPath(arcSlotOutlinePath(sc, startAngle, endAngle, sweepAngle,
                                                 screenInnerRadius, screenOuterRadius, screenHalfWidth));
        } else {
            // Arc radius too small for proper slot - draw centerline arc to show path
            double screenArcRadius = QLineF(sc, ss).length();
            if (screenArcRadius > 5) {
                QPainterPath path;
                QRectF arcRect(sc.x() - screenArcRadius, sc.y() - screenArcRadius,
                              screenArcRadius * 2, screenArcRadius * 2);
                path.arcMoveTo(arcRect, startAngle);
                path.arcTo(arcRect, startAngle, sweepAngle);
                painter.drawPath(path);

                // Draw slot width circles at start and end to indicate width
                painter.drawEllipse(ss, screenHalfWidth, screenHalfWidth);
                painter.drawEllipse(se, screenHalfWidth, screenHalfWidth);
            } else {
                // Very close to center - just draw lines to show relationship
                painter.drawLine(ss.toPoint(), sc.toPoint());
                painter.drawLine(se.toPoint(), sc.toPoint());
            }
        }

        // Draw control points
        painter.setBrush(QColor(0, 120, 215));
        painter.drawEllipse(ss.toPoint(), 3, 3);
        painter.drawEllipse(se.toPoint(), 3, 3);
        painter.drawEllipse(sc.toPoint(), 3, 3);

        // Draw sweep angle dim input field along the centerline arc
        double arcLengthWorld = arcRadius * std::abs(degreesToRadians(sweepAngle));
        if (arcLengthWorld > 0.1) {
            // Position dimension label at the midpoint of the arc (offset outward)
            double midAngle = startAngle + sweepAngle / 2.0;
            double midAngleRad = degreesToRadians(midAngle);
            double outwardAngle = -midAngleRad;
            double offsetDist = screenOuterRadius + 20;
            QPointF labelCenter = sc + QPointF(offsetDist * std::cos(outwardAngle),
                                               offsetDist * std::sin(outwardAngle));
            if (canvas.activeDimField() >= 0 && canvas.dimFieldCount() >= 1) {
                canvas.setDimFieldValue(0, std::abs(sweepAngle));
                canvas.paintDimInputField(painter, labelCenter, 0, 0);
            } else {
                canvas.paintArcDimensionLabel(painter, labelCenter, arcLengthWorld, sweepAngle);
            }
        }

        // Draw label to indicate what's being placed (3rd point)
        painter.save();
        painter.setPen(QColor(0, 120, 215));
        QFont font = painter.font();
        font.setPointSize(9);
        painter.setFont(font);
        QString line1 = (canvas.slotMode() == SketchCanvas::SlotMode::ArcRadius)
            ? tr("Click to place END point")
            : tr("Click to place ARC CENTER");
        QString line2 = tr("(Shift to flip arc direction)");
        QFontMetrics fm = painter.fontMetrics();
        QRectF rect1 = fm.boundingRect(line1);
        QRectF rect2 = fm.boundingRect(line2);
        double totalHeight = rect1.height() + rect2.height() + 2;
        double maxWidth = qMax(rect1.width(), rect2.width());
        // Position label below the moving point
        QPointF labelPos = (canvas.slotMode() == SketchCanvas::SlotMode::ArcRadius)
            ? se + QPointF(0, 15)   // ArcRadius: end point follows mouse
            : sc + QPointF(0, 15);  // ArcEnds: arc center follows mouse
        QRectF bgRect(-maxWidth / 2 - 2, 0, maxWidth + 4, totalHeight + 2);
        bgRect.translate(labelPos);
        // Draw background for readability
        painter.fillRect(bgRect, QColor(255, 255, 255, 200));
        // Draw line 1
        QPointF textPos1(labelPos.x() - rect1.width() / 2, labelPos.y() + rect1.height());
        painter.drawText(textPos1, line1);
        // Draw line 2
        QPointF textPos2(labelPos.x() - rect2.width() / 2, labelPos.y() + rect1.height() + rect2.height() + 2);
        painter.drawText(textPos2, line2);
        painter.restore();
    } else if (canvas.previewPointCount() == 1) {
        // Only have start point, draw line to current mouse (placing 2nd point)
        QPoint sp1 = canvas.toScreen(p1World);
        QPoint sp2 = canvas.toScreen(p2World);
        painter.drawLine(sp1, sp2);
        painter.setBrush(QColor(0, 120, 215));
        painter.drawEllipse(sp1, 3, 3);

        // Draw dimension below the line (Radius for ArcRadius, distance for ArcEnds)
        double distance = QLineF(p1World, p2World).length();
        if (distance > 0.1) {
            if (canvas.slotMode() == SketchCanvas::SlotMode::ArcRadius && canvas.activeDimField() >= 0 && canvas.dimFieldCount() >= 1) {
                canvas.setDimFieldValue(0, distance);
                QPointF midPt = (QPointF(sp1) + QPointF(sp2)) / 2.0;
                double screenAngle = screenAngleDeg(sp1, sp2);
                bool flipped = (screenAngle > 90 || screenAngle < -90);
                if (flipped) screenAngle += 180;
                canvas.paintDimInputField(painter, midPt + QPointF(0, 18), 0, screenAngle);
            } else {
                canvas.paintPreviewDimension(painter, sp1, sp2, distance);
            }
        }

        // Draw instruction label at midpoint of line, rotated to follow the line
        QPointF midPoint = (QPointF(sp1) + QPointF(sp2)) / 2.0;
        painter.save();
        painter.setPen(QColor(0, 120, 215));
        QFont font = painter.font();
        font.setPointSize(9);
        painter.setFont(font);

        // Calculate line angle
        double dx = sp2.x() - sp1.x();
        double dy = sp2.y() - sp1.y();
        double angle = radiansToDegrees(std::atan2(dy, dx));

        // Keep text readable (Z-up orientation) - flip if pointing left
        if (angle > 90 || angle < -90) {
            angle += 180;
        }

        // Different label based on mode
        // ArcRadius: arc center -> start -> end (constrained)
        // ArcEnds: start -> end -> arc center
        QString label = (canvas.slotMode() == SketchCanvas::SlotMode::ArcRadius)
            ? tr("Click to place START point")
            : tr("Click to place END point");
        QRectF textRect = painter.fontMetrics().boundingRect(label);

        // Translate to midpoint, rotate, then draw centered above the line
        painter.translate(midPoint);
        painter.rotate(angle);
        // Offset upward (negative Y in rotated coords) to sit on top of line
        QPointF offset(0, -textRect.height() / 2 - 4);
        textRect.moveCenter(offset);
        // Draw background for readability
        painter.fillRect(textRect.adjusted(-2, -1, 2, 1), QColor(255, 255, 255, 200));
        painter.drawText(textRect, Qt::AlignCenter, label);
        painter.restore();
    }
}

// drawPreview, linear slot modes (CenterToCenter, Overall).
void SlotToolHandler::drawLinearSlotPreview(SketchCanvas& canvas, QPainter& painter,
                                            const QPointF& p1World, const QPointF& p2World, double radius)
{
    // Linear slot modes (CenterToCenter and Overall)
    QPointF center1, center2;

    if (canvas.slotMode() == SketchCanvas::SlotMode::Overall) {
        // Overall mode: points are endpoints, centers are offset inward by radius
        double len = QLineF(p1World, p2World).length();
        if (len > 0.001) {
            double dx = (p2World.x() - p1World.x()) / len;
            double dy = (p2World.y() - p1World.y()) / len;
            center1 = QPointF(p1World.x() + dx * radius, p1World.y() + dy * radius);
            center2 = QPointF(p2World.x() - dx * radius, p2World.y() - dy * radius);
        } else {
            center1 = p1World;
            center2 = p2World;
        }
    } else {
        // CenterToCenter mode: points are arc centers
        center1 = p1World;
        center2 = p2World;
    }

    double halfWidth = radius * canvas.zoomFactor();
    QPointF sp1 = canvas.toScreen(center1);
    QPointF sp2 = canvas.toScreen(center2);
    QLineF centerLine(sp1, sp2);
    double len = centerLine.length();

    if (len > 0.001) {
        // Unit vectors along and perpendicular to the slot axis
        painter.drawPath(linearSlotOutlinePath(sp1, sp2, halfWidth));

        // Draw construction line along the centerline of the slot
        painter.save();
        QPen constructionPen(QColor(128, 128, 128), 1, Qt::DashLine);
        painter.setPen(constructionPen);
        painter.setBrush(Qt::NoBrush);
        painter.drawLine(sp1.toPoint(), sp2.toPoint());
        painter.restore();

        // Draw center points
        painter.setBrush(QColor(0, 120, 215));
        painter.drawEllipse(sp1.toPoint(), 3, 3);
        painter.drawEllipse(sp2.toPoint(), 3, 3);

        // In Overall mode, also show the actual endpoints
        if (canvas.slotMode() == SketchCanvas::SlotMode::Overall) {
            QPointF end1 = canvas.toScreen(p1World);
            QPointF end2 = canvas.toScreen(p2World);
            painter.setBrush(QColor(255, 100, 100));
            painter.drawEllipse(end1.toPoint(), 2, 2);
            painter.drawEllipse(end2.toPoint(), 2, 2);
        }

        // Draw dimension label below the center line
        double slotLength = (canvas.slotMode() == SketchCanvas::SlotMode::Overall)
            ? QLineF(p1World, p2World).length()       // Overall: endpoint to endpoint
            : QLineF(center1, center2).length();      // CenterToCenter: center to center
        if (slotLength > 0.1) {
            if (canvas.activeDimField() >= 0 && !(canvas.dimFieldCount() == 0)) {
                canvas.setDimFieldValue(0, slotLength);
                QPointF midPt = (sp1 + sp2) / 2.0 + QPointF(0, 18);
                canvas.paintDimInputField(painter, midPt, 0);
            } else {
                canvas.paintPreviewDimension(painter, sp1.toPoint(), sp2.toPoint(), slotLength);
            }
        }
    }
}

bool SlotToolHandler::drawPreview(SketchCanvas& canvas, QPainter& painter)
{
    if (canvas.previewPointCount() == 0) return true;
    QPointF p1World = canvas.previewPoint(0);
    // Use constrained endpoint from updateEntity for linear slots
    QPointF p2World = (canvas.pendingEntity().points.size() >= 2 &&
                       (canvas.slotMode() == SketchCanvas::SlotMode::CenterToCenter || canvas.slotMode() == SketchCanvas::SlotMode::Overall))
        ? QPointF(canvas.pendingEntity().points[1]) : canvas.currentMouseWorld();
    double radius = canvas.pendingEntity().radius;
    if (radius < 0.1) radius = 5.0;  // Default radius

    bool isArcSlot = (canvas.slotMode() == SketchCanvas::SlotMode::ArcRadius || canvas.slotMode() == SketchCanvas::SlotMode::ArcEnds);
    if (isArcSlot) {
        drawArcSlotPreview(canvas, painter, p1World, p2World, radius);
    } else {
        drawLinearSlotPreview(canvas, painter, p1World, p2World, radius);
    }
    return true;
}


bool SlotToolHandler::updateEntity(SketchCanvas& canvas, const QPointF& pos)
{
    if (canvas.slotMode() == SketchCanvas::SlotMode::ArcRadius) {
        // Arc slot (Radius mode): apply locked dimension constraints
        int stage = canvas.previewPointCount();
        if (stage == 1) {
            // Stage 1: arc center placed, defining start → locked Radius constrains distance
            double lockedR = canvas.lockedDim(0);  // Radius
            if (lockedR > 0) {
                QPointF arcCenter = canvas.previewPoint(0);
                if (geometry::length(pos - arcCenter) > geometry::kDegenerateLen)
                    canvas.currentMouseWorld() = geometry::applyPolarLock(arcCenter, pos, lockedR, -1.0);
            }
        } else if (stage >= 2) {
            // Stage 2: arc center+start placed, defining end → locked Sweep constrains angle
            double lockedSweep = canvas.lockedDim(0);  // Sweep Angle
            if (lockedSweep != -1.0) {
                QPointF arcCenter = canvas.previewPoint(0);
                QPointF start = canvas.previewPoint(1);
                canvas.currentMouseWorld() = QPointF(geometry::pointAtLockedSweep(
                    Point2D(arcCenter), Point2D(start), Point2D(pos), lockedSweep, canvas.arcSlotFlipped()));
            }
        }
    } else if (canvas.slotMode() == SketchCanvas::SlotMode::ArcEnds) {
        // Arc slot (Ends mode): apply locked dimension constraints
        int stage = canvas.previewPointCount();
        if (stage >= 2) {
            // Stage 2: start+end placed, mouse controls arc center on perp bisector
            double lockedSweep = canvas.lockedDim(0);  // Sweep Angle
            if (lockedSweep != -1.0) {
                QPointF start = canvas.previewPoint(0);
                QPointF end = canvas.previewPoint(1);
                canvas.currentMouseWorld() = QPointF(geometry::arcCenterFromChordAndSweep(
                    Point2D(start), Point2D(end), Point2D(pos), lockedSweep));
            }
        }
    } else {
        // Linear slot (CenterToCenter or Overall) - two endpoints
        QPointF endpoint = pos;
        double lockedLen = canvas.lockedDim(0);  // field 0 = Length
        if (lockedLen > 0) {
            QPointF start = canvas.pendingEntityRef().points[0];
            if (geometry::length(pos - start) > geometry::kDegenerateLen)
                endpoint = geometry::applyPolarLock(start, pos, lockedLen, -1.0);
        }
        if (canvas.pendingEntityRef().points.size() > 1) {
            canvas.pendingEntityRef().points[1] = endpoint;
        } else {
            canvas.pendingEntityRef().points.push_back(endpoint);
        }
    }
    return true;
}


bool SlotToolHandler::normalize(SketchCanvas& canvas, SketchEntity& entity, bool& valid)
{
    (void)canvas;
    if (entity.type == SketchEntityType::Slot) {
    if (canvas.slotMode() == SketchCanvas::SlotMode::ArcRadius) {
        // Arc slot (Radius mode): points are arc center, start, end (constrained)
        // Storage format: points[0] = arc center, points[1] = start, points[2] = end
        if (entity.points.size() >= 3) {
            QPointF arcCenter = entity.points[0];
            QPointF start = entity.points[1];
            QPointF end = entity.points[2];

            // Project end point onto arc radius (same distance from center as start)
            double arcRadius = QLineF(arcCenter, start).length();
            double endDist = QLineF(arcCenter, end).length();
            if (endDist > 0.001 && arcRadius > 0.001)
                end = geometry::closestPointOnCircle(end, arcCenter, arcRadius);

            // Points already in correct order: arc center, start, end
            entity.points[2] = end;  // Update projected end
            entity.arcFlipped = canvas.arcSlotFlipped();
            valid = true;
        }
        // Radius was set during startEntity or adjusted via scroll wheel
    } else if (canvas.slotMode() == SketchCanvas::SlotMode::ArcEnds) {
        // Arc slot (Ends mode): points are start, end, arc center
        // Both endpoints stay fixed; arc center is constrained to perpendicular bisector
        // Reorder to storage format: points[0] = arc center, points[1] = start, points[2] = end
        if (entity.points.size() >= 3) {
            QPointF start = entity.points[0];
            QPointF end = entity.points[1];
            QPointF arcCenter = entity.points[2];

            // The center goes to its projection on the chord's bisector, kept
            // half the library's distance floor off the chord (Aaron), then
            // the slot's own rule pushes it out so the ends stay separated.
            const geometry::ArcCenterFromChord ac = geometry::arcCenterOnBisector(
                start, end, arcCenter, false, false,
                geometry::ChordFloor::MinPerpDistance, geometry::kChordPerpFloor / 2.0);
            if (ac.valid) arcCenter = ac.center;

            // Push the center out to keep the ends separated (shared helper).
            arcCenter = sketch::enforceSlotArcSeparation(
                start, end, arcCenter, entity.radius).center;

            // Reorder to: arc center, start, end
            entity.points[0] = arcCenter;
            entity.points[1] = start;
            entity.points[2] = end;
            entity.arcFlipped = canvas.arcSlotFlipped();
            valid = true;
        }
        // Radius was set during startEntity or adjusted via scroll wheel
    } else if (canvas.slotMode() == SketchCanvas::SlotMode::Overall) {
        // Overall mode: user clicked endpoints, convert to centers
        valid = entity.points.size() >= 2 &&
                QLineF(entity.points[0], entity.points[1]).length() > 0.1;
        if (valid) {
            // Use user-adjusted radius (set during startEntity, adjusted via scroll wheel)
            double radius = entity.radius;

            // Convert endpoints to arc centers (move inward by radius)
            QPointF p1 = entity.points[0];
            QPointF p2 = entity.points[1];
            double len = QLineF(p1, p2).length();
            if (len > radius * 2) {
                double dx = (p2.x() - p1.x()) / len;
                double dy = (p2.y() - p1.y()) / len;
                entity.points[0] = QPointF(p1.x() + dx * radius, p1.y() + dy * radius);
                entity.points[1] = QPointF(p2.x() - dx * radius, p2.y() - dy * radius);
            } else {
                // Slot too short for the radius, just use endpoints
            }
        }
    } else {
        // CenterToCenter mode (default): points are arc centers
        valid = entity.points.size() >= 2 &&
                QLineF(entity.points[0], entity.points[1]).length() > 0.1;
        // Radius was set during startEntity or adjusted via scroll wheel
    }
        return true;
    }
    // --- additional entity types handled by this tool ---
    return false;
}


bool SlotToolHandler::beginEntity(SketchCanvas& canvas, SketchEntity& entity)
{
    (void)canvas;
    entity.type = SketchEntityType::Slot;
    entity.radius = 5.0;   // default half-width; the wheel adjusts it
    return true;
}

bool SlotToolHandler::keyPress(SketchCanvas& canvas, QKeyEvent* event)
{
    if (!canvas.isDrawing() || event->key() != Qt::Key_Shift) {
        return false;
    }
    const SlotMode mode = canvas.slotMode();
    if ((mode == SlotMode::ArcRadius || mode == SlotMode::ArcEnds)
        && canvas.previewPointCount() >= 2) {
        canvas.toggleArcFlip();
        canvas.update();
        return true;
    }
    return false;
}

bool SlotToolHandler::supportsAngleSnap(const SketchCanvas& canvas) const
{
    const SlotMode m = canvas.slotMode();
    return m == SlotMode::CenterToCenter || m == SlotMode::Overall;
}


bool SlotToolHandler::applyCreationMode(SketchCanvas& canvas, int modeValue)
{
    // Slot modes: 0=CenterToCenter, 1=Overall, 2=ArcRadius, 3=ArcEnds.
    // The mapping lives with the tool that acts on it, not in the canvas.
    switch (modeValue) {
    case 1:  canvas.setSlotMode(SketchCanvas::SlotMode::Overall); break;
    case 2:  canvas.setSlotMode(SketchCanvas::SlotMode::ArcRadius); break;
    case 3:  canvas.setSlotMode(SketchCanvas::SlotMode::ArcEnds); break;
    default: canvas.setSlotMode(SketchCanvas::SlotMode::CenterToCenter); break;
    }
    return true;
}

}  // namespace hobbycad
