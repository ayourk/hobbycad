// =====================================================================
//  src/hobbycad/gui/sketchcanvas.cpp — 2D Sketch canvas widget
// =====================================================================

#include "sketchcanvas.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>
#include <QtMath>

namespace hobbycad {

SketchCanvas::SketchCanvas(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("SketchCanvas"));
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);

    // Light gray background
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(240, 240, 240));
    setPalette(pal);

    // Initial zoom: 5 pixels per unit (so 10mm = 50 pixels)
    m_zoom = 5.0;
}

void SketchCanvas::setActiveTool(SketchTool tool)
{
    if (m_activeTool != tool) {
        // Cancel any in-progress drawing
        if (m_isDrawing) {
            cancelEntity();
        }
        m_activeTool = tool;

        // Update cursor
        switch (tool) {
        case SketchTool::Select:
            setCursor(Qt::ArrowCursor);
            break;
        case SketchTool::Line:
        case SketchTool::Rectangle:
        case SketchTool::Circle:
        case SketchTool::Arc:
        case SketchTool::Spline:
        case SketchTool::Point:
        case SketchTool::Text:
            setCursor(Qt::CrossCursor);
            break;
        case SketchTool::Dimension:
        case SketchTool::Constraint:
            setCursor(Qt::PointingHandCursor);
            break;
        }
        update();
    }
}

void SketchCanvas::setSketchPlane(SketchPlane plane)
{
    m_plane = plane;
    update();
}

void SketchCanvas::setGridVisible(bool visible)
{
    m_showGrid = visible;
    update();
}

void SketchCanvas::setGridSpacing(double spacing)
{
    m_gridSpacing = qMax(0.1, spacing);
    update();
}

void SketchCanvas::setSnapToGrid(bool snap)
{
    m_snapToGrid = snap;
}

SketchEntity* SketchCanvas::selectedEntity()
{
    for (auto& e : m_entities) {
        if (e.id == m_selectedId) return &e;
    }
    return nullptr;
}

const SketchEntity* SketchCanvas::selectedEntity() const
{
    for (const auto& e : m_entities) {
        if (e.id == m_selectedId) return &e;
    }
    return nullptr;
}

void SketchCanvas::clear()
{
    m_entities.clear();
    m_selectedId = -1;
    m_nextId = 1;
    cancelEntity();
    emit selectionChanged(-1);
    update();
}

void SketchCanvas::resetView()
{
    m_viewCenter = {0, 0};
    m_zoom = 5.0;
    update();
}

void SketchCanvas::zoomToFit()
{
    if (m_entities.isEmpty()) {
        resetView();
        return;
    }

    // Calculate bounding box
    double minX = std::numeric_limits<double>::max();
    double maxX = std::numeric_limits<double>::lowest();
    double minY = std::numeric_limits<double>::max();
    double maxY = std::numeric_limits<double>::lowest();

    for (const auto& e : m_entities) {
        for (const auto& p : e.points) {
            minX = qMin(minX, p.x());
            maxX = qMax(maxX, p.x());
            minY = qMin(minY, p.y());
            maxY = qMax(maxY, p.y());
        }
        // Account for circles
        if (e.type == SketchEntityType::Circle) {
            if (!e.points.isEmpty()) {
                minX = qMin(minX, e.points[0].x() - e.radius);
                maxX = qMax(maxX, e.points[0].x() + e.radius);
                minY = qMin(minY, e.points[0].y() - e.radius);
                maxY = qMax(maxY, e.points[0].y() + e.radius);
            }
        }
    }

    if (minX > maxX) {
        resetView();
        return;
    }

    // Add margin
    double margin = 20.0;
    double width = maxX - minX + margin * 2 / m_zoom;
    double height = maxY - minY + margin * 2 / m_zoom;

    m_viewCenter = {(minX + maxX) / 2, (minY + maxY) / 2};

    // Calculate zoom to fit
    double zoomX = (this->width() - margin * 2) / width;
    double zoomY = (this->height() - margin * 2) / height;
    m_zoom = qMin(zoomX, zoomY);
    m_zoom = qBound(0.1, m_zoom, 100.0);

    update();
}

QPointF SketchCanvas::screenToWorld(const QPoint& screen) const
{
    double x = (screen.x() - width() / 2.0) / m_zoom + m_viewCenter.x();
    double y = -(screen.y() - height() / 2.0) / m_zoom + m_viewCenter.y();
    return {x, y};
}

