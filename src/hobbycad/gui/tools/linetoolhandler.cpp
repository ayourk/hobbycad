// =====================================================================
//  src/hobbycad/gui/tools/linetoolhandler.cpp — Line tool handler
// =====================================================================
//
//  Part of HobbyCAD.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include "linetoolhandler.h"
#include "../screenmath.h"
#include <hobbycad/units.h>

#include <QMouseEvent>
#include <QMessageBox>
#include "../sketchcanvas.h"

#include <QCoreApplication>
#include <optional>
#include <hobbycad/geometry/utils.h>

#include <cmath>
#include <QtMath>
#include <QLineF>
#include <QPen>
#include <QPainter>

namespace hobbycad {

bool LineToolHandler::initDimFields(SketchCanvas& canvas)
{
    // Line offers Length + Angle from the first placed point onward, in every
    // mode. Locking either creates a real constraint at commit (Distance and
    // Angle respectively); see createLockedConstraints().
    if (canvas.previewPointCount() < 1) {
        return true;   // owned, but nothing to offer yet
    }
    canvas.addDimField(QCoreApplication::translate("hobbycad::SketchCanvas", "Length"), false);
    canvas.addDimField(QCoreApplication::translate("hobbycad::SketchCanvas", "Angle"),  true);
    return true;
}

QString LineToolHandler::hint(const SketchCanvas& canvas) const
{
    // Tangent line is a 3-stage tool: (1) select the arc/circle to be tangent
    // to, (2) place the start point, (3) place the end point. Give each stage
    // its own prompt, like the other primitives (Aaron).
    if (canvas.lineMode() == SketchCanvas::LineMode::Tangent) {
        if (!canvas.hasTangentTargets())
            return QCoreApplication::translate("hobbycad::SketchCanvas",
                "Tangent line: click the arc or circle to be tangent to");
        if (!canvas.isDrawing())
            return QCoreApplication::translate("hobbycad::SketchCanvas",
                "Tangent line: click the start point");
        return QCoreApplication::translate("hobbycad::SketchCanvas",
            "Tangent line: click the end point  (snaps to the tangent)");
    }
    // Per-stage prompt for the status bar. HobbyCAD had no hint mechanism at
    // all before this; every jsketcher tool has one.
    if (canvas.previewPointCount() < 1) {
        return QCoreApplication::translate("hobbycad::SketchCanvas",
                                           "Line: click the start point");
    }
    if (canvas.tangentArcChainAvailable()) {
        return QCoreApplication::translate("hobbycad::SketchCanvas",
            "Line: click for the next segment, or drag for a tangent arc");
    }
    return QCoreApplication::translate("hobbycad::SketchCanvas",
                                       "Line: click the end point, or type a length");
}

QString LineToolHandler::cursorHint(const SketchCanvas& canvas) const
{
    // Tangent line has its own 3-stage wording; every other line mode uses the
    // base default (derived from hint()), so normal lines still get a hint.
    if (canvas.lineMode() != SketchCanvas::LineMode::Tangent)
        return SketchToolHandler::cursorHint(canvas);
    if (!canvas.hasTangentTargets()) return tr("(select arc/circle for tangent)");
    if (!canvas.isDrawing())         return tr("(click the start point)");
    return tr("(click the end point)");
}


bool LineToolHandler::drawPreview(SketchCanvas& canvas, QPainter& painter)
{
    // Once a tangent target is selected, highlight it so the user sees which
    // circle/arc the line will be tangent to (Aaron).
    if (canvas.lineMode() == SketchCanvas::LineMode::Tangent
        && canvas.hasTangentTargets()) {
        if (const SketchEntity* t = canvas.findEntity(canvas.tangentTargets()[0])) {
            if ((t->type == SketchEntityType::Circle
                 || t->type == SketchEntityType::Arc) && !t->points.empty()) {
                painter.save();
                painter.setPen(QPen(QColor(60, 120, 215), 2));
                painter.setBrush(Qt::NoBrush);
                const QPoint c = canvas.toScreen(QPointF(t->points[0]));
                const int rpx = static_cast<int>(std::abs(t->radius) * canvas.zoomFactor());
                painter.drawEllipse(c, rpx, rpx);
                painter.restore();
            }
        }
    }
    if (!(canvas.previewPointCount() == 0)) {
        QPoint p1 = canvas.toScreen(canvas.previewPoint(0));
        // Use constrained endpoint from updateEntity when dims are locked
        QPointF lineEndWorld = (canvas.pendingEntity().points.size() >= 2)
            ? QPointF(canvas.pendingEntity().points[1]) : canvas.currentMouseWorld();
        QPoint p2 = canvas.toScreen(lineEndWorld);
        painter.drawLine(p1, p2);

        // Draw angle snap indicator when Ctrl is held
        if (canvas.angleSnapActive()) {
            painter.save();
            painter.setPen(QPen(QColor(255, 140, 0), 1, Qt::DashLine));
            // Draw extended guide line through the snapped angle
            double len = QLineF(p1, p2).length();
            double extendLen = qMax(len * 0.3, 30.0);
            double angleRad = degreesToRadians(canvas.snappedAngle());
            QPointF dir(std::cos(angleRad), -std::sin(angleRad));  // Screen Y is inverted
            QPointF ext1 = QPointF(p1) - dir * extendLen;
            QPointF ext2 = QPointF(p2) + dir * extendLen;
            painter.drawLine(ext1.toPoint(), p1);
            painter.drawLine(p2, ext2.toPoint());

            // Draw angle label near the start point
            QString angleText = QString::fromStdString(formatAngle(canvas.snappedAngle()));
            painter.setPen(QColor(255, 140, 0));
            QFont font = painter.font();
            font.setPointSize(9);
            painter.setFont(font);
            QFontMetrics fm(font);
            QRect textRect = fm.boundingRect(angleText);
            QPoint labelPos(p1.x() + 15, p1.y() - 15);
            QRectF bgRect(labelPos.x() - 2, labelPos.y() - textRect.height(),
                          textRect.width() + 4, textRect.height() + 2);
            painter.fillRect(bgRect, QColor(255, 255, 255, 200));
            painter.drawText(labelPos, angleText);
            painter.restore();
        }

        // Draw dimension input fields below the line
        double length = QLineF(canvas.previewPoint(0), lineEndWorld).length();
        if (length > 0.1) {
            if (canvas.activeDimField() >= 0 && canvas.dimFieldCount() >= 2) {
                // Update live values for dim fields (use constrained endpoint)
                double dx = lineEndWorld.x() - canvas.previewPoint(0).x();
                double dy = lineEndWorld.y() - canvas.previewPoint(0).y();
                double angleDeg = radiansToDegrees(std::atan2(dy, dx));
                canvas.setDimFieldValue(0, length);  // Length
                canvas.setDimFieldValue(1, angleDeg); // Angle

                // Position: midpoint below line, rotated to follow edge
                QPointF midPoint = (QPointF(p1) + QPointF(p2)) / 2.0;
                double screenAngle = screenAngleDeg(p1, p2);
                bool flipped = (screenAngle > 90 || screenAngle < -90);
                if (flipped) screenAngle += 180;

                // Length field: below the line at midpoint
                QPointF lengthPos = midPoint + QPointF(0, 18);
                canvas.paintDimInputField(painter, lengthPos, 0, screenAngle);

                // Angle field: further below
                QPointF anglePos = midPoint + QPointF(0, 38);
                canvas.paintDimInputField(painter, anglePos, 1, screenAngle);
            } else {
                canvas.paintPreviewDimension(painter, p1, p2, length);
            }
        }
    }
    return true;
}


bool LineToolHandler::updateEntity(SketchCanvas& canvas, const QPointF& pos)
{
        QPointF endpoint = pos;
        QPointF start = canvas.pendingEntityRef().points[0];

        // Apply locked dimensions if any
        double lockedLen = canvas.lockedDim(0);   // field 0 = Length
        double lockedAng = canvas.lockedDim(1);   // field 1 = Angle

        // For tangent lines with locked angle, move the tangent point along the circle/arc
        if (canvas.lineMode() == SketchCanvas::LineMode::Tangent && canvas.hasTangentTargets() && lockedAng != -1.0) {
            const SketchEntity* entity = canvas.findEntity(canvas.tangentTargets()[0]);
            if (entity && !entity->points.empty() &&
                (entity->type == SketchEntityType::Circle ||
                 entity->type == SketchEntityType::Arc)) {
                QPointF center = entity->points[0];
                double radius = entity->radius;

                // Tangent angle θ means radius angle is θ ± 90°
                // Choose the direction based on which side of center the mouse is
                double angRad = qDegreesToRadians(lockedAng);
                double radiusAng1 = angRad + M_PI / 2.0;
                double radiusAng2 = angRad - M_PI / 2.0;

                // Calculate both possible tangent points
                QPointF tp1 = center + QPointF(radius * std::cos(radiusAng1), radius * std::sin(radiusAng1));
                QPointF tp2 = center + QPointF(radius * std::cos(radiusAng2), radius * std::sin(radiusAng2));

                // Choose the tangent point that puts the endpoint on the correct side
                // (the side where the mouse is pointing)
                QPointF dir1 = QPointF(std::cos(angRad), std::sin(angRad));
                double dot1 = (pos.x() - tp1.x()) * dir1.x() + (pos.y() - tp1.y()) * dir1.y();
                double dot2 = (pos.x() - tp2.x()) * dir1.x() + (pos.y() - tp2.y()) * dir1.y();

                // Use the tangent point where the mouse is in the positive direction
                start = (dot1 > dot2) ? tp1 : tp2;

                // For arcs, verify the point is on the arc (within sweep)
                if (entity->type == SketchEntityType::Arc) {
                    double startAng = entity->startAngle;
                    double sweepAng = entity->sweepAngle;
                    double pointAng = screenAngleDeg(center, start);

                    // Normalize angles
                    auto normalizeAngle = [](double a) {
                        while (a < 0) a += 360;
                        while (a >= 360) a -= 360;
                        return a;
                    };
                    pointAng = normalizeAngle(pointAng);
                    double arcStart = normalizeAngle(startAng);
                    double arcEnd = normalizeAngle(startAng + sweepAng);

                    // Check if point is on the arc
                    bool onArc;
                    if (sweepAng >= 0) {
                        if (arcEnd >= arcStart) {
                            onArc = (pointAng >= arcStart && pointAng <= arcEnd);
                        } else {
                            onArc = (pointAng >= arcStart || pointAng <= arcEnd);
                        }
                    } else {
                        if (arcEnd <= arcStart) {
                            onArc = (pointAng <= arcStart && pointAng >= arcEnd);
                        } else {
                            onArc = (pointAng <= arcStart || pointAng >= arcEnd);
                        }
                    }

                    if (!onArc) {
                        // Try the other tangent point
                        start = (dot1 > dot2) ? tp2 : tp1;
                    }
                }

                // Update the pending entity's start point
                canvas.pendingEntityRef().points[0] = start;
                // Also update preview points for visual feedback
                if (!(canvas.previewPointCount() == 0)) {
                    canvas.previewPoint(0) = start;
                }
            }
        }

        if (lockedLen > 0 || lockedAng != -1.0) {
            // Locked length/angle placement now lives in libhobbycad, so a
            // second front-end need not reimplement it.
            endpoint = geometry::applyPolarLock(start, pos, lockedLen, lockedAng);
        }
        if (canvas.pendingEntityRef().points.size() > 1) {
            canvas.pendingEntityRef().points[1] = endpoint;
        } else {
            canvas.pendingEntityRef().points.push_back(endpoint);
        }
    return true;
}


bool LineToolHandler::normalize(SketchCanvas& canvas, SketchEntity& entity, bool& valid)
{
    (void)canvas;
    if (entity.type == SketchEntityType::Line) {
    valid = entity.points.size() >= 2 &&
            QLineF(entity.points[0], entity.points[1]).length() > 0.1;
    // Clear tangent targets if this was a tangent line
    if (canvas.lineMode() == SketchCanvas::LineMode::Tangent) {
        canvas.clearTangentTargets();
    }
        return true;
    }
    // --- additional entity types handled by this tool ---
    return false;
}


bool LineToolHandler::beginEntity(SketchCanvas& canvas, SketchEntity& entity)
{
    (void)canvas;
    entity.type = SketchEntityType::Line;
    if (canvas.lineMode() == SketchCanvas::LineMode::Construction) {
        entity.isConstruction = true;
    }
    return true;
}

bool LineToolHandler::mousePress(SketchCanvas& canvas, QMouseEvent* event,
                                 const QPointF& world)
{
    if (canvas.lineMode() != SketchCanvas::LineMode::Tangent) {
        return false;   // the other line modes use the shared placement path
    }
    const QPointF worldPos = world;

    // Once the target is chosen: stage 2 places the FIRST line point (free,
    // where clicked, not projected onto the circle), stage 3 lands the far end
    // (already snapped to the external tangent by constrainCursor) and commits.
    if (canvas.hasTangentTargets()) {
        if (!canvas.isDrawing()) {
            canvas.beginDragDetection(event->pos());
            canvas.beginPlacement(canvas.snapToGeometry(worldPos));
        } else {
            canvas.trackEntity(canvas.currentMouseWorld());
            // Capture a deliberate snap on this final point too, so the generic
            // snap evaluation welds a Coincident if it landed on other geometry
            // (a snap onto the tangent target itself is excluded downstream).
            canvas.recordEndpointSnap();
            canvas.commitEntity();
        }
        canvas.update();
        return true;
    }

            // Tangent line: pick a circle/arc to be tangent to, the one under
            // the cursor, else the nearest circle/arc in the sketch (Aaron: as
            // long as one exists, the tangent line should construct, even if the
            // click is not on it).
            int hitId = canvas.pick(worldPos);
            SketchEntity* entity = (hitId >= 0) ? canvas.findEntity(hitId) : nullptr;
            if (!entity || (entity->type != SketchEntityType::Circle
                            && entity->type != SketchEntityType::Arc)) {
                hitId = canvas.nearestCircleOrArc(worldPos);
                entity = (hitId >= 0) ? canvas.findEntity(hitId) : nullptr;
            }
            if (entity && (entity->type == SketchEntityType::Circle ||
                           entity->type == SketchEntityType::Arc)) {
                    canvas.addTangentTarget(hitId);
                    canvas.showStatus(tr("Tangent target selected; click the start point."));
            } else {
                // No circle or arc in the sketch: say so in the status bar, not
                // a modal dialog (Aaron).
                canvas.showStatus(tr("Tangent line needs a circle or arc in the sketch."));
            }
    return true;
}

bool LineToolHandler::previewPen(const SketchCanvas& canvas, QPen& pen) const
{
    if (canvas.lineMode() != SketchCanvas::LineMode::Construction) {
        return false;
    }
    pen.setColor(QColor(180, 100, 50));   // construction-geometry color
    return true;
}

bool LineToolHandler::supportsAngleSnap(const SketchCanvas& canvas) const
{
    const SketchCanvas::LineMode m = canvas.lineMode();
    return m != SketchCanvas::LineMode::Horizontal
        && m != SketchCanvas::LineMode::Vertical
        && m != SketchCanvas::LineMode::Tangent;
}

bool LineToolHandler::constrainCursor(SketchCanvas& canvas, QPointF& world,
                                      bool altHeld)
{
    if (!canvas.isDrawing() || canvas.previewPointCount() < 1) {
        return false;
    }
    const QPointF start = canvas.previewPoint(0);
    // Essentially exact: the test is "is the snap point already ON the
    // constraint", not "is it near it".
    const double tolerance = 0.001;

    switch (canvas.lineMode()) {
    case SketchCanvas::LineMode::Horizontal:
        if (std::abs(world.y() - start.y()) < tolerance
            && canvas.hasActiveSnap() && !altHeld) {
            return false;   // the snap point is already on the axis
        }
        world.setY(start.y());
        return true;

    case SketchCanvas::LineMode::Vertical:
        if (std::abs(world.x() - start.x()) < tolerance
            && canvas.hasActiveSnap() && !altHeld) {
            return false;
        }
        world.setX(start.x());
        return true;

    case SketchCanvas::LineMode::Tangent: {
        if (!canvas.hasTangentTargets()) {
            return false;
        }
        const SketchEntity* entity = canvas.findEntity(canvas.tangentTargets()[0]);
        if (!entity || (entity->type != SketchEntityType::Circle
                        && entity->type != SketchEntityType::Arc)) {
            return false;
        }
        // From the free start P there are TWO external tangent lines to the
        // circle (center C, radius r): the P->C direction rotated by +/- asin(r/d).
        // Snap the cursor onto whichever tangent line is nearer, so the line
        // "snaps at 2 different angles" as it is drawn (Aaron). Alt is NOT
        // consulted: it disables entity snapping, not the tangent constraint.
        const QPointF C = entity->points[0];
        const QPointF P = start;
        const double dx = C.x() - P.x(), dy = C.y() - P.y();
        const double d = geometry::length({dx, dy});
        if (d < geometry::kDegenerateLen) return false;
        const double r = std::abs(entity->radius);
        const double th = std::asin(std::min(1.0, r / d));
        const QPointF u(dx / d, dy / d);
        auto rot = [](const QPointF& a, double ang) {
            const double c = std::cos(ang), sn = std::sin(ang);
            return QPointF(a.x() * c - a.y() * sn, a.x() * sn + a.y() * c);
        };
        const QPointF q = canvas.rawMouseWorld();
        auto foot = [&](const QPointF& dir) {
            const double t = (q.x() - P.x()) * dir.x() + (q.y() - P.y()) * dir.y();
            return QPointF(P.x() + t * dir.x(), P.y() + t * dir.y());
        };
        const QPointF f1 = foot(rot(u,  th));
        const QPointF f2 = foot(rot(u, -th));
        world = (QLineF(q, f1).length() <= QLineF(q, f2).length()) ? f1 : f2;
        return true;
    }

    default:
        return false;
    }
}


bool LineToolHandler::canSwitchModeWhileDrawing(const SketchCanvas& canvas,
                                                int modeValue) const
{
    Q_UNUSED(canvas);
    // 0 = Two Point, 4 = Construction. Both place two free points; the only
    // difference is whether the result is construction geometry.
    const bool targetIsFree = (modeValue == 0 || modeValue == 4);
    const bool currentIsFree = (canvas.lineMode() == SketchCanvas::LineMode::TwoPoint
                                || canvas.lineMode() == SketchCanvas::LineMode::Construction);
    return targetIsFree && currentIsFree;
}


bool LineToolHandler::chainsFromLastPoint(const SketchCanvas& canvas) const
{
    return canvas.lineMode() != SketchCanvas::LineMode::Tangent;
}


bool LineToolHandler::applyCreationMode(SketchCanvas& canvas, int modeValue)
{
    // Line modes: 0=TwoPoint, 1=Horizontal, 2=Vertical, 3=Tangent, 4=Construction.
    // The mapping lives with the tool that acts on it, not in the canvas.
    switch (modeValue) {
    case 1:  canvas.setLineMode(SketchCanvas::LineMode::Horizontal); break;
    case 2:  canvas.setLineMode(SketchCanvas::LineMode::Vertical); break;
    case 3:  canvas.setLineMode(SketchCanvas::LineMode::Tangent); break;
    case 4:  canvas.setLineMode(SketchCanvas::LineMode::Construction); break;
    default: canvas.setLineMode(SketchCanvas::LineMode::TwoPoint); break;
    }
    return true;
}

}  // namespace hobbycad
