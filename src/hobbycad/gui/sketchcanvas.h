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

#include <hobbycad/project.h>
#include <hobbycad/sketch/background.h>

#include <QWidget>
#include <QPointF>
#include <QVector>
#include <QVector3D>
#include <QKeySequence>
#include <QHash>

class QContextMenuEvent;

namespace hobbycad {

// Use types from project.h for consistency
// SketchEntityType and SketchPlane are defined in hobbycad/project.h

/// A single sketch entity (GUI version with selection state)
struct SketchEntity {
    int id = 0;
    SketchEntityType type = SketchEntityType::Line;
    QVector<QPointF> points;      ///< Control points
    double radius = 0.0;          ///< For circles/arcs/slots
    double startAngle = 0.0;      ///< For arcs (degrees)
    double sweepAngle = 360.0;    ///< For arcs (degrees)
    int sides = 6;                ///< For polygons (number of sides)
    double majorRadius = 0.0;     ///< For ellipses (semi-major axis)
    double minorRadius = 0.0;     ///< For ellipses (semi-minor axis)
    QString text;                 ///< For text entities
    QString fontFamily;           ///< Font family (empty = default)
    double fontSize = 12.0;       ///< Font size in mm
    bool fontBold = false;        ///< Bold text
    bool fontItalic = false;      ///< Italic text
    double textRotation = 0.0;    ///< Text rotation in degrees
    bool selected = false;
    bool constrained = false;     ///< Has constraints applied
    bool isConstruction = false;  ///< Construction geometry (dashed, excluded from profiles)
};

/// A parametric constraint (GUI version with selection and solver state)
struct SketchConstraint {
    int id = 0;
    ConstraintType type = ConstraintType::Distance;
    QVector<int> entityIds;        ///< IDs of entities involved in constraint
    QVector<int> pointIndices;     ///< Point indices within entities (for multi-point entities)
    double value = 0.0;            ///< Constraint value (distance in mm, angle in degrees, etc.)
    bool isDriving = true;         ///< True = driving constraint, False = reference (display only)
    QPointF labelPosition;         ///< Where to display the dimension label in 2D sketch space
    bool labelVisible = true;      ///< Show/hide dimension text
    bool satisfied = true;         ///< Whether constraint is currently satisfied (for solver feedback)
    bool enabled = true;           ///< Whether constraint is active
    bool selected = false;         ///< UI selection state
};

/// A closed profile (loop) detected in the sketch
/// Used for extrusion and other 3D operations
struct SketchProfile {
    int id = 0;
    QVector<int> entityIds;        ///< IDs of entities forming the loop (in order)
    QVector<bool> reversed;        ///< Whether each entity is traversed in reverse
    QPolygonF polygon;             ///< Approximated polygon for the profile
    double area = 0.0;             ///< Area of the profile (signed: positive = CCW, negative = CW)
    bool isOuter = true;           ///< True if outer profile, false if inner (hole)
};

/// Entity group for organizing related entities
struct SketchGroup {
    int id = 0;
    QString name;
    QVector<int> entityIds;
    bool locked = false;           ///< Prevent modification of group members
};

/// Transform operation types
enum class TransformType {
    Move,
    Copy,
    Rotate,
    Scale,
    Mirror
};

/// Alignment types for multi-selection
enum class AlignmentType {
    Left,
    Right,
    Top,
    Bottom,
    HorizontalCenter,
    VerticalCenter,
    DistributeHorizontal,
    DistributeVertical
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

    /// Get selected entity (or nullptr if none) - returns primary selection
    SketchEntity* selectedEntity();
    const SketchEntity* selectedEntity() const;

    /// Multi-selection support
    QVector<SketchEntity*> selectedEntities();
    QVector<const SketchEntity*> selectedEntities() const;
    QSet<int> selectedEntityIds() const { return m_selectedIds; }
    bool isEntitySelected(int entityId) const { return m_selectedIds.contains(entityId); }
    void clearSelection();
    void selectEntity(int entityId, bool addToSelection = false);

    /// Select entities within a rectangular region
    /// If crossing is true, selects entities that intersect the region
    /// If crossing is false, selects only entities fully enclosed
    void selectEntitiesInRect(const QRectF& rect, bool crossing, bool addToSelection = false);

