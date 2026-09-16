// =====================================================================
//  src/hobbycad/gui/tools/ellipsetoolhandler.cpp — Ellipse tool handler
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#include "ellipsetoolhandler.h"
#include "../sketchcanvas.h"
#include <QCoreApplication>
#include <optional>
#include <hobbycad/geometry/utils.h>
#include <cmath>
#include <QtMath>
#include <QLineF>
#include <QPainter>
#include <QPointF>

namespace hobbycad {

bool EllipseToolHandler::initDimFields(SketchCanvas& canvas)
{
    if (canvas.previewPointCount() >= 1) {
        canvas.addDimField(QCoreApplication::translate("hobbycad::SketchCanvas",
                                                       "Major Radius"), false);
    }
    return true;
}

QString EllipseToolHandler::hint(const SketchCanvas& canvas) const
{
    const char* s = (canvas.previewPointCount() < 1)
        ? "Ellipse: click the center"
        : "Ellipse: click to set the radii, or type a major radius";
    return QCoreApplication::translate("hobbycad::SketchCanvas", s);
}


bool EllipseToolHandler::drawPreview(SketchCanvas& canvas, QPainter& painter)
{
            if (!(canvas.previewPointCount() == 0)) {
                QPointF center = canvas.previewPoint(0);
                // Use constrained edge point from updateEntity
                QPointF edge = (canvas.pendingEntity().points.size() >= 2)
                    ? QPointF(canvas.pendingEntity().points[1]) : canvas.currentMouseWorld();
                double majorR = QLineF(center, edge).length();

                if (majorR > 0.1) {
                    QPoint sc = canvas.toScreen(center);
                    int majorPx = static_cast<int>(majorR * canvas.zoomFactor());
                    int minorPx = majorPx / 2;  // Default 2:1 aspect ratio for preview

                    // For now, draw axis-aligned ellipse preview
                    painter.drawEllipse(sc, majorPx, minorPx);

                    // Draw center and edge points
                    painter.setBrush(QColor(0, 120, 215));
                    painter.drawEllipse(sc, 3, 3);
                    QPoint edgeScreen = canvas.toScreen(edge);
                    painter.drawEllipse(edgeScreen, 3, 3);

                    // Draw major radius dimension
                    if (canvas.activeDimField() >= 0 && !(canvas.dimFieldCount() == 0)) {
                        canvas.setDimFieldValue(0, majorR);
                        QPointF midPt = (QPointF(sc) + QPointF(edgeScreen)) / 2.0 + QPointF(0, 18);
                        canvas.paintDimInputField(painter, midPt, 0);
                    } else {
                        canvas.paintPreviewDimension(painter, sc, edgeScreen, majorR);
                    }
                }
    }
    return true;
}


bool EllipseToolHandler::updateEntity(SketchCanvas& canvas, const QPointF& pos)
{
        QPointF edgePt = pos;
        double lockedR = canvas.lockedDim(0);  // field 0 = Major Radius
        if (lockedR > 0 && !canvas.pendingEntityRef().points.empty()) {
            QPointF center = canvas.pendingEntityRef().points[0];
            if (geometry::length(pos - center) > geometry::kDegenerateLen)
                edgePt = geometry::applyPolarLock(center, pos, lockedR, -1.0);
        }
        if (canvas.pendingEntityRef().points.size() > 1) {
            canvas.pendingEntityRef().points[1] = edgePt;
        } else {
            canvas.pendingEntityRef().points.push_back(edgePt);
        }
    return true;
}


bool EllipseToolHandler::normalize(SketchCanvas& canvas, SketchEntity& entity, bool& valid)
{
    (void)canvas;
    if (entity.type == SketchEntityType::Ellipse) {
    valid = entity.points.size() >= 2;
    if (valid) {
        // Calculate major and minor radii from the two points
        QPointF center = entity.points[0];
        QPointF majorPoint = entity.points[1];
        entity.majorRadius = QLineF(center, majorPoint).length();
        entity.minorRadius = entity.majorRadius * 0.5;  // Default 2:1 ratio
    }
        return true;
    }
    // --- additional entity types handled by this tool ---
    return false;
}


bool EllipseToolHandler::beginEntity(SketchCanvas& canvas, SketchEntity& entity)
{
    (void)canvas;
    entity.type = SketchEntityType::Ellipse;
    return true;
}

}  // namespace hobbycad
