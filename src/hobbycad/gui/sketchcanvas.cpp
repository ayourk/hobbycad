// =====================================================================
//  src/hobbycad/gui/sketchcanvas.cpp — 2D Sketch canvas widget
// =====================================================================

#include "sketchcanvas.h"
#include "bindingsdialog.h"
#include "sketchsolver.h"
#include "sketchutils.h"

#include <hobbycad/parameters.h>
#include <hobbycad/sketch/decomposition.h>
#include <hobbycad/sketch/export.h>
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
#include <QFileDialog>
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

    // Expression evaluator for formula input in dimension fields
    m_paramEngine = new ParameterEngine();

    // Load key bindings from settings
    loadKeyBindings();
}

SketchCanvas::~SketchCanvas()
{
    delete m_paramEngine;
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

        // Clear tangent targets when switching tools
        m_tangentTargets.clear();

        // Leave any entered group when switching tools
        if (m_enteredGroupId >= 0)
            m_enteredGroupId = -1;

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
            setCursor(Qt::ArrowCursor);
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
            setCursor(Qt::ArrowCursor);
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

    // Clear any in-progress state when switching modes
    m_tangentTargets.clear();
    if (m_isDrawing) {
        cancelEntity();
    }

    // Check based on active tool to interpret the mode correctly
    switch (m_activeTool) {
    case SketchTool::Line:
        // Line modes: 0=TwoPoint, 1=Horizontal, 2=Vertical, 3=Tangent, 4=Construction
        switch (modeValue) {
        case 0: m_lineMode = LineMode::TwoPoint; break;
        case 1: m_lineMode = LineMode::Horizontal; break;
        case 2: m_lineMode = LineMode::Vertical; break;
        case 3: m_lineMode = LineMode::Tangent; break;
        case 4: m_lineMode = LineMode::Construction; break;
        default: m_lineMode = LineMode::TwoPoint; break;
        }
        break;

    case SketchTool::Arc:
        // Arc modes: 0=ThreePoint, 1=CenterStartEnd, 2=StartEndRadius, 3=Tangent
        // Note: Tangent arc validation is done in FullModeWindow before this is called
        switch (modeValue) {
        case 0: m_arcMode = ArcMode::ThreePoint; break;
        case 1: m_arcMode = ArcMode::CenterStartEnd; break;
        case 2: m_arcMode = ArcMode::StartEndRadius; break;
        case 3: m_arcMode = ArcMode::Tangent; break;
        default: m_arcMode = ArcMode::ThreePoint; break;
        }
        break;

    case SketchTool::Rectangle:
        // Rectangle modes: 0=Corner, 1=Center, 2=ThreePoint, 3=Parallelogram
        switch (modeValue) {
        case 0: m_rectMode = RectMode::Corner; break;
        case 1: m_rectMode = RectMode::Center; break;
        case 2: m_rectMode = RectMode::ThreePoint; break;
        case 3: m_rectMode = RectMode::Parallelogram; break;
        default: m_rectMode = RectMode::Corner; break;
        }
        break;

    case SketchTool::Circle:
        // Circle modes: 0=CenterRadius, 1=TwoPoint (diameter), 2=ThreePoint
        switch (modeValue) {
        case 0: m_circleMode = CircleMode::CenterRadius; break;
        case 1: m_circleMode = CircleMode::TwoPoint; break;
        case 2: m_circleMode = CircleMode::ThreePoint; break;
        default: m_circleMode = CircleMode::CenterRadius; break;
        }
        break;

    case SketchTool::Polygon:
        // Polygon modes: 0=Inscribed, 1=Circumscribed, 2=Freeform
        switch (modeValue) {
        case 0: m_polygonMode = PolygonMode::Inscribed; break;
        case 1: m_polygonMode = PolygonMode::Circumscribed; break;
        case 2: m_polygonMode = PolygonMode::Freeform; break;
        default: m_polygonMode = PolygonMode::Inscribed; break;
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

void SketchCanvas::setDisplayUnit(LengthUnit unit)
{
    m_displayUnit = unit;
    update();
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

// -----------------------------------------------------------------------
//  Group-aware selection expansion
// -----------------------------------------------------------------------
// If any entity in a decomposition group is selected, select all sibling
// entities in that group.  This ensures that window/crossing and single-
// click selection treats decomposed shapes (rectangles, polygons) as a
// cohesive unit.
void SketchCanvas::expandSelectionToGroups()
{
    // When inside a group, don't expand — we want individual selection
    if (m_enteredGroupId >= 0)
        return;

    // Collect group IDs that contain at least one selected entity
    QSet<int> touchedGroups;
    for (const auto& entity : m_entities) {
        if (entity.selected && entity.groupId >= 0) {
            touchedGroups.insert(entity.groupId);
        }
    }

    if (touchedGroups.isEmpty())
        return;

    // Select every entity that belongs to one of those groups
    for (auto& entity : m_entities) {
        if (!entity.selected && entity.groupId >= 0 &&
            touchedGroups.contains(entity.groupId)) {
            entity.selected = true;
            m_selectedIds.insert(entity.id);
            m_selectedId = entity.id;
        }
    }
}

// -----------------------------------------------------------------------
//  Enter / Leave group
// -----------------------------------------------------------------------
void SketchCanvas::enterGroup(int groupId)
{
    // Verify the group exists
    bool found = false;
    for (const SketchGroup& g : m_groups) {
        if (g.id == groupId) { found = true; break; }
    }
    if (!found) return;

    m_enteredGroupId = groupId;

    // Clear current selection — user will click individual members next
    clearSelection();
    update();
}

void SketchCanvas::leaveGroup()
{
    if (m_enteredGroupId < 0) return;

    int prevGroup = m_enteredGroupId;
    m_enteredGroupId = -1;

    // Select the whole group again so the user sees what they were in
    for (auto& entity : m_entities) {
        if (entity.groupId == prevGroup) {
            entity.selected = true;
            m_selectedIds.insert(entity.id);
            m_selectedId = entity.id;
        }
    }

    emit selectionChanged(m_selectedId);
    update();
}

void SketchCanvas::selectEntity(int entityId, bool addToSelection,
                                bool individualOnly)
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

    // Add/toggle entity selection.
    //
    // When inside a group (m_enteredGroupId >= 0):
    //   - clicking a member of the entered group selects it individually
    //   - clicking something outside the group leaves the group first
    //
    // When NOT inside a group:
    //   - normal click selects the entity + its group siblings
    //   - individualOnly (Alt+click) selects only the single entity
    SketchEntity* entity = entityById(entityId);
    if (entity) {
        // If inside a group and clicking outside it, leave the group first
        if (m_enteredGroupId >= 0 && entity->groupId != m_enteredGroupId) {
            leaveGroup();
            // leaveGroup() selects the whole group and emits; clear that
            // so we can do a fresh selection of the clicked entity below
            for (auto& e : m_entities) e.selected = false;
            m_selectedIds.clear();
            m_selectedId = -1;
        }

        // Determine whether this click should be individual
        bool isIndividual = individualOnly || (m_enteredGroupId >= 0);

        if (addToSelection && entity->selected) {
            // Ctrl+click on already selected entity — deselect it and
            // its group siblings (unless individual mode).
            entity->selected = false;
            m_selectedIds.remove(entityId);
            if (!isIndividual && entity->groupId >= 0) {
                for (auto& e : m_entities) {
                    if (e.groupId == entity->groupId) {
                        e.selected = false;
                        m_selectedIds.remove(e.id);
                    }
                }
            }
            // Update primary selection
            if (m_selectedId == entityId) {
                m_selectedId = m_selectedIds.isEmpty() ? -1 : *m_selectedIds.begin();
            }
        } else {
            entity->selected = true;
            m_selectedIds.insert(entityId);
            m_selectedId = entityId;  // Primary selection is the last clicked
            // Expand to group siblings unless in individual mode
            if (!isIndividual) {
                expandSelectionToGroups();
            }
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

    // Group-aware expansion: if any entity in a decomposition group was
    // selected, select all siblings in that group so the user doesn't have
    // to precisely enclose every segment of a decomposed rectangle/polygon.
    expandSelectionToGroups();

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
        description += QStringLiteral(" = ") + formatValueWithUnit(c->value, m_displayUnit);
        break;
    case ConstraintType::Radius:
        description += QStringLiteral(" R") + formatValueWithUnit(c->value, m_displayUnit);
        break;
    case ConstraintType::Diameter:
        description += QStringLiteral(" Ø") + formatValueWithUnit(c->value, m_displayUnit);
        break;
    case ConstraintType::Angle:
        description += QStringLiteral(" = ") + formatAngle(c->value);
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

QPointF SketchCanvas::worldToScreenF(const QPointF& world) const
{
    // Same as worldToScreen but returns floating-point for sub-pixel precision
    double wx = (world.x() - m_viewCenter.x()) * m_zoom;
    double wy = (world.y() - m_viewCenter.y()) * m_zoom;

    double rad = qDegreesToRadians(m_viewRotation);
    double cosR = qCos(rad);
    double sinR = qSin(rad);
    double rx = wx * cosR - wy * sinR;
    double ry = wx * sinR + wy * cosR;

    return {rx + width() / 2.0, -ry + height() / 2.0};
}

// Snap weight by type.  Higher weight = stronger pull (more gravity).
// Exact geometric points get high weight so they aren't eclipsed by
// nearby perimeter/axis snaps.  The effective distance used for
// comparison is:  rawDistance / weight.
QPointF SketchCanvas::snapPoint(const QPointF& world) const
{
    // Clear any previous snap (const cast needed for mutable snap state)
    auto* self = const_cast<SketchCanvas*>(this);
    self->m_activeSnap.reset();

    if (m_snapToEntities) {
        double tolerance = m_entitySnapTolerance / m_zoom;  // Convert pixels to world units
        int excludeId = m_isDraggingHandle ? m_selectedId : -1;

        // Delegate to the library for all snap evaluation
        QVector<sketch::Entity> libEntities = toLibraryEntities(m_entities);
        sketch::SnapResult result = sketch::findBestSnap(
            libEntities, world, tolerance, excludeId);

        if (result.found) {
            self->m_activeSnap = result.snap;
            return result.snap.position;
        }
    }

    // If no entity snap, try grid snap
    if (m_snapToGrid) {
        double snappedX = qRound(world.x() / m_gridSpacing) * m_gridSpacing;
        double snappedY = qRound(world.y() / m_gridSpacing) * m_gridSpacing;
        return {snappedX, snappedY};
    }

    return world;
}


QPointF SketchCanvas::snapToAngle(const QPointF& origin, const QPointF& target) const
{
    // Snap to nearest 45-degree increment (0, 45, 90, 135, 180, 225, 270, 315)
    auto* self = const_cast<SketchCanvas*>(this);

    double distance = QLineF(origin, target).length();
    if (distance < 0.001) {
        self->m_angleSnapActive = false;
        return target;
    }

    double snappedAngle;
    QPointF result = geometry::snapToAngleIncrementWithAngle(origin, target, 45.0, snappedAngle);

    self->m_angleSnapActive = true;
    self->m_snappedAngle = snappedAngle;

    return result;
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

    // Draw pre-click preview dot for entity creation tools (before first click)
    if (!m_isDrawing && m_activeTool != SketchTool::Select) {
        // Get current cursor position
        QPoint cursorPos = mapFromGlobal(QCursor::pos());
        if (rect().contains(cursorPos)) {
            QPointF cursorWorld = screenToWorld(cursorPos);

            // For tangent arc, project onto entity
            if (m_activeTool == SketchTool::Arc && m_arcMode == ArcMode::Tangent && m_tangentTargets.isEmpty()) {
                int hitId = const_cast<SketchCanvas*>(this)->hitTest(cursorWorld);
                if (hitId >= 0) {
                    const SketchEntity* hoverEntity = nullptr;
                    for (const auto& e : m_entities) {
                        if (e.id == hitId) {
                            hoverEntity = &e;
                            break;
                        }
                    }
                    if (hoverEntity && (hoverEntity->type == SketchEntityType::Line ||
                                        hoverEntity->type == SketchEntityType::Rectangle)) {
                        QPointF projectedPoint = cursorWorld;
                        bool altHeld = QGuiApplication::queryKeyboardModifiers() & Qt::AltModifier;

                        if (hoverEntity->type == SketchEntityType::Line && hoverEntity->points.size() >= 2) {
                            QPointF p1 = hoverEntity->points[0];
                            QPointF p2 = hoverEntity->points[1];
                            projectedPoint = geometry::closestPointOnLine(cursorWorld, p1, p2);
                            if (!altHeld) {
                                QPointF midpoint = (p1 + p2) / 2.0;
                                double snapDist = 10.0 / m_zoom;
                                if (QLineF(projectedPoint, p1).length() < snapDist) {
                                    projectedPoint = p1;
                                } else if (QLineF(projectedPoint, p2).length() < snapDist) {
                                    projectedPoint = p2;
                                } else if (QLineF(projectedPoint, midpoint).length() < snapDist) {
                                    projectedPoint = midpoint;
                                }
                            }
                        }
                        cursorWorld = projectedPoint;
                    }
                }
            } else {
                // For other tools, apply snapping
                cursorWorld = snapPoint(cursorWorld);
            }

            // Draw the preview dot
            QPoint screenPoint = worldToScreen(cursorWorld);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(0, 120, 215));
            painter.drawEllipse(screenPoint, 5, 5);
        }
    }

    // Draw active snap point indicator
    if (m_activeSnap && (m_isDrawing || m_isDraggingHandle)) {
        drawSnapIndicator(painter, m_activeSnap.value());
    }

    // Draw constraints (dimensions)
    drawConstraints(painter);

    // Draw selection handles
    if (auto* sel = selectedEntity()) {
        // If the primary entity is part of a group and the whole group is
        // selected, draw handles for all unique corner points across every
        // entity in the group (e.g. 4 corners of a decomposed rectangle).
        if (sel->groupId >= 0 && m_enteredGroupId < 0) {
            QVector<QPointF> uniquePts;
            const double eps2 = 1e-6;
            for (const auto& e : m_entities) {
                if (e.groupId != sel->groupId) continue;
                for (const QPointF& pt : e.points) {
                    bool dup = false;
                    for (const QPointF& u : uniquePts) {
                        double dx = pt.x() - u.x();
                        double dy = pt.y() - u.y();
                        if (dx * dx + dy * dy < eps2) { dup = true; break; }
                    }
                    if (!dup) uniquePts.append(pt);
                }
            }
            painter.setPen(QPen(QColor(0, 120, 215), 1));
            painter.setBrush(Qt::white);
            for (const QPointF& pt : uniquePts) {
                QPoint p = worldToScreen(pt);
                painter.drawRect(p.x() - 4, p.y() - 4, 8, 8);
            }
        } else {
            drawSelectionHandles(painter, *sel);
        }
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

    // Draw entered-group bounding box (KiCad-style visual feedback)
    if (m_enteredGroupId >= 0) {
        // Compute the bounding rect of all entities in the entered group
        QRectF groupBounds;
        bool first = true;
        for (const auto& e : m_entities) {
            if (e.groupId != m_enteredGroupId) continue;
            for (const QPointF& pt : e.points) {
                QRectF ptRect(pt, QSizeF(0, 0));
                if (first) { groupBounds = ptRect; first = false; }
                else        groupBounds = groupBounds.united(ptRect);
            }
            if ((e.type == SketchEntityType::Circle || e.type == SketchEntityType::Arc) &&
                !e.points.isEmpty()) {
                QPointF c = e.points[0];
                groupBounds = groupBounds.united(
                    QRectF(c.x() - e.radius, c.y() - e.radius,
                           e.radius * 2, e.radius * 2));
            }
        }
        if (!first) {
            // Add padding in world units
            double pad = 6.0 / m_zoom;
            groupBounds.adjust(-pad, -pad, pad, pad);

            QPointF tl = worldToScreen(groupBounds.topLeft());
            QPointF br = worldToScreen(groupBounds.bottomRight());
            QRectF screenRect = QRectF(tl, br).normalized();

            // Slightly different shade — dashed border with translucent fill
            painter.setPen(QPen(QColor(0, 120, 215, 160), 1.5, Qt::DashLine));
            painter.setBrush(QColor(0, 120, 215, 15));
            painter.drawRect(screenRect);

            // Label in top-left corner
            QString groupName;
            for (const SketchGroup& g : m_groups) {
                if (g.id == m_enteredGroupId) { groupName = g.name; break; }
            }
            if (!groupName.isEmpty()) {
                QFont labelFont = painter.font();
                labelFont.setPointSize(8);
                painter.setFont(labelFont);
                painter.setPen(QColor(0, 100, 180, 200));
                painter.drawText(screenRect.left() + 4,
                                 screenRect.top() - 3,
                                 tr("Editing: %1").arg(groupName));
            }
        }
    }

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

    case SketchEntityType::Parallelogram:
        if (entity.points.size() >= 4) {
            // 4-point parallelogram
            QPoint c1 = worldToScreen(entity.points[0]);
            QPoint c2 = worldToScreen(entity.points[1]);
            QPoint c3 = worldToScreen(entity.points[2]);
            QPoint c4 = worldToScreen(entity.points[3]);
            painter.drawLine(c1, c2);
            painter.drawLine(c2, c3);
            painter.drawLine(c3, c4);
            painter.drawLine(c4, c1);
        }
        break;

    case SketchEntityType::Circle:
        if (!entity.points.isEmpty()) {
            QPointF centerF = worldToScreenF(entity.points[0]);
            double r = entity.radius * m_zoom;
            painter.drawEllipse(centerF, r, r);

            // Draw center point marker (small cross)
            painter.save();
            int crossSize = 4;
            painter.drawLine(QPointF(centerF.x() - crossSize, centerF.y()), QPointF(centerF.x() + crossSize, centerF.y()));
            painter.drawLine(QPointF(centerF.x(), centerF.y() - crossSize), QPointF(centerF.x(), centerF.y() + crossSize));

            // Draw perimeter point markers for clicked points (points[1+] are perimeter points)
            for (int i = 1; i < entity.points.size(); ++i) {
                QPointF pt = worldToScreenF(entity.points[i]);
                painter.drawLine(QPointF(pt.x() - crossSize, pt.y()), QPointF(pt.x() + crossSize, pt.y()));
                painter.drawLine(QPointF(pt.x(), pt.y() - crossSize), QPointF(pt.x(), pt.y() + crossSize));
            }
            painter.restore();
        }
        break;

    case SketchEntityType::Arc:
        if (!entity.points.isEmpty()) {
            QPointF centerF = worldToScreenF(entity.points[0]);
            double r = entity.radius * m_zoom;
            QRectF arcRect(centerF.x() - r, centerF.y() - r, r * 2.0, r * 2.0);
            // Use QPainterPath for floating-point precision
            QPainterPath arcPath;
            arcPath.arcMoveTo(arcRect, entity.startAngle);
            arcPath.arcTo(arcRect, entity.startAngle, entity.sweepAngle);
            painter.drawPath(arcPath);

            // Draw center point marker (small cross)
            painter.save();
            int crossSize = 4;
            painter.drawLine(QPointF(centerF.x() - crossSize, centerF.y()), QPointF(centerF.x() + crossSize, centerF.y()));
            painter.drawLine(QPointF(centerF.x(), centerF.y() - crossSize), QPointF(centerF.x(), centerF.y() + crossSize));
            painter.restore();
        }
        break;

    case SketchEntityType::Polygon:
        // Polygons are decomposed into Lines + Circle at creation time;
        // no committed Polygon entities exist in development builds.
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
            QPointF centerF = worldToScreenF(entity.points[0]);
            double majorR = entity.majorRadius * m_zoom;
            double minorR = entity.minorRadius * m_zoom;
            // For now, draw axis-aligned ellipse
            painter.drawEllipse(centerF, majorR, minorR);
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
            painter.drawText(mid, formatValueWithUnit(dist, m_displayUnit));
        }
        break;
    }
}

void SketchCanvas::drawPreview(QPainter& painter)
{
    QPen pen(QColor(0, 120, 215), 2, Qt::DashLine);
    // Construction line mode uses construction geometry color
    if (m_activeTool == SketchTool::Line && m_lineMode == LineMode::Construction) {
        pen.setColor(QColor(180, 100, 50));  // Construction geometry color
    }
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    switch (m_activeTool) {
    case SketchTool::Line:
        if (!m_previewPoints.isEmpty()) {
            QPoint p1 = worldToScreen(m_previewPoints[0]);
            // Use constrained endpoint from updateEntity when dims are locked
            QPointF lineEndWorld = (m_pendingEntity.points.size() >= 2)
                ? m_pendingEntity.points[1] : m_currentMouseWorld;
            QPoint p2 = worldToScreen(lineEndWorld);
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
                QString angleText = formatAngle(m_snappedAngle);
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
            double length = QLineF(m_previewPoints[0], lineEndWorld).length();
            if (length > 0.1) {
                if (m_dimActiveIndex >= 0 && m_dimFields.size() >= 2) {
                    // Update live values for dim fields (use constrained endpoint)
                    double dx = lineEndWorld.x() - m_previewPoints[0].x();
                    double dy = lineEndWorld.y() - m_previewPoints[0].y();
                    double angleDeg = std::atan2(dy, dx) * 180.0 / M_PI;
                    m_dimFields[0].currentValue = length;  // Length
                    m_dimFields[1].currentValue = angleDeg; // Angle

                    // Position: midpoint below line, rotated to follow edge
                    QPointF midPoint = (QPointF(p1) + QPointF(p2)) / 2.0;
                    double screenAngle = std::atan2(p2.y() - p1.y(), p2.x() - p1.x()) * 180.0 / M_PI;
                    bool flipped = (screenAngle > 90 || screenAngle < -90);
                    if (flipped) screenAngle += 180;

                    // Length field: below the line at midpoint
                    QPointF lengthPos = midPoint + QPointF(0, 18);
                    drawDimInputField(painter, lengthPos, 0, screenAngle);

                    // Angle field: further below
                    QPointF anglePos = midPoint + QPointF(0, 38);
                    drawDimInputField(painter, anglePos, 1, screenAngle);
                } else {
                    drawPreviewDimension(painter, p1, p2, length);
                }
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

                    // Draw edge length + angle dim input fields
                    double edgeLen = QLineF(p1, p2).length();
                    if (edgeLen > 0.1) {
                        if (m_dimActiveIndex >= 0 && m_dimFields.size() >= 2) {
                            double dx = p2.x() - p1.x();
                            double dy = p2.y() - p1.y();
                            double angleDeg = std::atan2(dy, dx) * 180.0 / M_PI;
                            m_dimFields[0].currentValue = edgeLen;
                            m_dimFields[1].currentValue = angleDeg;

                            QPointF midPt = (QPointF(sp1) + QPointF(sp2)) / 2.0;
                            double screenAngle = std::atan2(sp2.y() - sp1.y(), sp2.x() - sp1.x()) * 180.0 / M_PI;
                            bool flipped = (screenAngle > 90 || screenAngle < -90);
                            if (flipped) screenAngle += 180;

                            drawDimInputField(painter, midPt + QPointF(0, 18), 0, screenAngle);
                            drawDimInputField(painter, midPt + QPointF(0, 38), 1, screenAngle);
                        } else {
                            drawPreviewDimension(painter, sp1, sp2, edgeLen);
                        }
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
                        drawPreviewDimension(painter, sc1, sc2, edgeLen);  // Fixed edge from stage 1
                        double width = std::abs(perpDist);
                        if (width > 0.1) {
                            if (m_dimActiveIndex >= 0 && m_dimFields.size() >= 1) {
                                m_dimFields[0].currentValue = width;
                                QPointF midEdge2 = (QPointF(sc2) + QPointF(sc3)) / 2.0;
                                double screenAngle2 = std::atan2(sc3.y() - sc2.y(), sc3.x() - sc2.x()) * 180.0 / M_PI;
                                bool flipped2 = (screenAngle2 > 90 || screenAngle2 < -90);
                                if (flipped2) screenAngle2 += 180;
                                drawDimInputField(painter, midEdge2 + QPointF(0, 18), 0, screenAngle2);
                            } else {
                                drawPreviewDimension(painter, sc2, sc3, width);
                            }
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
            } else if (m_rectMode == RectMode::Parallelogram) {
                // Parallelogram mode: p1 -> p2 -> p3 -> p4, where p4 = p1 + (p3 - p2)
                QPointF p1 = m_previewPoints[0];
                QPointF p2 = (m_previewPoints.size() >= 2) ? m_previewPoints[1] : m_currentMouseWorld;
                QPointF p3 = m_currentMouseWorld;

                if (m_previewPoints.size() < 2) {
                    // Still defining first edge - just draw the line
                    QPoint sp1 = worldToScreen(p1);
                    QPoint sp2 = worldToScreen(p2);
                    painter.drawLine(sp1, sp2);

                    // Draw edge length and angle dimensions
                    double edgeLen = QLineF(p1, p2).length();
                    if (edgeLen > 0.1) {
                        if (m_dimActiveIndex >= 0 && m_dimFields.size() >= 2) {
                            double dx = p2.x() - p1.x();
                            double dy = p2.y() - p1.y();
                            double angleDeg = std::atan2(dy, dx) * 180.0 / M_PI;
                            m_dimFields[0].currentValue = edgeLen;
                            m_dimFields[1].currentValue = angleDeg;

                            QPointF midPt = (QPointF(sp1) + QPointF(sp2)) / 2.0;
                            double screenAngle = std::atan2(sp2.y() - sp1.y(), sp2.x() - sp1.x()) * 180.0 / M_PI;
                            bool flipped = (screenAngle > 90 || screenAngle < -90);
                            if (flipped) screenAngle += 180;

                            drawDimInputField(painter, midPt + QPointF(0, 18), 0, screenAngle);
                            drawDimInputField(painter, midPt + QPointF(0, 38), 1, screenAngle);
                        } else {
                            drawPreviewDimension(painter, sp1, sp2, edgeLen);
                        }
                    }
                } else {
                    // Have two corners, now defining third (and computing fourth)
                    QPointF p4 = p1 + (p3 - p2);  // Complete the parallelogram

                    double edge1Len = QLineF(p1, p2).length();
                    double edge2Len = QLineF(p2, p3).length();

                    // Calculate the INSIDE angle at p2 (angle between rays p2->p1 and p2->p3)
                    QPointF toP1 = p1 - p2;  // Vector from p2 to p1
                    QPointF toP3 = p3 - p2;  // Vector from p2 to p3
                    double insideAngleDeg = 0.0;
                    if (edge1Len > 0.001 && edge2Len > 0.001) {
                        double dot = toP1.x() * toP3.x() + toP1.y() * toP3.y();
                        double cosAngle = dot / (edge1Len * edge2Len);
                        cosAngle = qBound(-1.0, cosAngle, 1.0);
                        insideAngleDeg = std::acos(cosAngle) * 180.0 / M_PI;
                    }

                    // Draw the parallelogram
                    QPoint sp1 = worldToScreen(p1);
                    QPoint sp2 = worldToScreen(p2);
                    QPoint sp3 = worldToScreen(p3);
                    QPoint sp4 = worldToScreen(p4);

                    painter.drawLine(sp1, sp2);
                    painter.drawLine(sp2, sp3);
                    painter.drawLine(sp3, sp4);
                    painter.drawLine(sp4, sp1);

                    // Draw edge1 dimension (already placed, not editable in stage 2)
                    drawPreviewDimension(painter, sp1, sp2, edge1Len);

                    // Draw edge2 dimension and angle via dim input fields
                    if (edge2Len > 0.1 && m_dimActiveIndex >= 0 && m_dimFields.size() >= 2) {
                        // Update current values
                        m_dimFields[0].currentValue = edge2Len;
                        m_dimFields[1].currentValue = insideAngleDeg;

                        // Edge2 length field along the sp2-sp3 edge
                        QPointF midEdge2 = (QPointF(sp2) + QPointF(sp3)) / 2.0;
                        double screenAngle2 = std::atan2(sp3.y() - sp2.y(), sp3.x() - sp2.x()) * 180.0 / M_PI;
                        bool flipped2 = (screenAngle2 > 90 || screenAngle2 < -90);
                        if (flipped2) screenAngle2 += 180;
                        drawDimInputField(painter, midEdge2 + QPointF(0, 18), 0, screenAngle2);

                        // Draw angle arc (decorative) at p2
                        if (edge1Len > 0.1) {
                            painter.save();
                            painter.setPen(QPen(QColor(255, 140, 0), 1));
                            double arcRadius = qMin(30.0, qMin(edge1Len, edge2Len) * m_zoom * 0.3);
                            double angleStart = std::atan2(-(sp1.y() - sp2.y()), sp1.x() - sp2.x()) * 180.0 / M_PI;
                            double angleEnd = std::atan2(-(sp3.y() - sp2.y()), sp3.x() - sp2.x()) * 180.0 / M_PI;
                            double angleSweep = angleEnd - angleStart;
                            while (angleSweep > 180) angleSweep -= 360;
                            while (angleSweep < -180) angleSweep += 360;
                            QRectF arcRect(sp2.x() - arcRadius, sp2.y() - arcRadius,
                                           arcRadius * 2, arcRadius * 2);
                            painter.drawArc(arcRect, static_cast<int>(angleStart * 16),
                                           static_cast<int>(angleSweep * 16));
                            painter.restore();

                            // Angle dim input field at label position
                            double labelAngle = (angleStart + angleSweep / 2.0) * M_PI / 180.0;
                            QPointF labelPos(sp2.x() + (arcRadius + 15) * std::cos(-labelAngle),
                                             sp2.y() + (arcRadius + 15) * std::sin(-labelAngle));
                            drawDimInputField(painter, labelPos, 1, 0.0);
                        }
                    } else if (edge2Len > 0.1) {
                        // Fallback: no dim fields available
                        drawPreviewDimension(painter, sp2, sp3, edge2Len);
                        // Draw inside angle indicator at p2
                        if (edge1Len > 0.1) {
                            painter.save();
                            painter.setPen(QPen(QColor(255, 140, 0), 1));
                            double arcRadius = qMin(30.0, qMin(edge1Len, edge2Len) * m_zoom * 0.3);
                            double startAngle = std::atan2(-(sp1.y() - sp2.y()), sp1.x() - sp2.x()) * 180.0 / M_PI;
                            double endAngle = std::atan2(-(sp3.y() - sp2.y()), sp3.x() - sp2.x()) * 180.0 / M_PI;
                            double sweepAngle = endAngle - startAngle;
                            while (sweepAngle > 180) sweepAngle -= 360;
                            while (sweepAngle < -180) sweepAngle += 360;
                            QRectF arcRect(sp2.x() - arcRadius, sp2.y() - arcRadius,
                                           arcRadius * 2, arcRadius * 2);
                            painter.drawArc(arcRect, static_cast<int>(startAngle * 16),
                                           static_cast<int>(sweepAngle * 16));
                            QString angleText = formatAngle(insideAngleDeg);
                            QFont font = painter.font();
                            font.setPointSize(9);
                            painter.setFont(font);
                            QFontMetrics fm(font);
                            QRect textRect = fm.boundingRect(angleText);
                            double labelAngle = (startAngle + sweepAngle / 2.0) * M_PI / 180.0;
                            QPointF labelPos(sp2.x() + (arcRadius + 15) * std::cos(-labelAngle),
                                             sp2.y() + (arcRadius + 15) * std::sin(-labelAngle));
                            QRectF bgRect(labelPos.x() - textRect.width() / 2 - 2,
                                          labelPos.y() - textRect.height() / 2 - 1,
                                          textRect.width() + 4, textRect.height() + 2);
                            painter.fillRect(bgRect, QColor(255, 255, 255, 200));
                            painter.drawText(bgRect, Qt::AlignCenter, angleText);
                            painter.restore();
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
                // Corner or Center mode - use constrained corners from updateEntity
                QPointF corner1, corner2;
                if (m_rectMode == RectMode::Center) {
                    // Center mode: pendingEntity stores [center, corner1, corner2]
                    if (m_pendingEntity.points.size() >= 3) {
                        corner1 = m_pendingEntity.points[1];
                        corner2 = m_pendingEntity.points[2];
                    } else {
                        QPointF center = m_previewPoints[0];
                        QPointF delta = m_currentMouseWorld - center;
                        corner1 = center - delta;
                        corner2 = m_currentMouseWorld;
                    }
                } else if (m_pendingEntity.points.size() >= 4) {
                    // Corner mode — rotated (both W+H locked): 4 corners
                    QPointF c0 = m_pendingEntity.points[0];
                    QPointF c1 = m_pendingEntity.points[1];
                    QPointF c2 = m_pendingEntity.points[2];
                    QPointF c3 = m_pendingEntity.points[3];
                    QPoint s0 = worldToScreen(c0);
                    QPoint s1 = worldToScreen(c1);
                    QPoint s2 = worldToScreen(c2);
                    QPoint s3 = worldToScreen(c3);

                    QPolygon poly;
                    poly << s0 << s1 << s2 << s3 << s0;
                    painter.drawPolyline(poly);

                    // Width and height from locked dims
                    double width = getLockedDim(0);
                    double height = getLockedDim(1);
                    if (width > 0 && height > 0 && m_dimActiveIndex >= 0 && m_dimFields.size() >= 2) {
                        m_dimFields[0].currentValue = width;
                        m_dimFields[1].currentValue = height;
                        // Width label along edge c0→c1
                        QPointF wMid = (QPointF(s0) + QPointF(s1)) / 2.0;
                        QPointF wDir = QPointF(s1) - QPointF(s0);
                        double wAngle = std::atan2(wDir.y(), wDir.x()) * 180.0 / M_PI;
                        if (wAngle > 90.0)  wAngle -= 180.0;
                        if (wAngle < -90.0) wAngle += 180.0;
                        // Offset perpendicular to the edge (outward)
                        QPointF wPerp(-wDir.y(), wDir.x());
                        double wLen = std::sqrt(wPerp.x() * wPerp.x() + wPerp.y() * wPerp.y());
                        if (wLen > 1e-6) wPerp /= wLen;
                        drawDimInputField(painter, wMid - wPerp * 18, 0, wAngle);
                        // Height label along edge c0→c3
                        QPointF hMid = (QPointF(s0) + QPointF(s3)) / 2.0;
                        QPointF hDir = QPointF(s3) - QPointF(s0);
                        double hAngle = std::atan2(hDir.y(), hDir.x()) * 180.0 / M_PI;
                        if (hAngle > 90.0)  hAngle -= 180.0;
                        if (hAngle < -90.0) hAngle += 180.0;
                        QPointF hPerp(-hDir.y(), hDir.x());
                        double hLen = std::sqrt(hPerp.x() * hPerp.x() + hPerp.y() * hPerp.y());
                        if (hLen > 1e-6) hPerp /= hLen;
                        drawDimInputField(painter, hMid - hPerp * 40, 1, hAngle);
                    }
                } else {
                    // Corner mode — axis-aligned: 2 points
                    corner1 = m_previewPoints[0];
                    corner2 = (m_pendingEntity.points.size() >= 2)
                        ? m_pendingEntity.points[1] : m_currentMouseWorld;
                }

                // Draw axis-aligned rectangle (2-point case and Center mode)
                if (m_pendingEntity.points.size() < 4 || m_rectMode == RectMode::Center) {
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
                    if (width > 0.1 || height > 0.1) {
                        if (m_dimActiveIndex >= 0 && m_dimFields.size() >= 2) {
                            m_dimFields[0].currentValue = width;
                            m_dimFields[1].currentValue = height;
                            // Width below bottom edge
                            QPointF bottomMid((rect.left() + rect.right()) / 2.0, rect.bottom() + 18);
                            drawDimInputField(painter, bottomMid, 0);
                            // Height along right edge
                            QPointF rightMid(rect.right() + 40, (rect.top() + rect.bottom()) / 2.0);
                            drawDimInputField(painter, rightMid, 1);
                        } else {
                            if (width > 0.1) {
                                QPoint bottomLeft(rect.left(), rect.bottom());
                                QPoint bottomRight(rect.right(), rect.bottom());
                                drawPreviewDimension(painter, bottomLeft, bottomRight, width);
                            }
                            if (height > 0.1) {
                                QPoint topRight(rect.right(), rect.top());
                                QPoint bottomRight2(rect.right(), rect.bottom());
                                drawPreviewDimension(painter, topRight, bottomRight2, height);
                            }
                        }
                    }
                }
            }
        }
        break;

    case SketchTool::Circle:
        if (!m_previewPoints.isEmpty()) {
            QPointF centerWorld;
            double r;
            int crossSize = 4;

            if (m_circleMode == CircleMode::TwoPoint) {
                // Two-point (diameter) mode: first point is one end of diameter
                QPointF p1 = m_previewPoints[0];
                // Use constrained endpoint from updateEntity
                QPointF p2 = (m_pendingEntity.points.size() >= 2)
                    ? m_pendingEntity.points[1] : m_currentMouseWorld;
                centerWorld = (p1 + p2) / 2.0;
                r = QLineF(p1, p2).length() / 2.0;

                QPoint center = worldToScreen(centerWorld);
                int rPx = static_cast<int>(r * m_zoom);
                painter.drawEllipse(center, rPx, rPx);

                // Draw center point marker
                painter.drawLine(center.x() - crossSize, center.y(), center.x() + crossSize, center.y());
                painter.drawLine(center.x(), center.y() - crossSize, center.x(), center.y() + crossSize);

                // Draw diameter line and endpoint markers
                QPoint sp1 = worldToScreen(p1);
                QPoint sp2 = worldToScreen(p2);
                painter.save();
                painter.setPen(QPen(QColor(128, 128, 128), 1, Qt::DashLine));
                painter.drawLine(sp1, sp2);
                painter.restore();

                // Draw perimeter point markers (the two diameter endpoints)
                painter.drawLine(sp1.x() - crossSize, sp1.y(), sp1.x() + crossSize, sp1.y());
                painter.drawLine(sp1.x(), sp1.y() - crossSize, sp1.x(), sp1.y() + crossSize);
                painter.drawLine(sp2.x() - crossSize, sp2.y(), sp2.x() + crossSize, sp2.y());
                painter.drawLine(sp2.x(), sp2.y() - crossSize, sp2.x(), sp2.y() + crossSize);

                // Draw diameter dimension
                if (r > 0.05) {
                    if (m_dimActiveIndex >= 0 && !m_dimFields.isEmpty()) {
                        m_dimFields[0].currentValue = r * 2.0;
                        QPointF midPt = (QPointF(sp1) + QPointF(sp2)) / 2.0 + QPointF(0, 18);
                        drawDimInputField(painter, midPt, 0);
                    } else {
                        drawPreviewDimension(painter, sp1, sp2, r * 2.0);
                    }
                }
            } else if (m_circleMode == CircleMode::ThreePoint) {
                // Three-point circle: calculate circumcircle through points
                QVector<QPointF> pts = m_previewPoints;
                pts.append(m_currentMouseWorld);  // Add current mouse as next point

                if (pts.size() >= 3) {
                    // Use library function for circumcircle calculation
                    auto arc = geometry::arcFromThreePoints(pts[0], pts[1], pts[2]);
                    if (arc.has_value()) {
                        centerWorld = arc->center;
                        r = arc->radius;

                        QPoint center = worldToScreen(centerWorld);
                        int rPx = static_cast<int>(r * m_zoom);
                        painter.drawEllipse(center, rPx, rPx);

                        // Draw center point marker
                        painter.drawLine(center.x() - crossSize, center.y(), center.x() + crossSize, center.y());
                        painter.drawLine(center.x(), center.y() - crossSize, center.x(), center.y() + crossSize);

                        // Draw the 3 perimeter points (no quadrant markers for 3-point circles)
                        for (int i = 0; i < 3 && i < pts.size(); ++i) {
                            QPoint pt = worldToScreen(pts[i]);
                            painter.drawLine(pt.x() - crossSize, pt.y(), pt.x() + crossSize, pt.y());
                            painter.drawLine(pt.x(), pt.y() - crossSize, pt.x(), pt.y() + crossSize);
                        }

                        // Draw radius dimension from center to first point
                        if (r > 0.1) {
                            QPoint sp1 = worldToScreen(pts[0]);
                            drawPreviewDimension(painter, center, sp1, r);
                        }
                    }
                } else if (pts.size() == 2) {
                    // Only 2 points - show the line between them and point markers
                    QPoint sp1 = worldToScreen(pts[0]);
                    QPoint sp2 = worldToScreen(pts[1]);
                    painter.save();
                    painter.setPen(QPen(QColor(128, 128, 128), 1, Qt::DashLine));
                    painter.drawLine(sp1, sp2);
                    painter.restore();

                    // Draw cross markers for placed points
                    painter.drawLine(sp1.x() - crossSize, sp1.y(), sp1.x() + crossSize, sp1.y());
                    painter.drawLine(sp1.x(), sp1.y() - crossSize, sp1.x(), sp1.y() + crossSize);
                    painter.drawLine(sp2.x() - crossSize, sp2.y(), sp2.x() + crossSize, sp2.y());
                    painter.drawLine(sp2.x(), sp2.y() - crossSize, sp2.x(), sp2.y() + crossSize);
                }
            } else {
                // Center-radius mode - use constrained point from updateEntity
                centerWorld = m_previewPoints[0];
                QPointF perimWorld = (m_pendingEntity.points.size() >= 2)
                    ? m_pendingEntity.points[1] : m_currentMouseWorld;
                r = QLineF(centerWorld, perimWorld).length();

                QPoint center = worldToScreen(centerWorld);
                int rPx = static_cast<int>(r * m_zoom);
                painter.drawEllipse(center, rPx, rPx);

                // Draw center point marker
                painter.drawLine(center.x() - crossSize, center.y(), center.x() + crossSize, center.y());
                painter.drawLine(center.x(), center.y() - crossSize, center.x(), center.y() + crossSize);

                // Draw perimeter point marker
                QPoint radiusEnd = worldToScreen(perimWorld);
                painter.drawLine(radiusEnd.x() - crossSize, radiusEnd.y(), radiusEnd.x() + crossSize, radiusEnd.y());
                painter.drawLine(radiusEnd.x(), radiusEnd.y() - crossSize, radiusEnd.x(), radiusEnd.y() + crossSize);

                // Draw radius dimension
                if (r > 0.1) {
                    if (m_dimActiveIndex >= 0 && !m_dimFields.isEmpty()) {
                        m_dimFields[0].currentValue = r;
                        QPointF midPt = (QPointF(center) + QPointF(radiusEnd)) / 2.0 + QPointF(0, 18);
                        drawDimInputField(painter, midPt, 0);
                    } else {
                        drawPreviewDimension(painter, center, radiusEnd, r);
                    }
                }
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
            // Use constrained endpoint from updateEntity for linear slots
            QPointF p2World = (m_pendingEntity.points.size() >= 2 &&
                               (m_slotMode == SlotMode::CenterToCenter || m_slotMode == SlotMode::Overall))
                ? m_pendingEntity.points[1] : m_currentMouseWorld;
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

                    // Draw sweep angle dim input field along the centerline arc
                    double arcLengthWorld = arcRadius * std::abs(sweepAngle * M_PI / 180.0);
                    if (arcLengthWorld > 0.1) {
                        // Position dimension label at the midpoint of the arc (offset outward)
                        double midAngle = startAngle + sweepAngle / 2.0;
                        double midAngleRad = midAngle * M_PI / 180.0;
                        double outwardAngle = -midAngleRad;
                        double offsetDist = screenOuterRadius + 20;
                        QPointF labelCenter = sc + QPointF(offsetDist * std::cos(outwardAngle),
                                                           offsetDist * std::sin(outwardAngle));
                        if (m_dimActiveIndex >= 0 && m_dimFields.size() >= 1) {
                            m_dimFields[0].currentValue = std::abs(sweepAngle);
                            drawDimInputField(painter, labelCenter, 0, 0);
                        } else {
                            drawArcDimensionLabel(painter, labelCenter, arcLengthWorld, sweepAngle);
                        }
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

                    // Draw dimension below the line (Radius for ArcRadius, distance for ArcEnds)
                    double distance = QLineF(p1World, p2World).length();
                    if (distance > 0.1) {
                        if (m_slotMode == SlotMode::ArcRadius && m_dimActiveIndex >= 0 && m_dimFields.size() >= 1) {
                            m_dimFields[0].currentValue = distance;
                            QPointF midPt = (QPointF(sp1) + QPointF(sp2)) / 2.0;
                            double screenAngle = std::atan2(sp2.y() - sp1.y(), sp2.x() - sp1.x()) * 180.0 / M_PI;
                            bool flipped = (screenAngle > 90 || screenAngle < -90);
                            if (flipped) screenAngle += 180;
                            drawDimInputField(painter, midPt + QPointF(0, 18), 0, screenAngle);
                        } else {
                            drawPreviewDimension(painter, sp1, sp2, distance);
                        }
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
                        if (m_dimActiveIndex >= 0 && !m_dimFields.isEmpty()) {
                            m_dimFields[0].currentValue = slotLength;
                            QPointF midPt = (sp1 + sp2) / 2.0 + QPointF(0, 18);
                            drawDimInputField(painter, midPt, 0);
                        } else {
                            drawPreviewDimension(painter, sp1.toPoint(), sp2.toPoint(), slotLength);
                        }
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

                // Always draw placed-point markers (filled blue dots)
                int numPlaced = m_previewPoints.size();
                painter.setBrush(QColor(0, 120, 215));
                for (int i = 0; i < numPlaced; ++i) {
                    QPoint sp = worldToScreen(pts[i]);
                    painter.drawEllipse(sp, 3, 3);
                }
                painter.setBrush(Qt::NoBrush);

                if (pts.size() == 2) {
                    // Just two points (1 placed + mouse) - draw a dashed line preview
                    painter.save();
                    painter.setPen(QPen(QColor(128, 128, 128), 1, Qt::DashLine));
                    QPoint sp1 = worldToScreen(pts[0]);
                    QPoint sp2 = worldToScreen(pts[1]);
                    painter.drawLine(sp1, sp2);
                    painter.restore();
                } else if (pts.size() >= 3) {
                    // Three points - calculate arc using library function
                    auto arc = geometry::arcFromThreePoints(pts[0], pts[1], pts[2]);
                    if (arc.has_value()) {
                        // Use floating-point screen coords for precise arc rendering
                        QPointF scf = worldToScreenF(arc->center);
                        double rPx = arc->radius * m_zoom;

                        // arcFromThreePoints returns angles in math convention
                        // (CCW positive from 3 o'clock) which matches Qt's arcTo
                        double startAngle = arc->startAngle;
                        double sweep = arc->sweepAngle;

                        // Draw arc using QPainterPath for sub-pixel precision
                        QRectF arcRect(scf.x() - rPx, scf.y() - rPx, rPx * 2.0, rPx * 2.0);
                        QPainterPath path;
                        path.arcMoveTo(arcRect, startAngle);
                        path.arcTo(arcRect, startAngle, sweep);
                        painter.drawPath(path);

                        // Draw center point marker (small cross)
                        QPoint sc = scf.toPoint();
                        int crossSize = 4;
                        painter.drawLine(sc.x() - crossSize, sc.y(), sc.x() + crossSize, sc.y());
                        painter.drawLine(sc.x(), sc.y() - crossSize, sc.x(), sc.y() + crossSize);

                        // Draw mouse-position marker (open circle at 3rd point)
                        QPoint sp3 = worldToScreen(pts[2]);
                        painter.drawEllipse(sp3, 3, 3);

                        // Draw arc length and angle dimension
                        double arcLen = geometry::arcLength(*arc);
                        if (arcLen > 0.1) {
                            double midAngle = (arc->startAngle + arc->sweepAngle / 2.0) * M_PI / 180.0;
                            double screenMidAngle = -midAngle;
                            double offsetDist = rPx + 20;
                            QPointF labelCenter = scf + QPointF(offsetDist * std::cos(screenMidAngle),
                                                                 offsetDist * std::sin(screenMidAngle));
                            drawArcDimensionLabel(painter, labelCenter, arcLen, arc->sweepAngle);
                        }
                    } else {
                        // Collinear points — arc cannot be computed.
                        // Draw dashed lines through all points as fallback.
                        painter.save();
                        painter.setPen(QPen(QColor(128, 128, 128), 1, Qt::DashLine));
                        QPoint sp1 = worldToScreen(pts[0]);
                        QPoint sp2 = worldToScreen(pts[1]);
                        QPoint sp3 = worldToScreen(pts[2]);
                        painter.drawLine(sp1, sp2);
                        painter.drawLine(sp2, sp3);
                        painter.restore();
                    }
                }
            } else if (m_arcMode == ArcMode::CenterStartEnd) {
                // Center-Start-End arc: center is first point, start defines radius
                QPoint centerScreen = worldToScreen(m_previewPoints[0]);

                if (m_previewPoints.size() == 1) {
                    // Only center placed - draw line from center to mouse (radius preview)
                    QPoint mouseScreen = worldToScreen(m_currentMouseWorld);
                    painter.drawLine(centerScreen, mouseScreen);

                    // Draw center marker
                    int crossSize = 4;
                    painter.drawLine(centerScreen.x() - crossSize, centerScreen.y(),
                                     centerScreen.x() + crossSize, centerScreen.y());
                    painter.drawLine(centerScreen.x(), centerScreen.y() - crossSize,
                                     centerScreen.x(), centerScreen.y() + crossSize);

                    // Show radius dim input field
                    double radius = QLineF(m_previewPoints[0], m_currentMouseWorld).length();
                    if (radius > 0.1) {
                        if (m_dimActiveIndex >= 0 && m_dimFields.size() >= 1) {
                            m_dimFields[0].currentValue = radius;
                            QPointF midPt = (QPointF(centerScreen) + QPointF(mouseScreen)) / 2.0;
                            double screenAngle = std::atan2(mouseScreen.y() - centerScreen.y(), mouseScreen.x() - centerScreen.x()) * 180.0 / M_PI;
                            bool flipped = (screenAngle > 90 || screenAngle < -90);
                            if (flipped) screenAngle += 180;
                            drawDimInputField(painter, midPt + QPointF(0, 18), 0, screenAngle);
                        } else {
                            QPointF labelPos = (QPointF(centerScreen) + QPointF(mouseScreen)) / 2.0;
                            labelPos += QPointF(10, -10);
                            drawDimensionLabel(painter, labelPos, radius);
                        }
                    }
                } else if (m_previewPoints.size() >= 2) {
                    // Center and start placed - draw arc from start to mouse (constrained to radius)
                    QPointF center = m_previewPoints[0];
                    QPointF start = m_previewPoints[1];
                    double radius = QLineF(center, start).length();

                    // Constrain mouse to arc
                    double endAngle = std::atan2(m_currentMouseWorld.y() - center.y(),
                                                  m_currentMouseWorld.x() - center.x());
                    QPointF endPoint = center + QPointF(radius * std::cos(endAngle),
                                                         radius * std::sin(endAngle));

                    // Convert to screen for drawing
                    QPoint startScreen = worldToScreen(start);
                    QPoint endScreen = worldToScreen(endPoint);
                    int rPx = static_cast<int>(radius * m_zoom);
                    QRect arcRect(centerScreen.x() - rPx, centerScreen.y() - rPx, rPx * 2, rPx * 2);

                    // Calculate angles in screen space
                    double startAngleScreen = std::atan2(startScreen.y() - centerScreen.y(),
                                                          startScreen.x() - centerScreen.x()) * 180.0 / M_PI;
                    double endAngleScreen = std::atan2(endScreen.y() - centerScreen.y(),
                                                        endScreen.x() - centerScreen.x()) * 180.0 / M_PI;

                    // Calculate sweep (take shorter path by default, flip with Shift)
                    double sweep = endAngleScreen - startAngleScreen;
                    if (sweep > 180) sweep -= 360;
                    if (sweep < -180) sweep += 360;

                    // Apply flip for > 180 degree arcs
                    if (m_arcSlotFlipped) {
                        if (sweep > 0) {
                            sweep = sweep - 360;
                        } else {
                            sweep = sweep + 360;
                        }
                    }

                    painter.drawArc(arcRect, static_cast<int>(-startAngleScreen * 16),
                                    static_cast<int>(-sweep * 16));

                    // Draw center marker
                    int crossSize = 4;
                    painter.drawLine(centerScreen.x() - crossSize, centerScreen.y(),
                                     centerScreen.x() + crossSize, centerScreen.y());
                    painter.drawLine(centerScreen.x(), centerScreen.y() - crossSize,
                                     centerScreen.x(), centerScreen.y() + crossSize);

                    // Draw start and end points
                    painter.setBrush(QColor(0, 120, 215));
                    painter.drawEllipse(startScreen, 3, 3);
                    painter.drawEllipse(endScreen, 3, 3);

                    // Draw sweep angle dim input field
                    double arcLength = radius / m_zoom * std::abs(sweep * M_PI / 180.0);
                    if (arcLength > 0.1) {
                        double midAngle = (startAngleScreen + sweep / 2.0) * M_PI / 180.0;
                        double offsetDist = rPx + 20;
                        QPointF labelCenter = QPointF(centerScreen) + QPointF(offsetDist * std::cos(midAngle),
                                                                               offsetDist * std::sin(midAngle));
                        if (m_dimActiveIndex >= 0 && m_dimFields.size() >= 1) {
                            m_dimFields[0].currentValue = std::abs(sweep);
                            drawDimInputField(painter, labelCenter, 0, 0);
                        } else {
                            drawArcDimensionLabel(painter, labelCenter, arcLength, sweep);
                        }
                    }

                    // Draw hint message for Shift to flip
                    QString line1 = tr("Arc: Center → Start → End");
                    QString line2 = tr("(Shift to flip arc direction)");
                    QFontMetrics fm(painter.font());
                    int textWidth = std::max(fm.horizontalAdvance(line1), fm.horizontalAdvance(line2));
                    int textHeight = fm.height() * 2 + 4;
                    QPoint textPos(centerScreen.x() - textWidth / 2, centerScreen.y() + rPx + 30);
                    painter.setPen(QColor(80, 80, 80));
                    painter.drawText(textPos.x(), textPos.y(), line1);
                    painter.drawText(textPos.x(), textPos.y() + fm.height() + 2, line2);
                }
            } else if (m_arcMode == ArcMode::StartEndRadius) {
                // Start-End-Radius arc: start is first click, end is second click, then set radius
                if (m_previewPoints.size() == 1) {
                    // Only start placed - draw line from start to mouse (chord preview)
                    QPoint startScreen = worldToScreen(m_previewPoints[0]);
                    QPoint mouseScreen = worldToScreen(m_currentMouseWorld);
                    painter.drawLine(startScreen, mouseScreen);

                    // Draw start point
                    painter.setBrush(QColor(0, 120, 215));
                    painter.drawEllipse(startScreen, 3, 3);

                    // Show chord length + angle dim input fields
                    double chordLength = QLineF(m_previewPoints[0], m_currentMouseWorld).length();
                    if (chordLength > 0.1) {
                        if (m_dimActiveIndex >= 0 && m_dimFields.size() >= 2) {
                            double dx = m_currentMouseWorld.x() - m_previewPoints[0].x();
                            double dy = m_currentMouseWorld.y() - m_previewPoints[0].y();
                            double angleDeg = std::atan2(dy, dx) * 180.0 / M_PI;
                            m_dimFields[0].currentValue = chordLength;
                            m_dimFields[1].currentValue = angleDeg;
                            QPointF midPt = (QPointF(startScreen) + QPointF(mouseScreen)) / 2.0;
                            double screenAngle = std::atan2(mouseScreen.y() - startScreen.y(), mouseScreen.x() - startScreen.x()) * 180.0 / M_PI;
                            bool flipped = (screenAngle > 90 || screenAngle < -90);
                            if (flipped) screenAngle += 180;
                            drawDimInputField(painter, midPt + QPointF(0, 18), 0, screenAngle);
                            drawDimInputField(painter, midPt + QPointF(0, 38), 1, screenAngle);
                        } else {
                            QPointF labelPos = (QPointF(startScreen) + QPointF(mouseScreen)) / 2.0;
                            labelPos += QPointF(10, -10);
                            drawDimensionLabel(painter, labelPos, chordLength);
                        }
                    }
                } else if (m_previewPoints.size() >= 2) {
                    // Start and end placed - mouse controls arc center (constrained to perpendicular bisector)
                    QPointF start = m_previewPoints[0];
                    QPointF end = m_previewPoints[1];
                    QPointF midChord = (start + end) / 2.0;
                    double chordLength = QLineF(start, end).length();

                    if (chordLength > 0.001) {
                        // Calculate perpendicular direction from chord midpoint
                        QPointF chordDir = (end - start) / chordLength;
                        QPointF perpDir(-chordDir.y(), chordDir.x());

                        // Project mouse onto perpendicular bisector to get arc center
                        QPointF toMouse = m_currentMouseWorld - midChord;
                        double projDist = toMouse.x() * perpDir.x() + toMouse.y() * perpDir.y();

                        // Ctrl snaps to midpoint (semicircle - exactly 180°)
                        bool ctrlHeld = (QGuiApplication::queryKeyboardModifiers() & Qt::ControlModifier);
                        if (ctrlHeld) {
                            projDist = 0.0;
                        }

                        // Apply flip (Shift key) - move center to opposite side of chord
                        if (m_arcSlotFlipped) {
                            projDist = -projDist;
                        }

                        // Arc center is on the perpendicular bisector
                        QPointF center = midChord + perpDir * projDist;

                        // Calculate radius from center to endpoints (equidistant)
                        double radius = QLineF(center, start).length();

                        // Minimum radius to avoid degenerate arcs (skip if Ctrl for exact 180°)
                        double halfChord = chordLength / 2.0;
                        if (!ctrlHeld) {
                            double minRadius = halfChord * 1.01;  // Just slightly larger than half chord
                            if (radius < minRadius) {
                                // Push center out to minimum distance
                                double minDist = std::sqrt(minRadius * minRadius - halfChord * halfChord);
                                double sign = (projDist >= 0) ? 1.0 : -1.0;
                                center = midChord + perpDir * sign * minDist;
                                radius = minRadius;
                                // Update projDist after adjustment
                                projDist = sign * minDist;
                            }
                        }

                        // Convert to screen for drawing
                        QPoint centerScreen = worldToScreen(center);
                        QPoint startScreen = worldToScreen(start);
                        QPoint endScreen = worldToScreen(end);
                        int rPx = static_cast<int>(radius * m_zoom);
                        QRect arcRect(centerScreen.x() - rPx, centerScreen.y() - rPx, rPx * 2, rPx * 2);

                        // Calculate angles in screen space
                        double startAngleScreen = std::atan2(startScreen.y() - centerScreen.y(),
                                                              startScreen.x() - centerScreen.x()) * 180.0 / M_PI;
                        double endAngleScreen = std::atan2(endScreen.y() - centerScreen.y(),
                                                            endScreen.x() - centerScreen.x()) * 180.0 / M_PI;

                        // Calculate sweep from start to end
                        double sweep = endAngleScreen - startAngleScreen;
                        // Normalize to [-180, 180]
                        while (sweep > 180) sweep -= 360;
                        while (sweep < -180) sweep += 360;

                        // The arc length is determined by center position:
                        // - Center far from chord (|projDist| > halfChord) = small arc (< 180°) because radius is large
                        // - Center close to chord (|projDist| < halfChord) = large arc (> 180°) because radius is small
                        // When |projDist| = halfChord, the arc is exactly 90° (radius = halfChord * sqrt(2))
                        bool wantLongArc = (std::abs(projDist) < halfChord);

                        // After normalization, sweep is in [-180, 180] (always "short" path)
                        // If we want the long arc, flip to get > 180 or < -180
                        if (wantLongArc) {
                            if (sweep > 0) sweep -= 360;
                            else sweep += 360;
                        }
                        // Note: m_arcSlotFlipped was already applied to projDist above,
                        // which changed the center position and wantLongArc calculation.
                        // We do NOT apply it again here.

                        // For Ctrl (exact 180°), force sweep to exactly 180 or -180 degrees
                        if (ctrlHeld) {
                            sweep = (sweep > 0) ? 180.0 : -180.0;
                        }

                        painter.drawArc(arcRect, static_cast<int>(-startAngleScreen * 16),
                                        static_cast<int>(-sweep * 16));

                        // Draw center marker (this is where the cursor is constrained to)
                        int crossSize = ctrlHeld ? 6 : 4;  // Larger when snapped
                        painter.drawLine(centerScreen.x() - crossSize, centerScreen.y(),
                                         centerScreen.x() + crossSize, centerScreen.y());
                        painter.drawLine(centerScreen.x(), centerScreen.y() - crossSize,
                                         centerScreen.x(), centerScreen.y() + crossSize);

                        // When Ctrl is held, also draw a circle at the snap point for visibility
                        if (ctrlHeld) {
                            painter.setBrush(Qt::NoBrush);
                            painter.drawEllipse(centerScreen, 8, 8);
                        }

                        // Draw start and end points (fixed)
                        painter.setBrush(QColor(0, 120, 215));
                        painter.drawEllipse(startScreen, 4, 4);
                        painter.drawEllipse(endScreen, 4, 4);

                        // Draw sweep angle dim input field at midpoint of arc
                        double arcLength = radius * std::abs(sweep * M_PI / 180.0);
                        if (arcLength > 0.1) {
                            double midAngle = (startAngleScreen + sweep / 2.0) * M_PI / 180.0;
                            double offsetDist = rPx + 25;
                            QPointF labelCenter = QPointF(centerScreen) + QPointF(offsetDist * std::cos(midAngle),
                                                                                   offsetDist * std::sin(midAngle));
                            if (m_dimActiveIndex >= 0 && m_dimFields.size() >= 1) {
                                m_dimFields[0].currentValue = std::abs(sweep);
                                drawDimInputField(painter, labelCenter, 0, 0);
                            } else {
                                drawArcDimensionLabel(painter, labelCenter, arcLength, sweep);
                            }
                        }

                        // Draw hint message (positioned well below arc to avoid dimension overlap)
                        QString line1 = tr("Arc: Start → End → Center");
                        QString line2 = tr("(Shift to flip, Ctrl for 180°)");
                        QFontMetrics fm(painter.font());
                        int textWidth = std::max(fm.horizontalAdvance(line1), fm.horizontalAdvance(line2));
                        QPoint textPos(centerScreen.x() - textWidth / 2, centerScreen.y() + rPx + 60);
                        painter.setPen(QColor(80, 80, 80));
                        painter.drawText(textPos.x(), textPos.y(), line1);
                        painter.drawText(textPos.x(), textPos.y() + fm.height() + 2, line2);
                    }
                }
            } else if (m_arcMode == ArcMode::Tangent) {
                // Tangent arc preview (after first click)
                if (!m_tangentTargets.isEmpty() && !m_previewPoints.isEmpty()) {
                    // Find the tangent entity
                    const SketchEntity* tangentEntity = nullptr;
                    for (const auto& e : m_entities) {
                        if (e.id == m_tangentTargets[0]) {
                            tangentEntity = &e;
                            break;
                        }
                    }

                    if (tangentEntity) {
                        QPointF clickPoint = m_previewPoints[0];
                        QPointF endPoint = m_currentMouseWorld;

                        // Project click point onto the entity to get the actual tangent point
                        QPointF tangentPoint = clickPoint;
                        if (tangentEntity->type == SketchEntityType::Line && tangentEntity->points.size() >= 2) {
                            tangentPoint = geometry::closestPointOnLine(clickPoint,
                                tangentEntity->points[0], tangentEntity->points[1]);
                        } else if (tangentEntity->type == SketchEntityType::Rectangle && tangentEntity->points.size() >= 2) {
                            // Find closest edge and project onto it
                            QPointF corners[4];
                            if (tangentEntity->points.size() >= 4) {
                                for (int i = 0; i < 4; ++i) corners[i] = tangentEntity->points[i];
                            } else {
                                corners[0] = tangentEntity->points[0];
                                corners[1] = QPointF(tangentEntity->points[1].x(), tangentEntity->points[0].y());
                                corners[2] = tangentEntity->points[1];
                                corners[3] = QPointF(tangentEntity->points[0].x(), tangentEntity->points[1].y());
                            }
                            double minDist = std::numeric_limits<double>::max();
                            for (int i = 0; i < 4; ++i) {
                                QPointF edgeStart = corners[i];
                                QPointF edgeEnd = corners[(i + 1) % 4];
                                QPointF projected = geometry::closestPointOnLine(clickPoint, edgeStart, edgeEnd);
                                double dist = QLineF(clickPoint, projected).length();
                                if (dist < minDist) {
                                    minDist = dist;
                                    tangentPoint = projected;
                                }
                            }
                        }

                        // Calculate the tangent arc
                        TangentArc ta = calculateTangentArc(*tangentEntity, tangentPoint, endPoint);

                        if (ta.valid) {
                            // Draw the arc preview
                            QPoint centerScreen = worldToScreen(ta.center);
                            QPoint startScreen = worldToScreen(tangentPoint);
                            QPoint endScreen = worldToScreen(endPoint);
                            int rPx = static_cast<int>(ta.radius * m_zoom);
                            QRect arcRect(centerScreen.x() - rPx, centerScreen.y() - rPx, rPx * 2, rPx * 2);

                            // Calculate angles in screen space (Y is inverted)
                            double startAngleScreen = std::atan2(startScreen.y() - centerScreen.y(),
                                                                  startScreen.x() - centerScreen.x()) * 180.0 / M_PI;
                            double endAngleScreen = std::atan2(endScreen.y() - centerScreen.y(),
                                                                endScreen.x() - centerScreen.x()) * 180.0 / M_PI;

                            // Use the world-space sweep angle from calculateTangentArc.
                            // Screen Y is inverted from world Y, so we negate the sweep.
                            double sweep = -ta.sweepAngle;

                            // Apply flip with Shift key
                            if (m_arcSlotFlipped) {
                                if (sweep > 0) sweep -= 360;
                                else sweep += 360;
                            }

                            // Qt uses 1/16th degree units
                            painter.drawArc(arcRect,
                                static_cast<int>(-startAngleScreen * 16),
                                static_cast<int>(-sweep * 16));

                            // Draw center marker
                            int crossSize = 4;
                            painter.drawLine(centerScreen.x() - crossSize, centerScreen.y(),
                                             centerScreen.x() + crossSize, centerScreen.y());
                            painter.drawLine(centerScreen.x(), centerScreen.y() - crossSize,
                                             centerScreen.x(), centerScreen.y() + crossSize);

                            // Draw start and end points
                            painter.setBrush(QColor(0, 120, 215));
                            painter.drawEllipse(startScreen, 3, 3);
                            painter.drawEllipse(endScreen, 3, 3);

                            // Draw arc length dimension
                            double arcLength = ta.radius * std::abs(sweep * M_PI / 180.0);
                            if (arcLength > 0.1) {
                                double midAngle = (startAngleScreen + sweep / 2.0) * M_PI / 180.0;
                                double offsetDist = rPx + 20;
                                QPointF labelCenter = QPointF(centerScreen) + QPointF(offsetDist * std::cos(midAngle),
                                                                                       offsetDist * std::sin(midAngle));
                                drawArcDimensionLabel(painter, labelCenter, arcLength, sweep);
                            }

                            // Draw hint message
                            QString hint = tr("(Shift to flip arc direction)");
                            QFontMetrics fm(painter.font());
                            int textWidth = fm.horizontalAdvance(hint);
                            QPoint textPos(centerScreen.x() - textWidth / 2, centerScreen.y() + rPx + 30);
                            painter.setPen(QColor(80, 80, 80));
                            painter.drawText(textPos.x(), textPos.y(), hint);
                        } else {
                            // Arc not valid yet - just draw a line from start to cursor
                            QPoint sp1 = worldToScreen(tangentPoint);
                            QPoint sp2 = worldToScreen(endPoint);
                            painter.drawLine(sp1, sp2);

                            // Draw start point
                            painter.setBrush(QColor(0, 120, 215));
                            painter.drawEllipse(sp1, 3, 3);
                        }
                    } else {
                        // Tangent entity not found - draw fallback line
                        QPoint sp1 = worldToScreen(m_previewPoints[0]);
                        QPoint sp2 = worldToScreen(m_currentMouseWorld);
                        painter.drawLine(sp1, sp2);
                        painter.setBrush(QColor(0, 120, 215));
                        painter.drawEllipse(sp1, 3, 3);
                    }
                } else if (!m_previewPoints.isEmpty()) {
                    // No tangent target yet - just show start point
                    QPoint sp1 = worldToScreen(m_previewPoints[0]);
                    QPoint sp2 = worldToScreen(m_currentMouseWorld);
                    painter.drawLine(sp1, sp2);
                }
            }
        }
        break;

    case SketchTool::Polygon:
        if (!m_previewPoints.isEmpty()) {
            if (m_polygonMode == PolygonMode::Freeform) {
                // Freeform polygon: draw polyline through clicked vertices + mouse
                QVector<QPoint> screenPts;
                for (const QPointF& wp : m_previewPoints) {
                    screenPts.append(worldToScreen(wp));
                }
                QPoint mousePt = worldToScreen(m_currentMouseWorld);
                screenPts.append(mousePt);

                // Draw edges
                for (int i = 0; i < screenPts.size() - 1; ++i) {
                    painter.drawLine(screenPts[i], screenPts[i + 1]);
                }

                // Draw closing line from mouse back to start (dashed hint)
                if (m_previewPoints.size() >= 2) {
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
                for (int i = 0; i < m_previewPoints.size(); ++i) {
                    painter.drawEllipse(screenPts[i], 3, 3);
                }

                // Highlight start point green if mouse is close (snap-to-close)
                if (m_previewPoints.size() >= 3) {
                    double distToStart = QLineF(m_currentMouseWorld, m_previewPoints[0]).length();
                    double snapDist = m_entitySnapTolerance / m_zoom;
                    if (distToStart < snapDist) {
                        painter.setBrush(QColor(0, 180, 100));
                        painter.drawEllipse(screenPts[0], 5, 5);
                    }
                }
            } else {
                // Regular polygon: center + radius + sides
                QPointF center = m_previewPoints[0];
                // Use constrained radius from updateEntity
                double radius = m_pendingEntity.radius;
                int sides = m_pendingEntity.sides > 0 ? m_pendingEntity.sides : 6;

                if (radius > 0.1) {
                    QPoint sc = worldToScreen(center);
                    int rPx = static_cast<int>(radius * m_zoom);

                    // Draw construction circle behind polygon (dashed, construction color)
                    painter.save();
                    QPen constructionPen(QColor(180, 100, 50), 1, Qt::DashLine);
                    painter.setPen(constructionPen);
                    painter.setBrush(Qt::NoBrush);
                    painter.drawEllipse(sc, rPx, rPx);
                    painter.restore();

                    // Calculate polygon vertices
                    QPolygonF poly;
                    double angleStep = 2.0 * M_PI / sides;
                    double startAngle = std::atan2(m_currentMouseWorld.y() - center.y(),
                                                   m_currentMouseWorld.x() - center.x());

                    // Compute vertex radius — differs for Inscribed vs Circumscribed
                    int vertexRPx = rPx;  // Inscribed: vertices on circle
                    if (m_polygonMode == PolygonMode::Circumscribed) {
                        // Circumscribed: radius is apothem, vertex distance = apothem / cos(pi/sides)
                        vertexRPx = static_cast<int>((radius / std::cos(M_PI / sides)) * m_zoom);
                        startAngle += M_PI / sides;  // Rotate so edge midpoint faces mouse
                    }

                    for (int i = 0; i < sides; ++i) {
                        double angle = startAngle + i * angleStep;
                        double x = sc.x() + vertexRPx * std::cos(angle);
                        double y = sc.y() - vertexRPx * std::sin(angle);  // Negate sin: world Y-up → screen Y-down
                        poly << QPointF(x, y);
                    }
                    poly << poly.first();  // Close the polygon

                    painter.drawPolyline(poly);

                    // Draw center point
                    painter.setBrush(QColor(0, 120, 215));
                    painter.drawEllipse(sc, 3, 3);

                    // Draw radius dimension along dashed radius line
                    QPoint radiusEnd = worldToScreen(m_currentMouseWorld);
                    {
                        // Dashed radius line from center to cursor
                        painter.save();
                        painter.setPen(QPen(QColor(128, 128, 128), 1, Qt::DashLine));
                        painter.drawLine(sc, radiusEnd);
                        painter.restore();

                        // Cross marker at cursor point
                        int crossSz = 4;
                        painter.drawLine(radiusEnd.x() - crossSz, radiusEnd.y(),
                                         radiusEnd.x() + crossSz, radiusEnd.y());
                        painter.drawLine(radiusEnd.x(), radiusEnd.y() - crossSz,
                                         radiusEnd.x(), radiusEnd.y() + crossSz);

                        // Label at midpoint of radius line, offset perpendicular
                        QPointF midPt = (QPointF(sc) + QPointF(radiusEnd)) / 2.0;
                        double rdx = radiusEnd.x() - sc.x();
                        double rdy = radiusEnd.y() - sc.y();
                        double rlen = std::sqrt(rdx * rdx + rdy * rdy);

                        // Perpendicular offset (always to the right of the line direction)
                        QPointF perpOff(0, 14);
                        if (rlen > 1.0) {
                            perpOff = QPointF(-rdy / rlen * 14, rdx / rlen * 14);
                        }
                        QPointF labelPos = midPt + perpOff;

                        if (m_dimActiveIndex >= 0 && !m_dimFields.isEmpty()) {
                            m_dimFields[0].currentValue = radius;
                            double screenAngle = std::atan2(rdy, rdx) * 180.0 / M_PI;
                            if (screenAngle > 90 || screenAngle < -90)
                                screenAngle += 180;
                            drawDimInputField(painter, labelPos, 0, screenAngle);
                        } else {
                            drawDimensionLabel(painter, labelPos, radius);
                        }
                    }
                }
            }
        }
        break;

    case SketchTool::Ellipse:
        if (!m_previewPoints.isEmpty()) {
            QPointF center = m_previewPoints[0];
            // Use constrained edge point from updateEntity
            QPointF edge = (m_pendingEntity.points.size() >= 2)
                ? m_pendingEntity.points[1] : m_currentMouseWorld;
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
                if (m_dimActiveIndex >= 0 && !m_dimFields.isEmpty()) {
                    m_dimFields[0].currentValue = majorR;
                    QPointF midPt = (QPointF(sc) + QPointF(edgeScreen)) / 2.0 + QPointF(0, 18);
                    drawDimInputField(painter, midPt, 0);
                } else {
                    drawPreviewDimension(painter, sc, edgeScreen, majorR);
                }
            }
        }
        break;

    default:
        break;
    }
}

void SketchCanvas::drawSelectionHandles(QPainter& painter, const SketchEntity& entity)
{
    // Arc-based entities use color-coded handles:
    //   handle 0 = center (blue), handle 1 = sweep/angle (green), handle 2 = radius (red)
    bool isArcBased = (entity.type == SketchEntityType::Arc && entity.points.size() >= 3)
                   || (entity.type == SketchEntityType::Slot && entity.points.size() >= 3);

    for (int i = 0; i < entity.points.size(); ++i) {
        QPoint p = worldToScreen(entity.points[i]);

        if (isArcBased && i == 1) {
            // Sweep/angle handle — GREEN
            painter.setPen(QPen(QColor(30, 160, 30), 2));
            painter.setBrush(QColor(150, 230, 150));
        } else if (isArcBased && i == 2) {
            // Radius handle — RED
            painter.setPen(QPen(QColor(200, 50, 50), 2));
            painter.setBrush(QColor(255, 150, 150));
        } else {
            // Default handle — BLUE/WHITE
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

    // Format the dimension value with unit conversion and suffix
    QString dimText = formatValueWithUnit(value, m_displayUnit);

    // Black text on solid white background (opaque so preview geometry doesn't bleed)
    painter.setPen(Qt::black);
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

    painter.fillRect(labelRect, Qt::white);

    // Draw the text
    painter.drawText(labelRect, Qt::AlignCenter, dimText);

    painter.restore();
}

void SketchCanvas::drawDimensionLabel(QPainter& painter, const QPointF& position, double value)
{
    // Draw a dimension label at a specific position
    // Black text on solid white background
    painter.save();

    QString dimText = formatValueWithUnit(value, m_displayUnit);
    painter.setPen(Qt::black);
    QFont font = painter.font();
    font.setPointSize(9);
    painter.setFont(font);

    QFontMetrics fm(font);
    QRect textRect = fm.boundingRect(dimText);

    QRectF labelRect(position.x() - textRect.width() / 2.0 - 3,
                     position.y() - textRect.height() / 2.0 - 1,
                     textRect.width() + 6, textRect.height() + 2);

    painter.fillRect(labelRect, Qt::white);
    painter.drawText(labelRect, Qt::AlignCenter, dimText);

    painter.restore();
}

void SketchCanvas::drawArcDimensionLabel(QPainter& painter, const QPointF& position, double arcLength, double angleDeg)
{
    // Draw a two-line dimension label showing arc length and angle in degrees
    // Black text on solid white background
    painter.save();

    QString line1 = formatValueWithUnit(arcLength, m_displayUnit);
    QString line2 = formatAngle(std::abs(angleDeg));

    painter.setPen(Qt::black);
    QFont font = painter.font();
    font.setPointSize(9);
    painter.setFont(font);

    QFontMetrics fm(font);
    int line1Width = fm.horizontalAdvance(line1);
    int line2Width = fm.horizontalAdvance(line2);
    int maxWidth = std::max(line1Width, line2Width);
    int lineHeight = fm.height();
    int totalHeight = lineHeight * 2 + 2;

    QRectF labelRect(position.x() - maxWidth / 2.0 - 4,
                     position.y() - totalHeight / 2.0 - 2,
                     maxWidth + 8, totalHeight + 4);

    painter.fillRect(labelRect, Qt::white);

    // Draw first line (arc length with unit)
    QRectF line1Rect(labelRect.x(), labelRect.y() + 2, labelRect.width(), lineHeight);
    painter.drawText(line1Rect, Qt::AlignCenter, line1);

    // Draw second line (angle with degree symbol)
    QRectF line2Rect(labelRect.x(), labelRect.y() + lineHeight + 2, labelRect.width(), lineHeight);
    painter.drawText(line2Rect, Qt::AlignCenter, line2);

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

    case SnapType::Point:
        // Filled circle for standalone sketch points
        painter.setBrush(snapColor);
        painter.drawEllipse(screenPos, size, size);
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

    case SnapType::Nearest:
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

    case SnapType::Origin:
        // Crosshair with circle for origin
        painter.drawEllipse(screenPos, size + 2, size + 2);
        painter.drawLine(screenPos.x() - size - 4, screenPos.y(), screenPos.x() + size + 4, screenPos.y());
        painter.drawLine(screenPos.x(), screenPos.y() - size - 4, screenPos.x(), screenPos.y() + size + 4);
        break;

    case SnapType::AxisX:
        // Horizontal line indicator for X axis
        painter.drawLine(screenPos.x() - size, screenPos.y(), screenPos.x() + size, screenPos.y());
        painter.drawLine(screenPos.x() - size, screenPos.y() - 3, screenPos.x() - size, screenPos.y() + 3);
        painter.drawLine(screenPos.x() + size, screenPos.y() - 3, screenPos.x() + size, screenPos.y() + 3);
        break;

    case SnapType::AxisY:
        // Vertical line indicator for Y axis
        painter.drawLine(screenPos.x(), screenPos.y() - size, screenPos.x(), screenPos.y() + size);
        painter.drawLine(screenPos.x() - 3, screenPos.y() - size, screenPos.x() + 3, screenPos.y() - size);
        painter.drawLine(screenPos.x() - 3, screenPos.y() + size, screenPos.x() + 3, screenPos.y() + size);
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

    // Get constraint endpoints (geometry points being dimensioned)
    QPointF p1, p2;
    if (!getConstraintEndpoints(constraint, p1, p2)) return;

    QPointF sp1 = worldToScreen(p1).toPointF();
    QPointF sp2 = worldToScreen(p2).toPointF();
    QPointF labelCenter = worldToScreen(constraint.labelPosition).toPointF();

    // ---- Dimension line direction and offset ----------------------------
    QPointF along = sp2 - sp1;
    double len = std::sqrt(along.x() * along.x() + along.y() * along.y());
    if (len < 1.0) return;

    QPointF dir = along / len;                      // unit direction
    QPointF perp(-dir.y(), dir.x());                // perpendicular (outward)

    // Angle of the dimension line (for rotating text)
    double angleDeg = std::atan2(along.y(), along.x()) * 180.0 / M_PI;
    // Flip so text is always readable (never upside-down)
    if (angleDeg > 90.0)  angleDeg -= 180.0;
    if (angleDeg < -90.0) angleDeg += 180.0;

    // Project the label position onto the perpendicular to get the offset
    QPointF labelDelta = labelCenter - sp1;
    double offset = labelDelta.x() * perp.x() + labelDelta.y() * perp.y();

    // Projected endpoints on the dimension line (parallel, offset from geometry)
    QPointF d1 = sp1 + perp * offset;
    QPointF d2 = sp2 + perp * offset;

    // ---- Extension (witness) lines --------------------------------------
    double extOvershoot = 3.0;
    double extGap = 2.0;
    QPen extPen(painter.pen().color(), 1, Qt::SolidLine);
    painter.setPen(extPen);

    QPointF extDir = (offset >= 0) ? perp : -perp;
    double absOffset = std::abs(offset);
    if (absOffset > extGap + 1.0) {
        painter.drawLine(sp1 + extDir * extGap, sp1 + extDir * (absOffset + extOvershoot));
        painter.drawLine(sp2 + extDir * extGap, sp2 + extDir * (absOffset + extOvershoot));
    }

    // ---- Value text -----------------------------------------------------
    QString text = formatValueWithUnit(constraint.value, m_displayUnit);
    if (!constraint.isDriving) {
        text = QStringLiteral("(") + text + QStringLiteral(")");
    }
    QFontMetricsF fm(painter.font());
    double textWidth = fm.horizontalAdvance(text);
    double textHeight = fm.height();

    QPointF dimMid = (d1 + d2) / 2.0;
    double halfText = textWidth / 2.0 + 3.0;  // padding
    bool textFits = (halfText * 2.0 < len);

    QPen dimPen(painter.pen().color(), 1.5, Qt::SolidLine);
    painter.setPen(dimPen);

    if (textFits) {
        // ---- NORMAL: text between ticks ----------------------------------
        // Dimension line with gap for text
        QPointF gapStart = dimMid - dir * halfText;
        QPointF gapEnd   = dimMid + dir * halfText;
        painter.drawLine(d1, gapStart);
        painter.drawLine(gapEnd, d2);

        // Perpendicular tick terminators at each end
        double tickSize = 5.0;
        painter.drawLine(d1 - perp * tickSize, d1 + perp * tickSize);
        painter.drawLine(d2 - perp * tickSize, d2 + perp * tickSize);

        // Rotated text centred in the gap
        painter.save();
        painter.translate(dimMid);
        painter.rotate(angleDeg);

        QRectF textRect(-textWidth / 2.0 - 2,
                        -textHeight / 2.0 - 1,
                         textWidth + 4, textHeight + 2);
        painter.fillRect(textRect, Qt::white);
        painter.drawText(textRect, Qt::AlignCenter, text);
        painter.restore();
    } else {
        // ---- COMPACT: inward arrows, text outside ------------------------
        // Two small filled arrowheads pointing inward (toward each other)
        double arrowLen = 8.0;
        double arrowHalf = 3.0;

        // Arrow at d1 pointing toward d2
        QPointF a1Tip  = d1;
        QPointF a1Base = d1 + dir * arrowLen;
        QPolygonF arrow1;
        arrow1 << a1Tip
               << (a1Base + perp * arrowHalf)
               << (a1Base - perp * arrowHalf);
        painter.setBrush(painter.pen().color());
        painter.drawPolygon(arrow1);

        // Arrow at d2 pointing toward d1
        QPointF a2Tip  = d2;
        QPointF a2Base = d2 - dir * arrowLen;
        QPolygonF arrow2;
        arrow2 << a2Tip
               << (a2Base + perp * arrowHalf)
               << (a2Base - perp * arrowHalf);
        painter.drawPolygon(arrow2);
        painter.setBrush(Qt::NoBrush);

        // Short leader line extending outward from d2, then text
        double leaderLen = 12.0;
        double textGap = 4.0;
        QPointF leaderEnd = d2 + dir * leaderLen;
        painter.drawLine(d2, leaderEnd);

        // Rotated text placed outside, past the leader
        QPointF textPos = leaderEnd + dir * (textWidth / 2.0 + textGap);
        painter.save();
        painter.translate(textPos);
        painter.rotate(angleDeg);

        QRectF textRect(-textWidth / 2.0 - 2,
                        -textHeight / 2.0 - 1,
                         textWidth + 4, textHeight + 2);
        painter.fillRect(textRect, Qt::white);
        painter.drawText(textRect, Qt::AlignCenter, text);
        painter.restore();
    }
}

void SketchCanvas::drawRadialConstraint(QPainter& painter, const SketchConstraint& constraint)
{
    if (constraint.entityIds.isEmpty()) return;

    const SketchEntity* entity = entityById(constraint.entityIds[0]);
    if (!entity || (entity->type != SketchEntityType::Circle && entity->type != SketchEntityType::Arc)) {
        return;
    }

    if (entity->points.isEmpty()) return;

    QPointF sc = worldToScreen(entity->points[0]).toPointF();
    QPointF labelPt = worldToScreen(constraint.labelPosition).toPointF();
    double radiusPx = entity->radius * m_zoom;

    // Direction from center toward label (= toward first vertex)
    QPointF along = labelPt - sc;
    double alongLen = std::sqrt(along.x() * along.x() + along.y() * along.y());
    if (alongLen < 1e-6) return;

    QPointF dir = along / alongLen;
    QPointF perp(-dir.y(), dir.x());

    // Edge point on circle
    QPointF edgePt = sc + dir * radiusPx;

    // Angle for text rotation
    double angleDeg = std::atan2(dir.y(), dir.x()) * 180.0 / M_PI;
    if (angleDeg > 90.0)  angleDeg -= 180.0;
    if (angleDeg < -90.0) angleDeg += 180.0;

    // Value text
    QString prefix = (constraint.type == ConstraintType::Radius) ? QStringLiteral("R") : QStringLiteral("Ø");
    QString text = prefix + formatValueWithUnit(constraint.value, m_displayUnit);
    if (!constraint.isDriving) {
        text = QStringLiteral("(") + text + QStringLiteral(")");
    }

    QFontMetricsF fm(painter.font());
    double textWidth = fm.horizontalAdvance(text);
    double textHeight = fm.height();
    double halfText = textWidth / 2.0 + 3.0;
    bool textFits = (halfText * 2.0 < radiusPx);

    QPointF dimMid = (sc + edgePt) / 2.0;

    if (textFits) {
        // ---- NORMAL: text inside, dashed line with gap for label -----------
        QPointF gapStart = dimMid - dir * halfText;
        QPointF gapEnd   = dimMid + dir * halfText;

        QPen dashPen(painter.pen().color(), 1, Qt::DashLine);
        painter.setPen(dashPen);
        painter.drawLine(sc.toPoint(), gapStart.toPoint());
        painter.drawLine(gapEnd.toPoint(), edgePt.toPoint());

        // Perpendicular tick terminators at center and edge
        QPen tickPen(painter.pen().color(), 1.5, Qt::SolidLine);
        painter.setPen(tickPen);
        double tickSize = 5.0;
        painter.drawLine((sc - perp * tickSize).toPoint(), (sc + perp * tickSize).toPoint());
        painter.drawLine((edgePt - perp * tickSize).toPoint(), (edgePt + perp * tickSize).toPoint());

        // Rotated text centred in the gap
        painter.save();
        painter.translate(dimMid);
        painter.rotate(angleDeg);

        QRectF textRect(-textWidth / 2.0 - 2,
                        -textHeight / 2.0 - 1,
                         textWidth + 4, textHeight + 2);
        painter.fillRect(textRect, Qt::white);
        painter.setPen(QPen(painter.pen().color(), 1, Qt::SolidLine));
        painter.drawText(textRect, Qt::AlignCenter, text);
        painter.restore();
    } else {
        // ---- COMPACT: inward arrows, text outside circle -------------------
        double arrowLen = 8.0;
        double arrowHalf = 3.0;

        // Dashed line from center to edge
        QPen dashPen(painter.pen().color(), 1, Qt::DashLine);
        painter.setPen(dashPen);
        painter.drawLine(sc.toPoint(), edgePt.toPoint());

        // Arrow at center pointing toward edge (3px gap from center point)
        double arrowGap = 3.0;
        QPen arrowPen(painter.pen().color(), 1.5, Qt::SolidLine);
        painter.setPen(arrowPen);
        QPointF a1Tip = sc + dir * arrowGap;
        QPointF a1Base = a1Tip + dir * arrowLen;
        QPolygonF arrow1;
        arrow1 << a1Tip << (a1Base + perp * arrowHalf) << (a1Base - perp * arrowHalf);
        painter.setBrush(painter.pen().color());
        painter.drawPolygon(arrow1);

        // Arrow at edge pointing toward center (3px gap from edge point)
        QPointF a2Tip = edgePt - dir * arrowGap;
        QPointF a2Base = a2Tip - dir * arrowLen;
        QPolygonF arrow2;
        arrow2 << a2Tip << (a2Base + perp * arrowHalf) << (a2Base - perp * arrowHalf);
        painter.drawPolygon(arrow2);
        painter.setBrush(Qt::NoBrush);

        // Leader line extending outward from edge, then text
        double leaderLen = 12.0;
        double textGap = 4.0;
        QPointF leaderEnd = edgePt + dir * leaderLen;
        painter.drawLine(edgePt.toPoint(), leaderEnd.toPoint());

        // Rotated text placed outside, past the leader
        QPointF textPos = leaderEnd + dir * (textWidth / 2.0 + textGap);
        painter.save();
        painter.translate(textPos);
        painter.rotate(angleDeg);

        QRectF textRect(-textWidth / 2.0 - 2,
                        -textHeight / 2.0 - 1,
                         textWidth + 4, textHeight + 2);
        painter.fillRect(textRect, Qt::white);
        painter.setPen(QPen(painter.pen().color(), 1, Qt::SolidLine));
        painter.drawText(textRect, Qt::AlignCenter, text);
        painter.restore();
    }
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

    // Find intersection point of the two lines
    QLineF line1(e1->points[0], e1->points[1]);
    QLineF line2(e2->points[0], e2->points[1]);

    QPointF intersection;
    QLineF::IntersectionType intersectType = line1.intersects(line2, &intersection);

    if (intersectType == QLineF::NoIntersection) {
        intersection = constraint.labelPosition;
    }

    QPointF originScreen = worldToScreen(intersection).toPointF();

    // ---- Compute angles of the two lines in screen space ----------------
    // Note: screen Y is inverted, so negate Y for angle calculation
    QPointF s1a = worldToScreen(e1->points[0]).toPointF();
    QPointF s1b = worldToScreen(e1->points[1]).toPointF();
    QPointF s2a = worldToScreen(e2->points[0]).toPointF();
    QPointF s2b = worldToScreen(e2->points[1]).toPointF();

    // Use the ray direction from the intersection toward the far end of each line
    QPointF dir1 = s1b - s1a;
    QPointF dir2 = s2b - s2a;
    // Orient rays away from intersection (pick the end farther from intersection)
    if (QLineF(originScreen, s1a).length() > QLineF(originScreen, s1b).length())
        dir1 = s1a - s1b;
    if (QLineF(originScreen, s2a).length() > QLineF(originScreen, s2b).length())
        dir2 = s2a - s2b;

    double a1 = std::atan2(-dir1.y(), dir1.x());   // negate Y for math coords
    double a2 = std::atan2(-dir2.y(), dir2.x());

    // Ensure we sweep the smaller angle between them
    double sweep = a2 - a1;
    while (sweep > M_PI)  sweep -= 2.0 * M_PI;
    while (sweep < -M_PI) sweep += 2.0 * M_PI;

    double startAngle = a1;
    double sweepAngle = sweep;

    // ---- Arc radius (screen pixels) --------------------------------------
    double arcRadius = 35.0;

    // ---- Draw the arc ----------------------------------------------------
    QPen arcPen(painter.pen().color(), 1.5, Qt::SolidLine);
    painter.setPen(arcPen);

    // Draw arc as a polyline for precision (QPainter::drawArc uses 1/16 degree ints)
    const int segments = 40;
    QVector<QPointF> arcPoints;
    arcPoints.reserve(segments + 1);
    for (int i = 0; i <= segments; ++i) {
        double t = static_cast<double>(i) / segments;
        double angle = startAngle + sweepAngle * t;
        // Convert back to screen coords (negate Y)
        arcPoints.append(QPointF(originScreen.x() + arcRadius * std::cos(angle),
                                 originScreen.y() - arcRadius * std::sin(angle)));
    }
    for (int i = 0; i < arcPoints.size() - 1; ++i) {
        painter.drawLine(arcPoints[i], arcPoints[i + 1]);
    }

    // ---- Arrowheads at both ends of the arc ------------------------------
    double arrowSize = 7.0;
    // Tangent at start (first end): perpendicular to radial direction
    {
        double angle = startAngle;
        // Tangent direction along increasing sweep
        double tx = -std::sin(angle) * (sweepAngle > 0 ? 1 : -1);
        double ty = -std::cos(angle) * (sweepAngle > 0 ? 1 : -1);
        QPointF tip = arcPoints.first();
        // Tangent in screen coords (negate ty since screen Y is flipped)
        QPointF tangent(tx, ty);
        drawArrow(painter, tip, tangent, arrowSize);
    }
    // Tangent at end (second end): perpendicular to radial, reversed
    {
        double angle = startAngle + sweepAngle;
        double tx = std::sin(angle) * (sweepAngle > 0 ? 1 : -1);
        double ty = std::cos(angle) * (sweepAngle > 0 ? 1 : -1);
        QPointF tip = arcPoints.last();
        QPointF tangent(tx, ty);
        drawArrow(painter, tip, tangent, arrowSize);
    }

    // ---- Extension lines from intersection to arc ends -------------------
    QPen extPen(painter.pen().color(), 1, Qt::SolidLine);
    painter.setPen(extPen);
    double extOvershoot = 5.0;
    QPointF radDir1 = arcPoints.first() - originScreen;
    double radLen1 = std::sqrt(radDir1.x() * radDir1.x() + radDir1.y() * radDir1.y());
    if (radLen1 > 1.0) {
        QPointF rn1 = radDir1 / radLen1;
        painter.drawLine(originScreen, arcPoints.first() + rn1 * extOvershoot);
    }
    QPointF radDir2 = arcPoints.last() - originScreen;
    double radLen2 = std::sqrt(radDir2.x() * radDir2.x() + radDir2.y() * radDir2.y());
    if (radLen2 > 1.0) {
        QPointF rn2 = radDir2 / radLen2;
        painter.drawLine(originScreen, arcPoints.last() + rn2 * extOvershoot);
    }

    // ---- Value text at midpoint of arc -----------------------------------
    double midAngle = startAngle + sweepAngle / 2.0;
    double textRadius = arcRadius + 14.0;
    QPointF textCenter(originScreen.x() + textRadius * std::cos(midAngle),
                       originScreen.y() - textRadius * std::sin(midAngle));

    QString text = formatAngle(constraint.value);
    if (!constraint.isDriving) {
        text = QStringLiteral("(") + text + QStringLiteral(")");
    }

    QFontMetricsF fm(painter.font());
    double textW = fm.horizontalAdvance(text);
    double textH = fm.height();
    QRectF textRect(textCenter.x() - textW / 2.0 - 2,
                    textCenter.y() - textH / 2.0 - 1,
                    textW + 4, textH + 2);

    painter.fillRect(textRect, Qt::white);
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

    // Right-click to finish multi-click entities (spline, freeform polygon)
    if (event->button() == Qt::RightButton) {
        if (m_isDrawing && (m_activeTool == SketchTool::Spline
                            || (m_activeTool == SketchTool::Polygon
                                && m_polygonMode == PolygonMode::Freeform))) {
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
            // First check if clicking on a handle — use group-aware test
            // so any corner of a decomposed rectangle is draggable.
            int handleEntityId = -1, handleIdx = -1;
            bool handleHit = hitTestGroupHandle(worldPos, handleEntityId, handleIdx);
            if (handleHit && handleIdx >= 0) {
                // If the handle belongs to a different entity than the
                // current primary, switch the primary so the drag code
                // operates on the correct entity.
                if (handleEntityId != m_selectedId) {
                    m_selectedId = handleEntityId;
                }

                // Start dragging the handle
                m_isDraggingHandle = true;
                m_dragHandleIndex = handleIdx;
                m_dragStartWorld = worldPos;
                m_lastRawMouseWorld = worldPos;
                m_shiftWasPressed = (event->modifiers() & Qt::ShiftModifier);
                m_ctrlWasPressed = (event->modifiers() & Qt::ControlModifier);

                // Store original handle positions for constraint behavior
                SketchEntity* sel = entityById(handleEntityId);
                if (sel && handleIdx < sel->points.size()) {
                    m_dragHandleOriginal = sel->points[handleIdx];
                    if (sel->points.size() > 1) {
                        m_dragHandleOriginal2 = sel->points[1];
                    }
                    m_dragOriginalRadius = sel->radius;
                }

                setCursor(Qt::ArrowCursor);
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

            // If the sketch was deselected (Save/Discard bar visible),
            // any click on the canvas re-engages the sketch.
            if (!m_sketchSelected) {
                m_sketchSelected = true;
                emit selectionChanged(-1);  // re-engage sketch
                // Fall through to normal selection handling below
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

                // Use selectEntity with addToSelection based on Ctrl key.
                // Group expansion is handled by enter-group mode now:
                //   normal click = whole group; double-click = enter group
                selectEntity(hitId, ctrlHeld);
            } else {
                // Clicked on empty space

                // If inside a group, leave it first
                if (m_enteredGroupId >= 0) {
                    leaveGroup();
                    // Don't start window selection — just leave the group
                    return;
                }

                // Start window selection
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

                    // ArcRadius stage 1: locked Radius constrains start distance from arc center
                    if (m_slotMode == SlotMode::ArcRadius && m_pendingEntity.points.size() == 1) {
                        double lockedR = getLockedDim(0);
                        if (lockedR > 0) {
                            QPointF arcCenter = m_pendingEntity.points[0];
                            QPointF dir = snapped - arcCenter;
                            double len = std::sqrt(dir.x() * dir.x() + dir.y() * dir.y());
                            if (len > 1e-6)
                                snapped = arcCenter + dir * (lockedR / len);
                        }
                    }

                    // ArcRadius stage 2: constrain end to arc + apply locked sweep angle
                    if (m_slotMode == SlotMode::ArcRadius && m_pendingEntity.points.size() == 2) {
                        QPointF arcCenterWorld = m_pendingEntity.points[0];
                        QPointF startWorld = m_pendingEntity.points[1];
                        double arcRadius = QLineF(arcCenterWorld, startWorld).length();
                        double mouseAngle = std::atan2(snapped.y() - arcCenterWorld.y(),
                                                       snapped.x() - arcCenterWorld.x());
                        double startAngle = std::atan2(startWorld.y() - arcCenterWorld.y(),
                                                       startWorld.x() - arcCenterWorld.x());

                        // Apply locked sweep angle if present
                        double lockedSweep = getLockedDim(0);  // Sweep Angle (stage 2 field 0)
                        if (lockedSweep != -1.0) {
                            double angleDiff = mouseAngle - startAngle;
                            while (angleDiff > M_PI) angleDiff -= 2.0 * M_PI;
                            while (angleDiff < -M_PI) angleDiff += 2.0 * M_PI;
                            if (m_arcSlotFlipped) {
                                angleDiff = (angleDiff > 0) ? angleDiff - 2.0 * M_PI : angleDiff + 2.0 * M_PI;
                            }
                            double sign = (angleDiff >= 0) ? 1.0 : -1.0;
                            mouseAngle = startAngle + sign * qDegreesToRadians(std::abs(lockedSweep));
                        } else {
                            // Minimum angular separation so slot ends don't overlap
                            double slotRadius = m_pendingEntity.radius;
                            if (slotRadius < 0.1) slotRadius = 5.0;
                            double minAngularSep = (arcRadius > 0.001) ? (2.0 * slotRadius / arcRadius) : 0.1;
                            double angleDiff = mouseAngle - startAngle;
                            while (angleDiff > M_PI) angleDiff -= 2 * M_PI;
                            while (angleDiff < -M_PI) angleDiff += 2 * M_PI;
                            if (std::abs(angleDiff) < minAngularSep) {
                                double sign = (angleDiff >= 0) ? 1.0 : -1.0;
                                mouseAngle = startAngle + sign * minAngularSep;
                            }
                        }

                        snapped = arcCenterWorld + QPointF(arcRadius * std::cos(mouseAngle),
                                                           arcRadius * std::sin(mouseAngle));
                    }

                    // ArcEnds stage 2: locked sweep constrains arc center on perp bisector
                    if (m_slotMode == SlotMode::ArcEnds && m_pendingEntity.points.size() == 2) {
                        double lockedSweep = getLockedDim(0);
                        if (lockedSweep != -1.0) {
                            QPointF start = m_pendingEntity.points[0];
                            QPointF end = m_pendingEntity.points[1];
                            QPointF midpoint = (start + end) / 2.0;
                            double chordLen = QLineF(start, end).length();
                            if (chordLen > 0.001) {
                                QPointF startToEnd = end - start;
                                QPointF perpDir(-startToEnd.y() / chordLen, startToEnd.x() / chordLen);
                                double halfSweepRad = qDegreesToRadians(std::abs(lockedSweep)) / 2.0;
                                double tanHalf = std::tan(halfSweepRad);
                                double projDist = (tanHalf > 1e-6) ? (chordLen / 2.0) / tanHalf : 1e6;
                                QPointF mouseToMid = snapped - midpoint;
                                double mouseProjDist = mouseToMid.x() * perpDir.x() + mouseToMid.y() * perpDir.y();
                                double sign = (mouseProjDist >= 0) ? 1.0 : -1.0;
                                snapped = midpoint + perpDir * sign * projDist;
                            }
                        }
                    }

                    m_pendingEntity.points.append(snapped);
                    m_previewPoints.append(snapped);
                    if (m_pendingEntity.points.size() >= 3) {
                        // m_arcSlotFlipped is already set via Shift key toggle
                        finishEntity();
                    } else {
                        initDimFields();  // Stage transition: new dim fields for next stage
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

                    // Apply locked dimension constraints
                    int pStage = m_previewPoints.size();
                    if (pStage == 1) {
                        QPointF p1 = m_previewPoints[0];
                        double lockedLen = getLockedDim(0);
                        double lockedAng = getLockedDim(1);
                        if (lockedLen > 0 || lockedAng != -1.0) {
                            QPointF dir = snapped - p1;
                            double mouseLen = std::sqrt(dir.x() * dir.x() + dir.y() * dir.y());
                            double mouseAng = std::atan2(dir.y(), dir.x());
                            double useLen = (lockedLen > 0) ? lockedLen : mouseLen;
                            double useAng = (lockedAng != -1.0) ? qDegreesToRadians(lockedAng) : mouseAng;
                            if (useLen > 0.001)
                                snapped = p1 + QPointF(useLen * std::cos(useAng), useLen * std::sin(useAng));
                        }
                    } else if (pStage >= 2) {
                        double lockedW = getLockedDim(0);
                        if (lockedW > 0) {
                            QPointF p1 = m_previewPoints[0];
                            QPointF p2 = m_previewPoints[1];
                            QPointF edge = p2 - p1;
                            double edgeLen = std::sqrt(edge.x() * edge.x() + edge.y() * edge.y());
                            if (edgeLen > 0.001) {
                                QPointF edgeDir = edge / edgeLen;
                                QPointF perpDir(-edgeDir.y(), edgeDir.x());
                                QPointF toMouse = snapped - p1;
                                double perpDot = toMouse.x() * perpDir.x() + toMouse.y() * perpDir.y();
                                double sign = (perpDot >= 0) ? 1.0 : -1.0;
                                double edgeDot = toMouse.x() * edgeDir.x() + toMouse.y() * edgeDir.y();
                                snapped = p1 + edgeDir * edgeDot + perpDir * sign * lockedW;
                            }
                        }
                    }

                    m_pendingEntity.points.append(snapped);
                    m_previewPoints.append(snapped);
                    if (m_pendingEntity.points.size() >= 3) {
                        finishEntity();
                    } else {
                        initDimFields();  // Stage transition: new dim fields for next stage
                        update();
                    }
                    return;
                }

                // Parallelogram mode (under Rectangle): 3 clicks define corners, 4th is computed
                bool isParallelogram = (m_activeTool == SketchTool::Rectangle &&
                                        m_rectMode == RectMode::Parallelogram);
                if (isParallelogram) {
                    // Reset drag detection for this click
                    m_drawStartPos = event->pos();
                    m_wasDragged = false;

                    QPointF snapped = snapPoint(worldPos);

                    // Apply locked dimension constraints
                    int pStage = m_previewPoints.size();
                    if (pStage == 1) {
                        QPointF p1 = m_previewPoints[0];
                        double lockedLen = getLockedDim(0);
                        double lockedAng = getLockedDim(1);
                        if (lockedLen > 0 || lockedAng != -1.0) {
                            QPointF dir = snapped - p1;
                            double mouseLen = std::sqrt(dir.x() * dir.x() + dir.y() * dir.y());
                            double mouseAng = std::atan2(dir.y(), dir.x());
                            double useLen = (lockedLen > 0) ? lockedLen : mouseLen;
                            double useAng = (lockedAng != -1.0) ? qDegreesToRadians(lockedAng) : mouseAng;
                            if (useLen > 0.001)
                                snapped = p1 + QPointF(useLen * std::cos(useAng), useLen * std::sin(useAng));
                        }
                    } else if (pStage >= 2) {
                        QPointF p1 = m_previewPoints[0];
                        QPointF p2 = m_previewPoints[1];
                        double lockedLen = getLockedDim(0);
                        double lockedAng = getLockedDim(1);
                        if (lockedLen > 0 || lockedAng != -1.0) {
                            QPointF dir = snapped - p2;
                            double mouseLen = std::sqrt(dir.x() * dir.x() + dir.y() * dir.y());
                            double mouseAng = std::atan2(dir.y(), dir.x());
                            double useLen = (lockedLen > 0) ? lockedLen : mouseLen;
                            double useAng;
                            if (lockedAng != -1.0) {
                                double edge1Dir = std::atan2(p1.y() - p2.y(), p1.x() - p2.x());
                                double dir1 = edge1Dir + qDegreesToRadians(lockedAng);
                                double dir2 = edge1Dir - qDegreesToRadians(lockedAng);
                                double diff1 = std::abs(std::remainder(mouseAng - dir1, 2.0 * M_PI));
                                double diff2 = std::abs(std::remainder(mouseAng - dir2, 2.0 * M_PI));
                                useAng = (diff1 <= diff2) ? dir1 : dir2;
                            } else {
                                useAng = mouseAng;
                            }
                            if (useLen > 0.001)
                                snapped = p2 + QPointF(useLen * std::cos(useAng), useLen * std::sin(useAng));
                        }
                    }

                    m_pendingEntity.points.append(snapped);
                    m_previewPoints.append(snapped);
                    if (m_pendingEntity.points.size() >= 3) {
                        finishEntity();
                    } else {
                        initDimFields();  // Stage transition: new dim fields for next stage
                        update();
                    }
                    return;
                }

                // 3-point circle: add points on click
                bool isThreePointCircle = (m_activeTool == SketchTool::Circle &&
                                           m_circleMode == CircleMode::ThreePoint);
                if (isThreePointCircle) {
                    // Reset drag detection for this click
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

                // 3-point arc: add points on click
                bool isThreePointArc = (m_activeTool == SketchTool::Arc &&
                                        m_arcMode == ArcMode::ThreePoint);
                if (isThreePointArc) {
                    // Reset drag detection for this click
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

                // Center-Start-End arc: add points on click
                bool isCenterStartEndArc = (m_activeTool == SketchTool::Arc &&
                                            m_arcMode == ArcMode::CenterStartEnd);
                if (isCenterStartEndArc) {
                    // Reset drag detection for this click
                    m_drawStartPos = event->pos();
                    m_wasDragged = false;

                    QPointF snapped = snapPoint(worldPos);

                    // Stage 1: locked Radius constrains distance from center
                    if (m_pendingEntity.points.size() == 1) {
                        double lockedR = getLockedDim(0);
                        if (lockedR > 0) {
                            QPointF center = m_pendingEntity.points[0];
                            QPointF dir = snapped - center;
                            double len = std::sqrt(dir.x() * dir.x() + dir.y() * dir.y());
                            if (len > 1e-6)
                                snapped = center + dir * (lockedR / len);
                        }
                    }

                    // Stage 2: constrain to arc radius + apply locked sweep angle
                    if (m_pendingEntity.points.size() == 2) {
                        QPointF center = m_pendingEntity.points[0];
                        QPointF start = m_pendingEntity.points[1];
                        double radius = QLineF(center, start).length();
                        double angle = std::atan2(snapped.y() - center.y(), snapped.x() - center.x());
                        // Apply locked sweep angle
                        double lockedSweep = getLockedDim(0);  // Sweep Angle (stage 2 field 0)
                        if (lockedSweep != -1.0) {
                            double startAngle = std::atan2(start.y() - center.y(), start.x() - center.x());
                            double defaultSweep = angle - startAngle;
                            while (defaultSweep > M_PI) defaultSweep -= 2.0 * M_PI;
                            while (defaultSweep < -M_PI) defaultSweep += 2.0 * M_PI;
                            if (m_arcSlotFlipped) {
                                defaultSweep = (defaultSweep > 0) ? defaultSweep - 2.0 * M_PI : defaultSweep + 2.0 * M_PI;
                            }
                            double sign = (defaultSweep >= 0) ? 1.0 : -1.0;
                            angle = startAngle + sign * qDegreesToRadians(std::abs(lockedSweep));
                        }
                        snapped = center + QPointF(radius * std::cos(angle), radius * std::sin(angle));
                    }

                    m_pendingEntity.points.append(snapped);
                    m_previewPoints.append(snapped);
                    if (m_pendingEntity.points.size() >= 3) {
                        finishEntity();
                    } else {
                        initDimFields();  // Stage transition: new dim fields for next stage
                        update();
                    }
                    return;
                }

                // Start-End-Radius arc: add points on click
                // First click = start (fixed), second click = end (fixed), third click defines radius
                bool isStartEndRadiusArc = (m_activeTool == SketchTool::Arc &&
                                            m_arcMode == ArcMode::StartEndRadius);
                if (isStartEndRadiusArc) {
                    // Reset drag detection for this click
                    m_drawStartPos = event->pos();
                    m_wasDragged = false;

                    QPointF snapped = snapPoint(worldPos);

                    // Apply locked dimension constraints
                    int pStage = m_previewPoints.size();
                    if (pStage == 1) {
                        // Stage 1: placing end point — locked Chord Length/Angle
                        QPointF start = m_previewPoints[0];
                        double lockedLen = getLockedDim(0);
                        double lockedAng = getLockedDim(1);
                        if (lockedLen > 0 || lockedAng != -1.0) {
                            QPointF dir = snapped - start;
                            double mouseLen = std::sqrt(dir.x() * dir.x() + dir.y() * dir.y());
                            double mouseAng = std::atan2(dir.y(), dir.x());
                            double useLen = (lockedLen > 0) ? lockedLen : mouseLen;
                            double useAng = (lockedAng != -1.0) ? qDegreesToRadians(lockedAng) : mouseAng;
                            if (useLen > 0.001)
                                snapped = start + QPointF(useLen * std::cos(useAng), useLen * std::sin(useAng));
                        }
                    } else if (pStage >= 2) {
                        // Stage 2: placing arc center — locked Sweep constrains perp bisector position
                        double lockedSweep = getLockedDim(0);
                        if (lockedSweep != -1.0) {
                            QPointF start = m_previewPoints[0];
                            QPointF end = m_previewPoints[1];
                            QPointF midChord = (start + end) / 2.0;
                            double chordLength = QLineF(start, end).length();
                            if (chordLength > 0.001) {
                                QPointF chordDir = (end - start) / chordLength;
                                QPointF perpDir(-chordDir.y(), chordDir.x());
                                double halfSweepRad = qDegreesToRadians(std::abs(lockedSweep)) / 2.0;
                                double tanHalf = std::tan(halfSweepRad);
                                double projDist = (tanHalf > 1e-6) ? (chordLength / 2.0) / tanHalf : 1e6;
                                QPointF toMouse = snapped - midChord;
                                double mouseProjDist = toMouse.x() * perpDir.x() + toMouse.y() * perpDir.y();
                                if (m_arcSlotFlipped) mouseProjDist = -mouseProjDist;
                                double sign = (mouseProjDist >= 0) ? 1.0 : -1.0;
                                if (m_arcSlotFlipped) sign = -sign;
                                snapped = midChord + perpDir * sign * projDist;
                            }
                        }
                    }

                    m_pendingEntity.points.append(snapped);
                    m_previewPoints.append(snapped);
                    if (m_pendingEntity.points.size() >= 3) {
                        finishEntity();
                    } else {
                        initDimFields();  // Stage transition: new dim fields for next stage
                        update();
                    }
                    return;
                }

                // For two-point tools, second click finishes the entity
                // (Arc and Spline have their own multi-click logic in mouseReleaseEvent)
                // (Arc slots and 3-point rectangles are handled above and return early)
                // (Point finishes on release, not second click)
                // Note: Tangent arc is a two-click tool (tangent point + end point)
                // Note: Tangent line needs special handling to constrain endpoint
                bool isMultiClickTool = (m_activeTool == SketchTool::Arc && m_arcMode != ArcMode::Tangent) ||
                                        (m_activeTool == SketchTool::Circle && m_circleMode == CircleMode::ThreePoint) ||
                                        (m_activeTool == SketchTool::Spline) ||
                                        (m_activeTool == SketchTool::Point);
                if (!isMultiClickTool) {
                    // Update the endpoint to the constrained position and finish
                    // m_currentMouseWorld already has angle snap and other constraints applied
                    updateEntity(m_currentMouseWorld);
                    finishEntity();
                    return;
                }
            }

            // 3-point arc, Center-Start-End arc, or Start-End-Radius arc: if not drawing yet, start entity on first click
            if (m_activeTool == SketchTool::Arc &&
                (m_arcMode == ArcMode::ThreePoint || m_arcMode == ArcMode::CenterStartEnd || m_arcMode == ArcMode::StartEndRadius) &&
                !m_isDrawing) {
                m_drawStartPos = event->pos();
                m_wasDragged = false;
                startEntity(snapPoint(worldPos));
                return;
            }

            // 3-point circle: if not drawing yet, start entity on first click
            if (m_activeTool == SketchTool::Circle &&
                m_circleMode == CircleMode::ThreePoint &&
                !m_isDrawing) {
                m_drawStartPos = event->pos();
                m_wasDragged = false;
                startEntity(snapPoint(worldPos));
                return;
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
                    // Check if there are any entities to be tangent to
                    if (m_entities.isEmpty()) {
                        QMessageBox::information(this, tr("Tangent Arc"),
                            tr("There are no entities to create a tangent arc from.\n"
                               "Please draw a line first."));
                        return;
                    }

                    int hitId = hitTest(worldPos);
                    if (hitId >= 0) {
                        // Check if the entity type is supported for tangent arcs
                        SketchEntity* entity = entityById(hitId);
                        if (entity) {
                            if (entity->type == SketchEntityType::Line ||
                                entity->type == SketchEntityType::Rectangle) {
                                m_tangentTargets.append(hitId);

                                // Project click point onto the entity to get the tangent point
                                QPointF clickPoint = snapPoint(worldPos);
                                QPointF tangentPoint = clickPoint;
                                bool altHeld = event->modifiers() & Qt::AltModifier;
                                QPointF closestEdgeStart, closestEdgeEnd;

                                if (entity->type == SketchEntityType::Line && entity->points.size() >= 2) {
                                    QPointF p1 = entity->points[0];
                                    QPointF p2 = entity->points[1];
                                    tangentPoint = geometry::closestPointOnLine(clickPoint, p1, p2);

                                    // Snap to endpoints or midpoint (unless Alt is held)
                                    if (!altHeld) {
                                        QPointF midpoint = (p1 + p2) / 2.0;
                                        double snapDist = 10.0 / m_zoom;

                                        if (QLineF(tangentPoint, p1).length() < snapDist) {
                                            tangentPoint = p1;
                                        } else if (QLineF(tangentPoint, p2).length() < snapDist) {
                                            tangentPoint = p2;
                                        } else if (QLineF(tangentPoint, midpoint).length() < snapDist) {
                                            tangentPoint = midpoint;
                                        }
                                    }
                                } else if (entity->type == SketchEntityType::Rectangle && entity->points.size() >= 2) {
                                    // Find closest edge and project onto it
                                    QPointF corners[4];
                                    if (entity->points.size() >= 4) {
                                        for (int i = 0; i < 4; ++i) corners[i] = entity->points[i];
                                    } else {
                                        corners[0] = entity->points[0];
                                        corners[1] = QPointF(entity->points[1].x(), entity->points[0].y());
                                        corners[2] = entity->points[1];
                                        corners[3] = QPointF(entity->points[0].x(), entity->points[1].y());
                                    }
                                    double minDist = std::numeric_limits<double>::max();
                                    for (int i = 0; i < 4; ++i) {
                                        QPointF edgeStart = corners[i];
                                        QPointF edgeEnd = corners[(i + 1) % 4];
                                        QPointF projected = geometry::closestPointOnLine(clickPoint, edgeStart, edgeEnd);
                                        double dist = QLineF(clickPoint, projected).length();
                                        if (dist < minDist) {
                                            minDist = dist;
                                            tangentPoint = projected;
                                            closestEdgeStart = edgeStart;
                                            closestEdgeEnd = edgeEnd;
                                        }
                                    }

                                    // Snap to corners or edge midpoint (unless Alt is held)
                                    if (!altHeld) {
                                        QPointF midpoint = (closestEdgeStart + closestEdgeEnd) / 2.0;
                                        double snapDist = 10.0 / m_zoom;

                                        if (QLineF(tangentPoint, closestEdgeStart).length() < snapDist) {
                                            tangentPoint = closestEdgeStart;
                                        } else if (QLineF(tangentPoint, closestEdgeEnd).length() < snapDist) {
                                            tangentPoint = closestEdgeEnd;
                                        } else if (QLineF(tangentPoint, midpoint).length() < snapDist) {
                                            tangentPoint = midpoint;
                                        }
                                    }
                                }

                                // Start the arc with the projected tangent point
                                startEntity(tangentPoint);
                                update();
                            } else {
                                // Entity type not supported (yet)
                                QMessageBox::information(this, tr("Tangent Arc"),
                                    tr("Tangent arcs can currently only be created from lines or rectangles.\n"
                                       "Please click on a line or rectangle edge."));
                            }
                        }
                    } else {
                        // User clicked but didn't hit any entity
                        QMessageBox::information(this, tr("Tangent Arc"),
                            tr("Please click on a line or rectangle edge to create a tangent arc from."));
                    }
                } else {
                    // Second click is the end point - update and finish the entity
                    updateEntity(snapPoint(worldPos));
                    finishEntity();
                }
            } else if (m_activeTool == SketchTool::Line && m_lineMode == LineMode::Tangent && !m_tangentTargets.isEmpty()) {
                // Tangent line second click: finish the entity
                updateEntity(snapPoint(worldPos));
                finishEntity();
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
            } else if (m_activeTool == SketchTool::Line && m_lineMode == LineMode::Tangent && m_tangentTargets.isEmpty()) {
                // Tangent line: first click selects a circle or arc to be tangent to
                int hitId = hitTest(worldPos);
                if (hitId >= 0) {
                    SketchEntity* entity = entityById(hitId);
                    if (entity && (entity->type == SketchEntityType::Circle ||
                                   entity->type == SketchEntityType::Arc)) {
                        m_tangentTargets.append(hitId);

                        // Project click point onto the circle/arc perimeter
                        QPointF center = entity->points[0];
                        double radius = entity->radius;
                        QPointF toClick = worldPos - center;
                        double dist = std::sqrt(toClick.x() * toClick.x() + toClick.y() * toClick.y());

                        QPointF tangentPoint;
                        if (dist > 0.001) {
                            tangentPoint = center + toClick * (radius / dist);
                        } else {
                            tangentPoint = center + QPointF(radius, 0);
                        }

                        // For arcs, clamp to the arc's angular range
                        if (entity->type == SketchEntityType::Arc) {
                            double angle = std::atan2(tangentPoint.y() - center.y(),
                                                      tangentPoint.x() - center.x()) * 180.0 / M_PI;
                            double startAngle = entity->startAngle;
                            double endAngle = startAngle + entity->sweepAngle;

                            // Check if on arc, if not clamp to nearest endpoint
                            auto normalizeAngle = [](double a) {
                                while (a < 0) a += 360;
                                while (a >= 360) a -= 360;
                                return a;
                            };

                            double normAngle = normalizeAngle(angle);
                            double normStart = normalizeAngle(startAngle);
                            double normEnd = normalizeAngle(endAngle);

                            bool onArc = false;
                            if (entity->sweepAngle > 0) {
                                onArc = (normStart <= normEnd) ?
                                    (normAngle >= normStart && normAngle <= normEnd) :
                                    (normAngle >= normStart || normAngle <= normEnd);
                            } else {
                                onArc = (normEnd <= normStart) ?
                                    (normAngle <= normStart && normAngle >= normEnd) :
                                    (normAngle <= normStart || normAngle >= normEnd);
                            }

                            if (!onArc) {
                                QPointF startPt = center + QPointF(
                                    radius * std::cos(startAngle * M_PI / 180.0),
                                    radius * std::sin(startAngle * M_PI / 180.0));
                                QPointF endPt = center + QPointF(
                                    radius * std::cos(endAngle * M_PI / 180.0),
                                    radius * std::sin(endAngle * M_PI / 180.0));

                                tangentPoint = (QLineF(worldPos, startPt).length() < QLineF(worldPos, endPt).length())
                                    ? startPt : endPt;
                            }
                        }

                        // Start the line entity
                        m_drawStartPos = event->pos();
                        m_wasDragged = false;
                        startEntity(tangentPoint);
                        update();
                    } else {
                        QMessageBox::information(this, tr("Tangent Line"),
                            tr("Please click on a circle or arc to create a tangent line from."));
                    }
                } else {
                    QMessageBox::information(this, tr("Tangent Line"),
                        tr("Please click on a circle or arc to create a tangent line from."));
                }
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
    // Skip for constrained line modes (Horizontal, Vertical, Tangent) - they have their own constraints
    if (ctrlHeld && m_isDrawing && !m_previewPoints.isEmpty()) {
        bool isConstrainedLineMode = (m_activeTool == SketchTool::Line &&
                                      (m_lineMode == LineMode::Horizontal ||
                                       m_lineMode == LineMode::Vertical ||
                                       m_lineMode == LineMode::Tangent));
        if (!isConstrainedLineMode &&
            (m_activeTool == SketchTool::Line ||
             m_activeTool == SketchTool::Rectangle ||
             (m_activeTool == SketchTool::Slot &&
              (m_slotMode == SlotMode::CenterToCenter || m_slotMode == SlotMode::Overall)))) {
            // Calculate the angle-snapped position
            QPointF startPoint = m_previewPoints[0];
            QPointF snappedPos = snapToAngle(startPoint, m_currentMouseWorld);

            // Get the direction of the snapped angle ray
            QPointF rayDir = snappedPos - startPoint;

            if (geometry::length(rayDir) > 0.001) {
                // Check if the entity snap point lies exactly on the angle ray
                if (geometry::pointOnRay(m_currentMouseWorld, startPoint, rayDir) &&
                    m_activeSnap.has_value() && !altHeld) {
                    // Snap point is on the angle ray - keep it
                } else {
                    // Snap point not on ray - use the angle-snapped position
                    m_currentMouseWorld = snappedPos;
                    m_activeSnap.reset();
                }
            } else {
                m_currentMouseWorld = snappedPos;
            }
        }
    }

    // Horizontal/Vertical line mode constrains movement to that axis
    if (m_isDrawing && !m_previewPoints.isEmpty() && m_activeTool == SketchTool::Line) {
        if (m_lineMode == LineMode::Horizontal) {
            // Horizontal line: Y must equal start point's Y
            double targetY = m_previewPoints[0].y();
            double tolerance = 0.001;  // World units - essentially exact
            if (std::abs(m_currentMouseWorld.y() - targetY) < tolerance && m_activeSnap.has_value() && !altHeld) {
                // Snap point is on the horizontal line - keep it
            } else {
                // Project to horizontal line
                m_currentMouseWorld.setY(targetY);
                m_activeSnap.reset();
            }
        } else if (m_lineMode == LineMode::Vertical) {
            // Vertical line: X must equal start point's X
            double targetX = m_previewPoints[0].x();
            double tolerance = 0.001;  // World units - essentially exact
            if (std::abs(m_currentMouseWorld.x() - targetX) < tolerance && m_activeSnap.has_value() && !altHeld) {
                // Snap point is on the vertical line - keep it
            } else {
                // Project to vertical line
                m_currentMouseWorld.setX(targetX);
                m_activeSnap.reset();
            }
        } else if (m_lineMode == LineMode::Tangent && !m_tangentTargets.isEmpty()) {
            // Tangent line: constrain to tangent direction (perpendicular to radius)
            SketchEntity* entity = entityById(m_tangentTargets[0]);
            if (entity && (entity->type == SketchEntityType::Circle ||
                          entity->type == SketchEntityType::Arc)) {
                QPointF center = entity->points[0];
                QPointF tangentPoint = m_previewPoints[0];

                // Tangent direction is perpendicular to radius
                QPointF tangentDir = geometry::perpendicular(tangentPoint - center);

                if (geometry::length(tangentDir) > 0.001) {
                    // Check if the snapped point lies exactly on the tangent ray
                    if (geometry::pointOnRay(m_currentMouseWorld, tangentPoint, tangentDir) &&
                        m_activeSnap.has_value()) {
                        // Snap point is on the tangent ray - keep it
                    } else {
                        // Snap point not on tangent ray - project mouse position onto tangent
                        QPointF rawMouse = screenToWorld(mapFromGlobal(QCursor::pos()));
                        m_currentMouseWorld = geometry::projectPointOntoRay(rawMouse, tangentPoint, tangentDir);
                        m_activeSnap.reset();
                    }
                }
            }
        }
    }

    // Tangent arc: constrain second click to lie on the arc path
    // Constraint always applies; Alt only disables entity snapping (handled above)
    if (m_isDrawing && !m_previewPoints.isEmpty() &&
        m_activeTool == SketchTool::Arc && m_arcMode == ArcMode::Tangent &&
        !m_tangentTargets.isEmpty()) {
        // Find the tangent entity
        SketchEntity* tangentEntity = entityById(m_tangentTargets[0]);
        if (tangentEntity) {
            QPointF tangentPoint = m_previewPoints[0];
            QPointF endPoint = m_currentMouseWorld;

            // Calculate the tangent arc to get its center and radius
            TangentArc ta = calculateTangentArc(*tangentEntity, tangentPoint, endPoint);

            if (ta.valid && ta.radius > 0.001) {
                // Check if the snapped point lies exactly on the arc (at radius distance from center)
                double distFromCenter = QLineF(m_currentMouseWorld, ta.center).length();
                double distFromArc = std::abs(distFromCenter - ta.radius);

                // Snap point must be exactly ON the arc path (within floating point tolerance)
                double tolerance = 0.001;  // World units - essentially exact
                if (distFromArc < tolerance && m_activeSnap.has_value() && !altHeld) {
                    // Snap point is on the arc path and snapping is enabled - keep it
                    // (m_currentMouseWorld is already the snap point)
                } else {
                    // Either: snap not on arc, no snap, or Alt held (snapping disabled)
                    // Project mouse position onto the arc path
                    QPointF rawMouse = screenToWorld(mapFromGlobal(QCursor::pos()));
                    TangentArc rawTa = calculateTangentArc(*tangentEntity, tangentPoint, rawMouse);
                    if (rawTa.valid && rawTa.radius > 0.001) {
                        // Project the raw mouse onto the arc (point on circle at radius from center)
                        QPointF toMouse = rawMouse - rawTa.center;
                        double dist = std::sqrt(toMouse.x() * toMouse.x() + toMouse.y() * toMouse.y());
                        if (dist > 0.001) {
                            m_currentMouseWorld = rawTa.center + toMouse * (rawTa.radius / dist);
                        }
                    }
                    m_activeSnap.reset();  // Clear snap indicator since we're not using it
                }
            }
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
            // Entity/origin snapping is always active (matches entity creation behavior)
            QPointF finalPos;
            if (ctrlPressed && m_snapAxis != SnapAxis::None) {
                if (m_snapToGrid || shiftPressed) {
                    // Snap to grid/entities AND constrain to axis
                    finalPos = axisLockedSnapPoint(worldPos);
                } else {
                    // Ctrl held with axis constraint - constrain without grid snap
                    QPointF raw = worldPos;
                    if (m_snapAxis == SnapAxis::X) {
                        finalPos = QPointF(raw.x(), m_dragHandleOriginal.y());
                    } else {
                        finalPos = QPointF(m_dragHandleOriginal.x(), raw.y());
                    }
                }
            } else {
                // Always snap to entities/origin; grid snap when enabled or Shift held
                finalPos = snapPoint(worldPos);
            }

            // Handle entity-specific handle dragging
            if (sel->type == SketchEntityType::Circle) {
                if (m_dragHandleIndex == 0) {
                    // Dragging center - move ALL points together
                    QPointF delta = finalPos - sel->points[0];
                    for (int i = 0; i < sel->points.size(); ++i)
                        sel->points[i] += delta;
                } else if (m_dragHandleIndex >= 1) {
                    // Dragging any perimeter point - update radius
                    sel->points[m_dragHandleIndex] = finalPos;
                    sel->radius = QLineF(sel->points[0], finalPos).length();
                    // Reposition all other perimeter points to new radius
                    QPointF center = sel->points[0];
                    for (int i = 1; i < sel->points.size(); ++i) {
                        if (i == m_dragHandleIndex) continue;
                        QPointF dir = sel->points[i] - center;
                        double len = std::sqrt(dir.x() * dir.x() + dir.y() * dir.y());
                        if (len > 1e-6) {
                            sel->points[i] = center + dir * (sel->radius / len);
                        }
                    }
                }
            } else if (sel->type == SketchEntityType::Arc && sel->points.size() >= 3) {
                // Arc: points[0]=center, points[1]=start endpoint, points[2]=end endpoint
                // Handle 0 (center): move entire arc
                // Handle 1 (start): slide along circle to adjust arc angle
                // Handle 2 (end): free drag to resize radius
                QPointF center = sel->points[0];
                if (m_dragHandleIndex == 0) {
                    // Dragging center - move all points together
                    QPointF delta = finalPos - center;
                    sel->points[0] = finalPos;
                    sel->points[1] += delta;
                    sel->points[2] += delta;
                } else if (m_dragHandleIndex == 1) {
                    // Dragging start endpoint - constrain to arc radius (adjust angle)
                    double radius = sel->radius;
                    QPointF dir = finalPos - center;
                    double len = std::sqrt(dir.x() * dir.x() + dir.y() * dir.y());
                    if (len > 1e-6) {
                        sel->points[1] = center + dir * (radius / len);

                        // Recompute angles from endpoint positions
                        double startAngle = std::atan2(sel->points[1].y() - center.y(),
                                                       sel->points[1].x() - center.x()) * 180.0 / M_PI;
                        double endAngle = std::atan2(sel->points[2].y() - center.y(),
                                                     sel->points[2].x() - center.x()) * 180.0 / M_PI;
                        double sweep = endAngle - startAngle;
                        if (sel->sweepAngle >= 0) {
                            while (sweep < 0) sweep += 360.0;
                        } else {
                            while (sweep > 0) sweep -= 360.0;
                        }
                        sel->startAngle = startAngle;
                        sel->sweepAngle = sweep;
                    }
                } else if (m_dragHandleIndex == 2) {
                    // Dragging end endpoint - free drag to resize radius
                    sel->points[2] = finalPos;
                    double newRadius = QLineF(center, finalPos).length();
                    if (newRadius > 1e-6) {
                        // Reposition start endpoint at same angle but new radius
                        double startRad = qDegreesToRadians(sel->startAngle);
                        sel->points[1] = center + QPointF(newRadius * qCos(startRad),
                                                           newRadius * qSin(startRad));
                        sel->radius = newRadius;

                        // Recompute end angle (start angle preserved)
                        double endAngle = std::atan2(finalPos.y() - center.y(),
                                                     finalPos.x() - center.x()) * 180.0 / M_PI;
                        double sweep = endAngle - sel->startAngle;
                        if (sel->sweepAngle >= 0) {
                            while (sweep < 0) sweep += 360.0;
                        } else {
                            while (sweep > 0) sweep -= 360.0;
                        }
                        sel->sweepAngle = sweep;
                    }
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
                } else if (m_dragHandleIndex == 1) {
                    // Handle 1 (start) - slide along arc to adjust angle
                    QPointF center = sel->points[0];
                    double arcRadius = QLineF(center, sel->points[2]).length();

                    QPointF dir = finalPos - center;
                    double len = std::sqrt(dir.x() * dir.x() + dir.y() * dir.y());
                    if (len > 1e-6 && arcRadius > 1e-6) {
                        double angle = std::atan2(dir.y(), dir.x());

                        // Ctrl snaps to 45-degree intervals
                        if (ctrlPressed) {
                            const double snapAngle = M_PI / 4.0;
                            angle = std::round(angle / snapAngle) * snapAngle;
                        }

                        sel->points[1] = center + QPointF(
                            arcRadius * std::cos(angle),
                            arcRadius * std::sin(angle));

                        // Clamp sweep: full circle minus 2x end cap radius
                        double minGap = (arcRadius > 0.001) ? (2.0 * sel->radius / arcRadius) : 0.1;
                        double maxSweep = 2.0 * M_PI - minGap;
                        double startAng = std::atan2(sel->points[1].y() - center.y(),
                                                     sel->points[1].x() - center.x());
                        double endAng = std::atan2(sel->points[2].y() - center.y(),
                                                   sel->points[2].x() - center.x());
                        double sweep = endAng - startAng;
                        if (sel->arcFlipped) {
                            if (sweep > 0) sweep -= 2.0 * M_PI;
                            else sweep += 2.0 * M_PI;
                        } else {
                            while (sweep > M_PI) sweep -= 2.0 * M_PI;
                            while (sweep < -M_PI) sweep += 2.0 * M_PI;
                        }
                        if (std::abs(sweep) > maxSweep) {
                            double clampedSweep = (sweep > 0) ? maxSweep : -maxSweep;
                            double newStartAng = endAng - clampedSweep;
                            sel->points[1] = center + QPointF(
                                arcRadius * std::cos(newStartAng),
                                arcRadius * std::sin(newStartAng));
                        }
                    } else {
                        sel->points[1] = finalPos;
                    }
                } else if (m_dragHandleIndex == 2) {
                    // Handle 2 (end) - free drag to resize arc radius
                    QPointF center = sel->points[0];
                    double newArcRadius = QLineF(center, finalPos).length();
                    double slotHalfWidth = sel->radius;

                    if (newArcRadius > 1e-6) {
                        // Place end point at clamped radius in the drag direction
                        QPointF endDir = finalPos - center;
                        double endLen = std::sqrt(endDir.x() * endDir.x() + endDir.y() * endDir.y());
                        if (endLen > 1e-6) {
                            sel->points[2] = center + endDir * (newArcRadius / endLen);
                        }

                        // Reposition start endpoint at same angle but new arc radius
                        QPointF startDir = sel->points[1] - center;
                        double startLen = std::sqrt(startDir.x() * startDir.x() + startDir.y() * startDir.y());
                        if (startLen > 1e-6) {
                            sel->points[1] = center + startDir * (newArcRadius / startLen);
                        }

                        // Clamp sweep after radius change (minGap may have increased)
                        double minGap = 2.0 * slotHalfWidth / newArcRadius;
                        double maxSweep = 2.0 * M_PI - minGap;
                        double startAng = std::atan2(sel->points[1].y() - center.y(),
                                                     sel->points[1].x() - center.x());
                        double endAng = std::atan2(sel->points[2].y() - center.y(),
                                                   sel->points[2].x() - center.x());
                        double sweep = endAng - startAng;
                        if (sel->arcFlipped) {
                            if (sweep > 0) sweep -= 2.0 * M_PI;
                            else sweep += 2.0 * M_PI;
                        } else {
                            while (sweep > M_PI) sweep -= 2.0 * M_PI;
                            while (sweep < -M_PI) sweep += 2.0 * M_PI;
                        }
                        if (std::abs(sweep) > maxSweep) {
                            double clampedSweep = (sweep > 0) ? maxSweep : -maxSweep;
                            double newStartAng = endAng - clampedSweep;
                            sel->points[1] = center + QPointF(
                                newArcRadius * std::cos(newStartAng),
                                newArcRadius * std::sin(newStartAng));
                        }
                    }
                }
            } else if (sel->type == SketchEntityType::Polygon && sel->points.size() >= 2) {
                // Polygon: points[0]=center, points[1]=radius handle
                if (m_dragHandleIndex == 0) {
                    QPointF delta = finalPos - sel->points[0];
                    sel->points[0] = finalPos;
                    sel->points[1] += delta;
                } else if (m_dragHandleIndex == 1) {
                    sel->points[1] = finalPos;
                    sel->radius = QLineF(sel->points[0], sel->points[1]).length();
                }
            } else if (sel->type == SketchEntityType::Ellipse && sel->points.size() >= 2) {
                // Ellipse: points[0]=center, points[1]=major axis point
                if (m_dragHandleIndex == 0) {
                    QPointF delta = finalPos - sel->points[0];
                    sel->points[0] = finalPos;
                    sel->points[1] += delta;
                } else if (m_dragHandleIndex == 1) {
                    double oldMajor = sel->majorRadius;
                    sel->points[1] = finalPos;
                    sel->majorRadius = QLineF(sel->points[0], finalPos).length();
                    // Scale minor radius proportionally to maintain aspect ratio
                    if (oldMajor > 1e-6) {
                        sel->minorRadius *= sel->majorRadius / oldMajor;
                    }
                }
            } else if (sel->type == SketchEntityType::Line && sel->points.size() == 2) {
                // Line handle drag — uses a temporary FixedPoint
                // (WHERE_DRAGGED) constraint on the non-dragged endpoint
                // so the solver knows which end is anchored.

                int fixedIdx = (m_dragHandleIndex == 0) ? 1 : 0;

                // Find the Distance constraint (if any) for label tracking
                SketchConstraint* distConstraint = nullptr;
                for (SketchConstraint& c : m_constraints) {
                    if (c.type == ConstraintType::Distance && c.isDriving && c.enabled) {
                        for (int eid : c.entityIds) {
                            if (eid == sel->id) { distConstraint = &c; break; }
                        }
                        if (distConstraint) break;
                    }
                }

                // Capture label's perpendicular offset from OLD orientation
                double labelPerpOffset = 0.0;
                if (distConstraint) {
                    QPointF oldAlong = sel->points[1] - sel->points[0];
                    double oldLen = std::sqrt(oldAlong.x() * oldAlong.x() + oldAlong.y() * oldAlong.y());
                    if (oldLen > 1e-6) {
                        QPointF oldPerp(-oldAlong.y() / oldLen, oldAlong.x() / oldLen);
                        QPointF oldMid = (sel->points[0] + sel->points[1]) / 2.0;
                        QPointF ld = distConstraint->labelPosition - oldMid;
                        labelPerpOffset = ld.x() * oldPerp.x() + ld.y() * oldPerp.y();
                    }
                }

                // Move dragged point to mouse position
                sel->points[m_dragHandleIndex] = finalPos;

                // Add temporary FixedPoint constraint on the non-dragged
                // endpoint, solve, then remove it.
                if (!m_constraints.isEmpty()) {
                    SketchConstraint pinConstraint;
                    pinConstraint.id = -999;  // temporary ID
                    pinConstraint.type = ConstraintType::FixedPoint;
                    pinConstraint.entityIds.append(sel->id);
                    pinConstraint.pointIndices.append(fixedIdx);
                    pinConstraint.isDriving = true;
                    pinConstraint.enabled = true;
                    pinConstraint.satisfied = true;
                    pinConstraint.labelVisible = false;

                    m_constraints.append(pinConstraint);
                    solveConstraints();
                    // Remove the temporary constraint
                    m_constraints.erase(
                        std::remove_if(m_constraints.begin(), m_constraints.end(),
                                       [](const SketchConstraint& c) { return c.id == -999; }),
                        m_constraints.end());
                }

                // Reposition label from FINAL line orientation
                if (distConstraint) {
                    // Re-find distConstraint — pointer may be stale after
                    // m_constraints append/erase
                    distConstraint = nullptr;
                    for (SketchConstraint& c : m_constraints) {
                        if (c.type == ConstraintType::Distance && c.isDriving && c.enabled) {
                            for (int eid : c.entityIds) {
                                if (eid == sel->id) { distConstraint = &c; break; }
                            }
                            if (distConstraint) break;
                        }
                    }
                    if (distConstraint) {
                        QPointF newAlong = sel->points[1] - sel->points[0];
                        double newLen = std::sqrt(newAlong.x() * newAlong.x() + newAlong.y() * newAlong.y());
                        if (newLen > 1e-6) {
                            QPointF newPerp(-newAlong.y() / newLen, newAlong.x() / newLen);
                            QPointF newMid = (sel->points[0] + sel->points[1]) / 2.0;
                            distConstraint->labelPosition = newMid + newPerp * labelPerpOffset;
                        }
                    }
                }
            } else {
                // For other entities, just move the point directly
                sel->points[m_dragHandleIndex] = finalPos;
            }

            // Non-line entities: run solver normally
            // (Lines use temporary FixedPoint pins above.)
            if (sel && sel->type != SketchEntityType::Line
                    && !m_constraints.isEmpty()) {
                solveConstraints();
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

        // When in pre-fill selectAll mode, mouse movement keeps live tracking.
        // Once the user starts typing (selectAll becomes false), don't interfere —
        // otherwise mouse tremor during keyboard input resets their typed value.
        // (selectAll=true is maintained as a no-op; selectAll=false is left alone.)

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
            setCursor(Qt::ArrowCursor);
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
        setCursor(Qt::ArrowCursor);
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
            // Re-solve constraints so dimensions are enforced after resize
            solveConstraints();
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

            // Multi-click tools: arc (3-point, center-start-end, start-end-radius), 3-point rectangle, arc slot, and spline
            if (m_activeTool == SketchTool::Arc &&
                (m_arcMode == ArcMode::ThreePoint || m_arcMode == ArcMode::CenterStartEnd || m_arcMode == ArcMode::StartEndRadius)) {
                // Only add point on release if user dragged (click already added point in mousePressEvent)
                if (m_wasDragged) {
                    QPointF worldPos = screenToWorld(event->pos());
                    QPointF snapped = snapPoint(worldPos);

                    if (m_arcMode == ArcMode::CenterStartEnd) {
                        // Stage 1: locked Radius constrains distance from center
                        if (m_pendingEntity.points.size() == 1) {
                            double lockedR = getLockedDim(0);
                            if (lockedR > 0) {
                                QPointF center = m_pendingEntity.points[0];
                                QPointF dir = snapped - center;
                                double len = std::sqrt(dir.x() * dir.x() + dir.y() * dir.y());
                                if (len > 1e-6)
                                    snapped = center + dir * (lockedR / len);
                            }
                        }
                        // Stage 2: constrain to arc radius + apply locked sweep angle
                        if (m_pendingEntity.points.size() == 2) {
                            QPointF center = m_pendingEntity.points[0];
                            QPointF start = m_pendingEntity.points[1];
                            double radius = QLineF(center, start).length();
                            double angle = std::atan2(snapped.y() - center.y(), snapped.x() - center.x());
                            double lockedSweep = getLockedDim(0);
                            if (lockedSweep != -1.0) {
                                double startAngle = std::atan2(start.y() - center.y(), start.x() - center.x());
                                double defaultSweep = angle - startAngle;
                                while (defaultSweep > M_PI) defaultSweep -= 2.0 * M_PI;
                                while (defaultSweep < -M_PI) defaultSweep += 2.0 * M_PI;
                                if (m_arcSlotFlipped) {
                                    defaultSweep = (defaultSweep > 0) ? defaultSweep - 2.0 * M_PI : defaultSweep + 2.0 * M_PI;
                                }
                                double sign = (defaultSweep >= 0) ? 1.0 : -1.0;
                                angle = startAngle + sign * qDegreesToRadians(std::abs(lockedSweep));
                            }
                            snapped = center + QPointF(radius * std::cos(angle), radius * std::sin(angle));
                        }
                    } else if (m_arcMode == ArcMode::StartEndRadius) {
                        int pStage = m_previewPoints.size();
                        if (pStage == 1) {
                            // Stage 1: locked Chord Length/Angle
                            QPointF start = m_previewPoints[0];
                            double lockedLen = getLockedDim(0);
                            double lockedAng = getLockedDim(1);
                            if (lockedLen > 0 || lockedAng != -1.0) {
                                QPointF dir = snapped - start;
                                double mouseLen = std::sqrt(dir.x() * dir.x() + dir.y() * dir.y());
                                double mouseAng = std::atan2(dir.y(), dir.x());
                                double useLen = (lockedLen > 0) ? lockedLen : mouseLen;
                                double useAng = (lockedAng != -1.0) ? qDegreesToRadians(lockedAng) : mouseAng;
                                if (useLen > 0.001)
                                    snapped = start + QPointF(useLen * std::cos(useAng), useLen * std::sin(useAng));
                            }
                        } else if (pStage >= 2) {
                            // Stage 2: locked Sweep constrains perp bisector position
                            double lockedSweep = getLockedDim(0);
                            if (lockedSweep != -1.0) {
                                QPointF start = m_previewPoints[0];
                                QPointF end = m_previewPoints[1];
                                QPointF midChord = (start + end) / 2.0;
                                double chordLength = QLineF(start, end).length();
                                if (chordLength > 0.001) {
                                    QPointF chordDir = (end - start) / chordLength;
                                    QPointF perpDir(-chordDir.y(), chordDir.x());
                                    double halfSweepRad = qDegreesToRadians(std::abs(lockedSweep)) / 2.0;
                                    double tanHalf = std::tan(halfSweepRad);
                                    double projDist = (tanHalf > 1e-6) ? (chordLength / 2.0) / tanHalf : 1e6;
                                    QPointF toMouse = snapped - midChord;
                                    double mouseProjDist = toMouse.x() * perpDir.x() + toMouse.y() * perpDir.y();
                                    if (m_arcSlotFlipped) mouseProjDist = -mouseProjDist;
                                    double sign = (mouseProjDist >= 0) ? 1.0 : -1.0;
                                    if (m_arcSlotFlipped) sign = -sign;
                                    snapped = midChord + perpDir * sign * projDist;
                                }
                            }
                        }
                    }

                    m_pendingEntity.points.append(snapped);
                    m_previewPoints.append(snapped);
                    if (m_pendingEntity.points.size() >= 3) {
                        finishEntity();
                    } else {
                        initDimFields();  // Stage transition
                        update();
                    }
                } else {
                    // Just a click - point was already added on press
                    if (m_pendingEntity.points.size() >= 3) {
                        finishEntity();
                    }
                }
            } else if (m_activeTool == SketchTool::Slot &&
                       (m_slotMode == SlotMode::ArcRadius || m_slotMode == SlotMode::ArcEnds)) {
                // Arc slot: supports both click-click-click and click-drag modes
                if (m_wasDragged) {
                    QPointF worldPos = screenToWorld(event->pos());
                    QPointF snapped = snapPoint(worldPos);

                    // ArcRadius stage 1: locked Radius constrains start distance
                    if (m_slotMode == SlotMode::ArcRadius && m_pendingEntity.points.size() == 1) {
                        double lockedR = getLockedDim(0);
                        if (lockedR > 0) {
                            QPointF arcCenter = m_pendingEntity.points[0];
                            QPointF dir = snapped - arcCenter;
                            double len = std::sqrt(dir.x() * dir.x() + dir.y() * dir.y());
                            if (len > 1e-6)
                                snapped = arcCenter + dir * (lockedR / len);
                        }
                    }

                    // ArcRadius stage 2: constrain to arc + apply locked sweep angle
                    if (m_slotMode == SlotMode::ArcRadius && m_pendingEntity.points.size() == 2) {
                        QPointF arcCenterWorld = m_pendingEntity.points[0];
                        QPointF startWorld = m_pendingEntity.points[1];
                        double arcRadius = QLineF(arcCenterWorld, startWorld).length();
                        double mouseAngle = std::atan2(snapped.y() - arcCenterWorld.y(),
                                                       snapped.x() - arcCenterWorld.x());
                        double lockedSweep = getLockedDim(0);
                        if (lockedSweep != -1.0) {
                            double startAngle = std::atan2(startWorld.y() - arcCenterWorld.y(),
                                                           startWorld.x() - arcCenterWorld.x());
                            double angleDiff = mouseAngle - startAngle;
                            while (angleDiff > M_PI) angleDiff -= 2.0 * M_PI;
                            while (angleDiff < -M_PI) angleDiff += 2.0 * M_PI;
                            if (m_arcSlotFlipped) {
                                angleDiff = (angleDiff > 0) ? angleDiff - 2.0 * M_PI : angleDiff + 2.0 * M_PI;
                            }
                            double sign = (angleDiff >= 0) ? 1.0 : -1.0;
                            mouseAngle = startAngle + sign * qDegreesToRadians(std::abs(lockedSweep));
                        }
                        snapped = arcCenterWorld + QPointF(arcRadius * std::cos(mouseAngle),
                                                           arcRadius * std::sin(mouseAngle));
                    }

                    // ArcEnds stage 2: locked sweep constrains arc center on perp bisector
                    if (m_slotMode == SlotMode::ArcEnds && m_pendingEntity.points.size() == 2) {
                        double lockedSweep = getLockedDim(0);
                        if (lockedSweep != -1.0) {
                            QPointF start = m_pendingEntity.points[0];
                            QPointF end = m_pendingEntity.points[1];
                            QPointF midpoint = (start + end) / 2.0;
                            double chordLen = QLineF(start, end).length();
                            if (chordLen > 0.001) {
                                QPointF startToEnd = end - start;
                                QPointF perpDir(-startToEnd.y() / chordLen, startToEnd.x() / chordLen);
                                double halfSweepRad = qDegreesToRadians(std::abs(lockedSweep)) / 2.0;
                                double tanHalf = std::tan(halfSweepRad);
                                double projDist = (tanHalf > 1e-6) ? (chordLen / 2.0) / tanHalf : 1e6;
                                QPointF mouseToMid = snapped - midpoint;
                                double mouseProjDist = mouseToMid.x() * perpDir.x() + mouseToMid.y() * perpDir.y();
                                double sign = (mouseProjDist >= 0) ? 1.0 : -1.0;
                                snapped = midpoint + perpDir * sign * projDist;
                            }
                        }
                    }

                    m_pendingEntity.points.append(snapped);
                    m_previewPoints.append(snapped);
                    if (m_pendingEntity.points.size() >= 3) {
                        finishEntity();
                    } else {
                        initDimFields();  // Stage transition
                        update();
                    }
                } else {
                    // User clicked without dragging - point already added on press
                    update();
                }
            } else if (m_activeTool == SketchTool::Rectangle &&
                       m_rectMode == RectMode::ThreePoint) {
                // 3-point rectangle: supports both click-click-click and click-drag modes
                if (m_wasDragged) {
                    QPointF worldPos = screenToWorld(event->pos());
                    QPointF snapped = snapPoint(worldPos);

                    // Apply locked dimension constraints
                    int pStage = m_previewPoints.size();
                    if (pStage == 1) {
                        QPointF p1 = m_previewPoints[0];
                        double lockedLen = getLockedDim(0);
                        double lockedAng = getLockedDim(1);
                        if (lockedLen > 0 || lockedAng != -1.0) {
                            QPointF dir = snapped - p1;
                            double mouseLen = std::sqrt(dir.x() * dir.x() + dir.y() * dir.y());
                            double mouseAng = std::atan2(dir.y(), dir.x());
                            double useLen = (lockedLen > 0) ? lockedLen : mouseLen;
                            double useAng = (lockedAng != -1.0) ? qDegreesToRadians(lockedAng) : mouseAng;
                            if (useLen > 0.001)
                                snapped = p1 + QPointF(useLen * std::cos(useAng), useLen * std::sin(useAng));
                        }
                    } else if (pStage >= 2) {
                        double lockedW = getLockedDim(0);
                        if (lockedW > 0) {
                            QPointF p1 = m_previewPoints[0];
                            QPointF p2 = m_previewPoints[1];
                            QPointF edge = p2 - p1;
                            double edgeLen = std::sqrt(edge.x() * edge.x() + edge.y() * edge.y());
                            if (edgeLen > 0.001) {
                                QPointF edgeDir = edge / edgeLen;
                                QPointF perpDir(-edgeDir.y(), edgeDir.x());
                                QPointF toMouse = snapped - p1;
                                double perpDot = toMouse.x() * perpDir.x() + toMouse.y() * perpDir.y();
                                double sign = (perpDot >= 0) ? 1.0 : -1.0;
                                double edgeDot = toMouse.x() * edgeDir.x() + toMouse.y() * edgeDir.y();
                                snapped = p1 + edgeDir * edgeDot + perpDir * sign * lockedW;
                            }
                        }
                    }

                    m_pendingEntity.points.append(snapped);
                    m_previewPoints.append(snapped);
                    if (m_pendingEntity.points.size() >= 3) {
                        finishEntity();
                    } else {
                        initDimFields();  // Stage transition
                        update();
                    }
                } else {
                    // User clicked without dragging - point already added on press
                    // Just update the display
                    update();
                }
            } else if (m_activeTool == SketchTool::Rectangle &&
                       m_rectMode == RectMode::Parallelogram) {
                // Parallelogram mode: supports both click-click-click and click-drag modes
                if (m_wasDragged) {
                    // User dragged - add the release point
                    QPointF worldPos = screenToWorld(event->pos());
                    QPointF snapped = snapPoint(worldPos);

                    // Apply locked dimension constraints
                    int pStage = m_previewPoints.size();
                    if (pStage == 1) {
                        QPointF p1 = m_previewPoints[0];
                        double lockedLen = getLockedDim(0);
                        double lockedAng = getLockedDim(1);
                        if (lockedLen > 0 || lockedAng != -1.0) {
                            QPointF dir = snapped - p1;
                            double mouseLen = std::sqrt(dir.x() * dir.x() + dir.y() * dir.y());
                            double mouseAng = std::atan2(dir.y(), dir.x());
                            double useLen = (lockedLen > 0) ? lockedLen : mouseLen;
                            double useAng = (lockedAng != -1.0) ? qDegreesToRadians(lockedAng) : mouseAng;
                            if (useLen > 0.001)
                                snapped = p1 + QPointF(useLen * std::cos(useAng), useLen * std::sin(useAng));
                        }
                    } else if (pStage >= 2) {
                        QPointF p1 = m_previewPoints[0];
                        QPointF p2 = m_previewPoints[1];
                        double lockedLen = getLockedDim(0);
                        double lockedAng = getLockedDim(1);
                        if (lockedLen > 0 || lockedAng != -1.0) {
                            QPointF dir = snapped - p2;
                            double mouseLen = std::sqrt(dir.x() * dir.x() + dir.y() * dir.y());
                            double mouseAng = std::atan2(dir.y(), dir.x());
                            double useLen = (lockedLen > 0) ? lockedLen : mouseLen;
                            double useAng;
                            if (lockedAng != -1.0) {
                                double edge1Dir = std::atan2(p1.y() - p2.y(), p1.x() - p2.x());
                                double dir1 = edge1Dir + qDegreesToRadians(lockedAng);
                                double dir2 = edge1Dir - qDegreesToRadians(lockedAng);
                                double diff1 = std::abs(std::remainder(mouseAng - dir1, 2.0 * M_PI));
                                double diff2 = std::abs(std::remainder(mouseAng - dir2, 2.0 * M_PI));
                                useAng = (diff1 <= diff2) ? dir1 : dir2;
                            } else {
                                useAng = mouseAng;
                            }
                            if (useLen > 0.001)
                                snapped = p2 + QPointF(useLen * std::cos(useAng), useLen * std::sin(useAng));
                        }
                    }

                    m_pendingEntity.points.append(snapped);
                    m_previewPoints.append(snapped);
                    if (m_pendingEntity.points.size() >= 3) {
                        finishEntity();
                    } else {
                        initDimFields();  // Stage transition
                        update();
                    }
                } else {
                    // User clicked without dragging - point already added on press
                    // Just update the display
                    update();
                }
            } else if (m_activeTool == SketchTool::Circle &&
                       m_circleMode == CircleMode::ThreePoint) {
                // Three-point circle: click-click-click mode
                // Points are added on press in mousePressEvent
                // On release, just check if we have enough points
                if (m_pendingEntity.points.size() >= 3) {
                    finishEntity();
                } else {
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
            } else if (m_activeTool == SketchTool::Polygon
                       && m_polygonMode == PolygonMode::Freeform) {
                // Freeform polygon: click to add vertex, close by clicking start
                QPointF worldPos = screenToWorld(event->pos());
                QPointF snapped = snapPoint(worldPos);

                // Check if close to first point to close the polygon
                if (m_previewPoints.size() >= 3) {
                    double distToStart = QLineF(snapped, m_previewPoints[0]).length();
                    double snapDist = m_entitySnapTolerance / m_zoom;
                    if (distToStart < snapDist) {
                        // Close the polygon — don't add the click as a new point
                        finishEntity();
                        return;
                    }
                }

                // Add new vertex
                m_pendingEntity.points.append(snapped);
                m_previewPoints.append(snapped);
                update();
                // Don't finish - user continues clicking or right-clicks/Enter
            } else if (m_activeTool == SketchTool::Arc && m_arcMode == ArcMode::Tangent) {
                // Tangent arc: first click selects target, second click sets endpoint
                // Don't finish on drag-release after first click - wait for second click
                // The second click is handled in mousePressEvent
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
            // Adjust number of sides (3 to 64) — only for regular polygons
            if (m_polygonMode != PolygonMode::Freeform) {
                m_pendingEntity.sides = qBound(3, m_pendingEntity.sides + delta, 64);
                update();
                event->accept();
                return;
            }
            break;

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

    if (sel->type == SketchEntityType::Circle) {
        if (m_dragHandleIndex == 0) {
            QPointF delta = finalPos - sel->points[0];
            for (int i = 0; i < sel->points.size(); ++i)
                sel->points[i] += delta;
        } else if (m_dragHandleIndex >= 1) {
            sel->points[m_dragHandleIndex] = finalPos;
            sel->radius = QLineF(sel->points[0], finalPos).length();
            QPointF center = sel->points[0];
            for (int i = 1; i < sel->points.size(); ++i) {
                if (i == m_dragHandleIndex) continue;
                QPointF dir = sel->points[i] - center;
                double len = std::sqrt(dir.x() * dir.x() + dir.y() * dir.y());
                if (len > 1e-6) {
                    sel->points[i] = center + dir * (sel->radius / len);
                }
            }
        }
    } else if (sel->type == SketchEntityType::Arc && sel->points.size() >= 3) {
        // Arc: points[0]=center, points[1]=start (angle), points[2]=end (radius)
        QPointF center = sel->points[0];
        if (m_dragHandleIndex == 0) {
            QPointF delta = finalPos - center;
            sel->points[0] = finalPos;
            sel->points[1] += delta;
            sel->points[2] += delta;
        } else if (m_dragHandleIndex == 1) {
            // Start endpoint - constrain to arc radius (adjust angle)
            double radius = sel->radius;
            QPointF dir = finalPos - center;
            double len = std::sqrt(dir.x() * dir.x() + dir.y() * dir.y());
            if (len > 1e-6) {
                sel->points[1] = center + dir * (radius / len);
                double startAngle = std::atan2(sel->points[1].y() - center.y(),
                                               sel->points[1].x() - center.x()) * 180.0 / M_PI;
                double endAngle = std::atan2(sel->points[2].y() - center.y(),
                                             sel->points[2].x() - center.x()) * 180.0 / M_PI;
                double sweep = endAngle - startAngle;
                if (sel->sweepAngle >= 0) {
                    while (sweep < 0) sweep += 360.0;
                } else {
                    while (sweep > 0) sweep -= 360.0;
                }
                sel->startAngle = startAngle;
                sel->sweepAngle = sweep;
            }
        } else if (m_dragHandleIndex == 2) {
            // End endpoint - free drag to resize radius
            sel->points[2] = finalPos;
            double newRadius = QLineF(center, finalPos).length();
            if (newRadius > 1e-6) {
                double startRad = qDegreesToRadians(sel->startAngle);
                sel->points[1] = center + QPointF(newRadius * qCos(startRad),
                                                   newRadius * qSin(startRad));
                sel->radius = newRadius;
                double endAngle = std::atan2(finalPos.y() - center.y(),
                                             finalPos.x() - center.x()) * 180.0 / M_PI;
                double sweep = endAngle - sel->startAngle;
                if (sel->sweepAngle >= 0) {
                    while (sweep < 0) sweep += 360.0;
                } else {
                    while (sweep > 0) sweep -= 360.0;
                }
                sel->sweepAngle = sweep;
            }
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
        } else if (m_dragHandleIndex == 1) {
            // Handle 1 (start) - slide along arc to adjust angle
            QPointF center = sel->points[0];
            double arcRadius = QLineF(center, sel->points[2]).length();

            QPointF dir = finalPos - center;
            double len = std::sqrt(dir.x() * dir.x() + dir.y() * dir.y());
            if (len > 1e-6 && arcRadius > 1e-6) {
                double angle = std::atan2(dir.y(), dir.x());
                if (m_ctrlWasPressed) {
                    const double snapAngle = M_PI / 4.0;
                    angle = std::round(angle / snapAngle) * snapAngle;
                }
                sel->points[1] = center + QPointF(
                    arcRadius * std::cos(angle),
                    arcRadius * std::sin(angle));

                // Clamp sweep: full circle minus 2x end cap radius
                double minGap = 2.0 * sel->radius / arcRadius;
                double maxSweep = 2.0 * M_PI - minGap;
                double startAng = std::atan2(sel->points[1].y() - center.y(),
                                             sel->points[1].x() - center.x());
                double endAng = std::atan2(sel->points[2].y() - center.y(),
                                           sel->points[2].x() - center.x());
                double sweep = endAng - startAng;
                if (sel->arcFlipped) {
                    if (sweep > 0) sweep -= 2.0 * M_PI;
                    else sweep += 2.0 * M_PI;
                } else {
                    while (sweep > M_PI) sweep -= 2.0 * M_PI;
                    while (sweep < -M_PI) sweep += 2.0 * M_PI;
                }
                if (std::abs(sweep) > maxSweep) {
                    double clampedSweep = (sweep > 0) ? maxSweep : -maxSweep;
                    double newStartAng = endAng - clampedSweep;
                    sel->points[1] = center + QPointF(
                        arcRadius * std::cos(newStartAng),
                        arcRadius * std::sin(newStartAng));
                }
            } else {
                sel->points[1] = finalPos;
            }
        } else if (m_dragHandleIndex == 2) {
            // Handle 2 (end) - free drag to resize arc radius
            // Clamp: arc radius between slot half-width and 2x slot half-width
            QPointF center = sel->points[0];
            double newArcRadius = QLineF(center, finalPos).length();
            double slotHalfWidth = sel->radius;

            if (newArcRadius > 1e-6) {
                QPointF endDir = finalPos - center;
                double endLen = std::sqrt(endDir.x() * endDir.x() + endDir.y() * endDir.y());
                if (endLen > 1e-6) {
                    sel->points[2] = center + endDir * (newArcRadius / endLen);
                }
                QPointF startDir = sel->points[1] - center;
                double startLen = std::sqrt(startDir.x() * startDir.x() + startDir.y() * startDir.y());
                if (startLen > 1e-6) {
                    sel->points[1] = center + startDir * (newArcRadius / startLen);
                }

                // Clamp sweep after radius change
                double minGap = 2.0 * slotHalfWidth / newArcRadius;
                double maxSweep = 2.0 * M_PI - minGap;
                double startAng = std::atan2(sel->points[1].y() - center.y(),
                                             sel->points[1].x() - center.x());
                double endAng = std::atan2(sel->points[2].y() - center.y(),
                                           sel->points[2].x() - center.x());
                double sweep = endAng - startAng;
                if (sel->arcFlipped) {
                    if (sweep > 0) sweep -= 2.0 * M_PI;
                    else sweep += 2.0 * M_PI;
                } else {
                    while (sweep > M_PI) sweep -= 2.0 * M_PI;
                    while (sweep < -M_PI) sweep += 2.0 * M_PI;
                }
                if (std::abs(sweep) > maxSweep) {
                    double clampedSweep = (sweep > 0) ? maxSweep : -maxSweep;
                    double newStartAng = endAng - clampedSweep;
                    sel->points[1] = center + QPointF(
                        newArcRadius * std::cos(newStartAng),
                        newArcRadius * std::sin(newStartAng));
                }
            }
        }
    } else if (sel->type == SketchEntityType::Polygon && sel->points.size() >= 2) {
        if (m_dragHandleIndex == 0) {
            QPointF delta = finalPos - sel->points[0];
            sel->points[0] = finalPos;
            sel->points[1] += delta;
        } else if (m_dragHandleIndex == 1) {
            sel->points[1] = finalPos;
            sel->radius = QLineF(sel->points[0], sel->points[1]).length();
        }
    } else if (sel->type == SketchEntityType::Ellipse && sel->points.size() >= 2) {
        if (m_dragHandleIndex == 0) {
            QPointF delta = finalPos - sel->points[0];
            sel->points[0] = finalPos;
            sel->points[1] += delta;
        } else if (m_dragHandleIndex == 1) {
            double oldMajor = sel->majorRadius;
            sel->points[1] = finalPos;
            sel->majorRadius = QLineF(sel->points[0], finalPos).length();
            if (oldMajor > 1e-6) {
                sel->minorRadius *= sel->majorRadius / oldMajor;
            }
        }
    } else if (sel->type == SketchEntityType::Parallelogram && sel->points.size() >= 4) {
        sel->points[m_dragHandleIndex] = finalPos;
        sel->points[3] = sel->points[0] + (sel->points[2] - sel->points[1]);
    } else {
        sel->points[m_dragHandleIndex] = finalPos;
    }

    // If the entity is part of a group, run the constraint solver so that
    // coincident / perpendicular / distance constraints propagate the drag
    // to sibling entities in real time (e.g. dragging one corner of a
    // decomposed rectangle moves the connected sides).
    if (sel->groupId >= 0) {
        solveConstraints();
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

        // Check if double-clicking on an entity
        if (m_activeTool == SketchTool::Select) {
            int entityId = hitTest(worldPos);
            if (entityId >= 0) {
                const SketchEntity* ent = entityById(entityId);
                // If the entity belongs to a group, enter that group
                // (KiCad-style: double-click group → enter group mode)
                if (ent && ent->groupId >= 0 && m_enteredGroupId < 0) {
                    enterGroup(ent->groupId);
                    // Select the specific member that was double-clicked
                    selectEntity(entityId);
                    return;
                }
                // Otherwise, select connected chain (ungrouped entities)
                selectConnectedChain(entityId);
                return;
            }
        }
    }

    QWidget::mouseDoubleClickEvent(event);
}


bool SketchCanvas::event(QEvent* event)
{
    // Intercept Tab/Backtab before Qt's focus-navigation machinery
    // consumes them.  During entity creation with dimension fields,
    // Tab cycles between dim fields instead of moving focus to another
    // widget.
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent* ke = static_cast<QKeyEvent*>(event);
        if (ke->key() == Qt::Key_Tab || ke->key() == Qt::Key_Backtab) {
            if (m_isDrawing && !m_dimFields.isEmpty()) {
                keyPressEvent(ke);   // route to our handler
                return true;         // consumed
            }
        }
    }
    return QWidget::event(event);
}

void SketchCanvas::keyPressEvent(QKeyEvent* event)
{
    // ---- Inline dimension input routing (during entity creation) ----
    if (m_isDrawing && m_dimActiveIndex >= 0 && m_dimActiveIndex < m_dimStates.size()) {
        auto& state = m_dimStates[m_dimActiveIndex];
        int key = event->key();

        // Helper: replace entire buffer if selectAll, otherwise insert at cursor
        auto replaceOrInsert = [&](QChar ch) {
            if (state.selectAll) {
                state.inputBuffer = QString(ch);
                state.cursorPos = 1;
                state.selectAll = false;
            } else {
                state.inputBuffer.insert(state.cursorPos, ch);
                state.cursorPos++;
            }
        };

        // Accept any character valid in an expression:
        // digits, letters (functions/params), operators, parens, period, comma, space
        // Skip if the active field is locked (all fields locked — no typing allowed)
        if (!state.locked) {
            QString text = event->text();
            if (!text.isEmpty()) {
                QChar ch = text[0];
                if (ch.isDigit() || ch.isLetter() || ch == QLatin1Char('_') ||
                    ch == QLatin1Char('.') || ch == QLatin1Char('-') || ch == QLatin1Char('+') ||
                    ch == QLatin1Char('*') || ch == QLatin1Char('/') || ch == QLatin1Char('^') ||
                    ch == QLatin1Char('(') || ch == QLatin1Char(')') || ch == QLatin1Char(',') ||
                    ch == QLatin1Char(' ') || ch == QLatin1Char('%') ||
                    ch == QChar(0x00B0) ||    // ° degree sign
                    ch == QLatin1Char('\'') || // ' arcminute
                    ch == QLatin1Char('"') ||  // " arcsecond
                    ch == QChar(0x2032) ||     // ′ prime (Unicode)
                    ch == QChar(0x2033)) {     // ″ double prime (Unicode)
                    replaceOrInsert(ch);
                    update();
                    return;
                }
            }
        }
        // Backspace
        if (key == Qt::Key_Backspace) {
            if (state.selectAll) {
                state.inputBuffer.clear();
                state.cursorPos = 0;
                state.selectAll = false;
                update();
                return;
            } else if (state.cursorPos > 0) {
                state.inputBuffer.remove(state.cursorPos - 1, 1);
                state.cursorPos--;
                update();
                return;
            }
            // If cursorPos == 0 and not selectAll, fall through to normal Backspace handling
        }
        // Delete key
        if (key == Qt::Key_Delete) {
            if (state.selectAll) {
                state.inputBuffer.clear();
                state.cursorPos = 0;
                state.selectAll = false;
                update();
                return;
            } else if (state.cursorPos < state.inputBuffer.length()) {
                state.inputBuffer.remove(state.cursorPos, 1);
                update();
                return;
            }
        }
        // Arrow keys: deselect and move cursor
        // When deselecting, refresh buffer with current live value first
        if (key == Qt::Key_Left) {
            if (state.selectAll) {
                prefillDimField(m_dimActiveIndex);  // Refresh buffer to current value
                state.cursorPos = 0;
                state.selectAll = false;
            } else if (state.cursorPos > 0) {
                state.cursorPos--;
            }
            update();
            return;
        }
        if (key == Qt::Key_Right) {
            if (state.selectAll) {
                prefillDimField(m_dimActiveIndex);  // Refresh buffer to current value
                state.selectAll = false;
                // cursorPos already at end from prefill
            } else if (state.cursorPos < state.inputBuffer.length()) {
                state.cursorPos++;
            }
            update();
            return;
        }
        // Home/End
        if (key == Qt::Key_Home) {
            if (state.selectAll) {
                prefillDimField(m_dimActiveIndex);
            }
            state.selectAll = false;
            state.cursorPos = 0;
            update();
            return;
        }
        if (key == Qt::Key_End) {
            if (state.selectAll) {
                prefillDimField(m_dimActiveIndex);
            }
            state.selectAll = false;
            state.cursorPos = state.inputBuffer.length();
            update();
            return;
        }
        // Enter/Return: lock the value
        if (key == Qt::Key_Return || key == Qt::Key_Enter) {
            if (!state.inputBuffer.isEmpty()) {
                double val;
                if (state.selectAll) {
                    // selectAll = live tracking — lock the current live value directly
                    val = m_dimFields[m_dimActiveIndex].currentValue;  // Already in mm/degrees
                } else {
                    // Evaluate expression (supports formulas, parameters, functions, unit suffixes)
                    double exprResult;
                    QString exprStr = state.inputBuffer.trimmed();
                    if (m_dimFields[m_dimActiveIndex].isAngle) {
                        // Angles: try DMS format first, then expression, then plain number
                        double dmsResult;
                        if (hobbycad::parseDMS(exprStr, dmsResult)) {
                            val = dmsResult;
                        } else if (m_paramEngine && m_paramEngine->evaluateExpression(exprStr, exprResult)) {
                            val = exprResult;
                        } else {
                            val = exprStr.toDouble();  // Fallback
                        }
                    } else {
                        // Lengths: unit-aware evaluation, result in display units
                        // Bare numbers stay as-is, suffixed numbers converted to display units
                        if (m_paramEngine && m_paramEngine->evaluateExpression(exprStr, exprResult, m_displayUnit)) {
                            val = hobbycad::unitToMm(exprResult, m_displayUnit);  // Convert display units → mm
                        } else {
                            val = parseValueWithUnit(exprStr, m_displayUnit);  // Fallback
                        }
                    }
                }
                bool valid = m_dimFields[m_dimActiveIndex].isAngle ? (val != 0.0) : (val > 0.0);
                if (valid) {
                    state.locked = true;
                    state.lockedValue = val;
                    state.inputBuffer.clear();
                    state.cursorPos = 0;
                    state.selectAll = false;
                    // Advance to next unlocked field and pre-fill
                    for (int i = 1; i < m_dimFields.size(); ++i) {
                        int next = (m_dimActiveIndex + i) % m_dimFields.size();
                        if (!m_dimStates[next].locked) {
                            m_dimActiveIndex = next;
                            prefillDimField(next);
                            break;
                        }
                    }
                    // Capture rotation reference when both rect dims become locked
                    if (m_activeTool == SketchTool::Rectangle &&
                        m_rectMode == RectMode::Corner &&
                        !m_rectBothLocked) {
                        bool allLocked = true;
                        for (int i = 0; i < m_dimStates.size(); ++i) {
                            if (!m_dimStates[i].locked) { allLocked = false; break; }
                        }
                        if (allLocked && m_dimFields.size() >= 2) {
                            QPointF origin = m_previewPoints[0];
                            QPointF delta = m_currentMouseWorld - origin;
                            m_rectLockRefAngle = std::atan2(delta.y(), delta.x());
                            m_rectLockWidthAngle = (delta.x() >= 0) ? 0.0 : M_PI;
                            m_rectLockHeightAngle = (delta.y() >= 0) ? M_PI / 2.0 : -M_PI / 2.0;
                            m_rectBothLocked = true;
                        }
                    }

                    // Force preview update with locked constraint
                    updateEntity(m_currentMouseWorld);
                }
            }
            update();
            return;
        }
        // Tab / Shift+Tab: cycle to next/previous unlocked field, pre-fill with current value
        if (key == Qt::Key_Tab || key == Qt::Key_Backtab) {
            state.inputBuffer.clear();
            state.cursorPos = 0;
            state.selectAll = false;
            // Advance (Tab) or retreat (Shift+Tab) to next unlocked field
            if (m_dimFields.size() > 1) {
                int n = m_dimFields.size();
                int step = (key == Qt::Key_Backtab) ? (n - 1) : 1;  // n-1 ≡ -1 mod n
                for (int i = 1; i <= n; ++i) {
                    int next = (m_dimActiveIndex + step * i) % n;
                    if (!m_dimStates[next].locked) {
                        m_dimActiveIndex = next;
                        break;
                    }
                }
            }
            prefillDimField(m_dimActiveIndex);
            update();
            return;
        }
        // Escape: clear buffer or unlock last locked field
        if (key == Qt::Key_Escape) {
            if (!state.inputBuffer.isEmpty()) {
                state.inputBuffer.clear();
                state.cursorPos = 0;
                state.selectAll = false;
                update();
                return;
            }
            // Unlock in reverse priority: angles first, then lengths
            int unlockIdx = -1;
            // First pass: find last locked angle field
            for (int i = m_dimFields.size() - 1; i >= 0; --i) {
                if (i < m_dimStates.size() && m_dimStates[i].locked && m_dimFields[i].isAngle) {
                    unlockIdx = i;
                    break;
                }
            }
            // Second pass: if no locked angles, find last locked length field
            if (unlockIdx < 0) {
                for (int i = m_dimFields.size() - 1; i >= 0; --i) {
                    if (i < m_dimStates.size() && m_dimStates[i].locked && !m_dimFields[i].isAngle) {
                        unlockIdx = i;
                        break;
                    }
                }
            }
            if (unlockIdx >= 0) {
                m_dimStates[unlockIdx].locked = false;
                m_dimStates[unlockIdx].lockedValue = 0.0;
                m_dimActiveIndex = unlockIdx;
                // Remove from accumulated locked-for-constraints list
                QString label = m_dimFields[unlockIdx].label;
                for (int i = m_dimLockedForConstraints.size() - 1; i >= 0; --i) {
                    if (m_dimLockedForConstraints[i].first == label) {
                        m_dimLockedForConstraints.removeAt(i);
                        break;
                    }
                }
                updateEntity(m_currentMouseWorld);  // Re-apply without the constraint
                update();
                return;
            }
            // Fall through to normal Escape handling (cancel entity)
        }
    }

    // Shift key during arc slot or arc drawing - toggle flip state and update preview
    if (event->key() == Qt::Key_Shift && m_isDrawing) {
        bool isArcSlot = (m_activeTool == SketchTool::Slot &&
                          (m_slotMode == SlotMode::ArcRadius || m_slotMode == SlotMode::ArcEnds) &&
                          m_previewPoints.size() >= 2);
        bool isFlippableArc = (m_activeTool == SketchTool::Arc &&
                               (m_arcMode == ArcMode::CenterStartEnd || m_arcMode == ArcMode::StartEndRadius) &&
                               m_previewPoints.size() >= 2);
        bool isTangentArc = (m_activeTool == SketchTool::Arc &&
                             m_arcMode == ArcMode::Tangent &&
                             !m_previewPoints.isEmpty());
        if (isArcSlot || isFlippableArc || isTangentArc) {
            m_arcSlotFlipped = !m_arcSlotFlipped;
            update();
            return;
        }
    }

    // Ctrl key during StartEndRadius arc - update preview for 180° snap
    if (event->key() == Qt::Key_Control && m_isDrawing && m_previewPoints.size() >= 2) {
        if (m_activeTool == SketchTool::Arc && m_arcMode == ArcMode::StartEndRadius) {
            update();
            return;
        }
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
        } else if (m_enteredGroupId >= 0) {
            // Leave the entered group (re-selects the whole group)
            leaveGroup();
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
            // No entity selected, but sketch is selected - show exit bar
            // but stay in the sketch so the user can return
            m_sketchSelected = false;
            emit sketchDeselected();
            emit exitRequested();   // shows Save/Discard bar
        } else {
            // Sketch already deselected — pressing Escape again returns
            // to the sketch instead of being stuck at the Save/Discard bar
            m_sketchSelected = true;
            emit selectionChanged(-1);  // re-engage sketch
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

            // Recompute which entities are still constrained (fixes green→black)
            refreshConstrainedFlags();

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
        // If a single 2-point line is selected, immediately add a
        // Distance constraint (prompting for the value) instead of
        // switching to the Dimension tool.
        if (m_activeTool == SketchTool::Select && m_selectedId >= 0) {
            SketchEntity* sel = selectedEntity();
            if (sel && sel->type == SketchEntityType::Line && sel->points.size() == 2) {
                // Check the line doesn't already have a Distance constraint
                bool alreadyConstrained = false;
                for (const SketchConstraint& c : m_constraints) {
                    if (c.type == ConstraintType::Distance && c.isDriving && c.enabled) {
                        for (int eid : c.entityIds) {
                            if (eid == sel->id) { alreadyConstrained = true; break; }
                        }
                        if (alreadyConstrained) break;
                    }
                }
                if (!alreadyConstrained) {
                    double currentLen = QLineF(sel->points[0], sel->points[1]).length();
                    bool ok = false;
                    double value = QInputDialog::getDouble(
                        this, tr("Add Length Constraint"),
                        tr("Length (mm):"),
                        currentLen, 0.001, 1000000.0, 3, &ok);
                    if (ok) {
                        m_constraintTargetEntities.clear();
                        m_constraintTargetPoints.clear();
                        m_constraintTargetEntities.append(sel->id);
                        m_constraintTargetEntities.append(sel->id);
                        m_constraintTargetPoints.append(sel->points[0]);
                        m_constraintTargetPoints.append(sel->points[1]);

                        QPointF mid = (sel->points[0] + sel->points[1]) / 2.0;
                        QPointF along = sel->points[1] - sel->points[0];
                        double len = std::sqrt(along.x() * along.x() + along.y() * along.y());
                        QPointF perp = (len > 1e-6)
                            ? QPointF(-along.y() / len, along.x() / len)
                            : QPointF(0, -1);
                        QPointF labelPos = mid + perp * 10.0;

                        // Pin first endpoint so the solver resizes from
                        // one end instead of shrinking/growing from center
                        SketchConstraint pin;
                        pin.id = -999;
                        pin.type = ConstraintType::FixedPoint;
                        pin.entityIds.append(sel->id);
                        pin.pointIndices.append(0);
                        pin.isDriving = true;
                        pin.enabled = true;
                        pin.satisfied = true;
                        pin.labelVisible = false;
                        m_constraints.append(pin);

                        createConstraint(ConstraintType::Distance, value, labelPos);

                        // Remove temporary pin
                        m_constraints.erase(
                            std::remove_if(m_constraints.begin(), m_constraints.end(),
                                           [](const SketchConstraint& c) { return c.id == -999; }),
                            m_constraints.end());
                    }
                    break;
                }
            }
        }
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
        // Ctrl released during StartEndRadius arc - update preview
        if (m_isDrawing && m_previewPoints.size() >= 2 &&
            m_activeTool == SketchTool::Arc && m_arcMode == ArcMode::StartEndRadius) {
            update();
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

                // Recompute which entities are still constrained (fixes green→black)
                refreshConstrainedFlags();

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

    // ---------------------------------------------------------------
    //  Helper lambdas for building entity context menus
    // ---------------------------------------------------------------

    // Collect group names that an entity belongs to
    auto groupNamesForEntity = [this](int eid) -> QStringList {
        QStringList names;
        for (const SketchGroup& g : m_groups) {
            if (g.entityIds.contains(eid))
                names.append(g.name);
        }
        return names;
    };

    // Collect the distinct set of group IDs that the current selection
    // (or a single entity) belongs to
    auto groupIdsForSelection = [this](const QSet<int>& ids) -> QSet<int> {
        QSet<int> gids;
        for (const SketchGroup& g : m_groups) {
            for (int eid : ids) {
                if (g.entityIds.contains(eid)) {
                    gids.insert(g.id);
                    break;
                }
            }
        }
        return gids;
    };

    // Adds the "Member of Group(s):" info label at the top of a menu.
    // Greyed-out when no group membership exists.
    auto addGroupInfoLabel = [&](QMenu& menu, const QSet<int>& entityIds) {
        QStringList allNames;
        for (int eid : entityIds) {
            for (const QString& n : groupNamesForEntity(eid)) {
                if (!allNames.contains(n))
                    allNames.append(n);
            }
        }

        QAction* infoAction;
        if (allNames.isEmpty()) {
            infoAction = menu.addAction(tr("Member of Group(s): (none)"));
        } else {
            infoAction = menu.addAction(
                tr("Member of Group(s): %1").arg(allNames.join(QStringLiteral(", "))));
        }
        infoAction->setEnabled(false);  // always greyed — informational only
        menu.addSeparator();
    };

    // Adds Transform and Align submenus (used for both single + multi)
    auto addTransformAlignMenus = [this](QMenu& menu) {
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
    };

    // ---------------------------------------------------------------
    //  Multi-selection context menu
    // ---------------------------------------------------------------
    if (entityId >= 0 && m_selectedIds.size() > 1 && m_selectedIds.contains(entityId)) {
        QMenu menu(this);
        int count = m_selectedIds.size();

        // --- Group info label ---
        addGroupInfoLabel(menu, m_selectedIds);

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

        // --- Transform / Align (shared helper) ---
        addTransformAlignMenus(menu);

        menu.addSeparator();

        // --- Group / Ungroup ---
        QAction* groupAction = menu.addAction(tr("Group (%1 entities)").arg(count));
        connect(groupAction, &QAction::triggered, this, [this]() {
            groupSelectedEntities();
        });

        // Enter Group / Ungroup — show for every group that has at least
        // one selected member
        QSet<int> selGroupIds = groupIdsForSelection(m_selectedIds);
        if (!selGroupIds.isEmpty()) {
            for (int gid : selGroupIds) {
                const SketchGroup* grp = nullptr;
                for (const SketchGroup& g : m_groups) {
                    if (g.id == gid) { grp = &g; break; }
                }
                if (!grp) continue;

                QAction* enterAction = menu.addAction(
                    tr("Enter Group \"%1\"").arg(grp->name));
                connect(enterAction, &QAction::triggered, this, [this, gid]() {
                    enterGroup(gid);
                });

                QAction* ungroupAction = menu.addAction(
                    tr("Ungroup \"%1\"").arg(grp->name));
                connect(ungroupAction, &QAction::triggered, this, [this, gid]() {
                    ungroupEntities(gid);
                });
            }
        }

        menu.addSeparator();

        // Boolean-like operations
        QAction* splitAllAction = menu.addAction(tr("Split All at Intersections"));
        connect(splitAllAction, &QAction::triggered, this, [this]() {
            splitSelectedAtIntersections();
        });

        // Rejoin — only enabled when all selected are collinear lines
        {
            bool allLines = true;
            for (int id : m_selectedIds) {
                const SketchEntity* ent = entityById(id);
                if (!ent || ent->type != SketchEntityType::Line) {
                    allLines = false;
                    break;
                }
            }
            QAction* rejoinAction = menu.addAction(tr("Rejoin Segments"));
            rejoinAction->setEnabled(allLines);
            connect(rejoinAction, &QAction::triggered, this, [this]() {
                rejoinCollinearSegments();
            });
        }

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

    // ---------------------------------------------------------------
    //  Single entity context menu
    // ---------------------------------------------------------------
    if (entityId >= 0) {
        SketchEntity* entity = entityById(entityId);
        if (entity) {
            QMenu menu(this);

            // --- Group info label ---
            addGroupInfoLabel(menu, {entityId});

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

            // --- Transform / Align (same as multi, operates on selection) ---
            // Ensure this entity is selected so the transform functions work
            if (!m_selectedIds.contains(entityId)) {
                selectEntity(entityId);
            }
            addTransformAlignMenus(menu);

            menu.addSeparator();

            // --- Enter Group / Leave Group / Ungroup ---
            if (m_enteredGroupId >= 0) {
                // Already inside a group — offer Leave Group
                QString gName;
                for (const SketchGroup& g : m_groups) {
                    if (g.id == m_enteredGroupId) { gName = g.name; break; }
                }
                QAction* leaveAction = menu.addAction(
                    tr("Leave Group \"%1\"").arg(gName));
                connect(leaveAction, &QAction::triggered, this, [this]() {
                    leaveGroup();
                });
                menu.addSeparator();
            }

            QSet<int> entGroupIds = groupIdsForSelection({entityId});
            if (!entGroupIds.isEmpty()) {
                for (int gid : entGroupIds) {
                    const SketchGroup* grp = nullptr;
                    for (const SketchGroup& g : m_groups) {
                        if (g.id == gid) { grp = &g; break; }
                    }
                    if (!grp) continue;

                    // Only show Enter Group when not already inside it
                    if (m_enteredGroupId != gid) {
                        QAction* enterAction = menu.addAction(
                            tr("Enter Group \"%1\"").arg(grp->name));
                        connect(enterAction, &QAction::triggered, this, [this, gid]() {
                            enterGroup(gid);
                        });
                    }

                    QAction* ungroupAction = menu.addAction(
                        tr("Ungroup \"%1\"").arg(grp->name));
                    connect(ungroupAction, &QAction::triggered, this, [this, gid]() {
                        ungroupEntities(gid);
                    });
                }
                menu.addSeparator();
            }

            // --- Split submenu ---
            QMenu* splitMenu = menu.addMenu(tr("Split"));

            QAction* splitNearAction = splitMenu->addAction(tr("At Nearest Intersections"));
            connect(splitNearAction, &QAction::triggered, this, [this, entityId, worldPos]() {
                QVector<int> newIds = splitEntityNearClick(entityId, worldPos);
                if (newIds.isEmpty()) {
                    QMessageBox::information(this, tr("Split"),
                        tr("No intersections found near the click point."));
                }
            });

            QAction* splitAllAction = splitMenu->addAction(tr("At All Intersections"));
            connect(splitAllAction, &QAction::triggered, this, [this, entityId]() {
                QVector<int> newIds = splitEntityAtIntersections(entityId);
                if (newIds.isEmpty()) {
                    QMessageBox::information(this, tr("Split"),
                        tr("No intersections found on this entity."));
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

    // No specific item clicked - show general sketch menu with export options
    if (!m_entities.isEmpty()) {
        QMenu menu(this);

        QAction* exportDXFAction = menu.addAction(tr("Export as DXF..."));
        connect(exportDXFAction, &QAction::triggered, this, [this]() {
            QString filePath = QFileDialog::getSaveFileName(
                this, tr("Export DXF File"), QString(),
                tr("DXF Files (*.dxf);;All Files (*)"));
            if (filePath.isEmpty()) return;
            if (!filePath.toLower().endsWith(QLatin1String(".dxf")))
                filePath += QStringLiteral(".dxf");

            QVector<sketch::Entity> entities;
            entities.reserve(m_entities.size());
            for (const auto& e : m_entities)
                entities.append(static_cast<const sketch::Entity&>(e));

            sketch::DXFExportOptions options;
            if (!sketch::exportSketchToDXF(entities, filePath, options)) {
                QMessageBox::critical(this, tr("Export Failed"),
                    tr("Failed to export DXF file."));
            }
        });

        QAction* exportSVGAction = menu.addAction(tr("Export as SVG..."));
        connect(exportSVGAction, &QAction::triggered, this, [this]() {
            QString filePath = QFileDialog::getSaveFileName(
                this, tr("Export SVG File"), QString(),
                tr("SVG Files (*.svg);;All Files (*)"));
            if (filePath.isEmpty()) return;
            if (!filePath.toLower().endsWith(QLatin1String(".svg")))
                filePath += QStringLiteral(".svg");

            QVector<sketch::Entity> entities;
            entities.reserve(m_entities.size());
            for (const auto& e : m_entities)
                entities.append(static_cast<const sketch::Entity&>(e));

            QVector<sketch::Constraint> constraints;
            for (const auto& c : m_constraints) {
                sketch::Constraint lc;
                lc.id = c.id;
                lc.type = static_cast<sketch::ConstraintType>(c.type);
                lc.entityIds = c.entityIds;
                lc.pointIndices = c.pointIndices;
                lc.value = c.value;
                lc.isDriving = c.isDriving;
                lc.labelPosition = c.labelPosition;
                lc.labelVisible = c.labelVisible;
                lc.enabled = c.enabled;
                constraints.append(lc);
            }

            sketch::SVGExportOptions options;
            if (!sketch::exportSketchToSVG(entities, constraints, filePath, options)) {
                QMessageBox::critical(this, tr("Export Failed"),
                    tr("Failed to export SVG file."));
            }
        });

        menu.exec(event->globalPos());
        return;
    }

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
            pushUndoCommand(sketch::UndoCommand::deleteEntity(m_entities[i]));
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
            pushUndoCommand(sketch::UndoCommand::deleteConstraint(m_constraints[i]));
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

    // Remove deleted entities and orphaned constraints from groups
    for (SketchGroup& group : m_groups) {
        for (int id : toDelete) {
            group.entityIds.removeAll(id);
        }
        // Remove constraints that no longer exist
        group.constraintIds.erase(
            std::remove_if(group.constraintIds.begin(), group.constraintIds.end(),
                           [this](int cid) {
                               return std::none_of(m_constraints.begin(), m_constraints.end(),
                                                   [cid](const SketchConstraint& c) { return c.id == cid; });
                           }),
            group.constraintIds.end());
    }
    // Remove empty groups
    m_groups.erase(
        std::remove_if(m_groups.begin(), m_groups.end(),
                       [](const SketchGroup& g) { return g.isEmpty(); }),
        m_groups.end());

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

    // Include constraints whose referenced entities are all within the
    // selection — they logically belong to this group.
    for (const auto& c : m_constraints) {
        bool allInside = !c.entityIds.isEmpty();
        for (int eid : c.entityIds) {
            if (!m_selectedIds.contains(eid)) {
                allInside = false;
                break;
            }
        }
        if (allInside)
            group.constraintIds.append(c.id);
    }

    // Set groupId on each member entity
    for (int eid : group.entityIds) {
        SketchEntity* ent = entityById(eid);
        if (ent) ent->groupId = group.id;
    }

    m_groups.append(group);
    update();
    return group.id;
}

void SketchCanvas::ungroupEntities(int groupId)
{
    // Clear groupId on member entities before removing the group
    for (const SketchGroup& g : m_groups) {
        if (g.id == groupId) {
            for (int eid : g.entityIds) {
                SketchEntity* ent = entityById(eid);
                if (ent && ent->groupId == groupId)
                    ent->groupId = -1;
            }
            break;
        }
    }

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

    // Text needs QFont/QFontMetrics and zoom — genuinely GUI-specific
    if (entity.type == SketchEntityType::Text)
        return hitTestTextEntity(entity, worldPos, tolerance);

    // All other entity types delegate to the library's containsPoint()
    return entity.containsPoint(worldPos, tolerance);
}

bool SketchCanvas::hitTestTextEntity(const SketchEntity& entity, const QPointF& worldPos, double /*tolerance*/) const
{
    if (entity.points.isEmpty()) return false;

    // Construct font matching drawEntity to get accurate metrics
    QFont font;
    if (!entity.fontFamily.isEmpty()) {
        font.setFamily(entity.fontFamily);
    }
    // fontSize is in mm; scale by zoom for screen metrics, then convert back to world
    double scaledSize = entity.fontSize * m_zoom;
    font.setPointSizeF(qMax(6.0, scaledSize));
    font.setBold(entity.fontBold);
    font.setItalic(entity.fontItalic);

    QFontMetricsF fm(font);
    QRectF textBounds = fm.boundingRect(entity.text);
    // Convert screen-space bounds back to world-space dimensions
    double worldWidth = textBounds.width() / m_zoom;
    double worldHeight = textBounds.height() / m_zoom;

    QPointF pos = entity.points[0];
    // Text baseline is at pos; bounding box extends upward
    QRectF worldRect(pos.x(), pos.y() - worldHeight, worldWidth, worldHeight);

    if (qAbs(entity.textRotation) > 0.01) {
        // For rotated text, transform the test point into the text's local frame
        double rad = qDegreesToRadians(entity.textRotation);
        double cosR = std::cos(rad);
        double sinR = std::sin(rad);
        double dx = worldPos.x() - pos.x();
        double dy = worldPos.y() - pos.y();
        // Rotate worldPos into text-local coordinates (inverse rotation)
        QPointF localPos(dx * cosR + dy * sinR, -dx * sinR + dy * cosR);
        QRectF localRect(0, -worldHeight, worldWidth, worldHeight);
        return localRect.contains(localPos);
    } else {
        return worldRect.contains(worldPos);
    }
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
        if (entity.points.size() >= 4) {
            // 4-point rotated rectangle: check if any edge intersects the rect
            for (int i = 0; i < 4; ++i) {
                QPointF p1 = entity.points[i];
                QPointF p2 = entity.points[(i + 1) % 4];
                if (rect.contains(p1)) return true;
                // Check if edge crosses rect
                QLineF edge(p1, p2);
                QLineF sides[4] = {
                    QLineF(rect.topLeft(), rect.topRight()),
                    QLineF(rect.topRight(), rect.bottomRight()),
                    QLineF(rect.bottomRight(), rect.bottomLeft()),
                    QLineF(rect.bottomLeft(), rect.topLeft())
                };
                for (const auto& side : sides) {
                    QPointF intersection;
                    if (edge.intersects(side, &intersection) == QLineF::BoundedIntersection)
                        return true;
                }
            }
        } else if (entity.points.size() >= 2) {
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
        if (entity.points.size() >= 4) {
            // 4-point rotated rectangle: all 4 corners must be enclosed
            return rect.contains(entity.points[0]) && rect.contains(entity.points[1]) &&
                   rect.contains(entity.points[2]) && rect.contains(entity.points[3]);
        } else if (entity.points.size() >= 2) {
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
        if (entity.points.size() >= 4) {
            // 4-point (rotated) rectangle: directly return the stored corners
            for (int i = 0; i < 4; ++i) {
                endpoints.append(entity.points[i]);
            }
        } else if (entity.points.size() >= 2) {
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

bool SketchCanvas::hitTestGroupHandle(const QPointF& worldPos,
                                       int& outEntityId, int& outHandleIdx) const
{
    const SketchEntity* sel = selectedEntity();
    if (!sel) return false;

    const double tolerance = 6.0 / m_zoom;

    // If the primary entity is in a group (and we're not inside the group),
    // test handles across every entity in the group.
    if (sel->groupId >= 0 && m_enteredGroupId < 0) {
        double bestDist = tolerance;
        bool found = false;
        for (const auto& e : m_entities) {
            if (e.groupId != sel->groupId) continue;
            for (int i = 0; i < e.points.size(); ++i) {
                double d = QLineF(e.points[i], worldPos).length();
                if (d < bestDist) {
                    bestDist = d;
                    outEntityId = e.id;
                    outHandleIdx = i;
                    found = true;
                }
            }
        }
        return found;
    }

    // Fall back to primary entity only
    for (int i = 0; i < sel->points.size(); ++i) {
        if (QLineF(sel->points[i], worldPos).length() < tolerance) {
            outEntityId = sel->id;
            outHandleIdx = i;
            return true;
        }
    }
    return false;
}

void SketchCanvas::startEntity(const QPointF& pos)
{
    m_isDrawing = true;
    m_previewPoints.clear();
    m_previewPoints.append(pos);
    m_arcSlotFlipped = false;  // Reset flip state for new arc slot
    m_rectBothLocked = false;  // Reset rotation state for new rectangle
    clearDimFields();  // Reset dim input for new entity

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
        if (m_lineMode == LineMode::Construction) {
            m_pendingEntity.isConstruction = true;
        }
        break;
    case SketchTool::Rectangle:
        // Parallelogram mode creates a Parallelogram entity, others create Rectangle
        if (m_rectMode == RectMode::Parallelogram) {
            m_pendingEntity.type = SketchEntityType::Parallelogram;
        } else {
            m_pendingEntity.type = SketchEntityType::Rectangle;
        }
        break;
    case SketchTool::Circle:
        m_pendingEntity.type = SketchEntityType::Circle;
        break;
    case SketchTool::Arc:
        m_pendingEntity.type = SketchEntityType::Arc;
        break;
    case SketchTool::Polygon:
        m_pendingEntity.type = SketchEntityType::Polygon;
        if (m_polygonMode != PolygonMode::Freeform) {
            m_pendingEntity.sides = 6;  // Default hexagon
        }
        // Freeform: sides will be derived from point count at finish time
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

    // Initialize inline dimension fields for the new entity
    if (m_isDrawing) {
        initDimFields();
    }
}

void SketchCanvas::updateEntity(const QPointF& pos)
{
    if (!m_isDrawing) return;

    // Update pending entity based on tool
    switch (m_activeTool) {
    case SketchTool::Line: {
        QPointF endpoint = pos;
        // Apply locked dimensions if any
        double lockedLen = getLockedDim(0);   // field 0 = Length
        double lockedAng = getLockedDim(1);   // field 1 = Angle
        if (lockedLen > 0 || lockedAng != -1.0) {
            QPointF start = m_pendingEntity.points[0];
            QPointF dir = pos - start;
            double mouseLen = std::sqrt(dir.x() * dir.x() + dir.y() * dir.y());
            double mouseAng = std::atan2(dir.y(), dir.x());

            double useLen = (lockedLen > 0) ? lockedLen : mouseLen;
            double useAng = (lockedAng != -1.0) ? qDegreesToRadians(lockedAng) : mouseAng;

            if (useLen > 0.001) {
                endpoint = start + QPointF(useLen * std::cos(useAng), useLen * std::sin(useAng));
            }
        }
        if (m_pendingEntity.points.size() > 1) {
            m_pendingEntity.points[1] = endpoint;
        } else {
            m_pendingEntity.points.append(endpoint);
        }
        break;
    }

    case SketchTool::Rectangle:
        if (m_rectMode == RectMode::Center) {
            // Center mode: point[0] is center, compute opposite corners
            if (!m_pendingEntity.points.isEmpty()) {
                QPointF center = m_pendingEntity.points[0];
                // Compute delta from center to mouse position
                QPointF delta = pos - center;
                // Apply locked dimensions
                double lockedW = getLockedDim(0);
                double lockedH = getLockedDim(1);
                if (lockedW > 0) delta.setX(delta.x() >= 0 ? lockedW / 2.0 : -lockedW / 2.0);
                if (lockedH > 0) delta.setY(delta.y() >= 0 ? lockedH / 2.0 : -lockedH / 2.0);
                // Opposite corner mirrors across center
                QPointF corner1 = center - delta;
                QPointF corner2 = center + delta;
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
            // 3-Point mode: apply locked dimension constraints to preview position.
            // Override m_currentMouseWorld so the preview draws at the constrained position.
            int stage = m_previewPoints.size();
            if (stage == 1) {
                // Stage 1: defining edge (p1 → p2). Same math as Line/Parallelogram.
                QPointF p1 = m_previewPoints[0];
                double lockedLen = getLockedDim(0);   // Edge Length
                double lockedAng = getLockedDim(1);   // Edge Angle
                if (lockedLen > 0 || lockedAng != -1.0) {
                    QPointF dir = pos - p1;
                    double mouseLen = std::sqrt(dir.x() * dir.x() + dir.y() * dir.y());
                    double mouseAng = std::atan2(dir.y(), dir.x());
                    double useLen = (lockedLen > 0) ? lockedLen : mouseLen;
                    double useAng = (lockedAng != -1.0) ? qDegreesToRadians(lockedAng) : mouseAng;
                    if (useLen > 0.001) {
                        m_currentMouseWorld = p1 + QPointF(useLen * std::cos(useAng),
                                                           useLen * std::sin(useAng));
                    }
                }
            } else if (stage >= 2) {
                // Stage 2: defining width (perpendicular offset from edge).
                // Preview projects m_currentMouseWorld onto perpendicular of p1-p2.
                // Locked width → set m_currentMouseWorld so projection gives locked width.
                double lockedW = getLockedDim(0);  // Width
                if (lockedW > 0) {
                    QPointF p1 = m_previewPoints[0];
                    QPointF p2 = m_previewPoints[1];
                    QPointF edge = p2 - p1;
                    double edgeLen = std::sqrt(edge.x() * edge.x() + edge.y() * edge.y());
                    if (edgeLen > 0.001) {
                        QPointF edgeDir = edge / edgeLen;
                        QPointF perpDir(-edgeDir.y(), edgeDir.x());
                        QPointF toMouse = pos - p1;
                        double perpDot = toMouse.x() * perpDir.x() + toMouse.y() * perpDir.y();
                        double sign = (perpDot >= 0) ? 1.0 : -1.0;
                        // Keep edge-parallel position from mouse
                        double edgeDot = toMouse.x() * edgeDir.x() + toMouse.y() * edgeDir.y();
                        m_currentMouseWorld = p1 + edgeDir * edgeDot + perpDir * sign * lockedW;
                    }
                }
            }
        } else if (m_rectMode == RectMode::Parallelogram) {
            // Parallelogram mode: apply locked dimension constraints to preview position.
            // Override m_currentMouseWorld so the preview draws at the constrained position.
            // Don't modify m_pendingEntity.points here — that breaks click-click mode.
            int stage = m_previewPoints.size();
            if (stage == 1) {
                // Stage 1: defining edge1 (p1 → p2)
                QPointF p1 = m_previewPoints[0];
                double lockedLen = getLockedDim(0);   // Edge1
                double lockedAng = getLockedDim(1);   // Edge1 Angle
                if (lockedLen > 0 || lockedAng != -1.0) {
                    QPointF dir = pos - p1;
                    double mouseLen = std::sqrt(dir.x() * dir.x() + dir.y() * dir.y());
                    double mouseAng = std::atan2(dir.y(), dir.x());
                    double useLen = (lockedLen > 0) ? lockedLen : mouseLen;
                    double useAng = (lockedAng != -1.0) ? qDegreesToRadians(lockedAng) : mouseAng;
                    if (useLen > 0.001) {
                        m_currentMouseWorld = p1 + QPointF(useLen * std::cos(useAng),
                                                           useLen * std::sin(useAng));
                    }
                }
            } else if (stage >= 2) {
                // Stage 2: defining edge2 (p2 → p3)
                QPointF p1 = m_previewPoints[0];
                QPointF p2 = m_previewPoints[1];
                double lockedLen = getLockedDim(0);   // Edge2
                double lockedAng = getLockedDim(1);   // Edge2 Angle (inside angle at p2)
                if (lockedLen > 0 || lockedAng != -1.0) {
                    QPointF dir = pos - p2;
                    double mouseLen = std::sqrt(dir.x() * dir.x() + dir.y() * dir.y());
                    double mouseAng = std::atan2(dir.y(), dir.x());
                    double useLen = (lockedLen > 0) ? lockedLen : mouseLen;
                    double useAng;
                    if (lockedAng != -1.0) {
                        // Inside angle: between vectors p2→p1 and p2→p3
                        double edge1Dir = std::atan2(p1.y() - p2.y(), p1.x() - p2.x());
                        double dir1 = edge1Dir + qDegreesToRadians(lockedAng);
                        double dir2 = edge1Dir - qDegreesToRadians(lockedAng);
                        // Pick direction closest to mouse
                        double diff1 = std::abs(std::remainder(mouseAng - dir1, 2.0 * M_PI));
                        double diff2 = std::abs(std::remainder(mouseAng - dir2, 2.0 * M_PI));
                        useAng = (diff1 <= diff2) ? dir1 : dir2;
                    } else {
                        useAng = mouseAng;
                    }
                    if (useLen > 0.001) {
                        m_currentMouseWorld = p2 + QPointF(useLen * std::cos(useAng),
                                                           useLen * std::sin(useAng));
                    }
                }
            }
        } else {
            // Corner mode: standard corner-to-corner
            double lockedW = getLockedDim(0);  // field 0 = Width
            double lockedH = getLockedDim(1);  // field 1 = Height

            if (lockedW > 0 && lockedH > 0 && m_rectBothLocked) {
                // Both locked: user rotates the rectangle around the first corner.
                // Rotation is relative to the axis-aligned state at lock time,
                // so the rectangle starts axis-aligned and rotates as the mouse moves.
                QPointF origin = m_previewPoints[0];
                QPointF delta = pos - origin;
                double currentAngle = std::atan2(delta.y(), delta.x());
                double rotation = currentAngle - m_rectLockRefAngle;
                double wAngle = m_rectLockWidthAngle + rotation;
                double hAngle = m_rectLockHeightAngle + rotation;
                QPointF wDir(std::cos(wAngle), std::sin(wAngle));
                QPointF hDir(std::cos(hAngle), std::sin(hAngle));
                // 4 corners: origin, along width, diagonal, along height
                QPointF p0 = origin;
                QPointF p1 = origin + wDir * lockedW;
                QPointF p2 = p1 + hDir * lockedH;
                QPointF p3 = origin + hDir * lockedH;
                // Store all 4 corners for rotated rectangle
                while (m_pendingEntity.points.size() < 4)
                    m_pendingEntity.points.append(QPointF());
                m_pendingEntity.points[0] = p0;
                m_pendingEntity.points[1] = p1;
                m_pendingEntity.points[2] = p2;
                m_pendingEntity.points[3] = p3;
            } else {
                // One or no dim locked: axis-aligned (2-point) rectangle
                QPointF corner = pos;
                if (lockedW > 0 || lockedH > 0) {
                    QPointF origin = m_pendingEntity.points[0];
                    double dx = pos.x() - origin.x();
                    double dy = pos.y() - origin.y();
                    if (lockedW > 0) dx = (dx >= 0 ? lockedW : -lockedW);
                    if (lockedH > 0) dy = (dy >= 0 ? lockedH : -lockedH);
                    corner = origin + QPointF(dx, dy);
                }
                // Keep only 2 points for axis-aligned mode
                while (m_pendingEntity.points.size() > 2)
                    m_pendingEntity.points.removeLast();
                if (m_pendingEntity.points.size() > 1) {
                    m_pendingEntity.points[1] = corner;
                } else {
                    m_pendingEntity.points.append(corner);
                }
            }
        }
        break;

    case SketchTool::Circle:
        if (!m_pendingEntity.points.isEmpty()) {
            if (m_circleMode == CircleMode::TwoPoint) {
                // Two-point (diameter) mode: point[0] is one end, mouse is other end
                QPointF p1 = m_pendingEntity.points[0];
                QPointF endpoint = pos;
                double lockedD = getLockedDim(0);  // field 0 = Diameter
                if (lockedD > 0) {
                    // Constrain endpoint at locked diameter distance in mouse direction
                    QPointF dir = pos - p1;
                    double len = std::sqrt(dir.x() * dir.x() + dir.y() * dir.y());
                    if (len > 1e-6) {
                        endpoint = p1 + dir * (lockedD / len);
                    }
                }
                double diameter = QLineF(p1, endpoint).length();
                m_pendingEntity.radius = diameter / 2.0;
                if (m_pendingEntity.points.size() > 1) {
                    m_pendingEntity.points[1] = endpoint;
                } else {
                    m_pendingEntity.points.append(endpoint);
                }
            } else if (m_circleMode == CircleMode::ThreePoint) {
                // ThreePoint mode: points are added on click, not on mouse move
                // Just update m_currentMouseWorld which is used by drawPreview()
                // Don't modify m_pendingEntity.points here - that breaks click-click mode.
            } else {
                // Center-radius mode: point[0] is center, mouse defines radius
                double lockedR = getLockedDim(0);  // field 0 = Radius
                QPointF center = m_pendingEntity.points[0];
                QPointF perimPt = pos;
                if (lockedR > 0) {
                    // Constrain to locked radius in mouse direction
                    QPointF dir = pos - center;
                    double len = std::sqrt(dir.x() * dir.x() + dir.y() * dir.y());
                    if (len > 1e-6) {
                        perimPt = center + dir * (lockedR / len);
                    }
                    m_pendingEntity.radius = lockedR;
                } else {
                    m_pendingEntity.radius = QLineF(center, pos).length();
                }
                // Store the perimeter point
                if (m_pendingEntity.points.size() > 1) {
                    m_pendingEntity.points[1] = perimPt;
                } else {
                    m_pendingEntity.points.append(perimPt);
                }
            }
        }
        break;

    case SketchTool::Polygon:  // Polygon uses radius like circle
        if (!m_pendingEntity.points.isEmpty()) {
            double lockedR = getLockedDim(0);  // field 0 = Radius
            if (lockedR > 0) {
                m_pendingEntity.radius = lockedR;
                // Constrain cursor position to locked radius (keeps angle, fixes distance)
                QPointF center = m_pendingEntity.points[0];
                QPointF dir = pos - center;
                double len = std::sqrt(dir.x() * dir.x() + dir.y() * dir.y());
                if (len > 1e-6) {
                    m_currentMouseWorld = center + dir * (lockedR / len);
                }
            } else {
                m_pendingEntity.radius = QLineF(m_pendingEntity.points[0], pos).length();
            }
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
        } else if (m_arcMode == ArcMode::CenterStartEnd) {
            // Apply locked dimension constraints to preview position.
            int stage = m_previewPoints.size();
            if (stage == 1) {
                // Stage 1: center placed, defining start point → locked Radius constrains distance
                double lockedR = getLockedDim(0);  // Radius
                if (lockedR > 0) {
                    QPointF center = m_previewPoints[0];
                    QPointF dir = pos - center;
                    double len = std::sqrt(dir.x() * dir.x() + dir.y() * dir.y());
                    if (len > 1e-6) {
                        m_currentMouseWorld = center + dir * (lockedR / len);
                    }
                }
            } else if (stage >= 2) {
                // Stage 2: center+start placed, defining end → locked Sweep constrains angle
                double lockedSweep = getLockedDim(0);  // Sweep Angle (field 0 in stage 2)
                if (lockedSweep != -1.0) {
                    QPointF center = m_previewPoints[0];
                    QPointF start = m_previewPoints[1];
                    double radius = QLineF(center, start).length();
                    double startAngle = std::atan2(start.y() - center.y(), start.x() - center.x());
                    double mouseAngle = std::atan2(pos.y() - center.y(), pos.x() - center.x());
                    // Determine sweep direction from mouse
                    double defaultSweep = mouseAngle - startAngle;
                    while (defaultSweep > M_PI) defaultSweep -= 2.0 * M_PI;
                    while (defaultSweep < -M_PI) defaultSweep += 2.0 * M_PI;
                    if (m_arcSlotFlipped) {
                        defaultSweep = (defaultSweep > 0) ? defaultSweep - 2.0 * M_PI : defaultSweep + 2.0 * M_PI;
                    }
                    double sign = (defaultSweep >= 0) ? 1.0 : -1.0;
                    double endAngle = startAngle + sign * qDegreesToRadians(std::abs(lockedSweep));
                    m_currentMouseWorld = center + QPointF(radius * std::cos(endAngle),
                                                           radius * std::sin(endAngle));
                }
            }
        } else if (m_arcMode == ArcMode::StartEndRadius) {
            int stage = m_previewPoints.size();
            if (stage == 1) {
                // Stage 1: start placed, defining end → locked Chord Length/Angle
                QPointF start = m_previewPoints[0];
                double lockedLen = getLockedDim(0);  // Chord Length
                double lockedAng = getLockedDim(1);  // Chord Angle
                if (lockedLen > 0 || lockedAng != -1.0) {
                    QPointF dir = pos - start;
                    double mouseLen = std::sqrt(dir.x() * dir.x() + dir.y() * dir.y());
                    double mouseAng = std::atan2(dir.y(), dir.x());
                    double useLen = (lockedLen > 0) ? lockedLen : mouseLen;
                    double useAng = (lockedAng != -1.0) ? qDegreesToRadians(lockedAng) : mouseAng;
                    if (useLen > 0.001) {
                        m_currentMouseWorld = start + QPointF(useLen * std::cos(useAng),
                                                              useLen * std::sin(useAng));
                    }
                }
            } else if (stage >= 2) {
                // Stage 2: start+end placed, mouse controls arc center on perp bisector
                // Locked Sweep → compute perpendicular bisector projection distance
                double lockedSweep = getLockedDim(0);  // Sweep Angle
                if (lockedSweep != -1.0) {
                    QPointF start = m_previewPoints[0];
                    QPointF end = m_previewPoints[1];
                    QPointF midChord = (start + end) / 2.0;
                    double chordLength = QLineF(start, end).length();
                    if (chordLength > 0.001) {
                        QPointF chordDir = (end - start) / chordLength;
                        QPointF perpDir(-chordDir.y(), chordDir.x());
                        // projDist = (chordLength/2) / tan(sweepAngle/2)
                        double halfSweepRad = qDegreesToRadians(std::abs(lockedSweep)) / 2.0;
                        double tanHalf = std::tan(halfSweepRad);
                        double projDist = (tanHalf > 1e-6) ? (chordLength / 2.0) / tanHalf : 1e6;
                        // Sign from mouse position (or flip)
                        QPointF toMouse = pos - midChord;
                        double mouseProjDist = toMouse.x() * perpDir.x() + toMouse.y() * perpDir.y();
                        if (m_arcSlotFlipped) mouseProjDist = -mouseProjDist;
                        double sign = (mouseProjDist >= 0) ? 1.0 : -1.0;
                        if (m_arcSlotFlipped) sign = -sign;
                        m_currentMouseWorld = midChord + perpDir * sign * projDist;
                    }
                }
            }
        } else if (m_arcMode == ArcMode::ThreePoint) {
            // ThreePoint arc has no dim fields — no constraint support needed
        }
        break;

    case SketchTool::Slot:
        if (m_slotMode == SlotMode::ArcRadius) {
            // Arc slot (Radius mode): apply locked dimension constraints
            int stage = m_previewPoints.size();
            if (stage == 1) {
                // Stage 1: arc center placed, defining start → locked Radius constrains distance
                double lockedR = getLockedDim(0);  // Radius
                if (lockedR > 0) {
                    QPointF arcCenter = m_previewPoints[0];
                    QPointF dir = pos - arcCenter;
                    double len = std::sqrt(dir.x() * dir.x() + dir.y() * dir.y());
                    if (len > 1e-6) {
                        m_currentMouseWorld = arcCenter + dir * (lockedR / len);
                    }
                }
            } else if (stage >= 2) {
                // Stage 2: arc center+start placed, defining end → locked Sweep constrains angle
                double lockedSweep = getLockedDim(0);  // Sweep Angle
                if (lockedSweep != -1.0) {
                    QPointF arcCenter = m_previewPoints[0];
                    QPointF start = m_previewPoints[1];
                    double arcRadius = QLineF(arcCenter, start).length();
                    double startAngle = std::atan2(start.y() - arcCenter.y(),
                                                   start.x() - arcCenter.x());
                    double mouseAngle = std::atan2(pos.y() - arcCenter.y(),
                                                   pos.x() - arcCenter.x());
                    double defaultSweep = mouseAngle - startAngle;
                    while (defaultSweep > M_PI) defaultSweep -= 2.0 * M_PI;
                    while (defaultSweep < -M_PI) defaultSweep += 2.0 * M_PI;
                    if (m_arcSlotFlipped) {
                        defaultSweep = (defaultSweep > 0) ? defaultSweep - 2.0 * M_PI : defaultSweep + 2.0 * M_PI;
                    }
                    double sign = (defaultSweep >= 0) ? 1.0 : -1.0;
                    double endAngle = startAngle + sign * qDegreesToRadians(std::abs(lockedSweep));
                    m_currentMouseWorld = arcCenter + QPointF(arcRadius * std::cos(endAngle),
                                                              arcRadius * std::sin(endAngle));
                }
            }
        } else if (m_slotMode == SlotMode::ArcEnds) {
            // Arc slot (Ends mode): apply locked dimension constraints
            int stage = m_previewPoints.size();
            if (stage >= 2) {
                // Stage 2: start+end placed, mouse controls arc center on perp bisector
                double lockedSweep = getLockedDim(0);  // Sweep Angle
                if (lockedSweep != -1.0) {
                    QPointF start = m_previewPoints[0];
                    QPointF end = m_previewPoints[1];
                    QPointF midpoint = (start + end) / 2.0;
                    double chordLen = QLineF(start, end).length();
                    if (chordLen > 0.001) {
                        QPointF startToEnd = end - start;
                        QPointF perpDir(-startToEnd.y() / chordLen, startToEnd.x() / chordLen);
                        double halfSweepRad = qDegreesToRadians(std::abs(lockedSweep)) / 2.0;
                        double tanHalf = std::tan(halfSweepRad);
                        double projDist = (tanHalf > 1e-6) ? (chordLen / 2.0) / tanHalf : 1e6;
                        QPointF mouseToMid = pos - midpoint;
                        double mouseProjDist = mouseToMid.x() * perpDir.x() + mouseToMid.y() * perpDir.y();
                        double sign = (mouseProjDist >= 0) ? 1.0 : -1.0;
                        m_currentMouseWorld = midpoint + perpDir * sign * projDist;
                    }
                }
            }
        } else {
            // Linear slot (CenterToCenter or Overall) - two endpoints
            QPointF endpoint = pos;
            double lockedLen = getLockedDim(0);  // field 0 = Length
            if (lockedLen > 0) {
                QPointF start = m_pendingEntity.points[0];
                QPointF dir = pos - start;
                double len = std::sqrt(dir.x() * dir.x() + dir.y() * dir.y());
                if (len > 1e-6) {
                    endpoint = start + dir * (lockedLen / len);
                }
            }
            if (m_pendingEntity.points.size() > 1) {
                m_pendingEntity.points[1] = endpoint;
            } else {
                m_pendingEntity.points.append(endpoint);
            }
        }
        break;

    case SketchTool::Ellipse: {  // Ellipse: center to edge defines major axis, then minor
        QPointF edgePt = pos;
        double lockedR = getLockedDim(0);  // field 0 = Major Radius
        if (lockedR > 0 && !m_pendingEntity.points.isEmpty()) {
            QPointF center = m_pendingEntity.points[0];
            QPointF dir = pos - center;
            double len = std::sqrt(dir.x() * dir.x() + dir.y() * dir.y());
            if (len > 1e-6) {
                edgePt = center + dir * (lockedR / len);
            }
        }
        if (m_pendingEntity.points.size() > 1) {
            m_pendingEntity.points[1] = edgePt;
        } else {
            m_pendingEntity.points.append(edgePt);
        }
        break;
    }

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
        // Clear tangent targets if this was a tangent line
        if (m_lineMode == LineMode::Tangent) {
            m_tangentTargets.clear();
        }
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
    case SketchEntityType::Parallelogram:
        // Parallelogram: 3 points clicked (p1, p2, p3), 4th is computed
        // p1-p2 is first edge, p2-p3 is second edge, p4 = p1 + (p3 - p2)
        if (m_pendingEntity.points.size() >= 3) {
            QPointF p1 = m_pendingEntity.points[0];
            QPointF p2 = m_pendingEntity.points[1];
            QPointF p3 = m_pendingEntity.points[2];

            // p4 completes the parallelogram: p4 = p1 + (p3 - p2)
            QPointF p4 = p1 + (p3 - p2);

            // Store all 4 corners
            m_pendingEntity.points.clear();
            m_pendingEntity.points.append(p1);
            m_pendingEntity.points.append(p2);
            m_pendingEntity.points.append(p3);
            m_pendingEntity.points.append(p4);

            valid = QLineF(p1, p2).length() > 0.1 && QLineF(p2, p3).length() > 0.1;
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
        } else if (m_circleMode == CircleMode::TwoPoint) {
            // Two-point (diameter) circle: points[0] is first diameter end, points[1] is second
            valid = m_pendingEntity.radius > 0.1;
            if (valid && m_pendingEntity.points.size() >= 2) {
                QPointF p1 = m_pendingEntity.points[0];
                QPointF p2 = m_pendingEntity.points[1];
                QPointF center = (p1 + p2) / 2.0;
                double diameter = QLineF(p1, p2).length();
                m_pendingEntity.radius = diameter / 2.0;
                // Store as [center, p1, p2] - the two diameter endpoints
                m_pendingEntity.points.clear();
                m_pendingEntity.points.append(center);
                m_pendingEntity.points.append(p1);
                m_pendingEntity.points.append(p2);
            }
        } else if (m_circleMode == CircleMode::ThreePoint) {
            // Three-point circle: calculate circumcircle from 3 points
            // Store as: [center, p1, p2, p3] where p1, p2, p3 are the clicked points on perimeter
            if (m_pendingEntity.points.size() >= 3) {
                QPointF p1 = m_pendingEntity.points[0];
                QPointF p2 = m_pendingEntity.points[1];
                QPointF p3 = m_pendingEntity.points[2];

                // Use library function for circumcircle calculation
                auto arc = geometry::arcFromThreePoints(p1, p2, p3);
                if (arc.has_value() && arc->radius > 0.1) {
                    m_pendingEntity.radius = arc->radius;
                    m_pendingEntity.points.clear();
                    m_pendingEntity.points.append(arc->center);  // points[0] = center
                    m_pendingEntity.points.append(p1);           // points[1] = first clicked point
                    m_pendingEntity.points.append(p2);           // points[2] = second clicked point
                    m_pendingEntity.points.append(p3);           // points[3] = third clicked point
                    valid = true;
                }
            }
        } else {
            // Standard center-radius circle
            // points[0] = center, points[1] = clicked perimeter point (set by updateEntity)
            valid = m_pendingEntity.radius > 0.1 && m_pendingEntity.points.size() >= 2;
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
                    // Apply flip if Shift was held
                    double sweepAngle = ta.sweepAngle;
                    if (m_arcSlotFlipped) {
                        if (sweepAngle > 0) sweepAngle -= 360;
                        else sweepAngle += 360;
                    }

                    m_pendingEntity.points.clear();
                    m_pendingEntity.points.append(ta.center);
                    m_pendingEntity.points.append(QPointF(ta.center.x() + ta.radius, ta.center.y()));
                    m_pendingEntity.radius = ta.radius;
                    m_pendingEntity.startAngle = ta.startAngle;
                    m_pendingEntity.sweepAngle = sweepAngle;
                    valid = true;
                }
            }
            m_tangentTargets.clear();
        } else if (m_arcMode == ArcMode::ThreePoint && m_pendingEntity.points.size() >= 3) {
            // Calculate arc from 3 points: p1 (start) -> p2 (middle/through point) -> p3 (end)
            QPointF p1 = m_pendingEntity.points[0];  // start (first click)
            QPointF p2 = m_pendingEntity.points[1];  // through point (second click)
            QPointF p3 = m_pendingEntity.points[2];  // end (third click)

            // Use library function for circumcircle calculation
            auto arc = geometry::arcFromThreePoints(p1, p2, p3);
            if (arc.has_value()) {
                // Store final arc parameters: center + start/end endpoints
                double startRad = qDegreesToRadians(arc->startAngle);
                double endRad = qDegreesToRadians(arc->startAngle + arc->sweepAngle);
                m_pendingEntity.points.clear();
                m_pendingEntity.points.append(arc->center);
                m_pendingEntity.points.append(arc->center + QPointF(arc->radius * qCos(startRad), arc->radius * qSin(startRad)));
                m_pendingEntity.points.append(arc->center + QPointF(arc->radius * qCos(endRad), arc->radius * qSin(endRad)));
                m_pendingEntity.radius = arc->radius;
                m_pendingEntity.startAngle = arc->startAngle;
                m_pendingEntity.sweepAngle = arc->sweepAngle;
                valid = arc->radius > 0.1;
            }
        } else if (m_arcMode == ArcMode::CenterStartEnd && m_pendingEntity.points.size() >= 3) {
            // Center-Start-End arc: points[0] = center, points[1] = start, points[2] = end
            QPointF center = m_pendingEntity.points[0];
            QPointF start = m_pendingEntity.points[1];
            QPointF end = m_pendingEntity.points[2];

            // Determine sweep direction: default to shorter path, flip if m_arcSlotFlipped
            bool sweepCCW = true;
            double startAngle = std::atan2(start.y() - center.y(), start.x() - center.x()) * 180.0 / M_PI;
            double endAngle = std::atan2(end.y() - center.y(), end.x() - center.x()) * 180.0 / M_PI;
            double sweep = endAngle - startAngle;
            if (sweep > 180) sweep -= 360;
            if (sweep < -180) sweep += 360;
            // If sweep is positive, shorter path is CCW; if negative, shorter path is CW
            sweepCCW = (sweep > 0);
            // Flip reverses the direction
            if (m_arcSlotFlipped) {
                sweepCCW = !sweepCCW;
            }

            // Use library function
            auto arc = geometry::arcFromCenterAndEndpoints(center, start, end, sweepCCW);

            // Store final arc parameters: center + start/end endpoints
            {
                double startRad = qDegreesToRadians(arc.startAngle);
                double endRad = qDegreesToRadians(arc.startAngle + arc.sweepAngle);
                m_pendingEntity.points.clear();
                m_pendingEntity.points.append(arc.center);
                m_pendingEntity.points.append(arc.center + QPointF(arc.radius * qCos(startRad), arc.radius * qSin(startRad)));
                m_pendingEntity.points.append(arc.center + QPointF(arc.radius * qCos(endRad), arc.radius * qSin(endRad)));
                m_pendingEntity.radius = arc.radius;
                m_pendingEntity.startAngle = arc.startAngle;
                m_pendingEntity.sweepAngle = arc.sweepAngle;
                valid = arc.radius > 0.1;
            }
        } else if (m_arcMode == ArcMode::StartEndRadius && m_pendingEntity.points.size() >= 3) {
            // Start-End-Radius arc: points[0] = start, points[1] = end, points[2] = center point
            // Third click defines arc center (constrained to perpendicular bisector)
            QPointF start = m_pendingEntity.points[0];
            QPointF end = m_pendingEntity.points[1];
            QPointF centerPoint = m_pendingEntity.points[2];
            QPointF midChord = (start + end) / 2.0;
            double chordLength = QLineF(start, end).length();

            if (chordLength > 0.001) {
                // Calculate perpendicular direction from chord midpoint
                QPointF chordDir = (end - start) / chordLength;
                QPointF perpDir(-chordDir.y(), chordDir.x());

                // Project center point onto perpendicular bisector
                QPointF toCenter = centerPoint - midChord;
                double projDist = toCenter.x() * perpDir.x() + toCenter.y() * perpDir.y();

                // Ctrl snaps to midpoint (semicircle - exactly 180°)
                bool ctrlHeld = (QGuiApplication::queryKeyboardModifiers() & Qt::ControlModifier);
                if (ctrlHeld) {
                    projDist = 0.0;
                }

                // Apply flip (Shift key) - move center to opposite side of chord
                if (m_arcSlotFlipped) {
                    projDist = -projDist;
                }

                // Arc center is on the perpendicular bisector
                QPointF center = midChord + perpDir * projDist;

                // Calculate radius from center to endpoints
                double radius = QLineF(center, start).length();

                // Minimum radius to avoid degenerate arcs (skip if Ctrl for exact 180°)
                double halfChord = chordLength / 2.0;
                if (!ctrlHeld) {
                    double minRadius = halfChord * 1.01;
                    if (radius < minRadius) {
                        double minDist = std::sqrt(minRadius * minRadius - halfChord * halfChord);
                        double sign = (projDist >= 0) ? 1.0 : -1.0;
                        center = midChord + perpDir * sign * minDist;
                        radius = minRadius;
                        // Recalculate projDist after adjustment
                        projDist = sign * minDist;
                    }
                }

                // Match the preview's sweep calculation exactly.
                // The preview uses screen-space angles with Qt's drawArc.
                // We need to compute the same sweep and convert to world-space for the library.

                // Calculate angles in screen space (same as preview)
                QPoint centerScreen = worldToScreen(center);
                QPoint startScreen = worldToScreen(start);
                QPoint endScreen = worldToScreen(end);

                double startAngleScreen = std::atan2(startScreen.y() - centerScreen.y(),
                                                      startScreen.x() - centerScreen.x()) * 180.0 / M_PI;
                double endAngleScreen = std::atan2(endScreen.y() - centerScreen.y(),
                                                    endScreen.x() - centerScreen.x()) * 180.0 / M_PI;

                // Calculate sweep from start to end (same as preview)
                double sweep = endAngleScreen - startAngleScreen;
                // Normalize to [-180, 180] to get the "short" path
                while (sweep > 180) sweep -= 360;
                while (sweep < -180) sweep += 360;

                // Determine if we want the long arc based on center position
                // When center is close to chord (small |projDist|), we want the long arc
                bool wantLongArc = (std::abs(projDist) < halfChord);

                // If we want long arc, flip to the complementary sweep
                if (wantLongArc) {
                    if (sweep > 0) sweep -= 360;
                    else sweep += 360;
                }

                // Now convert screen sweep to world sweep direction.
                // Screen Y is inverted from world Y, so:
                // - Positive screen sweep (CCW on screen) = CW in world = negative world sweep
                // - Negative screen sweep (CW on screen) = CCW in world = positive world sweep
                // The library's sweepCCW=true means positive sweep in world coords.
                bool sweepCCW = (sweep < 0);  // negative screen sweep = CCW in world

                // Use library function to create the arc
                auto arc = geometry::arcFromCenterAndEndpoints(center, start, end, sweepCCW);

                // Store final arc parameters: center + start/end endpoints
                {
                    double startRad = qDegreesToRadians(arc.startAngle);
                    double endRad = qDegreesToRadians(arc.startAngle + arc.sweepAngle);
                    m_pendingEntity.points.clear();
                    m_pendingEntity.points.append(arc.center);
                    m_pendingEntity.points.append(arc.center + QPointF(arc.radius * qCos(startRad), arc.radius * qSin(startRad)));
                    m_pendingEntity.points.append(arc.center + QPointF(arc.radius * qCos(endRad), arc.radius * qSin(endRad)));
                    m_pendingEntity.radius = arc.radius;
                    m_pendingEntity.startAngle = arc.startAngle;
                    m_pendingEntity.sweepAngle = arc.sweepAngle;
                    valid = arc.radius > 0.1;
                }
            }
        } else {
            // Center-point arc (original behavior / tangent arc)
            valid = m_pendingEntity.radius > 0.1;
            if (valid && m_pendingEntity.points.size() == 1) {
                // Store center + start/end endpoints
                QPointF center = m_pendingEntity.points[0];
                double r = m_pendingEntity.radius;
                double startRad = qDegreesToRadians(m_pendingEntity.startAngle);
                double endRad = qDegreesToRadians(m_pendingEntity.startAngle + m_pendingEntity.sweepAngle);
                m_pendingEntity.points.append(center + QPointF(r * qCos(startRad), r * qSin(startRad)));
                m_pendingEntity.points.append(center + QPointF(r * qCos(endRad), r * qSin(endRad)));
            }
        }
        break;
    case SketchEntityType::Polygon:
        if (m_polygonMode == PolygonMode::Freeform) {
            // Freeform polygon: need at least 3 vertices (triangle)
            valid = m_pendingEntity.points.size() >= 3;
            if (valid) {
                m_pendingEntity.sides = m_pendingEntity.points.size();
            }
        } else {
            // Regular polygon: center + radius
            valid = m_pendingEntity.radius > 0.1;
            if (valid && m_pendingEntity.points.size() == 1) {
                QPointF center = m_pendingEntity.points[0];
                int sides = m_pendingEntity.sides > 0 ? m_pendingEntity.sides : 6;
                double radius = m_pendingEntity.radius;
                double angleStep = 2.0 * M_PI / sides;
                double startAngle = std::atan2(m_currentMouseWorld.y() - center.y(),
                                               m_currentMouseWorld.x() - center.x());

                // Circumscribed: vertex distance = apothem / cos(pi/sides)
                double vertexRadius = radius;
                if (m_polygonMode == PolygonMode::Circumscribed) {
                    vertexRadius = radius / std::cos(M_PI / sides);
                    startAngle += M_PI / sides;
                }

                // Store N vertex positions: points[0] = center, points[1..N] = vertices
                for (int i = 0; i < sides; ++i) {
                    double angle = startAngle + i * angleStep;
                    m_pendingEntity.points.append(QPointF(
                        center.x() + vertexRadius * std::cos(angle),
                        center.y() + vertexRadius * std::sin(angle)));
                }
            }
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
        // Save any remaining locked fields before clearing
        for (int i = 0; i < m_dimFields.size(); ++i) {
            if (i < m_dimStates.size() && m_dimStates[i].locked) {
                m_dimLockedForConstraints.append({m_dimFields[i].label, m_dimStates[i].lockedValue});
            }
        }

        // --- Decomposition path for compound entities (Rectangle, Parallelogram) ---
        sketch::UndoCommand compoundCmd;
        if (decomposeCompoundEntity(m_pendingEntity, m_dimLockedForConstraints, compoundCmd)) {
            // Decomposition succeeded: 4 lines + constraints + group already added
            // to m_entities, m_constraints, m_groups by the decompose function.
            m_profilesCacheDirty = true;
            pushUndoCommand(compoundCmd);

            // Emit signals for each created entity
            const SketchGroup& group = m_groups.last();
            for (int eid : group.entityIds) {
                emit entityCreated(eid);
            }

            // Clear locked dims (already consumed by decomposition)
            m_dimLockedForConstraints.clear();
        } else {
            // --- Normal (non-decomposable) entity path ---
            m_entities.append(m_pendingEntity);
            m_profilesCacheDirty = true;

            // Push undo command for entity creation
            pushUndoCommand(sketch::UndoCommand::addEntity(m_pendingEntity));

            emit entityCreated(m_pendingEntity.id);

            // Auto-create constraints from locked dimension values
            if (!m_dimLockedForConstraints.isEmpty()) {
                createLockedConstraints(m_pendingEntity.id);
            }
        }
    }

    m_isDrawing = false;
    m_previewPoints.clear();
    clearDimFields();
    update();
}

void SketchCanvas::cancelEntity()
{
    m_isDrawing = false;
    m_previewPoints.clear();
    m_tangentTargets.clear();  // Clear any tangent arc targets
    clearDimFields();
    update();
}

// ---- Compound entity decomposition (Fusion 360 style) -------------------

bool SketchCanvas::decomposeCompoundEntity(
        const SketchEntity& pendingEntity,
        const QVector<QPair<QString, double>>& lockedDims,
        sketch::UndoCommand& compoundCmd)
{
    const auto type = pendingEntity.type;
    if (type != SketchEntityType::Rectangle && type != SketchEntityType::Parallelogram
        && type != SketchEntityType::Polygon)
        return false;

    // Determine type name for the group
    QString typeName;
    if (type == SketchEntityType::Polygon)
        typeName = tr("Polygon");
    else if (type == SketchEntityType::Rectangle)
        typeName = tr("Rectangle");
    else
        typeName = tr("Parallelogram");

    // Determine freeform flag for polygons
    bool isFreeform = (type == SketchEntityType::Polygon)
                      && (m_polygonMode == PolygonMode::Freeform || pendingEntity.radius < 0.001);

    // Call library decomposition
    auto result = sketch::decomposeEntity(
        pendingEntity, lockedDims,
        [this]() { return nextEntityId(); },
        [this]() { return m_nextConstraintId++; },
        m_nextGroupId++, m_groups, typeName, isFreeform);

    if (!result.success) return false;

    // Insert entities into GUI state
    for (const auto& e : result.entities) {
        m_entities.append(SketchEntity(e));
    }

    // Insert constraints into GUI state
    for (const auto& c : result.constraints) {
        m_constraints.append(SketchConstraint(c));
    }

    // Insert group
    m_groups.append(result.group);

    // Set groupId on entities in m_entities
    for (auto& e : m_entities) {
        if (result.group.entityIds.contains(e.id))
            e.groupId = result.group.id;
    }

    // Build compound undo command
    QVector<sketch::UndoCommand> subs;
    for (const auto& e : result.entities)
        subs.append(sketch::UndoCommand::addEntity(e));
    for (const auto& c : result.constraints)
        subs.append(sketch::UndoCommand::addConstraint(c));
    subs.append(sketch::UndoCommand::addGroup(result.group));
    compoundCmd = sketch::UndoCommand::compound(subs, typeName);

    m_profilesCacheDirty = true;
    return true;
}

// ---- Inline dimension input helpers ------------------------------------

void SketchCanvas::initDimFields()
{
    // Save any locked fields from previous stage before reinitializing
    for (int i = 0; i < m_dimFields.size(); ++i) {
        if (i < m_dimStates.size() && m_dimStates[i].locked) {
            m_dimLockedForConstraints.append({m_dimFields[i].label, m_dimStates[i].lockedValue});
        }
    }

    m_dimFields.clear();
    m_dimStates.clear();
    m_dimActiveIndex = -1;

    int stage = m_previewPoints.size();  // Number of points placed so far

    if (m_activeTool == SketchTool::Line) {
        // After 1st click: Length + Angle
        if (stage >= 1) {
            m_dimFields.append({QStringLiteral("Length"), false, 0.0});
            m_dimFields.append({QStringLiteral("Angle"), true, 0.0});
        }
    } else if (m_activeTool == SketchTool::Rectangle) {
        if (m_rectMode == RectMode::Corner || m_rectMode == RectMode::Center) {
            if (stage >= 1) {
                m_dimFields.append({QStringLiteral("Width"), false, 0.0});
                m_dimFields.append({QStringLiteral("Height"), false, 0.0});
            }
        } else if (m_rectMode == RectMode::ThreePoint) {
            if (stage == 1) {
                m_dimFields.append({QStringLiteral("Edge Length"), false, 0.0});
                m_dimFields.append({QStringLiteral("Edge Angle"), true, 0.0});
            } else if (stage >= 2) {
                m_dimFields.append({QStringLiteral("Width"), false, 0.0});
            }
        } else if (m_rectMode == RectMode::Parallelogram) {
            if (stage == 1) {
                // Lengths first, then angles
                m_dimFields.append({QStringLiteral("Edge1"), false, 0.0});
                m_dimFields.append({QStringLiteral("Edge1 Angle"), true, 0.0});
            } else if (stage >= 2) {
                // Lengths first, then angles
                m_dimFields.append({QStringLiteral("Edge2"), false, 0.0});
                m_dimFields.append({QStringLiteral("Edge2 Angle"), true, 0.0});
            }
        }
    } else if (m_activeTool == SketchTool::Circle) {
        if (m_circleMode == CircleMode::CenterRadius && stage >= 1) {
            m_dimFields.append({QStringLiteral("Radius"), false, 0.0});
        } else if (m_circleMode == CircleMode::TwoPoint && stage >= 1) {
            m_dimFields.append({QStringLiteral("Diameter"), false, 0.0});
        }
    } else if (m_activeTool == SketchTool::Polygon) {
        // Radius field only for regular polygons (Inscribed/Circumscribed), not Freeform
        if (m_polygonMode != PolygonMode::Freeform && stage >= 1) {
            m_dimFields.append({QStringLiteral("Radius"), false, 0.0});
        }
    } else if (m_activeTool == SketchTool::Ellipse) {
        if (stage >= 1) {
            m_dimFields.append({QStringLiteral("Major Radius"), false, 0.0});
        }
    } else if (m_activeTool == SketchTool::Arc) {
        if (m_arcMode == ArcMode::CenterStartEnd) {
            if (stage == 1) {
                m_dimFields.append({QStringLiteral("Radius"), false, 0.0});
            } else if (stage >= 2) {
                m_dimFields.append({QStringLiteral("Sweep Angle"), true, 0.0});
            }
        } else if (m_arcMode == ArcMode::StartEndRadius) {
            if (stage == 1) {
                m_dimFields.append({QStringLiteral("Chord Length"), false, 0.0});
                m_dimFields.append({QStringLiteral("Chord Angle"), true, 0.0});
            } else if (stage >= 2) {
                m_dimFields.append({QStringLiteral("Sweep Angle"), true, 0.0});
            }
        }
    } else if (m_activeTool == SketchTool::Slot) {
        if (m_slotMode == SlotMode::CenterToCenter || m_slotMode == SlotMode::Overall) {
            if (stage >= 1) {
                m_dimFields.append({QStringLiteral("Length"), false, 0.0});
            }
        } else if (m_slotMode == SlotMode::ArcRadius) {
            if (stage == 1) {
                m_dimFields.append({QStringLiteral("Radius"), false, 0.0});
            } else if (stage >= 2) {
                m_dimFields.append({QStringLiteral("Sweep Angle"), true, 0.0});
            }
        } else if (m_slotMode == SlotMode::ArcEnds) {
            if (stage >= 2) {
                m_dimFields.append({QStringLiteral("Sweep Angle"), true, 0.0});
            }
        }
    }

    // Initialize states for all fields
    for (int i = 0; i < m_dimFields.size(); ++i) {
        m_dimStates.append({QString(), false, 0.0, 0, false});
    }

    if (!m_dimFields.isEmpty()) {
        m_dimActiveIndex = 0;
    }
}

void SketchCanvas::clearDimFields()
{
    m_dimFields.clear();
    m_dimStates.clear();
    m_dimActiveIndex = -1;
    m_dimLockedForConstraints.clear();
}

double SketchCanvas::getLockedDim(int fieldIndex) const
{
    if (fieldIndex >= 0 && fieldIndex < m_dimStates.size() && m_dimStates[fieldIndex].locked)
        return m_dimStates[fieldIndex].lockedValue;
    return -1.0;
}

void SketchCanvas::prefillDimField(int fieldIndex)
{
    if (fieldIndex < 0 || fieldIndex >= m_dimFields.size()) return;
    if (fieldIndex >= m_dimStates.size()) return;
    auto& field = m_dimFields[fieldIndex];
    auto& state = m_dimStates[fieldIndex];
    if (state.locked) return;

    if (field.isAngle) {
        state.inputBuffer = hobbycad::formatValue(field.currentValue);
    } else {
        state.inputBuffer = hobbycad::formatValue(hobbycad::mmToUnit(field.currentValue, m_displayUnit));
    }
    state.cursorPos = state.inputBuffer.length();
    state.selectAll = true;
}

void SketchCanvas::createLockedConstraints(int entityId)
{
    const SketchEntity* entity = entityById(entityId);
    if (!entity) return;

    for (const auto& [label, value] : m_dimLockedForConstraints) {
        m_constraintTargetEntities.clear();
        m_constraintTargetPoints.clear();

        ConstraintType ctype;
        if (label == QStringLiteral("Radius") || label == QStringLiteral("Major Radius")) {
            ctype = ConstraintType::Radius;
            m_constraintTargetEntities.append(entityId);
        } else if (label == QStringLiteral("Diameter")) {
            ctype = ConstraintType::Diameter;
            m_constraintTargetEntities.append(entityId);
        } else if (label.contains(QStringLiteral("Angle"))) {
            ctype = ConstraintType::Angle;
            // Angle between two lines requires 2 entities — skip auto-creation
            // for single-entity angle locks (e.g., line angle from horizontal).
            // TODO: Support angle-from-horizontal as a solver constraint.
            continue;
        } else {
            ctype = ConstraintType::Distance;

            // Distance: use start/end points
            if (entity->points.size() >= 2) {
                m_constraintTargetEntities.append(entityId);
                m_constraintTargetEntities.append(entityId);
                m_constraintTargetPoints.append(entity->points[0]);
                m_constraintTargetPoints.append(entity->points[1]);
            } else {
                continue;  // Cannot form a valid Distance constraint
            }
        }

        // Position label near entity center
        QPointF labelPos;
        if (!entity->points.isEmpty()) {
            if (entity->points.size() >= 2) {
                labelPos = (entity->points[0] + entity->points[1]) / 2.0 + QPointF(0, -10);
            } else {
                labelPos = entity->points[0] + QPointF(15, -15);
            }
        }

        // Skip the over-constrained check: the user explicitly locked
        // this dimension by typing a value and pressing Enter, so we
        // honour their intent and create the constraint directly.
        createConstraint(ctype, value, labelPos, /*skipOverConstrainCheck=*/true);
    }
}

void SketchCanvas::drawDimInputField(QPainter& painter, const QPointF& position,
                                      int fieldIndex, double rotation)
{
    if (fieldIndex < 0 || fieldIndex >= m_dimFields.size()) return;

    const auto& field = m_dimFields[fieldIndex];
    const auto& state = m_dimStates[fieldIndex];
    bool isActive = (fieldIndex == m_dimActiveIndex);

    painter.save();

    QFont font = painter.font();
    font.setPointSize(9);
    painter.setFont(font);
    QFontMetricsF fm(font);

    // Determine display text and colors
    QString displayText;
    QColor bgColor;
    QColor textColor;
    QColor borderColor = Qt::transparent;

    if (state.locked) {
        // Locked: green, show formatted value with checkmark
        if (field.isAngle) {
            displayText = formatAngle(state.lockedValue) + QStringLiteral(" \u2713");
        } else {
            displayText = formatValueWithUnit(state.lockedValue, m_displayUnit) + QStringLiteral(" \u2713");
        }
        bgColor = QColor(200, 255, 200);
        textColor = QColor(30, 100, 30);
        borderColor = QColor(80, 180, 80);
    } else if (isActive && !state.inputBuffer.isEmpty()) {
        if (state.selectAll) {
            // Active + selected: show LIVE currentValue with selection highlight
            // Blue highlight + white text = standard "text selected, type to replace"
            if (field.isAngle) {
                displayText = hobbycad::formatValue(field.currentValue);
            } else {
                displayText = hobbycad::formatValue(hobbycad::mmToUnit(field.currentValue, m_displayUnit));
            }
            bgColor = QColor(51, 153, 255);        // Selection blue
            textColor = Qt::white;
            borderColor = QColor(30, 100, 200);
        } else {
            // Active + typing with cursor: yellow, insert │ at cursorPos
            int safePos = qBound(0, state.cursorPos, state.inputBuffer.length());
            displayText = state.inputBuffer;
            displayText.insert(safePos, QStringLiteral("\u2502"));  // │ cursor
            bgColor = QColor(255, 235, 160);
            textColor = Qt::black;
            borderColor = QColor(210, 170, 50);
        }
    } else if (isActive) {
        // Active + empty: live measurement, not yet editing
        if (field.isAngle) {
            displayText = formatAngle(field.currentValue);
        } else {
            displayText = formatValueWithUnit(field.currentValue, m_displayUnit);
        }
        bgColor = Qt::white;
        textColor = Qt::black;
        borderColor = QColor(100, 100, 100);
    } else {
        // Inactive: black text on white background
        if (field.isAngle) {
            displayText = formatAngle(field.currentValue);
        } else {
            displayText = formatValueWithUnit(field.currentValue, m_displayUnit);
        }
        bgColor = Qt::white;
        textColor = Qt::black;
    }

    // Add field label prefix for multi-field tools
    if (m_dimFields.size() > 1) {
        displayText = field.label + QStringLiteral(": ") + displayText;
    }

    QRectF textBounds = fm.boundingRect(displayText);

    // Apply rotation if provided (for dimension labels along edges)
    painter.translate(position);
    if (qAbs(rotation) > 0.01) {
        painter.rotate(rotation);
    }

    // Draw background
    QRectF labelRect(-textBounds.width() / 2.0 - 5,
                     -textBounds.height() / 2.0 - 2,
                     textBounds.width() + 10,
                     textBounds.height() + 4);
    painter.fillRect(labelRect, bgColor);

    // Draw border for active/locked fields (NoBrush so drawRect doesn't fill over the background)
    if (borderColor != Qt::transparent) {
        painter.setPen(QPen(borderColor, 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(labelRect);
    }

    // Draw text
    painter.setPen(textColor);
    painter.drawText(labelRect, Qt::AlignCenter, displayText);

    painter.restore();
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
    // Handle rectangle - find closest edge to tangent point
    else if (tangentEntity.type == SketchEntityType::Rectangle && tangentEntity.points.size() >= 2) {
        // Get rectangle corners
        QPointF p1 = tangentEntity.points[0];
        QPointF p2 = tangentEntity.points[1];

        // Calculate all 4 corners
        QPointF corners[4];
        if (tangentEntity.points.size() >= 4) {
            // Rotated rectangle with 4 corners stored
            for (int i = 0; i < 4; ++i) {
                corners[i] = tangentEntity.points[i];
            }
        } else {
            // Axis-aligned rectangle: 2 opposite corners
            corners[0] = p1;
            corners[1] = QPointF(p2.x(), p1.y());
            corners[2] = p2;
            corners[3] = QPointF(p1.x(), p2.y());
        }

        // Find which edge is closest to the tangent point
        double minDist = std::numeric_limits<double>::max();
        QPointF closestEdgeStart, closestEdgeEnd;

        for (int i = 0; i < 4; ++i) {
            QPointF edgeStart = corners[i];
            QPointF edgeEnd = corners[(i + 1) % 4];

            // Calculate distance from tangent point to this edge
            double dist = geometry::pointToLineDistance(tangentPoint, edgeStart, edgeEnd);
            if (dist < minDist) {
                minDist = dist;
                closestEdgeStart = edgeStart;
                closestEdgeEnd = edgeEnd;
            }
        }

        // Use the closest edge as the tangent line
        auto libResult = geometry::arcTangentToLine(
            closestEdgeStart, closestEdgeEnd,
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

void SketchCanvas::createConstraint(ConstraintType type, double value, const QPointF& labelPos,
                                     bool skipOverConstrainCheck)
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
    // (skipped for auto-created constraints from locked dimension fields,
    //  since the user explicitly typed a value and pressed Enter)
    if (!skipOverConstrainCheck && SketchSolver::isAvailable()) {
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

    // NOTE: Cannot directly delegate to sketch::suggestConstraintType() because
    // hobbycad::ConstraintType (project.h) and sketch::ConstraintType (constraint.h)
    // are separate enums with different value sets (library has Concentric etc.).
    // This function is specifically for dimension type detection, not general
    // constraint suggestion.

    // Point to point → Distance
    if (e1->type == SketchEntityType::Point && e2->type == SketchEntityType::Point)
        return ConstraintType::Distance;

    // Point to line → Distance
    if ((e1->type == SketchEntityType::Point && e2->type == SketchEntityType::Line) ||
        (e1->type == SketchEntityType::Line && e2->type == SketchEntityType::Point))
        return ConstraintType::Distance;

    // Line to line → Angle
    if (e1->type == SketchEntityType::Line && e2->type == SketchEntityType::Line)
        return ConstraintType::Angle;

    // Circle or Arc → Radius
    if (e1->type == SketchEntityType::Circle || e1->type == SketchEntityType::Arc ||
        e2->type == SketchEntityType::Circle || e2->type == SketchEntityType::Arc)
        return ConstraintType::Radius;

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
    // Hit-test against the actual rendered text position, not just the raw
    // labelPosition.  For distance constraints the text is drawn at the
    // midpoint of the dimension line (which may differ from labelPosition
    // for non-horizontal entities).
    const QPointF screenPos = worldToScreen(worldPos).toPointF();
    const double tolerance = 14.0;  // pixels – generous click target

    for (const SketchConstraint& c : m_constraints) {
        if (!c.enabled || !c.labelVisible) continue;

        QPointF textCenter;  // in screen coordinates

        if (c.type == ConstraintType::Distance) {
            // Replicate the position logic from drawDistanceConstraint()
            QPointF p1, p2;
            if (!getConstraintEndpoints(c, p1, p2)) continue;

            QPointF sp1 = worldToScreen(p1).toPointF();
            QPointF sp2 = worldToScreen(p2).toPointF();
            QPointF labelCenterPt = worldToScreen(c.labelPosition).toPointF();

            QPointF along = sp2 - sp1;
            double len = std::sqrt(along.x() * along.x() + along.y() * along.y());
            if (len < 1.0) continue;

            QPointF dir = along / len;
            QPointF perp(-dir.y(), dir.x());

            QPointF labelDelta = labelCenterPt - sp1;
            double offset = labelDelta.x() * perp.x() + labelDelta.y() * perp.y();

            QPointF d1 = sp1 + perp * offset;
            QPointF d2 = sp2 + perp * offset;

            // Check if text is in compact (outside) mode — same logic as draw
            QString text = formatValueWithUnit(c.value, m_displayUnit);
            if (!c.isDriving) text = QStringLiteral("(") + text + QStringLiteral(")");
            QFontMetricsF fm(font());
            double textWidth = fm.horizontalAdvance(text);
            double halfText = textWidth / 2.0 + 3.0;
            bool textFits = (halfText * 2.0 < len);

            if (textFits) {
                textCenter = (d1 + d2) / 2.0;
            } else {
                // Compact mode: text is outside past d2
                double leaderLen = 12.0;
                double textGap = 4.0;
                QPointF leaderEnd = d2 + dir * leaderLen;
                textCenter = leaderEnd + dir * (textWidth / 2.0 + textGap);
            }
        } else {
            // For Radius/Diameter/Angle and geometric constraints,
            // the text is drawn at or near labelPosition.
            textCenter = worldToScreen(c.labelPosition).toPointF();
        }

        // Check distance from click to rendered text centre
        double dist = QLineF(textCenter, screenPos).length();
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

        // --- Use solver with temporary pin ---
        // Pin endpoints so the solver adjusts geometry from a fixed
        // anchor instead of sliding everything symmetrically.
        //  - Distance on a line: pin first endpoint
        //  - Angle between two lines: pin both endpoints of the first
        //    line so only the second line rotates
        int pinsAdded = 0;
        if (constraint->type == ConstraintType::Distance
            && !constraint->entityIds.isEmpty()) {
            const SketchEntity* entity = entityById(constraint->entityIds[0]);
            if (entity && entity->type == SketchEntityType::Line
                && entity->points.size() == 2) {
                SketchConstraint pin;
                pin.id = -999;
                pin.type = ConstraintType::FixedPoint;
                pin.entityIds.append(entity->id);
                pin.pointIndices.append(0);
                pin.isDriving = true;
                pin.enabled = true;
                pin.satisfied = true;
                pin.labelVisible = false;
                m_constraints.append(pin);
                pinsAdded = 1;
            }
        } else if (constraint->type == ConstraintType::Angle
                   && !constraint->entityIds.isEmpty()) {
            // Pin both endpoints of the first line so only the second
            // line rotates to satisfy the new angle.
            const SketchEntity* e1 = entityById(constraint->entityIds[0]);
            if (e1 && e1->type == SketchEntityType::Line
                && e1->points.size() == 2) {
                for (int pi = 0; pi < 2; ++pi) {
                    SketchConstraint pin;
                    pin.id = -999 - pi;  // -999, -1000
                    pin.type = ConstraintType::FixedPoint;
                    pin.entityIds.append(e1->id);
                    pin.pointIndices.append(pi);
                    pin.isDriving = true;
                    pin.enabled = true;
                    pin.satisfied = true;
                    pin.labelVisible = false;
                    m_constraints.append(pin);
                }
                pinsAdded = 2;
            }
        }

        solveConstraints();

        // Remove temporary pin(s)
        if (pinsAdded > 0) {
            m_constraints.erase(
                std::remove_if(m_constraints.begin(), m_constraints.end(),
                               [](const SketchConstraint& c) { return c.id <= -999; }),
                m_constraints.end());
            // constraint pointer may be stale after erase — re-find if needed
        }

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

        // Recompute dimension label positions so they track the geometry
        // after the solver has moved entity points.
        updateConstraintLabelPositions();

        update();
    } else {
        // Mark failed constraints visually (drawn in red) — no modal dialog.
        // The user can see which constraints are unsatisfied from the colour,
        // and can edit or delete them.
        for (SketchConstraint& c : m_constraints) {
            c.satisfied = !result.failedConstraintIds.contains(c.id);
        }

        qWarning("Solver: %s (DOF %d, %d failed constraint(s))",
                 qPrintable(result.errorMessage), result.dof,
                 static_cast<int>(result.failedConstraintIds.size()));

        update();
    }
}

void SketchCanvas::refreshConstrainedFlags()
{
    // Delegate to library — computes which entities have driving constraints
    QSet<int> ids = sketch::getConstrainedEntityIds(
        toLibraryConstraints(m_constraints));
    for (SketchEntity& e : m_entities) {
        e.constrained = ids.contains(e.id);
    }
}

void SketchCanvas::updateDrivenDimensions()
{
    // Delegate computation to library
    QVector<sketch::Entity> libEntities = toLibraryEntities(m_entities);
    for (SketchConstraint& c : m_constraints) {
        if (c.isDriving) continue;
        c.value = sketch::computeDrivenValue(c, libEntities);
        c.satisfied = true;
    }
}

void SketchCanvas::updateConstraintLabelPositions()
{
    for (SketchConstraint& c : m_constraints) {
        if (!c.labelVisible) continue;

        if (c.type == ConstraintType::Distance) {
            // Recompute label at the midpoint of the two constrained
            // points, offset slightly perpendicular to the line.
            QPointF p1, p2;
            if (getConstraintEndpoints(c, p1, p2)) {
                QPointF mid = (p1 + p2) / 2.0;
                QPointF dir = p2 - p1;
                double len = std::sqrt(dir.x() * dir.x() + dir.y() * dir.y());
                if (len > 1e-9) {
                    // Perpendicular offset (10 world units)
                    QPointF perp(-dir.y() / len, dir.x() / len);
                    c.labelPosition = mid + perp * 10.0;
                } else {
                    c.labelPosition = mid + QPointF(0, -10);
                }
            }
        } else if (c.type == ConstraintType::Radius ||
                   c.type == ConstraintType::Diameter) {
            // Recompute label at midpoint between center and edge
            if (!c.entityIds.isEmpty()) {
                const SketchEntity* ent = entityById(c.entityIds[0]);
                if (ent && !ent->points.isEmpty()) {
                    QPointF center = ent->points[0];
                    if (ent->points.size() > 1) {
                        c.labelPosition = (center + ent->points[1]) / 2.0;
                    } else {
                        c.labelPosition = center + QPointF(ent->radius / 2.0, 0);
                    }
                }
            }
        }
        // Geometric constraints (coincident, perpendicular, etc.) don't
        // have visible labels that need repositioning.
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
        // Helper: resolve a point index on an entity.
        // For Rectangle entities, indices 2 and 3 are virtual corners:
        //   0 = points[0] = (x1,y1)   1 = points[1] = (x2,y2)
        //   2 = (x1,y2)               3 = (x2,y1)
        auto resolvePoint = [](const SketchEntity* e, int idx) -> QPointF {
            if (idx >= 0 && idx < e->points.size())
                return e->points[idx];
            return e->points.isEmpty() ? QPointF() : e->points[qMin(idx, e->points.size() - 1)];
        };

        // For distance constraints, use the closest points or centers
        if (e1->type == SketchEntityType::Point) {
            p1 = e1->points[0];
        } else if (e1->type == SketchEntityType::Circle || e1->type == SketchEntityType::Arc) {
            p1 = e1->points[0];  // center
        } else if (!e1->points.isEmpty()) {
            int idx1 = (constraint.pointIndices.size() > 0) ? constraint.pointIndices[0] : 0;
            p1 = resolvePoint(e1, idx1);
        } else {
            return false;
        }

        if (e2->type == SketchEntityType::Point) {
            p2 = e2->points[0];
        } else if (e2->type == SketchEntityType::Circle || e2->type == SketchEntityType::Arc) {
            p2 = e2->points[0];  // center
        } else if (!e2->points.isEmpty()) {
            int idx2 = (constraint.pointIndices.size() > 1) ? constraint.pointIndices[1] : 0;
            p2 = resolvePoint(e2, idx2);
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

// ---------------------------------------------------------------------------
//  Smart split — only at the two intersections bracketing the click point
// ---------------------------------------------------------------------------
QVector<int> SketchCanvas::splitEntityNearClick(int entityId, const QPointF& clickPoint)
{
    QVector<int> newIds;

    SketchEntity* entity = entityById(entityId);
    if (!entity) return newIds;

    // Currently only lines are supported
    if (entity->type != SketchEntityType::Line || entity->points.size() < 2)
        return newIds;

    const QPointF& p0 = entity->points[0];
    const QPointF& p1 = entity->points[1];

    // Gather all intersections involving this entity
    QVector<sketch::Entity> libEntities = hobbycad::toLibraryEntities(m_entities);
    QVector<sketch::Intersection> allIntersections = sketch::findAllIntersections(libEntities);

    // Compute parameter (0-1) along the line for each intersection point
    QVector<QPair<double, QPointF>> paramPts;   // (t, point)
    for (const auto& inter : allIntersections) {
        if (inter.entityId1 != entityId && inter.entityId2 != entityId)
            continue;
        double t = geometry::projectPointOnLine(inter.point, p0, p1);
        if (t > 0.001 && t < 0.999)
            paramPts.append({t, inter.point});
    }

    if (paramPts.isEmpty()) return newIds;

    // Parameter of the click position on the line
    double clickT = geometry::projectPointOnLine(clickPoint, p0, p1);

    // Find the nearest intersection before and after the click
    double bestBefore = -1.0;
    QPointF ptBefore;
    double bestAfter = 2.0;
    QPointF ptAfter;

    for (const auto& [t, pt] : paramPts) {
        if (t <= clickT && t > bestBefore) {
            bestBefore = t;
            ptBefore = pt;
        }
        if (t >= clickT && t < bestAfter) {
            bestAfter = t;
            ptAfter = pt;
        }
    }

    // Build the filtered list of split points
    QVector<QPointF> splitPoints;
    if (bestBefore >= 0.0)
        splitPoints.append(ptBefore);
    if (bestAfter <= 1.0 && qAbs(bestAfter - bestBefore) > 0.001)
        splitPoints.append(ptAfter);

    if (splitPoints.isEmpty()) return newIds;

    // Use the library's multi-point split with only the bracketing points
    sketch::Entity libEntity = hobbycad::toLibraryEntity(*entity);
    sketch::SplitResult result = sketch::splitEntityAtIntersections(
        libEntity, splitPoints,
        [this]() { return m_nextId++; });

    if (!result.success) return newIds;

    // Remove original entity
    m_entities.erase(std::remove_if(m_entities.begin(), m_entities.end(),
                     [entityId](const SketchEntity& e) { return e.id == entityId; }),
                     m_entities.end());

    // Add new segments
    for (const sketch::Entity& ne : result.newEntities) {
        SketchEntity guiEntity = hobbycad::toGuiEntity(ne);
        m_entities.append(guiEntity);
        newIds.append(guiEntity.id);
        emit entityCreated(guiEntity.id);
    }

    m_profilesCacheDirty = true;
    update();
    return newIds;
}

// ---------------------------------------------------------------------------
//  Rejoin collinear segments back into a single line
// ---------------------------------------------------------------------------
int SketchCanvas::rejoinCollinearSegments()
{
    if (m_selectedIds.size() < 2) return -1;

    // --- Validate: all selected must be lines ---
    QVector<SketchEntity*> lines;
    for (int id : m_selectedIds) {
        SketchEntity* e = entityById(id);
        if (!e || e->type != SketchEntityType::Line || e->points.size() < 2) {
            QMessageBox::warning(this, tr("Rejoin"),
                tr("All selected entities must be line segments."));
            return -1;
        }
        lines.append(e);
    }

    // --- Validate: all collinear (same direction, within tolerance) ---
    // Use the direction of the first line as reference
    QPointF refDir = lines[0]->points[1] - lines[0]->points[0];
    double refLen = std::sqrt(refDir.x() * refDir.x() + refDir.y() * refDir.y());
    if (refLen < 1e-9) {
        QMessageBox::warning(this, tr("Rejoin"),
            tr("Selected line has zero length."));
        return -1;
    }
    refDir /= refLen;

    const double angleTol = 0.001;  // radians (~0.06 degrees)
    for (int i = 1; i < lines.size(); ++i) {
        QPointF d = lines[i]->points[1] - lines[i]->points[0];
        double len = std::sqrt(d.x() * d.x() + d.y() * d.y());
        if (len < 1e-9) continue;
        d /= len;
        // Cross product magnitude gives sin(angle)
        double cross = std::abs(refDir.x() * d.y() - refDir.y() * d.x());
        if (cross > angleTol) {
            QMessageBox::warning(this, tr("Rejoin"),
                tr("Selected lines are not collinear."));
            return -1;
        }
    }

    // --- Validate: segments form a contiguous chain ---
    // Project all endpoints onto the reference line to get parameters
    QPointF refP0 = lines[0]->points[0];
    struct Seg {
        int id;
        double t0, t1;
        QPointF p0, p1;
    };
    QVector<Seg> segs;
    for (auto* l : lines) {
        QPointF d0 = l->points[0] - refP0;
        QPointF d1 = l->points[1] - refP0;
        double t0 = d0.x() * refDir.x() + d0.y() * refDir.y();
        double t1 = d1.x() * refDir.x() + d1.y() * refDir.y();
        // Normalise so t0 < t1
        if (t0 > t1) {
            std::swap(t0, t1);
            segs.append({l->id, t0, t1, l->points[1], l->points[0]});
        } else {
            segs.append({l->id, t0, t1, l->points[0], l->points[1]});
        }
    }

    // Sort by parameter
    std::sort(segs.begin(), segs.end(),
              [](const Seg& a, const Seg& b) { return a.t0 < b.t0; });

    // Check contiguity: each segment's start must match previous segment's end
    const double endpointTol = 1e-4;
    for (int i = 1; i < segs.size(); ++i) {
        if (std::abs(segs[i].t0 - segs[i - 1].t1) > endpointTol) {
            QMessageBox::warning(this, tr("Rejoin"),
                tr("Selected lines do not form a contiguous chain.\n"
                   "There is a gap between segments."));
            return -1;
        }
    }

    // --- Validate: no other entities attach at interior junction points ---
    // Collect the interior junction points (between consecutive segments)
    QVector<QPointF> junctions;
    for (int i = 0; i < segs.size() - 1; ++i) {
        junctions.append(segs[i].p1);
    }

    for (const QPointF& jp : junctions) {
        for (const auto& e : m_entities) {
            if (m_selectedIds.contains(e.id)) continue;  // skip selected
            for (const QPointF& ep : e.points) {
                double dx = ep.x() - jp.x();
                double dy = ep.y() - jp.y();
                if (dx * dx + dy * dy < endpointTol * endpointTol) {
                    QMessageBox::warning(this, tr("Rejoin"),
                        tr("Another entity is attached at an interior junction point.\n"
                           "Cannot rejoin without breaking connectivity."));
                    return -1;
                }
            }
        }
    }

    // --- Perform the rejoin ---
    QPointF mergedP0 = segs.first().p0;
    QPointF mergedP1 = segs.last().p1;
    bool isConstruction = lines[0]->isConstruction;

    // Remove all the old segments
    QSet<int> removeIds = m_selectedIds;
    m_entities.erase(
        std::remove_if(m_entities.begin(), m_entities.end(),
                       [&removeIds](const SketchEntity& e) {
                           return removeIds.contains(e.id);
                       }),
        m_entities.end());

    // Remove coincident constraints between the old segments
    m_constraints.erase(
        std::remove_if(m_constraints.begin(), m_constraints.end(),
                       [&removeIds](const SketchConstraint& c) {
                           if (c.type != ConstraintType::Coincident) return false;
                           // Remove if ALL referenced entities were in the selection
                           for (int eid : c.entityIds) {
                               if (!removeIds.contains(eid)) return false;
                           }
                           return true;
                       }),
        m_constraints.end());

    // Create the merged line
    int newId = m_nextId++;
    SketchEntity merged;
    merged.id = newId;
    merged.type = SketchEntityType::Line;
    merged.points = {mergedP0, mergedP1};
    merged.isConstruction = isConstruction;
    m_entities.append(merged);

    // Update selection
    clearSelection();
    selectEntity(newId);

    m_profilesCacheDirty = true;
    refreshConstrainedFlags();
    update();
    emit entityCreated(newId);
    return newId;
}
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
        setCursor(Qt::ArrowCursor);
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

void SketchCanvas::pushUndoCommand(const sketch::UndoCommand& cmd)
{
    m_libUndoStack.push(cmd);
    updateUndoRedoState();
}

void SketchCanvas::updateUndoRedoState()
{
    emit undoAvailabilityChanged(m_libUndoStack.canUndo());
    emit redoAvailabilityChanged(m_libUndoStack.canRedo());
}

void SketchCanvas::undoSingleCommand(const sketch::UndoCommand& cmd)
{
    switch (cmd.type) {
    case sketch::CommandType::AddEntity:
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

    case sketch::CommandType::DeleteEntity:
        // Undo delete = restore the entity
        m_entities.append(SketchEntity(cmd.entity));
        m_profilesCacheDirty = true;
        break;

    case sketch::CommandType::ModifyEntity:
        // Undo modify = restore previous state
        for (int i = 0; i < m_entities.size(); ++i) {
            if (m_entities[i].id == cmd.entity.id) {
                m_entities[i] = SketchEntity(cmd.previousEntity);
                break;
            }
        }
        m_profilesCacheDirty = true;
        break;

    case sketch::CommandType::AddConstraint:
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

    case sketch::CommandType::DeleteConstraint:
        // Undo delete = restore the constraint
        m_constraints.append(SketchConstraint(cmd.constraint));
        break;

    case sketch::CommandType::ModifyConstraint:
        // Undo modify = restore previous state
        for (int i = 0; i < m_constraints.size(); ++i) {
            if (m_constraints[i].id == cmd.constraint.id) {
                m_constraints[i] = SketchConstraint(cmd.previousConstraint);
                break;
            }
        }
        break;

    case sketch::CommandType::AddGroup:
        // Undo add = remove the group
        m_groups.erase(
            std::remove_if(m_groups.begin(), m_groups.end(),
                           [&cmd](const SketchGroup& g) { return g.id == cmd.group.id; }),
            m_groups.end());
        break;

    case sketch::CommandType::DeleteGroup:
        // Undo delete = restore the group
        m_groups.append(cmd.group);
        break;

    case sketch::CommandType::ModifyGroup:
        // Undo modify = restore previous group state
        for (int i = 0; i < m_groups.size(); ++i) {
            if (m_groups[i].id == cmd.group.id) {
                m_groups[i] = cmd.previousGroup;
                break;
            }
        }
        break;

    case sketch::CommandType::Compound:
        // Undo compound = undo sub-commands in reverse order
        for (int i = cmd.subCommands.size() - 1; i >= 0; --i) {
            undoSingleCommand(cmd.subCommands[i]);
        }
        break;
    }
}

void SketchCanvas::redoSingleCommand(const sketch::UndoCommand& cmd)
{
    switch (cmd.type) {
    case sketch::CommandType::AddEntity:
        // Redo add = add the entity back
        m_entities.append(SketchEntity(cmd.entity));
        m_profilesCacheDirty = true;
        break;

    case sketch::CommandType::DeleteEntity:
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

    case sketch::CommandType::ModifyEntity:
        // Redo modify = apply the modification again
        for (int i = 0; i < m_entities.size(); ++i) {
            if (m_entities[i].id == cmd.entity.id) {
                m_entities[i] = SketchEntity(cmd.entity);
                break;
            }
        }
        m_profilesCacheDirty = true;
        break;

    case sketch::CommandType::AddConstraint:
        // Redo add = add the constraint back
        m_constraints.append(SketchConstraint(cmd.constraint));
        break;

    case sketch::CommandType::DeleteConstraint:
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

    case sketch::CommandType::ModifyConstraint:
        // Redo modify = apply the modification again
        for (int i = 0; i < m_constraints.size(); ++i) {
            if (m_constraints[i].id == cmd.constraint.id) {
                m_constraints[i] = SketchConstraint(cmd.constraint);
                break;
            }
        }
        break;

    case sketch::CommandType::AddGroup:
        // Redo add = add the group back
        m_groups.append(cmd.group);
        break;

    case sketch::CommandType::DeleteGroup:
        // Redo delete = remove the group again
        m_groups.erase(
            std::remove_if(m_groups.begin(), m_groups.end(),
                           [&cmd](const SketchGroup& g) { return g.id == cmd.group.id; }),
            m_groups.end());
        break;

    case sketch::CommandType::ModifyGroup:
        // Redo modify = apply the modification again
        for (int i = 0; i < m_groups.size(); ++i) {
            if (m_groups[i].id == cmd.group.id) {
                m_groups[i] = cmd.group;
                break;
            }
        }
        break;

    case sketch::CommandType::Compound:
        // Redo compound = redo sub-commands in forward order
        for (const auto& sub : cmd.subCommands) {
            redoSingleCommand(sub);
        }
        break;
    }
}

void SketchCanvas::undo()
{
    if (!m_libUndoStack.canUndo()) return;

    sketch::UndoCommand cmd = m_libUndoStack.undo();
    undoSingleCommand(cmd);

    updateUndoRedoState();
    update();
}

void SketchCanvas::redo()
{
    if (!m_libUndoStack.canRedo()) return;

    sketch::UndoCommand cmd = m_libUndoStack.redo();
    redoSingleCommand(cmd);

    updateUndoRedoState();
    update();
}

}  // namespace hobbycad