    /// Select chain of connected entities starting from the given entity
    void selectConnectedChain(int startEntityId);

    /// Sketch selection state (for Escape key progression)
    void setSketchSelected(bool selected) { m_sketchSelected = selected; }
    bool isSketchSelected() const { return m_sketchSelected; }

    /// Profile detection - find closed loops in the sketch
    QVector<SketchProfile> detectProfiles() const;

    /// Check if sketch has at least one valid profile for extrusion
    bool hasValidProfile() const;

    /// Profile visualization (highlight detected closed loops)
    void setShowProfiles(bool show);
    bool showProfiles() const { return m_showProfiles; }

    /// Clear all entities
    void clear();

    /// Reset view to fit all entities
    void resetView();

    /// Zoom to fit
    void zoomToFit();

    /// View rotation (rotation of the 2D canvas itself, not the sketch content)
    void setViewRotation(double degrees);
    double viewRotation() const { return m_viewRotation; }
    void rotateViewCW();   ///< Rotate view 90° clockwise
    void rotateViewCCW();  ///< Rotate view 90° counter-clockwise

    /// Set the plane origin in absolute coordinates (for coordinate display)
    /// This allows the status bar to show both relative and absolute coordinates
    void setPlaneOrigin(double x, double y, double z);
    QVector3D planeOrigin() const { return m_planeOrigin; }

    /// Reload key bindings from settings (call when bindings change)
    void reloadBindings();

    /// Toggle construction geometry flag for an entity
    void setEntityConstruction(int entityId, bool isConstruction);

    // Trim/Extend/Split operations
    /// Find all intersections between entities
    struct Intersection {
        int entityId1;
        int entityId2;
        QPointF point;
        double param1;  ///< Parameter along entity 1 (0-1 for lines, angle for circles/arcs)
        double param2;  ///< Parameter along entity 2
    };
    QVector<Intersection> findAllIntersections() const;

    /// Trim entity at click point (removes segment between intersections)
    bool trimEntityAt(int entityId, const QPointF& clickPoint);

    /// Extend entity to nearest intersection or boundary
    bool extendEntityTo(int entityId, const QPointF& clickPoint);

    /// Split entity at all intersections with other entities
    QVector<int> splitEntityAtIntersections(int entityId);

    /// Split entity at a specific point
    QVector<int> splitEntityAt(int entityId, const QPointF& splitPoint);

    // Apply geometric constraints to selected entities
    void applyHorizontalConstraint();
    void applyVerticalConstraint();
    void applyParallelConstraint();
    void applyPerpendicularConstraint();
    void applyCoincidentConstraint();
    void applyTangentConstraint();
    void applyEqualConstraint();
    void applyMidpointConstraint();
    void applySymmetricConstraint();

    // Multi-selection operations
    /// Delete all selected entities
    void deleteSelectedEntities();

    /// Transform selected entities (move, copy, rotate, scale, mirror)
    void transformSelectedEntities(TransformType type);

    /// Align selected entities
    void alignSelectedEntities(AlignmentType type);

    /// Group selected entities
    int groupSelectedEntities();

    /// Ungroup a group
    void ungroupEntities(int groupId);

    /// Split all selected entities at their mutual intersections
    void splitSelectedAtIntersections();

    /// Offset an entity by a distance
    /// Creates a parallel copy of the entity at the specified distance
    /// direction is determined by clickPos relative to entity
    void offsetEntity(int entityId, double distance, const QPointF& clickPos);

    /// Create fillet (rounded corner) between two connected lines
    void filletCorner(int lineId1, int lineId2, double radius);

    /// Create chamfer (beveled corner) between two connected lines
    void chamferCorner(int lineId1, int lineId2, double distance);

    /// Create rectangular pattern of selected entities
    void createRectangularPattern();

    /// Create circular pattern of selected entities
    void createCircularPattern();

    /// Find a line connected to the given line at the corner nearest to clickPos
    int findConnectedLineAtCorner(int lineId, const QPointF& clickPos) const;

    /// Get groups
    const QVector<SketchGroup>& groups() const { return m_groups; }

