// =====================================================================
//  src/hobbycad/gui/tools/pointtoolhandler.cpp — Point tool handler
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#include "pointtoolhandler.h"
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

QString PointToolHandler::hint(const SketchCanvas&) const
{
    // Point commits on release, so the prompt does not change with stage.
    return QCoreApplication::translate("hobbycad::SketchCanvas",
                                       "Point: click to place");
}


bool PointToolHandler::drawPreview(SketchCanvas& canvas, QPainter& painter)
{
    {
        QPoint p = canvas.toScreen(canvas.snapToGeometry(canvas.currentMouseWorld()));
        painter.setBrush(QColor(0, 120, 215));
        painter.drawEllipse(p, 4, 4);
    }
    return true;
}


bool PointToolHandler::normalize(SketchCanvas& canvas, SketchEntity& entity, bool& valid)
{
    (void)canvas;
    if (entity.type == SketchEntityType::Point) {
    valid = !entity.points.empty();
        return true;
    }
    // --- additional entity types handled by this tool ---
    return false;
}


bool PointToolHandler::beginEntity(SketchCanvas& canvas, SketchEntity& entity)
{
    (void)canvas;
    entity.type = SketchEntityType::Point;
    // Point commits on mouse RELEASE, not on a second click.
    return true;
}

bool PointToolHandler::mouseRelease(SketchCanvas& canvas, QMouseEvent*,
                                    const QPointF& world)
{
    if (!canvas.isDrawing()) {
        return false;
    }
    // A point has a single position, so wherever the button comes up is where
    // it lands.
    canvas.pendingEntityRef().points[0] = canvas.snapToGeometry(world);
    canvas.commitEntity();
    return true;
}

}  // namespace hobbycad
