// =====================================================================
//  src/hobbycad/gui/tools/circletoolhandler.cpp — Circle tool handler
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#include "circletoolhandler.h"
#include "../screenmath.h"

#include "../sketchcanvas.h"

#include <QCoreApplication>
#include <optional>
#include <hobbycad/geometry/utils.h>
#include <hobbycad/geometry/intersections.h>
#include <QLineF>
#include <QPainter>
#include <QPen>
#include <QtMath>

#include <cmath>
#include <QMouseEvent>
#include <QPointF>

namespace hobbycad {

using CircleMode = SketchCanvas::CircleMode;

bool CircleToolHandler::initDimFields(SketchCanvas& canvas)
{
    const int stage = canvas.previewPointCount();

    switch (canvas.circleMode()) {
    case CircleMode::CenterRadius:
        if (stage >= 1) canvas.addDimField(tr("Radius"), false);
        break;
    case CircleMode::TwoPoint:
        // Two clicks span a DIAMETER, so the field is diameter, not radius.
        if (stage >= 1) canvas.addDimField(tr("Diameter"), false);
        break;
    case CircleMode::ThreePoint:
        // Radius only from stage 2: with fewer than two points on the
        // perimeter the radius is not yet meaningful. Locking it here makes
        // the third click choose between the two candidate centers.
        if (stage >= 2) canvas.addDimField(tr("Radius"), false);
        break;
    case CircleMode::TwoTangent:
    case CircleMode::ThreeTangent:
        // Tangent modes consume clicks as entity picks; no dimensional input.
        break;
    }
    return true;
}

QString CircleToolHandler::hint(const SketchCanvas& canvas) const
{
    const int stage = canvas.previewPointCount();
    const int targets = canvas.tangentTargetCount();

    switch (canvas.circleMode()) {
    case CircleMode::CenterRadius:
        return stage < 1 ? tr("Circle: click the center")
                         : tr("Circle: click to set the radius, or type one");
    case CircleMode::TwoPoint:
        return stage < 1 ? tr("Circle (2-point): click one end of the diameter")
                         : tr("Circle (2-point): click the other end, or type a diameter");
    case CircleMode::ThreePoint:
        if (stage < 1) return tr("Circle (3-point): click the first point");
        if (stage < 2) return tr("Circle (3-point): click the second point");
        return tr("Circle (3-point): click the third point, or type a radius");
    case CircleMode::TwoTangent:
        if (targets < 2)
            return tr("Tangent circle: click 2 curves to be tangent to (%1 of 2)")
                       .arg(targets);
        return tr("Tangent circle: click to place");
    case CircleMode::ThreeTangent:
        if (targets < 3)
            return tr("Tangent circle: click 3 curves to be tangent to (%1 of 3)")
                       .arg(targets);
        return tr("Tangent circle: click to place");
    }
    return {};
}


bool CircleToolHandler::mousePress(SketchCanvas& canvas, QMouseEvent* event, const QPointF& world)
{
    const CircleMode mode = canvas.circleMode();

    if (mode == CircleMode::TwoTangent || mode == CircleMode::ThreeTangent) {
        // Tangent circle: clicks pick the entities to be tangent to. Once
        // enough targets exist, the next click places the circle itself.
        const int hitId = canvas.pick(world);
        if (hitId >= 0 && !canvas.tangentTargets().contains(hitId)) {
            canvas.addTangentTarget(hitId);
            const int need = (mode == CircleMode::TwoTangent) ? 2 : 3;
            if (canvas.tangentTargetCount() >= need) {
                canvas.beginPlacement(canvas.snapToGeometry(world));
            }
            canvas.update();
        }
        return true;
    }

    // Only ThreePoint is staged. CenterRadius/TwoPoint place with two clicks
    // through the shared path.
    if (mode != CircleMode::ThreePoint || !canvas.isDrawing()) {
        return false;
    }

    canvas.beginDragDetection(event->pos());   // per STAGE, not per entity

    // No constraint step: the lockable Radius is applied at commit, in
    // finishEntity(), where it picks between the two candidate centers.
    canvas.appendPlacementPoint(canvas.snapToGeometry(world));

    if (canvas.pendingEntity().points.size() >= 3) canvas.commitEntity();
    else                                           canvas.refreshDimFields();
    return true;
}

bool CircleToolHandler::mouseRelease(SketchCanvas& canvas, QMouseEvent*, const QPointF& world)
{
    if (canvas.circleMode() != CircleMode::ThreePoint || !canvas.isDrawing()) {
        return false;
    }

    if (!canvas.wasDragged()) {
        if (canvas.pendingEntity().points.size() >= 3) canvas.commitEntity();
        return true;
    }

    canvas.appendPlacementPoint(canvas.snapToGeometry(world));
    if (canvas.pendingEntity().points.size() >= 3) canvas.commitEntity();
    else                                           canvas.refreshDimFields();
    return true;
}


bool CircleToolHandler::drawPreview(SketchCanvas& canvas, QPainter& painter)
{
    if (!(canvas.previewPointCount() == 0)) {
        QPointF centerWorld;
        double r = 0.0;

        if (canvas.circleMode() == SketchCanvas::CircleMode::TwoPoint) {
            // Two-point (diameter) mode: first point is one end of diameter
            QPointF p1 = canvas.previewPoint(0);
            // Use constrained endpoint from updateEntity
            QPointF p2 = (canvas.pendingEntity().points.size() >= 2)
                ? QPointF(canvas.pendingEntity().points[1]) : canvas.currentMouseWorld();
            centerWorld = (p1 + p2) / 2.0;
            r = QLineF(p1, p2).length() / 2.0;

            QPoint center = canvas.toScreen(centerWorld);
            int rPx = static_cast<int>(r * canvas.zoomFactor());
            painter.drawEllipse(center, rPx, rPx);

            // Draw center point marker
            paintCenterCross(painter, center, 4);

            // Draw diameter line and endpoint markers
            QPoint sp1 = canvas.toScreen(p1);
            QPoint sp2 = canvas.toScreen(p2);
            painter.save();
            painter.setPen(QPen(QColor(128, 128, 128), 1, Qt::DashLine));
            painter.drawLine(sp1, sp2);
            painter.restore();

            // Draw perimeter point markers (the two diameter endpoints)
            paintCenterCross(painter, sp1, 4);
            paintCenterCross(painter, sp2, 4);

            // Draw diameter dimension
            if (r > 0.05) {
                if (canvas.activeDimField() >= 0 && !(canvas.dimFieldCount() == 0)) {
                    canvas.setDimFieldValue(0, r * 2.0);
                    QPointF midPt = (QPointF(sp1) + QPointF(sp2)) / 2.0 + QPointF(0, 18);
                    canvas.paintDimInputField(painter, midPt, 0);
                } else {
                    canvas.paintPreviewDimension(painter, sp1, sp2, r * 2.0);
                }
            }
        } else if (canvas.circleMode() == SketchCanvas::CircleMode::ThreePoint) {
            // Three-point circle: calculate circumcircle through points
            QVector<QPointF> pts = canvas.previewPoints();
            pts.append(canvas.currentMouseWorld());  // Add current mouse as next point

            double lockedR = canvas.lockedDim(0);  // Radius field (available at stage 2)

            if (pts.size() >= 3) {
                bool drawn = false;
                if (lockedR > 0) {
                    // Locked radius: compute center on perpendicular bisector of p1-p2
                    QPointF p1 = pts[0], p2 = pts[1], p3 = pts[2];
                    const geometry::ChordCenters cc =
                        geometry::circleCentersThroughPoints(p1, p2, lockedR);
                    if (cc.valid) {
                        QPointF c1 = cc.first;
                        QPointF c2 = cc.second;
                        double d1 = QLineF(c1, p3).length();
                        double d2 = QLineF(c2, p3).length();
                        centerWorld = (d1 <= d2) ? c1 : c2;
                        r = lockedR;
                        // Project p3 onto circle for display
                        QPointF dir3 = p3 - centerWorld;
                        double dir3Len = geometry::length(dir3);
                        QPointF p3proj = p3;
                        if (dir3Len > geometry::kDegenerateLen)
                            p3proj = centerWorld + dir3 * (lockedR / dir3Len);
                        pts[2] = p3proj;
                        drawn = true;
                    }
                }
                if (!drawn) {
                    // Use library function for circumcircle calculation
                    auto arc = geometry::arcFromThreePoints(pts[0], pts[1], pts[2]);
                    if (arc.has_value()) {
                        centerWorld = arc->center;
                        r = arc->radius;
                        drawn = true;
                    }
                }
                if (drawn) {
                    QPoint center = canvas.toScreen(centerWorld);
                    int rPx = static_cast<int>(r * canvas.zoomFactor());
                    painter.drawEllipse(center, rPx, rPx);

                    // Draw center point marker
                    paintCenterCross(painter, center, 4);

                    // Draw the 3 perimeter points (no quadrant markers for 3-point circles)
                    for (int i = 0; i < 3 && i < pts.size(); ++i) {
                        QPoint pt = canvas.toScreen(pts[i]);
                        paintCenterCross(painter, pt, 4);
                    }

                    // Draw radius dimension from center to first point
                    if (r > 0.1) {
                        if (canvas.activeDimField() >= 0 && !(canvas.dimFieldCount() == 0)) {
                            canvas.setDimFieldValue(0, r);
                            QPoint sp1 = canvas.toScreen(pts[0]);
                            QPointF midPt = (QPointF(center) + QPointF(sp1)) / 2.0 + QPointF(0, 18);
                            canvas.paintDimInputField(painter, midPt, 0);
                        } else {
                            QPoint sp1 = canvas.toScreen(pts[0]);
                            canvas.paintPreviewDimension(painter, center, sp1, r);
                        }
                    }
                }
            } else if (pts.size() == 2) {
                // Only 2 points - show the line between them and point markers
                QPoint sp1 = canvas.toScreen(pts[0]);
                QPoint sp2 = canvas.toScreen(pts[1]);
                painter.save();
                painter.setPen(QPen(QColor(128, 128, 128), 1, Qt::DashLine));
                painter.drawLine(sp1, sp2);
                painter.restore();

                // Draw cross markers for placed points
                paintCenterCross(painter, sp1, 4);
                paintCenterCross(painter, sp2, 4);

                // If radius is locked, show the constrained circle preview
                if (lockedR > 0) {
                    const geometry::ChordCenters cc =
                        geometry::circleCentersThroughPoints(pts[0], pts[1], lockedR);
                    if (cc.valid) {
                        // Show both possible circles as dashed
                        painter.save();
                        painter.setPen(QPen(QColor(128, 128, 128, 100), 1, Qt::DashLine));
                        QPointF c1 = cc.first;
                        QPointF c2 = cc.second;
                        QPoint sc1 = canvas.toScreen(c1);
                        QPoint sc2 = canvas.toScreen(c2);
                        int rPx = static_cast<int>(lockedR * canvas.zoomFactor());
                        painter.drawEllipse(sc1, rPx, rPx);
                        painter.drawEllipse(sc2, rPx, rPx);
                        painter.restore();
                    }
                }
            }
        } else {
            // Center-radius mode - use constrained point from updateEntity
            centerWorld = canvas.previewPoint(0);
            QPointF perimWorld = (canvas.pendingEntity().points.size() >= 2)
                ? QPointF(canvas.pendingEntity().points[1]) : canvas.currentMouseWorld();
            r = QLineF(centerWorld, perimWorld).length();

            QPoint center = canvas.toScreen(centerWorld);
            int rPx = static_cast<int>(r * canvas.zoomFactor());
            painter.drawEllipse(center, rPx, rPx);

            // Draw center point marker
            paintCenterCross(painter, center, 4);

            // Draw perimeter point marker
            QPoint radiusEnd = canvas.toScreen(perimWorld);
            paintCenterCross(painter, radiusEnd, 4);

            // Draw radius dimension
            if (r > 0.1) {
                if (canvas.activeDimField() >= 0 && !(canvas.dimFieldCount() == 0)) {
                    canvas.setDimFieldValue(0, r);
                    QPointF midPt = (QPointF(center) + QPointF(radiusEnd)) / 2.0 + QPointF(0, 18);
                    canvas.paintDimInputField(painter, midPt, 0);
                } else {
                    canvas.paintPreviewDimension(painter, center, radiusEnd, r);
                }
            }
        }
    }
    return true;
}


bool CircleToolHandler::updateEntity(SketchCanvas& canvas, const QPointF& pos)
{
    if (!canvas.pendingEntityRef().points.empty()) {
        if (canvas.circleMode() == SketchCanvas::CircleMode::TwoPoint) {
            // Two-point (diameter) mode: point[0] is one end, mouse is other end
            QPointF p1 = canvas.pendingEntityRef().points[0];
            QPointF endpoint = pos;
            double lockedD = canvas.lockedDim(0);  // field 0 = Diameter
            if (lockedD > 0) {
                // Constrain endpoint at locked diameter distance in mouse direction
                if (geometry::length(pos - p1) > geometry::kDegenerateLen)
                    endpoint = geometry::applyPolarLock(p1, pos, lockedD, -1.0);
            }
            double diameter = QLineF(p1, endpoint).length();
            canvas.pendingEntityRef().radius = diameter / 2.0;
            if (canvas.pendingEntityRef().points.size() > 1) {
                canvas.pendingEntityRef().points[1] = endpoint;
            } else {
                canvas.pendingEntityRef().points.push_back(endpoint);
            }
        } else if (canvas.circleMode() == SketchCanvas::CircleMode::ThreePoint) {
            // ThreePoint mode: points are added on click, not on mouse move
            // Just update canvas.currentMouseWorld() which is used by drawPreview()
            // Don't modify canvas.pendingEntityRef().points here - that breaks click-click mode.
        } else {
            // Center-radius mode: point[0] is center, mouse defines radius
            double lockedR = canvas.lockedDim(0);  // field 0 = Radius
            QPointF center = canvas.pendingEntityRef().points[0];
            QPointF perimPt = pos;
            if (lockedR > 0) {
                // Constrain to locked radius in mouse direction
                if (geometry::length(pos - center) > geometry::kDegenerateLen)
                    perimPt = geometry::applyPolarLock(center, pos, lockedR, -1.0);
                canvas.pendingEntityRef().radius = lockedR;
            } else {
                canvas.pendingEntityRef().radius = QLineF(center, pos).length();
            }
            // Store the perimeter point
            if (canvas.pendingEntityRef().points.size() > 1) {
                canvas.pendingEntityRef().points[1] = perimPt;
            } else {
                canvas.pendingEntityRef().points.push_back(perimPt);
            }
        }
    }
    return true;
}


bool CircleToolHandler::normalize(SketchCanvas& canvas, SketchEntity& entity, bool& valid)
{
    (void)canvas;
    if (entity.type == SketchEntityType::Circle) {
    // Handle tangent circles
    if (canvas.circleMode() == SketchCanvas::CircleMode::TwoTangent && canvas.tangentTargetCount() >= 2) {
        const SketchEntity* e1 = nullptr;
        const SketchEntity* e2 = nullptr;
        e1 = canvas.entityById(canvas.tangentTargets()[0]);
        e2 = canvas.entityById(canvas.tangentTargets()[1]);
        if (e1 && e2 && !entity.points.empty()) {
            SketchCanvas::TangentCircleResult tc = canvas.tangentCircleFor(*e1, *e2, entity.points[0]);
            if (tc.valid) {
                entity.points.clear();
                entity.points.push_back(tc.center);
                entity.points.push_back(Point2D(tc.center.x + tc.radius, tc.center.y));
                entity.radius = tc.radius;
                valid = true;
            }
        }
        canvas.clearTangentTargets();
    } else if (canvas.circleMode() == SketchCanvas::CircleMode::ThreeTangent && canvas.tangentTargetCount() >= 3) {
        const SketchEntity* e1 = nullptr;
        const SketchEntity* e2 = nullptr;
        const SketchEntity* e3 = nullptr;
        e1 = canvas.entityById(canvas.tangentTargets()[0]);
        e2 = canvas.entityById(canvas.tangentTargets()[1]);
        e3 = canvas.entityById(canvas.tangentTargets()[2]);
        if (e1 && e2 && e3) {
            SketchCanvas::TangentCircleResult tc = canvas.tangentCircleFor(*e1, *e2, *e3);
            if (tc.valid) {
                entity.points.clear();
                entity.points.push_back(tc.center);
                entity.points.push_back(Point2D(tc.center.x + tc.radius, tc.center.y));
                entity.radius = tc.radius;
                valid = true;
            }
        }
        canvas.clearTangentTargets();
    } else if (canvas.circleMode() == SketchCanvas::CircleMode::TwoPoint) {
        // Two-point (diameter) circle: points[0] is first diameter end, points[1] is second
        valid = entity.radius > 0.1;
        if (valid && entity.points.size() >= 2) {
            QPointF p1 = entity.points[0];
            QPointF p2 = entity.points[1];
            QPointF center = (p1 + p2) / 2.0;
            double diameter = QLineF(p1, p2).length();
            entity.radius = diameter / 2.0;
            // Store as [center, p1, p2] - the two diameter endpoints
            entity.points.clear();
            entity.points.push_back(center);
            entity.points.push_back(p1);
            entity.points.push_back(p2);
        }
    } else if (canvas.circleMode() == SketchCanvas::CircleMode::ThreePoint) {
        // Three-point circle: calculate circumcircle from 3 points
        // Store as: [center, p1, p2, p3] where p1, p2, p3 are the clicked points on perimeter
        if (entity.points.size() >= 3) {
            QPointF p1 = entity.points[0];
            QPointF p2 = entity.points[1];
            QPointF p3 = entity.points[2];

            // Check for locked radius
            double lockedR = canvas.lockedDim(0);  // Radius field (stage 2, field 0)

            if (lockedR > 0) {
                // Locked radius: compute center on perpendicular bisector of p1-p2
                // that gives the locked radius, then project p3 onto that circle
                Point2D center;
                if (geometry::lockedRadiusCenterToward(p1, p2, lockedR, p3, center)) {
                    {
                        // Project p3 onto the circle
                        if (geometry::lineLength(center, p3) > geometry::kDegenerateLen)
                            p3 = geometry::closestPointOnCircle(p3, center, lockedR);
                        entity.radius = lockedR;
                        entity.points.clear();
                        entity.points.push_back(center);
                        entity.points.push_back(p1);
                        entity.points.push_back(p2);
                        entity.points.push_back(p3);
                        valid = true;
                    }
                }
                // else: p1-p2 too far apart for locked radius; fall through to normal
            }

            if (!valid) {
                // Use library function for circumcircle calculation
                auto arc = geometry::arcFromThreePoints(p1, p2, p3);
                if (arc.has_value() && arc->radius > 0.1) {
                    entity.radius = arc->radius;
                    entity.points.clear();
                    entity.points.push_back(arc->center);  // points[0] = center
                    entity.points.push_back(p1);           // points[1] = first clicked point
                    entity.points.push_back(p2);           // points[2] = second clicked point
                    entity.points.push_back(p3);           // points[3] = third clicked point
                    valid = true;
                }
            }
        }
    } else {
        // Standard center-radius circle
        // points[0] = center, points[1] = clicked perimeter point (set by updateEntity)
        valid = entity.radius > 0.1 && entity.points.size() >= 2;
    }
        return true;
    }
    // --- additional entity types handled by this tool ---
    return false;
}


bool CircleToolHandler::beginEntity(SketchCanvas& canvas, SketchEntity& entity)
{
    (void)canvas;
    entity.type = SketchEntityType::Circle;
    return true;
}

bool CircleToolHandler::isMultiClick(const SketchCanvas& canvas) const
{
    return canvas.circleMode() == SketchCanvas::CircleMode::ThreePoint;
}

bool CircleToolHandler::beginsOnFirstClick(const SketchCanvas& canvas) const
{
    return canvas.circleMode() == SketchCanvas::CircleMode::ThreePoint;
}


bool CircleToolHandler::applyCreationMode(SketchCanvas& canvas, int modeValue)
{
    // Circle modes: 0=CenterRadius, 1=TwoPoint (diameter), 2=ThreePoint,
    // 3=TwoTangent, 4=ThreeTangent. Moved off SketchCanvas::setCreationMode()
    // so the mapping lives with the code that acts on it.
    switch (modeValue) {
    case 1:  canvas.setCircleMode(SketchCanvas::CircleMode::TwoPoint);     break;
    case 2:  canvas.setCircleMode(SketchCanvas::CircleMode::ThreePoint);   break;
    case 3:  canvas.setCircleMode(SketchCanvas::CircleMode::TwoTangent);   break;
    case 4:  canvas.setCircleMode(SketchCanvas::CircleMode::ThreeTangent); break;
    default: canvas.setCircleMode(SketchCanvas::CircleMode::CenterRadius); break;
    }
    return true;
}
}  // namespace hobbycad
