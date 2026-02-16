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

#include <QApplication>
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

void SketchCanvas::setCreationMode(CreationMode mode)
{
    // Map CreationMode to internal mode enums
    // Note: CreationMode values overlap because different tools reuse value 0
    // The mode must be interpreted based on the active tool

    int modeValue = static_cast<int>(mode);

    // Check based on active tool to interpret the mode correctly
    switch (m_activeTool) {
    case SketchTool::Arc:
        // Arc modes: 0=ThreePoint, 1=CenterStartEnd, 2=StartEndRadius, 3=Tangent
        switch (modeValue) {
        case 0: m_arcMode = ArcMode::ThreePoint; break;
        case 3: m_arcMode = ArcMode::Tangent; break;
        default: m_arcMode = ArcMode::ThreePoint; break;
        }
        break;

    case SketchTool::Rectangle:
        // Rectangle modes: 0=Corner, 1=Center, 2=ThreePoint
        switch (modeValue) {
        case 0: m_rectMode = RectMode::Corner; break;
        case 1: m_rectMode = RectMode::Center; break;
        case 2: m_rectMode = RectMode::ThreePoint; break;
        default: m_rectMode = RectMode::Corner; break;
        }
        break;

    case SketchTool::Circle:
        // Circle modes: 0=CenterRadius, 1=TwoPoint, 2=ThreePoint
        switch (modeValue) {
        case 0: m_circleMode = CircleMode::CenterRadius; break;
        case 1: m_circleMode = CircleMode::TwoTangent; break;  // TwoPoint used as TwoTangent
        case 2: m_circleMode = CircleMode::ThreeTangent; break;  // ThreePoint used as ThreeTangent
        default: m_circleMode = CircleMode::CenterRadius; break;
        }
        break;

    case SketchTool::Slot:
        // Slot modes: 0=CenterToCenter, 1=Overall, 2=ArcRadius, 3=ArcEnds
        switch (modeValue) {
        case 0: m_slotMode = SlotMode::CenterToCenter; break;
        case 1: m_slotMode = SlotMode::Overall; break;
        case 2: m_slotMode = SlotMode::ArcRadius; break;
        case 3: m_slotMode = SlotMode::ArcEnds; break;
        default: m_slotMode = SlotMode::CenterToCenter; break;
        }
        break;

    default:
        // Other tools don't need special mode handling
        break;
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

    // Clear fixed handle
    m_fixedHandleIndex = -1;

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

        // Clear fixed handle when selection changes
        m_fixedHandleIndex = -1;
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

void SketchCanvas::setEntities(const QVector<SketchEntity>& entities)
{
    m_entities = entities;
    m_constraints.clear();
    m_selectedId = -1;
    m_selectedConstraintId = -1;

    // Find the maximum entity ID and set m_nextId accordingly
    int maxId = 0;
    for (const SketchEntity& e : entities) {
        if (e.id > maxId) {
            maxId = e.id;
        }
    }
    m_nextId = maxId + 1;
    m_nextConstraintId = 1;  // Reset constraints

    m_profilesCacheDirty = true;
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
    // Clear any previous snap (const cast needed for mutable snap state)
    auto* self = const_cast<SketchCanvas*>(this);
    self->m_activeSnap.reset();

    double tolerance = m_entitySnapTolerance / m_zoom;  // Convert pixels to world units
    QPointF result = world;
    double bestDist = tolerance;

    // First, check entity snap points (higher priority than grid)
    if (m_snapToEntities) {
        QVector<SnapPoint> snapPoints = collectEntitySnapPoints();
        for (const SnapPoint& sp : snapPoints) {
            double dist = QLineF(world, sp.position).length();
            if (dist < bestDist) {
                bestDist = dist;
                result = sp.position;
                self->m_activeSnap = sp;
            }
        }
    }

    // If no entity snap, try grid snap
    if (!m_activeSnap && m_snapToGrid) {
        double snappedX = qRound(world.x() / m_gridSpacing) * m_gridSpacing;
        double snappedY = qRound(world.y() / m_gridSpacing) * m_gridSpacing;
        result = {snappedX, snappedY};
    }

    return result;
}

QVector<SketchCanvas::SnapPoint> SketchCanvas::collectEntitySnapPoints() const
{
    QVector<SnapPoint> points;
    for (const SketchEntity& entity : m_entities) {
        // Don't snap to the entity being dragged
        if (m_isDraggingHandle && entity.id == m_selectedId) continue;
        collectSnapPointsForEntity(entity, points);
    }
    return points;
}

void SketchCanvas::collectSnapPointsForEntity(const SketchEntity& entity, QVector<SnapPoint>& points) const
{
    switch (entity.type) {
    case SketchEntityType::Point:
        if (!entity.points.isEmpty()) {
            points.append({entity.points[0], SnapType::Endpoint, entity.id});
        }
        break;

    case SketchEntityType::Line:
        if (entity.points.size() >= 2) {
            // Endpoints
            points.append({entity.points[0], SnapType::Endpoint, entity.id});
            points.append({entity.points[1], SnapType::Endpoint, entity.id});
            // Midpoint
            QPointF mid = (entity.points[0] + entity.points[1]) / 2.0;
            points.append({mid, SnapType::Midpoint, entity.id});
        }
        break;

    case SketchEntityType::Rectangle:
        if (entity.points.size() >= 4) {
            // 4-point rotated rectangle
            QPointF c0 = entity.points[0];
            QPointF c1 = entity.points[1];
            QPointF c2 = entity.points[2];
            QPointF c3 = entity.points[3];
            // Four corners (endpoints)
            points.append({c0, SnapType::Endpoint, entity.id});
            points.append({c1, SnapType::Endpoint, entity.id});
            points.append({c2, SnapType::Endpoint, entity.id});
            points.append({c3, SnapType::Endpoint, entity.id});
            // Four edge midpoints
            points.append({(c0 + c1) / 2.0, SnapType::Midpoint, entity.id});
            points.append({(c1 + c2) / 2.0, SnapType::Midpoint, entity.id});
            points.append({(c2 + c3) / 2.0, SnapType::Midpoint, entity.id});
            points.append({(c3 + c0) / 2.0, SnapType::Midpoint, entity.id});
            // Center
            QPointF center = (c0 + c1 + c2 + c3) / 4.0;
            points.append({center, SnapType::Center, entity.id});
        } else if (entity.points.size() >= 2) {
            // Axis-aligned rectangle (2 opposite corners)
            QPointF p0 = entity.points[0];
            QPointF p1 = entity.points[1];
            // Four corners (endpoints)
            points.append({p0, SnapType::Endpoint, entity.id});
            points.append({QPointF(p1.x(), p0.y()), SnapType::Endpoint, entity.id});
            points.append({p1, SnapType::Endpoint, entity.id});
            points.append({QPointF(p0.x(), p1.y()), SnapType::Endpoint, entity.id});
            // Four edge midpoints
            points.append({QPointF((p0.x() + p1.x()) / 2, p0.y()), SnapType::Midpoint, entity.id});
            points.append({QPointF(p1.x(), (p0.y() + p1.y()) / 2), SnapType::Midpoint, entity.id});
            points.append({QPointF((p0.x() + p1.x()) / 2, p1.y()), SnapType::Midpoint, entity.id});
            points.append({QPointF(p0.x(), (p0.y() + p1.y()) / 2), SnapType::Midpoint, entity.id});
            // Center
            QPointF center = (p0 + p1) / 2.0;
            points.append({center, SnapType::Center, entity.id});
        }
        break;

    case SketchEntityType::Circle:
        if (!entity.points.isEmpty()) {
            QPointF center = entity.points[0];
            double r = entity.radius;
            // Center
            points.append({center, SnapType::Center, entity.id});
            // Quadrant points
            points.append({center + QPointF(r, 0), SnapType::Quadrant, entity.id});
            points.append({center + QPointF(-r, 0), SnapType::Quadrant, entity.id});
            points.append({center + QPointF(0, r), SnapType::Quadrant, entity.id});
            points.append({center + QPointF(0, -r), SnapType::Quadrant, entity.id});
        }
        break;

    case SketchEntityType::Arc:
        if (!entity.points.isEmpty()) {
            QPointF center = entity.points[0];
            double r = entity.radius;
            // Center
            points.append({center, SnapType::Center, entity.id});
            // Arc endpoints
            double startRad = entity.startAngle * M_PI / 180.0;
            double endRad = (entity.startAngle + entity.sweepAngle) * M_PI / 180.0;
            QPointF start = center + QPointF(r * std::cos(startRad), r * std::sin(startRad));
            QPointF end = center + QPointF(r * std::cos(endRad), r * std::sin(endRad));
            points.append({start, SnapType::Endpoint, entity.id});
            points.append({end, SnapType::Endpoint, entity.id});
            // Arc midpoint
            double midRad = (startRad + endRad) / 2.0;
            QPointF mid = center + QPointF(r * std::cos(midRad), r * std::sin(midRad));
            points.append({mid, SnapType::Midpoint, entity.id});
        }
        break;

    case SketchEntityType::Slot:
        if (entity.points.size() >= 3) {
            // Arc slot: points[0] = arc center, points[1] = start, points[2] = end
            QPointF arcCenter = entity.points[0];
            QPointF start = entity.points[1];
            QPointF end = entity.points[2];
            double halfWidth = entity.radius;

            // Arc center (for the centerline arc)
            points.append({arcCenter, SnapType::Center, entity.id});

            // Slot endpoint centers (where the semicircular ends are centered)
            points.append({start, SnapType::ArcEndCenter, entity.id});
            points.append({end, SnapType::ArcEndCenter, entity.id});

            // Midpoint of the centerline arc
            double arcRadius = QLineF(arcCenter, start).length();
            double startAngle = std::atan2(start.y() - arcCenter.y(), start.x() - arcCenter.x());
            double endAngle = std::atan2(end.y() - arcCenter.y(), end.x() - arcCenter.x());
            double sweep = endAngle - startAngle;
            while (sweep > M_PI) sweep -= 2 * M_PI;
            while (sweep < -M_PI) sweep += 2 * M_PI;
            if (entity.arcFlipped) {
                sweep = (sweep > 0) ? sweep - 2 * M_PI : sweep + 2 * M_PI;
            }
            double midAngle = startAngle + sweep / 2.0;
            QPointF midArc = arcCenter + QPointF(arcRadius * std::cos(midAngle), arcRadius * std::sin(midAngle));
            points.append({midArc, SnapType::Midpoint, entity.id});

            // Outer edge endpoints (extreme tips of the slot)
            QPointF startDir = (start - arcCenter);
            if (QLineF(arcCenter, start).length() > 0.001) {
                startDir = startDir / QLineF(arcCenter, start).length();
            }
            QPointF endDir = (end - arcCenter);
            if (QLineF(arcCenter, end).length() > 0.001) {
                endDir = endDir / QLineF(arcCenter, end).length();
            }
            QPointF startOuter = start + startDir * halfWidth;
            QPointF endOuter = end + endDir * halfWidth;
            points.append({startOuter, SnapType::Endpoint, entity.id});
            points.append({endOuter, SnapType::Endpoint, entity.id});
        } else if (entity.points.size() >= 2) {
            // Linear slot: points[0] and points[1] are arc centers
            QPointF p1 = entity.points[0];
            QPointF p2 = entity.points[1];
            double halfWidth = entity.radius;

            // Arc centers (slot end centers)
            points.append({p1, SnapType::ArcEndCenter, entity.id});
            points.append({p2, SnapType::ArcEndCenter, entity.id});

            // Centerline midpoint
            QPointF mid = (p1 + p2) / 2.0;
            points.append({mid, SnapType::Midpoint, entity.id});

            // Slot extreme endpoints
            double len = QLineF(p1, p2).length();
            if (len > 0.001) {
                QPointF dir = (p2 - p1) / len;
                QPointF end1 = p1 - dir * halfWidth;
                QPointF end2 = p2 + dir * halfWidth;
                points.append({end1, SnapType::Endpoint, entity.id});
                points.append({end2, SnapType::Endpoint, entity.id});
            }
        }
        break;

    case SketchEntityType::Polygon:
        if (!entity.points.isEmpty()) {
            QPointF center = entity.points[0];
            double r = entity.radius;
            int sides = entity.sides > 0 ? entity.sides : 6;

            // Center
            points.append({center, SnapType::Center, entity.id});

            // Vertices and edge midpoints
            double angleStep = 2.0 * M_PI / sides;
            for (int i = 0; i < sides; ++i) {
                double angle = i * angleStep - M_PI / 2;  // Start at top
                QPointF vertex = center + QPointF(r * std::cos(angle), r * std::sin(angle));
                points.append({vertex, SnapType::Endpoint, entity.id});

                // Edge midpoint (between this vertex and next)
                double nextAngle = (i + 1) * angleStep - M_PI / 2;
                QPointF nextVertex = center + QPointF(r * std::cos(nextAngle), r * std::sin(nextAngle));
                QPointF edgeMid = (vertex + nextVertex) / 2.0;
                points.append({edgeMid, SnapType::Midpoint, entity.id});
            }
        }
        break;

    case SketchEntityType::Ellipse:
        if (!entity.points.isEmpty()) {
            QPointF center = entity.points[0];
            double major = entity.majorRadius;
            double minor = entity.minorRadius;

            // Center
            points.append({center, SnapType::Center, entity.id});

            // Quadrant points (major and minor axis endpoints)
            points.append({center + QPointF(major, 0), SnapType::Quadrant, entity.id});
            points.append({center + QPointF(-major, 0), SnapType::Quadrant, entity.id});
            points.append({center + QPointF(0, minor), SnapType::Quadrant, entity.id});
            points.append({center + QPointF(0, -minor), SnapType::Quadrant, entity.id});
        }
        break;

    case SketchEntityType::Spline:
        if (entity.points.size() >= 2) {
            // Endpoints
            points.append({entity.points.first(), SnapType::Endpoint, entity.id});
            points.append({entity.points.last(), SnapType::Endpoint, entity.id});

            // Control points as endpoints (useful for editing)
            for (int i = 1; i < entity.points.size() - 1; ++i) {
                points.append({entity.points[i], SnapType::Endpoint, entity.id});
            }
        }
        break;

    case SketchEntityType::Text:
        if (!entity.points.isEmpty()) {
            // Text anchor point
            points.append({entity.points[0], SnapType::Endpoint, entity.id});
        }
        break;

    default:
        break;
    }
}

QPointF SketchCanvas::snapToAngle(const QPointF& origin, const QPointF& target) const
{
    // Snap to nearest 45-degree increment (0, 45, 90, 135, 180, 225, 270, 315)
    auto* self = const_cast<SketchCanvas*>(this);

    double dx = target.x() - origin.x();
    double dy = target.y() - origin.y();
    double distance = std::sqrt(dx * dx + dy * dy);

    if (distance < 0.001) {
        self->m_angleSnapActive = false;
        return target;
    }

    // Calculate current angle in degrees
    double angle = std::atan2(dy, dx) * 180.0 / M_PI;

    // Snap to nearest 45 degrees
    double snappedAngle = std::round(angle / 45.0) * 45.0;

    self->m_angleSnapActive = true;
    self->m_snappedAngle = snappedAngle;

    // Calculate new position at snapped angle
    double snappedRad = snappedAngle * M_PI / 180.0;
    return origin + QPointF(distance * std::cos(snappedRad), distance * std::sin(snappedRad));
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

    // Draw active snap point indicator
    if (m_activeSnap && (m_isDrawing || m_isDraggingHandle)) {
        drawSnapIndicator(painter, m_activeSnap.value());
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
        if (entity.points.size() >= 4) {
            // 4-point rotated rectangle (from 3-point mode)
            QPoint c1 = worldToScreen(entity.points[0]);
            QPoint c2 = worldToScreen(entity.points[1]);
            QPoint c3 = worldToScreen(entity.points[2]);
            QPoint c4 = worldToScreen(entity.points[3]);
            painter.drawLine(c1, c2);
            painter.drawLine(c2, c3);
            painter.drawLine(c3, c4);
            painter.drawLine(c4, c1);
        } else if (entity.points.size() >= 2) {
            // Standard axis-aligned rectangle (2 points = opposite corners)
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
        if (entity.points.size() >= 3) {
            // Arc slot: points[0] = arc center, points[1] = start, points[2] = end
            QPointF arcCenterWorld = entity.points[0];
            QPointF startWorld = entity.points[1];
            QPointF endWorld = entity.points[2];
            double halfWidth = entity.radius;

            double arcRadius = QLineF(arcCenterWorld, startWorld).length();

            // Project end point onto the arc (same radius from center)
            // This ensures both endpoints are on the arc
            double endDist = QLineF(arcCenterWorld, endWorld).length();
            if (endDist > 0.001) {
                double scale = arcRadius / endDist;
                endWorld = arcCenterWorld + (endWorld - arcCenterWorld) * scale;
            }

            double innerRadius = arcRadius - halfWidth;
            double outerRadius = arcRadius + halfWidth;

            // Convert to screen coords
            QPointF start = worldToScreen(startWorld);
            QPointF end = worldToScreen(endWorld);  // Now projected onto arc
            QPointF arcCenter = worldToScreen(arcCenterWorld);
            double screenHalfWidth = halfWidth * m_zoom;
            double screenInnerRadius = innerRadius * m_zoom;
            double screenOuterRadius = outerRadius * m_zoom;

            if (screenInnerRadius > 1 && screenOuterRadius > screenInnerRadius) {
                double startAngle = std::atan2(-(start.y() - arcCenter.y()), start.x() - arcCenter.x()) * 180.0 / M_PI;
                double endAngle = std::atan2(-(end.y() - arcCenter.y()), end.x() - arcCenter.x()) * 180.0 / M_PI;
                double sweepAngle = endAngle - startAngle;

                // Normalize sweep angle
                while (sweepAngle > 180) sweepAngle -= 360;
                while (sweepAngle < -180) sweepAngle += 360;

                // Apply arc flip for > 180 degree arcs
                if (entity.arcFlipped) {
                    if (sweepAngle > 0) {
                        sweepAngle -= 360;
                    } else {
                        sweepAngle += 360;
                    }
                }

                QPainterPath path;
                // Outer arc
                QRectF outerRect(arcCenter.x() - screenOuterRadius, arcCenter.y() - screenOuterRadius,
                                screenOuterRadius * 2, screenOuterRadius * 2);
                path.arcMoveTo(outerRect, startAngle);
                path.arcTo(outerRect, startAngle, sweepAngle);

                // End cap (semicircle at end)
                QPointF outerEnd = path.currentPosition();
                QRectF innerRect(arcCenter.x() - screenInnerRadius, arcCenter.y() - screenInnerRadius,
                                screenInnerRadius * 2, screenInnerRadius * 2);
                QPainterPath tempPath;
                tempPath.arcMoveTo(innerRect, endAngle);
                QPointF innerEnd = tempPath.currentPosition();

                QPointF capCenter = (outerEnd + innerEnd) / 2;
                double capRadius = screenHalfWidth;
                QRectF capRect(capCenter.x() - capRadius, capCenter.y() - capRadius,
                              capRadius * 2, capRadius * 2);
                double capStartAngle = std::atan2(-(outerEnd.y() - capCenter.y()), outerEnd.x() - capCenter.x()) * 180.0 / M_PI;
                // End cap direction depends on sweep direction
                double capSweep = (sweepAngle >= 0) ? 180 : -180;
                path.arcTo(capRect, capStartAngle, capSweep);

                // Inner arc (reverse direction)
                path.arcTo(innerRect, endAngle, -sweepAngle);

                // Start cap (semicircle at start)
                tempPath.arcMoveTo(outerRect, startAngle);
                QPointF outerStart = tempPath.currentPosition();
                tempPath.arcMoveTo(innerRect, startAngle);
                QPointF innerStart = tempPath.currentPosition();
                capCenter = (outerStart + innerStart) / 2;
                capRect = QRectF(capCenter.x() - capRadius, capCenter.y() - capRadius,
                                capRadius * 2, capRadius * 2);
                capStartAngle = std::atan2(-(innerStart.y() - capCenter.y()), innerStart.x() - capCenter.x()) * 180.0 / M_PI;
                // Start cap direction also depends on sweep direction
                path.arcTo(capRect, capStartAngle, capSweep);

                path.closeSubpath();
                painter.drawPath(path);

                // Draw construction line arc along centerline (connecting arc centers of slot ends)
                painter.save();
                QPen constructionPen(QColor(100, 100, 100, 180), 1, Qt::DashLine);
                painter.setPen(constructionPen);
                painter.setBrush(Qt::NoBrush);
                double screenArcRadius = arcRadius * m_zoom;
                QRectF centerlineRect(arcCenter.x() - screenArcRadius, arcCenter.y() - screenArcRadius,
                                      screenArcRadius * 2, screenArcRadius * 2);
                QPainterPath centerlinePath;
                centerlinePath.arcMoveTo(centerlineRect, startAngle);
                centerlinePath.arcTo(centerlineRect, startAngle, sweepAngle);
                painter.drawPath(centerlinePath);
                painter.restore();
            } else {
                // Arc radius too small for proper slot - draw centerline arc with endpoint circles
                double startAngle = std::atan2(-(start.y() - arcCenter.y()), start.x() - arcCenter.x()) * 180.0 / M_PI;
                double endAngle = std::atan2(-(end.y() - arcCenter.y()), end.x() - arcCenter.x()) * 180.0 / M_PI;
                double sweepAngle = endAngle - startAngle;
                while (sweepAngle > 180) sweepAngle -= 360;
                while (sweepAngle < -180) sweepAngle += 360;

                double screenArcRadius = QLineF(arcCenter, start).length();
                if (screenArcRadius > 5) {
                    QPainterPath path;
                    QRectF arcRect(arcCenter.x() - screenArcRadius, arcCenter.y() - screenArcRadius,
                                  screenArcRadius * 2, screenArcRadius * 2);
                    path.arcMoveTo(arcRect, startAngle);
                    path.arcTo(arcRect, startAngle, sweepAngle);
                    painter.drawPath(path);

                    // Draw slot width circles at start and end
                    painter.drawEllipse(start, screenHalfWidth, screenHalfWidth);
                    painter.drawEllipse(end, screenHalfWidth, screenHalfWidth);
                } else {
                    // Very close to center - just draw lines
                    painter.drawLine(start.toPoint(), arcCenter.toPoint());
                    painter.drawLine(end.toPoint(), arcCenter.toPoint());
                }
            }
        } else if (entity.points.size() >= 2) {
            // Linear slot: points[0] and points[1] are arc centers
            QPointF p1 = worldToScreen(entity.points[0]);
            QPointF p2 = worldToScreen(entity.points[1]);
            double halfWidth = entity.radius * m_zoom;

            // Calculate the direction and perpendicular vectors
            QLineF centerLine(p1, p2);
            double len = centerLine.length();
            if (len < 0.001) break;  // Degenerate slot

            // Unit vectors along and perpendicular to the slot axis
            double dx = (p2.x() - p1.x()) / len;
            double dy = (p2.y() - p1.y()) / len;
            double px = -dy * halfWidth;  // Perpendicular x
            double py = dx * halfWidth;   // Perpendicular y

            // Four corners of the slot body (going clockwise around the slot)
            QPointF c1(p1.x() + px, p1.y() + py);  // p1 + perp (top-left if horizontal)
            QPointF c2(p2.x() + px, p2.y() + py);  // p2 + perp (top-right if horizontal)
            QPointF c3(p2.x() - px, p2.y() - py);  // p2 - perp (bottom-right if horizontal)
            QPointF c4(p1.x() - px, p1.y() - py);  // p1 - perp (bottom-left if horizontal)

            // Calculate angles for arc start points relative to circle centers
            // For c2 (on p2's circle): angle from p2 to c2
            double startAngle2 = std::atan2(-(c2.y() - p2.y()), c2.x() - p2.x()) * 180.0 / M_PI;
            // For c4 (on p1's circle): angle from p1 to c4
            double startAngle1 = std::atan2(-(c4.y() - p1.y()), c4.x() - p1.x()) * 180.0 / M_PI;

            QPainterPath path;
            // Start at c1, line to c2 (along one side)
            path.moveTo(c1);
            path.lineTo(c2);
            // Semicircle at p2 end (from c2 to c3) - counter-clockwise sweep (outward)
            QRectF arcRect2(p2.x() - halfWidth, p2.y() - halfWidth,
                           halfWidth * 2, halfWidth * 2);
            path.arcTo(arcRect2, startAngle2, 180);
            // Line from c3 to c4 (along other side)
            path.lineTo(c4);
            // Semicircle at p1 end (from c4 to c1) - counter-clockwise sweep (outward)
            QRectF arcRect1(p1.x() - halfWidth, p1.y() - halfWidth,
                           halfWidth * 2, halfWidth * 2);
            path.arcTo(arcRect1, startAngle1, 180);
            path.closeSubpath();
            painter.drawPath(path);

            // Draw construction line along the centerline of the slot
            painter.save();
            QPen constructionPen(QColor(100, 100, 100, 180), 1, Qt::DashLine);
            painter.setPen(constructionPen);
            painter.setBrush(Qt::NoBrush);
            painter.drawLine(p1.toPoint(), p2.toPoint());
            painter.restore();
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

            // Draw angle snap indicator when Ctrl is held
            if (m_angleSnapActive) {
                painter.save();
                painter.setPen(QPen(QColor(255, 140, 0), 1, Qt::DashLine));
                // Draw extended guide line through the snapped angle
                double len = QLineF(p1, p2).length();
                double extendLen = qMax(len * 0.3, 30.0);
                double angleRad = m_snappedAngle * M_PI / 180.0;
                QPointF dir(std::cos(angleRad), -std::sin(angleRad));  // Screen Y is inverted
                QPointF ext1 = QPointF(p1) - dir * extendLen;
                QPointF ext2 = QPointF(p2) + dir * extendLen;
                painter.drawLine(ext1.toPoint(), p1);
                painter.drawLine(p2, ext2.toPoint());

                // Draw angle label near the start point
                QString angleText = QString::number(static_cast<int>(m_snappedAngle)) + QStringLiteral("°");
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

            // Draw dimension label below the line
            double length = QLineF(m_previewPoints[0], m_currentMouseWorld).length();
            if (length > 0.1) {
                drawPreviewDimension(painter, p1, p2, length);
            }
        }
        break;

    case SketchTool::Rectangle:
        if (!m_previewPoints.isEmpty()) {
            if (m_rectMode == RectMode::ThreePoint) {
                // 3-Point mode: draw angled rectangle
                // Point 1: first corner, Point 2: second corner (defines first edge)
                // Point 3 (or mouse): perpendicular offset (defines width)
                QPointF p1 = m_previewPoints[0];
                QPointF p2 = (m_previewPoints.size() >= 2) ? m_previewPoints[1] : m_currentMouseWorld;
                QPointF p3 = m_currentMouseWorld;

                if (m_previewPoints.size() < 2) {
                    // Still defining first edge - just draw the line
                    QPoint sp1 = worldToScreen(p1);
                    QPoint sp2 = worldToScreen(p2);
                    painter.drawLine(sp1, sp2);

                    // Draw length dimension
                    double edgeLen = QLineF(p1, p2).length();
                    if (edgeLen > 0.1) {
                        drawPreviewDimension(painter, sp1, sp2, edgeLen);
                    }
                } else {
                    // Have two corners, now defining width
                    // Calculate the direction of the first edge
                    QPointF edge = p2 - p1;
                    double edgeLen = QLineF(p1, p2).length();
                    if (edgeLen > 0.01) {
                        // Normalize edge direction
                        QPointF edgeDir = edge / edgeLen;
                        // Perpendicular direction (rotate 90 degrees CCW)
                        QPointF perpDir(-edgeDir.y(), edgeDir.x());

                        // Project mouse position onto perpendicular direction
                        QPointF toMouse = p3 - p1;
                        double perpDist = toMouse.x() * perpDir.x() + toMouse.y() * perpDir.y();

                        // Calculate all four corners
                        QPointF c1 = p1;
                        QPointF c2 = p2;
                        QPointF c3 = p2 + perpDir * perpDist;
                        QPointF c4 = p1 + perpDir * perpDist;

                        // Draw the rectangle
                        QPoint sc1 = worldToScreen(c1);
                        QPoint sc2 = worldToScreen(c2);
                        QPoint sc3 = worldToScreen(c3);
                        QPoint sc4 = worldToScreen(c4);

                        painter.drawLine(sc1, sc2);
                        painter.drawLine(sc2, sc3);
                        painter.drawLine(sc3, sc4);
                        painter.drawLine(sc4, sc1);

                        // Draw dimensions
                        drawPreviewDimension(painter, sc1, sc2, edgeLen);
                        double width = std::abs(perpDist);
                        if (width > 0.1) {
                            drawPreviewDimension(painter, sc2, sc3, width);
                        }
                    }
                }

                // Draw corner markers for placed points
                painter.save();
                painter.setPen(QPen(QColor(255, 140, 0), 1));
                painter.setBrush(QColor(255, 140, 0));
                for (int i = 0; i < m_previewPoints.size(); ++i) {
                    QPoint sp = worldToScreen(m_previewPoints[i]);
                    painter.drawEllipse(sp, 4, 4);
                }
                painter.restore();
            } else {
                // Corner or Center mode
                QPointF corner1, corner2;
                if (m_rectMode == RectMode::Center) {
                    // Center mode: first point is center, mouse is one corner
                    QPointF center = m_previewPoints[0];
                    QPointF delta = m_currentMouseWorld - center;
                    corner1 = center - delta;  // Opposite corner
                    corner2 = m_currentMouseWorld;
                } else {
                    // Corner mode: first point is one corner, mouse is opposite
                    corner1 = m_previewPoints[0];
                    corner2 = m_currentMouseWorld;
                }

                QPoint p1 = worldToScreen(corner1);
                QPoint p2 = worldToScreen(corner2);
                QRect rect = QRect(p1, p2).normalized();
                painter.drawRect(rect);

                // Draw center marker in center mode
                if (m_rectMode == RectMode::Center) {
                    QPoint centerScreen = worldToScreen(m_previewPoints[0]);
                    painter.save();
                    painter.setPen(QPen(QColor(255, 140, 0), 1));
                    painter.drawLine(centerScreen.x() - 5, centerScreen.y(),
                                   centerScreen.x() + 5, centerScreen.y());
                    painter.drawLine(centerScreen.x(), centerScreen.y() - 5,
                                   centerScreen.x(), centerScreen.y() + 5);
                    painter.restore();
                }

                // Draw width and height dimensions
                double width = std::abs(corner2.x() - corner1.x());
                double height = std::abs(corner2.y() - corner1.y());
                if (width > 0.1) {
                    // Width dimension along bottom edge
                    QPoint bottomLeft(rect.left(), rect.bottom());
                    QPoint bottomRight(rect.right(), rect.bottom());
                    drawPreviewDimension(painter, bottomLeft, bottomRight, width);
                }
                if (height > 0.1) {
                    // Height dimension along right edge
                    QPoint topRight(rect.right(), rect.top());
                    QPoint bottomRight(rect.right(), rect.bottom());
                    drawPreviewDimension(painter, topRight, bottomRight, height);
                }
            }
        }
        break;

    case SketchTool::Circle:
        if (!m_previewPoints.isEmpty()) {
            QPoint center = worldToScreen(m_previewPoints[0]);
            double r = QLineF(m_previewPoints[0], m_currentMouseWorld).length();
            int rPx = static_cast<int>(r * m_zoom);
            painter.drawEllipse(center, rPx, rPx);

            // Draw radius dimension
            if (r > 0.1) {
                QPoint radiusEnd = worldToScreen(m_currentMouseWorld);
                drawPreviewDimension(painter, center, radiusEnd, r);
            }
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

    case SketchTool::Slot:
        if (!m_previewPoints.isEmpty()) {
            QPointF p1World = m_previewPoints[0];
            QPointF p2World = m_currentMouseWorld;
            double radius = m_pendingEntity.radius;
            if (radius < 0.1) radius = 5.0;  // Default radius

            bool isArcSlot = (m_slotMode == SlotMode::ArcRadius || m_slotMode == SlotMode::ArcEnds);
            if (isArcSlot) {
                // Arc slot mode - needs 3 points
                // ArcRadius: arc center -> start -> end (both endpoints constrained to arc)
                // ArcEnds: start -> end -> arc center (free placement)
                if (m_previewPoints.size() >= 2) {
                    QPointF startWorld, endWorld, arcCenterWorld;

                    if (m_slotMode == SlotMode::ArcRadius) {
                        // Have arc center and start, current mouse is end (constrained to arc)
                        arcCenterWorld = m_previewPoints[0];
                        startWorld = m_previewPoints[1];
                        // Constrain end point to arc radius
                        double arcRadius = QLineF(arcCenterWorld, startWorld).length();
                        double mouseAngle = std::atan2(m_currentMouseWorld.y() - arcCenterWorld.y(),
                                                       m_currentMouseWorld.x() - arcCenterWorld.x());
                        double startAngle = std::atan2(startWorld.y() - arcCenterWorld.y(),
                                                       startWorld.x() - arcCenterWorld.x());

                        // Minimum angular separation so slot ends don't overlap
                        // Arc length between centers must be >= 2 * slot radius
                        double slotRadius = m_pendingEntity.radius;
                        if (slotRadius < 0.1) slotRadius = 5.0;
                        double minAngularSep = (arcRadius > 0.001) ? (2.0 * slotRadius / arcRadius) : 0.1;

                        // Calculate angular difference
                        double angleDiff = mouseAngle - startAngle;
                        // Normalize to [-PI, PI]
                        while (angleDiff > M_PI) angleDiff -= 2 * M_PI;
                        while (angleDiff < -M_PI) angleDiff += 2 * M_PI;

                        // Clamp to minimum separation
                        if (std::abs(angleDiff) < minAngularSep) {
                            // Push to minimum distance in same direction
                            double sign = (angleDiff >= 0) ? 1.0 : -1.0;
                            mouseAngle = startAngle + sign * minAngularSep;
                        }

                        endWorld = arcCenterWorld + QPointF(arcRadius * std::cos(mouseAngle),
                                                            arcRadius * std::sin(mouseAngle));
                    } else {
                        // ArcEnds: Have start and end points, current mouse is arc center
                        // Both endpoints stay fixed; arc center is constrained to the perpendicular
                        // bisector of the line between start and end (equidistant from both)
                        startWorld = m_previewPoints[0];
                        endWorld = m_previewPoints[1];

                        // Find the perpendicular bisector of start-end line
                        QPointF midpoint = (startWorld + endWorld) / 2.0;
                        QPointF startToEnd = endWorld - startWorld;
                        double chordLen = QLineF(startWorld, endWorld).length();

                        if (chordLen > 0.001) {
                            // Perpendicular direction (rotate 90 degrees)
                            QPointF perpDir(-startToEnd.y() / chordLen, startToEnd.x() / chordLen);

                            // Project mouse position onto the perpendicular bisector
                            QPointF mouseToMid = m_currentMouseWorld - midpoint;
                            double projDist = mouseToMid.x() * perpDir.x() + mouseToMid.y() * perpDir.y();

                            // Arc center is on the perpendicular bisector
                            arcCenterWorld = midpoint + perpDir * projDist;
                        } else {
                            arcCenterWorld = m_currentMouseWorld;
                        }
                    }

                    // Calculate arc radius (now equidistant from both endpoints for ArcEnds mode)
                    double arcRadius = QLineF(arcCenterWorld, startWorld).length();

                    // Enforce minimum angular separation so slot ends don't overlap
                    double startAngleRad = std::atan2(startWorld.y() - arcCenterWorld.y(),
                                                       startWorld.x() - arcCenterWorld.x());
                    double endAngleRad = std::atan2(endWorld.y() - arcCenterWorld.y(),
                                                     endWorld.x() - arcCenterWorld.x());
                    double slotRadius = m_pendingEntity.radius;
                    if (slotRadius < 0.1) slotRadius = 5.0;
                    double minAngularSep = (arcRadius > 0.001) ? (2.0 * slotRadius / arcRadius) : 0.1;

                    double angleDiff = endAngleRad - startAngleRad;
                    while (angleDiff > M_PI) angleDiff -= 2 * M_PI;
                    while (angleDiff < -M_PI) angleDiff += 2 * M_PI;

                    // If endpoints are too close angularly, push arc center further out
                    if (std::abs(angleDiff) < minAngularSep && arcRadius > 0.001) {
                        double halfChord = QLineF(startWorld, endWorld).length() / 2.0;
                        // For minimum separation, we need a larger radius
                        // halfChord = radius * sin(angle/2), so radius = halfChord / sin(minAngularSep/2)
                        double requiredRadius = halfChord / std::sin(minAngularSep / 2.0);
                        if (requiredRadius > arcRadius) {
                            QPointF midpoint = (startWorld + endWorld) / 2.0;
                            QPointF toCenter = arcCenterWorld - midpoint;
                            double toCenterLen = QLineF(midpoint, arcCenterWorld).length();
                            if (toCenterLen > 0.001) {
                                double newDist = std::sqrt(requiredRadius * requiredRadius - halfChord * halfChord);
                                arcCenterWorld = midpoint + toCenter * (newDist / toCenterLen);
                                arcRadius = requiredRadius;
                            }
                        }
                    }

                    double innerRadius = arcRadius - radius;
                    double outerRadius = arcRadius + radius;

                    // Draw the arc slot preview
                    QPointF ss = worldToScreen(startWorld);
                    QPointF se = worldToScreen(endWorld);  // Now projected onto arc
                    QPointF sc = worldToScreen(arcCenterWorld);

                    double screenInnerRadius = innerRadius * m_zoom;
                    double screenOuterRadius = outerRadius * m_zoom;
                    double screenHalfWidth = radius * m_zoom;

                    double startAngle = std::atan2(-(ss.y() - sc.y()), ss.x() - sc.x()) * 180.0 / M_PI;
                    double endAngle = std::atan2(-(se.y() - sc.y()), se.x() - sc.x()) * 180.0 / M_PI;
                    double sweepAngle = endAngle - startAngle;

                    // Normalize sweep angle to [-180, 180]
                    while (sweepAngle > 180) sweepAngle -= 360;
                    while (sweepAngle < -180) sweepAngle += 360;

                    // Use tracked flip state (toggled by Shift key)
                    if (m_arcSlotFlipped) {
                        if (sweepAngle > 0) {
                            sweepAngle -= 360;
                        } else {
                            sweepAngle += 360;
                        }
                    }

                    if (screenInnerRadius > 1 && screenOuterRadius > screenInnerRadius) {
                        QPainterPath path;
                        // Outer arc
                        QRectF outerRect(sc.x() - screenOuterRadius, sc.y() - screenOuterRadius,
                                        screenOuterRadius * 2, screenOuterRadius * 2);
                        path.arcMoveTo(outerRect, startAngle);
                        path.arcTo(outerRect, startAngle, sweepAngle);

                        // End cap
                        QPointF outerEnd = path.currentPosition();
                        QRectF innerRect(sc.x() - screenInnerRadius, sc.y() - screenInnerRadius,
                                        screenInnerRadius * 2, screenInnerRadius * 2);
                        QPainterPath tempPath;
                        tempPath.arcMoveTo(innerRect, endAngle);
                        QPointF innerEnd = tempPath.currentPosition();

                        // Semi-circle end cap at end
                        QPointF capCenter = (outerEnd + innerEnd) / 2;
                        double capRadius = screenHalfWidth;
                        QRectF capRect(capCenter.x() - capRadius, capCenter.y() - capRadius,
                                      capRadius * 2, capRadius * 2);
                        double capStartAngle = std::atan2(-(outerEnd.y() - capCenter.y()), outerEnd.x() - capCenter.x()) * 180.0 / M_PI;
                        // End cap direction depends on sweep direction
                        double capSweep = (sweepAngle >= 0) ? 180 : -180;
                        path.arcTo(capRect, capStartAngle, capSweep);

                        // Inner arc (reverse direction)
                        path.arcTo(innerRect, endAngle, -sweepAngle);

                        // Start cap
                        tempPath.arcMoveTo(outerRect, startAngle);
                        QPointF outerStart = tempPath.currentPosition();
                        tempPath.arcMoveTo(innerRect, startAngle);
                        QPointF innerStart = tempPath.currentPosition();
                        capCenter = (outerStart + innerStart) / 2;
                        capRect = QRectF(capCenter.x() - capRadius, capCenter.y() - capRadius,
                                        capRadius * 2, capRadius * 2);
                        capStartAngle = std::atan2(-(innerStart.y() - capCenter.y()), innerStart.x() - capCenter.x()) * 180.0 / M_PI;
                        // Start cap direction also depends on sweep direction
                        path.arcTo(capRect, capStartAngle, capSweep);

                        path.closeSubpath();
                        painter.drawPath(path);
                    } else {
                        // Arc radius too small for proper slot - draw centerline arc to show path
                        double screenArcRadius = QLineF(sc, ss).length();
                        if (screenArcRadius > 5) {
                            QPainterPath path;
                            QRectF arcRect(sc.x() - screenArcRadius, sc.y() - screenArcRadius,
                                          screenArcRadius * 2, screenArcRadius * 2);
                            path.arcMoveTo(arcRect, startAngle);
                            path.arcTo(arcRect, startAngle, sweepAngle);
                            painter.drawPath(path);

                            // Draw slot width circles at start and end to indicate width
                            painter.drawEllipse(ss, screenHalfWidth, screenHalfWidth);
                            painter.drawEllipse(se, screenHalfWidth, screenHalfWidth);
                        } else {
                            // Very close to center - just draw lines to show relationship
                            painter.drawLine(ss.toPoint(), sc.toPoint());
                            painter.drawLine(se.toPoint(), sc.toPoint());
                        }
                    }

                    // Draw control points
                    painter.setBrush(QColor(0, 120, 215));
                    painter.drawEllipse(ss.toPoint(), 3, 3);
                    painter.drawEllipse(se.toPoint(), 3, 3);
                    painter.drawEllipse(sc.toPoint(), 3, 3);

                    // Draw arc length dimension along the centerline arc
                    double arcLengthWorld = arcRadius * std::abs(sweepAngle * M_PI / 180.0);
                    if (arcLengthWorld > 0.1) {
                        // Position dimension label at the midpoint of the arc (offset outward)
                        double midAngle = startAngle + sweepAngle / 2.0;
                        double midAngleRad = midAngle * M_PI / 180.0;
                        double outwardAngle = -midAngleRad;
                        double offsetDist = screenOuterRadius + 20;
                        QPointF labelCenter = sc + QPointF(offsetDist * std::cos(outwardAngle),
                                                           offsetDist * std::sin(outwardAngle));
                        drawDimensionLabel(painter, labelCenter, arcLengthWorld);
                    }

                    // Draw label to indicate what's being placed (3rd point)
                    painter.save();
                    painter.setPen(QColor(0, 120, 215));
                    QFont font = painter.font();
                    font.setPointSize(9);
                    painter.setFont(font);
                    QString line1 = (m_slotMode == SlotMode::ArcRadius)
                        ? tr("Click to place END point")
                        : tr("Click to place ARC CENTER");
                    QString line2 = tr("(Shift to flip arc direction)");
                    QFontMetrics fm = painter.fontMetrics();
                    QRectF rect1 = fm.boundingRect(line1);
                    QRectF rect2 = fm.boundingRect(line2);
                    double totalHeight = rect1.height() + rect2.height() + 2;
                    double maxWidth = qMax(rect1.width(), rect2.width());
                    // Position label below the moving point
                    QPointF labelPos = (m_slotMode == SlotMode::ArcRadius)
                        ? se + QPointF(0, 15)   // ArcRadius: end point follows mouse
                        : sc + QPointF(0, 15);  // ArcEnds: arc center follows mouse
                    QRectF bgRect(-maxWidth / 2 - 2, 0, maxWidth + 4, totalHeight + 2);
                    bgRect.translate(labelPos);
                    // Draw background for readability
                    painter.fillRect(bgRect, QColor(255, 255, 255, 200));
                    // Draw line 1
                    QPointF textPos1(labelPos.x() - rect1.width() / 2, labelPos.y() + rect1.height());
                    painter.drawText(textPos1, line1);
                    // Draw line 2
                    QPointF textPos2(labelPos.x() - rect2.width() / 2, labelPos.y() + rect1.height() + rect2.height() + 2);
                    painter.drawText(textPos2, line2);
                    painter.restore();
                } else if (m_previewPoints.size() == 1) {
                    // Only have start point, draw line to current mouse (placing 2nd point)
                    QPoint sp1 = worldToScreen(p1World);
                    QPoint sp2 = worldToScreen(p2World);
                    painter.drawLine(sp1, sp2);
                    painter.setBrush(QColor(0, 120, 215));
                    painter.drawEllipse(sp1, 3, 3);

                    // Draw dimension below the line
                    double distance = QLineF(p1World, p2World).length();
                    if (distance > 0.1) {
                        drawPreviewDimension(painter, sp1, sp2, distance);
                    }

                    // Draw instruction label at midpoint of line, rotated to follow the line
                    QPointF midPoint = (QPointF(sp1) + QPointF(sp2)) / 2.0;
                    painter.save();
                    painter.setPen(QColor(0, 120, 215));
                    QFont font = painter.font();
                    font.setPointSize(9);
                    painter.setFont(font);

                    // Calculate line angle
                    double dx = sp2.x() - sp1.x();
                    double dy = sp2.y() - sp1.y();
                    double angle = std::atan2(dy, dx) * 180.0 / M_PI;

                    // Keep text readable (Z-up orientation) - flip if pointing left
                    if (angle > 90 || angle < -90) {
                        angle += 180;
                    }

                    // Different label based on mode
                    // ArcRadius: arc center -> start -> end (constrained)
                    // ArcEnds: start -> end -> arc center
                    QString label = (m_slotMode == SlotMode::ArcRadius)
                        ? tr("Click to place START point")
                        : tr("Click to place END point");
                    QRectF textRect = painter.fontMetrics().boundingRect(label);

                    // Translate to midpoint, rotate, then draw centered above the line
                    painter.translate(midPoint);
                    painter.rotate(angle);
                    // Offset upward (negative Y in rotated coords) to sit on top of line
                    QPointF offset(0, -textRect.height() / 2 - 4);
                    textRect.moveCenter(offset);
                    // Draw background for readability
                    painter.fillRect(textRect.adjusted(-2, -1, 2, 1), QColor(255, 255, 255, 200));
                    painter.drawText(textRect, Qt::AlignCenter, label);
                    painter.restore();
                }
            } else {
                // Linear slot modes (CenterToCenter and Overall)
                QPointF center1, center2;

                if (m_slotMode == SlotMode::Overall) {
                    // Overall mode: points are endpoints, centers are offset inward by radius
                    double len = QLineF(p1World, p2World).length();
                    if (len > 0.001) {
                        double dx = (p2World.x() - p1World.x()) / len;
                        double dy = (p2World.y() - p1World.y()) / len;
                        center1 = QPointF(p1World.x() + dx * radius, p1World.y() + dy * radius);
                        center2 = QPointF(p2World.x() - dx * radius, p2World.y() - dy * radius);
                    } else {
                        center1 = p1World;
                        center2 = p2World;
                    }
                } else {
                    // CenterToCenter mode: points are arc centers
                    center1 = p1World;
                    center2 = p2World;
                }

                double halfWidth = radius * m_zoom;
                QPointF sp1 = worldToScreen(center1);
                QPointF sp2 = worldToScreen(center2);
                QLineF centerLine(sp1, sp2);
                double len = centerLine.length();

                if (len > 0.001) {
                    // Unit vectors along and perpendicular to the slot axis
                    double dx = (sp2.x() - sp1.x()) / len;
                    double dy = (sp2.y() - sp1.y()) / len;
                    double px = -dy * halfWidth;  // Perpendicular x
                    double py = dx * halfWidth;   // Perpendicular y

                    // Four corners of the slot body (going clockwise around the slot)
                    QPointF c1(sp1.x() + px, sp1.y() + py);  // p1 + perp
                    QPointF c2(sp2.x() + px, sp2.y() + py);  // p2 + perp
                    QPointF c3(sp2.x() - px, sp2.y() - py);  // p2 - perp
                    QPointF c4(sp1.x() - px, sp1.y() - py);  // p1 - perp

                    // Calculate angles for arc start points relative to circle centers
                    double startAngle2 = std::atan2(-(c2.y() - sp2.y()), c2.x() - sp2.x()) * 180.0 / M_PI;
                    double startAngle1 = std::atan2(-(c4.y() - sp1.y()), c4.x() - sp1.x()) * 180.0 / M_PI;

                    QPainterPath path;
                    path.moveTo(c1);
                    path.lineTo(c2);
                    QRectF arcRect2(sp2.x() - halfWidth, sp2.y() - halfWidth,
                                   halfWidth * 2, halfWidth * 2);
                    path.arcTo(arcRect2, startAngle2, 180);
                    path.lineTo(c4);
                    QRectF arcRect1(sp1.x() - halfWidth, sp1.y() - halfWidth,
                                   halfWidth * 2, halfWidth * 2);
                    path.arcTo(arcRect1, startAngle1, 180);
                    path.closeSubpath();
                    painter.drawPath(path);

                    // Draw construction line along the centerline of the slot
                    painter.save();
                    QPen constructionPen(QColor(128, 128, 128), 1, Qt::DashLine);
                    painter.setPen(constructionPen);
                    painter.setBrush(Qt::NoBrush);
                    painter.drawLine(sp1.toPoint(), sp2.toPoint());
                    painter.restore();

                    // Draw center points
                    painter.setBrush(QColor(0, 120, 215));
                    painter.drawEllipse(sp1.toPoint(), 3, 3);
                    painter.drawEllipse(sp2.toPoint(), 3, 3);

                    // In Overall mode, also show the actual endpoints
                    if (m_slotMode == SlotMode::Overall) {
                        QPointF end1 = worldToScreen(p1World);
                        QPointF end2 = worldToScreen(p2World);
                        painter.setBrush(QColor(255, 100, 100));
                        painter.drawEllipse(end1.toPoint(), 2, 2);
                        painter.drawEllipse(end2.toPoint(), 2, 2);
                    }

                    // Draw dimension label below the center line
                    double slotLength = (m_slotMode == SlotMode::Overall)
                        ? QLineF(p1World, p2World).length()       // Overall: endpoint to endpoint
                        : QLineF(center1, center2).length();      // CenterToCenter: center to center
                    if (slotLength > 0.1) {
                        drawPreviewDimension(painter, sp1.toPoint(), sp2.toPoint(), slotLength);
                    }
                }
            }
        }
        break;

    case SketchTool::Arc:
        if (!m_previewPoints.isEmpty()) {
            if (m_arcMode == ArcMode::ThreePoint) {
                // 3-point arc: draw through existing points and current mouse
                QVector<QPointF> pts = m_previewPoints;
                pts.append(m_currentMouseWorld);

                if (pts.size() == 2) {
                    // Just two points - draw a line preview
                    QPoint sp1 = worldToScreen(pts[0]);
                    QPoint sp2 = worldToScreen(pts[1]);
                    painter.drawLine(sp1, sp2);
                } else if (pts.size() >= 3) {
                    // Three points - calculate and draw arc
                    QPointF p1 = pts[0], p2 = pts[1], p3 = pts[2];

                    // Calculate circumcenter (center of circle through 3 points)
                    double ax = p1.x(), ay = p1.y();
                    double bx = p2.x(), by = p2.y();
                    double cx = p3.x(), cy = p3.y();

                    double d = 2 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by));
                    if (std::abs(d) > 0.0001) {
                        double ux = ((ax*ax + ay*ay) * (by - cy) + (bx*bx + by*by) * (cy - ay) + (cx*cx + cy*cy) * (ay - by)) / d;
                        double uy = ((ax*ax + ay*ay) * (cx - bx) + (bx*bx + by*by) * (ax - cx) + (cx*cx + cy*cy) * (bx - ax)) / d;
                        QPointF center(ux, uy);
                        double radius = QLineF(center, p1).length();

                        // Calculate angles
                        double angle1 = std::atan2(p1.y() - center.y(), p1.x() - center.x()) * 180.0 / M_PI;
                        double angle3 = std::atan2(p3.y() - center.y(), p3.x() - center.x()) * 180.0 / M_PI;

                        // Draw arc
                        QPoint sc = worldToScreen(center);
                        int rPx = static_cast<int>(radius * m_zoom);
                        QRect arcRect(sc.x() - rPx, sc.y() - rPx, rPx * 2, rPx * 2);

                        double sweep = angle3 - angle1;
                        if (sweep > 180) sweep -= 360;
                        if (sweep < -180) sweep += 360;

                        painter.drawArc(arcRect, static_cast<int>(-angle1 * 16), static_cast<int>(-sweep * 16));

                        // Draw the three points
                        painter.setBrush(QColor(0, 120, 215));
                        for (const QPointF& pt : pts) {
                            painter.drawEllipse(worldToScreen(pt), 3, 3);
                        }

                        // Draw arc length dimension
                        double arcLength = radius * std::abs(sweep * M_PI / 180.0);
                        if (arcLength > 0.1) {
                            // Position at the midpoint of the arc (offset outward from center)
                            double midAngle = std::atan2(p2.y() - center.y(), p2.x() - center.x());
                            double offsetDist = rPx + 20;
                            QPointF labelCenter = QPointF(sc) + QPointF(offsetDist * std::cos(midAngle),
                                                                         offsetDist * std::sin(midAngle));
                            drawDimensionLabel(painter, labelCenter, arcLength);
                        }
                    }
                }
            } else {
                // Tangent arc - show line from start to cursor
                QPoint sp1 = worldToScreen(m_previewPoints[0]);
                QPoint sp2 = worldToScreen(m_currentMouseWorld);
                painter.drawLine(sp1, sp2);
            }
        }
        break;

    case SketchTool::Polygon:
        if (!m_previewPoints.isEmpty()) {
            QPointF center = m_previewPoints[0];
            double radius = QLineF(center, m_currentMouseWorld).length();
            int sides = m_pendingEntity.sides > 0 ? m_pendingEntity.sides : 6;

            if (radius > 0.1) {
                QPoint sc = worldToScreen(center);
                int rPx = static_cast<int>(radius * m_zoom);

                // Calculate polygon vertices
                QPolygonF poly;
                double angleStep = 2.0 * M_PI / sides;
                double startAngle = std::atan2(m_currentMouseWorld.y() - center.y(),
                                               m_currentMouseWorld.x() - center.x());

                for (int i = 0; i < sides; ++i) {
                    double angle = startAngle + i * angleStep;
                    double x = sc.x() + rPx * std::cos(angle);
                    double y = sc.y() + rPx * std::sin(angle);
                    poly << QPointF(x, y);
                }
                poly << poly.first();  // Close the polygon

                painter.drawPolyline(poly);

                // Draw center point
                painter.setBrush(QColor(0, 120, 215));
                painter.drawEllipse(sc, 3, 3);

                // Draw radius dimension (center to first vertex)
                QPoint radiusEnd = worldToScreen(m_currentMouseWorld);
                drawPreviewDimension(painter, sc, radiusEnd, radius);
            }
        }
        break;

    case SketchTool::Ellipse:
        if (!m_previewPoints.isEmpty()) {
            QPointF center = m_previewPoints[0];
            QPointF edge = m_currentMouseWorld;
            double majorR = QLineF(center, edge).length();

            if (majorR > 0.1) {
                QPoint sc = worldToScreen(center);
                int majorPx = static_cast<int>(majorR * m_zoom);
                int minorPx = majorPx / 2;  // Default 2:1 aspect ratio for preview

                // For now, draw axis-aligned ellipse preview
                painter.drawEllipse(sc, majorPx, minorPx);

                // Draw center and edge points
                painter.setBrush(QColor(0, 120, 215));
                painter.drawEllipse(sc, 3, 3);
                QPoint edgeScreen = worldToScreen(edge);
                painter.drawEllipse(edgeScreen, 3, 3);

                // Draw major radius dimension
                drawPreviewDimension(painter, sc, edgeScreen, majorR);
            }
        }
        break;

    default:
        break;
    }
}

void SketchCanvas::drawSelectionHandles(QPainter& painter, const SketchEntity& entity)
{
    for (int i = 0; i < entity.points.size(); ++i) {
        QPoint p = worldToScreen(entity.points[i]);

        // Fixed handle (for arc slot resize) is drawn in red
        if (i == m_fixedHandleIndex && entity.type == SketchEntityType::Slot && entity.points.size() >= 3) {
            painter.setPen(QPen(QColor(200, 50, 50), 2));
            painter.setBrush(QColor(255, 150, 150));
        } else {
            painter.setPen(QPen(QColor(0, 120, 215), 1));
            painter.setBrush(Qt::white);
        }

        painter.drawRect(p.x() - 4, p.y() - 4, 8, 8);
    }
}

void SketchCanvas::drawPreviewDimension(QPainter& painter, const QPoint& p1, const QPoint& p2, double value)
{
    // Draw dimension label below the preview line during entity creation
    // Uses same style as instruction text (blue text on white background, no border)
    painter.save();

    // Calculate midpoint and line angle
    QPointF midPoint = (QPointF(p1) + QPointF(p2)) / 2.0;
    double dx = p2.x() - p1.x();
    double dy = p2.y() - p1.y();
    double angle = std::atan2(dy, dx) * 180.0 / M_PI;

    // Keep text readable - flip if pointing left
    bool flipped = (angle > 90 || angle < -90);
    if (flipped) {
        angle += 180;
    }

    // Format the dimension value
    QString dimText = QString::number(value, 'f', 2);

    // Set up font - same style as instruction labels
    painter.setPen(QColor(0, 120, 215));
    QFont font = painter.font();
    font.setPointSize(9);
    painter.setFont(font);

    QFontMetrics fm(font);
    QRect textRect = fm.boundingRect(dimText);

    // Position below the line (positive Y offset in rotated coords)
    painter.translate(midPoint);
    painter.rotate(angle);

    // Offset below the line
    QPointF offset(0, textRect.height() + 6);
    QRectF labelRect(-textRect.width() / 2.0 - 3, offset.y() - textRect.height(),
                     textRect.width() + 6, textRect.height() + 2);

    // Draw background for readability (no border, matching instruction text style)
    painter.fillRect(labelRect, QColor(255, 255, 255, 200));

    // Draw the text
    painter.drawText(labelRect, Qt::AlignCenter, dimText);

    painter.restore();
}

void SketchCanvas::drawDimensionLabel(QPainter& painter, const QPointF& position, double value)
{
    // Draw a dimension label at a specific position (for arc-based dimensions)
    // Uses same style as instruction text (blue text on white background, no border)
    painter.save();

    QString dimText = QString::number(value, 'f', 2);
    painter.setPen(QColor(0, 120, 215));
    QFont font = painter.font();
    font.setPointSize(9);
    painter.setFont(font);

    QFontMetrics fm(font);
    QRect textRect = fm.boundingRect(dimText);

    QRectF labelRect(position.x() - textRect.width() / 2.0 - 3,
                     position.y() - textRect.height() / 2.0 - 1,
                     textRect.width() + 6, textRect.height() + 2);

    painter.fillRect(labelRect, QColor(255, 255, 255, 200));
    painter.drawText(labelRect, Qt::AlignCenter, dimText);

    painter.restore();
}

void SketchCanvas::drawSnapIndicator(QPainter& painter, const SnapPoint& snap)
{
    QPoint screenPos = worldToScreen(snap.position);
    painter.save();

    // Choose color and shape based on snap type
    QColor snapColor(255, 140, 0);  // Orange for snap indicators
    int size = 6;

    painter.setPen(QPen(snapColor, 2));
    painter.setBrush(Qt::NoBrush);

    switch (snap.type) {
    case SnapType::Endpoint:
        // Square for endpoints
        painter.drawRect(screenPos.x() - size, screenPos.y() - size, size * 2, size * 2);
        break;

    case SnapType::Midpoint:
        // Triangle for midpoints
        {
            QPolygon triangle;
            triangle << QPoint(screenPos.x(), screenPos.y() - size)
                     << QPoint(screenPos.x() - size, screenPos.y() + size)
                     << QPoint(screenPos.x() + size, screenPos.y() + size);
            painter.drawPolygon(triangle);
        }
        break;

    case SnapType::Center:
        // Circle with cross for centers
        painter.drawEllipse(screenPos, size, size);
        painter.drawLine(screenPos.x() - size, screenPos.y(), screenPos.x() + size, screenPos.y());
        painter.drawLine(screenPos.x(), screenPos.y() - size, screenPos.x(), screenPos.y() + size);
        break;

    case SnapType::Quadrant:
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

    case SnapType::ArcEndCenter:
        // Circle for arc end centers (slot endpoints)
        painter.drawEllipse(screenPos, size, size);
        break;

    case SnapType::Intersection:
        // X for intersections
        painter.drawLine(screenPos.x() - size, screenPos.y() - size,
                         screenPos.x() + size, screenPos.y() + size);
        painter.drawLine(screenPos.x() - size, screenPos.y() + size,
                         screenPos.x() + size, screenPos.y() - size);
        break;
    }

    painter.restore();
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
            // Check if already drawing (click-click mode: second click to finish)
            if (m_isDrawing) {
                // Arc Slot (both modes): add points on click
                bool isArcSlot = (m_activeTool == SketchTool::Slot &&
                                  (m_slotMode == SlotMode::ArcRadius || m_slotMode == SlotMode::ArcEnds));
                if (isArcSlot) {
                    // Reset drag detection for this click (so we can detect drag from THIS point)
                    m_drawStartPos = event->pos();
                    m_wasDragged = false;

                    QPointF snapped = snapPoint(worldPos);

                    // For ArcRadius mode, constrain the 3rd point (end) to the arc
                    // Point order: arc center (0), start (1), end (2)
                    if (m_slotMode == SlotMode::ArcRadius && m_pendingEntity.points.size() == 2) {
                        // Constrain end point to arc radius
                        QPointF arcCenterWorld = m_pendingEntity.points[0];
                        QPointF startWorld = m_pendingEntity.points[1];
                        double arcRadius = QLineF(arcCenterWorld, startWorld).length();
                        double mouseAngle = std::atan2(snapped.y() - arcCenterWorld.y(),
                                                       snapped.x() - arcCenterWorld.x());
                        double startAngle = std::atan2(startWorld.y() - arcCenterWorld.y(),
                                                       startWorld.x() - arcCenterWorld.x());

                        // Minimum angular separation so slot ends don't overlap
                        // Arc length between centers must be >= 2 * slot radius
                        double slotRadius = m_pendingEntity.radius;
                        if (slotRadius < 0.1) slotRadius = 5.0;
                        double minAngularSep = (arcRadius > 0.001) ? (2.0 * slotRadius / arcRadius) : 0.1;

                        // Calculate angular difference
                        double angleDiff = mouseAngle - startAngle;
                        // Normalize to [-PI, PI]
                        while (angleDiff > M_PI) angleDiff -= 2 * M_PI;
                        while (angleDiff < -M_PI) angleDiff += 2 * M_PI;

                        // Clamp to minimum separation
                        if (std::abs(angleDiff) < minAngularSep) {
                            // Push to minimum distance in same direction
                            double sign = (angleDiff >= 0) ? 1.0 : -1.0;
                            mouseAngle = startAngle + sign * minAngularSep;
                        }

                        snapped = arcCenterWorld + QPointF(arcRadius * std::cos(mouseAngle),
                                                           arcRadius * std::sin(mouseAngle));
                    }

                    m_pendingEntity.points.append(snapped);
                    m_previewPoints.append(snapped);
                    if (m_pendingEntity.points.size() >= 3) {
                        // m_arcSlotFlipped is already set via Shift key toggle
                        finishEntity();
                    } else {
                        update();
                    }
                    return;
                }

                // 3-point rectangle: add points on click
                bool isThreePointRect = (m_activeTool == SketchTool::Rectangle &&
                                         m_rectMode == RectMode::ThreePoint);
                if (isThreePointRect) {
                    // Reset drag detection for this click (so we can detect drag from THIS point)
                    m_drawStartPos = event->pos();
                    m_wasDragged = false;

                    QPointF snapped = snapPoint(worldPos);
                    m_pendingEntity.points.append(snapped);
                    m_previewPoints.append(snapped);
                    if (m_pendingEntity.points.size() >= 3) {
                        finishEntity();
                    } else {
                        update();
                    }
                    return;
                }

                // For two-point tools, second click finishes the entity
                // (Arc and Spline have their own multi-click logic in mouseReleaseEvent)
                // (Arc slots and 3-point rectangles are handled above and return early)
                // (Point finishes on release, not second click)
                bool isMultiClickTool = (m_activeTool == SketchTool::Arc) ||
                                        (m_activeTool == SketchTool::Spline) ||
                                        (m_activeTool == SketchTool::Point);
                if (!isMultiClickTool) {
                    // Update the endpoint to clicked position and finish
                    updateEntity(snapPoint(worldPos));
                    finishEntity();
                    return;
                }
            }

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
                m_drawStartPos = event->pos();
                m_wasDragged = false;
                startEntity(snapPoint(worldPos));
            }
        }
    }
}

