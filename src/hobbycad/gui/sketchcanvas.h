// =====================================================================
//  src/hobbycad/gui/sketchcanvas.h — 2D Sketch canvas widget
// =====================================================================
//
//  A 2D drawing canvas for creating and editing sketches. This widget
//  does not require OpenGL and can be used in reduced mode.
//
//  Supports:
//  - Pan and zoom with mouse/keyboard
//  - Grid display with snap
//  - Drawing lines, rectangles, circles, arcs, splines
//  - Selection and editing of entities
//  - Constraints visualization
//  - Dimensions display
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_SKETCHCANVAS_H
#define HOBBYCAD_SKETCHCANVAS_H

#include "sketchtoolbar.h"

#include <QWidget>
#include <QPointF>
#include <QVector>

namespace hobbycad {

/// Types of sketch entities
enum class SketchEntityType {
    Point,
    Line,
    Rectangle,
    Circle,
    Arc,
    Spline,
    Text,
    Dimension
};

/// A single sketch entity
struct SketchEntity {
    int id = 0;
    SketchEntityType type = SketchEntityType::Line;
    QVector<QPointF> points;      ///< Control points
    double radius = 0.0;          ///< For circles/arcs
    double startAngle = 0.0;      ///< For arcs (degrees)
    double sweepAngle = 360.0;    ///< For arcs (degrees)
    QString text;                 ///< For text entities
    bool selected = false;
    bool constrained = false;     ///< Has constraints applied
};

/// Sketch plane orientation
enum class SketchPlane {
    XY,
    XZ,
    YZ,
    Custom
};

class SketchCanvas : public QWidget {
    Q_OBJECT

public:
    explicit SketchCanvas(QWidget* parent = nullptr);

    /// Set the active drawing tool
    void setActiveTool(SketchTool tool);
    SketchTool activeTool() const { return m_activeTool; }

    /// Set the sketch plane
    void setSketchPlane(SketchPlane plane);
    SketchPlane sketchPlane() const { return m_plane; }

    /// Grid settings
    void setGridVisible(bool visible);
    bool isGridVisible() const { return m_showGrid; }
    void setGridSpacing(double spacing);
    double gridSpacing() const { return m_gridSpacing; }
    void setSnapToGrid(bool snap);
    bool snapToGrid() const { return m_snapToGrid; }

    /// Get all entities
    const QVector<SketchEntity>& entities() const { return m_entities; }

    /// Get selected entity (or nullptr if none)
    SketchEntity* selectedEntity();
    const SketchEntity* selectedEntity() const;

    /// Clear all entities
    void clear();

    /// Reset view to fit all entities
    void resetView();

    /// Zoom to fit
    void zoomToFit();

signals:
    /// Emitted when an entity is selected or deselected
    void selectionChanged(int entityId);

    /// Emitted when an entity is created
    void entityCreated(int entityId);

    /// Emitted when an entity is modified
    void entityModified(int entityId);

    /// Emitted when the mouse position changes (for status bar)
    void mousePositionChanged(const QPointF& pos);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    // Coordinate transforms
    QPointF screenToWorld(const QPoint& screen) const;
    QPoint worldToScreen(const QPointF& world) const;
    QPointF snapPoint(const QPointF& world) const;

    // Drawing helpers
    void drawGrid(QPainter& painter);
    void drawAxes(QPainter& painter);
    void drawEntity(QPainter& painter, const SketchEntity& entity);
    void drawPreview(QPainter& painter);
    void drawSelectionHandles(QPainter& painter, const SketchEntity& entity);

    // Hit testing
    int hitTest(const QPointF& worldPos) const;
    bool hitTestEntity(const SketchEntity& entity, const QPointF& worldPos) const;

    // Entity creation
    void startEntity(const QPointF& pos);
    void updateEntity(const QPointF& pos);
    void finishEntity();
    void cancelEntity();
    int nextEntityId();

    // View state
    QPointF m_viewCenter = {0, 0};  ///< Center of view in world coords
    double m_zoom = 1.0;             ///< Pixels per world unit
    double m_gridSpacing = 10.0;     ///< Grid spacing in world units
    bool m_showGrid = true;
    bool m_snapToGrid = true;
    SketchPlane m_plane = SketchPlane::XY;

    // Tool state
    SketchTool m_activeTool = SketchTool::Select;
    bool m_isDrawing = false;
    QVector<QPointF> m_previewPoints;
    QPointF m_currentMouseWorld;

    // Pan state
    bool m_isPanning = false;
    QPoint m_lastMousePos;

    // Entities
    QVector<SketchEntity> m_entities;
    int m_nextId = 1;
    int m_selectedId = -1;

    // Entity being created
    SketchEntity m_pendingEntity;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_SKETCHCANVAS_H
