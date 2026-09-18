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
#include "../sketchcanvas.h"

#include <hobbycad/units.h>

#include <QFontMetrics>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>

#include <cmath>

namespace hobbycad {

using LineMode = SketchCanvas::LineMode;

sketch::PlacementKind LineToolHandler::kind(const SketchCanvas& canvas) const
{
    return placementKind(SketchTool::Line, canvas.lineMode());
}

bool LineToolHandler::beginEntity(SketchCanvas& canvas, SketchEntity& entity)
{
    PlacementToolHandler::beginEntity(canvas, entity);
    if (canvas.lineMode() == LineMode::Construction) entity.isConstruction = true;
    return true;
}

bool LineToolHandler::normalize(SketchCanvas& canvas, SketchEntity& entity, bool& valid)
{
    if (!PlacementToolHandler::normalize(canvas, entity, valid)) return false;
    // The picked circle belongs to this line only.
    if (canvas.lineMode() == LineMode::Tangent) canvas.clearTangentTargets();
    return true;
}

bool LineToolHandler::drawPreview(SketchCanvas& canvas, QPainter& painter)
{
    PlacementToolHandler::drawPreview(canvas, painter);
    if (!canvas.angleSnapActive() || canvas.previewPointCount() < 1) return true;

    // Ctrl's angle snap: the snapped direction extended past both ends, and
    // the angle by the start.
    const auto& pending = canvas.pendingEntity().points;
    const QPoint p1 = canvas.toScreen(canvas.previewPoint(0));
    const QPoint p2 = canvas.toScreen(pending.size() >= 2 ? QPointF(pending[1])
                                                          : canvas.currentMouseWorld());
    painter.save();
    painter.setPen(QPen(QColor(255, 140, 0), 1, Qt::DashLine));
    const double extend = std::max(QLineF(p1, p2).length() * 0.3, 30.0);
    const double angle = degreesToRadians(canvas.snappedAngle());
    const QPointF dir(std::cos(angle), -std::sin(angle));   // screen y points down
    painter.drawLine((QPointF(p1) - dir * extend).toPoint(), p1);
    painter.drawLine(p2, (QPointF(p2) + dir * extend).toPoint());

    const QString text = QString::fromStdString(formatAngle(canvas.snappedAngle()));
    painter.setPen(QColor(255, 140, 0));
    QFont font = painter.font();
    font.setPointSize(9);
    painter.setFont(font);
    const QRect textRect = QFontMetrics(font).boundingRect(text);
    const QPoint at(p1.x() + 15, p1.y() - 15);
    painter.fillRect(QRectF(at.x() - 2, at.y() - textRect.height(), textRect.width() + 4,
                            textRect.height() + 2),
                     QColor(255, 255, 255, 200));
    painter.drawText(at, text);
    painter.restore();
    return true;
}

bool LineToolHandler::mousePress(SketchCanvas& canvas, QMouseEvent* event, const QPointF& world)
{
    if (canvas.lineMode() != LineMode::Tangent) {
        return false;   // the other line modes use the shared placement path
    }

    // Once the circle is chosen: the second click places the start (free,
    // where clicked, not projected onto the circle), the third lands the
    // end, already on the tangent, and commits.
    if (canvas.hasTangentTargets()) {
        if (!canvas.isDrawing()) {
            canvas.beginDragDetection(event->pos());
            canvas.beginPlacement(canvas.snapToGeometry(world));
        } else {
            canvas.trackEntity(canvas.currentMouseWorld());
            // A deliberate snap on this point too, so the generic snap
            // evaluation welds a Coincident if it landed on other geometry
            // (a snap onto the tangent circle itself is excluded downstream).
            canvas.recordEndpointSnap();
            canvas.commitEntity();
        }
        canvas.update();
        return true;
    }

    // Pick the circle or arc under the cursor, else the nearest one in the
    // sketch: while one exists the tangent line can be built, even from a
    // click beside it (Aaron).
    int hitId = canvas.pick(world);
    const SketchEntity* target = hitId >= 0 ? canvas.findEntity(hitId) : nullptr;
    if (!target || !sketch::tangentLineTarget(*target)) {
        hitId = canvas.nearestCircleOrArc(world);
        target = hitId >= 0 ? canvas.findEntity(hitId) : nullptr;
    }
    if (target && sketch::tangentLineTarget(*target)) {
        canvas.addTangentTarget(hitId);
        canvas.showStatus(tr("Tangent target selected; click the start point."));
    } else {
        // The status bar says so, not a modal dialog (Aaron).
        canvas.showStatus(tr("Tangent line needs a circle or arc in the sketch."));
    }
    return true;
}

bool LineToolHandler::previewPen(const SketchCanvas& canvas, QPen& pen) const
{
    if (canvas.lineMode() != LineMode::Construction) return false;
    pen.setColor(QColor(180, 100, 50));   // construction-geometry color
    return true;
}

bool LineToolHandler::applyCreationMode(SketchCanvas& canvas, int modeValue)
{
    // Line modes: 0=TwoPoint, 1=Horizontal, 2=Vertical, 3=Tangent, 4=Construction.
    switch (modeValue) {
    case 1:  canvas.setLineMode(LineMode::Horizontal); break;
    case 2:  canvas.setLineMode(LineMode::Vertical); break;
    case 3:  canvas.setLineMode(LineMode::Tangent); break;
    case 4:  canvas.setLineMode(LineMode::Construction); break;
    default: canvas.setLineMode(LineMode::TwoPoint); break;
    }
    return true;
}

}  // namespace hobbycad
