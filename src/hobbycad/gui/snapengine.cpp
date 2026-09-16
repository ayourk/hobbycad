// =====================================================================
//  src/hobbycad/gui/snapengine.cpp
// =====================================================================
//
//  The GUI snapping & inference service, extracted from SketchCanvas.
//  See snapengine.h. The snap/inference ALGORITHMS live in the library
//  (hobbycad/sketch/snap.h, hobbycad/sketch/inference.h); this is the GUI
//  wiring around them: queries, transient visual state, and overlay
//  drawing. Model mutation (committing a snap/inference to a constraint)
//  stays on the canvas.
//
//  Part of HobbyCAD.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include "snapengine.h"
#include "screenmath.h"
#include "sketchcanvas.h"
#include "constraintglyphs.h"
#include "sketchutils.h"

#include <hobbycad/sketch/entity.h>
#include <hobbycad/geometry/utils.h>

#include <QPainter>
#include <QPen>
#include <cmath>

namespace hobbycad {

namespace {
// Angle tolerance for straight-segment inference (Fusion-style axis / parallel
// / perpendicular alignment). Degrees.
constexpr double kInferenceAngleTolDeg = 3.0;
}  // namespace

// Geometric inference for a segment p0->current while drawing: nudge the moving
// end onto a horizontal or vertical axis, or parallel/perpendicular to an
// existing line, and remember that alignment so the tool can turn it into a
// real constraint at commit time. Returns the adjusted point.
QPointF SnapEngine::computeInferences(const QPointF& p0, const QPointF& current) const
{
    m_activeInferences.clear();
    QPointF adjusted = current;
    const std::vector<sketch::Entity> libEnts = toLibraryEntities(m_canvas.m_entities);
    const sketch::InferenceResult ir = sketch::inferSegment(
        libEnts,
        hobbycad::Point2D(p0.x(), p0.y()),
        hobbycad::Point2D(current.x(), current.y()),
        kInferenceAngleTolDeg, /*excludeId=*/-1);
    if (!ir.empty()) {
        adjusted = QPointF(ir.adjusted.x, ir.adjusted.y);
        m_activeInferences = ir.inferences;
    }
    return adjusted;
}

QPointF SnapEngine::snapPoint(const QPointF& world) const
{
    m_activeSnap.reset();

    if (m_snapToEntities) {
        double tolerance = m_entitySnapTolerance / m_canvas.m_zoom;  // Convert pixels to world units
        int excludeId = m_canvas.m_isDraggingHandle ? m_canvas.m_selectedId : -1;

        // Delegate to the library for all snap evaluation
        std::vector<sketch::Entity> libEntities = toLibraryEntities(m_canvas.m_entities);
        sketch::SnapResult result = sketch::findBestSnap(
            libEntities, world, tolerance, excludeId);

        if (result.found) {
            m_activeSnap = result.snap;
            return result.snap.position;
        }
    }

    // If no entity snap, try grid snap
    if (m_canvas.m_snapToGrid) {
        return hobbycad::geometry::snapToGrid(world, m_canvas.m_gridSpacing);
    }

    return world;
}

QPointF SnapEngine::snapToAngle(const QPointF& origin, const QPointF& target) const
{
    // Snap to nearest 45-degree increment (0, 45, 90, 135, 180, 225, 270, 315)

    double distance = QLineF(origin, target).length();
    if (distance < 0.001) {
        m_angleSnapActive = false;
        return target;
    }

    double snappedAngle;
    QPointF result = geometry::snapToAngleIncrementWithAngle(origin, target, 45.0, snappedAngle);

    m_angleSnapActive = true;
    m_snappedAngle = snappedAngle;

    return result;
}

void SnapEngine::drawSnapIndicator(QPainter& painter, const sketch::SnapPoint& snap) const
{
    QPoint screenPos = m_canvas.worldToScreen(snap.position);
    painter.save();

    // Choose color and shape based on snap type
    QColor snapColor = m_canvas.m_theme.snap;  // snap indicators
    int size = 6;

    painter.setPen(QPen(snapColor, 2));
    painter.setBrush(Qt::NoBrush);

    switch (snap.type) {
    case sketch::SnapType::Endpoint:
        // Square for endpoints
        painter.drawRect(screenPos.x() - size, screenPos.y() - size, size * 2, size * 2);
        break;

    case sketch::SnapType::Point:
        // Filled circle for standalone sketch points
        painter.setBrush(snapColor);
        painter.drawEllipse(screenPos, size, size);
        break;

    case sketch::SnapType::Midpoint:
        // Triangle for midpoints
        {
            QPolygon triangle;
            triangle << QPoint(screenPos.x(), screenPos.y() - size)
                     << QPoint(screenPos.x() - size, screenPos.y() + size)
                     << QPoint(screenPos.x() + size, screenPos.y() + size);
            painter.drawPolygon(triangle);
        }
        break;

    case sketch::SnapType::Center:
        // Circle with cross for centers
        painter.drawEllipse(screenPos, size, size);
        paintCenterCross(painter, screenPos, size);
        break;

    case sketch::SnapType::Quadrant:
        // Diamond for quadrant points
        {
            QPolygon diamond;
            diamond << QPoint(screenPos.x(), screenPos.y() - size)
                    << QPoint(screenPos.x() + size, screenPos.y())
                    << QPoint(screenPos.x(), screenPos.y() + size)
                    << QPoint(screenPos.x() - size, screenPos.y());
            painter.drawPolygon(diamond);
        }
        break;

    case sketch::SnapType::ArcEndCenter:
        // Circle for arc end centers (slot endpoints)
        painter.drawEllipse(screenPos, size, size);
        break;

    case sketch::SnapType::Intersection:
        // X for intersections
        painter.drawLine(screenPos.x() - size, screenPos.y() - size,
                         screenPos.x() + size, screenPos.y() + size);
        painter.drawLine(screenPos.x() - size, screenPos.y() + size,
                         screenPos.x() + size, screenPos.y() - size);
        break;

    case sketch::SnapType::Nearest:
        // Perpendicular symbol (right angle) for nearest point
        {
            int halfSize = size / 2;
            // Draw a small right angle symbol
            painter.drawLine(screenPos.x() - halfSize, screenPos.y(),
                             screenPos.x(), screenPos.y());
            painter.drawLine(screenPos.x(), screenPos.y(),
                             screenPos.x(), screenPos.y() - halfSize);
            // Draw a small perpendicular line
            painter.drawLine(screenPos.x() - size, screenPos.y() + size,
                             screenPos.x() + size, screenPos.y() + size);
        }
        break;

    case sketch::SnapType::Origin:
        // Crosshair with circle for origin
        painter.drawEllipse(screenPos, size + 2, size + 2);
        painter.drawLine(screenPos.x() - size - 4, screenPos.y(), screenPos.x() + size + 4, screenPos.y());
        painter.drawLine(screenPos.x(), screenPos.y() - size - 4, screenPos.x(), screenPos.y() + size + 4);
        break;

    case sketch::SnapType::AxisX:
        // Horizontal line indicator for X axis
        painter.drawLine(screenPos.x() - size, screenPos.y(), screenPos.x() + size, screenPos.y());
        painter.drawLine(screenPos.x() - size, screenPos.y() - 3, screenPos.x() - size, screenPos.y() + 3);
        painter.drawLine(screenPos.x() + size, screenPos.y() - 3, screenPos.x() + size, screenPos.y() + 3);
        break;

    case sketch::SnapType::AxisY:
        // Vertical line indicator for Y axis
        painter.drawLine(screenPos.x(), screenPos.y() - size, screenPos.x(), screenPos.y() + size);
        painter.drawLine(screenPos.x() - 3, screenPos.y() - size, screenPos.x() + 3, screenPos.y() - size);
        painter.drawLine(screenPos.x() - 3, screenPos.y() + size, screenPos.x() + 3, screenPos.y() + size);
        break;
    }

    painter.restore();
}

void SnapEngine::drawSnapGuides(QPainter& painter) const
{
    // Get current handle position
    const SketchEntity* sel = m_canvas.selectedEntity();
    if (!sel || m_canvas.m_dragHandleIndex < 0 || m_canvas.m_dragHandleIndex >= sel->points.size()) {
        return;
    }

    QPointF handlePos = sel->points[m_canvas.m_dragHandleIndex];
    QPoint handleScreen = m_canvas.worldToScreen(handlePos);

    // Calculate visible area for drawing constraint lines
    QPointF topLeft = m_canvas.screenToWorld(QPoint(0, 0));
    QPointF bottomRight = m_canvas.screenToWorld(QPoint(m_canvas.width(), m_canvas.height()));

    // Colors for constraint guides
    QColor guideColor = m_canvas.m_theme.guide;  // alignment guides
    QColor xAxisColor = m_canvas.m_theme.guideX;   // X constraint
    QColor yAxisColor = m_canvas.m_theme.guideY;   // Y constraint

    // Dashed line style for guides
    QPen guidePen(guideColor, 1, Qt::DashLine);

    // Draw guide from original position to current snapped position
    QPoint origScreen = m_canvas.worldToScreen(m_canvas.m_dragHandleOriginal);

    if (m_canvas.m_snapAxis == SketchCanvas::SnapAxis::None) {
        // Full snap - draw crosshair at snapped position
        guidePen.setColor(guideColor);
        painter.setPen(guidePen);

        // Horizontal line through handle
        painter.drawLine(0, handleScreen.y(), m_canvas.width(), handleScreen.y());
        // Vertical line through handle
        painter.drawLine(handleScreen.x(), 0, handleScreen.x(), m_canvas.height());

        // Draw small indicator showing snap is active
        painter.setPen(QPen(guideColor, 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(handleScreen, 12, 12);

    } else if (m_canvas.m_snapAxis == SketchCanvas::SnapAxis::X) {
        // X-axis locked - draw horizontal constraint line
        guidePen.setColor(xAxisColor);
        guidePen.setStyle(Qt::SolidLine);
        guidePen.setWidth(2);
        painter.setPen(guidePen);

        // Draw horizontal line at the locked Y position
        int lockedY = m_canvas.worldToScreen(QPointF(0, m_canvas.m_dragHandleOriginal.y())).y();
        painter.drawLine(0, lockedY, m_canvas.width(), lockedY);

        // Draw vertical dashed line showing X movement
        guidePen.setStyle(Qt::DashLine);
        guidePen.setWidth(1);
        painter.setPen(guidePen);
        painter.drawLine(handleScreen.x(), 0, handleScreen.x(), m_canvas.height());

        // Draw "X" label near cursor
        painter.setPen(QPen(xAxisColor, 1));
        QFont font = painter.font();
        font.setBold(true);
        painter.setFont(font);
        painter.drawText(handleScreen.x() + 15, handleScreen.y() - 10, QStringLiteral("X"));

        // Draw arrow indicating constrained axis
        painter.setPen(QPen(xAxisColor, 2));
        painter.drawLine(handleScreen.x() - 20, lockedY, handleScreen.x() + 20, lockedY);
        // Arrow heads
        painter.drawLine(handleScreen.x() - 20, lockedY, handleScreen.x() - 15, lockedY - 4);
        painter.drawLine(handleScreen.x() - 20, lockedY, handleScreen.x() - 15, lockedY + 4);
        painter.drawLine(handleScreen.x() + 20, lockedY, handleScreen.x() + 15, lockedY - 4);
        painter.drawLine(handleScreen.x() + 20, lockedY, handleScreen.x() + 15, lockedY + 4);

    } else if (m_canvas.m_snapAxis == SketchCanvas::SnapAxis::Y) {
        // Y-axis locked - draw vertical constraint line
        guidePen.setColor(yAxisColor);
        guidePen.setStyle(Qt::SolidLine);
        guidePen.setWidth(2);
        painter.setPen(guidePen);

        // Draw vertical line at the locked X position
        int lockedX = m_canvas.worldToScreen(QPointF(m_canvas.m_dragHandleOriginal.x(), 0)).x();
        painter.drawLine(lockedX, 0, lockedX, m_canvas.height());

        // Draw horizontal dashed line showing Y movement
        guidePen.setStyle(Qt::DashLine);
        guidePen.setWidth(1);
        painter.setPen(guidePen);
        painter.drawLine(0, handleScreen.y(), m_canvas.width(), handleScreen.y());

        // Draw "Y" label near cursor
        painter.setPen(QPen(yAxisColor, 1));
        QFont font = painter.font();
        font.setBold(true);
        painter.setFont(font);
        painter.drawText(handleScreen.x() + 15, handleScreen.y() - 10, QStringLiteral("Y"));

        // Draw arrow indicating constrained axis
        painter.setPen(QPen(yAxisColor, 2));
        painter.drawLine(lockedX, handleScreen.y() - 20, lockedX, handleScreen.y() + 20);
        // Arrow heads
        painter.drawLine(lockedX, handleScreen.y() - 20, lockedX - 4, handleScreen.y() - 15);
        painter.drawLine(lockedX, handleScreen.y() - 20, lockedX + 4, handleScreen.y() - 15);
        painter.drawLine(lockedX, handleScreen.y() + 20, lockedX - 4, handleScreen.y() + 15);
        painter.drawLine(lockedX, handleScreen.y() + 20, lockedX + 4, handleScreen.y() + 15);
    }

    // Draw snap point indicator (small filled circle at snapped position)
    painter.setPen(QPen(guideColor, 1));
    painter.setBrush(guideColor);
    painter.drawEllipse(handleScreen, 4, 4);
}

void SnapEngine::drawInferenceGuides(QPainter& painter) const
{
    painter.save();

    // Fusion draws inference hints in a distinct green; keep to that so they
    // read as "what the tool is about to do", separate from snap orange and
    // the blue preview line.
    const QColor hint = m_canvas.m_theme.inference;

    for (const auto& inf : m_activeInferences) {
        QPen guidePen(hint, 1.0, Qt::DashLine);
        guidePen.setCosmetic(true);
        painter.setPen(guidePen);
        const QPointF a = m_canvas.worldToScreenF(QPointF(inf.guideA.x, inf.guideA.y));
        const QPointF b = m_canvas.worldToScreenF(QPointF(inf.guideB.x, inf.guideB.y));
        painter.drawLine(a, b);

        // A small glyph beside the moving end says which alignment it is.
        const QPointF g = m_canvas.worldToScreenF(QPointF(inf.glyphAt.x, inf.glyphAt.y));
        const QRectF box(g.x() + 12.0, g.y() - 28.0, 16.0, 16.0);
        QPen glyphPen(hint, 1.5);
        glyphPen.setCosmetic(true);
        painter.setPen(glyphPen);
        painter.setBrush(Qt::NoBrush);
        drawConstraintGlyph(painter, inf.constraint, box, QColor());
    }

    painter.restore();
}

}  // namespace hobbycad