QPoint SketchCanvas::worldToScreen(const QPointF& world) const
{
    int x = static_cast<int>((world.x() - m_viewCenter.x()) * m_zoom + width() / 2.0);
    int y = static_cast<int>(-(world.y() - m_viewCenter.y()) * m_zoom + height() / 2.0);
    return {x, y};
}

QPointF SketchCanvas::snapPoint(const QPointF& world) const
{
    if (!m_snapToGrid) return world;

    double snappedX = qRound(world.x() / m_gridSpacing) * m_gridSpacing;
    double snappedY = qRound(world.y() / m_gridSpacing) * m_gridSpacing;
    return {snappedX, snappedY};
}

void SketchCanvas::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Draw grid
    if (m_showGrid) {
        drawGrid(painter);
    }

    // Draw axes
    drawAxes(painter);

    // Draw entities
    for (const auto& e : m_entities) {
        drawEntity(painter, e);
    }

    // Draw preview of entity being created
    if (m_isDrawing) {
        drawPreview(painter);
    }

    // Draw selection handles
    if (auto* sel = selectedEntity()) {
        drawSelectionHandles(painter, *sel);
    }

    // Draw plane label
    painter.setPen(Qt::darkGray);
    QString planeLabel;
    switch (m_plane) {
    case SketchPlane::XY: planeLabel = QStringLiteral("XY Plane"); break;
    case SketchPlane::XZ: planeLabel = QStringLiteral("XZ Plane"); break;
    case SketchPlane::YZ: planeLabel = QStringLiteral("YZ Plane"); break;
    case SketchPlane::Custom: planeLabel = QStringLiteral("Custom Plane"); break;
    }
    painter.drawText(10, 20, planeLabel);

    // Draw coordinates at cursor
    painter.drawText(10, height() - 10,
                     QStringLiteral("(%1, %2)")
                         .arg(m_currentMouseWorld.x(), 0, 'f', 2)
                         .arg(m_currentMouseWorld.y(), 0, 'f', 2));
}

void SketchCanvas::drawGrid(QPainter& painter)
{
    // Calculate visible area in world coordinates
    QPointF topLeft = screenToWorld(QPoint(0, 0));
    QPointF bottomRight = screenToWorld(QPoint(width(), height()));

    // Adjust for Y-flip
    double minY = qMin(topLeft.y(), bottomRight.y());
    double maxY = qMax(topLeft.y(), bottomRight.y());
    double minX = qMin(topLeft.x(), bottomRight.x());
    double maxX = qMax(topLeft.x(), bottomRight.x());

    // Determine grid spacing based on zoom level
    double spacing = m_gridSpacing;
    while (spacing * m_zoom < 10) spacing *= 5;  // Don't draw grid too dense
    while (spacing * m_zoom > 100) spacing /= 5; // Don't draw grid too sparse

    // Light grid lines
    painter.setPen(QPen(QColor(200, 200, 200), 1));

    // Vertical lines
    double startX = qFloor(minX / spacing) * spacing;
    for (double x = startX; x <= maxX; x += spacing) {
        QPoint p1 = worldToScreen({x, minY});
        QPoint p2 = worldToScreen({x, maxY});
        painter.drawLine(p1, p2);
    }

    // Horizontal lines
    double startY = qFloor(minY / spacing) * spacing;
    for (double y = startY; y <= maxY; y += spacing) {
        QPoint p1 = worldToScreen({minX, y});
        QPoint p2 = worldToScreen({maxX, y});
        painter.drawLine(p1, p2);
    }
}

void SketchCanvas::drawAxes(QPainter& painter)
{
    // Red X-axis
    painter.setPen(QPen(Qt::red, 2));
    QPoint origin = worldToScreen({0, 0});
    QPoint xEnd = worldToScreen({50, 0});
    painter.drawLine(origin, xEnd);

    // Green Y-axis
    painter.setPen(QPen(Qt::green, 2));
    QPoint yEnd = worldToScreen({0, 50});
    painter.drawLine(origin, yEnd);

    // Origin dot
    painter.setBrush(Qt::black);
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(origin, 4, 4);
}