    // Background image support
    /// Set background image for the sketch
    void setBackgroundImage(const sketch::BackgroundImage& bg);

    /// Get current background image
    const sketch::BackgroundImage& backgroundImage() const { return m_backgroundImage; }

    /// Check if sketch has a background image
    bool hasBackgroundImage() const { return m_backgroundImage.enabled; }

    /// Remove background image
    void clearBackgroundImage();

    /// Enable/disable background manipulation mode
    void setBackgroundEditMode(bool enabled);
    bool isBackgroundEditMode() const { return m_backgroundEditMode; }

    /// Enable/disable background calibration mode (point picking)
    void setBackgroundCalibrationMode(bool enabled);
    bool isBackgroundCalibrationMode() const { return m_backgroundCalibrationMode; }

    /// Enable/disable entity selection mode for calibration alignment
    void setCalibrationEntitySelectionMode(bool enabled);
    bool isCalibrationEntitySelectionMode() const { return m_calibrationEntitySelectionMode; }

    /// Get the angle of a line entity (returns angle in degrees, 0 if not a line)
    double getEntityAngle(int entityId) const;

signals:
    /// Emitted when an entity is selected or deselected
    void selectionChanged(int entityId);

    /// Emitted when an entity is created
    void entityCreated(int entityId);

    /// Emitted when an entity is modified
    void entityModified(int entityId);

    /// Emitted during handle dragging for real-time property updates
    void entityDragging(int entityId);

    /// Emitted when the mouse position changes (for status bar)
    /// pos is in relative (plane-local) coordinates
    void mousePositionChanged(const QPointF& pos);

    /// Emitted when the mouse position changes with absolute coordinates
    /// absolutePos is in global 3D space, relativePos is in plane-local 2D space
    void mousePositionChangedAbsolute(const QVector3D& absolutePos, const QPointF& relativePos);

    /// Emitted when the canvas requests a tool change (e.g., Escape to Select)
    void toolChangeRequested(SketchTool tool);

    /// Emitted when Escape is pressed with no entity selected (deselect sketch)
    void sketchDeselected();

    /// Emitted when Escape is pressed with sketch already deselected (exit sketch mode)
    void exitRequested();

    /// Emitted when a constraint is created
    void constraintCreated(int constraintId);

    /// Emitted when a constraint value is modified
    void constraintModified(int constraintId);

    /// Emitted when a constraint is deleted
    void constraintDeleted(int constraintId);

    /// Emitted when background image changes
    void backgroundImageChanged(const sketch::BackgroundImage& bg);

    /// Emitted when background edit mode changes
    void backgroundEditModeChanged(bool enabled);

    /// Emitted when a calibration point is picked on the background
    void calibrationPointPicked(const QPointF& sketchCoords);

    /// Emitted when an entity is selected for calibration alignment
    void calibrationEntitySelected(int entityId, double angle);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    // Coordinate transforms
    QPointF screenToWorld(const QPoint& screen) const;
    QPoint worldToScreen(const QPointF& world) const;
    QPointF snapPoint(const QPointF& world) const;

    // Drawing helpers
    void drawGrid(QPainter& painter);
    void drawAxes(QPainter& painter);
    void drawBackgroundImage(QPainter& painter);
    void drawEntity(QPainter& painter, const SketchEntity& entity);
    void drawPreview(QPainter& painter);
    void drawSelectionHandles(QPainter& painter, const SketchEntity& entity);
    void drawSnapGuides(QPainter& painter);

    // Constraint drawing helpers
    void drawConstraints(QPainter& painter);
    void drawConstraint(QPainter& painter, const SketchConstraint& constraint);
    void drawDistanceConstraint(QPainter& painter, const SketchConstraint& constraint);
    void drawRadialConstraint(QPainter& painter, const SketchConstraint& constraint);
    void drawAngleConstraint(QPainter& painter, const SketchConstraint& constraint);
    void drawGeometricConstraint(QPainter& painter, const SketchConstraint& constraint);
    void drawArrow(QPainter& painter, const QPointF& pos, const QPointF& dir, double size);

