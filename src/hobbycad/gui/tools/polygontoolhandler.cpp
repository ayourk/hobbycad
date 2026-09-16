// =====================================================================
//  src/hobbycad/gui/tools/polygontoolhandler.cpp
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#include "polygontoolhandler.h"
#include "../screenmath.h"
#include <hobbycad/units.h>
#include "../sketchcanvas.h"

#include <QCoreApplication>
#include <optional>
#include <hobbycad/geometry/utils.h>

#include <cmath>
#include <QtMath>
#include <QLineF>
#include <QPen>
#include <QPainter>
#include <QWheelEvent>
#include <QtGlobal>

namespace hobbycad {

using PolygonMode = SketchCanvas::PolygonMode;

bool PolygonToolHandler::initDimFields(SketchCanvas& canvas)
{
    // Freeform has no radius; the regular modes do. Note the locked "Radius"
    // for Circumscribed means the INSCRIBED radius (apothem); finishEntity
    // converts it to a vertex distance as radius / cos(pi / sides).
    if (canvas.polygonMode() != PolygonMode::Freeform && canvas.previewPointCount() >= 1) {
        canvas.addDimField(tr("Radius"), false);
    }
    return true;
}

bool PolygonToolHandler::wheel(SketchCanvas& canvas, QWheelEvent* event)
{
    if (!canvas.isDrawing() || canvas.polygonMode() == PolygonMode::Freeform) {
        return false;   // fall through to zoom
    }
    const int delta = event->angleDelta().y() > 0 ? 1 : -1;
    SketchEntity& e = canvas.pendingEntityRef();
    e.sides = qBound(3, e.sides + delta, 64);
    return true;
}

QString PolygonToolHandler::hint(const SketchCanvas& canvas) const
{
    if (canvas.polygonMode() == PolygonMode::Freeform) {
        return canvas.previewPointCount() < 3
            ? tr("Polygon: click to add vertices")
            : tr("Polygon: click to add vertices, or click the first to close");
    }
    if (canvas.previewPointCount() < 1) {
        return tr("Polygon: click the center");
    }
    return tr("Polygon: click to set the radius, or type one  (scroll = side count)");
}


bool PolygonToolHandler::drawPreview(SketchCanvas& canvas, QPainter& painter)
{
    if (!(canvas.previewPointCount() == 0)) {
        if (canvas.polygonMode() == SketchCanvas::PolygonMode::Freeform) {
            // Freeform polygon: draw polyline through clicked vertices + mouse
            QVector<QPoint> screenPts;
            for (const QPointF& wp : canvas.previewPoints()) {
                screenPts.append(canvas.toScreen(wp));
            }
            QPoint mousePt = canvas.toScreen(canvas.currentMouseWorld());
            screenPts.append(mousePt);

            // Draw edges
            for (int i = 0; i < screenPts.size() - 1; ++i) {
                painter.drawLine(screenPts[i], screenPts[i + 1]);
            }

            // Draw closing line from mouse back to start (dashed hint)
            if (canvas.previewPointCount() >= 2) {
                QPen savedPen = painter.pen();
                QPen dashPen = savedPen;
                dashPen.setStyle(Qt::DashLine);
                dashPen.setColor(QColor(100, 100, 100, 128));
                painter.setPen(dashPen);
                painter.drawLine(mousePt, screenPts[0]);
                painter.setPen(savedPen);  // Restore
            }

            // Draw vertex dots
            painter.setBrush(QColor(0, 120, 215));
            for (int i = 0; i < canvas.previewPointCount(); ++i) {
                painter.drawEllipse(screenPts[i], 3, 3);
            }

            // Highlight start point green if mouse is close (snap-to-close)
            if (canvas.previewPointCount() >= 3) {
                double distToStart = QLineF(canvas.currentMouseWorld(), canvas.previewPoint(0)).length();
                double snapDist = canvas.entitySnapTolerance() / canvas.zoomFactor();
                if (distToStart < snapDist) {
                    painter.setBrush(QColor(0, 180, 100));
                    painter.drawEllipse(screenPts[0], 5, 5);
                }
            }
        } else {
            // Regular polygon: center + radius + sides
            QPointF center = canvas.previewPoint(0);
            // Use constrained radius from updateEntity
            double radius = canvas.pendingEntity().radius;
            int sides = canvas.pendingEntity().sides > 0 ? canvas.pendingEntity().sides : 6;

            if (radius > 0.1) {
                QPoint sc = canvas.toScreen(center);
                int rPx = static_cast<int>(radius * canvas.zoomFactor());

                // Draw construction circle behind polygon (dashed, construction color)
                painter.save();
                QPen constructionPen(QColor(180, 100, 50), 1, Qt::DashLine);
                painter.setPen(constructionPen);
                painter.setBrush(Qt::NoBrush);
                painter.drawEllipse(sc, rPx, rPx);
                painter.restore();

                // Vertices from the shared library generator, then to screen.
                double startAngle = std::atan2(canvas.currentMouseWorld().y() - center.y(),
                                               canvas.currentMouseWorld().x() - center.x());
                const bool circumscribed =
                    canvas.polygonMode() == SketchCanvas::PolygonMode::Circumscribed;
                QPolygonF poly;
                for (const auto& v : geometry::regularPolygonVertices(
                         center, radius, sides, startAngle, circumscribed))
                    poly << canvas.toScreenF(QPointF(v.x, v.y));
                if (!poly.isEmpty()) {
                    poly << poly.first();  // Close the polygon
                    painter.drawPolyline(poly);
                }

                // Draw center point
                painter.setBrush(QColor(0, 120, 215));
                painter.drawEllipse(sc, 3, 3);

                // Draw radius dimension along dashed radius line
                QPoint radiusEnd = canvas.toScreen(canvas.currentMouseWorld());
                {
                    // Dashed radius line from center to cursor
                    painter.save();
                    painter.setPen(QPen(QColor(128, 128, 128), 1, Qt::DashLine));
                    painter.drawLine(sc, radiusEnd);
                    painter.restore();

                    // Cross marker at cursor point
                    int crossSz = 4;
                    paintCenterCross(painter, radiusEnd, crossSz);

                    // Label at midpoint of radius line, offset perpendicular
                    QPointF midPt = (QPointF(sc) + QPointF(radiusEnd)) / 2.0;
                    double rdx = radiusEnd.x() - sc.x();
                    double rdy = radiusEnd.y() - sc.y();
                    double rlen = geometry::length({rdx, rdy});

                    // Perpendicular offset (always to the right of the line direction)
                    QPointF perpOff(0, 14);
                    if (rlen > 1.0) {
                        perpOff = QPointF(-rdy / rlen * 14, rdx / rlen * 14);
                    }
                    QPointF labelPos = midPt + perpOff;

                    if (canvas.activeDimField() >= 0 && !(canvas.dimFieldCount() == 0)) {
                        canvas.setDimFieldValue(0, radius);
                        double screenAngle = radiansToDegrees(std::atan2(rdy, rdx));
                        if (screenAngle > 90 || screenAngle < -90)
                            screenAngle += 180;
                        canvas.paintDimInputField(painter, labelPos, 0, screenAngle);
                    } else {
                        canvas.paintDimensionLabel(painter, labelPos, radius);
                    }
                }
            }
        }
    }
    return true;
}


bool PolygonToolHandler::updateEntity(SketchCanvas& canvas, const QPointF& pos)
{
    if (!canvas.pendingEntityRef().points.empty()) {
        double lockedR = canvas.lockedDim(0);  // field 0 = Radius
        if (lockedR > 0) {
            canvas.pendingEntityRef().radius = lockedR;
            // Constrain cursor position to locked radius (keeps angle, fixes distance)
            QPointF center = canvas.pendingEntityRef().points[0];
            if (geometry::length(pos - center) > geometry::kDegenerateLen)
                canvas.currentMouseWorld() = geometry::applyPolarLock(center, pos, lockedR, -1.0);
        } else {
            canvas.pendingEntityRef().radius = QLineF(canvas.pendingEntityRef().points[0], pos).length();
        }
    }
    return true;
}


bool PolygonToolHandler::normalize(SketchCanvas& canvas, SketchEntity& entity, bool& valid)
{
    (void)canvas;
    if (entity.type == SketchEntityType::Polygon) {
    if (canvas.polygonMode() == SketchCanvas::PolygonMode::Freeform) {
        // Freeform polygon: need at least 3 vertices (triangle)
        valid = entity.points.size() >= 3;
        if (valid) {
            entity.sides = entity.points.size();
        }
    } else {
        // Regular polygon: center + radius
        valid = entity.radius > 0.1;
        if (valid && entity.points.size() == 1) {
            QPointF center = entity.points[0];
            int sides = entity.sides > 0 ? entity.sides : 6;
            double radius = entity.radius;
            double startAngle = std::atan2(canvas.currentMouseWorld().y() - center.y(),
                                           canvas.currentMouseWorld().x() - center.x());
            const bool circumscribed =
                canvas.polygonMode() == SketchCanvas::PolygonMode::Circumscribed;
            // points[0] = center, points[1..N] = vertices; the library generator
            // owns the inscribed/circumscribed + orientation math.
            for (const auto& v : geometry::regularPolygonVertices(
                     center, radius, sides, startAngle, circumscribed))
                entity.points.push_back(v);
        }
    }
        return true;
    }
    // --- additional entity types handled by this tool ---
    return false;
}


bool PolygonToolHandler::beginEntity(SketchCanvas& canvas, SketchEntity& entity)
{
    (void)canvas;
    entity.type = SketchEntityType::Polygon;
    if (canvas.polygonMode() != SketchCanvas::PolygonMode::Freeform) {
        entity.sides = 6;   // default hexagon; the wheel adjusts it
    }
    // Freeform derives its side count from the point count at finish time.
    return true;
}

bool PolygonToolHandler::mouseRelease(SketchCanvas& canvas, QMouseEvent*,
                                      const QPointF& world)
{
    if (!canvas.isDrawing()
        || canvas.polygonMode() != SketchCanvas::PolygonMode::Freeform) {
        return false;
    }

    const QPointF snapped = canvas.snapToGeometry(world);

    // Releasing within snap range of the first vertex closes the polygon, and
    // that click is NOT added as another vertex.
    if (canvas.previewPointCount() >= 3) {
        const double distToStart = QLineF(snapped, canvas.previewPoint(0)).length();
        const double snapDist = canvas.entitySnapTolerance() / canvas.zoomFactor();
        if (distToStart < snapDist) {
            canvas.commitEntity();
            return true;
        }
    }

    canvas.appendPlacementPoint(snapped);
    canvas.update();
    // Deliberately does not finish: the user keeps clicking, or ends the
    // polygon with a right-click or Enter.
    return true;
}

bool PolygonToolHandler::finishesOnRightClick(const SketchCanvas& canvas) const
{
    return canvas.polygonMode() == SketchCanvas::PolygonMode::Freeform;
}

bool PolygonToolHandler::isMultiClick(const SketchCanvas& canvas) const
{
    return canvas.polygonMode() == SketchCanvas::PolygonMode::Freeform;
}


bool PolygonToolHandler::applyCreationMode(SketchCanvas& canvas, int modeValue)
{
    // Polygon modes: 0=Inscribed, 1=Circumscribed, 2=Freeform.
    // The mapping lives with the tool that acts on it, not in the canvas.
    switch (modeValue) {
    case 1:  canvas.setPolygonMode(SketchCanvas::PolygonMode::Circumscribed); break;
    case 2:  canvas.setPolygonMode(SketchCanvas::PolygonMode::Freeform); break;
    default: canvas.setPolygonMode(SketchCanvas::PolygonMode::Inscribed); break;
    }
    return true;
}

}  // namespace hobbycad