void SketchCanvas::drawEntity(QPainter& painter, const SketchEntity& entity)
{
    QPen pen(entity.selected ? QColor(0, 120, 215) : Qt::black, 2);
    if (entity.constrained) {
        pen.setColor(entity.selected ? QColor(0, 180, 0) : QColor(0, 128, 0));
    }
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    switch (entity.type) {
    case SketchEntityType::Point:
        if (!entity.points.isEmpty()) {
            QPoint p = worldToScreen(entity.points[0]);
            painter.setBrush(pen.color());
            painter.drawEllipse(p, 4, 4);
        }
        break;

    case SketchEntityType::Line:
        if (entity.points.size() >= 2) {
            QPoint p1 = worldToScreen(entity.points[0]);
            QPoint p2 = worldToScreen(entity.points[1]);
            painter.drawLine(p1, p2);
        }
        break;

    case SketchEntityType::Rectangle:
        if (entity.points.size() >= 2) {
            QPoint p1 = worldToScreen(entity.points[0]);
            QPoint p2 = worldToScreen(entity.points[1]);
            painter.drawRect(QRect(p1, p2).normalized());
        }
        break;

    case SketchEntityType::Circle:
        if (!entity.points.isEmpty()) {
            QPoint center = worldToScreen(entity.points[0]);
            int r = static_cast<int>(entity.radius * m_zoom);
            painter.drawEllipse(center, r, r);
        }
        break;

    case SketchEntityType::Arc:
        if (!entity.points.isEmpty()) {
            QPoint center = worldToScreen(entity.points[0]);
            int r = static_cast<int>(entity.radius * m_zoom);
            QRect arcRect(center.x() - r, center.y() - r, r * 2, r * 2);
            // Qt uses 1/16th degree and goes counter-clockwise from 3 o'clock
            int startAngle = static_cast<int>(entity.startAngle * 16);
            int sweepAngle = static_cast<int>(entity.sweepAngle * 16);
            painter.drawArc(arcRect, startAngle, sweepAngle);
        }
        break;

    case SketchEntityType::Spline:
        if (entity.points.size() >= 2) {
            QPainterPath path;
            path.moveTo(worldToScreen(entity.points[0]));
            for (int i = 1; i < entity.points.size(); ++i) {
                path.lineTo(worldToScreen(entity.points[i]));
            }
            painter.drawPath(path);
        }
        break;

    case SketchEntityType::Text:
        if (!entity.points.isEmpty()) {
            QPoint p = worldToScreen(entity.points[0]);
            painter.drawText(p, entity.text);
        }
        break;

    case SketchEntityType::Dimension:
        // Draw dimension line and text
        if (entity.points.size() >= 2) {
            QPoint p1 = worldToScreen(entity.points[0]);
            QPoint p2 = worldToScreen(entity.points[1]);
            painter.setPen(QPen(Qt::blue, 1));
            painter.drawLine(p1, p2);

            // Draw value at midpoint
            QPoint mid((p1.x() + p2.x()) / 2, (p1.y() + p2.y()) / 2 - 10);
            double dist = QLineF(entity.points[0], entity.points[1]).length();
            painter.drawText(mid, QString::number(dist, 'f', 2));
        }
        break;
    }
}

void SketchCanvas::drawPreview(QPainter& painter)
{
    QPen pen(QColor(0, 120, 215), 2, Qt::DashLine);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    switch (m_activeTool) {
    case SketchTool::Line:
        if (!m_previewPoints.isEmpty()) {
            QPoint p1 = worldToScreen(m_previewPoints[0]);
            QPoint p2 = worldToScreen(m_currentMouseWorld);
            painter.drawLine(p1, p2);
        }
        break;

    case SketchTool::Rectangle:
        if (!m_previewPoints.isEmpty()) {
            QPoint p1 = worldToScreen(m_previewPoints[0]);
            QPoint p2 = worldToScreen(m_currentMouseWorld);
            painter.drawRect(QRect(p1, p2).normalized());
        }
        break;

    case SketchTool::Circle:
        if (!m_previewPoints.isEmpty()) {
            QPoint center = worldToScreen(m_previewPoints[0]);
            double r = QLineF(m_previewPoints[0], m_currentMouseWorld).length();
            int rPx = static_cast<int>(r * m_zoom);
            painter.drawEllipse(center, rPx, rPx);
        }
        break;

    case SketchTool::Point:
        {
            QPoint p = worldToScreen(snapPoint(m_currentMouseWorld));
            painter.setBrush(QColor(0, 120, 215));
            painter.drawEllipse(p, 4, 4);
        }
        break;

    default:
        break;
    }
}

void SketchCanvas::drawSelectionHandles(QPainter& painter, const SketchEntity& entity)
{
    painter.setPen(QPen(QColor(0, 120, 215), 1));
    painter.setBrush(Qt::white);

    for (const auto& pt : entity.points) {
        QPoint p = worldToScreen(pt);
        painter.drawRect(p.x() - 4, p.y() - 4, 8, 8);
    }
}