void SketchCanvas::mouseMoveEvent(QMouseEvent* event)
{
    QPointF worldPos = screenToWorld(event->pos());
    bool ctrlHeld = event->modifiers() & Qt::ControlModifier;
    bool altHeld = event->modifiers() & Qt::AltModifier;

    // Alt disables entity snapping temporarily
    // Otherwise entity snapping is always active
    m_angleSnapActive = false;
    if (altHeld) {
        // Free form - no entity snap
        m_currentMouseWorld = worldPos;
        // Still apply grid snap if enabled
        if (m_snapToGrid) {
            double snappedX = qRound(m_currentMouseWorld.x() / m_gridSpacing) * m_gridSpacing;
            double snappedY = qRound(m_currentMouseWorld.y() / m_gridSpacing) * m_gridSpacing;
            m_currentMouseWorld = {snappedX, snappedY};
        }
        m_activeSnap.reset();
    } else {
        // Entity snapping active
        m_currentMouseWorld = snapPoint(worldPos);
    }

    // Ctrl enables angle snapping (45° increments) during line-based entity creation
    if (ctrlHeld && m_isDrawing && !m_previewPoints.isEmpty()) {
        if (m_activeTool == SketchTool::Line ||
            m_activeTool == SketchTool::Rectangle ||
            (m_activeTool == SketchTool::Slot &&
             (m_slotMode == SlotMode::CenterToCenter || m_slotMode == SlotMode::Overall))) {
            m_currentMouseWorld = snapToAngle(m_previewPoints[0], m_currentMouseWorld);
        }
    }

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
            } else if (sel->type == SketchEntityType::Slot && sel->points.size() >= 3) {
                // Arc slot with 3 points: arc center, start, end
                // Storage format: points[0] = arc center, points[1] = start, points[2] = end
                // Modifiers for endpoint dragging (index 1 or 2):
                //   Normal: constrain to arc radius (slide along arc)
                //   Ctrl: snap to 45-degree intervals on arc
                //   Shift: fix other endpoint, resize arc (center moves)
                //   Alt: free move endpoint, resize arc (center moves)
                // Dragging arc center (point 0) moves the whole slot
                bool altPressed = (event->modifiers() & Qt::AltModifier);

                if (m_dragHandleIndex == 0) {
                    // Dragging arc center - move all points together
                    QPointF delta = finalPos - sel->points[0];
                    sel->points[0] = finalPos;
                    sel->points[1] += delta;
                    sel->points[2] += delta;
                } else if (shiftPressed) {
                    // Shift+drag endpoint - fix the OTHER endpoint, resize arc
                    // The arc center moves to maintain the arc through both points
                    QPointF draggedPt = finalPos;
                    int otherIdx = (m_dragHandleIndex == 1) ? 2 : 1;
                    QPointF fixedPt = sel->points[otherIdx];  // This point stays fixed

                    // Calculate new arc center: must be equidistant from both points
                    // and on the perpendicular bisector of the chord
                    QPointF chordMid = (draggedPt + fixedPt) / 2.0;
                    QPointF chordDir = fixedPt - draggedPt;
                    double chordLen = std::sqrt(chordDir.x() * chordDir.x() + chordDir.y() * chordDir.y());

                    if (chordLen > 1e-6) {
                        QPointF perpDir(-chordDir.y() / chordLen, chordDir.x() / chordLen);

                        // Project old center onto the new perpendicular bisector
                        // to find where the new center should be
                        QPointF oldCenter = sel->points[0];
                        QPointF toOldCenter = oldCenter - chordMid;
                        double perpDist = toOldCenter.x() * perpDir.x() + toOldCenter.y() * perpDir.y();

                        // Ensure minimum distance to avoid degenerate arc
                        double minDist = 0.1;
                        if (std::abs(perpDist) < minDist) {
                            perpDist = (perpDist >= 0) ? minDist : -minDist;
                        }

                        // Update dragged point and center (fixed point stays)
                        sel->points[m_dragHandleIndex] = draggedPt;
                        sel->points[0] = chordMid + perpDir * perpDist;
                    } else {
                        sel->points[m_dragHandleIndex] = finalPos;
                    }
                } else if (altPressed) {
                    // Alt+drag endpoint - free move, resize arc radius
                    // Move the arc center to maintain the new radius while keeping
                    // the center on its perpendicular bisector line
                    QPointF draggedPt = finalPos;
                    int otherIdx = (m_dragHandleIndex == 1) ? 2 : 1;
                    QPointF otherPt = sel->points[otherIdx];

                    // New chord midpoint (between dragged point and other endpoint)
                    QPointF chordMid = (draggedPt + otherPt) / 2.0;

                    // Perpendicular direction to the new chord
                    QPointF chordDir = otherPt - draggedPt;
                    double chordLen = std::sqrt(chordDir.x() * chordDir.x() + chordDir.y() * chordDir.y());

                    if (chordLen > 1e-6) {
                        QPointF perpDir(-chordDir.y() / chordLen, chordDir.x() / chordLen);

                        // New arc radius is distance from new position to arc center
                        double newRadius = QLineF(sel->points[0], draggedPt).length();

                        // Calculate where the center should be on the perpendicular bisector
                        // to achieve this radius: radius^2 = (chordLen/2)^2 + perpDist^2
                        double halfChord = chordLen / 2.0;
                        double perpDistSq = newRadius * newRadius - halfChord * halfChord;

                        if (perpDistSq > 0) {
                            double perpDist = std::sqrt(perpDistSq);

                            // Preserve which side of chord the center is on
                            QPointF oldToCenter = sel->points[0] - chordMid;
                            double oldProjDist = oldToCenter.x() * perpDir.x() + oldToCenter.y() * perpDir.y();
                            if (oldProjDist < 0) perpDist = -perpDist;

                            // Update all points
                            sel->points[m_dragHandleIndex] = draggedPt;
                            sel->points[0] = chordMid + perpDir * perpDist;
                        } else {
                            // Radius too small for chord - just move endpoint
                            sel->points[m_dragHandleIndex] = draggedPt;
                        }
                    } else {
                        sel->points[m_dragHandleIndex] = finalPos;
                    }
                } else if (m_fixedHandleIndex >= 0 && m_fixedHandleIndex != m_dragHandleIndex &&
                           (m_fixedHandleIndex == 1 || m_fixedHandleIndex == 2)) {
                    // A fixed point is set - resize arc keeping the fixed point in place
                    QPointF draggedPt = finalPos;
                    QPointF fixedPt = sel->points[m_fixedHandleIndex];

                    // Calculate new arc center on the perpendicular bisector
                    QPointF chordMid = (draggedPt + fixedPt) / 2.0;
                    QPointF chordDir = fixedPt - draggedPt;
                    double chordLen = std::sqrt(chordDir.x() * chordDir.x() + chordDir.y() * chordDir.y());

                    if (chordLen > 1e-6) {
                        QPointF perpDir(-chordDir.y() / chordLen, chordDir.x() / chordLen);

                        // Project old center onto the new perpendicular bisector
                        QPointF oldCenter = sel->points[0];
                        QPointF toOldCenter = oldCenter - chordMid;
                        double perpDist = toOldCenter.x() * perpDir.x() + toOldCenter.y() * perpDir.y();

                        // Ensure minimum distance to avoid degenerate arc
                        double minDist = 0.1;
                        if (std::abs(perpDist) < minDist) {
                            perpDist = (perpDist >= 0) ? minDist : -minDist;
                        }

                        // Ctrl snaps to 45-degree intervals relative to the arc center
                        if (ctrlPressed) {
                            QPointF newCenter = chordMid + perpDir * perpDist;
                            QPointF dir = draggedPt - newCenter;
                            double angle = std::atan2(dir.y(), dir.x());
                            const double snapAngle = M_PI / 4.0;
                            angle = std::round(angle / snapAngle) * snapAngle;
                            double radius = QLineF(newCenter, fixedPt).length();
                            draggedPt = newCenter + QPointF(radius * std::cos(angle), radius * std::sin(angle));

                            // Recalculate chord and center with snapped point
                            chordMid = (draggedPt + fixedPt) / 2.0;
                            chordDir = fixedPt - draggedPt;
                            chordLen = std::sqrt(chordDir.x() * chordDir.x() + chordDir.y() * chordDir.y());
                            if (chordLen > 1e-6) {
                                perpDir = QPointF(-chordDir.y() / chordLen, chordDir.x() / chordLen);
                                toOldCenter = oldCenter - chordMid;
                                perpDist = toOldCenter.x() * perpDir.x() + toOldCenter.y() * perpDir.y();
                                if (std::abs(perpDist) < minDist) {
                                    perpDist = (perpDist >= 0) ? minDist : -minDist;
                                }
                            }
                        }

                        // Update dragged point and center (fixed point stays)
                        sel->points[m_dragHandleIndex] = draggedPt;
                        sel->points[0] = chordMid + perpDir * perpDist;
                    } else {
                        sel->points[m_dragHandleIndex] = finalPos;
                    }
                } else {
                    // Normal drag - constrain to arc radius
                    // Ctrl snaps to 45-degree intervals
                    QPointF center = sel->points[0];
                    int otherIdx = (m_dragHandleIndex == 1) ? 2 : 1;
                    double arcRadius = QLineF(center, sel->points[otherIdx]).length();

                    QPointF dir = finalPos - center;
                    double len = std::sqrt(dir.x() * dir.x() + dir.y() * dir.y());
                    if (len > 1e-6 && arcRadius > 1e-6) {
                        double angle = std::atan2(dir.y(), dir.x());

                        // Ctrl snaps to 45-degree intervals
                        if (ctrlPressed) {
                            const double snapAngle = M_PI / 4.0;  // 45 degrees
                            angle = std::round(angle / snapAngle) * snapAngle;
                        }

                        // Place point on arc at the (possibly snapped) angle
                        sel->points[m_dragHandleIndex] = center + QPointF(
                            arcRadius * std::cos(angle),
                            arcRadius * std::sin(angle));
                    } else {
                        sel->points[m_dragHandleIndex] = finalPos;
                    }
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

        // Check if we've moved enough to consider this a drag (5 pixels threshold)
        if (!m_wasDragged) {
            QPointF delta = event->pos() - m_drawStartPos;
            if (delta.manhattanLength() > 5.0) {
                m_wasDragged = true;
            }
        }
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
            // Point tool: finish immediately on release
            if (m_activeTool == SketchTool::Point) {
                // Update point position to release location and finish
                QPointF worldPos = screenToWorld(event->pos());
                m_pendingEntity.points[0] = snapPoint(worldPos);
                finishEntity();
                return;
            }

            // Multi-click tools: arc (3-point), 3-point rectangle, arc slot, and spline
            if (m_activeTool == SketchTool::Arc && m_arcMode == ArcMode::ThreePoint) {
                if (m_pendingEntity.points.size() < 3) {
                    // Add another point and continue
                    QPointF worldPos = screenToWorld(event->pos());
                    QPointF snapped = snapPoint(worldPos);
                    m_pendingEntity.points.append(snapped);
                    m_previewPoints.append(snapped);  // Also update preview points
                    if (m_pendingEntity.points.size() >= 3) {
                        // Have all 3 points, can finish now
                        finishEntity();
                    } else {
                        update();
                    }
                } else {
                    finishEntity();
                }
            } else if (m_activeTool == SketchTool::Slot &&
                       (m_slotMode == SlotMode::ArcRadius || m_slotMode == SlotMode::ArcEnds)) {
                // Arc slot: supports both click-click-click and click-drag modes
                if (m_wasDragged) {
                    // User dragged - add the release point
                    QPointF worldPos = screenToWorld(event->pos());
                    QPointF snapped = snapPoint(worldPos);

                    // For ArcRadius mode, constrain the point to the arc if this is the 3rd point
                    if (m_slotMode == SlotMode::ArcRadius && m_pendingEntity.points.size() == 2) {
                        QPointF arcCenterWorld = m_pendingEntity.points[0];
                        QPointF startWorld = m_pendingEntity.points[1];
                        double arcRadius = QLineF(arcCenterWorld, startWorld).length();
                        double mouseAngle = std::atan2(snapped.y() - arcCenterWorld.y(),
                                                       snapped.x() - arcCenterWorld.x());
                        snapped = arcCenterWorld + QPointF(arcRadius * std::cos(mouseAngle),
                                                           arcRadius * std::sin(mouseAngle));
                    }

                    m_pendingEntity.points.append(snapped);
                    m_previewPoints.append(snapped);
                    if (m_pendingEntity.points.size() >= 3) {
                        finishEntity();
                    } else {
                        update();
                    }
                } else {
                    // User clicked without dragging - point already added on press
                    // Just update the display
                    update();
                }
            } else if (m_activeTool == SketchTool::Rectangle &&
                       m_rectMode == RectMode::ThreePoint) {
                // 3-point rectangle: supports both click-click-click and click-drag modes
                if (m_wasDragged) {
                    // User dragged - add the release point
                    QPointF worldPos = screenToWorld(event->pos());
                    QPointF snapped = snapPoint(worldPos);
                    m_pendingEntity.points.append(snapped);
                    m_previewPoints.append(snapped);
                    if (m_pendingEntity.points.size() >= 3) {
                        finishEntity();
                    } else {
                        update();
                    }
                } else {
                    // User clicked without dragging - point already added on press
                    // Just update the display
                    update();
                }
            } else if (m_activeTool == SketchTool::Spline) {
                // Spline: add point and continue (finish with right-click or Enter)
                QPointF worldPos = screenToWorld(event->pos());
                QPointF snapped = snapPoint(worldPos);
                m_pendingEntity.points.append(snapped);
                m_previewPoints.append(snapped);  // Also update preview points
                update();
                // Don't finish - user needs to right-click or press Enter
            } else {
                // Two-point tools (Line, Rectangle, Circle, Slot, Ellipse, Polygon)
                // Support both click-drag and click-click modes
                if (m_wasDragged) {
                    // User dragged - finish the entity now
                    finishEntity();
                } else {
                    // User clicked without dragging - wait for second click
                    // The entity is already started, just continue showing preview
                    // Second click will come through mousePressEvent which will
                    // call finishEntity since m_isDrawing is already true
                }
            }
        }
    }
}