    // Constraint helpers
    void createConstraint(ConstraintType type, double value, const QPointF& labelPos);
    void createGeometricConstraint(ConstraintType type);  // For non-dimensional constraints
    void editConstraintValue(int constraintId);
    void finishConstraintCreation();
    void solveConstraints();
    void updateDrivenDimensions();  // Update Driven dimension values from geometry
    ConstraintType detectConstraintType(int entityId1, int entityId2) const;
    double calculateConstraintValue(ConstraintType type, const QVector<int>& entityIds,
                                     const QVector<QPointF>& points) const;
    bool getConstraintEndpoints(const SketchConstraint& constraint, QPointF& p1, QPointF& p2) const;
    SketchConstraint* constraintById(int id);
    const SketchConstraint* constraintById(int id) const;
    QString describeConstraint(int constraintId) const;  // Human-readable constraint description
    QPointF findClosestPointOnEntity(const SketchEntity* entity, const QPointF& worldPos) const;
    int findNearestPointIndex(const SketchEntity* entity, const QPointF& worldPos) const;

    // Hit testing
    int hitTest(const QPointF& worldPos) const;
    bool hitTestEntity(const SketchEntity& entity, const QPointF& worldPos) const;
    int hitTestConstraintLabel(const QPointF& worldPos) const;

    // Rectangle selection helpers
    bool entityIntersectsRect(const SketchEntity& entity, const QRectF& rect) const;
    bool entityEnclosedByRect(const SketchEntity& entity, const QRectF& rect) const;
    QVector<QPointF> getEntityEndpointsVec(const SketchEntity& entity) const;

    // Constraint conversion
    bool convertToDriving(int constraintId);   // Convert Driven to Driving (returns false if would over-constrain)
    void convertToDriven(int constraintId);    // Convert Driving to Driven

    // Entity lookup helpers
    SketchEntity* entityById(int id);
    const SketchEntity* entityById(int id) const;

    // Entity creation
    void startEntity(const QPointF& pos);
    void updateEntity(const QPointF& pos);
    void finishEntity();
    void cancelEntity();
    int nextEntityId();

    // Handle dragging helpers
    void applyCtrlSnapToHandle();
    QPointF axisLockedSnapPoint(const QPointF& worldPos) const;

    // View state
    QPointF m_viewCenter = {0, 0};  ///< Center of view in world coords
    double m_zoom = 1.0;             ///< Pixels per world unit
    double m_viewRotation = 0.0;     ///< View rotation in degrees (CW positive)
    double m_gridSpacing = 10.0;     ///< Grid spacing in world units
    bool m_showGrid = true;
    bool m_snapToGrid = false;  ///< Off by default, toggle via View menu
    SketchPlane m_plane = SketchPlane::XY;
    QVector3D m_planeOrigin = {0, 0, 0};  ///< Plane center in absolute 3D coords

    // Tool state
    SketchTool m_activeTool = SketchTool::Select;
    bool m_isDrawing = false;
    bool m_sketchSelected = true;  ///< Whether the sketch itself is selected (for Escape progression)
    QVector<QPointF> m_previewPoints;
    QPointF m_currentMouseWorld;

    // Arc creation modes
    enum class ArcMode { ThreePoint, Tangent };
    ArcMode m_arcMode = ArcMode::ThreePoint;

    // Circle creation modes
    enum class CircleMode { CenterRadius, TwoTangent, ThreeTangent };
    CircleMode m_circleMode = CircleMode::CenterRadius;
    QVector<int> m_tangentTargets;  ///< Entity IDs for tangent targets (circle/arc)

    // Pan state
    bool m_isPanning = false;
    QPoint m_lastMousePos;

    // Window/box selection state
    bool m_isWindowSelecting = false;
    QPointF m_windowSelectStart;       ///< Start point in world coords
    QPointF m_windowSelectEnd;         ///< Current end point in world coords
    bool m_windowSelectCrossing = false; ///< True if right-to-left (crossing mode)

    // Entities
    QVector<SketchEntity> m_entities;
    int m_nextId = 1;
    int m_selectedId = -1;              ///< Primary selected entity (for properties panel)
    QSet<int> m_selectedIds;            ///< All selected entity IDs (for multi-select)

    // Groups
    QVector<SketchGroup> m_groups;
    int m_nextGroupId = 1;

    // Entity being created
    SketchEntity m_pendingEntity;