void SketchCanvas::mousePressEvent(QMouseEvent* event)
{
    QPointF worldPos = screenToWorld(event->pos());
    m_lastMousePos = event->pos();

    if (event->button() == Qt::MiddleButton) {
        m_isPanning = true;
        setCursor(Qt::ClosedHandCursor);
        return;
    }

    if (event->button() == Qt::LeftButton) {
        if (m_activeTool == SketchTool::Select) {
            // Hit test
            int hitId = hitTest(worldPos);
            if (hitId != m_selectedId) {
                // Deselect old
                for (auto& e : m_entities) {
                    e.selected = (e.id == hitId);
                }
                m_selectedId = hitId;
                emit selectionChanged(m_selectedId);
                update();
            }
        } else {
            // Start drawing
            startEntity(snapPoint(worldPos));
        }
    }
}

void SketchCanvas::mouseMoveEvent(QMouseEvent* event)
{
    QPointF worldPos = screenToWorld(event->pos());
    m_currentMouseWorld = snapPoint(worldPos);
    emit mousePositionChanged(m_currentMouseWorld);

    if (m_isPanning) {
        QPoint delta = event->pos() - m_lastMousePos;
        m_viewCenter.rx() -= delta.x() / m_zoom;
        m_viewCenter.ry() += delta.y() / m_zoom;
        m_lastMousePos = event->pos();
        update();
        return;
    }

    if (m_isDrawing) {
        updateEntity(m_currentMouseWorld);
    }

    update();
}

void SketchCanvas::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::MiddleButton) {
        m_isPanning = false;
        setCursor(m_activeTool == SketchTool::Select ? Qt::ArrowCursor : Qt::CrossCursor);
        return;
    }

    if (event->button() == Qt::LeftButton && m_isDrawing) {
        finishEntity();
    }
}

void SketchCanvas::wheelEvent(QWheelEvent* event)
{
    // Zoom centered on mouse position
    QPointF worldPosBefore = screenToWorld(event->position().toPoint());

    double factor = event->angleDelta().y() > 0 ? 1.1 : 0.9;
    m_zoom *= factor;
    m_zoom = qBound(0.1, m_zoom, 100.0);

    // Adjust view center to keep point under cursor
    QPointF worldPosAfter = screenToWorld(event->position().toPoint());
    m_viewCenter += worldPosBefore - worldPosAfter;

    update();
}

void SketchCanvas::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
    case Qt::Key_Escape:
        if (m_isDrawing) {
            cancelEntity();
        } else {
            // Deselect
            for (auto& e : m_entities) e.selected = false;
            m_selectedId = -1;
            emit selectionChanged(-1);
        }
        update();
        break;

    case Qt::Key_Delete:
    case Qt::Key_Backspace:
        if (m_selectedId >= 0) {
            m_entities.erase(
                std::remove_if(m_entities.begin(), m_entities.end(),
                               [this](const SketchEntity& e) { return e.id == m_selectedId; }),
                m_entities.end());
            m_selectedId = -1;
            emit selectionChanged(-1);
            update();
        }
        break;

    case Qt::Key_S:
        setActiveTool(SketchTool::Select);
        break;
    case Qt::Key_L:
        setActiveTool(SketchTool::Line);
        break;
    case Qt::Key_R:
        setActiveTool(SketchTool::Rectangle);
        break;
    case Qt::Key_C:
        setActiveTool(SketchTool::Circle);
        break;
    case Qt::Key_A:
        setActiveTool(SketchTool::Arc);
        break;
    case Qt::Key_P:
        setActiveTool(SketchTool::Point);
        break;
    case Qt::Key_D:
        setActiveTool(SketchTool::Dimension);
        break;
    case Qt::Key_G:
        setGridVisible(!m_showGrid);
        break;

    default:
        QWidget::keyPressEvent(event);
    }
}

void SketchCanvas::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    update();
}

int SketchCanvas::hitTest(const QPointF& worldPos) const
{
    // Test in reverse order (top-most first)
    for (int i = m_entities.size() - 1; i >= 0; --i) {
        if (hitTestEntity(m_entities[i], worldPos)) {
            return m_entities[i].id;
        }
    }
    return -1;
}

