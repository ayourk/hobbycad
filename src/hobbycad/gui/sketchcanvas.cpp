// =====================================================================
//  src/hobbycad/gui/sketchcanvas.cpp — 2D Sketch canvas widget
// =====================================================================

#include "sketchcanvas.h"
#include "bindingsdialog.h"
#include "sketchsolver.h"
#include "sketchutils.h"

#include <hobbycad/sketch/profiles.h>
#include <hobbycad/sketch/patterns.h>
#include <hobbycad/sketch/operations.h>
#include <hobbycad/geometry/utils.h>
#include <hobbycad/geometry/intersections.h>

#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>
#include <QInputDialog>
#include <QMessageBox>
#include <QMenu>
#include <QQueue>
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

    // Load key bindings from settings
    loadKeyBindings();
}

void SketchCanvas::setActiveTool(SketchTool tool)
{
    if (m_activeTool != tool) {
        // Cancel any in-progress drawing
        if (m_isDrawing) {
            cancelEntity();
        }

        // Cancel any in-progress constraint creation
        if (m_isCreatingConstraint) {
            finishConstraintCreation();
        }

        m_activeTool = tool;

        // Initialize constraint creation state for Dimension tool
        if (tool == SketchTool::Dimension) {
            m_isCreatingConstraint = true;
            m_pendingConstraintType = ConstraintType::Distance;
            m_constraintTargetEntities.clear();
            m_constraintTargetPoints.clear();
        }

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
        case SketchTool::Polygon:
        case SketchTool::Slot:
        case SketchTool::Ellipse:
            setCursor(Qt::CrossCursor);
            break;
        case SketchTool::Dimension:
        case SketchTool::Constraint:
            setCursor(Qt::PointingHandCursor);
            break;
        case SketchTool::Trim:
        case SketchTool::Extend:
        case SketchTool::Split:
        case SketchTool::Offset:
        case SketchTool::Fillet:
        case SketchTool::Chamfer:
            setCursor(Qt::CrossCursor);
            break;
        case SketchTool::RectPattern:
        case SketchTool::CircPattern:
        case SketchTool::Project:
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

QVector<SketchEntity*> SketchCanvas::selectedEntities()
{
    QVector<SketchEntity*> result;
    for (auto& e : m_entities) {
        if (m_selectedIds.contains(e.id)) {
            result.append(&e);
        }
    }
    return result;
}

QVector<const SketchEntity*> SketchCanvas::selectedEntities() const
{
    QVector<const SketchEntity*> result;
    for (const auto& e : m_entities) {
        if (m_selectedIds.contains(e.id)) {
            result.append(&e);
        }
    }
    return result;
}

void SketchCanvas::clearSelection()
{
    for (auto& e : m_entities) {
        e.selected = false;
    }
    m_selectedId = -1;
    m_selectedIds.clear();

    // Also clear constraint selection
    for (auto& c : m_constraints) {
        c.selected = false;
    }
    m_selectedConstraintId = -1;

    emit selectionChanged(-1);
    update();
}

void SketchCanvas::selectEntity(int entityId, bool addToSelection)
{
    if (!addToSelection) {
        // Clear existing selection
        for (auto& e : m_entities) {
            e.selected = false;
        }
        m_selectedIds.clear();

        // Clear constraint selection
        for (auto& c : m_constraints) {
            c.selected = false;
        }
        m_selectedConstraintId = -1;
    }

    // Add/toggle entity selection
    SketchEntity* entity = entityById(entityId);
    if (entity) {
        if (addToSelection && entity->selected) {
            // Ctrl+click on already selected entity - deselect it
            entity->selected = false;
            m_selectedIds.remove(entityId);
            // Update primary selection
            if (m_selectedId == entityId) {
                m_selectedId = m_selectedIds.isEmpty() ? -1 : *m_selectedIds.begin();
            }
        } else {
            entity->selected = true;
            m_selectedIds.insert(entityId);
            m_selectedId = entityId;  // Primary selection is the last clicked
        }
    }

    emit selectionChanged(m_selectedId);
    update();
}

void SketchCanvas::selectEntitiesInRect(const QRectF& rect, bool crossing, bool addToSelection)
{
    if (!addToSelection) {
        // Clear existing selection
        for (auto& e : m_entities) {
            e.selected = false;
        }
        m_selectedIds.clear();
        m_selectedId = -1;

        // Clear constraint selection
        for (auto& c : m_constraints) {
            c.selected = false;
        }
        m_selectedConstraintId = -1;
    }

    // Check each entity
    for (auto& entity : m_entities) {
        bool shouldSelect = false;

        if (crossing) {
            // Crossing mode: select if entity intersects the rectangle
            shouldSelect = entityIntersectsRect(entity, rect);
        } else {
            // Window mode: select only if entity is fully enclosed
            shouldSelect = entityEnclosedByRect(entity, rect);
        }

        if (shouldSelect) {
            entity.selected = true;
            m_selectedIds.insert(entity.id);
            m_selectedId = entity.id;
        }
    }

    emit selectionChanged(m_selectedId);
    update();
}

void SketchCanvas::selectConnectedChain(int startEntityId)
{
    const SketchEntity* startEntity = entityById(startEntityId);
    if (!startEntity) return;

    // Clear selection and start fresh with the clicked entity
    clearSelection();

    // BFS to find all connected entities
    QSet<int> visited;
    QQueue<int> queue;
    queue.enqueue(startEntityId);

    while (!queue.isEmpty()) {
        int currentId = queue.dequeue();
        if (visited.contains(currentId)) continue;
        visited.insert(currentId);

        const SketchEntity* current = entityById(currentId);
        if (!current) continue;

        // Get endpoints of current entity
        QVector<QPointF> currentEndpoints = getEntityEndpointsVec(*current);
        if (currentEndpoints.isEmpty()) continue;

        // Find entities that share an endpoint
        for (const auto& other : m_entities) {
            if (other.id == currentId || visited.contains(other.id)) continue;

            QVector<QPointF> otherEndpoints = getEntityEndpointsVec(other);

            // Check if any endpoints coincide
            for (const QPointF& ep1 : currentEndpoints) {
                for (const QPointF& ep2 : otherEndpoints) {
                    if (QLineF(ep1, ep2).length() < 0.01) {
                        // Connected!
                        queue.enqueue(other.id);
                        break;
                    }
                }
            }
        }
    }

    // Select all visited entities
    for (int id : visited) {
        selectEntity(id, true);
    }
}

SketchEntity* SketchCanvas::entityById(int id)
{
    for (auto& e : m_entities) {
        if (e.id == id) return &e;
    }
    return nullptr;
}

const SketchEntity* SketchCanvas::entityById(int id) const
{
    for (const auto& e : m_entities) {
        if (e.id == id) return &e;
    }
    return nullptr;
}

SketchConstraint* SketchCanvas::constraintById(int id)
{
    for (auto& c : m_constraints) {
        if (c.id == id) return &c;
    }
    return nullptr;
}

const SketchConstraint* SketchCanvas::constraintById(int id) const
{
    for (const auto& c : m_constraints) {
        if (c.id == id) return &c;
    }
    return nullptr;
}

QString SketchCanvas::describeConstraint(int constraintId) const
{
    const SketchConstraint* c = constraintById(constraintId);
    if (!c) return QString();

    QString typeName;
    switch (c->type) {
    case ConstraintType::Distance:
        typeName = tr("Distance");
        break;
    case ConstraintType::Radius:
        typeName = tr("Radius");
        break;
    case ConstraintType::Diameter:
        typeName = tr("Diameter");
        break;
    case ConstraintType::Angle:
        typeName = tr("Angle");
        break;
    case ConstraintType::Horizontal:
        typeName = tr("Horizontal");
        break;
    case ConstraintType::Vertical:
        typeName = tr("Vertical");
        break;
    case ConstraintType::Parallel:
        typeName = tr("Parallel");
        break;
    case ConstraintType::Perpendicular:
        typeName = tr("Perpendicular");
        break;
    case ConstraintType::Coincident:
        typeName = tr("Coincident");
        break;
    case ConstraintType::Tangent:
        typeName = tr("Tangent");
        break;
    case ConstraintType::Equal:
        typeName = tr("Equal");
        break;
    case ConstraintType::Midpoint:
        typeName = tr("Midpoint");
        break;
    case ConstraintType::Symmetric:
        typeName = tr("Symmetric");
        break;
    }

    // For dimensional constraints, include the value
    QString description = typeName;
    switch (c->type) {
    case ConstraintType::Distance:
        description += QString(" = %1").arg(c->value, 0, 'f', 2);
        break;
    case ConstraintType::Radius:
        description += QString(" R%1").arg(c->value, 0, 'f', 2);
        break;
    case ConstraintType::Diameter:
        description += QString(" Ø%1").arg(c->value, 0, 'f', 2);
        break;
    case ConstraintType::Angle:
        description += QString(" = %1°").arg(c->value, 0, 'f', 1);
        break;
    default:
        break;
    }

    // Add entity information if available
    if (!c->entityIds.isEmpty()) {
        QStringList entityNames;
        for (int entityId : c->entityIds) {
            const SketchEntity* entity = entityById(entityId);
            if (entity) {
                QString entityType;
                switch (entity->type) {
                case SketchEntityType::Point:
                    entityType = tr("Point");
                    break;
                case SketchEntityType::Line:
                    entityType = tr("Line");
                    break;
                case SketchEntityType::Circle:
                    entityType = tr("Circle");
                    break;
                case SketchEntityType::Arc:
                    entityType = tr("Arc");
                    break;
                case SketchEntityType::Rectangle:
                    entityType = tr("Rectangle");
                    break;
                case SketchEntityType::Spline:
                    entityType = tr("Spline");
                    break;
                default:
                    entityType = tr("Entity");
                    break;
                }
                entityNames.append(entityType + QString(" %1").arg(entityId));
            }
        }
        if (!entityNames.isEmpty()) {
            description += QString(" (%1)").arg(entityNames.join(", "));
        }
    }

    return description;
}

void SketchCanvas::clear()
{
    m_entities.clear();
    m_constraints.clear();
    m_selectedId = -1;
    m_selectedConstraintId = -1;
    m_nextId = 1;
    m_nextConstraintId = 1;
    m_profilesCacheDirty = true;
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

void SketchCanvas::setViewRotation(double degrees)
{
    m_viewRotation = degrees;
    // Normalize to [-180, 180]
    while (m_viewRotation > 180.0) m_viewRotation -= 360.0;
    while (m_viewRotation < -180.0) m_viewRotation += 360.0;
    update();
}

void SketchCanvas::rotateViewCW()
{
    setViewRotation(m_viewRotation + 90.0);
}

void SketchCanvas::rotateViewCCW()
{
    setViewRotation(m_viewRotation - 90.0);
}

void SketchCanvas::setPlaneOrigin(double x, double y, double z)
{
    m_planeOrigin = QVector3D(x, y, z);
}

QPointF SketchCanvas::screenToWorld(const QPoint& screen) const
{
    // Translate to center of widget
    double sx = screen.x() - width() / 2.0;
    double sy = -(screen.y() - height() / 2.0);

    // Apply inverse rotation (rotate in opposite direction)
    double rad = qDegreesToRadians(-m_viewRotation);
    double cosR = qCos(rad);
    double sinR = qSin(rad);
    double rx = sx * cosR - sy * sinR;
    double ry = sx * sinR + sy * cosR;

    // Scale and translate to world
    double x = rx / m_zoom + m_viewCenter.x();
    double y = ry / m_zoom + m_viewCenter.y();
    return {x, y};
}

QPoint SketchCanvas::worldToScreen(const QPointF& world) const
{
    // Translate to view center and scale
    double wx = (world.x() - m_viewCenter.x()) * m_zoom;
    double wy = (world.y() - m_viewCenter.y()) * m_zoom;

    // Apply rotation
    double rad = qDegreesToRadians(m_viewRotation);
    double cosR = qCos(rad);
    double sinR = qSin(rad);
    double rx = wx * cosR - wy * sinR;
    double ry = wx * sinR + wy * cosR;

    // Translate to screen center (flip Y for screen coords)
    int x = static_cast<int>(rx + width() / 2.0);
    int y = static_cast<int>(-ry + height() / 2.0);
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

    // Draw background image first (behind everything)
    if (m_backgroundImage.enabled) {
        drawBackgroundImage(painter);
    }

    // Draw grid
    if (m_showGrid) {
        drawGrid(painter);
    }

    // Draw axes
    drawAxes(painter);

    // Draw profile highlights (behind entities)
    if (m_showProfiles) {
        drawProfiles(painter);
    }

    // Draw entities
    for (const auto& e : m_entities) {
        drawEntity(painter, e);
    }

    // Draw preview of entity being created
    if (m_isDrawing) {
        drawPreview(painter);
    }

    // Draw constraints (dimensions)
    drawConstraints(painter);

    // Draw selection handles
    if (auto* sel = selectedEntity()) {
        drawSelectionHandles(painter, *sel);
    }

    // Draw snap constraint guides during modifier+drag
    // Show when: Shift held for snap, or Ctrl held with axis constraint
    if (m_isDraggingHandle && (m_shiftWasPressed || (m_ctrlWasPressed && m_snapAxis != SnapAxis::None))) {
        drawSnapGuides(painter);
    }

    // Draw background manipulation handles when in edit mode
    if (m_backgroundEditMode && m_backgroundImage.enabled) {
        drawBackgroundHandles(painter);
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

    // Draw window selection rectangle
    if (m_isWindowSelecting) {
        QRectF selRect = QRectF(m_windowSelectStart, m_windowSelectEnd).normalized();
        QPointF screenTopLeft = worldToScreen(selRect.topLeft());
        QPointF screenBottomRight = worldToScreen(selRect.bottomRight());
        QRectF screenRect = QRectF(screenTopLeft, screenBottomRight).normalized();

        // Different colors for window vs crossing selection
        if (m_windowSelectCrossing) {
            // Crossing (right-to-left): green, dashed
            painter.setPen(QPen(QColor(0, 180, 0), 1, Qt::DashLine));
            painter.setBrush(QColor(0, 180, 0, 30));
        } else {
            // Window (left-to-right): blue, solid
            painter.setPen(QPen(QColor(0, 120, 215), 1, Qt::SolidLine));
            painter.setBrush(QColor(0, 120, 215, 30));
        }
        painter.drawRect(screenRect);
    }
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

    // Construction geometry: dashed line, orange/brown color
    if (entity.isConstruction) {
        pen.setColor(entity.selected ? QColor(255, 140, 0) : QColor(180, 100, 50));
        pen.setStyle(Qt::DashLine);
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

    case SketchEntityType::Polygon:
        if (!entity.points.isEmpty() && entity.sides >= 3) {
            QPoint center = worldToScreen(entity.points[0]);
            double r = entity.radius * m_zoom;
            QPainterPath path;
            // Draw regular polygon with specified number of sides
            for (int i = 0; i <= entity.sides; ++i) {
                double angle = (2.0 * M_PI * i) / entity.sides - M_PI / 2;  // Start at top
                double x = center.x() + r * qCos(angle);
                double y = center.y() + r * qSin(angle);
                if (i == 0) {
                    path.moveTo(x, y);
                } else {
                    path.lineTo(x, y);
                }
            }
            painter.drawPath(path);
        }
        break;

    case SketchEntityType::Slot:
        if (entity.points.size() >= 2) {
            QPoint p1 = worldToScreen(entity.points[0]);
            QPoint p2 = worldToScreen(entity.points[1]);
            double halfWidth = entity.radius * m_zoom;

            // Calculate perpendicular offset
            QLineF line(p1, p2);
            QLineF perpendicular = line.normalVector();
            perpendicular.setLength(halfWidth);

            // Draw slot as rounded rectangle
            QPointF offset = perpendicular.p2() - perpendicular.p1();
            QPointF corner1 = p1 + offset;
            QPointF corner2 = p1 - offset;
            QPointF corner3 = p2 - offset;
            QPointF corner4 = p2 + offset;

            QPainterPath path;
            path.moveTo(corner1);
            path.lineTo(corner4);
            path.arcTo(QRectF(corner4.x() - halfWidth, corner4.y() - halfWidth,
                             halfWidth * 2, halfWidth * 2), line.angle(), 180);
            path.lineTo(corner3);
            path.lineTo(corner2);
            path.arcTo(QRectF(corner2.x() - halfWidth, corner2.y() - halfWidth,
                             halfWidth * 2, halfWidth * 2), line.angle() + 180, 180);
            path.closeSubpath();
            painter.drawPath(path);
        }
        break;

    case SketchEntityType::Ellipse:
        if (!entity.points.isEmpty()) {
            QPoint center = worldToScreen(entity.points[0]);
            int majorR = static_cast<int>(entity.majorRadius * m_zoom);
            int minorR = static_cast<int>(entity.minorRadius * m_zoom);
            // For now, draw axis-aligned ellipse
            painter.drawEllipse(center, majorR, minorR);
        }
        break;

    case SketchEntityType::Spline:
        if (entity.points.size() >= 2) {
            QPainterPath path;

            // Convert to screen coordinates first
            QVector<QPointF> screenPoints;
            for (const QPointF& wp : entity.points) {
                screenPoints.append(worldToScreen(wp));
            }

            path.moveTo(screenPoints[0]);

            if (screenPoints.size() == 2) {
                // Just two points - draw a line
                path.lineTo(screenPoints[1]);
            } else {
                // Catmull-Rom spline through all points
                // For each segment between consecutive points
                for (int i = 0; i < screenPoints.size() - 1; ++i) {
                    QPointF p0, p1, p2, p3;

                    // Get control points (with endpoint duplication for first/last segments)
                    p1 = screenPoints[i];
                    p2 = screenPoints[i + 1];

                    if (i == 0) {
                        p0 = p1;  // Duplicate first point
                    } else {
                        p0 = screenPoints[i - 1];
                    }

                    if (i == screenPoints.size() - 2) {
                        p3 = p2;  // Duplicate last point
                    } else {
                        p3 = screenPoints[i + 2];
                    }

                    // Convert Catmull-Rom to cubic Bezier control points
                    // Catmull-Rom uses tension = 0.5
                    QPointF c1 = p1 + (p2 - p0) / 6.0;
                    QPointF c2 = p2 - (p3 - p1) / 6.0;

                    // Draw cubic Bezier curve
                    path.cubicTo(c1, c2, p2);
                }
            }

            painter.drawPath(path);
        }
        break;

    case SketchEntityType::Text:
        if (!entity.points.isEmpty()) {
            QPoint p = worldToScreen(entity.points[0]);

            // Apply font properties
            QFont font = painter.font();
            if (!entity.fontFamily.isEmpty()) {
                font.setFamily(entity.fontFamily);
            }
            // Scale font size by zoom level (fontSize is in mm)
            double scaledSize = entity.fontSize * m_zoom;
            font.setPointSizeF(qMax(6.0, scaledSize));  // Minimum 6pt for readability
            font.setBold(entity.fontBold);
            font.setItalic(entity.fontItalic);
            painter.setFont(font);

            // Apply rotation if needed
            if (qAbs(entity.textRotation) > 0.01) {
                painter.save();
                painter.translate(p);
                painter.rotate(-entity.textRotation);  // Negative for screen coords
                painter.drawText(QPoint(0, 0), entity.text);
                painter.restore();
            } else {
                painter.drawText(p, entity.text);
            }
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

    case SketchTool::Spline:
        if (!m_previewPoints.isEmpty()) {
            // Draw the spline curve preview with current mouse position
            QVector<QPointF> allPoints = m_previewPoints;
            allPoints.append(m_currentMouseWorld);

            // Convert to screen coordinates
            QVector<QPointF> screenPoints;
            for (const QPointF& wp : allPoints) {
                screenPoints.append(worldToScreen(wp));
            }

            QPainterPath path;
            path.moveTo(screenPoints[0]);

            if (screenPoints.size() == 2) {
                // Just two points - draw a line
                path.lineTo(screenPoints[1]);
            } else {
                // Catmull-Rom spline through all points
                for (int i = 0; i < screenPoints.size() - 1; ++i) {
                    QPointF p0, p1, p2, p3;

                    p1 = screenPoints[i];
                    p2 = screenPoints[i + 1];

                    if (i == 0) {
                        p0 = p1;
                    } else {
                        p0 = screenPoints[i - 1];
                    }

                    if (i == screenPoints.size() - 2) {
                        p3 = p2;
                    } else {
                        p3 = screenPoints[i + 2];
                    }

                    // Convert Catmull-Rom to cubic Bezier control points
                    QPointF c1 = p1 + (p2 - p0) / 6.0;
                    QPointF c2 = p2 - (p3 - p1) / 6.0;

                    path.cubicTo(c1, c2, p2);
                }
            }

            painter.drawPath(path);

            // Draw control points
            painter.setBrush(QColor(0, 120, 215));
            for (const QPointF& sp : screenPoints) {
                painter.drawEllipse(sp, 3, 3);
            }
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

void SketchCanvas::drawSnapGuides(QPainter& painter)
{
    // Get current handle position
    const SketchEntity* sel = selectedEntity();
    if (!sel || m_dragHandleIndex < 0 || m_dragHandleIndex >= sel->points.size()) {
        return;
    }

    QPointF handlePos = sel->points[m_dragHandleIndex];
    QPoint handleScreen = worldToScreen(handlePos);

    // Calculate visible area for drawing constraint lines
    QPointF topLeft = screenToWorld(QPoint(0, 0));
    QPointF bottomRight = screenToWorld(QPoint(width(), height()));

    // Colors for constraint guides
    QColor guideColor(255, 140, 0);  // Orange for guides
    QColor xAxisColor(255, 80, 80);   // Red-ish for X constraint
    QColor yAxisColor(80, 200, 80);   // Green-ish for Y constraint

    // Dashed line style for guides
    QPen guidePen(guideColor, 1, Qt::DashLine);

    // Draw guide from original position to current snapped position
    QPoint origScreen = worldToScreen(m_dragHandleOriginal);

    if (m_snapAxis == SnapAxis::None) {
        // Full snap - draw crosshair at snapped position
        guidePen.setColor(guideColor);
        painter.setPen(guidePen);

        // Horizontal line through handle
        painter.drawLine(0, handleScreen.y(), width(), handleScreen.y());
        // Vertical line through handle
        painter.drawLine(handleScreen.x(), 0, handleScreen.x(), height());

        // Draw small indicator showing snap is active
        painter.setPen(QPen(guideColor, 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(handleScreen, 12, 12);

    } else if (m_snapAxis == SnapAxis::X) {
        // X-axis locked - draw horizontal constraint line
        guidePen.setColor(xAxisColor);
        guidePen.setStyle(Qt::SolidLine);
        guidePen.setWidth(2);
        painter.setPen(guidePen);

        // Draw horizontal line at the locked Y position
        int lockedY = worldToScreen(QPointF(0, m_dragHandleOriginal.y())).y();
        painter.drawLine(0, lockedY, width(), lockedY);

        // Draw vertical dashed line showing X movement
        guidePen.setStyle(Qt::DashLine);
        guidePen.setWidth(1);
        painter.setPen(guidePen);
        painter.drawLine(handleScreen.x(), 0, handleScreen.x(), height());

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

    } else if (m_snapAxis == SnapAxis::Y) {
        // Y-axis locked - draw vertical constraint line
        guidePen.setColor(yAxisColor);
        guidePen.setStyle(Qt::SolidLine);
        guidePen.setWidth(2);
        painter.setPen(guidePen);

        // Draw vertical line at the locked X position
        int lockedX = worldToScreen(QPointF(m_dragHandleOriginal.x(), 0)).x();
        painter.drawLine(lockedX, 0, lockedX, height());

        // Draw horizontal dashed line showing Y movement
        guidePen.setStyle(Qt::DashLine);
        guidePen.setWidth(1);
        painter.setPen(guidePen);
        painter.drawLine(0, handleScreen.y(), width(), handleScreen.y());

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

// ---- Constraint Drawing Functions ----

void SketchCanvas::drawConstraints(QPainter& painter)
{
    for (const SketchConstraint& constraint : m_constraints) {
        if (!constraint.enabled || !constraint.labelVisible) continue;
        drawConstraint(painter, constraint);
    }
}

void SketchCanvas::drawConstraint(QPainter& painter, const SketchConstraint& constraint)
{
    QColor constraintColor;
    if (!constraint.isDriving) {
        constraintColor = QColor(128, 128, 128);  // Gray for Driven (reference) dimensions
    } else if (constraint.satisfied) {
        constraintColor = QColor(0, 120, 215);    // Blue for satisfied driving constraints
    } else {
        constraintColor = Qt::red;                 // Red for failed constraints
    }

    QPen pen(constraintColor, 1);
    if (constraint.selected) {
        pen.setColor(QColor(255, 140, 0));  // Orange for selected
        pen.setWidth(2);
    }
    painter.setPen(pen);

    switch (constraint.type) {
    case ConstraintType::Distance:
        drawDistanceConstraint(painter, constraint);
        break;
    case ConstraintType::Radius:
    case ConstraintType::Diameter:
        drawRadialConstraint(painter, constraint);
        break;
    case ConstraintType::Angle:
        drawAngleConstraint(painter, constraint);
        break;
    case ConstraintType::Horizontal:
    case ConstraintType::Vertical:
    case ConstraintType::Parallel:
    case ConstraintType::Perpendicular:
    case ConstraintType::Coincident:
    case ConstraintType::Equal:
    case ConstraintType::Tangent:
    case ConstraintType::Midpoint:
    case ConstraintType::Symmetric:
        drawGeometricConstraint(painter, constraint);
        break;
    default:
        break;
    }
}

void SketchCanvas::drawDistanceConstraint(QPainter& painter, const SketchConstraint& constraint)
{
    if (constraint.entityIds.size() < 2) return;

    // Get constraint endpoints
    QPointF p1, p2;
    if (!getConstraintEndpoints(constraint, p1, p2)) return;

    QPoint sp1 = worldToScreen(p1);
    QPoint sp2 = worldToScreen(p2);
    QPoint labelScreen = worldToScreen(constraint.labelPosition);

    // Draw witness lines (from geometry to dimension line location)
    QPen witnessLinePen = painter.pen();
    witnessLinePen.setStyle(Qt::DashLine);
    witnessLinePen.setWidth(1);
    painter.setPen(witnessLinePen);
    painter.drawLine(sp1, labelScreen);
    painter.drawLine(sp2, labelScreen);

    // Draw dimension line
    QPen dimLinePen = painter.pen();
    dimLinePen.setStyle(Qt::SolidLine);
    dimLinePen.setWidth(2);
    painter.setPen(dimLinePen);

    QLineF dimLine(sp1, sp2);
    if (dimLine.length() > 0) {
        QPointF arrowDir = (sp2 - sp1) / dimLine.length();

        // Draw arrows at both ends
        drawArrow(painter, sp1, arrowDir, 8);
        drawArrow(painter, sp2, -arrowDir, 8);
    }

    // Draw value text (parentheses for Driven dimensions)
    QString text = QString::number(constraint.value, 'f', 2);
    if (!constraint.isDriving) {
        text = "(" + text + ")";
    }
    QFontMetrics fm(painter.font());
    QRect textRect = fm.boundingRect(text);
    textRect.moveCenter(labelScreen);

    // White background for text
    painter.fillRect(textRect.adjusted(-2, -2, 2, 2), Qt::white);
    painter.drawText(textRect, Qt::AlignCenter, text);
}

void SketchCanvas::drawRadialConstraint(QPainter& painter, const SketchConstraint& constraint)
{
    if (constraint.entityIds.isEmpty()) return;

    const SketchEntity* entity = entityById(constraint.entityIds[0]);
    if (!entity || (entity->type != SketchEntityType::Circle && entity->type != SketchEntityType::Arc)) {
        return;
    }

    if (entity->points.isEmpty()) return;

    QPoint center = worldToScreen(entity->points[0]);
    QPoint labelScreen = worldToScreen(constraint.labelPosition);

    // Draw radius/diameter line from center to label
    painter.setPen(QPen(painter.pen().color(), 1));
    painter.drawLine(center, labelScreen);

    // Draw value text with R or Ø prefix (parentheses for Driven dimensions)
    QString prefix = (constraint.type == ConstraintType::Radius) ? "R" : "Ø";
    QString text = prefix + QString::number(constraint.value, 'f', 2);
    if (!constraint.isDriving) {
        text = "(" + text + ")";
    }

    QFontMetrics fm(painter.font());
    QRect textRect = fm.boundingRect(text);
    textRect.moveCenter(labelScreen);

    // White background for text
    painter.fillRect(textRect.adjusted(-2, -2, 2, 2), Qt::white);
    painter.drawText(textRect, Qt::AlignCenter, text);
}

void SketchCanvas::drawAngleConstraint(QPainter& painter, const SketchConstraint& constraint)
{
    if (constraint.entityIds.size() < 2) return;

    const SketchEntity* e1 = entityById(constraint.entityIds[0]);
    const SketchEntity* e2 = entityById(constraint.entityIds[1]);

    if (!e1 || !e2 || e1->type != SketchEntityType::Line || e2->type != SketchEntityType::Line) {
        return;
    }

    if (e1->points.size() < 2 || e2->points.size() < 2) return;

    // Find intersection point of the two lines (or closest points)
    QLineF line1(e1->points[0], e1->points[1]);
    QLineF line2(e2->points[0], e2->points[1]);

    QPointF intersection;
    QLineF::IntersectionType intersectType = line1.intersects(line2, &intersection);

    if (intersectType == QLineF::NoIntersection) {
        // Lines don't intersect, use midpoint of label position
        intersection = constraint.labelPosition;
    }

    QPoint intersectScreen = worldToScreen(intersection);
    QPoint labelScreen = worldToScreen(constraint.labelPosition);

    // Draw arc to show angle
    double angle1 = line1.angle();
    double angle2 = line2.angle();
    double startAngle = qMin(angle1, angle2);
    double sweepAngle = qAbs(angle2 - angle1);
    if (sweepAngle > 180) sweepAngle = 360 - sweepAngle;

    int radius = 30;  // Fixed screen-space radius for angle arc
    QRect arcRect(intersectScreen.x() - radius, intersectScreen.y() - radius,
                  radius * 2, radius * 2);

    painter.setPen(QPen(painter.pen().color(), 1));
    painter.drawArc(arcRect, static_cast<int>(startAngle * 16), static_cast<int>(sweepAngle * 16));

    // Draw value text with degree symbol (parentheses for Driven dimensions)
    QString text = QString::number(constraint.value, 'f', 1) + "°";
    if (!constraint.isDriving) {
        text = "(" + text + ")";
    }

    QFontMetrics fm(painter.font());
    QRect textRect = fm.boundingRect(text);
    textRect.moveCenter(labelScreen);

    // White background for text
    painter.fillRect(textRect.adjusted(-2, -2, 2, 2), Qt::white);
    painter.drawText(textRect, Qt::AlignCenter, text);
}

void SketchCanvas::drawGeometricConstraint(QPainter& painter, const SketchConstraint& constraint)
{
    if (constraint.entityIds.isEmpty()) return;

    const SketchEntity* entity = entityById(constraint.entityIds[0]);
    if (!entity) return;

    // Get position for symbol - usually midpoint of line or center of entity
    QPointF symbolPos;
    if (entity->type == SketchEntityType::Line && entity->points.size() >= 2) {
        symbolPos = (entity->points[0] + entity->points[1]) / 2.0;
    } else if (!entity->points.isEmpty()) {
        symbolPos = entity->points[0];
    } else {
        return;
    }

    QPoint symbolScreen = worldToScreen(symbolPos);

    // Draw constraint symbol
    QFont font = painter.font();
    font.setPointSize(12);
    font.setBold(true);
    painter.setFont(font);

    QString symbol;
    switch (constraint.type) {
    case ConstraintType::Horizontal:
        symbol = "—";  // Horizontal line
        break;
    case ConstraintType::Vertical:
        symbol = "|";  // Vertical line
        break;
    case ConstraintType::Parallel:
        symbol = "//";
        break;
    case ConstraintType::Perpendicular:
        symbol = "⊥";
        break;
    case ConstraintType::Coincident:
        // Draw small filled circle
        painter.setBrush(painter.pen().color());
        painter.drawEllipse(symbolScreen, 4, 4);
        return;
    case ConstraintType::Equal:
        symbol = "=";
        break;
    case ConstraintType::Tangent:
        symbol = "⌒";  // Arc symbol for tangent
        break;
    case ConstraintType::Midpoint:
        symbol = "◇";  // Diamond for midpoint
        break;
    case ConstraintType::Symmetric:
        symbol = "⟷";  // Double arrow for symmetry
        break;
    default:
        return;
    }

    // Draw symbol with white background
    QFontMetrics fm(painter.font());
    QRect textRect = fm.boundingRect(symbol);
    textRect.moveCenter(symbolScreen);

    painter.fillRect(textRect.adjusted(-2, -2, 2, 2), Qt::white);
    painter.drawText(textRect, Qt::AlignCenter, symbol);
}

void SketchCanvas::drawArrow(QPainter& painter, const QPointF& pos, const QPointF& dir, double size)
{
    // Draw simple arrow at position pointing in direction
    QPointF perpDir(-dir.y(), dir.x());

    QPointF arrowTip = pos;
    QPointF arrowLeft = arrowTip - dir * size + perpDir * (size / 2.0);
    QPointF arrowRight = arrowTip - dir * size - perpDir * (size / 2.0);

    painter.drawLine(arrowTip, arrowLeft);
    painter.drawLine(arrowTip, arrowRight);
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

    // Right-click to finish spline
    if (event->button() == Qt::RightButton) {
        if (m_isDrawing && m_activeTool == SketchTool::Spline) {
            finishEntity();
        }
        return;
    }

    if (event->button() == Qt::LeftButton) {
        // Handle calibration entity selection mode - select line for alignment
        if (m_calibrationEntitySelectionMode) {
            int hitId = hitTest(worldPos);
            if (hitId >= 0) {
                const SketchEntity* entity = entityById(hitId);
                // Only allow lines (including construction lines) for alignment
                if (entity && entity->type == SketchEntityType::Line) {
                    double angle = getEntityAngle(hitId);
                    emit calibrationEntitySelected(hitId, angle);
                    return;
                }
            }
            // Click didn't hit a valid entity - ignore
            return;
        }

        // Handle background calibration mode - pick points for scale calibration
        if (m_backgroundCalibrationMode && m_backgroundImage.enabled) {
            // Check if click is within background bounds
            if (m_backgroundImage.containsPoint(worldPos)) {
                emit calibrationPointPicked(worldPos);
                return;
            }
        }

        // Handle background edit mode first
        if (m_backgroundEditMode && m_backgroundImage.enabled) {
            BackgroundHandle handle = hitTestBackgroundHandle(worldPos);
            if (handle != BackgroundHandle::None) {
                m_bgDragHandle = handle;
                m_bgDragStartWorld = worldPos;
                m_bgOriginalPosition = m_backgroundImage.position;
                m_bgOriginalWidth = m_backgroundImage.width;
                m_bgOriginalHeight = m_backgroundImage.height;
                updateCursorForBackgroundHandle(handle);
                return;
            }
            // Clicking outside background exits edit mode
            setBackgroundEditMode(false);
        }

        if (m_activeTool == SketchTool::Select) {
            // First check if clicking on a handle of the selected entity
            int handleIdx = hitTestHandle(worldPos);
            if (handleIdx >= 0) {
                // Start dragging the handle
                m_isDraggingHandle = true;
                m_dragHandleIndex = handleIdx;
                m_dragStartWorld = worldPos;
                m_lastRawMouseWorld = worldPos;
                m_shiftWasPressed = (event->modifiers() & Qt::ShiftModifier);
                m_ctrlWasPressed = (event->modifiers() & Qt::ControlModifier);

                // Store original handle positions for constraint behavior
                SketchEntity* sel = selectedEntity();
                if (sel && handleIdx < sel->points.size()) {
                    m_dragHandleOriginal = sel->points[handleIdx];
                    if (sel->points.size() > 1) {
                        m_dragHandleOriginal2 = sel->points[1];
                    }
                    m_dragOriginalRadius = sel->radius;
                }

                setCursor(Qt::SizeAllCursor);
                return;
            }

            // Check if clicking on a constraint label first
            int constraintId = hitTestConstraintLabel(worldPos);
            if (constraintId >= 0) {
                // If already selected, start dragging the label
                if (constraintId == m_selectedConstraintId) {
                    SketchConstraint* constraint = constraintById(constraintId);
                    if (constraint) {
                        m_isDraggingConstraintLabel = true;
                        m_constraintLabelOriginal = constraint->labelPosition;
                        m_dragStartWorld = worldPos;
                        setCursor(Qt::SizeAllCursor);
                    }
                } else {
                    // Select constraint
                    // Deselect all entities
                    for (auto& e : m_entities) {
                        e.selected = false;
                    }
                    m_selectedId = -1;
                    m_selectedIds.clear();

                    // Deselect old constraint
                    for (auto& c : m_constraints) {
                        c.selected = (c.id == constraintId);
                    }
                    m_selectedConstraintId = constraintId;
                    emit selectionChanged(-1);  // Deselect entity
                    update();
                }
                return;
            }

            // Hit test for entity selection
            int hitId = hitTest(worldPos);
            bool ctrlHeld = (event->modifiers() & Qt::ControlModifier);

            if (hitId >= 0) {
                // Clicked on an entity
                // Deselect constraints
                for (auto& c : m_constraints) {
                    c.selected = false;
                }
                m_selectedConstraintId = -1;

                // Use selectEntity with addToSelection based on Ctrl key
                selectEntity(hitId, ctrlHeld);
            } else {
                // Clicked on empty space - start window selection
                m_isWindowSelecting = true;
                m_windowSelectStart = worldPos;
                m_windowSelectEnd = worldPos;
                m_windowSelectCrossing = false;  // Will be determined by drag direction

                // Clear selection unless Ctrl held
                if (!ctrlHeld) {
                    clearSelection();
                }
            }
        } else {
            // Check if in tangent mode and selecting targets
            if (m_activeTool == SketchTool::Circle &&
                (m_circleMode == CircleMode::TwoTangent || m_circleMode == CircleMode::ThreeTangent)) {
                // Select entity for tangent target
                int hitId = hitTest(worldPos);
                if (hitId >= 0 && !m_tangentTargets.contains(hitId)) {
                    m_tangentTargets.append(hitId);

                    // Check if we have enough targets
                    if ((m_circleMode == CircleMode::TwoTangent && m_tangentTargets.size() >= 2) ||
                        (m_circleMode == CircleMode::ThreeTangent && m_tangentTargets.size() >= 3)) {
                        // Have enough targets, now place the circle
                        startEntity(snapPoint(worldPos));
                    }
                    update();
                }
            } else if (m_activeTool == SketchTool::Arc && m_arcMode == ArcMode::Tangent) {
                // For tangent arc, first click selects the entity to be tangent to
                if (m_tangentTargets.isEmpty()) {
                    int hitId = hitTest(worldPos);
                    if (hitId >= 0) {
                        m_tangentTargets.append(hitId);
                        // Start the arc with the tangent point
                        startEntity(snapPoint(worldPos));
                        update();
                    }
                } else {
                    // Second click is the end point
                    startEntity(snapPoint(worldPos));
                }
            } else if (m_activeTool == SketchTool::Dimension && m_isCreatingConstraint) {
                // Dimension tool: 3-click workflow
                if (m_constraintTargetEntities.size() < 2) {
                    // First or second click: select entity
                    int hitId = hitTest(worldPos);
                    if (hitId >= 0) {
                        m_constraintTargetEntities.append(hitId);

                        // Find closest point on entity for constraint attachment
                        SketchEntity* entity = entityById(hitId);
                        QPointF closestPoint = findClosestPointOnEntity(entity, worldPos);
                        m_constraintTargetPoints.append(closestPoint);

                        if (m_constraintTargetEntities.size() == 2) {
                            // Auto-detect constraint type based on selected entities
                            m_pendingConstraintType = detectConstraintType(
                                m_constraintTargetEntities[0],
                                m_constraintTargetEntities[1]
                            );
                        }
                        update();
                    }
                } else if (m_constraintTargetEntities.size() == 2) {
                    // Third click: place dimension label
                    QPointF labelPos = worldPos;

                    // Calculate initial value from current geometry
                    double initialValue = calculateConstraintValue(
                        m_pendingConstraintType,
                        m_constraintTargetEntities,
                        m_constraintTargetPoints
                    );

                    // Prompt user to edit value
                    QString title, label;
                    switch (m_pendingConstraintType) {
                    case ConstraintType::Distance:
                        title = tr("Dimension Value");
                        label = tr("Distance (mm):");
                        break;
                    case ConstraintType::Radius:
                        title = tr("Radius Dimension");
                        label = tr("Radius (mm):");
                        break;
                    case ConstraintType::Angle:
                        title = tr("Angle Dimension");
                        label = tr("Angle (degrees):");
                        break;
                    default:
                        title = tr("Dimension Value");
                        label = tr("Value:");
                        break;
                    }

                    bool ok = false;
                    double value = QInputDialog::getDouble(
                        this,
                        title,
                        label,
                        initialValue,      // default value
                        0.0,               // minimum
                        1000000.0,         // maximum
                        2,                 // decimals
                        &ok
                    );

                    if (ok) {
                        createConstraint(m_pendingConstraintType, value, labelPos);
                    }

                    // Reset for next constraint
                    m_constraintTargetEntities.clear();
                    m_constraintTargetPoints.clear();
                }
            } else if (m_activeTool == SketchTool::Trim) {
                // Trim tool: click on entity to remove segment between intersections
                int hitId = hitTest(worldPos);
                if (hitId >= 0) {
                    if (trimEntityAt(hitId, worldPos)) {
                        // Successfully trimmed
                        m_selectedId = -1;  // Deselect since original entity may be deleted
                    } else {
                        // No intersections found - show feedback
                        QMessageBox::information(this, tr("Trim"),
                            tr("No intersections found on this entity to trim."));
                    }
                }
            } else if (m_activeTool == SketchTool::Extend) {
                // Extend tool: click on entity to extend to nearest intersection
                int hitId = hitTest(worldPos);
                if (hitId >= 0) {
                    if (extendEntityTo(hitId, worldPos)) {
                        // Successfully extended
                    } else {
                        // No target found - show feedback
                        QMessageBox::information(this, tr("Extend"),
                            tr("No intersection target found in the extension direction."));
                    }
                }
            } else if (m_activeTool == SketchTool::Split) {
                // Split tool behavior depends on selection state:
                // 1. If a line and point are selected, split line at point
                // 2. If two entities are selected, split at their intersection
                // 3. Otherwise, click to split at that point or at all intersections
                int hitId = hitTest(worldPos);

                // Check if we have pre-selected entities for targeted split
                if (m_selectedIds.size() == 2) {
                    // Two entities selected - find their intersection and split there
                    QList<int> ids = m_selectedIds.values();
                    int id1 = ids[0];
                    int id2 = ids[1];

                    const SketchEntity* e1 = entityById(id1);
                    const SketchEntity* e2 = entityById(id2);

                    if (e1 && e2) {
                        // Check if one is a point - split the other at that point
                        if (e1->type == SketchEntityType::Point && !e1->points.isEmpty()) {
                            QVector<int> newIds = splitEntityAt(id2, e1->points[0]);
                            if (!newIds.isEmpty()) {
                                clearSelection();
                                for (int id : newIds) selectEntity(id, true);
                            }
                        } else if (e2->type == SketchEntityType::Point && !e2->points.isEmpty()) {
                            QVector<int> newIds = splitEntityAt(id1, e2->points[0]);
                            if (!newIds.isEmpty()) {
                                clearSelection();
                                for (int id : newIds) selectEntity(id, true);
                            }
                        } else {
                            // Find intersection between the two entities
                            QVector<Intersection> allIntersections = findAllIntersections();
                            QPointF splitPoint;
                            bool foundIntersection = false;

                            for (const Intersection& inter : allIntersections) {
                                if ((inter.entityId1 == id1 && inter.entityId2 == id2) ||
                                    (inter.entityId1 == id2 && inter.entityId2 == id1)) {
                                    splitPoint = inter.point;
                                    foundIntersection = true;
                                    break;
                                }
                            }

                            if (foundIntersection) {
                                // Split both entities at the intersection
                                QVector<int> newIds1 = splitEntityAt(id1, splitPoint);
                                QVector<int> newIds2 = splitEntityAt(id2, splitPoint);
                                clearSelection();
                                for (int id : newIds1) selectEntity(id, true);
                                for (int id : newIds2) selectEntity(id, true);
                            } else {
                                QMessageBox::information(this, tr("Split"),
                                    tr("No intersection found between selected entities."));
                            }
                        }
                    }
                } else if (hitId >= 0) {
                    // No pre-selection or single selection - split clicked entity
                    QVector<int> newIds = splitEntityAtIntersections(hitId);
                    if (newIds.isEmpty()) {
                        // No intersections - try splitting at click point
                        newIds = splitEntityAt(hitId, worldPos);
                        if (newIds.isEmpty()) {
                            QMessageBox::information(this, tr("Split"),
                                tr("Could not split entity at this location."));
                        }
                    }
                    if (!newIds.isEmpty()) {
                        m_selectedId = -1;  // Deselect since original entity is deleted
                    }
                }
            } else if (m_activeTool == SketchTool::Offset) {
                // Offset tool: click on entity to create parallel geometry
                int hitId = hitTest(worldPos);
                if (hitId >= 0) {
                    const SketchEntity* entity = entityById(hitId);
                    if (entity && (entity->type == SketchEntityType::Line ||
                                   entity->type == SketchEntityType::Circle ||
                                   entity->type == SketchEntityType::Arc)) {
                        // Prompt for offset distance
                        bool ok = false;
                        double distance = QInputDialog::getDouble(
                            this,
                            tr("Offset Distance"),
                            tr("Enter offset distance (mm):"),
                            5.0,    // default
                            0.1,    // minimum
                            1000.0, // maximum
                            2,      // decimals
                            &ok
                        );

                        if (ok) {
                            // Determine offset direction based on click position relative to entity
                            offsetEntity(hitId, distance, worldPos);
                        }
                    } else {
                        QMessageBox::information(this, tr("Offset"),
                            tr("Offset is supported for lines, circles, and arcs."));
                    }
                }
            } else if (m_activeTool == SketchTool::Fillet) {
                // Fillet tool: click on corner (intersection of two lines) to round it
                int hitId = hitTest(worldPos);
                if (hitId >= 0) {
                    const SketchEntity* entity = entityById(hitId);
                    if (entity && entity->type == SketchEntityType::Line) {
                        // Find connected line at closest endpoint
                        int connectedId = findConnectedLineAtCorner(hitId, worldPos);
                        if (connectedId >= 0) {
                            // Prompt for fillet radius
                            bool ok = false;
                            double radius = QInputDialog::getDouble(
                                this,
                                tr("Fillet Radius"),
                                tr("Enter fillet radius (mm):"),
                                5.0,    // default
                                0.1,    // minimum
                                1000.0, // maximum
                                2,      // decimals
                                &ok
                            );

                            if (ok) {
                                filletCorner(hitId, connectedId, radius);
                            }
                        } else {
                            QMessageBox::information(this, tr("Fillet"),
                                tr("Click on a corner where two lines meet."));
                        }
                    } else {
                        QMessageBox::information(this, tr("Fillet"),
                            tr("Fillet requires two connected lines. Click on a line near a corner."));
                    }
                }
            } else if (m_activeTool == SketchTool::Chamfer) {
                // Chamfer tool: click on corner to create beveled edge
                int hitId = hitTest(worldPos);
                if (hitId >= 0) {
                    const SketchEntity* entity = entityById(hitId);
                    if (entity && entity->type == SketchEntityType::Line) {
                        // Find connected line at closest endpoint
                        int connectedId = findConnectedLineAtCorner(hitId, worldPos);
                        if (connectedId >= 0) {
                            // Prompt for chamfer distance
                            bool ok = false;
                            double distance = QInputDialog::getDouble(
                                this,
                                tr("Chamfer Distance"),
                                tr("Enter chamfer distance (mm):"),
                                5.0,    // default
                                0.1,    // minimum
                                1000.0, // maximum
                                2,      // decimals
                                &ok
                            );

                            if (ok) {
                                chamferCorner(hitId, connectedId, distance);
                            }
                        } else {
                            QMessageBox::information(this, tr("Chamfer"),
                                tr("Click on a corner where two lines meet."));
                        }
                    } else {
                        QMessageBox::information(this, tr("Chamfer"),
                            tr("Chamfer requires two connected lines. Click on a line near a corner."));
                    }
                }
            } else if (m_activeTool == SketchTool::RectPattern) {
                // Rectangular pattern: select entities then configure pattern
                int hitId = hitTest(worldPos);
                if (hitId >= 0) {
                    // Add to selection for pattern
                    bool ctrlHeld = (event->modifiers() & Qt::ControlModifier);
                    selectEntity(hitId, ctrlHeld);
                    update();

                    // If we have a selection, offer to create pattern
                    if (!m_selectedIds.isEmpty()) {
                        createRectangularPattern();
                    }
                }
            } else if (m_activeTool == SketchTool::CircPattern) {
                // Circular pattern: select entities then configure pattern
                int hitId = hitTest(worldPos);
                if (hitId >= 0) {
                    // Add to selection for pattern
                    bool ctrlHeld = (event->modifiers() & Qt::ControlModifier);
                    selectEntity(hitId, ctrlHeld);
                    update();

                    // If we have a selection, offer to create pattern
                    if (!m_selectedIds.isEmpty()) {
                        createCircularPattern();
                    }
                }
            } else if (m_activeTool == SketchTool::Project) {
                // Project tool: project geometry from other sketches or 3D edges
                // For now, show a dialog explaining this is for projecting external geometry
                QMessageBox::information(this, tr("Project"),
                    tr("Project tool allows projecting geometry from:\n"
                       "• Other sketches in this document\n"
                       "• 3D model edges onto this sketch plane\n\n"
                       "Select geometry in the model tree or another sketch to project it here."));
            } else {
                // Start drawing normally
                startEntity(snapPoint(worldPos));
            }
        }
    }
}

void SketchCanvas::mouseMoveEvent(QMouseEvent* event)
{
    QPointF worldPos = screenToWorld(event->pos());
    m_currentMouseWorld = snapPoint(worldPos);
    emit mousePositionChanged(m_currentMouseWorld);

    // Emit absolute coordinates based on sketch plane
    // Convert 2D sketch coords to 3D absolute coords based on plane orientation
    QVector3D absolutePos;
    switch (m_plane) {
    case SketchPlane::XY:
        absolutePos = QVector3D(
            m_planeOrigin.x() + m_currentMouseWorld.x(),
            m_planeOrigin.y() + m_currentMouseWorld.y(),
            m_planeOrigin.z());
        break;
    case SketchPlane::XZ:
        absolutePos = QVector3D(
            m_planeOrigin.x() + m_currentMouseWorld.x(),
            m_planeOrigin.y(),
            m_planeOrigin.z() + m_currentMouseWorld.y());
        break;
    case SketchPlane::YZ:
        absolutePos = QVector3D(
            m_planeOrigin.x(),
            m_planeOrigin.y() + m_currentMouseWorld.x(),
            m_planeOrigin.z() + m_currentMouseWorld.y());
        break;
    default:
        // For custom planes, use the origin plus 2D coords (simplified)
        absolutePos = QVector3D(
            m_planeOrigin.x() + m_currentMouseWorld.x(),
            m_planeOrigin.y() + m_currentMouseWorld.y(),
            m_planeOrigin.z());
        break;
    }
    emit mousePositionChangedAbsolute(absolutePos, m_currentMouseWorld);

    if (m_isPanning) {
        QPoint delta = event->pos() - m_lastMousePos;
        m_viewCenter.rx() -= delta.x() / m_zoom;
        m_viewCenter.ry() += delta.y() / m_zoom;
        m_lastMousePos = event->pos();
        update();
        return;
    }

    if (m_isWindowSelecting) {
        // Update window selection rectangle
        m_windowSelectEnd = worldPos;
        // Determine if crossing mode (right-to-left drag)
        m_windowSelectCrossing = (m_windowSelectEnd.x() < m_windowSelectStart.x());
        update();
        return;
    }

    // Handle background dragging
    if (m_bgDragHandle != BackgroundHandle::None) {
        double dx = worldPos.x() - m_bgDragStartWorld.x();
        double dy = worldPos.y() - m_bgDragStartWorld.y();

        switch (m_bgDragHandle) {
        case BackgroundHandle::Move:
            m_backgroundImage.position.setX(m_bgOriginalPosition.x() + dx);
            m_backgroundImage.position.setY(m_bgOriginalPosition.y() + dy);
            break;

        case BackgroundHandle::TopLeft: {
            double newWidth = m_bgOriginalWidth - dx;
            double newHeight = m_bgOriginalHeight + dy;  // Y is flipped
            if (newWidth > 1 && newHeight > 1) {
                if (m_backgroundImage.lockAspectRatio) {
                    double ratio = m_bgOriginalHeight / m_bgOriginalWidth;
                    newHeight = newWidth * ratio;
                    dy = newHeight - m_bgOriginalHeight;
                }
                m_backgroundImage.width = newWidth;
                m_backgroundImage.height = newHeight;
                m_backgroundImage.position.setX(m_bgOriginalPosition.x() + dx);
            }
            break;
        }

        case BackgroundHandle::TopRight: {
            double newWidth = m_bgOriginalWidth + dx;
            double newHeight = m_bgOriginalHeight + dy;
            if (newWidth > 1 && newHeight > 1) {
                if (m_backgroundImage.lockAspectRatio) {
                    double ratio = m_bgOriginalHeight / m_bgOriginalWidth;
                    newHeight = newWidth * ratio;
                }
                m_backgroundImage.width = newWidth;
                m_backgroundImage.height = newHeight;
            }
            break;
        }

        case BackgroundHandle::BottomRight: {
            double newWidth = m_bgOriginalWidth + dx;
            double newHeight = m_bgOriginalHeight - dy;
            if (newWidth > 1 && newHeight > 1) {
                if (m_backgroundImage.lockAspectRatio) {
                    double ratio = m_bgOriginalHeight / m_bgOriginalWidth;
                    newHeight = newWidth * ratio;
                    dy = m_bgOriginalHeight - newHeight;
                }
                m_backgroundImage.width = newWidth;
                m_backgroundImage.height = newHeight;
                m_backgroundImage.position.setY(m_bgOriginalPosition.y() + dy);
            }
            break;
        }

        case BackgroundHandle::BottomLeft: {
            double newWidth = m_bgOriginalWidth - dx;
            double newHeight = m_bgOriginalHeight - dy;
            if (newWidth > 1 && newHeight > 1) {
                if (m_backgroundImage.lockAspectRatio) {
                    double ratio = m_bgOriginalHeight / m_bgOriginalWidth;
                    newHeight = newWidth * ratio;
                    dy = m_bgOriginalHeight - newHeight;
                }
                m_backgroundImage.width = newWidth;
                m_backgroundImage.height = newHeight;
                m_backgroundImage.position.setX(m_bgOriginalPosition.x() + dx);
                m_backgroundImage.position.setY(m_bgOriginalPosition.y() + dy);
            }
            break;
        }

        case BackgroundHandle::Top: {
            double newHeight = m_bgOriginalHeight + dy;
            if (newHeight > 1) {
                m_backgroundImage.height = newHeight;
            }
            break;
        }

        case BackgroundHandle::Bottom: {
            double newHeight = m_bgOriginalHeight - dy;
            if (newHeight > 1) {
                m_backgroundImage.height = newHeight;
                m_backgroundImage.position.setY(m_bgOriginalPosition.y() + dy);
            }
            break;
        }

        case BackgroundHandle::Left: {
            double newWidth = m_bgOriginalWidth - dx;
            if (newWidth > 1) {
                m_backgroundImage.width = newWidth;
                m_backgroundImage.position.setX(m_bgOriginalPosition.x() + dx);
            }
            break;
        }

        case BackgroundHandle::Right: {
            double newWidth = m_bgOriginalWidth + dx;
            if (newWidth > 1) {
                m_backgroundImage.width = newWidth;
            }
            break;
        }

        default:
            break;
        }

        emit backgroundImageChanged(m_backgroundImage);
        update();
        return;
    }

    // Update cursor in background edit mode
    if (m_backgroundEditMode && m_backgroundImage.enabled) {
        BackgroundHandle handle = hitTestBackgroundHandle(worldPos);
        updateCursorForBackgroundHandle(handle);
    }

    if (m_isDraggingConstraintLabel) {
        // Drag the constraint label
        SketchConstraint* constraint = constraintById(m_selectedConstraintId);
        if (constraint) {
            QPointF delta = worldPos - m_dragStartWorld;
            constraint->labelPosition = m_constraintLabelOriginal + delta;
            update();
        }
        return;
    }

    if (m_isDraggingHandle) {
        // Move the handle point of the selected entity
        SketchEntity* sel = selectedEntity();
        if (sel && m_dragHandleIndex >= 0 && m_dragHandleIndex < sel->points.size()) {
            m_lastRawMouseWorld = worldPos;
            bool shiftPressed = (event->modifiers() & Qt::ShiftModifier);
            bool ctrlPressed = (event->modifiers() & Qt::ControlModifier);

            // Determine the final position based on modifiers:
            // - Shift: snap to grid
            // - Ctrl: constrain to axis (X or Y key selects which)
            // - Shift+Ctrl: snap to grid AND constrain to axis
            QPointF finalPos;
            if (m_snapToGrid || shiftPressed) {
                // Snap to grid (global setting or Shift held)
                if (ctrlPressed && m_snapAxis != SnapAxis::None) {
                    // Also constrain to axis
                    finalPos = axisLockedSnapPoint(worldPos);
                } else {
                    finalPos = snapPoint(worldPos);
                }
            } else if (ctrlPressed && m_snapAxis != SnapAxis::None) {
                // Ctrl held with axis constraint - constrain without snap
                QPointF raw = worldPos;
                if (m_snapAxis == SnapAxis::X) {
                    finalPos = QPointF(raw.x(), m_dragHandleOriginal.y());
                } else {
                    finalPos = QPointF(m_dragHandleOriginal.x(), raw.y());
                }
            } else {
                // No modifiers - use raw position
                finalPos = worldPos;
            }

            // Handle circles and arcs specially
            if (sel->type == SketchEntityType::Circle || sel->type == SketchEntityType::Arc) {
                if (m_dragHandleIndex == 0 && sel->points.size() >= 2) {
                    // Dragging center - move both center and radius point together
                    QPointF delta = finalPos - sel->points[0];
                    sel->points[0] = finalPos;
                    sel->points[1] += delta;
                } else if (m_dragHandleIndex == 1) {
                    // Dragging radius point - update the point and radius
                    sel->points[1] = finalPos;
                    sel->radius = QLineF(sel->points[0], sel->points[1]).length();
                }
            } else {
                // For other entities, just move the point directly
                sel->points[m_dragHandleIndex] = finalPos;
            }

            // Emit real-time property update
            if (m_selectedId >= 0) {
                emit entityDragging(m_selectedId);
            }

            update();
        }
        return;
    }

    if (m_isDrawing) {
        updateEntity(m_currentMouseWorld);
    }

    // Update cursor when hovering over handles in Select mode
    if (m_activeTool == SketchTool::Select && !m_isPanning && !m_isDraggingHandle) {
        int handleIdx = hitTestHandle(worldPos);
        if (handleIdx >= 0) {
            setCursor(Qt::SizeAllCursor);
        } else {
            setCursor(Qt::ArrowCursor);
        }
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

    if (event->button() == Qt::LeftButton) {
        // Finish background drag
        if (m_bgDragHandle != BackgroundHandle::None) {
            m_bgDragHandle = BackgroundHandle::None;
            emit backgroundImageChanged(m_backgroundImage);
            update();
            return;
        }

        if (m_isWindowSelecting) {
            // Finish window selection
            m_isWindowSelecting = false;

            // Build selection rectangle
            QRectF selRect = QRectF(m_windowSelectStart, m_windowSelectEnd).normalized();

            // Only select if the rectangle has some size (not just a click)
            if (selRect.width() > 2.0 / m_zoom && selRect.height() > 2.0 / m_zoom) {
                bool ctrlHeld = (event->modifiers() & Qt::ControlModifier);
                selectEntitiesInRect(selRect, m_windowSelectCrossing, ctrlHeld);
            }

            update();
            return;
        }

        if (m_isDraggingConstraintLabel) {
            // Finish constraint label drag
            m_isDraggingConstraintLabel = false;
            setCursor(Qt::ArrowCursor);
            if (m_selectedConstraintId >= 0) {
                emit constraintModified(m_selectedConstraintId);
            }
            return;
        }

        if (m_isDraggingHandle) {
            // Finish handle drag - emit modified signal
            m_isDraggingHandle = false;
            m_dragHandleIndex = -1;
            m_snapAxis = SnapAxis::None;  // Reset axis lock
            m_shiftWasPressed = false;
            m_ctrlWasPressed = false;
            setCursor(Qt::ArrowCursor);
            if (m_selectedId >= 0) {
                emit entityModified(m_selectedId);
            }
            return;
        }

        if (m_isDrawing) {
            // Multi-click tools: arc and spline
            if (m_activeTool == SketchTool::Arc && m_arcMode == ArcMode::ThreePoint) {
                if (m_pendingEntity.points.size() < 3) {
                    // Add another point and continue
                    QPointF worldPos = screenToWorld(event->pos());
                    m_pendingEntity.points.append(snapPoint(worldPos));
                    if (m_pendingEntity.points.size() >= 3) {
                        // Have all 3 points, can finish now
                        finishEntity();
                    }
                } else {
                    finishEntity();
                }
            } else if (m_activeTool == SketchTool::Spline) {
                // Spline: add point and continue (finish with right-click or Enter)
                QPointF worldPos = screenToWorld(event->pos());
                m_pendingEntity.points.append(snapPoint(worldPos));
                update();
                // Don't finish - user needs to right-click or press Enter
            } else {
                finishEntity();
            }
        }
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

QPointF SketchCanvas::axisLockedSnapPoint(const QPointF& worldPos) const
{
    QPointF snapped = snapPoint(worldPos);

    // If no axis lock, return fully snapped point
    if (m_snapAxis == SnapAxis::None) {
        return snapped;
    }

    // Lock to specified axis - keep original position on the other axis
    if (m_snapAxis == SnapAxis::X) {
        // Only snap X, keep original Y from drag start
        return QPointF(snapped.x(), m_dragHandleOriginal.y());
    } else {  // SnapAxis::Y
        // Only snap Y, keep original X from drag start
        return QPointF(m_dragHandleOriginal.x(), snapped.y());
    }
}

void SketchCanvas::applyCtrlSnapToHandle()
{
    // Recompute handle position based on current modifier state
    SketchEntity* sel = selectedEntity();
    if (!sel || m_dragHandleIndex < 0 || m_dragHandleIndex >= sel->points.size()) {
        return;
    }

    // Determine final position based on current state
    QPointF finalPos;
    if (m_shiftWasPressed || m_snapToGrid) {
        // Snap enabled
        if (m_ctrlWasPressed && m_snapAxis != SnapAxis::None) {
            finalPos = axisLockedSnapPoint(m_lastRawMouseWorld);
        } else {
            finalPos = snapPoint(m_lastRawMouseWorld);
        }
    } else if (m_ctrlWasPressed && m_snapAxis != SnapAxis::None) {
        // Axis constraint without snap
        if (m_snapAxis == SnapAxis::X) {
            finalPos = QPointF(m_lastRawMouseWorld.x(), m_dragHandleOriginal.y());
        } else {
            finalPos = QPointF(m_dragHandleOriginal.x(), m_lastRawMouseWorld.y());
        }
    } else {
        // No modifiers - raw position
        finalPos = m_lastRawMouseWorld;
    }

    if (sel->type == SketchEntityType::Circle || sel->type == SketchEntityType::Arc) {
        if (m_dragHandleIndex == 0 && sel->points.size() >= 2) {
            QPointF delta = finalPos - sel->points[0];
            sel->points[0] = finalPos;
            sel->points[1] += delta;
        } else if (m_dragHandleIndex == 1) {
            sel->points[1] = finalPos;
            sel->radius = QLineF(sel->points[0], sel->points[1]).length();
        }
    } else {
        sel->points[m_dragHandleIndex] = finalPos;
    }

    if (m_selectedId >= 0) {
        emit entityDragging(m_selectedId);
    }
    update();
}

void SketchCanvas::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        QPointF worldPos = screenToWorld(event->pos());

        // Check if double-clicking on a constraint label
        int constraintId = hitTestConstraintLabel(worldPos);
        if (constraintId >= 0) {
            editConstraintValue(constraintId);
            return;
        }

        // Check if double-clicking on an entity - select connected chain
        if (m_activeTool == SketchTool::Select) {
            int entityId = hitTest(worldPos);
            if (entityId >= 0) {
                selectConnectedChain(entityId);
                return;
            }
        }
    }

    QWidget::mouseDoubleClickEvent(event);
}

void SketchCanvas::keyPressEvent(QKeyEvent* event)
{
    // Check configurable bindings first (for view rotation)
    if (matchesBinding(QStringLiteral("sketch.rotateCCW"), event)) {
        rotateViewCCW();
        return;
    }
    if (matchesBinding(QStringLiteral("sketch.rotateCW"), event)) {
        rotateViewCW();
        return;
    }
    if (matchesBinding(QStringLiteral("sketch.rotateReset"), event)) {
        setViewRotation(0.0);
        return;
    }

    switch (event->key()) {
    case Qt::Key_Escape:
        if (m_isDrawing) {
            // Cancel current drawing operation
            cancelEntity();
        } else if (m_activeTool != SketchTool::Select) {
            // Switch back to Select mode, keeping current selection
            m_activeTool = SketchTool::Select;
            setCursor(Qt::ArrowCursor);
            emit toolChangeRequested(SketchTool::Select);
            // Re-emit selection to update properties panel with selected entity
            if (m_selectedId >= 0) {
                emit selectionChanged(m_selectedId);
            }
        } else if (m_selectedConstraintId >= 0) {
            // Constraint selected - deselect constraint
            for (auto& c : m_constraints) c.selected = false;
            m_selectedConstraintId = -1;
            update();
        } else if (!m_selectedIds.isEmpty()) {
            // Already in Select mode with entity selected - deselect all entities
            for (auto& e : m_entities) e.selected = false;
            m_selectedId = -1;
            m_selectedIds.clear();
            emit selectionChanged(-1);
        } else if (m_sketchSelected) {
            // No entity selected, but sketch is selected - deselect sketch
            m_sketchSelected = false;
            emit sketchDeselected();
        } else {
            // Sketch already deselected - request to exit sketch mode
            emit exitRequested();
        }
        update();
        break;

    case Qt::Key_Delete:
    case Qt::Key_Backspace:
        if (m_selectedConstraintId >= 0) {
            // Delete selected constraint
            int deletedId = m_selectedConstraintId;
            m_constraints.erase(
                std::remove_if(m_constraints.begin(), m_constraints.end(),
                               [this](const SketchConstraint& c) { return c.id == m_selectedConstraintId; }),
                m_constraints.end());
            m_selectedConstraintId = -1;

            // Re-solve after deleting constraint
            solveConstraints();

            emit constraintDeleted(deletedId);
            update();
        } else if (!m_selectedIds.isEmpty()) {
            // Delete all selected entities
            int count = m_selectedIds.size();

            // Show confirmation for multiple entities
            if (count > 1) {
                QMessageBox::StandardButton reply = QMessageBox::question(
                    this,
                    tr("Delete Entities"),
                    tr("Delete %1 selected entities?").arg(count),
                    QMessageBox::Yes | QMessageBox::No,
                    QMessageBox::Yes
                );
                if (reply != QMessageBox::Yes) {
                    break;
                }
            }

            // Delete all selected entities
            QSet<int> toDelete = m_selectedIds;
            m_entities.erase(
                std::remove_if(m_entities.begin(), m_entities.end(),
                               [&toDelete](const SketchEntity& e) { return toDelete.contains(e.id); }),
                m_entities.end());

            // Also remove any constraints that reference deleted entities
            m_constraints.erase(
                std::remove_if(m_constraints.begin(), m_constraints.end(),
                               [&toDelete](const SketchConstraint& c) {
                                   for (int id : c.entityIds) {
                                       if (toDelete.contains(id)) return true;
                                   }
                                   return false;
                               }),
                m_constraints.end());

            m_selectedId = -1;
            m_selectedIds.clear();
            m_profilesCacheDirty = true;
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

    // Note: Q, E, and Ctrl+0 for view rotation are handled via configurable
    // bindings at the top of this function (sketch.rotateCCW, sketch.rotateCW,
    // sketch.rotateReset)

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

    case Qt::Key_Shift:
        // Shift pressed during handle drag - enable snap to grid
        if (m_isDraggingHandle && !m_snapToGrid) {
            m_shiftWasPressed = true;
            applyCtrlSnapToHandle();
        }
        break;

    case Qt::Key_Control:
        // Ctrl pressed during handle drag - enable axis constraint mode
        if (m_isDraggingHandle) {
            m_ctrlWasPressed = true;
            // Don't apply yet - wait for X/Y key to select axis
        }
        break;

    case Qt::Key_X:
        // X key during Ctrl+drag - lock to X axis
        // X is the horizontal axis on XY and XZ planes, ignored on YZ plane
        if (m_isDraggingHandle && m_ctrlWasPressed) {
            if (m_plane == SketchPlane::XY || m_plane == SketchPlane::XZ) {
                m_snapAxis = SnapAxis::X;  // X is horizontal
                applyCtrlSnapToHandle();
            }
            // Ignored on YZ plane (X is perpendicular to the sketch)
        }
        break;

    case Qt::Key_Y:
        // Y key during Ctrl+drag - lock to Y axis
        // Y is vertical on XY, horizontal on YZ, ignored on XZ plane
        if (m_isDraggingHandle && m_ctrlWasPressed) {
            if (m_plane == SketchPlane::XY) {
                m_snapAxis = SnapAxis::Y;  // Y is vertical
                applyCtrlSnapToHandle();
            } else if (m_plane == SketchPlane::YZ) {
                m_snapAxis = SnapAxis::X;  // Y maps to horizontal in 2D canvas
                applyCtrlSnapToHandle();
            }
            // Ignored on XZ plane (Y is perpendicular to the sketch)
        }
        break;

    case Qt::Key_Z:
        // Z key during Ctrl+drag - lock to Z axis
        // Z is vertical on XZ and YZ, ignored on XY plane
        if (m_isDraggingHandle && m_ctrlWasPressed) {
            if (m_plane == SketchPlane::XZ || m_plane == SketchPlane::YZ) {
                m_snapAxis = SnapAxis::Y;  // Z maps to vertical in 2D canvas
                applyCtrlSnapToHandle();
            }
            // Ignored on XY plane (Z is perpendicular to the sketch)
        }
        break;

    default:
        QWidget::keyPressEvent(event);
    }
}

void SketchCanvas::keyReleaseEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Shift) {
        // Shift released during handle drag - disable snap to grid (unless global snap is on)
        if (m_isDraggingHandle && !m_snapToGrid && m_shiftWasPressed) {
            m_shiftWasPressed = false;
            // Recompute position without snap (but keep axis constraint if active)
            applyCtrlSnapToHandle();
        }
    } else if (event->key() == Qt::Key_Control) {
        // Ctrl released during handle drag - reset axis constraint
        if (m_isDraggingHandle && m_ctrlWasPressed) {
            m_ctrlWasPressed = false;
            m_snapAxis = SnapAxis::None;  // Reset axis lock
            // Recompute position without axis constraint (but keep snap if Shift still held)
            applyCtrlSnapToHandle();
        }
    }
    QWidget::keyReleaseEvent(event);
}

void SketchCanvas::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    update();
}

void SketchCanvas::contextMenuEvent(QContextMenuEvent* event)
{
    QPointF worldPos = screenToWorld(event->pos());

    // Check if right-clicking on a constraint label
    int constraintId = hitTestConstraintLabel(worldPos);
    if (constraintId >= 0) {
        SketchConstraint* constraint = constraintById(constraintId);
        if (constraint) {
            QMenu menu(this);

            // Only show conversion options for dimensional constraints
            bool isDimensional = (constraint->type == ConstraintType::Distance ||
                                  constraint->type == ConstraintType::Radius ||
                                  constraint->type == ConstraintType::Diameter ||
                                  constraint->type == ConstraintType::Angle);

            if (isDimensional) {
                if (constraint->isDriving) {
                    QAction* toDrivenAction = menu.addAction(tr("Make Driven (Reference)"));
                    connect(toDrivenAction, &QAction::triggered, this, [this, constraintId]() {
                        convertToDriven(constraintId);
                    });
                } else {
                    QAction* toDrivingAction = menu.addAction(tr("Make Driving"));
                    connect(toDrivingAction, &QAction::triggered, this, [this, constraintId]() {
                        convertToDriving(constraintId);
                    });
                }
                menu.addSeparator();
            }

            QAction* editAction = menu.addAction(tr("Edit Value..."));
            connect(editAction, &QAction::triggered, this, [this, constraintId]() {
                editConstraintValue(constraintId);
            });

            QAction* deleteAction = menu.addAction(tr("Delete"));
            connect(deleteAction, &QAction::triggered, this, [this, constraintId]() {
                // Remove the constraint
                m_constraints.erase(
                    std::remove_if(m_constraints.begin(), m_constraints.end(),
                                   [constraintId](const SketchConstraint& c) { return c.id == constraintId; }),
                    m_constraints.end());

                if (m_selectedConstraintId == constraintId) {
                    m_selectedConstraintId = -1;
                }

                solveConstraints();
                emit constraintDeleted(constraintId);
                update();
            });

            menu.exec(event->globalPos());
            return;
        }
    }

    // Check if right-clicking on an entity
    int entityId = hitTest(worldPos);

    // If clicking on an entity that's part of a multi-selection, show multi-select menu
    if (entityId >= 0 && m_selectedIds.size() > 1 && m_selectedIds.contains(entityId)) {
        QMenu menu(this);
        int count = m_selectedIds.size();

        // Check if all selected are construction or all normal
        bool allConstruction = true;
        bool allNormal = true;
        for (int id : m_selectedIds) {
            const SketchEntity* ent = entityById(id);
            if (ent) {
                if (ent->isConstruction) allNormal = false;
                else allConstruction = false;
            }
        }

        // Construction geometry toggle for all
        if (allConstruction) {
            QAction* normalAction = menu.addAction(tr("Make All Normal Geometry (%1)").arg(count));
            connect(normalAction, &QAction::triggered, this, [this]() {
                for (int id : m_selectedIds) {
                    SketchEntity* ent = entityById(id);
                    if (ent) ent->isConstruction = false;
                }
                m_profilesCacheDirty = true;
                emit selectionChanged(m_selectedId);
                update();
            });
        } else if (allNormal) {
            QAction* constructionAction = menu.addAction(tr("Make All Construction Geometry (%1)").arg(count));
            connect(constructionAction, &QAction::triggered, this, [this]() {
                for (int id : m_selectedIds) {
                    SketchEntity* ent = entityById(id);
                    if (ent) ent->isConstruction = true;
                }
                m_profilesCacheDirty = true;
                emit selectionChanged(m_selectedId);
                update();
            });
        } else {
            // Mixed - show both options
            QAction* normalAction = menu.addAction(tr("Make All Normal Geometry (%1)").arg(count));
            connect(normalAction, &QAction::triggered, this, [this]() {
                for (int id : m_selectedIds) {
                    SketchEntity* ent = entityById(id);
                    if (ent) ent->isConstruction = false;
                }
                m_profilesCacheDirty = true;
                emit selectionChanged(m_selectedId);
                update();
            });

            QAction* constructionAction = menu.addAction(tr("Make All Construction Geometry (%1)").arg(count));
            connect(constructionAction, &QAction::triggered, this, [this]() {
                for (int id : m_selectedIds) {
                    SketchEntity* ent = entityById(id);
                    if (ent) ent->isConstruction = true;
                }
                m_profilesCacheDirty = true;
                emit selectionChanged(m_selectedId);
                update();
            });
        }

        menu.addSeparator();

        // Transform submenu
        QMenu* transformMenu = menu.addMenu(tr("Transform"));

        QAction* moveAction = transformMenu->addAction(tr("Move..."));
        connect(moveAction, &QAction::triggered, this, [this]() {
            transformSelectedEntities(TransformType::Move);
        });

        QAction* copyAction = transformMenu->addAction(tr("Copy..."));
        connect(copyAction, &QAction::triggered, this, [this]() {
            transformSelectedEntities(TransformType::Copy);
        });

        QAction* rotateAction = transformMenu->addAction(tr("Rotate..."));
        connect(rotateAction, &QAction::triggered, this, [this]() {
            transformSelectedEntities(TransformType::Rotate);
        });

        QAction* scaleAction = transformMenu->addAction(tr("Scale..."));
        connect(scaleAction, &QAction::triggered, this, [this]() {
            transformSelectedEntities(TransformType::Scale);
        });

        QAction* mirrorAction = transformMenu->addAction(tr("Mirror..."));
        connect(mirrorAction, &QAction::triggered, this, [this]() {
            transformSelectedEntities(TransformType::Mirror);
        });

        menu.addSeparator();

        // Alignment submenu
        QMenu* alignMenu = menu.addMenu(tr("Align"));

        QAction* alignLeftAction = alignMenu->addAction(tr("Align Left"));
        connect(alignLeftAction, &QAction::triggered, this, [this]() {
            alignSelectedEntities(AlignmentType::Left);
        });

        QAction* alignRightAction = alignMenu->addAction(tr("Align Right"));
        connect(alignRightAction, &QAction::triggered, this, [this]() {
            alignSelectedEntities(AlignmentType::Right);
        });

        QAction* alignTopAction = alignMenu->addAction(tr("Align Top"));
        connect(alignTopAction, &QAction::triggered, this, [this]() {
            alignSelectedEntities(AlignmentType::Top);
        });

        QAction* alignBottomAction = alignMenu->addAction(tr("Align Bottom"));
        connect(alignBottomAction, &QAction::triggered, this, [this]() {
            alignSelectedEntities(AlignmentType::Bottom);
        });

        alignMenu->addSeparator();

        QAction* alignHCenterAction = alignMenu->addAction(tr("Center Horizontally"));
        connect(alignHCenterAction, &QAction::triggered, this, [this]() {
            alignSelectedEntities(AlignmentType::HorizontalCenter);
        });

        QAction* alignVCenterAction = alignMenu->addAction(tr("Center Vertically"));
        connect(alignVCenterAction, &QAction::triggered, this, [this]() {
            alignSelectedEntities(AlignmentType::VerticalCenter);
        });

        alignMenu->addSeparator();

        QAction* distributeHAction = alignMenu->addAction(tr("Distribute Horizontally"));
        connect(distributeHAction, &QAction::triggered, this, [this]() {
            alignSelectedEntities(AlignmentType::DistributeHorizontal);
        });

        QAction* distributeVAction = alignMenu->addAction(tr("Distribute Vertically"));
        connect(distributeVAction, &QAction::triggered, this, [this]() {
            alignSelectedEntities(AlignmentType::DistributeVertical);
        });

        menu.addSeparator();

        // Group action
        QAction* groupAction = menu.addAction(tr("Group (%1 entities)").arg(count));
        connect(groupAction, &QAction::triggered, this, [this]() {
            groupSelectedEntities();
        });

        menu.addSeparator();

        // Boolean-like operations
        QAction* splitAllAction = menu.addAction(tr("Split All at Intersections"));
        connect(splitAllAction, &QAction::triggered, this, [this]() {
            splitSelectedAtIntersections();
        });

        menu.addSeparator();

        // Delete all selected
        QAction* deleteAction = menu.addAction(tr("Delete All (%1)").arg(count));
        connect(deleteAction, &QAction::triggered, this, [this, count]() {
            QMessageBox::StandardButton reply = QMessageBox::question(
                this,
                tr("Delete Entities"),
                tr("Delete %1 selected entities?").arg(count),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::Yes
            );
            if (reply == QMessageBox::Yes) {
                deleteSelectedEntities();
            }
        });

        menu.exec(event->globalPos());
        return;
    }

    // Single entity context menu
    if (entityId >= 0) {
        SketchEntity* entity = entityById(entityId);
        if (entity) {
            QMenu menu(this);

            // Construction geometry toggle
            QAction* constructionAction = menu.addAction(
                entity->isConstruction ? tr("Make Normal Geometry") : tr("Make Construction Geometry"));
            connect(constructionAction, &QAction::triggered, this, [this, entityId]() {
                SketchEntity* ent = entityById(entityId);
                if (ent) {
                    ent->isConstruction = !ent->isConstruction;
                    m_profilesCacheDirty = true;
                    emit entityModified(entityId);
                    update();
                }
            });

            menu.addSeparator();

            QAction* deleteAction = menu.addAction(tr("Delete"));
            connect(deleteAction, &QAction::triggered, this, [this, entityId]() {
                m_entities.erase(
                    std::remove_if(m_entities.begin(), m_entities.end(),
                                   [entityId](const SketchEntity& e) { return e.id == entityId; }),
                    m_entities.end());

                if (m_selectedId == entityId) {
                    m_selectedId = -1;
                    m_selectedIds.remove(entityId);
                    emit selectionChanged(-1);
                }
                m_profilesCacheDirty = true;
                update();
            });

            menu.exec(event->globalPos());
            return;
        }
    }

    // No specific item clicked - could add general menu items here
    QWidget::contextMenuEvent(event);
}

bool SketchCanvas::convertToDriving(int constraintId)
{
    SketchConstraint* constraint = constraintById(constraintId);
    if (!constraint || constraint->isDriving) {
        return true;  // Already driving or doesn't exist
    }

    // Check if converting to driving would over-constrain
    if (SketchSolver::isAvailable()) {
        // Create a temporary constraint that's driving
        SketchConstraint testConstraint = *constraint;
        testConstraint.isDriving = true;

        // Get all other constraints (excluding this one)
        QVector<SketchConstraint> otherConstraints;
        for (const SketchConstraint& c : m_constraints) {
            if (c.id != constraintId && c.isDriving) {
                otherConstraints.append(c);
            }
        }

        SketchSolver solver;
        OverConstraintInfo info = solver.checkOverConstrain(m_entities, otherConstraints, testConstraint);

        if (info.wouldOverConstrain) {
            // Build description of conflicting constraints
            QString conflictDetails;
            if (!info.conflictingConstraintIds.isEmpty()) {
                QStringList conflictDescriptions;
                for (int conflictId : info.conflictingConstraintIds) {
                    QString desc = describeConstraint(conflictId);
                    if (!desc.isEmpty()) {
                        conflictDescriptions.append("  • " + desc);
                    }
                }
                if (!conflictDescriptions.isEmpty()) {
                    conflictDetails = tr("\n\nConflicting constraints:\n") + conflictDescriptions.join("\n");
                }
            }

            QMessageBox::warning(
                this,
                tr("Cannot Convert to Driving"),
                tr("Converting this dimension to driving would over-constrain the sketch.") +
                conflictDetails +
                tr("\n\nRemove or modify the conflicting constraints first.")
            );
            return false;
        }
    }

    // Safe to convert
    constraint->isDriving = true;

    // Mark affected entities as constrained
    for (int entityId : constraint->entityIds) {
        SketchEntity* entity = entityById(entityId);
        if (entity) {
            entity->constrained = true;
        }
    }

    solveConstraints();
    emit constraintModified(constraintId);
    update();
    return true;
}

void SketchCanvas::convertToDriven(int constraintId)
{
    SketchConstraint* constraint = constraintById(constraintId);
    if (!constraint || !constraint->isDriving) {
        return;  // Already driven or doesn't exist
    }

    constraint->isDriving = false;
    constraint->satisfied = true;  // Driven dimensions are always "satisfied"

    // Update the driven dimension value to reflect current geometry
    updateDrivenDimensions();

    // Re-solve remaining driving constraints
    solveConstraints();

    emit constraintModified(constraintId);
    update();
}

// ============================================================================
// Multi-selection Operations
// ============================================================================

void SketchCanvas::deleteSelectedEntities()
{
    if (m_selectedIds.isEmpty()) return;

    QSet<int> toDelete = m_selectedIds;

    // Delete entities
    m_entities.erase(
        std::remove_if(m_entities.begin(), m_entities.end(),
                       [&toDelete](const SketchEntity& e) { return toDelete.contains(e.id); }),
        m_entities.end());

    // Remove constraints referencing deleted entities
    m_constraints.erase(
        std::remove_if(m_constraints.begin(), m_constraints.end(),
                       [&toDelete](const SketchConstraint& c) {
                           for (int id : c.entityIds) {
                               if (toDelete.contains(id)) return true;
                           }
                           return false;
                       }),
        m_constraints.end());

    // Remove from groups
    for (SketchGroup& group : m_groups) {
        for (int id : toDelete) {
            group.entityIds.removeAll(id);
        }
    }

    m_selectedId = -1;
    m_selectedIds.clear();
    m_profilesCacheDirty = true;
    emit selectionChanged(-1);
    update();
}

void SketchCanvas::transformSelectedEntities(TransformType type)
{
    if (m_selectedIds.isEmpty()) return;

    // Get the bounding box center of selected entities
    QRectF bounds;
    bool first = true;
    for (int id : m_selectedIds) {
        const SketchEntity* entity = entityById(id);
        if (!entity) continue;

        for (const QPointF& pt : entity->points) {
            if (first) {
                bounds = QRectF(pt, QSizeF(0, 0));
                first = false;
            } else {
                bounds = bounds.united(QRectF(pt, QSizeF(0, 0)));
            }
        }
        // Include circle/arc radius in bounds
        if ((entity->type == SketchEntityType::Circle || entity->type == SketchEntityType::Arc) &&
            !entity->points.isEmpty()) {
            QPointF center = entity->points[0];
            bounds = bounds.united(QRectF(center.x() - entity->radius, center.y() - entity->radius,
                                          entity->radius * 2, entity->radius * 2));
        }
    }

    QPointF center = bounds.center();
    bool ok = false;

    switch (type) {
    case TransformType::Move: {
        double dx = QInputDialog::getDouble(this, tr("Move"), tr("X offset (mm):"), 0, -10000, 10000, 2, &ok);
        if (!ok) return;
        double dy = QInputDialog::getDouble(this, tr("Move"), tr("Y offset (mm):"), 0, -10000, 10000, 2, &ok);
        if (!ok) return;

        for (int id : m_selectedIds) {
            SketchEntity* entity = entityById(id);
            if (!entity) continue;
            for (QPointF& pt : entity->points) {
                pt += QPointF(dx, dy);
            }
        }
        break;
    }
    case TransformType::Copy: {
        double dx = QInputDialog::getDouble(this, tr("Copy"), tr("X offset (mm):"), 10, -10000, 10000, 2, &ok);
        if (!ok) return;
        double dy = QInputDialog::getDouble(this, tr("Copy"), tr("Y offset (mm):"), 0, -10000, 10000, 2, &ok);
        if (!ok) return;

        QVector<int> newIds;
        for (int id : m_selectedIds) {
            const SketchEntity* entity = entityById(id);
            if (!entity) continue;

            SketchEntity copy = *entity;
            copy.id = m_nextId++;
            copy.selected = false;
            for (QPointF& pt : copy.points) {
                pt += QPointF(dx, dy);
            }
            m_entities.append(copy);
            newIds.append(copy.id);
            emit entityCreated(copy.id);
        }

        // Select the new copies
        clearSelection();
        for (int id : newIds) {
            selectEntity(id, true);
        }
        break;
    }
    case TransformType::Rotate: {
        double angle = QInputDialog::getDouble(this, tr("Rotate"), tr("Angle (degrees):"), 45, -360, 360, 1, &ok);
        if (!ok) return;

        double rad = qDegreesToRadians(angle);
        double cosA = qCos(rad);
        double sinA = qSin(rad);

        for (int id : m_selectedIds) {
            SketchEntity* entity = entityById(id);
            if (!entity) continue;
            for (QPointF& pt : entity->points) {
                QPointF rel = pt - center;
                pt = center + QPointF(rel.x() * cosA - rel.y() * sinA,
                                      rel.x() * sinA + rel.y() * cosA);
            }
            // Adjust arc angles
            if (entity->type == SketchEntityType::Arc) {
                entity->startAngle += angle;
                while (entity->startAngle >= 360) entity->startAngle -= 360;
                while (entity->startAngle < 0) entity->startAngle += 360;
            }
        }
        break;
    }
    case TransformType::Scale: {
        double scale = QInputDialog::getDouble(this, tr("Scale"), tr("Scale factor:"), 1.0, 0.01, 100, 3, &ok);
        if (!ok || qFuzzyCompare(scale, 1.0)) return;

        for (int id : m_selectedIds) {
            SketchEntity* entity = entityById(id);
            if (!entity) continue;
            for (QPointF& pt : entity->points) {
                QPointF rel = pt - center;
                pt = center + rel * scale;
            }
            entity->radius *= scale;
            entity->majorRadius *= scale;
            entity->minorRadius *= scale;
        }
        break;
    }
    case TransformType::Mirror: {
        QStringList options;
        options << tr("Horizontal (X axis)") << tr("Vertical (Y axis)");
        QString choice = QInputDialog::getItem(this, tr("Mirror"), tr("Mirror axis:"), options, 0, false, &ok);
        if (!ok) return;

        bool horizontal = (choice == options[0]);

        for (int id : m_selectedIds) {
            SketchEntity* entity = entityById(id);
            if (!entity) continue;
            for (QPointF& pt : entity->points) {
                if (horizontal) {
                    pt.setY(2 * center.y() - pt.y());
                } else {
                    pt.setX(2 * center.x() - pt.x());
                }
            }
            // Mirror arc angles
            if (entity->type == SketchEntityType::Arc) {
                if (horizontal) {
                    entity->startAngle = -entity->startAngle - entity->sweepAngle;
                } else {
                    entity->startAngle = 180 - entity->startAngle - entity->sweepAngle;
                }
                while (entity->startAngle >= 360) entity->startAngle -= 360;
                while (entity->startAngle < 0) entity->startAngle += 360;
            }
        }
        break;
    }
    }

    m_profilesCacheDirty = true;
    solveConstraints();
    update();
}

void SketchCanvas::alignSelectedEntities(AlignmentType type)
{
    if (m_selectedIds.size() < 2) return;

    // Collect bounds for each selected entity
    struct EntityBounds {
        int id;
        QRectF bounds;
        QPointF center;
    };
    QVector<EntityBounds> allBounds;

    for (int id : m_selectedIds) {
        const SketchEntity* entity = entityById(id);
        if (!entity || entity->points.isEmpty()) continue;

        QRectF bounds;
        bool first = true;
        for (const QPointF& pt : entity->points) {
            if (first) {
                bounds = QRectF(pt, QSizeF(0, 0));
                first = false;
            } else {
                bounds = bounds.united(QRectF(pt, QSizeF(0, 0)));
            }
        }
        // Include radius for circles/arcs
        if ((entity->type == SketchEntityType::Circle || entity->type == SketchEntityType::Arc) &&
            !entity->points.isEmpty()) {
            QPointF c = entity->points[0];
            bounds = bounds.united(QRectF(c.x() - entity->radius, c.y() - entity->radius,
                                          entity->radius * 2, entity->radius * 2));
        }
        allBounds.append({id, bounds, bounds.center()});
    }

    if (allBounds.size() < 2) return;

    // Calculate target values
    double targetLeft = std::numeric_limits<double>::max();
    double targetRight = std::numeric_limits<double>::lowest();
    double targetTop = std::numeric_limits<double>::lowest();
    double targetBottom = std::numeric_limits<double>::max();
    double targetHCenter = 0, targetVCenter = 0;

    for (const auto& eb : allBounds) {
        targetLeft = qMin(targetLeft, eb.bounds.left());
        targetRight = qMax(targetRight, eb.bounds.right());
        targetTop = qMax(targetTop, eb.bounds.top());
        targetBottom = qMin(targetBottom, eb.bounds.bottom());
        targetHCenter += eb.center.x();
        targetVCenter += eb.center.y();
    }
    targetHCenter /= allBounds.size();
    targetVCenter /= allBounds.size();

    // Apply alignment
    for (const auto& eb : allBounds) {
        SketchEntity* entity = entityById(eb.id);
        if (!entity) continue;

        QPointF offset(0, 0);
        switch (type) {
        case AlignmentType::Left:
            offset.setX(targetLeft - eb.bounds.left());
            break;
        case AlignmentType::Right:
            offset.setX(targetRight - eb.bounds.right());
            break;
        case AlignmentType::Top:
            offset.setY(targetTop - eb.bounds.top());
            break;
        case AlignmentType::Bottom:
            offset.setY(targetBottom - eb.bounds.bottom());
            break;
        case AlignmentType::HorizontalCenter:
            offset.setX(targetHCenter - eb.center.x());
            break;
        case AlignmentType::VerticalCenter:
            offset.setY(targetVCenter - eb.center.y());
            break;
        case AlignmentType::DistributeHorizontal:
        case AlignmentType::DistributeVertical:
            // Handled separately below
            break;
        }

        if (!offset.isNull()) {
            for (QPointF& pt : entity->points) {
                pt += offset;
            }
        }
    }

    // Handle distribution
    if (type == AlignmentType::DistributeHorizontal || type == AlignmentType::DistributeVertical) {
        // Sort by position
        std::sort(allBounds.begin(), allBounds.end(), [type](const EntityBounds& a, const EntityBounds& b) {
            if (type == AlignmentType::DistributeHorizontal) {
                return a.center.x() < b.center.x();
            } else {
                return a.center.y() < b.center.y();
            }
        });

        if (allBounds.size() >= 3) {
            // Calculate spacing
            double firstPos = (type == AlignmentType::DistributeHorizontal) ?
                              allBounds.first().center.x() : allBounds.first().center.y();
            double lastPos = (type == AlignmentType::DistributeHorizontal) ?
                             allBounds.last().center.x() : allBounds.last().center.y();
            double spacing = (lastPos - firstPos) / (allBounds.size() - 1);

            for (int i = 1; i < allBounds.size() - 1; ++i) {
                SketchEntity* entity = entityById(allBounds[i].id);
                if (!entity) continue;

                double targetPos = firstPos + i * spacing;
                double currentPos = (type == AlignmentType::DistributeHorizontal) ?
                                    allBounds[i].center.x() : allBounds[i].center.y();
                double delta = targetPos - currentPos;

                for (QPointF& pt : entity->points) {
                    if (type == AlignmentType::DistributeHorizontal) {
                        pt.setX(pt.x() + delta);
                    } else {
                        pt.setY(pt.y() + delta);
                    }
                }
            }
        }
    }

    m_profilesCacheDirty = true;
    solveConstraints();
    update();
}

int SketchCanvas::groupSelectedEntities()
{
    if (m_selectedIds.size() < 2) return -1;

    SketchGroup group;
    group.id = m_nextGroupId++;
    group.name = tr("Group %1").arg(group.id);
    group.entityIds = m_selectedIds.values().toVector();

    m_groups.append(group);
    update();
    return group.id;
}

void SketchCanvas::ungroupEntities(int groupId)
{
    m_groups.erase(
        std::remove_if(m_groups.begin(), m_groups.end(),
                       [groupId](const SketchGroup& g) { return g.id == groupId; }),
        m_groups.end());
    update();
}

void SketchCanvas::splitSelectedAtIntersections()
{
    if (m_selectedIds.isEmpty()) return;

    // Find all intersections among selected entities
    QVector<Intersection> allIntersections = findAllIntersections();

    // Filter to only intersections between selected entities
    QVector<Intersection> selectedIntersections;
    for (const Intersection& inter : allIntersections) {
        if (m_selectedIds.contains(inter.entityId1) && m_selectedIds.contains(inter.entityId2)) {
            selectedIntersections.append(inter);
        }
    }

    if (selectedIntersections.isEmpty()) {
        QMessageBox::information(this, tr("Split"), tr("No intersections found between selected entities."));
        return;
    }

    // Split each entity at its intersections
    QSet<int> processedIds;
    QVector<int> newEntityIds;

    for (int id : m_selectedIds) {
        if (processedIds.contains(id)) continue;

        QVector<int> newIds = splitEntityAtIntersections(id);
        if (!newIds.isEmpty()) {
            processedIds.insert(id);
            newEntityIds.append(newIds);
        }
    }

    // Select the new entities
    clearSelection();
    for (int id : newEntityIds) {
        selectEntity(id, true);
    }
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

bool SketchCanvas::entityIntersectsRect(const SketchEntity& entity, const QRectF& rect) const
{
    // Check if entity intersects or is contained by the rectangle (crossing mode)
    switch (entity.type) {
    case SketchEntityType::Point:
        if (!entity.points.isEmpty()) {
            return rect.contains(entity.points[0]);
        }
        break;

    case SketchEntityType::Line:
        if (entity.points.size() >= 2) {
            // Check if either endpoint is in rect
            if (rect.contains(entity.points[0]) || rect.contains(entity.points[1])) {
                return true;
            }
            // Check if line crosses any edge of rect
            QLineF line(entity.points[0], entity.points[1]);
            QLineF edges[] = {
                {rect.topLeft(), rect.topRight()},
                {rect.topRight(), rect.bottomRight()},
                {rect.bottomRight(), rect.bottomLeft()},
                {rect.bottomLeft(), rect.topLeft()}
            };
            for (const auto& edge : edges) {
                QPointF intersection;
                if (line.intersects(edge, &intersection) == QLineF::BoundedIntersection) {
                    return true;
                }
            }
        }
        break;

    case SketchEntityType::Rectangle:
        if (entity.points.size() >= 2) {
            QRectF entityRect(entity.points[0], entity.points[1]);
            entityRect = entityRect.normalized();
            return rect.intersects(entityRect);
        }
        break;

    case SketchEntityType::Circle:
        if (!entity.points.isEmpty()) {
            // Check if circle intersects rectangle
            QPointF center = entity.points[0];
            double r = entity.radius;
            // Expand rect by radius and check if center is inside
            QRectF expanded = rect.adjusted(-r, -r, r, r);
            if (!expanded.contains(center)) return false;
            // More precise check: find closest point on rect to center
            double closestX = qBound(rect.left(), center.x(), rect.right());
            double closestY = qBound(rect.top(), center.y(), rect.bottom());
            double dist = QLineF(center, QPointF(closestX, closestY)).length();
            return dist <= r || rect.contains(center);
        }
        break;

    case SketchEntityType::Arc:
        if (!entity.points.isEmpty()) {
            // Simplified: check if arc center or endpoints are in rect
            QPointF center = entity.points[0];
            double r = entity.radius;
            double startRad = qDegreesToRadians(entity.startAngle);
            double endRad = qDegreesToRadians(entity.startAngle + entity.sweepAngle);
            QPointF startPt = center + QPointF(r * qCos(startRad), r * qSin(startRad));
            QPointF endPt = center + QPointF(r * qCos(endRad), r * qSin(endRad));
            if (rect.contains(startPt) || rect.contains(endPt)) return true;
            // Also check midpoint
            double midRad = qDegreesToRadians(entity.startAngle + entity.sweepAngle / 2);
            QPointF midPt = center + QPointF(r * qCos(midRad), r * qSin(midRad));
            return rect.contains(midPt);
        }
        break;

    case SketchEntityType::Spline:
        // Check if any control point is in rect
        for (const QPointF& pt : entity.points) {
            if (rect.contains(pt)) return true;
        }
        break;

    default:
        // For other types, check all points
        for (const QPointF& pt : entity.points) {
            if (rect.contains(pt)) return true;
        }
        break;
    }
    return false;
}

bool SketchCanvas::entityEnclosedByRect(const SketchEntity& entity, const QRectF& rect) const
{
    // Check if entity is fully enclosed by the rectangle (window mode)
    switch (entity.type) {
    case SketchEntityType::Point:
        if (!entity.points.isEmpty()) {
            return rect.contains(entity.points[0]);
        }
        break;

    case SketchEntityType::Line:
        if (entity.points.size() >= 2) {
            return rect.contains(entity.points[0]) && rect.contains(entity.points[1]);
        }
        break;

    case SketchEntityType::Rectangle:
        if (entity.points.size() >= 2) {
            return rect.contains(entity.points[0]) && rect.contains(entity.points[1]);
        }
        break;

    case SketchEntityType::Circle:
        if (!entity.points.isEmpty()) {
            // Circle is enclosed if its bounding box is enclosed
            QPointF center = entity.points[0];
            double r = entity.radius;
            return rect.contains(QRectF(center.x() - r, center.y() - r, r * 2, r * 2));
        }
        break;

    case SketchEntityType::Arc:
        if (!entity.points.isEmpty()) {
            // Simplified: check endpoints and a few sample points
            QPointF center = entity.points[0];
            double r = entity.radius;
            double startRad = qDegreesToRadians(entity.startAngle);
            double endRad = qDegreesToRadians(entity.startAngle + entity.sweepAngle);
            QPointF startPt = center + QPointF(r * qCos(startRad), r * qSin(startRad));
            QPointF endPt = center + QPointF(r * qCos(endRad), r * qSin(endRad));
            if (!rect.contains(startPt) || !rect.contains(endPt)) return false;
            // Check midpoint too
            double midRad = qDegreesToRadians(entity.startAngle + entity.sweepAngle / 2);
            QPointF midPt = center + QPointF(r * qCos(midRad), r * qSin(midRad));
            return rect.contains(midPt);
        }
        break;

    case SketchEntityType::Spline:
    default:
        // All control points must be enclosed
        for (const QPointF& pt : entity.points) {
            if (!rect.contains(pt)) return false;
        }
        return !entity.points.isEmpty();
    }
    return false;
}

QVector<QPointF> SketchCanvas::getEntityEndpointsVec(const SketchEntity& entity) const
{
    QVector<QPointF> endpoints;

    switch (entity.type) {
    case SketchEntityType::Point:
        if (!entity.points.isEmpty()) {
            endpoints.append(entity.points[0]);
        }
        break;

    case SketchEntityType::Line:
        if (entity.points.size() >= 2) {
            endpoints.append(entity.points[0]);
            endpoints.append(entity.points[1]);
        }
        break;

    case SketchEntityType::Arc:
        if (!entity.points.isEmpty()) {
            QPointF center = entity.points[0];
            double r = entity.radius;
            double startRad = qDegreesToRadians(entity.startAngle);
            double endRad = qDegreesToRadians(entity.startAngle + entity.sweepAngle);
            endpoints.append(center + QPointF(r * qCos(startRad), r * qSin(startRad)));
            endpoints.append(center + QPointF(r * qCos(endRad), r * qSin(endRad)));
        }
        break;

    case SketchEntityType::Spline:
        if (entity.points.size() >= 2) {
            endpoints.append(entity.points.first());
            endpoints.append(entity.points.last());
        }
        break;

    case SketchEntityType::Rectangle:
        // Rectangle corners - all four as potential connection points
        if (entity.points.size() >= 2) {
            QRectF rect(entity.points[0], entity.points[1]);
            rect = rect.normalized();
            endpoints.append(rect.topLeft());
            endpoints.append(rect.topRight());
            endpoints.append(rect.bottomRight());
            endpoints.append(rect.bottomLeft());
        }
        break;

    case SketchEntityType::Circle:
        // Circles don't have endpoints (closed curve)
        break;

    default:
        break;
    }

    return endpoints;
}

int SketchCanvas::hitTestHandle(const QPointF& worldPos) const
{
    // Only test handles on selected entity
    const SketchEntity* sel = selectedEntity();
    if (!sel) return -1;

    const double tolerance = 6.0 / m_zoom;  // 6 pixels in world units

    for (int i = 0; i < sel->points.size(); ++i) {
        if (QLineF(sel->points[i], worldPos).length() < tolerance) {
            return i;
        }
    }

    return -1;
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
    case SketchTool::Polygon:
        m_pendingEntity.type = SketchEntityType::Polygon;
        m_pendingEntity.sides = 6;  // Default hexagon
        break;
    case SketchTool::Slot:
        m_pendingEntity.type = SketchEntityType::Slot;
        break;
    case SketchTool::Ellipse:
        m_pendingEntity.type = SketchEntityType::Ellipse;
        break;
    case SketchTool::Spline:
        m_pendingEntity.type = SketchEntityType::Spline;
        // Spline uses multi-click mode (keep adding points until finished)
        break;
    case SketchTool::Text:
        m_pendingEntity.type = SketchEntityType::Text;
        // Prompt for text immediately
        {
            bool ok = false;
            QString text = QInputDialog::getText(this, tr("Sketch Text"),
                                                  tr("Enter text:"), QLineEdit::Normal,
                                                  QString(), &ok);
            if (ok && !text.isEmpty()) {
                m_pendingEntity.text = text;
                finishEntity();  // Text is instant once entered
            } else {
                m_isDrawing = false;  // Canceled
            }
        }
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
    case SketchTool::Polygon:  // Polygon uses radius like circle
        if (!m_pendingEntity.points.isEmpty()) {
            m_pendingEntity.radius = QLineF(m_pendingEntity.points[0], pos).length();
        }
        break;

    case SketchTool::Arc:
        if (m_arcMode == ArcMode::Tangent) {
            // Tangent arc: update end point
            if (m_pendingEntity.points.size() > 1) {
                m_pendingEntity.points[1] = pos;
            } else {
                m_pendingEntity.points.append(pos);
            }
        } else if (m_arcMode == ArcMode::ThreePoint) {
            // 3-point arc: update based on how many points we have
            if (m_pendingEntity.points.size() == 1) {
                // First point placed, second point follows mouse
                if (m_pendingEntity.points.size() > 1) {
                    m_pendingEntity.points[1] = pos;
                } else {
                    m_pendingEntity.points.append(pos);
                }
            } else if (m_pendingEntity.points.size() == 2) {
                // Two points placed, third point follows mouse
                if (m_pendingEntity.points.size() > 2) {
                    m_pendingEntity.points[2] = pos;
                } else {
                    m_pendingEntity.points.append(pos);
                }
            }
        }
        break;

    case SketchTool::Slot:  // Slot uses two endpoints
        if (m_pendingEntity.points.size() > 1) {
            m_pendingEntity.points[1] = pos;
        } else {
            m_pendingEntity.points.append(pos);
        }
        break;

    case SketchTool::Ellipse:  // Ellipse: center to edge defines major axis, then minor
        if (m_pendingEntity.points.size() > 1) {
            m_pendingEntity.points[1] = pos;
        } else {
            m_pendingEntity.points.append(pos);
        }
        break;

    case SketchTool::Spline:  // Spline: preview next point
        // Don't add the point yet, just show preview
        // Points are added on mouse release
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
        // Handle tangent circles
        if (m_circleMode == CircleMode::TwoTangent && m_tangentTargets.size() >= 2) {
            const SketchEntity* e1 = nullptr;
            const SketchEntity* e2 = nullptr;
            for (const auto& e : m_entities) {
                if (e.id == m_tangentTargets[0]) e1 = &e;
                if (e.id == m_tangentTargets[1]) e2 = &e;
            }
            if (e1 && e2 && !m_pendingEntity.points.isEmpty()) {
                TangentCircle tc = calculate2TangentCircle(*e1, *e2, m_pendingEntity.points[0]);
                if (tc.valid) {
                    m_pendingEntity.points.clear();
                    m_pendingEntity.points.append(tc.center);
                    m_pendingEntity.points.append(QPointF(tc.center.x() + tc.radius, tc.center.y()));
                    m_pendingEntity.radius = tc.radius;
                    valid = true;
                }
            }
            m_tangentTargets.clear();
        } else if (m_circleMode == CircleMode::ThreeTangent && m_tangentTargets.size() >= 3) {
            const SketchEntity* e1 = nullptr;
            const SketchEntity* e2 = nullptr;
            const SketchEntity* e3 = nullptr;
            for (const auto& e : m_entities) {
                if (e.id == m_tangentTargets[0]) e1 = &e;
                if (e.id == m_tangentTargets[1]) e2 = &e;
                if (e.id == m_tangentTargets[2]) e3 = &e;
            }
            if (e1 && e2 && e3) {
                TangentCircle tc = calculate3TangentCircle(*e1, *e2, *e3);
                if (tc.valid) {
                    m_pendingEntity.points.clear();
                    m_pendingEntity.points.append(tc.center);
                    m_pendingEntity.points.append(QPointF(tc.center.x() + tc.radius, tc.center.y()));
                    m_pendingEntity.radius = tc.radius;
                    valid = true;
                }
            }
            m_tangentTargets.clear();
        } else {
            // Standard center-radius circle
            valid = m_pendingEntity.radius > 0.1;
            if (valid && m_pendingEntity.points.size() == 1) {
                QPointF center = m_pendingEntity.points[0];
                m_pendingEntity.points.append(QPointF(center.x() + m_pendingEntity.radius, center.y()));
            }
        }
        break;
    case SketchEntityType::Arc:
        // Handle tangent arc
        if (m_arcMode == ArcMode::Tangent && !m_tangentTargets.isEmpty() && m_pendingEntity.points.size() >= 2) {
            const SketchEntity* tangentEntity = nullptr;
            for (const auto& e : m_entities) {
                if (e.id == m_tangentTargets[0]) {
                    tangentEntity = &e;
                    break;
                }
            }
            if (tangentEntity) {
                QPointF tangentPoint = m_pendingEntity.points[0];
                QPointF endPoint = m_pendingEntity.points[1];
                TangentArc ta = calculateTangentArc(*tangentEntity, tangentPoint, endPoint);
                if (ta.valid) {
                    m_pendingEntity.points.clear();
                    m_pendingEntity.points.append(ta.center);
                    m_pendingEntity.points.append(QPointF(ta.center.x() + ta.radius, ta.center.y()));
                    m_pendingEntity.radius = ta.radius;
                    m_pendingEntity.startAngle = ta.startAngle;
                    m_pendingEntity.sweepAngle = ta.sweepAngle;
                    valid = true;
                }
            }
            m_tangentTargets.clear();
        } else if (m_arcMode == ArcMode::ThreePoint && m_pendingEntity.points.size() >= 3) {
            // Calculate arc from 3 points
            QPointF p1 = m_pendingEntity.points[0];  // start
            QPointF p2 = m_pendingEntity.points[1];  // end
            QPointF p3 = m_pendingEntity.points[2];  // point on arc

            // Calculate center using perpendicular bisectors
            QPointF mid1 = (p1 + p3) / 2.0;
            QPointF mid2 = (p2 + p3) / 2.0;

            QLineF line1(p1, p3);
            QLineF line2(p2, p3);

            // Perpendicular lines
            QLineF perp1 = line1.normalVector();
            QLineF perp2 = line2.normalVector();
            perp1.translate(mid1 - perp1.p1());
            perp2.translate(mid2 - perp2.p1());

            // Find intersection (center)
            QPointF center;
            QLineF::IntersectionType itype = perp1.intersects(perp2, &center);

            if (itype == QLineF::BoundedIntersection || itype == QLineF::UnboundedIntersection) {
                double radius = QLineF(center, p1).length();

                // Calculate angles
                double angle1 = qAtan2(p1.y() - center.y(), p1.x() - center.x()) * 180.0 / M_PI;
                double angle2 = qAtan2(p2.y() - center.y(), p2.x() - center.x()) * 180.0 / M_PI;
                double angle3 = qAtan2(p3.y() - center.y(), p3.x() - center.x()) * 180.0 / M_PI;

                // Normalize angles to 0-360
                if (angle1 < 0) angle1 += 360;
                if (angle2 < 0) angle2 += 360;
                if (angle3 < 0) angle3 += 360;

                // Determine sweep direction (does p3 lie between p1 and p2 going CCW?)
                double sweep = angle2 - angle1;
                if (sweep < 0) sweep += 360;

                double check = angle3 - angle1;
                if (check < 0) check += 360;

                // If p3 is not between p1 and p2 in CCW direction, go the other way
                if (check > sweep) {
                    sweep = sweep - 360;
                }

                // Store final arc parameters
                m_pendingEntity.points.clear();
                m_pendingEntity.points.append(center);
                m_pendingEntity.points.append(QPointF(center.x() + radius, center.y()));  // radius handle
                m_pendingEntity.radius = radius;
                m_pendingEntity.startAngle = angle1;
                m_pendingEntity.sweepAngle = sweep;
                valid = radius > 0.1;
            }
        } else {
            // Center-point arc (original behavior)
            valid = m_pendingEntity.radius > 0.1;
            // Add a radius handle point (to the right of center)
            if (valid && m_pendingEntity.points.size() == 1) {
                QPointF center = m_pendingEntity.points[0];
                m_pendingEntity.points.append(QPointF(center.x() + m_pendingEntity.radius, center.y()));
            }
        }
        break;
    case SketchEntityType::Polygon:
        valid = m_pendingEntity.radius > 0.1;
        // Add a radius handle point (to the right of center)
        if (valid && m_pendingEntity.points.size() == 1) {
            QPointF center = m_pendingEntity.points[0];
            m_pendingEntity.points.append(QPointF(center.x() + m_pendingEntity.radius, center.y()));
        }
        break;
    case SketchEntityType::Slot:
        valid = m_pendingEntity.points.size() >= 2 &&
                QLineF(m_pendingEntity.points[0], m_pendingEntity.points[1]).length() > 0.1;
        m_pendingEntity.radius = 5.0;  // Default slot width (half-width)
        break;
    case SketchEntityType::Ellipse:
        valid = m_pendingEntity.points.size() >= 2;
        if (valid) {
            // Calculate major and minor radii from the two points
            QPointF center = m_pendingEntity.points[0];
            QPointF majorPoint = m_pendingEntity.points[1];
            m_pendingEntity.majorRadius = QLineF(center, majorPoint).length();
            m_pendingEntity.minorRadius = m_pendingEntity.majorRadius * 0.5;  // Default 2:1 ratio
        }
        break;
    case SketchEntityType::Spline:
        valid = m_pendingEntity.points.size() >= 2;  // Need at least 2 points
        break;
    case SketchEntityType::Text:
        valid = !m_pendingEntity.points.isEmpty() && !m_pendingEntity.text.isEmpty();
        break;
    default:
        break;
    }

    if (valid) {
        m_entities.append(m_pendingEntity);
        m_profilesCacheDirty = true;
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

// ---- Key Bindings ---------------------------------------------------

void SketchCanvas::loadKeyBindings()
{
    m_keyBindings.clear();

    auto bindings = BindingsDialog::loadBindings();

    // Helper to extract keyboard shortcuts from an action binding
    auto extractKeyboardBindings = [](const ActionBinding& ab) {
        QList<QKeySequence> shortcuts;

        auto addIfKeyboard = [&shortcuts](const QString& binding) {
            if (binding.isEmpty()) return;
            // Skip mouse bindings
            if (binding.contains(QStringLiteral("Button"), Qt::CaseInsensitive) ||
                binding.contains(QStringLiteral("Wheel"), Qt::CaseInsensitive) ||
                binding.contains(QStringLiteral("Drag"), Qt::CaseInsensitive) ||
                binding.contains(QStringLiteral("Click"), Qt::CaseInsensitive)) {
                return;
            }
            QKeySequence seq(binding);
            if (!seq.isEmpty()) {
                shortcuts.append(seq);
            }
        };

        addIfKeyboard(ab.binding1);
        addIfKeyboard(ab.binding2);
        addIfKeyboard(ab.binding3);

        return shortcuts;
    };

    // Load sketch-specific bindings
    for (auto it = bindings.constBegin(); it != bindings.constEnd(); ++it) {
        if (it.key().startsWith(QStringLiteral("sketch."))) {
            m_keyBindings.insert(it.key(), extractKeyboardBindings(it.value()));
        }
    }
}

void SketchCanvas::reloadBindings()
{
    loadKeyBindings();
}

void SketchCanvas::setEntityConstruction(int entityId, bool isConstruction)
{
    SketchEntity* entity = entityById(entityId);
    if (entity) {
        entity->isConstruction = isConstruction;
        m_profilesCacheDirty = true;  // Construction status affects profile detection
        emit entityModified(entityId);
        update();
    }
}

bool SketchCanvas::matchesBinding(const QString& actionId, QKeyEvent* event) const
{
    if (!m_keyBindings.contains(actionId)) return false;

    // Build QKeySequence from the current key event
    int key = event->key();
    Qt::KeyboardModifiers mods = event->modifiers();

    // Ignore standalone modifier keys
    if (key == Qt::Key_Shift || key == Qt::Key_Control ||
        key == Qt::Key_Alt || key == Qt::Key_Meta) {
        return false;
    }

    int combined = key;
    if (mods & Qt::ControlModifier) combined |= Qt::CTRL;
    if (mods & Qt::ShiftModifier) combined |= Qt::SHIFT;
    if (mods & Qt::AltModifier) combined |= Qt::ALT;
    if (mods & Qt::MetaModifier) combined |= Qt::META;

    QKeySequence eventSeq(combined);

    const QList<QKeySequence>& bindings = m_keyBindings.value(actionId);
    for (const QKeySequence& seq : bindings) {
        if (seq == eventSeq) {
            return true;
        }
    }

    return false;
}

// ---- Tangent Circle Calculations -----------------------------------

SketchCanvas::TangentCircle SketchCanvas::calculate2TangentCircle(
    const SketchEntity& e1, const SketchEntity& e2, const QPointF& hint) const
{
    TangentCircle result;
    result.valid = false;

    // Handle line-line tangency using library function
    if (e1.type == SketchEntityType::Line && e2.type == SketchEntityType::Line &&
        e1.points.size() >= 2 && e2.points.size() >= 2) {

        // Calculate distance from hint to intersection for radius estimation
        auto lineIntersect = geometry::infiniteLineIntersection(
            e1.points[0], e1.points[1], e2.points[0], e2.points[1]);
        if (!lineIntersect.intersects) return result;

        double radius = geometry::length(hint - lineIntersect.point);

        auto libResult = geometry::circleTangentToTwoLines(
            e1.points[0], e1.points[1],
            e2.points[0], e2.points[1],
            radius, hint);

        result.valid = libResult.valid;
        result.center = libResult.center;
        result.radius = libResult.radius;
    }

    return result;
}

SketchCanvas::TangentCircle SketchCanvas::calculate3TangentCircle(
    const SketchEntity& e1, const SketchEntity& e2, const SketchEntity& e3) const
{
    TangentCircle result;
    result.valid = false;

    // Handle 3 lines using library function (incircle)
    if (e1.type == SketchEntityType::Line && e2.type == SketchEntityType::Line &&
        e3.type == SketchEntityType::Line && e1.points.size() >= 2 &&
        e2.points.size() >= 2 && e3.points.size() >= 2) {

        auto libResult = geometry::circleTangentToThreeLines(
            e1.points[0], e1.points[1],
            e2.points[0], e2.points[1],
            e3.points[0], e3.points[1]);

        result.valid = libResult.valid;
        result.center = libResult.center;
        result.radius = libResult.radius;
    }

    return result;
}

SketchCanvas::TangentArc SketchCanvas::calculateTangentArc(
    const SketchEntity& tangentEntity, const QPointF& tangentPoint, const QPointF& endPoint) const
{
    TangentArc result;
    result.valid = false;

    // Handle line tangency using library function
    if (tangentEntity.type == SketchEntityType::Line && tangentEntity.points.size() >= 2) {
        auto libResult = geometry::arcTangentToLine(
            tangentEntity.points[0], tangentEntity.points[1],
            tangentPoint, endPoint);

        result.valid = libResult.valid;
        result.center = libResult.center;
        result.radius = libResult.radius;
        result.startAngle = libResult.startAngle;
        result.sweepAngle = libResult.sweepAngle;
    }

    return result;
}

// ---- Constraint Helper Functions ----

void SketchCanvas::finishConstraintCreation()
{
    m_isCreatingConstraint = false;
    m_constraintTargetEntities.clear();
    m_constraintTargetPoints.clear();
    update();
}

void SketchCanvas::createConstraint(ConstraintType type, double value, const QPointF& labelPos)
{
    SketchConstraint constraint;
    constraint.id = m_nextConstraintId++;
    constraint.type = type;
    constraint.entityIds = m_constraintTargetEntities;
    constraint.value = value;
    constraint.isDriving = true;
    constraint.labelPosition = labelPos;
    constraint.enabled = true;
    constraint.satisfied = true;

    // Determine which points on entities are constrained
    for (int i = 0; i < m_constraintTargetEntities.size(); ++i) {
        SketchEntity* entity = entityById(m_constraintTargetEntities[i]);
        if (entity && i < m_constraintTargetPoints.size()) {
            int pointIndex = findNearestPointIndex(entity, m_constraintTargetPoints[i]);
            constraint.pointIndices.append(pointIndex);
        }
    }

    // Check if this constraint would over-constrain the sketch
    if (SketchSolver::isAvailable()) {
        SketchSolver solver;
        OverConstraintInfo overConstraintInfo = solver.checkOverConstrain(m_entities, m_constraints, constraint);

        if (overConstraintInfo.wouldOverConstrain) {
            // Build description of conflicting constraints
            QString conflictDetails;
            if (!overConstraintInfo.conflictingConstraintIds.isEmpty()) {
                QStringList conflictDescriptions;
                for (int conflictId : overConstraintInfo.conflictingConstraintIds) {
                    QString desc = describeConstraint(conflictId);
                    if (!desc.isEmpty()) {
                        conflictDescriptions.append("  • " + desc);
                    }
                }
                if (!conflictDescriptions.isEmpty()) {
                    conflictDetails = tr("\n\nConflicting constraints:\n") + conflictDescriptions.join("\n");
                }
            }

            // Offer to create a Driven dimension instead
            QMessageBox::StandardButton reply = QMessageBox::question(
                this,
                tr("Over-Constrained"),
                tr("This dimension would over-constrain the sketch.") +
                conflictDetails +
                tr("\n\nCreate a Driven (reference) dimension instead?"),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::Yes
            );

            if (reply == QMessageBox::Yes) {
                // Create as Driven dimension (non-driving, reference only)
                constraint.isDriving = false;
            } else {
                // User chose not to add the constraint
                return;
            }
        }
    }

    m_constraints.append(constraint);

    // Mark affected entities as constrained (only for driving constraints)
    if (constraint.isDriving) {
        for (int entityId : constraint.entityIds) {
            SketchEntity* entity = entityById(entityId);
            if (entity) {
                entity->constrained = true;
            }
        }
    }

    // Solve constraints to update geometry (only if driving)
    if (constraint.isDriving) {
        solveConstraints();
    }

    emit constraintCreated(constraint.id);
    update();
}

ConstraintType SketchCanvas::detectConstraintType(int entityId1, int entityId2) const
{
    const SketchEntity* e1 = entityById(entityId1);
    const SketchEntity* e2 = entityById(entityId2);

    if (!e1 || !e2) return ConstraintType::Distance;

    // Point to point → Distance
    if (e1->type == SketchEntityType::Point && e2->type == SketchEntityType::Point) {
        return ConstraintType::Distance;
    }

    // Point to line → Distance
    if ((e1->type == SketchEntityType::Point && e2->type == SketchEntityType::Line) ||
        (e1->type == SketchEntityType::Line && e2->type == SketchEntityType::Point)) {
        return ConstraintType::Distance;
    }

    // Line to line → Angle
    if (e1->type == SketchEntityType::Line && e2->type == SketchEntityType::Line) {
        return ConstraintType::Angle;
    }

    // Circle or Arc alone → Radius
    if (e1->type == SketchEntityType::Circle || e1->type == SketchEntityType::Arc ||
        e2->type == SketchEntityType::Circle || e2->type == SketchEntityType::Arc) {
        return ConstraintType::Radius;
    }

    return ConstraintType::Distance;
}

double SketchCanvas::calculateConstraintValue(ConstraintType type, const QVector<int>& entityIds,
                                               const QVector<QPointF>& points) const
{
    switch (type) {
    case ConstraintType::Distance:
        if (points.size() >= 2) {
            return QLineF(points[0], points[1]).length();
        }
        break;

    case ConstraintType::Radius:
        if (!entityIds.isEmpty()) {
            const SketchEntity* e = entityById(entityIds[0]);
            if (e && (e->type == SketchEntityType::Circle || e->type == SketchEntityType::Arc)) {
                return e->radius;
            }
        }
        break;

    case ConstraintType::Diameter:
        if (!entityIds.isEmpty()) {
            const SketchEntity* e = entityById(entityIds[0]);
            if (e && (e->type == SketchEntityType::Circle || e->type == SketchEntityType::Arc)) {
                return e->radius * 2.0;
            }
        }
        break;

    case ConstraintType::Angle:
        if (entityIds.size() >= 2) {
            const SketchEntity* e1 = entityById(entityIds[0]);
            const SketchEntity* e2 = entityById(entityIds[1]);
            if (e1 && e2 && e1->type == SketchEntityType::Line && e2->type == SketchEntityType::Line) {
                if (e1->points.size() >= 2 && e2->points.size() >= 2) {
                    QLineF line1(e1->points[0], e1->points[1]);
                    QLineF line2(e2->points[0], e2->points[1]);
                    double angle = line1.angleTo(line2);
                    // Return smaller angle (0-180)
                    if (angle > 180) angle = 360 - angle;
                    return angle;
                }
            }
        }
        break;
    }

    return 0.0;
}

QPointF SketchCanvas::findClosestPointOnEntity(const SketchEntity* entity, const QPointF& worldPos) const
{
    if (!entity || entity->points.isEmpty()) {
        return worldPos;
    }

    // Use library's closestPoint implementation
    sketch::Entity libEntity = hobbycad::toLibraryEntity(*entity);
    return libEntity.closestPoint(worldPos);
}

int SketchCanvas::findNearestPointIndex(const SketchEntity* entity, const QPointF& worldPos) const
{
    if (!entity || entity->points.isEmpty()) {
        return 0;
    }

    int nearestIndex = 0;
    double minDist = QLineF(entity->points[0], worldPos).length();

    for (int i = 1; i < entity->points.size(); ++i) {
        double dist = QLineF(entity->points[i], worldPos).length();
        if (dist < minDist) {
            minDist = dist;
            nearestIndex = i;
        }
    }

    return nearestIndex;
}

int SketchCanvas::hitTestConstraintLabel(const QPointF& worldPos) const
{
    const double tolerance = 10.0 / m_zoom;  // 10 pixels in world units

    for (const SketchConstraint& c : m_constraints) {
        if (!c.enabled || !c.labelVisible) continue;

        double dist = QLineF(c.labelPosition, worldPos).length();
        if (dist < tolerance) {
            return c.id;
        }
    }

    return -1;
}

void SketchCanvas::editConstraintValue(int constraintId)
{
    SketchConstraint* constraint = constraintById(constraintId);
    if (!constraint || !constraint->isDriving) return;

    QString title, label;
    switch (constraint->type) {
    case ConstraintType::Distance:
        title = tr("Edit Distance");
        label = tr("Distance (mm):");
        break;
    case ConstraintType::Radius:
        title = tr("Edit Radius");
        label = tr("Radius (mm):");
        break;
    case ConstraintType::Diameter:
        title = tr("Edit Diameter");
        label = tr("Diameter (mm):");
        break;
    case ConstraintType::Angle:
        title = tr("Edit Angle");
        label = tr("Angle (degrees):");
        break;
    default:
        return;
    }

    bool ok = false;
    double newValue = QInputDialog::getDouble(
        this, title, label,
        constraint->value,  // current value
        0.0, 1000000.0, 2, &ok
    );

    if (ok && !qFuzzyCompare(newValue, constraint->value)) {
        constraint->value = newValue;
        // Phase 2: Call solver to update geometry
        solveConstraints();
        emit constraintModified(constraintId);
        update();
    }
}

void SketchCanvas::solveConstraints()
{
    if (m_constraints.isEmpty()) return;

    if (!SketchSolver::isAvailable()) {
        // Show one-time warning that solver is not available
        static bool warningShown = false;
        if (!warningShown) {
            QMessageBox::information(this, tr("Solver Unavailable"),
                tr("Constraint solving is not available (libslvs not compiled).\n\n"
                   "Dimensions will be displayed as reference values only."));
            warningShown = true;
        }
        return;
    }

    SketchSolver solver;
    SolveResult result = solver.solve(m_entities, m_constraints);

    if (result.success) {
        // Mark all driving constraints as satisfied
        for (SketchConstraint& c : m_constraints) {
            if (c.isDriving) {
                c.satisfied = true;
            }
        }

        // Update Driven dimension values to reflect actual geometry
        updateDrivenDimensions();

        update();
    } else {
        // Mark failed constraints (red)
        for (SketchConstraint& c : m_constraints) {
            c.satisfied = !result.failedConstraintIds.contains(c.id);
        }

        // Show error to user
        QString msg = tr("Constraint solving failed: %1\n\nDegrees of freedom: %2")
                      .arg(result.errorMessage)
                      .arg(result.dof);
        QMessageBox::warning(this, tr("Constraint Error"), msg);

        update();
    }
}

void SketchCanvas::updateDrivenDimensions()
{
    for (SketchConstraint& c : m_constraints) {
        if (c.isDriving) continue;  // Skip driving constraints

        switch (c.type) {
        case ConstraintType::Distance: {
            QPointF p1, p2;
            if (getConstraintEndpoints(c, p1, p2)) {
                c.value = QLineF(p1, p2).length();
            }
            break;
        }
        case ConstraintType::Radius: {
            if (!c.entityIds.isEmpty()) {
                const SketchEntity* entity = entityById(c.entityIds[0]);
                if (entity && (entity->type == SketchEntityType::Circle ||
                               entity->type == SketchEntityType::Arc)) {
                    c.value = entity->radius;
                }
            }
            break;
        }
        case ConstraintType::Diameter: {
            if (!c.entityIds.isEmpty()) {
                const SketchEntity* entity = entityById(c.entityIds[0]);
                if (entity && (entity->type == SketchEntityType::Circle ||
                               entity->type == SketchEntityType::Arc)) {
                    c.value = entity->radius * 2.0;
                }
            }
            break;
        }
        case ConstraintType::Angle: {
            if (c.entityIds.size() >= 2) {
                const SketchEntity* e1 = entityById(c.entityIds[0]);
                const SketchEntity* e2 = entityById(c.entityIds[1]);
                if (e1 && e2 && e1->type == SketchEntityType::Line &&
                    e2->type == SketchEntityType::Line &&
                    e1->points.size() >= 2 && e2->points.size() >= 2) {
                    QLineF line1(e1->points[0], e1->points[1]);
                    QLineF line2(e2->points[0], e2->points[1]);
                    c.value = qAbs(line1.angleTo(line2));
                    if (c.value > 180) c.value = 360 - c.value;
                }
            }
            break;
        }
        default:
            // Geometric constraints don't have values to update
            break;
        }

        // Driven dimensions are always satisfied (they just report values)
        c.satisfied = true;
    }
}

bool SketchCanvas::getConstraintEndpoints(const SketchConstraint& constraint, QPointF& p1, QPointF& p2) const
{
    if (constraint.entityIds.size() < 2) return false;

    const SketchEntity* e1 = entityById(constraint.entityIds[0]);
    const SketchEntity* e2 = entityById(constraint.entityIds[1]);

    if (!e1 || !e2) return false;

    // Get points based on constraint type
    if (constraint.type == ConstraintType::Distance) {
        // For distance constraints, use the closest points or centers
        if (e1->type == SketchEntityType::Point) {
            p1 = e1->points[0];
        } else if (e1->type == SketchEntityType::Circle || e1->type == SketchEntityType::Arc) {
            p1 = e1->points[0];  // center
        } else if (!e1->points.isEmpty()) {
            int idx1 = (constraint.pointIndices.size() > 0) ? constraint.pointIndices[0] : 0;
            p1 = e1->points[qMin(idx1, e1->points.size() - 1)];
        } else {
            return false;
        }

        if (e2->type == SketchEntityType::Point) {
            p2 = e2->points[0];
        } else if (e2->type == SketchEntityType::Circle || e2->type == SketchEntityType::Arc) {
            p2 = e2->points[0];  // center
        } else if (!e2->points.isEmpty()) {
            int idx2 = (constraint.pointIndices.size() > 1) ? constraint.pointIndices[1] : 0;
            p2 = e2->points[qMin(idx2, e2->points.size() - 1)];
        } else {
            return false;
        }

        return true;
    }

    return false;
}

// ---- Geometric Constraint Application ----

void SketchCanvas::createGeometricConstraint(ConstraintType type)
{
    SketchConstraint constraint;
    constraint.id = m_nextConstraintId++;
    constraint.type = type;
    constraint.entityIds = m_constraintTargetEntities;
    constraint.value = 0.0;  // Geometric constraints don't have values
    constraint.isDriving = true;
    constraint.labelPosition = QPointF(0, 0);  // No label for geometric constraints
    constraint.labelVisible = false;  // Don't show label
    constraint.enabled = true;
    constraint.satisfied = true;

    m_constraints.append(constraint);

    // Mark affected entities as constrained
    for (int entityId : constraint.entityIds) {
        SketchEntity* entity = entityById(entityId);
        if (entity) {
            entity->constrained = true;
        }
    }

    // Solve constraints to update geometry
    solveConstraints();

    emit constraintCreated(constraint.id);
    update();
}

void SketchCanvas::applyHorizontalConstraint()
{
    if (m_selectedId < 0) return;

    SketchEntity* entity = entityById(m_selectedId);
    if (!entity || entity->type != SketchEntityType::Line) return;

    m_constraintTargetEntities.clear();
    m_constraintTargetEntities.append(m_selectedId);

    createGeometricConstraint(ConstraintType::Horizontal);

    m_constraintTargetEntities.clear();
}

void SketchCanvas::applyVerticalConstraint()
{
    if (m_selectedId < 0) return;

    SketchEntity* entity = entityById(m_selectedId);
    if (!entity || entity->type != SketchEntityType::Line) return;

    m_constraintTargetEntities.clear();
    m_constraintTargetEntities.append(m_selectedId);

    createGeometricConstraint(ConstraintType::Vertical);

    m_constraintTargetEntities.clear();
}

void SketchCanvas::applyParallelConstraint()
{
    // Need two lines selected - for now, use a simple approach
    // In the future, this could use a multi-select mode
    if (m_selectedId < 0) return;

    SketchEntity* entity = entityById(m_selectedId);
    if (!entity || entity->type != SketchEntityType::Line) return;

    // For now, show a message that this requires two lines
    // TODO: Implement proper two-entity selection workflow
    QMessageBox::information(this, tr("Parallel Constraint"),
        tr("Parallel constraint requires selecting two lines.\n\n"
           "This feature will be enhanced in a future update."));
}

void SketchCanvas::applyPerpendicularConstraint()
{
    // Need two lines selected
    if (m_selectedId < 0) return;

    SketchEntity* entity = entityById(m_selectedId);
    if (!entity || entity->type != SketchEntityType::Line) return;

    // For now, show a message that this requires two lines
    QMessageBox::information(this, tr("Perpendicular Constraint"),
        tr("Perpendicular constraint requires selecting two lines.\n\n"
           "This feature will be enhanced in a future update."));
}

void SketchCanvas::applyCoincidentConstraint()
{
    // Need two points selected
    if (m_selectedId < 0) return;

    SketchEntity* entity = entityById(m_selectedId);
    if (!entity || entity->type != SketchEntityType::Point) return;

    QMessageBox::information(this, tr("Coincident Constraint"),
        tr("Coincident constraint requires selecting two points.\n\n"
           "This feature will be enhanced in a future update."));
}

void SketchCanvas::applyTangentConstraint()
{
    // Need a line and circle/arc, or two circles/arcs
    if (m_selectedId < 0) return;

    QMessageBox::information(this, tr("Tangent Constraint"),
        tr("Tangent constraint requires selecting a line and circle/arc,\n"
           "or two circles/arcs.\n\n"
           "This feature will be enhanced in a future update."));
}

void SketchCanvas::applyEqualConstraint()
{
    // Need two entities of same type (two lines or two circles)
    if (m_selectedId < 0) return;

    QMessageBox::information(this, tr("Equal Constraint"),
        tr("Equal constraint requires selecting two entities of the same type\n"
           "(two lines for equal length, or two circles for equal radius).\n\n"
           "This feature will be enhanced in a future update."));
}

void SketchCanvas::applyMidpointConstraint()
{
    // Need a point and a line - the point will be constrained to the line's midpoint
    if (m_selectedId < 0) return;

    SketchEntity* entity = entityById(m_selectedId);
    if (!entity) return;

    // Check if selected entity is a point
    if (entity->type == SketchEntityType::Point) {
        QMessageBox::information(this, tr("Midpoint Constraint"),
            tr("Midpoint constraint requires a point and a line.\n"
               "After selecting a point, select the line whose midpoint\n"
               "the point should coincide with.\n\n"
               "This feature will be enhanced in a future update."));
        return;
    }

    // Check if selected entity is a line
    if (entity->type == SketchEntityType::Line) {
        QMessageBox::information(this, tr("Midpoint Constraint"),
            tr("Midpoint constraint requires a point and a line.\n"
               "Select a point first, then the line whose midpoint\n"
               "the point should coincide with.\n\n"
               "This feature will be enhanced in a future update."));
        return;
    }

    QMessageBox::information(this, tr("Midpoint Constraint"),
        tr("Midpoint constraint requires selecting a point and a line.\n\n"
           "This feature will be enhanced in a future update."));
}

void SketchCanvas::applySymmetricConstraint()
{
    // Need two entities and a symmetry line
    if (m_selectedId < 0) return;

    SketchEntity* entity = entityById(m_selectedId);
    if (!entity) return;

    QMessageBox::information(this, tr("Symmetric Constraint"),
        tr("Symmetric constraint requires selecting two entities\n"
           "and a line of symmetry.\n\n"
           "The two entities will be constrained to be symmetric\n"
           "about the symmetry line.\n\n"
           "This feature will be enhanced in a future update."));
}

// ============================================================================
// Trim / Extend / Split Operations
// ============================================================================

QVector<SketchCanvas::Intersection> SketchCanvas::findAllIntersections() const
{
    // Use library implementation via conversion helpers
    QVector<sketch::Entity> libEntities = hobbycad::toLibraryEntities(m_entities);
    QVector<sketch::Intersection> libIntersections = sketch::findAllIntersections(libEntities);
    return hobbycad::toGuiIntersections(libIntersections);
}

bool SketchCanvas::trimEntityAt(int entityId, const QPointF& clickPoint)
{
    SketchEntity* entity = entityById(entityId);
    if (!entity) return false;

    // Find intersections involving this entity using library
    QVector<sketch::Entity> libEntities = hobbycad::toLibraryEntities(m_entities);
    sketch::Entity libEntity = hobbycad::toLibraryEntity(*entity);

    QVector<sketch::Intersection> allIntersections = sketch::findAllIntersections(libEntities);

    // Extract intersection points for this entity
    QVector<QPointF> intersectionPoints;
    for (const sketch::Intersection& inter : allIntersections) {
        if (inter.entityId1 == entityId || inter.entityId2 == entityId) {
            intersectionPoints.append(inter.point);
        }
    }

    if (intersectionPoints.isEmpty()) return false;

    // Use library trim function
    sketch::TrimResult result = sketch::trimEntity(
        libEntity, intersectionPoints, clickPoint,
        [this]() { return m_nextId++; });

    if (!result.success) return false;

    // Remove original entity
    m_entities.erase(std::remove_if(m_entities.begin(), m_entities.end(),
                     [entityId](const SketchEntity& e) { return e.id == entityId; }),
                     m_entities.end());

    // Add new entities from trim result
    for (const sketch::Entity& newEntity : result.newEntities) {
        SketchEntity guiEntity = hobbycad::toGuiEntity(newEntity);
        m_entities.append(guiEntity);
        emit entityCreated(guiEntity.id);
    }

    m_profilesCacheDirty = true;
    update();
    return true;
}

bool SketchCanvas::extendEntityTo(int entityId, const QPointF& clickPoint)
{
    SketchEntity* entity = entityById(entityId);
    if (!entity) return false;

    if (entity->type != SketchEntityType::Line) {
        return false;  // Only lines for now
    }

    if (entity->points.size() < 2) return false;

    QLineF line(entity->points[0], entity->points[1]);

    // Determine which end to extend based on click position
    double distToStart = QLineF(clickPoint, entity->points[0]).length();
    double distToEnd = QLineF(clickPoint, entity->points[1]).length();
    bool extendStart = distToStart < distToEnd;

    // Find nearest intersection point in the extension direction
    QPointF bestIntersection;
    double bestDist = std::numeric_limits<double>::max();

    for (const SketchEntity& other : m_entities) {
        if (other.id == entityId || other.isConstruction) continue;

        if (other.type == SketchEntityType::Line && other.points.size() >= 2) {
            auto result = geometry::infiniteLineIntersection(
                entity->points[0], entity->points[1],
                other.points[0], other.points[1]);
            if (result.intersects) {
                // Check if intersection is in the extension direction
                bool validExtension = extendStart ? (result.t1 < 0) : (result.t1 > 1);
                if (validExtension && result.t2 >= 0 && result.t2 <= 1) {
                    double dist = QLineF(extendStart ? entity->points[0] : entity->points[1],
                                         result.point).length();
                    if (dist < bestDist) {
                        bestDist = dist;
                        bestIntersection = result.point;
                    }
                }
            }
        }
        else if ((other.type == SketchEntityType::Circle || other.type == SketchEntityType::Arc) &&
                 !other.points.isEmpty()) {
            // Extend line to circle/arc - use infinite line intersection
            auto result = geometry::infiniteLineCircleIntersection(
                entity->points[0], entity->points[1],
                other.points[0], other.radius);

            auto checkAndAddIntersection = [&](const QPointF& point, double t) {
                // Check if point is on arc (or full circle)
                bool onArc = true;
                if (other.type == SketchEntityType::Arc) {
                    geometry::Arc arc;
                    arc.center = other.points[0];
                    arc.radius = other.radius;
                    arc.startAngle = other.startAngle;
                    arc.sweepAngle = other.sweepAngle;
                    onArc = arc.containsAngle(geometry::vectorAngle(point - other.points[0]));
                }
                if (onArc) {
                    // Check if in extension direction using the t parameter
                    bool validExtension = extendStart ? (t < 0) : (t > 1);
                    if (validExtension) {
                        double dist = QLineF(extendStart ? entity->points[0] : entity->points[1],
                                             point).length();
                        if (dist < bestDist) {
                            bestDist = dist;
                            bestIntersection = point;
                        }
                    }
                }
            };

            if (result.count >= 1) {
                checkAndAddIntersection(result.point1, result.t1);
            }
            if (result.count >= 2) {
                checkAndAddIntersection(result.point2, result.t2);
            }
        }
    }

    if (bestDist < std::numeric_limits<double>::max()) {
        if (extendStart) {
            entity->points[0] = bestIntersection;
        } else {
            entity->points[1] = bestIntersection;
        }
        m_profilesCacheDirty = true;
        emit entityModified(entityId);
        update();
        return true;
    }

    return false;
}

QVector<int> SketchCanvas::splitEntityAtIntersections(int entityId)
{
    QVector<int> newIds;

    SketchEntity* entity = entityById(entityId);
    if (!entity) return newIds;

    // Find all intersections for this entity using library
    QVector<sketch::Entity> libEntities = hobbycad::toLibraryEntities(m_entities);
    sketch::Entity libEntity = hobbycad::toLibraryEntity(*entity);

    QVector<sketch::Intersection> allIntersections = sketch::findAllIntersections(libEntities);

    // Extract intersection points for this entity
    QVector<QPointF> intersectionPoints;
    for (const sketch::Intersection& inter : allIntersections) {
        if (inter.entityId1 == entityId || inter.entityId2 == entityId) {
            intersectionPoints.append(inter.point);
        }
    }

    if (intersectionPoints.isEmpty()) return newIds;

    // Use library split function
    sketch::SplitResult result = sketch::splitEntityAtIntersections(
        libEntity, intersectionPoints,
        [this]() { return m_nextId++; });

    if (!result.success) return newIds;

    // Remove original entity
    m_entities.erase(std::remove_if(m_entities.begin(), m_entities.end(),
                     [entityId](const SketchEntity& e) { return e.id == entityId; }),
                     m_entities.end());

    // Add new entities from split result
    for (const sketch::Entity& newEntity : result.newEntities) {
        SketchEntity guiEntity = hobbycad::toGuiEntity(newEntity);
        m_entities.append(guiEntity);
        newIds.append(guiEntity.id);
        emit entityCreated(guiEntity.id);
    }

    m_profilesCacheDirty = true;
    update();
    return newIds;
}

QVector<int> SketchCanvas::splitEntityAt(int entityId, const QPointF& splitPoint)
{
    QVector<int> newIds;

    SketchEntity* entity = entityById(entityId);
    if (!entity) return newIds;

    sketch::Entity libEntity = hobbycad::toLibraryEntity(*entity);

    // Use library split function
    sketch::SplitResult result = sketch::splitEntityAt(
        libEntity, splitPoint,
        [this]() { return m_nextId++; });

    if (!result.success) return newIds;

    // Remove original entity
    m_entities.erase(std::remove_if(m_entities.begin(), m_entities.end(),
                     [entityId](const SketchEntity& e) { return e.id == entityId; }),
                     m_entities.end());

    // Add new entities from split result
    for (const sketch::Entity& newEntity : result.newEntities) {
        SketchEntity guiEntity = hobbycad::toGuiEntity(newEntity);
        m_entities.append(guiEntity);
        newIds.append(guiEntity.id);
        emit entityCreated(guiEntity.id);
    }

    m_profilesCacheDirty = true;
    update();
    return newIds;
}

// ============================================================================
// Profile Detection
// ============================================================================

QVector<SketchProfile> SketchCanvas::detectProfiles() const
{
    // Convert GUI entities to library entities
    QVector<sketch::Entity> libEntities = toLibraryEntities(m_entities);

    // Use library profile detection
    sketch::ProfileDetectionOptions options;
    options.excludeConstruction = true;
    options.maxProfiles = 100;
    options.polygonSegments = 32;

    QVector<sketch::Profile> libProfiles = sketch::detectProfilesWithHoles(libEntities, options);

    // Convert back to GUI profiles
    return toGuiProfiles(libProfiles);
}

bool SketchCanvas::hasValidProfile() const
{
    return !detectProfiles().isEmpty();
}

void SketchCanvas::setShowProfiles(bool show)
{
    if (m_showProfiles != show) {
        m_showProfiles = show;
        m_profilesCacheDirty = true;
        update();
    }
}

void SketchCanvas::drawProfiles(QPainter& painter) const
{
    // Update cache if needed
    if (m_profilesCacheDirty) {
        m_cachedProfiles = detectProfiles();
        m_profilesCacheDirty = false;
    }

    if (m_cachedProfiles.isEmpty()) return;

    // Draw each profile with a semi-transparent fill
    for (const SketchProfile& profile : m_cachedProfiles) {
        if (profile.polygon.isEmpty()) continue;

        // Convert polygon to screen coordinates
        QPolygonF screenPoly;
        for (const QPointF& p : profile.polygon) {
            screenPoly.append(worldToScreen(p));
        }

        // Choose color based on outer/inner
        QColor fillColor = profile.isOuter
            ? QColor(100, 180, 100, 60)   // Green for outer profiles
            : QColor(180, 100, 100, 60);  // Red for inner profiles (holes)

        painter.setPen(Qt::NoPen);
        painter.setBrush(fillColor);
        painter.drawPolygon(screenPoly);

        // Draw a subtle outline
        QColor outlineColor = profile.isOuter
            ? QColor(100, 180, 100, 150)
            : QColor(180, 100, 100, 150);
        painter.setPen(QPen(outlineColor, 1, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawPolygon(screenPoly);
    }
}

// =====================================================================
//  Offset, Fillet, Chamfer, Pattern Tools
// =====================================================================

int SketchCanvas::findConnectedLineAtCorner(int lineId, const QPointF& clickPos) const
{
    const SketchEntity* line = entityById(lineId);
    if (!line || line->type != SketchEntityType::Line || line->points.size() < 2)
        return -1;

    // Find which endpoint is closer to click position
    double dist0 = QLineF(line->points[0], clickPos).length();
    double dist1 = QLineF(line->points[1], clickPos).length();
    QPointF cornerPoint = (dist0 < dist1) ? line->points[0] : line->points[1];

    // Find another line that shares this endpoint
    const double tolerance = 0.5;  // Connection tolerance in world units

    for (const SketchEntity& entity : m_entities) {
        if (entity.id == lineId) continue;
        if (entity.type != SketchEntityType::Line) continue;
        if (entity.points.size() < 2) continue;

        // Check if either endpoint matches
        if (QLineF(entity.points[0], cornerPoint).length() < tolerance ||
            QLineF(entity.points[1], cornerPoint).length() < tolerance) {
            return entity.id;
        }
    }

    return -1;
}

void SketchCanvas::offsetEntity(int entityId, double distance, const QPointF& clickPos)
{
    SketchEntity* entity = entityById(entityId);
    if (!entity) return;

    // Use library offset function
    sketch::Entity libEntity = hobbycad::toLibraryEntity(*entity);
    sketch::OffsetResult result = sketch::offsetEntity(libEntity, distance, clickPos, m_nextId++);

    if (!result.success) return;

    // Convert result back to GUI entity
    SketchEntity newEntity = hobbycad::toGuiEntity(result.entity);
    m_entities.append(newEntity);
    emit entityCreated(newEntity.id);
    m_profilesCacheDirty = true;
    update();
}

void SketchCanvas::filletCorner(int lineId1, int lineId2, double radius)
{
    SketchEntity* line1 = entityById(lineId1);
    SketchEntity* line2 = entityById(lineId2);

    if (!line1 || !line2) return;

    // Convert to library entities and call library function
    sketch::Entity libLine1 = toLibraryEntity(*line1);
    sketch::Entity libLine2 = toLibraryEntity(*line2);

    sketch::FilletResult result = sketch::createFillet(libLine1, libLine2, radius, m_nextId++);

    if (!result.success) {
        QMessageBox::warning(const_cast<SketchCanvas*>(this), tr("Fillet"),
            tr(result.errorMessage.toUtf8().constData()));
        return;
    }

    // Apply the modified lines
    line1->points = result.line1.points;
    line2->points = result.line2.points;

    // Add the fillet arc
    SketchEntity arc = toGuiEntity(result.arc);
    m_entities.append(arc);

    emit entityCreated(arc.id);
    emit entityModified(lineId1);
    emit entityModified(lineId2);

    m_profilesCacheDirty = true;
    update();
}

void SketchCanvas::chamferCorner(int lineId1, int lineId2, double distance)
{
    SketchEntity* line1 = entityById(lineId1);
    SketchEntity* line2 = entityById(lineId2);

    if (!line1 || !line2) return;

    // Convert to library entities and call library function
    sketch::Entity libLine1 = toLibraryEntity(*line1);
    sketch::Entity libLine2 = toLibraryEntity(*line2);

    sketch::ChamferResult result = sketch::createChamfer(libLine1, libLine2, distance, m_nextId++);

    if (!result.success) {
        QMessageBox::warning(const_cast<SketchCanvas*>(this), tr("Chamfer"),
            tr(result.errorMessage.toUtf8().constData()));
        return;
    }

    // Apply the modified lines
    line1->points = result.line1.points;
    line2->points = result.line2.points;

    // Add the chamfer line
    SketchEntity chamferLine = toGuiEntity(result.chamferLine);
    m_entities.append(chamferLine);

    emit entityCreated(chamferLine.id);
    emit entityModified(lineId1);
    emit entityModified(lineId2);

    m_profilesCacheDirty = true;
    update();
}

void SketchCanvas::createRectangularPattern()
{
    if (m_selectedIds.isEmpty()) return;

    // Prompt for pattern parameters
    bool ok = false;
    int xCount = QInputDialog::getInt(this, tr("Rectangular Pattern"),
        tr("Number of copies in X direction:"), 3, 1, 100, 1, &ok);
    if (!ok) return;

    int yCount = QInputDialog::getInt(this, tr("Rectangular Pattern"),
        tr("Number of copies in Y direction:"), 3, 1, 100, 1, &ok);
    if (!ok) return;

    double xSpacing = QInputDialog::getDouble(this, tr("Rectangular Pattern"),
        tr("Spacing in X direction (mm):"), 20.0, 0.1, 10000.0, 2, &ok);
    if (!ok) return;

    double ySpacing = QInputDialog::getDouble(this, tr("Rectangular Pattern"),
        tr("Spacing in Y direction (mm):"), 20.0, 0.1, 10000.0, 2, &ok);
    if (!ok) return;

    // Collect selected entities and convert to library format
    QVector<sketch::Entity> sourceEntities;
    for (int id : m_selectedIds) {
        const SketchEntity* entity = entityById(id);
        if (entity) {
            sourceEntities.append(toLibraryEntity(*entity));
        }
    }

    // Set up pattern parameters
    sketch::RectPatternParams params;
    params.countX = xCount;
    params.countY = yCount;
    params.spacingX = xSpacing;
    params.spacingY = ySpacing;
    params.includeOriginal = false;

    // Use library to create pattern
    int nextId = m_nextId;
    sketch::RectPatternResult result = sketch::createRectangularPattern(
        sourceEntities, params, [&nextId]() { return nextId++; });

    if (!result.success) {
        QMessageBox::warning(this, tr("Pattern Error"), result.errorMessage);
        return;
    }

    // Add new entities to canvas
    QVector<int> newIds;
    for (const sketch::Entity& libEntity : result.entities) {
        SketchEntity guiEntity = toGuiEntity(libEntity);
        m_entities.append(guiEntity);
        newIds.append(guiEntity.id);
        emit entityCreated(guiEntity.id);
    }
    m_nextId = nextId;

    // Select the new copies
    for (int id : newIds) {
        selectEntity(id, true);
    }

    m_profilesCacheDirty = true;
    update();
}

void SketchCanvas::createCircularPattern()
{
    if (m_selectedIds.isEmpty()) return;

    // Prompt for pattern parameters
    bool ok = false;

    double centerX = QInputDialog::getDouble(this, tr("Circular Pattern"),
        tr("Center X coordinate (mm):"), 0.0, -100000.0, 100000.0, 2, &ok);
    if (!ok) return;

    double centerY = QInputDialog::getDouble(this, tr("Circular Pattern"),
        tr("Center Y coordinate (mm):"), 0.0, -100000.0, 100000.0, 2, &ok);
    if (!ok) return;

    int count = QInputDialog::getInt(this, tr("Circular Pattern"),
        tr("Number of copies (including original):"), 6, 2, 360, 1, &ok);
    if (!ok) return;

    double totalAngle = QInputDialog::getDouble(this, tr("Circular Pattern"),
        tr("Total angle (degrees, 360 for full circle):"), 360.0, 1.0, 360.0, 1, &ok);
    if (!ok) return;

    // Collect selected entities and convert to library format
    QVector<sketch::Entity> sourceEntities;
    for (int id : m_selectedIds) {
        const SketchEntity* entity = entityById(id);
        if (entity) {
            sourceEntities.append(toLibraryEntity(*entity));
        }
    }

    // Set up pattern parameters
    sketch::CircPatternParams params;
    params.center = QPointF(centerX, centerY);
    params.count = count;
    params.totalAngle = totalAngle;

    // Use library to create pattern
    int nextId = m_nextId;
    sketch::CircPatternResult result = sketch::createCircularPattern(
        sourceEntities, params, [&nextId]() { return nextId++; });

    if (!result.success) {
        QMessageBox::warning(this, tr("Pattern Error"), result.errorMessage);
        return;
    }

    // Add new entities to canvas
    QVector<int> newIds;
    for (const sketch::Entity& libEntity : result.entities) {
        SketchEntity guiEntity = toGuiEntity(libEntity);
        m_entities.append(guiEntity);
        newIds.append(guiEntity.id);
        emit entityCreated(guiEntity.id);
    }
    m_nextId = nextId;

    // Select the new copies
    for (int id : newIds) {
        selectEntity(id, true);
    }

    m_profilesCacheDirty = true;
    update();
}

// =====================================================================
//  Background Image Support
// =====================================================================

void SketchCanvas::setBackgroundImage(const sketch::BackgroundImage& bg)
{
    m_backgroundImage = bg;
    m_backgroundCacheDirty = true;
    update();
    emit backgroundImageChanged(m_backgroundImage);
}

void SketchCanvas::clearBackgroundImage()
{
    m_backgroundImage = sketch::BackgroundImage();
    m_cachedBackgroundImage = QImage();
    m_backgroundCacheDirty = false;
    update();
    emit backgroundImageChanged(m_backgroundImage);
}

void SketchCanvas::drawBackgroundImage(QPainter& painter)
{
    if (!m_backgroundImage.enabled) return;

    // Rebuild cached image if needed
    if (m_backgroundCacheDirty) {
        QImage rawImage = sketch::getBackgroundQImage(m_backgroundImage);
        if (!rawImage.isNull()) {
            m_cachedBackgroundImage = sketch::applyBackgroundAdjustments(rawImage, m_backgroundImage);
        } else {
            m_cachedBackgroundImage = QImage();
        }
        m_backgroundCacheDirty = false;
    }

    if (m_cachedBackgroundImage.isNull()) return;

    // Calculate screen coordinates for the background image
    QPointF topLeft = m_backgroundImage.position;
    QPointF bottomRight(topLeft.x() + m_backgroundImage.width,
                        topLeft.y() + m_backgroundImage.height);

    QPoint screenTopLeft = worldToScreen(topLeft);
    QPoint screenBottomRight = worldToScreen(bottomRight);

    // Account for Y-flip in our coordinate system
    QRect destRect = QRect(screenTopLeft, screenBottomRight).normalized();

    // Save painter state
    painter.save();

    // Apply rotation if set
    if (qAbs(m_backgroundImage.rotation) > 0.01) {
        QPointF center = m_backgroundImage.center();
        QPoint screenCenter = worldToScreen(center);
        painter.translate(screenCenter);
        painter.rotate(-m_backgroundImage.rotation);  // Negative because Y is flipped
        painter.translate(-screenCenter);
    }

    // Draw with smooth scaling
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    // Draw the image flipped vertically (our Y axis goes up, image Y goes down)
    QImage flippedImage = m_cachedBackgroundImage.mirrored(false, true);
    painter.drawImage(destRect, flippedImage);

    painter.restore();
}

void SketchCanvas::setBackgroundEditMode(bool enabled)
{
    if (m_backgroundEditMode == enabled) return;

    m_backgroundEditMode = enabled;

    if (enabled) {
        // Clear entity selection when entering background edit mode
        clearSelection();
        setCursor(Qt::OpenHandCursor);
    } else {
        // Reset cursor when exiting
        setCursor(Qt::ArrowCursor);
        m_bgDragHandle = BackgroundHandle::None;
    }

    update();
    emit backgroundEditModeChanged(enabled);
}

void SketchCanvas::setBackgroundCalibrationMode(bool enabled)
{
    if (m_backgroundCalibrationMode == enabled) return;

    m_backgroundCalibrationMode = enabled;

    if (enabled) {
        // Exit edit mode if active
        if (m_backgroundEditMode) {
            setBackgroundEditMode(false);
        }
        // Exit entity selection mode if active
        if (m_calibrationEntitySelectionMode) {
            setCalibrationEntitySelectionMode(false);
        }
        // Clear entity selection
        clearSelection();
        setCursor(Qt::CrossCursor);
    } else {
        setCursor(Qt::ArrowCursor);
    }

    update();
}

void SketchCanvas::setCalibrationEntitySelectionMode(bool enabled)
{
    if (m_calibrationEntitySelectionMode == enabled) return;

    m_calibrationEntitySelectionMode = enabled;

    if (enabled) {
        // Exit other modes
        if (m_backgroundEditMode) {
            setBackgroundEditMode(false);
        }
        if (m_backgroundCalibrationMode) {
            setBackgroundCalibrationMode(false);
        }
        setCursor(Qt::PointingHandCursor);
    } else {
        setCursor(Qt::ArrowCursor);
    }

    update();
}

double SketchCanvas::getEntityAngle(int entityId) const
{
    const SketchEntity* entity = entityById(entityId);
    if (!entity) return 0.0;

    // Only lines have a meaningful single angle
    if (entity->type == SketchEntityType::Line && entity->points.size() >= 2) {
        double dx = entity->points[1].x() - entity->points[0].x();
        double dy = entity->points[1].y() - entity->points[0].y();
        return qRadiansToDegrees(qAtan2(dy, dx));
    }

    // For other entity types, return 0
    return 0.0;
}

void SketchCanvas::drawBackgroundHandles(QPainter& painter)
{
    if (!m_backgroundImage.enabled) return;

    const double handleSize = 8.0;  // Size in screen pixels

    // Get background corners in screen coords
    QPointF tl = m_backgroundImage.position;
    QPointF br(tl.x() + m_backgroundImage.width, tl.y() + m_backgroundImage.height);
    QPointF tr(br.x(), tl.y());
    QPointF bl(tl.x(), br.y());
    QPointF center = m_backgroundImage.center();

    QPoint stl = worldToScreen(tl);
    QPoint str = worldToScreen(tr);
    QPoint sbr = worldToScreen(br);
    QPoint sbl = worldToScreen(bl);
    QPoint sc = worldToScreen(center);

    // Draw bounding box
    painter.setPen(QPen(QColor(0, 120, 215), 2, Qt::DashLine));
    painter.setBrush(Qt::NoBrush);

    QPolygon outline;
    outline << stl << str << sbr << sbl << stl;
    painter.drawPolygon(outline);

    // Draw handle rectangles
    auto drawHandle = [&](const QPoint& pos, bool filled = true) {
        QRectF rect(pos.x() - handleSize/2, pos.y() - handleSize/2, handleSize, handleSize);
        painter.setPen(QPen(QColor(0, 120, 215), 1));
        if (filled) {
            painter.setBrush(Qt::white);
        } else {
            painter.setBrush(QColor(0, 120, 215));
        }
        painter.drawRect(rect);
    };

    // Corner handles (filled white)
    drawHandle(stl);
    drawHandle(str);
    drawHandle(sbr);
    drawHandle(sbl);

    // Edge midpoint handles
    drawHandle(QPoint((stl.x() + str.x()) / 2, (stl.y() + str.y()) / 2));  // Top
    drawHandle(QPoint((str.x() + sbr.x()) / 2, (str.y() + sbr.y()) / 2));  // Right
    drawHandle(QPoint((sbr.x() + sbl.x()) / 2, (sbr.y() + sbl.y()) / 2));  // Bottom
    drawHandle(QPoint((sbl.x() + stl.x()) / 2, (sbl.y() + stl.y()) / 2));  // Left

    // Center move handle (filled blue)
    drawHandle(sc, false);
}

SketchCanvas::BackgroundHandle SketchCanvas::hitTestBackgroundHandle(const QPointF& worldPos) const
{
    if (!m_backgroundImage.enabled) return BackgroundHandle::None;

    const double handleSize = 10.0 / m_zoom;  // Handle size in world units

    QPointF tl = m_backgroundImage.position;
    QPointF br(tl.x() + m_backgroundImage.width, tl.y() + m_backgroundImage.height);
    QPointF tr(br.x(), tl.y());
    QPointF bl(tl.x(), br.y());
    QPointF center = m_backgroundImage.center();

    auto nearPoint = [&](const QPointF& pt) {
        return qAbs(worldPos.x() - pt.x()) < handleSize &&
               qAbs(worldPos.y() - pt.y()) < handleSize;
    };

    // Check corners first (higher priority)
    if (nearPoint(tl)) return BackgroundHandle::TopLeft;
    if (nearPoint(tr)) return BackgroundHandle::TopRight;
    if (nearPoint(br)) return BackgroundHandle::BottomRight;
    if (nearPoint(bl)) return BackgroundHandle::BottomLeft;

    // Check edge midpoints
    QPointF topMid((tl.x() + tr.x()) / 2, (tl.y() + tr.y()) / 2);
    QPointF rightMid((tr.x() + br.x()) / 2, (tr.y() + br.y()) / 2);
    QPointF bottomMid((br.x() + bl.x()) / 2, (br.y() + bl.y()) / 2);
    QPointF leftMid((bl.x() + tl.x()) / 2, (bl.y() + tl.y()) / 2);

    if (nearPoint(topMid)) return BackgroundHandle::Top;
    if (nearPoint(rightMid)) return BackgroundHandle::Right;
    if (nearPoint(bottomMid)) return BackgroundHandle::Bottom;
    if (nearPoint(leftMid)) return BackgroundHandle::Left;

    // Check center handle
    if (nearPoint(center)) return BackgroundHandle::Move;

    // Check if inside background bounds (for move)
    if (worldPos.x() >= tl.x() && worldPos.x() <= br.x() &&
        worldPos.y() >= qMin(tl.y(), br.y()) && worldPos.y() <= qMax(tl.y(), br.y())) {
        return BackgroundHandle::Move;
    }

    return BackgroundHandle::None;
}

QRectF SketchCanvas::backgroundHandleRect(BackgroundHandle handle) const
{
    Q_UNUSED(handle);
    // Not used currently, but could return the rect for a specific handle
    return QRectF();
}

void SketchCanvas::updateCursorForBackgroundHandle(BackgroundHandle handle)
{
    switch (handle) {
    case BackgroundHandle::None:
        setCursor(Qt::ArrowCursor);
        break;
    case BackgroundHandle::Move:
        setCursor(Qt::SizeAllCursor);
        break;
    case BackgroundHandle::TopLeft:
    case BackgroundHandle::BottomRight:
        setCursor(Qt::SizeFDiagCursor);
        break;
    case BackgroundHandle::TopRight:
    case BackgroundHandle::BottomLeft:
        setCursor(Qt::SizeBDiagCursor);
        break;
    case BackgroundHandle::Top:
    case BackgroundHandle::Bottom:
        setCursor(Qt::SizeVerCursor);
        break;
    case BackgroundHandle::Left:
    case BackgroundHandle::Right:
        setCursor(Qt::SizeHorCursor);
        break;
    }
}

}  // namespace hobbycad
