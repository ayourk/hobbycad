// =====================================================================
//  src/hobbycad/gui/tools/arctoolhandler.cpp — Arc tool handler
// =====================================================================
//
//  Part of HobbyCAD.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include "arctoolhandler.h"
#include "../screenmath.h"
#include <hobbycad/units.h>

#include "../sketchcanvas.h"

#include <hobbycad/geometry/utils.h>
#include <hobbycad/geometry/intersections.h>
#include <hobbycad/sketch/handles.h>

#include <QCoreApplication>
#include <optional>
#include <QPen>
#include <QPainter>
#include <QGuiApplication>
#include <QPainterPath>
#include <QMouseEvent>
#include <QLineF>
#include <QPointF>
#include <QMessageBox>
#include <QKeyEvent>
#include <QtMath>

#include <cmath>

namespace hobbycad {

// Modes are nested in SketchCanvas; alias them for readable case labels.
using ArcMode = SketchCanvas::ArcMode;

// ---------------------------------------------------------------------
//  Staged placement constraints
//
//  Moved off SketchCanvas: this is per-tool logic and belongs with the tool.
//  Each is called from BOTH mousePressEvent (click placement) and
//  mouseReleaseEvent (drag-through placement); both call sites are required,
//  because a staged mode accepts either input style and drag detection is
//  per-stage (m_wasDragged, 5 px threshold).
// ---------------------------------------------------------------------

namespace {
// Center of a tangent arc of locked radius: off the host's edge at the tangent
// point, on the side the cursor is. The host's edge direction comes from the
// library, so a rectangle's edge is a real edge and not a fallback.
QPointF lockedRadiusCenter(const SketchEntity& host, const QPointF& tangentPoint,
                           const QPointF& toward, double lockedRadius)
{
    QPointF edgeDir = QPointF(sketch::tangentHostEdgeDirAt(host, Point2D(tangentPoint)));
    double edgeLen = geometry::length(edgeDir);
    if (edgeLen > geometry::kDegenerateLen) edgeDir /= edgeLen;
    QPointF normal = geometry::perpendicular(edgeDir);
    QPointF candidateCenter1 = tangentPoint + normal * lockedRadius;
    QPointF candidateCenter2 = tangentPoint - normal * lockedRadius;
    return (QLineF(toward, candidateCenter1).length() < QLineF(toward, candidateCenter2).length())
           ? candidateCenter1 : candidateCenter2;
}

// Final arc parameters onto the entity: center, start and end points (what the
// solver and the GUI both expect) plus the angle fields Entity::toArc() reads.
bool storeArc(SketchEntity& entity, const geometry::Arc& arc)
{
    entity.points.assign(3, arc.center);
    entity.radius = arc.radius;
    entity.startAngle = arc.startAngle;
    entity.sweepAngle = arc.sweepAngle;
    sketch::resyncArcEndpoints(entity);     // points[1], points[2] from the angles
    return arc.radius > 0.1;
}
}  // namespace

void ArcToolHandler::constrainCenterStartEnd(const SketchCanvas& canvas, QPointF& snapped)
{
    // Stage 1: locked Radius constrains distance from center
    if (canvas.pendingEntity().points.size() == 1) {
        double lockedR = canvas.lockedDim(0);
        if (lockedR > 0) {
            QPointF center = canvas.pendingEntity().points[0];
            if (geometry::length(snapped - center) > geometry::kDegenerateLen)
                snapped = geometry::applyPolarLock(center, snapped, lockedR, -1.0);
        }
    }

    // Stage 2: constrain to arc radius + apply locked sweep angle
    if (canvas.pendingEntity().points.size() == 2) {
        QPointF center = canvas.pendingEntity().points[0];
        QPointF start = canvas.pendingEntity().points[1];
        double radius = QLineF(center, start).length();
        double angle = std::atan2(snapped.y() - center.y(), snapped.x() - center.x());
        // Apply locked sweep angle
        double lockedSweep = canvas.lockedDim(0);  // Sweep Angle (stage 2 field 0)
        if (lockedSweep != -1.0) {
            snapped = QPointF(geometry::pointAtLockedSweep(Point2D(center), Point2D(start), Point2D(snapped),
                                                           lockedSweep, canvas.arcSlotFlipped()));
        } else {
            snapped = center + QPointF(radius * std::cos(angle), radius * std::sin(angle));
        }
    }
}

void ArcToolHandler::constrainStartEndRadius(const SketchCanvas& canvas, QPointF& snapped)
{
    int pStage = canvas.previewPointCount();
    if (pStage == 1) {
        // Stage 1: placing end point (locked Chord Length/Angle)
        QPointF start = canvas.previewPoint(0);
        double lockedLen = canvas.lockedDim(0);
        double lockedAng = canvas.lockedDim(1);
        if (lockedLen > 0 || lockedAng != -1.0) {
            // Locked length/angle placement now lives in libhobbycad, so a
            // second front-end need not reimplement it.
            snapped = geometry::applyPolarLock(start, snapped, lockedLen, lockedAng);
        }
    } else if (pStage >= 2) {
        // Stage 2: placing arc center (locked Sweep constrains perp bisector position)
        double lockedSweep = canvas.lockedDim(0);
        if (lockedSweep != -1.0) {
            QPointF start = canvas.previewPoint(0);
            QPointF end = canvas.previewPoint(1);
            // (The two flips the old code applied here canceled out: the center
            // follows the cursor's side of the chord.)
            snapped = QPointF(geometry::arcCenterFromChordAndSweep(Point2D(start), Point2D(end), Point2D(snapped), lockedSweep));
        }
    }
}

// ---------------------------------------------------------------------
//  Staged placement
// ---------------------------------------------------------------------

bool ArcToolHandler::mousePress(SketchCanvas& canvas, QMouseEvent* event, const QPointF& world)
{
    const ArcMode mode = canvas.arcMode();

    if (mode == ArcMode::Tangent) {
        // Tangent arc: the first click picks the curve to be tangent to, the
        // second places the end point. Clicks are entity picks until a target
        // exists, which is a different click semantic from the staged modes.
        const QPointF worldPos = world;
        // For tangent arc, first click selects the entity to be tangent to
        if (canvas.tangentTargets().isEmpty()) {
            // Check if there are any entities to be tangent to
            if (canvas.entities().isEmpty()) {
                QMessageBox::information(&canvas, tr("Tangent Arc"),
                    tr("There are no entities to create a tangent arc from.\n"
                       "Please draw a line first."));
                return true;
            }

            int hitId = canvas.pick(worldPos);
            if (hitId >= 0) {
                // Check if the entity type is supported for tangent arcs
                SketchEntity* entity = canvas.findEntity(hitId);
                if (entity) {
                    if (entity->type == SketchEntityType::Line ||
                        entity->type == SketchEntityType::Rectangle) {
                        canvas.addTangentTarget(hitId);

                        // Project click point onto the entity to get the tangent point
                        QPointF clickPoint = canvas.snapToGeometry(worldPos);
                        QPointF tangentPoint = clickPoint;
                        bool altHeld = event->modifiers() & Qt::AltModifier;
                        QPointF closestEdgeStart, closestEdgeEnd;

                        if (entity->type == SketchEntityType::Line && entity->points.size() >= 2) {
                            QPointF p1 = entity->points[0];
                            QPointF p2 = entity->points[1];
                            tangentPoint = geometry::closestPointOnLine(clickPoint, p1, p2);

                            // Snap to endpoints or midpoint (unless Alt is held)
                            if (!altHeld) {
                                QPointF midpoint = (p1 + p2) / 2.0;
                                double snapDist = 10.0 / canvas.zoomFactor();

                                if (QLineF(tangentPoint, p1).length() < snapDist) {
                                    tangentPoint = p1;
                                } else if (QLineF(tangentPoint, p2).length() < snapDist) {
                                    tangentPoint = p2;
                                } else if (QLineF(tangentPoint, midpoint).length() < snapDist) {
                                    tangentPoint = midpoint;
                                }
                            }
                        } else if (entity->type == SketchEntityType::Rectangle && entity->points.size() >= 2) {
                            // Nearest edge (library), then project onto it.
                            Point2D a, b;
                            if (sketch::closestTangentHostEdge(*entity, Point2D(clickPoint), a, b)) {
                                closestEdgeStart = QPointF(a);
                                closestEdgeEnd = QPointF(b);
                                tangentPoint = geometry::closestPointOnLine(clickPoint, closestEdgeStart, closestEdgeEnd);
                            }

                            // Snap to corners or edge midpoint (unless Alt is held)
                            if (!altHeld) {
                                QPointF midpoint = (closestEdgeStart + closestEdgeEnd) / 2.0;
                                double snapDist = 10.0 / canvas.zoomFactor();

                                if (QLineF(tangentPoint, closestEdgeStart).length() < snapDist) {
                                    tangentPoint = closestEdgeStart;
                                } else if (QLineF(tangentPoint, closestEdgeEnd).length() < snapDist) {
                                    tangentPoint = closestEdgeEnd;
                                } else if (QLineF(tangentPoint, midpoint).length() < snapDist) {
                                    tangentPoint = midpoint;
                                }
                            }
                        }

                        // Start the arc with the projected tangent point
                        canvas.beginPlacement(tangentPoint);
                        canvas.update();
                    } else {
                        // Entity type not supported (yet)
                        QMessageBox::information(&canvas, tr("Tangent Arc"),
                            tr("Tangent arcs can currently only be created from lines or rectangles.\n"
                               "Please click on a line or rectangle edge."));
                    }
                }
            } else {
                // User clicked but didn't hit any entity
                QMessageBox::information(&canvas, tr("Tangent Arc"),
                    tr("Please click on a line or rectangle edge to create a tangent arc from."));
            }
        } else {
            // Second click is the end point - update and finish the entity
            canvas.trackEntity(canvas.snapToGeometry(worldPos));
            canvas.commitEntity();
        }
        return true;
    }

    if (!canvas.isDrawing()) {
        return false;   // first click still starts the entity in the canvas
    }

    // Drag detection is per STAGE, not per entity: resetting here is what
    // lets the user click some points and drag through others.
    canvas.beginDragDetection(event->pos());

    QPointF snapped = canvas.snapToGeometry(world);
    if (mode == ArcMode::CenterStartEnd)      constrainCenterStartEnd(canvas, snapped);
    else if (mode == ArcMode::StartEndRadius) constrainStartEndRadius(canvas, snapped);
    // ThreePoint takes the mouse position unconstrained: it is the one arc
    // mode with no dimension fields, so there is nothing to honor.

    canvas.appendPlacementPoint(snapped);

    if (canvas.pendingEntity().points.size() >= 3) {
        canvas.commitEntity();
    } else if (mode != ArcMode::ThreePoint) {
        canvas.refreshDimFields();   // stage transition
    } else {
        canvas.update();
    }
    return true;
}

bool ArcToolHandler::mouseRelease(SketchCanvas& canvas, QMouseEvent*, const QPointF& world)
{
    const ArcMode mode = canvas.arcMode();
    if (!canvas.isDrawing()) {
        return false;
    }
    if (mode == ArcMode::Tangent) {
        // Tangent arc is click-click: the first click fixed the tangent
        // point, the second sets the end point in mousePress. A drag-release
        // in between must NOT finish the entity, so consume it and do
        // nothing.
        return true;
    }

    if (!canvas.wasDragged()) {
        // Click placement: mousePress already appended the point, so release
        // only has to notice that the entity is complete.
        if (canvas.pendingEntity().points.size() >= 3) {
            canvas.commitEntity();
        }
        return true;
    }

    // Drag-through placement: this release places the next point.
    QPointF snapped = canvas.snapToGeometry(world);
    if (mode == ArcMode::CenterStartEnd)      constrainCenterStartEnd(canvas, snapped);
    else if (mode == ArcMode::StartEndRadius) constrainStartEndRadius(canvas, snapped);

    canvas.appendPlacementPoint(snapped);

    if (canvas.pendingEntity().points.size() >= 3) {
        canvas.commitEntity();
    } else {
        canvas.refreshDimFields();
    }
    return true;
}

// ---------------------------------------------------------------------
//  Dimension fields and hints
// ---------------------------------------------------------------------

bool ArcToolHandler::initDimFields(SketchCanvas& canvas)
{
    const int stage = canvas.previewPointCount();

    switch (canvas.arcMode()) {
    case ArcMode::CenterStartEnd:
        if (stage == 1)      canvas.addDimField(tr("Radius"), false);
        else if (stage >= 2) canvas.addDimField(tr("Sweep Angle"), true);
        break;

    case ArcMode::StartEndRadius:
        if (stage == 1) {
            canvas.addDimField(tr("Chord Length"), false);
            canvas.addDimField(tr("Chord Angle"), true);
        } else if (stage >= 2) {
            canvas.addDimField(tr("Sweep Angle"), true);
        }
        break;

    case ArcMode::Tangent:
        // Tangent arc has only ONE preview point (the tangent point); the
        // mouse acts as the second, so its fields appear as soon as a tangent
        // target exists rather than on a stage boundary.
        if (canvas.hasTangentTargets()) {
            canvas.addDimField(tr("Radius"), false);
            canvas.addDimField(tr("Sweep Angle"), true);
        }
        break;

    case ArcMode::ThreePoint:
        // Deliberately none: 3-point arc offers no dimensional input. This is
        // the one staged mode with no dim fields, and correspondingly the one
        // that skips the stage transition in mousePressEvent.
        break;
    }
    return true;
}

QString ArcToolHandler::hint(const SketchCanvas& canvas) const
{
    const int stage = canvas.previewPointCount();

    switch (canvas.arcMode()) {
    case ArcMode::ThreePoint:
        if (stage == 0) return tr("Arc (3-point): click the start point");
        if (stage == 1) return tr("Arc (3-point): click the end point");
        return tr("Arc (3-point): click a point on the arc");

    case ArcMode::CenterStartEnd:
        if (stage == 0) return tr("Arc: click the center");
        if (stage == 1) return tr("Arc: click the start point, or type a radius");
        return tr("Arc: click the end point, or type a sweep angle  (Shift = long way round)");

    case ArcMode::StartEndRadius:
        if (stage == 0) return tr("Arc: click the start point");
        if (stage == 1) return tr("Arc: click the end point, or type a chord length");
        return tr("Arc: set the bulge, or type a sweep angle  (Shift = long way round)");

    case ArcMode::Tangent:
        if (!canvas.hasTangentTargets())
            return tr("Tangent arc: click the curve to be tangent to");
        return tr("Tangent arc: click the end point  (Shift = long way round)");
    }
    return {};
}


// drawPreview, 3-point arc mode.
void ArcToolHandler::drawThreePointPreview(SketchCanvas& canvas, QPainter& painter)
{
    // 3-point arc: draw through existing points and current mouse
    QVector<QPointF> pts = canvas.previewPoints();
    pts.append(canvas.currentMouseWorld());

    // Always draw placed-point markers (filled blue dots)
    int numPlaced = canvas.previewPointCount();
    painter.setBrush(QColor(0, 120, 215));
    for (int i = 0; i < numPlaced; ++i) {
        QPoint sp = canvas.toScreen(pts[i]);
        painter.drawEllipse(sp, 3, 3);
    }
    painter.setBrush(Qt::NoBrush);

    if (pts.size() == 2) {
        // Just two points (1 placed + mouse) - draw a dashed line preview
        painter.save();
        painter.setPen(QPen(QColor(128, 128, 128), 1, Qt::DashLine));
        QPoint sp1 = canvas.toScreen(pts[0]);
        QPoint sp2 = canvas.toScreen(pts[1]);
        painter.drawLine(sp1, sp2);
        painter.restore();
    } else if (pts.size() >= 3) {
        // Three points - calculate arc using library function
        auto arc = geometry::arcFromThreePoints(pts[0], pts[1], pts[2]);
        if (arc.has_value()) {
            // Use floating-point screen coords for precise arc rendering
            QPointF scf = canvas.toScreenF(arc->center);
            double rPx = arc->radius * canvas.zoomFactor();

            // arcFromThreePoints returns angles in math convention
            // (CCW positive from 3 o'clock) which matches Qt's arcTo
            double startAngle = arc->startAngle;
            double sweep = arc->sweepAngle;

            // Draw arc using QPainterPath for sub-pixel precision
            QRectF arcRect(scf.x() - rPx, scf.y() - rPx, rPx * 2.0, rPx * 2.0);
            QPainterPath path;
            path.arcMoveTo(arcRect, startAngle);
            path.arcTo(arcRect, startAngle, sweep);
            painter.drawPath(path);

            // Draw center point marker (small cross)
            QPoint sc = scf.toPoint();
            paintCenterCross(painter, sc, 4);

            // Draw mouse-position marker (open circle at 3rd point)
            QPoint sp3 = canvas.toScreen(pts[2]);
            painter.drawEllipse(sp3, 3, 3);

            // Draw arc length and angle dimension
            double arcLen = geometry::arcLength(*arc);
            if (arcLen > 0.1) {
                double midAngle = degreesToRadians(arc->startAngle + arc->sweepAngle / 2.0);
                double screenMidAngle = -midAngle;
                double offsetDist = rPx + 20;
                QPointF labelCenter = scf + QPointF(offsetDist * std::cos(screenMidAngle),
                                                     offsetDist * std::sin(screenMidAngle));
                canvas.paintArcDimensionLabel(painter, labelCenter, arcLen, arc->sweepAngle);
            }
        } else {
            // Collinear points: arc cannot be computed.
            // Draw dashed lines through all points as fallback.
            painter.save();
            painter.setPen(QPen(QColor(128, 128, 128), 1, Qt::DashLine));
            QPoint sp1 = canvas.toScreen(pts[0]);
            QPoint sp2 = canvas.toScreen(pts[1]);
            QPoint sp3 = canvas.toScreen(pts[2]);
            painter.drawLine(sp1, sp2);
            painter.drawLine(sp2, sp3);
            painter.restore();
        }
    }
}

// drawPreview, Center-Start-End mode.
void ArcToolHandler::drawCenterStartEndPreview(SketchCanvas& canvas, QPainter& painter)
{
    // Center-Start-End arc: center is first point, start defines radius
    QPoint centerScreen = canvas.toScreen(canvas.previewPoint(0));

    if (canvas.previewPointCount() == 1) {
        // Only center placed - draw line from center to mouse (radius preview)
        QPoint mouseScreen = canvas.toScreen(canvas.currentMouseWorld());
        painter.drawLine(centerScreen, mouseScreen);

        // Draw center marker
        int crossSize = 4;
        paintCenterCross(painter, centerScreen, crossSize);

        // Show radius dim input field
        double radius = QLineF(canvas.previewPoint(0), canvas.currentMouseWorld()).length();
        if (radius > 0.1) {
            if (canvas.activeDimField() >= 0 && canvas.dimFieldCount() >= 1) {
                canvas.setDimFieldValue(0, radius);
                QPointF midPt = (QPointF(centerScreen) + QPointF(mouseScreen)) / 2.0;
                double screenAngle = screenAngleDeg(centerScreen, mouseScreen);
                bool flipped = (screenAngle > 90 || screenAngle < -90);
                if (flipped) screenAngle += 180;
                canvas.paintDimInputField(painter, midPt + QPointF(0, 18), 0, screenAngle);
            } else {
                QPointF labelPos = (QPointF(centerScreen) + QPointF(mouseScreen)) / 2.0;
                labelPos += QPointF(10, -10);
                canvas.paintDimensionLabel(painter, labelPos, radius);
            }
        }
    } else if (canvas.previewPointCount() >= 2) {
        // Center and start placed - draw arc from start to mouse (constrained to radius)
        QPointF center = canvas.previewPoint(0);
        QPointF start = canvas.previewPoint(1);
        double radius = QLineF(center, start).length();

        // Constrain mouse to arc
        double endAngle = std::atan2(canvas.currentMouseWorld().y() - center.y(),
                                      canvas.currentMouseWorld().x() - center.x());
        QPointF endPoint = center + QPointF(radius * std::cos(endAngle),
                                             radius * std::sin(endAngle));

        // Convert to screen for drawing
        QPoint startScreen = canvas.toScreen(start);
        QPoint endScreen = canvas.toScreen(endPoint);
        int rPx = static_cast<int>(radius * canvas.zoomFactor());
        QRect arcRect(centerScreen.x() - rPx, centerScreen.y() - rPx, rPx * 2, rPx * 2);

        // Calculate angles in screen space
        double startAngleScreen = screenAngleDeg(centerScreen, startScreen);
        double endAngleScreen = screenAngleDeg(centerScreen, endScreen);

        // Calculate sweep (take shorter path by default, flip with Shift)
        double sweep = endAngleScreen - startAngleScreen;
        if (sweep > 180) sweep -= 360;
        if (sweep < -180) sweep += 360;

        // Apply flip for > 180 degree arcs
        if (canvas.arcSlotFlipped()) {
            sweep = hobbycad::geometry::oppositeSweepDeg(sweep);
        }

        painter.drawArc(arcRect, static_cast<int>(-startAngleScreen * 16),
                        static_cast<int>(-sweep * 16));

        // Draw center marker
        int crossSize = 4;
        paintCenterCross(painter, centerScreen, crossSize);

        // Draw start and end points
        painter.setBrush(QColor(0, 120, 215));
        painter.drawEllipse(startScreen, 3, 3);
        painter.drawEllipse(endScreen, 3, 3);

        // Draw sweep angle dim input field
        double arcLength = radius / canvas.zoomFactor() * std::abs(degreesToRadians(sweep));
        if (arcLength > 0.1) {
            double midAngle = degreesToRadians(startAngleScreen + sweep / 2.0);
            double offsetDist = rPx + 20;
            QPointF labelCenter = QPointF(centerScreen) + QPointF(offsetDist * std::cos(midAngle),
                                                                   offsetDist * std::sin(midAngle));
            if (canvas.activeDimField() >= 0 && canvas.dimFieldCount() >= 1) {
                canvas.setDimFieldValue(0, std::abs(sweep));
                canvas.paintDimInputField(painter, labelCenter, 0, 0);
            } else {
                canvas.paintArcDimensionLabel(painter, labelCenter, arcLength, sweep);
            }
        }

        // Draw hint message for Shift to flip
        QString line1 = tr("Arc: Center → Start → End");
        QString line2 = tr("(Shift to flip arc direction)");
        QFontMetrics fm(painter.font());
        int textWidth = std::max(fm.horizontalAdvance(line1), fm.horizontalAdvance(line2));
        int textHeight = fm.height() * 2 + 4;
        QPoint textPos(centerScreen.x() - textWidth / 2, centerScreen.y() + rPx + 30);
        painter.setPen(QColor(80, 80, 80));
        painter.drawText(textPos.x(), textPos.y(), line1);
        painter.drawText(textPos.x(), textPos.y() + fm.height() + 2, line2);
    }
}

// drawPreview, Start-End-Radius mode.
void ArcToolHandler::drawStartEndRadiusPreview(SketchCanvas& canvas, QPainter& painter)
{
    // Start-End-Radius arc: start is first click, end is second click, then set radius
    if (canvas.previewPointCount() == 1) {
        // Only start placed - draw line from start to mouse (chord preview)
        QPoint startScreen = canvas.toScreen(canvas.previewPoint(0));
        QPoint mouseScreen = canvas.toScreen(canvas.currentMouseWorld());
        painter.drawLine(startScreen, mouseScreen);

        // Draw start point
        painter.setBrush(QColor(0, 120, 215));
        painter.drawEllipse(startScreen, 3, 3);

        // Show chord length + angle dim input fields
        double chordLength = QLineF(canvas.previewPoint(0), canvas.currentMouseWorld()).length();
        if (chordLength > 0.1) {
            if (canvas.activeDimField() >= 0 && canvas.dimFieldCount() >= 2) {
                double dx = canvas.currentMouseWorld().x() - canvas.previewPoint(0).x();
                double dy = canvas.currentMouseWorld().y() - canvas.previewPoint(0).y();
                double angleDeg = radiansToDegrees(std::atan2(dy, dx));
                canvas.setDimFieldValue(0, chordLength);
                canvas.setDimFieldValue(1, angleDeg);
                QPointF midPt = (QPointF(startScreen) + QPointF(mouseScreen)) / 2.0;
                double screenAngle = screenAngleDeg(startScreen, mouseScreen);
                bool flipped = (screenAngle > 90 || screenAngle < -90);
                if (flipped) screenAngle += 180;
                canvas.paintDimInputField(painter, midPt + QPointF(0, 18), 0, screenAngle);
                canvas.paintDimInputField(painter, midPt + QPointF(0, 38), 1, screenAngle);
            } else {
                QPointF labelPos = (QPointF(startScreen) + QPointF(mouseScreen)) / 2.0;
                labelPos += QPointF(10, -10);
                canvas.paintDimensionLabel(painter, labelPos, chordLength);
            }
        }
    } else if (canvas.previewPointCount() >= 2) {
        // Start and end placed - mouse controls arc center (constrained to perpendicular bisector)
        QPointF start = canvas.previewPoint(0);
        QPointF end = canvas.previewPoint(1);
        double chordLength = QLineF(start, end).length();

        if (chordLength > 0.001) {
            // Arc center on the chord's perpendicular bisector at the
            // mouse projection, with the min-radius floor (shared with
            // the commit path via the library). [maintainability audit]
            const bool ctrlHeld =
                (QGuiApplication::queryKeyboardModifiers() & Qt::ControlModifier);
            const auto arcCtr = geometry::arcCenterOnBisector(
                start, end, canvas.currentMouseWorld(),
                ctrlHeld, canvas.arcSlotFlipped());
            const QPointF center(arcCtr.center.x, arcCtr.center.y);
            const double radius = arcCtr.radius;
            const double projDist = arcCtr.projection;
            const double halfChord = chordLength / 2.0;

            // Convert to screen for drawing
            QPoint centerScreen = canvas.toScreen(center);
            QPoint startScreen = canvas.toScreen(start);
            QPoint endScreen = canvas.toScreen(end);
            int rPx = static_cast<int>(radius * canvas.zoomFactor());
            QRect arcRect(centerScreen.x() - rPx, centerScreen.y() - rPx, rPx * 2, rPx * 2);

            // Calculate angles in screen space
            double startAngleScreen = screenAngleDeg(centerScreen, startScreen);
            double endAngleScreen = screenAngleDeg(centerScreen, endScreen);

            // Calculate sweep from start to end
            double sweep = endAngleScreen - startAngleScreen;
            // Normalize to [-180, 180]
            sweep = hobbycad::geometry::wrapSweepDeg(sweep);

            // The arc length is determined by center position:
            // - Center far from chord (|projDist| > halfChord) = small arc (< 180°) because radius is large
            // - Center close to chord (|projDist| < halfChord) = large arc (> 180°) because radius is small
            // When |projDist| = halfChord, the arc is exactly 90° (radius = halfChord * sqrt(2))
            bool wantLongArc = (std::abs(projDist) < halfChord);

            // After normalization, sweep is in [-180, 180] (always "short" path)
            // If we want the long arc, flip to get > 180 or < -180
            if (wantLongArc) {
                sweep = hobbycad::geometry::oppositeSweepDeg(sweep);
            }
            // Note: canvas.arcSlotFlipped() was already applied to projDist above,
            // which changed the center position and wantLongArc calculation.
            // We do NOT apply it again here.

            // For Ctrl (exact 180°), force sweep to exactly 180 or -180 degrees
            if (ctrlHeld) {
                sweep = (sweep > 0) ? 180.0 : -180.0;
            }

            painter.drawArc(arcRect, static_cast<int>(-startAngleScreen * 16),
                            static_cast<int>(-sweep * 16));

            // Draw center marker (this is where the cursor is constrained to)
            int crossSize = ctrlHeld ? 6 : 4;  // Larger when snapped
            paintCenterCross(painter, centerScreen, crossSize);

            // When Ctrl is held, also draw a circle at the snap point for visibility
            if (ctrlHeld) {
                painter.setBrush(Qt::NoBrush);
                painter.drawEllipse(centerScreen, 8, 8);
            }

            // Draw start and end points (fixed)
            painter.setBrush(QColor(0, 120, 215));
            painter.drawEllipse(startScreen, 4, 4);
            painter.drawEllipse(endScreen, 4, 4);

            // Draw sweep angle dim input field at midpoint of arc
            double arcLength = radius * std::abs(degreesToRadians(sweep));
            if (arcLength > 0.1) {
                double midAngle = degreesToRadians(startAngleScreen + sweep / 2.0);
                double offsetDist = rPx + 25;
                QPointF labelCenter = QPointF(centerScreen) + QPointF(offsetDist * std::cos(midAngle),
                                                                       offsetDist * std::sin(midAngle));
                if (canvas.activeDimField() >= 0 && canvas.dimFieldCount() >= 1) {
                    canvas.setDimFieldValue(0, std::abs(sweep));
                    canvas.paintDimInputField(painter, labelCenter, 0, 0);
                } else {
                    canvas.paintArcDimensionLabel(painter, labelCenter, arcLength, sweep);
                }
            }

            // Draw hint message (positioned well below arc to avoid dimension overlap)
            QString line1 = tr("Arc: Start → End → Center");
            QString line2 = tr("(Shift to flip, Ctrl for 180°)");
            QFontMetrics fm(painter.font());
            int textWidth = std::max(fm.horizontalAdvance(line1), fm.horizontalAdvance(line2));
            QPoint textPos(centerScreen.x() - textWidth / 2, centerScreen.y() + rPx + 60);
            painter.setPen(QColor(80, 80, 80));
            painter.drawText(textPos.x(), textPos.y(), line1);
            painter.drawText(textPos.x(), textPos.y() + fm.height() + 2, line2);
        }
    }
}

// drawPreview, tangent arc mode (after the first click).
void ArcToolHandler::drawTangentPreview(SketchCanvas& canvas, QPainter& painter)
{
    // Tangent arc preview (after first click)
    if (!!canvas.hasTangentTargets() && !(canvas.previewPointCount() == 0)) {
        // Find the tangent entity
        const SketchEntity* tangentEntity = canvas.entityById(canvas.tangentTargets()[0]);

        if (tangentEntity) {
            QPointF clickPoint = canvas.previewPoint(0);
            QPointF endPoint = canvas.currentMouseWorld();

            // Project click point onto the entity to get the actual tangent point
            // Project the click onto the host (line, or the nearest rectangle edge).
            QPointF tangentPoint = QPointF(sketch::projectOntoTangentHost(*tangentEntity, Point2D(clickPoint)));

            // Calculate the tangent arc
            SketchCanvas::TangentArcResult ta = canvas.tangentArcFor(*tangentEntity, tangentPoint, endPoint);

            if (ta.valid) {
                // Draw the arc preview
                QPoint centerScreen = canvas.toScreen(ta.center);
                QPoint startScreen = canvas.toScreen(tangentPoint);
                QPoint endScreen = canvas.toScreen(endPoint);
                int rPx = static_cast<int>(ta.radius * canvas.zoomFactor());
                QRect arcRect(centerScreen.x() - rPx, centerScreen.y() - rPx, rPx * 2, rPx * 2);

                // Calculate angles in screen space (Y is inverted)
                double startAngleScreen = screenAngleDeg(centerScreen, startScreen);
                double endAngleScreen = screenAngleDeg(centerScreen, endScreen);

                // Use the world-space sweep angle from calculateTangentArc.
                // Screen Y is inverted from world Y, so we negate the sweep.
                double sweep = -ta.sweepAngle;

                // Apply flip with Shift key
                if (canvas.arcSlotFlipped()) {
                    sweep = hobbycad::geometry::oppositeSweepDeg(sweep);
                }

                // Qt uses 1/16th degree units
                painter.drawArc(arcRect,
                    static_cast<int>(-startAngleScreen * 16),
                    static_cast<int>(-sweep * 16));

                // Draw center marker
                paintCenterCross(painter, centerScreen, 4);

                // Draw start and end points
                painter.setBrush(QColor(0, 120, 215));
                painter.drawEllipse(startScreen, 3, 3);
                painter.drawEllipse(endScreen, 3, 3);

                // Draw arc length dimension
                double arcLength = ta.radius * std::abs(degreesToRadians(sweep));
                if (arcLength > 0.1) {
                    double midAngle = degreesToRadians(startAngleScreen + sweep / 2.0);
                    double offsetDist = rPx + 20;
                    QPointF labelCenter = QPointF(centerScreen) + QPointF(offsetDist * std::cos(midAngle),
                                                                           offsetDist * std::sin(midAngle));
                    if (canvas.activeDimField() >= 0 && canvas.dimFieldCount() >= 2) {
                        canvas.setDimFieldValue(0, ta.radius);
                        canvas.setDimFieldValue(1, std::abs(sweep));
                        canvas.paintDimInputField(painter, labelCenter, 0, 0);
                        canvas.paintDimInputField(painter, labelCenter + QPointF(0, 20), 1, 0);
                    } else {
                        canvas.paintArcDimensionLabel(painter, labelCenter, arcLength, sweep);
                    }
                }

                // Draw hint message
                QString hint = tr("(Shift to flip arc direction)");
                QFontMetrics fm(painter.font());
                int textWidth = fm.horizontalAdvance(hint);
                QPoint textPos(centerScreen.x() - textWidth / 2, centerScreen.y() + rPx + 30);
                painter.setPen(QColor(80, 80, 80));
                painter.drawText(textPos.x(), textPos.y(), hint);
            } else {
                // Arc not valid yet - just draw a line from start to cursor
                QPoint sp1 = canvas.toScreen(tangentPoint);
                QPoint sp2 = canvas.toScreen(endPoint);
                painter.drawLine(sp1, sp2);

                // Draw start point
                painter.setBrush(QColor(0, 120, 215));
                painter.drawEllipse(sp1, 3, 3);
            }
        } else {
            // Tangent entity not found - draw fallback line
            QPoint sp1 = canvas.toScreen(canvas.previewPoint(0));
            QPoint sp2 = canvas.toScreen(canvas.currentMouseWorld());
            painter.drawLine(sp1, sp2);
            painter.setBrush(QColor(0, 120, 215));
            painter.drawEllipse(sp1, 3, 3);
        }
    } else if (!(canvas.previewPointCount() == 0)) {
        // No tangent target yet - just show start point
        QPoint sp1 = canvas.toScreen(canvas.previewPoint(0));
        QPoint sp2 = canvas.toScreen(canvas.currentMouseWorld());
        painter.drawLine(sp1, sp2);
    }
}

bool ArcToolHandler::drawPreview(SketchCanvas& canvas, QPainter& painter)
{
    if (canvas.previewPointCount() == 0) return true;
    switch (canvas.arcMode()) {
    case SketchCanvas::ArcMode::ThreePoint:     drawThreePointPreview(canvas, painter); break;
    case SketchCanvas::ArcMode::CenterStartEnd: drawCenterStartEndPreview(canvas, painter); break;
    case SketchCanvas::ArcMode::StartEndRadius: drawStartEndRadiusPreview(canvas, painter); break;
    case SketchCanvas::ArcMode::Tangent:        drawTangentPreview(canvas, painter); break;
    default: break;
    }
    return true;
}


bool ArcToolHandler::updateEntity(SketchCanvas& canvas, const QPointF& pos)
{
    if (canvas.arcMode() == SketchCanvas::ArcMode::Tangent) {
        // Tangent arc: update end point
        if (canvas.pendingEntityRef().points.size() > 1) {
            canvas.pendingEntityRef().points[1] = pos;
        } else {
            canvas.pendingEntityRef().points.push_back(pos);
        }
        // Apply locked dimension constraints (Radius and/or Sweep Angle)
        double lockedRadius = canvas.lockedDim(0);  // Radius (field index 0)
        double lockedSweep = canvas.lockedDim(1);   // Sweep Angle (field index 1)
        if ((lockedRadius > 0 || lockedSweep != -1.0) &&
            !!canvas.hasTangentTargets() && !(canvas.previewPointCount() == 0)) {
            const SketchEntity* tangentEntity = canvas.entityById(canvas.tangentTargets()[0]);
            if (tangentEntity) {
                // Project tangent point onto entity (same as paintEvent)
                QPointF tangentPoint = QPointF(sketch::projectOntoTangentHost(
                    *tangentEntity, Point2D(canvas.previewPoint(0))));

                if (lockedRadius > 0 && lockedSweep != -1.0) {
                    // Both locked: compute center from tangent point + locked radius,
                    // then place endpoint at locked sweep angle
                    // Get edge direction for the normal
                    const QPointF center = lockedRadiusCenter(*tangentEntity, tangentPoint, pos, lockedRadius);

                    // Start angle from center to tangent point
                    double startAngle = screenAngleDeg(center, tangentPoint);
                    double sign = (screenAngleDeg(center, pos) - startAngle > 0) ? 1.0 : -1.0;
                    if (canvas.arcSlotFlipped()) sign = -sign;
                    double endAngle = startAngle + sign * std::abs(lockedSweep);
                    double endRad = qDegreesToRadians(endAngle);
                    canvas.currentMouseWorld() = center + QPointF(lockedRadius * qCos(endRad),
                                                            lockedRadius * qSin(endRad));
                } else if (lockedRadius > 0) {
                    // Radius locked only: constrain the arc to the locked radius
                    // Calculate tangent arc first to get direction, then override radius
                    const QPointF center = lockedRadiusCenter(*tangentEntity, tangentPoint, pos, lockedRadius);

                    // Project mouse onto circle of locked radius centered at 'center'
                    QPointF dir = pos - center;
                    double dist = geometry::length(dir);
                    if (dist > geometry::kDegenerateLen) {
                        canvas.currentMouseWorld() = center + dir * (lockedRadius / dist);
                    }
                } else {
                    // Sweep angle locked only (original logic)
                    SketchCanvas::TangentArcResult ta = canvas.tangentArcFor(*tangentEntity, tangentPoint, pos);
                    if (ta.valid) {
                        double sign = (ta.sweepAngle >= 0) ? 1.0 : -1.0;
                        if (canvas.arcSlotFlipped()) sign = -sign;
                        double endAngle = ta.startAngle + sign * std::abs(lockedSweep);
                        double endRad = qDegreesToRadians(endAngle);
                        canvas.currentMouseWorld() = ta.center + Point2D(ta.radius * qCos(endRad),
                                                                    ta.radius * qSin(endRad));
                    }
                }
            }
        }
    } else if (canvas.arcMode() == SketchCanvas::ArcMode::CenterStartEnd) {
        // Apply locked dimension constraints to preview position.
        int stage = canvas.previewPointCount();
        if (stage == 1) {
            // Stage 1: center placed, defining start point → locked Radius constrains distance
            double lockedR = canvas.lockedDim(0);  // Radius
            if (lockedR > 0) {
                QPointF center = canvas.previewPoint(0);
                if (geometry::length(pos - center) > geometry::kDegenerateLen)
                    canvas.currentMouseWorld() = geometry::applyPolarLock(center, pos, lockedR, -1.0);
            }
        } else if (stage >= 2) {
            // Stage 2: center+start placed, defining end → locked Sweep constrains angle
            double lockedSweep = canvas.lockedDim(0);  // Sweep Angle (field 0 in stage 2)
            if (lockedSweep != -1.0) {
                QPointF center = canvas.previewPoint(0);
                QPointF start = canvas.previewPoint(1);
                canvas.currentMouseWorld() = QPointF(geometry::pointAtLockedSweep(
                    Point2D(center), Point2D(start), Point2D(pos), lockedSweep, canvas.arcSlotFlipped()));
            }
        }
    } else if (canvas.arcMode() == SketchCanvas::ArcMode::StartEndRadius) {
        int stage = canvas.previewPointCount();
        if (stage == 1) {
            // Stage 1: start placed, defining end → locked Chord Length/Angle
            QPointF start = canvas.previewPoint(0);
            double lockedLen = canvas.lockedDim(0);  // Chord Length
            double lockedAng = canvas.lockedDim(1);  // Chord Angle
            if (lockedLen > 0 || lockedAng != -1.0) {
                // Locked length/angle placement now lives in libhobbycad.
                canvas.currentMouseWorld() = geometry::applyPolarLock(start, pos, lockedLen, lockedAng);
            }
        } else if (stage >= 2) {
            // Stage 2: start+end placed, mouse controls arc center on perp bisector
            // Locked Sweep → compute perpendicular bisector projection distance
            double lockedSweep = canvas.lockedDim(0);  // Sweep Angle
            if (lockedSweep != -1.0) {
                QPointF start = canvas.previewPoint(0);
                QPointF end = canvas.previewPoint(1);
                canvas.currentMouseWorld() = QPointF(geometry::arcCenterFromChordAndSweep(
                    Point2D(start), Point2D(end), Point2D(pos), lockedSweep));
            }
        }
    } else if (canvas.arcMode() == SketchCanvas::ArcMode::ThreePoint) {
        // ThreePoint arc has no dim fields; no constraint support needed
    }
    return true;
}


bool ArcToolHandler::normalize(SketchCanvas& canvas, SketchEntity& entity, bool& valid)
{
    (void)canvas;
    if (entity.type == SketchEntityType::Arc) {
    // Handle tangent arc
    if (canvas.arcMode() == SketchCanvas::ArcMode::Tangent && !!canvas.hasTangentTargets() && entity.points.size() >= 2) {
        const SketchEntity* tangentEntity = canvas.entityById(canvas.tangentTargets()[0]);
        if (tangentEntity) {
            QPointF tangentPoint = entity.points[0];
            QPointF endPoint = entity.points[1];
            SketchCanvas::TangentArcResult ta = canvas.tangentArcFor(*tangentEntity, tangentPoint, endPoint);
            if (ta.valid) {
                // Apply flip if Shift was held
                double sweepAngle = ta.sweepAngle;
                if (canvas.arcSlotFlipped()) {
                    sweepAngle = hobbycad::geometry::oppositeSweepDeg(sweepAngle);
                }

                geometry::Arc stored;
                stored.center = ta.center;
                stored.radius = ta.radius;
                stored.startAngle = ta.startAngle;
                stored.sweepAngle = sweepAngle;
                storeArc(entity, stored);
                entity.tangentEntityId = canvas.tangentTargets()[0];
                valid = true;
            }
        }
        canvas.clearTangentTargets();
    } else if (canvas.arcMode() == SketchCanvas::ArcMode::ThreePoint && entity.points.size() >= 3) {
        // Calculate arc from 3 points: p1 (start) -> p2 (middle/through point) -> p3 (end)
        QPointF p1 = entity.points[0];  // start (first click)
        QPointF p2 = entity.points[1];  // through point (second click)
        QPointF p3 = entity.points[2];  // end (third click)

        // Use library function for circumcircle calculation
        auto arc = geometry::arcFromThreePoints(p1, p2, p3);
        if (arc.has_value()) {
            valid = storeArc(entity, *arc);
        }
    } else if (canvas.arcMode() == SketchCanvas::ArcMode::CenterStartEnd && entity.points.size() >= 3) {
        // Center-Start-End arc: points[0] = center, points[1] = start, points[2] = end
        QPointF center = entity.points[0];
        QPointF start = entity.points[1];
        QPointF end = entity.points[2];

        // Determine sweep direction: default to shorter path, flip if canvas.arcSlotFlipped()
        bool sweepCCW = true;
        double startAngle = screenAngleDeg(center, start);
        double endAngle = screenAngleDeg(center, end);
        double sweep = endAngle - startAngle;
        if (sweep > 180) sweep -= 360;
        if (sweep < -180) sweep += 360;
        // If sweep is positive, shorter path is CCW; if negative, shorter path is CW
        sweepCCW = (sweep > 0);
        // Flip reverses the direction
        if (canvas.arcSlotFlipped()) {
            sweepCCW = !sweepCCW;
        }

        // Use library function
        auto arc = geometry::arcFromCenterAndEndpoints(center, start, end, sweepCCW);

        valid = storeArc(entity, arc);
    } else if (canvas.arcMode() == SketchCanvas::ArcMode::StartEndRadius && entity.points.size() >= 3) {
        // Start-End-Radius arc: points[0] = start, points[1] = end, points[2] = center point
        // Third click defines arc center (constrained to perpendicular bisector)
        QPointF start = entity.points[0];
        QPointF end = entity.points[1];
        QPointF centerPoint = entity.points[2];
        double chordLength = QLineF(start, end).length();

        if (chordLength > 0.001) {
            // Arc center on the chord's perpendicular bisector at the
            // projection of the picked center point, with the min-radius floor
            // (shared with the preview via the library). [maintainability audit]
            const bool ctrlHeld =
                (QGuiApplication::queryKeyboardModifiers() & Qt::ControlModifier);
            const auto arcCtr = geometry::arcCenterOnBisector(
                start, end, centerPoint, ctrlHeld, canvas.arcSlotFlipped());
            const QPointF center(arcCtr.center.x, arcCtr.center.y);
            const double radius = arcCtr.radius;
            const double projDist = arcCtr.projection;
            const double halfChord = chordLength / 2.0;

            // Match the preview's sweep calculation exactly.
            // The preview uses screen-space angles with Qt's drawArc.
            // We need to compute the same sweep and convert to world-space for the library.

            // Calculate angles in screen space (same as preview)
            QPoint centerScreen = canvas.toScreen(center);
            QPoint startScreen = canvas.toScreen(start);
            QPoint endScreen = canvas.toScreen(end);

            double startAngleScreen = screenAngleDeg(centerScreen, startScreen);
            double endAngleScreen = screenAngleDeg(centerScreen, endScreen);

            // Calculate sweep from start to end (same as preview)
            double sweep = endAngleScreen - startAngleScreen;
            // Normalize to [-180, 180] to get the "short" path
            sweep = hobbycad::geometry::wrapSweepDeg(sweep);

            // Determine if we want the long arc based on center position
            // When center is close to chord (small |projDist|), we want the long arc
            bool wantLongArc = (std::abs(projDist) < halfChord);

            // If we want long arc, flip to the complementary sweep
            if (wantLongArc) {
                sweep = hobbycad::geometry::oppositeSweepDeg(sweep);
            }

            // Now convert screen sweep to world sweep direction.
            // Screen Y is inverted from world Y, so:
            // - Positive screen sweep (CCW on screen) = CW in world = negative world sweep
            // - Negative screen sweep (CW on screen) = CCW in world = positive world sweep
            // The library's sweepCCW=true means positive sweep in world coords.
            bool sweepCCW = (sweep < 0);  // negative screen sweep = CCW in world

            // Use library function to create the arc
            auto arc = geometry::arcFromCenterAndEndpoints(center, start, end, sweepCCW);

            valid = storeArc(entity, arc);
        }
    } else {
        // Center-point arc (original behavior / tangent arc)
        valid = entity.radius > 0.1;
        if (valid && entity.points.size() == 1) {
            // Store center + start/end endpoints
            QPointF center = entity.points[0];
            double r = entity.radius;
            double startRad = qDegreesToRadians(entity.startAngle);
            double endRad = qDegreesToRadians(entity.startAngle + entity.sweepAngle);
            entity.points.push_back(center + QPointF(r * qCos(startRad), r * qSin(startRad)));
            entity.points.push_back(center + QPointF(r * qCos(endRad), r * qSin(endRad)));
        }
    }
        return true;
    }
    // --- additional entity types handled by this tool ---
    return false;
}


bool ArcToolHandler::beginEntity(SketchCanvas& canvas, SketchEntity& entity)
{
    (void)canvas;
    entity.type = SketchEntityType::Arc;
    return true;
}

bool ArcToolHandler::isMultiClick(const SketchCanvas& canvas) const
{
    return canvas.arcMode() != SketchCanvas::ArcMode::Tangent;
}

bool ArcToolHandler::beginsOnFirstClick(const SketchCanvas& canvas) const
{
    const SketchCanvas::ArcMode m = canvas.arcMode();
    return m == SketchCanvas::ArcMode::ThreePoint
        || m == SketchCanvas::ArcMode::CenterStartEnd
        || m == SketchCanvas::ArcMode::StartEndRadius;
}

bool ArcToolHandler::keyPress(SketchCanvas& canvas, QKeyEvent* event)
{
    if (!canvas.isDrawing()) {
        return false;
    }
    const ArcMode mode = canvas.arcMode();
    const int pts = canvas.previewPointCount();

    if (event->key() == Qt::Key_Shift) {
        const bool flippable =
            ((mode == ArcMode::CenterStartEnd || mode == ArcMode::StartEndRadius) && pts >= 2)
            || (mode == ArcMode::Tangent && pts >= 1);
        if (flippable) {
            canvas.toggleArcFlip();
            canvas.update();
            return true;
        }
        return false;
    }

    if (event->key() == Qt::Key_Control
        && mode == ArcMode::StartEndRadius && pts >= 2) {
        // The 180-degree snap is applied while tracking; this only forces the
        // preview to redraw the moment Ctrl goes down.
        canvas.update();
        return true;
    }
    return false;
}

bool ArcToolHandler::constrainCursor(SketchCanvas& canvas, QPointF& world,
                                     bool altHeld)
{
    // The constraint always applies; Alt only disables ENTITY snapping.
    if (!canvas.isDrawing() || canvas.arcMode() != ArcMode::Tangent
        || canvas.previewPointCount() < 1 || !canvas.hasTangentTargets()) {
        return false;
    }
    const SketchEntity* target = canvas.findEntity(canvas.tangentTargets()[0]);
    if (!target) {
        return false;
    }

    const QPointF tangentPoint = canvas.previewPoint(0);
    const auto ta = canvas.tangentArcFor(*target, tangentPoint, world);
    if (!ta.valid || ta.radius <= 0.001) {
        return false;
    }

    // Keep an entity snap only if it lands exactly ON the arc path.
    const double distFromCenter = QLineF(world, QPointF(ta.center)).length();
    if (std::abs(distFromCenter - ta.radius) < 0.001
        && canvas.hasActiveSnap() && !altHeld) {
        return false;
    }

    // Otherwise project the RAW mouse onto the arc the raw mouse implies;
    // re-solving from the snapped position would compound the two.
    const QPointF rawMouse = canvas.rawMouseWorld();
    const auto rawTa = canvas.tangentArcFor(*target, tangentPoint, rawMouse);
    if (rawTa.valid && rawTa.radius > 0.001) {
        const Point2D toMouse = Point2D(rawMouse) - rawTa.center;
        const double dist = geometry::length(toMouse);
        if (dist > 0.001) {
            world = rawTa.center + toMouse * (rawTa.radius / dist);
        }
    }
    return true;
}


bool ArcToolHandler::applyCreationMode(SketchCanvas& canvas, int modeValue)
{
    // Arc modes: 0=ThreePoint, 1=CenterStartEnd, 2=StartEndRadius, 3=Tangent.
    // The mapping lives with the tool that acts on it, not in the canvas.
    switch (modeValue) {
    case 1:  canvas.setArcMode(SketchCanvas::ArcMode::CenterStartEnd); break;
    case 2:  canvas.setArcMode(SketchCanvas::ArcMode::StartEndRadius); break;
    case 3:  canvas.setArcMode(SketchCanvas::ArcMode::Tangent); break;
    default: canvas.setArcMode(SketchCanvas::ArcMode::ThreePoint); break;
    }
    return true;
}

}  // namespace hobbycad