bool SketchCanvas::hitTestEntity(const SketchEntity& entity, const QPointF& worldPos) const
{
    const double tolerance = 5.0 / m_zoom;  // 5 pixels in world units

    switch (entity.type) {
    case SketchEntityType::Point:
        if (!entity.points.isEmpty()) {
            return QLineF(entity.points[0], worldPos).length() < tolerance;
        }
        break;

    case SketchEntityType::Line:
        if (entity.points.size() >= 2) {
            QLineF line(entity.points[0], entity.points[1]);
            // Distance from point to line segment
            QPointF d = entity.points[1] - entity.points[0];
            double len = line.length();
            if (len < 0.001) return false;

            double t = QPointF::dotProduct(worldPos - entity.points[0], d) / (len * len);
            t = qBound(0.0, t, 1.0);
            QPointF closest = entity.points[0] + t * d;
            return QLineF(closest, worldPos).length() < tolerance;
        }
        break;

    case SketchEntityType::Rectangle:
        if (entity.points.size() >= 2) {
            QRectF rect(entity.points[0], entity.points[1]);
            rect = rect.normalized();
            // Check if near any edge
            QLineF edges[] = {
                {rect.topLeft(), rect.topRight()},
                {rect.topRight(), rect.bottomRight()},
                {rect.bottomRight(), rect.bottomLeft()},
                {rect.bottomLeft(), rect.topLeft()}
            };
            for (const auto& edge : edges) {
                QPointF d = edge.p2() - edge.p1();
                double len = edge.length();
                if (len < 0.001) continue;
                double t = QPointF::dotProduct(worldPos - edge.p1(), d) / (len * len);
                t = qBound(0.0, t, 1.0);
                QPointF closest = edge.p1() + t * d;
                if (QLineF(closest, worldPos).length() < tolerance) return true;
            }
        }
        break;

    case SketchEntityType::Circle:
        if (!entity.points.isEmpty()) {
            double dist = QLineF(entity.points[0], worldPos).length();
            return qAbs(dist - entity.radius) < tolerance;
        }
        break;

    default:
        break;
    }

    return false;
}

void SketchCanvas::startEntity(const QPointF& pos)
{
    m_isDrawing = true;
    m_previewPoints.clear();
    m_previewPoints.append(pos);

    m_pendingEntity = SketchEntity();
    m_pendingEntity.id = nextEntityId();
    m_pendingEntity.points.append(pos);

    switch (m_activeTool) {
    case SketchTool::Point:
        m_pendingEntity.type = SketchEntityType::Point;
        finishEntity();  // Points are instant
        break;
    case SketchTool::Line:
        m_pendingEntity.type = SketchEntityType::Line;
        break;
    case SketchTool::Rectangle:
        m_pendingEntity.type = SketchEntityType::Rectangle;
        break;
    case SketchTool::Circle:
        m_pendingEntity.type = SketchEntityType::Circle;
        break;
    case SketchTool::Arc:
        m_pendingEntity.type = SketchEntityType::Arc;
        break;
    default:
        m_isDrawing = false;
        break;
    }
}

void SketchCanvas::updateEntity(const QPointF& pos)
{
    if (!m_isDrawing) return;

    // Update pending entity based on tool
    switch (m_activeTool) {
    case SketchTool::Line:
    case SketchTool::Rectangle:
        if (m_pendingEntity.points.size() > 1) {
            m_pendingEntity.points[1] = pos;
        } else {
            m_pendingEntity.points.append(pos);
        }
        break;

    case SketchTool::Circle:
    case SketchTool::Arc:
        if (!m_pendingEntity.points.isEmpty()) {
            m_pendingEntity.radius = QLineF(m_pendingEntity.points[0], pos).length();
        }
        break;

    default:
        break;
    }
}

void SketchCanvas::finishEntity()
{
    if (!m_isDrawing) return;

    // Validate entity
    bool valid = false;
    switch (m_pendingEntity.type) {
    case SketchEntityType::Point:
        valid = !m_pendingEntity.points.isEmpty();
        break;
    case SketchEntityType::Line:
    case SketchEntityType::Rectangle:
        valid = m_pendingEntity.points.size() >= 2 &&
                QLineF(m_pendingEntity.points[0], m_pendingEntity.points[1]).length() > 0.1;
        break;
    case SketchEntityType::Circle:
    case SketchEntityType::Arc:
        valid = m_pendingEntity.radius > 0.1;
        break;
    default:
        break;
    }

    if (valid) {
        m_entities.append(m_pendingEntity);
        emit entityCreated(m_pendingEntity.id);
    }

    m_isDrawing = false;
    m_previewPoints.clear();
    update();
}

void SketchCanvas::cancelEntity()
{
    m_isDrawing = false;
    m_previewPoints.clear();
    update();
}

int SketchCanvas::nextEntityId()
{
    return m_nextId++;
}

}  // namespace hobbycad