void SketchCanvas::wheelEvent(QWheelEvent* event)
{
    // During drawing, scroll wheel adjusts entity parameters
    if (m_isDrawing) {
        int delta = event->angleDelta().y() > 0 ? 1 : -1;

        switch (m_activeTool) {
        case SketchTool::Slot:
            // Adjust slot width (radius) by 1mm per scroll step
            m_pendingEntity.radius = qMax(1.0, m_pendingEntity.radius + delta);
            update();
            event->accept();
            return;

        case SketchTool::Polygon:
            // Adjust number of sides (3 to 64)
            m_pendingEntity.sides = qBound(3, m_pendingEntity.sides + delta, 64);
            update();
            event->accept();
            return;

        default:
            break;
        }
    }

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
    } else if (sel->type == SketchEntityType::Slot && sel->points.size() >= 3) {
        // Arc slot with 3 points: arc center, start, end
        // Note: applyCtrlSnapToHandle doesn't have access to current modifiers,
        // so Alt+drag resize is only handled in mouseMoveEvent
        if (m_dragHandleIndex == 0) {
            // Dragging arc center - move all points together
            QPointF delta = finalPos - sel->points[0];
            sel->points[0] = finalPos;
            sel->points[1] += delta;
            sel->points[2] += delta;
        } else {
            // Dragging start or end point - constrain to arc radius
            // Ctrl snaps to 45-degree intervals
            QPointF center = sel->points[0];
            int otherIdx = (m_dragHandleIndex == 1) ? 2 : 1;
            double arcRadius = QLineF(center, sel->points[otherIdx]).length();

            QPointF dir = finalPos - center;
            double len = std::sqrt(dir.x() * dir.x() + dir.y() * dir.y());
            if (len > 1e-6 && arcRadius > 1e-6) {
                double angle = std::atan2(dir.y(), dir.x());

                // Ctrl snaps to 45-degree intervals
                if (m_ctrlWasPressed) {
                    const double snapAngle = M_PI / 4.0;  // 45 degrees
                    angle = std::round(angle / snapAngle) * snapAngle;
                }

                sel->points[m_dragHandleIndex] = center + QPointF(
                    arcRadius * std::cos(angle),
                    arcRadius * std::sin(angle));
            } else {
                sel->points[m_dragHandleIndex] = finalPos;
            }
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
    // Shift key during arc slot drawing - toggle flip state and update preview
    if (event->key() == Qt::Key_Shift && m_isDrawing &&
        m_activeTool == SketchTool::Slot &&
        (m_slotMode == SlotMode::ArcRadius || m_slotMode == SlotMode::ArcEnds) &&
        m_previewPoints.size() >= 2) {
        m_arcSlotFlipped = !m_arcSlotFlipped;
        update();
        return;
    }

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
        // Shift released during arc slot drawing - update preview immediately
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

    // Check if right-clicking on a handle of an arc slot
    const SketchEntity* sel = selectedEntity();
    if (sel && sel->type == SketchEntityType::Slot && sel->points.size() >= 3) {
        int handleIdx = hitTestHandle(worldPos);
        // Only allow fixing endpoint handles (1 or 2), not arc center (0)
        // Storage: points[0] = arc center, points[1] = start, points[2] = end
        if (handleIdx == 1 || handleIdx == 2) {
            QMenu menu(this);

            if (m_fixedHandleIndex == handleIdx) {
                QAction* unfixAction = menu.addAction(tr("Unfix Point"));
                connect(unfixAction, &QAction::triggered, this, [this]() {
                    m_fixedHandleIndex = -1;
                    update();
                });
            } else {
                // Only one handle can be fixed at a time
                QString actionText = (m_fixedHandleIndex >= 0)
                    ? tr("Fix This Point Instead")
                    : tr("Fix Point for Resize");
                QAction* fixAction = menu.addAction(actionText);
                connect(fixAction, &QAction::triggered, this, [this, handleIdx]() {
                    m_fixedHandleIndex = handleIdx;
                    update();
                });
            }

            menu.exec(event->globalPos());
            return;
        }
    }

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

    // Push undo commands for deleted entities (in reverse order for proper undo)
    for (int i = m_entities.size() - 1; i >= 0; --i) {
        if (toDelete.contains(m_entities[i].id)) {
            UndoCommand cmd;
            cmd.type = UndoCommandType::DeleteEntity;
            cmd.entity = m_entities[i];
            pushUndoCommand(cmd);
        }
    }

    // Push undo commands for deleted constraints
    for (int i = m_constraints.size() - 1; i >= 0; --i) {
        bool shouldDelete = false;
        for (int id : m_constraints[i].entityIds) {
            if (toDelete.contains(id)) {
                shouldDelete = true;
                break;
            }
        }
        if (shouldDelete) {
            UndoCommand cmd;
            cmd.type = UndoCommandType::DeleteConstraint;
            cmd.constraint = m_constraints[i];
            pushUndoCommand(cmd);
        }
    }

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

    case SketchEntityType::Slot:
        // Use library function via conversion
        return toLibraryEntity(entity).containsPoint(worldPos, tolerance);
        break;

    case SketchEntityType::Arc:
        if (entity.points.size() >= 3) {
            // Arc defined by three points - check if near the arc curve
            QPointF start = entity.points[0];
            QPointF mid = entity.points[1];
            QPointF end = entity.points[2];

            // Calculate arc center and radius from three points
            double ax = start.x(), ay = start.y();
            double bx = mid.x(), by = mid.y();
            double cx = end.x(), cy = end.y();

            double d = 2.0 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by));
            if (qAbs(d) < 0.0001) {
                // Points are collinear - treat as line
                return false;
            }

            double ux = ((ax * ax + ay * ay) * (by - cy) + (bx * bx + by * by) * (cy - ay) + (cx * cx + cy * cy) * (ay - by)) / d;
            double uy = ((ax * ax + ay * ay) * (cx - bx) + (bx * bx + by * by) * (ax - cx) + (cx * cx + cy * cy) * (bx - ax)) / d;
            QPointF center(ux, uy);
            double radius = QLineF(center, start).length();

            // Check if point is near the arc radius
            double dist = QLineF(center, worldPos).length();
            if (qAbs(dist - radius) > tolerance) return false;

            // Check if point is within the arc's angular range
            double startAngle = std::atan2(start.y() - center.y(), start.x() - center.x());
            double midAngle = std::atan2(mid.y() - center.y(), mid.x() - center.x());
            double endAngle = std::atan2(end.y() - center.y(), end.x() - center.x());
            double testAngle = std::atan2(worldPos.y() - center.y(), worldPos.x() - center.x());

            // Normalize angles to determine arc direction and containment
            auto normalizeAngle = [](double a) {
                while (a < 0) a += 2 * M_PI;
                while (a >= 2 * M_PI) a -= 2 * M_PI;
                return a;
            };

            startAngle = normalizeAngle(startAngle);
            midAngle = normalizeAngle(midAngle);
            endAngle = normalizeAngle(endAngle);
            testAngle = normalizeAngle(testAngle);

            // Check if midAngle is between start and end going one way or the other
            auto isBetween = [](double test, double a1, double a2) {
                if (a1 <= a2) {
                    return test >= a1 && test <= a2;
                } else {
                    return test >= a1 || test <= a2;
                }
            };

            // The arc goes from start through mid to end
            bool midInForward = isBetween(midAngle, startAngle, endAngle);
            if (midInForward) {
                return isBetween(testAngle, startAngle, endAngle);
            } else {
                return isBetween(testAngle, endAngle, startAngle);
            }
        }
        break;

    case SketchEntityType::Ellipse:
        if (!entity.points.isEmpty()) {
            QPointF center = entity.points[0];
            double majorR = entity.majorRadius;
            double minorR = entity.minorRadius;
            if (majorR < 0.001 || minorR < 0.001) return false;

            // Transform point to unit circle space and check distance
            double dx = worldPos.x() - center.x();
            double dy = worldPos.y() - center.y();
            double normalized = (dx * dx) / (majorR * majorR) + (dy * dy) / (minorR * minorR);
            // On ellipse outline if normalized is close to 1
            return qAbs(normalized - 1.0) < (tolerance / qMin(majorR, minorR));
        }
        break;

    case SketchEntityType::Polygon:
        if (!entity.points.isEmpty() && entity.sides >= 3) {
            QPointF center = entity.points[0];
            double radius = entity.radius;
            if (radius < 0.001) return false;

            // Check distance to each edge of the polygon
            double angleStep = 2.0 * M_PI / entity.sides;
            for (int i = 0; i < entity.sides; ++i) {
                double a1 = i * angleStep - M_PI / 2;
                double a2 = (i + 1) * angleStep - M_PI / 2;
                QPointF v1(center.x() + radius * std::cos(a1), center.y() + radius * std::sin(a1));
                QPointF v2(center.x() + radius * std::cos(a2), center.y() + radius * std::sin(a2));

                // Distance from point to line segment
                QPointF d = v2 - v1;
                double len = QLineF(v1, v2).length();
                if (len < 0.001) continue;
                double t = QPointF::dotProduct(worldPos - v1, d) / (len * len);
                t = qBound(0.0, t, 1.0);
                QPointF closest = v1 + t * d;
                if (QLineF(closest, worldPos).length() < tolerance) return true;
            }
        }
        break;

    case SketchEntityType::Spline:
        if (entity.points.size() >= 2) {
            // Check distance to each segment of the spline
            // (Simplified - treats spline as polyline through control points)
            for (int i = 0; i < entity.points.size() - 1; ++i) {
                QPointF p1 = entity.points[i];
                QPointF p2 = entity.points[i + 1];
                QPointF d = p2 - p1;
                double len = QLineF(p1, p2).length();
                if (len < 0.001) continue;
                double t = QPointF::dotProduct(worldPos - p1, d) / (len * len);
                t = qBound(0.0, t, 1.0);
                QPointF closest = p1 + t * d;
                if (QLineF(closest, worldPos).length() < tolerance) return true;
            }
        }
        break;

    case SketchEntityType::Text:
        if (!entity.points.isEmpty()) {
            // Simple bounding box hit test for text
            QPointF pos = entity.points[0];
            // Approximate text bounds (would need actual font metrics for accuracy)
            double textWidth = entity.text.length() * 8.0;  // Rough estimate
            double textHeight = 16.0;
            QRectF textRect(pos.x(), pos.y() - textHeight, textWidth, textHeight);
            return textRect.contains(worldPos);
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
    m_arcSlotFlipped = false;  // Reset flip state for new arc slot

    m_pendingEntity = SketchEntity();
    m_pendingEntity.id = nextEntityId();
    m_pendingEntity.points.append(pos);

    switch (m_activeTool) {
    case SketchTool::Point:
        m_pendingEntity.type = SketchEntityType::Point;
        // Point is placed on mouse release, not immediately
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
        m_pendingEntity.radius = 5.0;  // Default slot half-width
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
        if (m_pendingEntity.points.size() > 1) {
            m_pendingEntity.points[1] = pos;
        } else {
            m_pendingEntity.points.append(pos);
        }
        break;

    case SketchTool::Rectangle:
        if (m_rectMode == RectMode::Center) {
            // Center mode: point[0] is center, compute opposite corners
            if (!m_pendingEntity.points.isEmpty()) {
                QPointF center = m_pendingEntity.points[0];
                // Compute delta from center to mouse position
                QPointF delta = pos - center;
                // Opposite corner mirrors across center
                QPointF corner1 = center - delta;
                QPointF corner2 = pos;
                // Store as corner-to-corner (points[0] and points[1] are opposite corners)
                if (m_pendingEntity.points.size() > 2) {
                    m_pendingEntity.points[1] = corner1;
                    m_pendingEntity.points[2] = corner2;
                } else if (m_pendingEntity.points.size() > 1) {
                    m_pendingEntity.points[1] = corner1;
                    m_pendingEntity.points.append(corner2);
                } else {
                    m_pendingEntity.points.append(corner1);
                    m_pendingEntity.points.append(corner2);
                }
            }
        } else if (m_rectMode == RectMode::ThreePoint) {
            // 3-Point mode: point[0] is first corner, point[1] is second corner (defines edge),
            // point[2] is the perpendicular offset (defines width)
            // Points are added on click/release, here we just update m_currentMouseWorld
            // which is used by drawPreview() to show where the next point will be placed.
            // Don't modify m_pendingEntity.points here - that breaks click-click mode.
        } else {
            // Corner mode: standard corner-to-corner
            if (m_pendingEntity.points.size() > 1) {
                m_pendingEntity.points[1] = pos;
            } else {
                m_pendingEntity.points.append(pos);
            }
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

    case SketchTool::Slot:
        if (m_slotMode == SlotMode::ArcRadius || m_slotMode == SlotMode::ArcEnds) {
            // Arc slot needs 3 points
            // Points are added on click/release, here we just update m_currentMouseWorld
            // which is used by drawPreview() to show where the next point will be placed.
            // Don't modify m_pendingEntity.points here - that breaks click-click mode.
        } else {
            // Linear slot (CenterToCenter or Overall) - two endpoints
            if (m_pendingEntity.points.size() > 1) {
                m_pendingEntity.points[1] = pos;
            } else {
                m_pendingEntity.points.append(pos);
            }
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
        valid = m_pendingEntity.points.size() >= 2 &&
                QLineF(m_pendingEntity.points[0], m_pendingEntity.points[1]).length() > 0.1;
        break;
    case SketchEntityType::Rectangle:
        // For center mode, we have 3 points: [center, corner1, corner2]
        // Convert to standard 2-point corner format [corner1, corner2]
        if (m_rectMode == RectMode::Center && m_pendingEntity.points.size() >= 3) {
            QPointF corner1 = m_pendingEntity.points[1];
            QPointF corner2 = m_pendingEntity.points[2];
            m_pendingEntity.points.clear();
            m_pendingEntity.points.append(corner1);
            m_pendingEntity.points.append(corner2);
        } else if (m_rectMode == RectMode::ThreePoint && m_pendingEntity.points.size() >= 3) {
            // 3-point angled rectangle: [p1, p2, p3] where p1-p2 is first edge
            // and p3 defines the perpendicular offset (width)
            QPointF p1 = m_pendingEntity.points[0];
            QPointF p2 = m_pendingEntity.points[1];
            QPointF p3 = m_pendingEntity.points[2];

            // Calculate edge direction and perpendicular
            QPointF edge = p2 - p1;
            double edgeLen = QLineF(p1, p2).length();
            if (edgeLen > 0.01) {
                QPointF edgeDir = edge / edgeLen;
                QPointF perpDir(-edgeDir.y(), edgeDir.x());

                // Project p3 onto perpendicular to get width
                QPointF toP3 = p3 - p1;
                double perpDist = toP3.x() * perpDir.x() + toP3.y() * perpDir.y();

                // Calculate all four corners: p1, p2, p2+perp, p1+perp
                QPointF c1 = p1;
                QPointF c2 = p2;
                QPointF c3 = p2 + perpDir * perpDist;
                QPointF c4 = p1 + perpDir * perpDist;

                // Store as 4-point polygon-style rectangle for proper rendering
                // The drawing code will handle this as a rotated rectangle
                m_pendingEntity.points.clear();
                m_pendingEntity.points.append(c1);
                m_pendingEntity.points.append(c2);
                m_pendingEntity.points.append(c3);
                m_pendingEntity.points.append(c4);
            }
        }
        // Validate: need at least 2 points with some distance
        // For 3-point mode, we now have 4 points (all corners)
        if (m_pendingEntity.points.size() == 4) {
            // 4-point rotated rectangle
            valid = QLineF(m_pendingEntity.points[0], m_pendingEntity.points[1]).length() > 0.1;
        } else {
            valid = m_pendingEntity.points.size() >= 2 &&
                    QLineF(m_pendingEntity.points[0], m_pendingEntity.points[1]).length() > 0.1;
        }
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
        if (m_slotMode == SlotMode::ArcRadius) {
            // Arc slot (Radius mode): points are arc center, start, end (constrained)
            // Storage format: points[0] = arc center, points[1] = start, points[2] = end
            if (m_pendingEntity.points.size() >= 3) {
                QPointF arcCenter = m_pendingEntity.points[0];
                QPointF start = m_pendingEntity.points[1];
                QPointF end = m_pendingEntity.points[2];

                // Project end point onto arc radius (same distance from center as start)
                double arcRadius = QLineF(arcCenter, start).length();
                double endDist = QLineF(arcCenter, end).length();
                if (endDist > 0.001 && arcRadius > 0.001) {
                    double scale = arcRadius / endDist;
                    end = arcCenter + (end - arcCenter) * scale;
                }

                // Points already in correct order: arc center, start, end
                m_pendingEntity.points[2] = end;  // Update projected end
                m_pendingEntity.arcFlipped = m_arcSlotFlipped;
                valid = true;
            }
            // Radius was set during startEntity or adjusted via scroll wheel
        } else if (m_slotMode == SlotMode::ArcEnds) {
            // Arc slot (Ends mode): points are start, end, arc center
            // Both endpoints stay fixed; arc center is constrained to perpendicular bisector
            // Reorder to storage format: points[0] = arc center, points[1] = start, points[2] = end
            if (m_pendingEntity.points.size() >= 3) {
                QPointF start = m_pendingEntity.points[0];
                QPointF end = m_pendingEntity.points[1];
                QPointF arcCenter = m_pendingEntity.points[2];

                // Constrain arc center to perpendicular bisector of start-end
                QPointF midpoint = (start + end) / 2.0;
                QPointF startToEnd = end - start;
                double chordLen = QLineF(start, end).length();

                if (chordLen > 0.001) {
                    QPointF perpDir(-startToEnd.y() / chordLen, startToEnd.x() / chordLen);
                    QPointF toCenter = arcCenter - midpoint;
                    double projDist = toCenter.x() * perpDir.x() + toCenter.y() * perpDir.y();
                    arcCenter = midpoint + perpDir * projDist;
                }

                // Calculate arc radius (equidistant from both endpoints)
                double arcRadius = QLineF(arcCenter, start).length();

                // Enforce minimum angular separation
                double slotRadius = m_pendingEntity.radius;
                if (slotRadius < 0.1) slotRadius = 5.0;
                double minAngularSep = (arcRadius > 0.001) ? (2.0 * slotRadius / arcRadius) : 0.1;

                double startAngleRad = std::atan2(start.y() - arcCenter.y(), start.x() - arcCenter.x());
                double endAngleRad = std::atan2(end.y() - arcCenter.y(), end.x() - arcCenter.x());
                double angleDiff = endAngleRad - startAngleRad;
                while (angleDiff > M_PI) angleDiff -= 2 * M_PI;
                while (angleDiff < -M_PI) angleDiff += 2 * M_PI;

                // If endpoints are too close angularly, push arc center further out
                if (std::abs(angleDiff) < minAngularSep && chordLen > 0.001) {
                    double halfChord = chordLen / 2.0;
                    double requiredRadius = halfChord / std::sin(minAngularSep / 2.0);
                    if (requiredRadius > arcRadius) {
                        QPointF toCenter = arcCenter - midpoint;
                        double toCenterLen = QLineF(midpoint, arcCenter).length();
                        if (toCenterLen > 0.001) {
                            double newDist = std::sqrt(requiredRadius * requiredRadius - halfChord * halfChord);
                            arcCenter = midpoint + toCenter * (newDist / toCenterLen);
                            arcRadius = requiredRadius;
                        }
                    }
                }

                // Reorder to: arc center, start, end
                m_pendingEntity.points[0] = arcCenter;
                m_pendingEntity.points[1] = start;
                m_pendingEntity.points[2] = end;
                m_pendingEntity.arcFlipped = m_arcSlotFlipped;
                valid = true;
            }
            // Radius was set during startEntity or adjusted via scroll wheel
        } else if (m_slotMode == SlotMode::Overall) {
            // Overall mode: user clicked endpoints, convert to centers
            valid = m_pendingEntity.points.size() >= 2 &&
                    QLineF(m_pendingEntity.points[0], m_pendingEntity.points[1]).length() > 0.1;
            if (valid) {
                // Use user-adjusted radius (set during startEntity, adjusted via scroll wheel)
                double radius = m_pendingEntity.radius;

                // Convert endpoints to arc centers (move inward by radius)
                QPointF p1 = m_pendingEntity.points[0];
                QPointF p2 = m_pendingEntity.points[1];
                double len = QLineF(p1, p2).length();
                if (len > radius * 2) {
                    double dx = (p2.x() - p1.x()) / len;
                    double dy = (p2.y() - p1.y()) / len;
                    m_pendingEntity.points[0] = QPointF(p1.x() + dx * radius, p1.y() + dy * radius);
                    m_pendingEntity.points[1] = QPointF(p2.x() - dx * radius, p2.y() - dy * radius);
                } else {
                    // Slot too short for the radius, just use endpoints
                }
            }
        } else {
            // CenterToCenter mode (default): points are arc centers
            valid = m_pendingEntity.points.size() >= 2 &&
                    QLineF(m_pendingEntity.points[0], m_pendingEntity.points[1]).length() > 0.1;
            // Radius was set during startEntity or adjusted via scroll wheel
        }
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

        // Push undo command for entity creation
        UndoCommand cmd;
        cmd.type = UndoCommandType::AddEntity;
        cmd.entity = m_pendingEntity;
        pushUndoCommand(cmd);

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

// ---- Undo/Redo -------------------------------------------------------

void SketchCanvas::pushUndoCommand(const UndoCommand& cmd)
{
    // Clear redo stack when new action is performed
    if (!m_redoStack.isEmpty()) {
        m_redoStack.clear();
        emit redoAvailabilityChanged(false);
    }

    // Add to undo stack
    m_undoStack.append(cmd);

    // Limit stack size
    while (m_undoStack.size() > MaxUndoStackSize) {
        m_undoStack.removeFirst();
    }

    updateUndoRedoState();
}

void SketchCanvas::updateUndoRedoState()
{
    emit undoAvailabilityChanged(!m_undoStack.isEmpty());
    emit redoAvailabilityChanged(!m_redoStack.isEmpty());
}

void SketchCanvas::undo()
{
    if (m_undoStack.isEmpty()) return;

    UndoCommand cmd = m_undoStack.takeLast();

    switch (cmd.type) {
    case UndoCommandType::AddEntity:
        // Undo add = delete the entity
        for (int i = 0; i < m_entities.size(); ++i) {
            if (m_entities[i].id == cmd.entity.id) {
                m_entities.removeAt(i);
                break;
            }
        }
        m_selectedIds.remove(cmd.entity.id);
        if (m_selectedId == cmd.entity.id) {
            m_selectedId = -1;
        }
        m_profilesCacheDirty = true;
        break;

    case UndoCommandType::DeleteEntity:
        // Undo delete = restore the entity
        m_entities.append(cmd.entity);
        m_profilesCacheDirty = true;
        break;

    case UndoCommandType::ModifyEntity:
        // Undo modify = restore previous state
        for (int i = 0; i < m_entities.size(); ++i) {
            if (m_entities[i].id == cmd.entity.id) {
                m_entities[i] = cmd.previousEntity;
                break;
            }
        }
        m_profilesCacheDirty = true;
        break;

    case UndoCommandType::AddConstraint:
        // Undo add = delete the constraint
        for (int i = 0; i < m_constraints.size(); ++i) {
            if (m_constraints[i].id == cmd.constraint.id) {
                m_constraints.removeAt(i);
                break;
            }
        }
        if (m_selectedConstraintId == cmd.constraint.id) {
            m_selectedConstraintId = -1;
        }
        break;

    case UndoCommandType::DeleteConstraint:
        // Undo delete = restore the constraint
        m_constraints.append(cmd.constraint);
        break;

    case UndoCommandType::ModifyConstraint:
        // Undo modify = restore previous state
        for (int i = 0; i < m_constraints.size(); ++i) {
            if (m_constraints[i].id == cmd.constraint.id) {
                m_constraints[i] = cmd.previousConstraint;
                break;
            }
        }
        break;
    }

    // Move to redo stack
    m_redoStack.append(cmd);

    updateUndoRedoState();
    update();
}

void SketchCanvas::redo()
{
    if (m_redoStack.isEmpty()) return;

    UndoCommand cmd = m_redoStack.takeLast();

    switch (cmd.type) {
    case UndoCommandType::AddEntity:
        // Redo add = add the entity back
        m_entities.append(cmd.entity);
        m_profilesCacheDirty = true;
        break;

    case UndoCommandType::DeleteEntity:
        // Redo delete = delete the entity again
        for (int i = 0; i < m_entities.size(); ++i) {
            if (m_entities[i].id == cmd.entity.id) {
                m_entities.removeAt(i);
                break;
            }
        }
        m_selectedIds.remove(cmd.entity.id);
        if (m_selectedId == cmd.entity.id) {
            m_selectedId = -1;
        }
        m_profilesCacheDirty = true;
        break;

    case UndoCommandType::ModifyEntity:
        // Redo modify = apply the modification again
        for (int i = 0; i < m_entities.size(); ++i) {
            if (m_entities[i].id == cmd.entity.id) {
                m_entities[i] = cmd.entity;
                break;
            }
        }
        m_profilesCacheDirty = true;
        break;

    case UndoCommandType::AddConstraint:
        // Redo add = add the constraint back
        m_constraints.append(cmd.constraint);
        break;

    case UndoCommandType::DeleteConstraint:
        // Redo delete = delete the constraint again
        for (int i = 0; i < m_constraints.size(); ++i) {
            if (m_constraints[i].id == cmd.constraint.id) {
                m_constraints.removeAt(i);
                break;
            }
        }
        if (m_selectedConstraintId == cmd.constraint.id) {
            m_selectedConstraintId = -1;
        }
        break;

    case UndoCommandType::ModifyConstraint:
        // Redo modify = apply the modification again
        for (int i = 0; i < m_constraints.size(); ++i) {
            if (m_constraints[i].id == cmd.constraint.id) {
                m_constraints[i] = cmd.constraint;
                break;
            }
        }
        break;
    }

    // Move back to undo stack
    m_undoStack.append(cmd);

    updateUndoRedoState();
    update();
}

}  // namespace hobbycad