    // Constraints
    QVector<SketchConstraint> m_constraints;
    int m_nextConstraintId = 1;
    int m_selectedConstraintId = -1;

    // Constraint creation state
    bool m_isCreatingConstraint = false;
    ConstraintType m_pendingConstraintType = ConstraintType::Distance;
    QVector<int> m_constraintTargetEntities;
    QVector<QPointF> m_constraintTargetPoints;

    // Constraint label dragging state
    bool m_isDraggingConstraintLabel = false;
    QPointF m_constraintLabelOriginal;

    // Handle dragging state
    bool m_isDraggingHandle = false;
    int m_dragHandleIndex = -1;      ///< Index of handle point being dragged
    QPointF m_dragStartWorld;        ///< World position when drag started
    QPointF m_dragHandleOriginal;    ///< Original handle position before drag
    QPointF m_dragHandleOriginal2;   ///< Second point for circles (radius point)
    double m_dragOriginalRadius = 0; ///< Original radius for circles/arcs
    QPointF m_lastRawMouseWorld;     ///< Last raw (unsnapped) mouse position
    bool m_shiftWasPressed = false;  ///< Track Shift state for snap-to-grid during drag
    bool m_ctrlWasPressed = false;   ///< Track Ctrl state for axis constraint during drag

    /// Axis lock for Ctrl+drag constraint
    enum class SnapAxis { None, X, Y };
    SnapAxis m_snapAxis = SnapAxis::None;  ///< Locked axis during Ctrl+drag

    // Handle hit testing
    int hitTestHandle(const QPointF& worldPos) const;

    // Tangent circle helpers
    struct TangentCircle {
        QPointF center;
        double radius;
        bool valid;
    };
    TangentCircle calculate2TangentCircle(const SketchEntity& e1, const SketchEntity& e2,
                                          const QPointF& hint) const;
    TangentCircle calculate3TangentCircle(const SketchEntity& e1, const SketchEntity& e2,
                                          const SketchEntity& e3) const;

    // Tangent arc helper
    struct TangentArc {
        QPointF center;
        double radius;
        double startAngle;
        double sweepAngle;
        bool valid;
    };
    TangentArc calculateTangentArc(const SketchEntity& tangentEntity, const QPointF& tangentPoint,
                                   const QPointF& endPoint) const;

    // Key bindings (loaded from settings)
    QHash<QString, QList<QKeySequence>> m_keyBindings;
    void loadKeyBindings();
    bool matchesBinding(const QString& actionId, QKeyEvent* event) const;

    // Profile visualization
    bool m_showProfiles = false;
    mutable QVector<SketchProfile> m_cachedProfiles;
    mutable bool m_profilesCacheDirty = true;
    void drawProfiles(QPainter& painter) const;
    void invalidateProfileCache() { m_profilesCacheDirty = true; }

    // Background image
    sketch::BackgroundImage m_backgroundImage;
    mutable QImage m_cachedBackgroundImage;  ///< Cached adjusted image for rendering
    mutable bool m_backgroundCacheDirty = true;
    void invalidateBackgroundCache() { m_backgroundCacheDirty = true; }

    // Background manipulation mode
    bool m_backgroundEditMode = false;
    bool m_backgroundCalibrationMode = false;  ///< Picking points for calibration
    bool m_calibrationEntitySelectionMode = false;  ///< Selecting entity for alignment
    enum class BackgroundHandle { None, Move, TopLeft, Top, TopRight, Right, BottomRight, Bottom, BottomLeft, Left };
    BackgroundHandle m_bgDragHandle = BackgroundHandle::None;
    QPointF m_bgDragStartWorld;
    QPointF m_bgOriginalPosition;
    double m_bgOriginalWidth = 0;
    double m_bgOriginalHeight = 0;

    void drawBackgroundHandles(QPainter& painter);
    BackgroundHandle hitTestBackgroundHandle(const QPointF& worldPos) const;
    QRectF backgroundHandleRect(BackgroundHandle handle) const;
    void updateCursorForBackgroundHandle(BackgroundHandle handle);
};

}  // namespace hobbycad

#endif  // HOBBYCAD_SKETCHCANVAS_H
