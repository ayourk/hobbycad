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
#include <hobbycad/geometry/utils.h>
#include <hobbycad/sketch/background.h>
#include <hobbycad/sketch/entity.h>
#include <hobbycad/sketch/group.h>
#include <hobbycad/sketch/snap.h>
#include <hobbycad/sketch/undo.h>
#include <hobbycad/units.h>

#include <QWidget>
#include <QPointF>
#include <QVector>
#include <QVector3D>
#include <QKeySequence>
#include <QHash>

#include <optional>

class QContextMenuEvent;

namespace hobbycad {

class ParameterEngine;  // Forward declaration (defined in parameters.h)

// Use types from project.h for consistency
// SketchEntityType and SketchPlane are defined in hobbycad/project.h

/// Describes one dimension input field (e.g., "Length", "Angle")
struct DimFieldDef {
    QString label;        ///< Display name: "Length", "Angle", "Width", etc.
    bool isAngle;         ///< true = angle in degrees, false = length in mm
    double currentValue;  ///< Live value from mouse position (updated each frame)
};

/// Runtime state for each dimension input field
struct DimFieldState {
    QString inputBuffer;  ///< Characters typed so far (empty = not typing)
    bool locked;          ///< Enter was pressed — value is locked
    double lockedValue;   ///< The locked value (mm or degrees)
    int cursorPos = 0;    ///< Cursor position within inputBuffer (0..length)
    bool selectAll = false; ///< Entire buffer is selected (next char replaces all)
};

/// A single sketch entity (GUI version with selection state)
/// Inherits all geometry data and methods from sketch::Entity,
/// adding only GUI-specific state (selected).
struct SketchEntity : public sketch::Entity {
    bool selected = false;        ///< UI selection state
    int tangentEntityId = -1;     ///< Entity this arc is tangent to (-1 if none)

    // Default constructor
    SketchEntity() = default;

    // Construct from library entity
    explicit SketchEntity(const sketch::Entity& e)
        : sketch::Entity(e), selected(false), tangentEntityId(-1) {}
};

/// A parametric constraint (GUI version — inherits library Constraint, adds selection state)
struct SketchConstraint : public sketch::Constraint {
    bool selected = false;         ///< UI selection state

    // Default constructor
    SketchConstraint() = default;

    // Construct from library constraint
    explicit SketchConstraint(const sketch::Constraint& c)
        : sketch::Constraint(c), selected(false) {}
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

// Use library types for groups, undo, transforms
using SketchGroup = sketch::Group;
using TransformType = sketch::TransformType;
using AlignmentType = sketch::AlignmentType;

// Undo command types and UndoCommand struct are provided by the library (sketch::UndoCommand)
// GUI uses the library types directly — no GUI-specific undo types needed.

class SketchCanvas : public QWidget {
    Q_OBJECT

public:
    explicit SketchCanvas(QWidget* parent = nullptr);
    ~SketchCanvas() override;

    /// Set the active drawing tool
    void setActiveTool(SketchTool tool);
    SketchTool activeTool() const { return m_activeTool; }

    /// Set creation mode (for tools with variants like Arc, Circle, Slot)
    void setCreationMode(CreationMode mode);

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

    /// Set display units for dimensions
    void setDisplayUnit(LengthUnit unit);
    void setUnitSuffix(const QString& suffix) { setDisplayUnit(parseUnitSuffix(suffix.toStdString())); }
    LengthUnit displayUnit() const { return m_displayUnit; }
    QString unitSuffix() const { return QString::fromLatin1(hobbycad::unitSuffix(m_displayUnit)); }
    double unitScale() const { return hobbycad::unitScale(m_displayUnit); }

    /// Get all entities
    const QVector<SketchEntity>& entities() const { return m_entities; }

    /// Get all constraints
    const QVector<SketchConstraint>& constraints() const { return m_constraints; }

    /// Get a constraint by ID
    SketchConstraint* constraintById(int id);
    const SketchConstraint* constraintById(int id) const;

    /// Set a constraint value programmatically (used by Properties panel)
    void setConstraintValue(int constraintId, double newValue);

    /// Set all entities (replaces current entities, used for loading)
    void setEntities(const QVector<SketchEntity>& entities);

    /// Get selected entity (or nullptr if none) - returns primary selection
    SketchEntity* selectedEntity();
    const SketchEntity* selectedEntity() const;

    /// Get selected constraint ID (-1 if none)
    int selectedConstraintId() const { return m_selectedConstraintId; }

    /// Multi-selection support
    QVector<SketchEntity*> selectedEntities();
    QVector<const SketchEntity*> selectedEntities() const;
    QSet<int> selectedEntityIds() const { return m_selectedIds; }
    bool isEntitySelected(int entityId) const { return m_selectedIds.contains(entityId); }
    void clearSelection();
    /// Select an entity.  When individualOnly is false (default) the
    /// entire decomposition group is selected; when true only the single
    /// entity is selected (Alt+click behaviour).
    void selectEntity(int entityId, bool addToSelection = false,
                      bool individualOnly = false);

    /// Select entities within a rectangular region
    /// If crossing is true, selects entities that intersect the region
    /// If crossing is false, selects only entities fully enclosed
    void selectEntitiesInRect(const QRectF& rect, bool crossing, bool addToSelection = false);

    /// Select chain of connected entities starting from the given entity
    void selectConnectedChain(int startEntityId);

    /// Expand selection to include all entities in decomposition groups
    /// that have at least one selected member
    void expandSelectionToGroups();

    /// Enter a group — subsequent clicks select individual members.
    /// Pass -1 or call leaveGroup() to exit.
    void enterGroup(int groupId);
    void leaveGroup();
    bool isInsideGroup() const { return m_enteredGroupId >= 0; }
    int enteredGroupId() const { return m_enteredGroupId; }

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

    /// Notify that an entity's geometry was modified externally (e.g. property panel).
    /// Re-solves constraints, re-establishes tangency if needed, emits entityModified, and repaints.
    void notifyEntityChanged(int entityId);

    /// Notify that a specific point on an entity was modified.
    /// Adds a temporary FixedPoint constraint during solving so the edited point
    /// stays exactly where the user placed it, and other geometry adjusts instead.
    void notifyEntityPointChanged(int entityId, int pointIndex);

    /// Re-establish tangency for a tangent arc by re-projecting the tangent
    /// point onto the parent entity and repositioning the center.
    /// Preserves the arc's current radius and sweep angle.
    void reestablishTangency(SketchEntity& arc);

    /// Ensure a Text entity has its rotation handle point (points[1]).
    /// Call this for legacy text entities that only have the anchor point.
    static void ensureTextRotationHandle(SketchEntity& entity);

    /// Recompute a Text entity's rotation handle position from its current
    /// anchor, textRotation, fontSize and text content.
    static void recomputeTextRotationHandle(SketchEntity& entity);

    /// Push an undo command onto the undo stack.
    void pushUndoCommand(const sketch::UndoCommand& cmd);

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

    /// Smart split: split an entity only at the two intersections that
    /// bracket @a clickPoint (nearest before and after along the entity).
    /// Returns IDs of the new segments, or empty if nothing was split.
    QVector<int> splitEntityNearClick(int entityId, const QPointF& clickPoint);

    /// Rejoin collinear line segments back into a single line.
    /// All selected entities must be lines that are collinear (same ray)
    /// and form a contiguous chain sharing endpoints.
    /// Returns the ID of the merged line, or -1 on failure.
    int rejoinCollinearSegments();

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


    /// Get groups
    const QVector<SketchGroup>& groups() const { return m_groups; }

    /// Check if a group is a sweep-angle constraint group
    bool isSweepAngleGroup(int groupId) const;

    // Undo/Redo support
    /// Undo the last operation
    void undo();

    /// Redo the last undone operation
    void redo();

    /// Check if undo is available
    bool canUndo() const { return m_libUndoStack.canUndo(); }

    /// Check if redo is available
    bool canRedo() const { return m_libUndoStack.canRedo(); }

    /// Get descriptions of all undo operations (most recent first)
    QStringList undoDescriptions() const;

    /// Get descriptions of all redo operations (most recent first)
    QStringList redoDescriptions() const;

    /// Undo multiple levels at once (for click-to-navigate in history panel)
    void undoMultiple(int levels);

    /// Redo multiple levels at once (for click-to-navigate in history panel)
    void redoMultiple(int levels);

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

    // Entity lookup helpers
    SketchEntity* entityById(int id);
    const SketchEntity* entityById(int id) const;

signals:
    /// Emitted when an entity is selected or deselected
    void selectionChanged(int entityId);

    /// Emitted when a constraint is selected (constraintId = -1 means deselected)
    void constraintSelectionChanged(int constraintId);

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

    /// Emitted when a creation mode is rejected (e.g., Tangent arc with no valid entities)
    /// The toolbar should revert to the previous mode for that tool
    void creationModeRejected(SketchTool tool);

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

    /// Emitted when undo/redo availability changes
    void undoAvailabilityChanged(bool canUndo);
    void redoAvailabilityChanged(bool canRedo);

    /// Emitted when the undo stack contents change (push, undo, redo, clear)
    void undoStackChanged();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    bool event(QEvent* event) override;  // Intercept Tab before Qt focus navigation
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    // Snap types from the library (aliased for convenience)
    using SnapType = sketch::SnapType;
    using SnapPoint = sketch::SnapPoint;

    // Coordinate transforms
    QPointF screenToWorld(const QPoint& screen) const;
    QPoint worldToScreen(const QPointF& world) const;
    QPointF worldToScreenF(const QPointF& world) const;
    QPointF snapPoint(const QPointF& world) const;
    QPointF snapToAngle(const QPointF& origin, const QPointF& target) const;

    // Drawing helpers
    void drawGrid(QPainter& painter);
    void drawAxes(QPainter& painter);
    void drawBackgroundImage(QPainter& painter);
    void drawEntity(QPainter& painter, const SketchEntity& entity);
    void drawPreview(QPainter& painter);
    void drawSelectionHandles(QPainter& painter, const SketchEntity& entity);
    void drawSnapGuides(QPainter& painter);
    void drawPreviewDimension(QPainter& painter, const QPoint& p1, const QPoint& p2, double value);
    void drawDimensionLabel(QPainter& painter, const QPointF& position, double value);
    void drawArcDimensionLabel(QPainter& painter, const QPointF& position, double arcLength, double angleDeg);
    void drawSnapIndicator(QPainter& painter, const SnapPoint& snap);

    // Constraint drawing helpers
    void drawConstraints(QPainter& painter);
    void drawConstraint(QPainter& painter, const SketchConstraint& constraint);
    void drawDistanceConstraint(QPainter& painter, const SketchConstraint& constraint);
    void drawRadialConstraint(QPainter& painter, const SketchConstraint& constraint);
    void drawAngleConstraint(QPainter& painter, const SketchConstraint& constraint);
    void drawFixedAngleConstraint(QPainter& painter, const SketchConstraint& constraint);
    void drawGeometricConstraint(QPainter& painter, const SketchConstraint& constraint);
    void drawArrow(QPainter& painter, const QPointF& pos, const QPointF& dir, double size);

    // Constraint helpers
    void createConstraint(ConstraintType type, double value, const QPointF& labelPos,
                          bool skipOverConstrainCheck = false,
                          bool startEditing = false);
    void createGeometricConstraint(ConstraintType type);  // For non-dimensional constraints
    void editConstraintValue(int constraintId);
    void finishConstraintCreation();
    void deleteConstraintById(int constraintId);  // Delete a constraint with full cleanup + undo
    void solveConstraints();
    void refreshConstrainedFlags();  // Recompute entity.constrained from remaining constraints
    void updateDrivenDimensions();   // Update Driven dimension values from geometry
    void updateConstraintLabelPositions(); // Reposition labels to track geometry after solving
    ConstraintType detectConstraintType(int entityId1, int entityId2) const;
    bool getConstraintEndpoints(const SketchConstraint& constraint, QPointF& p1, QPointF& p2) const;
    QString describeConstraint(int constraintId) const;  // Human-readable constraint description

    // Constraint label position (shared by hitTest, inline edit overlay, etc.)
    struct ConstraintLabelPosition {
        QPointF textCenter;   // screen-space label center
        QString prefix;       // "R", "Ø", or ""
        bool found = false;
    };
    ConstraintLabelPosition computeConstraintLabelPosition(const SketchConstraint& c) const;

    // Constraint search helpers
    SketchConstraint* findDrivingConstraint(int entityId, ConstraintType type);
    const SketchConstraint* findDrivingConstraint(int entityId, ConstraintType type) const;

    // Constraint target setup helpers
    void setConstraintTargetsForLine(int entityId, const QPointF& p1, const QPointF& p2);
    void setConstraintTargetsForRadial(int entityId, const QPointF& center);

    // Sweep-angle construction line helpers
    void syncSweepAngleConstructionLines(const SketchEntity& arc);
    int findSweepAngleGroupForArc(int arcId) const;

    // Label collision avoidance
    void resolveConstraintLabelOverlaps();
    QHash<int, QPointF> m_labelNudgeOffsets;  ///< constraint id → screen-space nudge (per-frame)
    QPointF findClosestPointOnEntity(const SketchEntity* entity, const QPointF& worldPos) const;
    int findNearestPointIndex(const SketchEntity* entity, const QPointF& worldPos) const;

    // Hit testing
    int hitTest(const QPointF& worldPos) const;
    bool hitTestEntity(const SketchEntity& entity, const QPointF& worldPos) const;
    bool hitTestTextEntity(const SketchEntity& entity, const QPointF& worldPos, double tolerance) const;
    int hitTestConstraintLabel(const QPointF& worldPos) const;

    // Rectangle selection helpers
    bool entityIntersectsRect(const SketchEntity& entity, const QRectF& rect) const;
    bool entityEnclosedByRect(const SketchEntity& entity, const QRectF& rect) const;
    QVector<QPointF> getEntityEndpointsVec(const SketchEntity& entity) const;

    // Constraint conversion
    bool convertToDriving(int constraintId);   // Convert Driven to Driving (returns false if would over-constrain)
    void convertToDriven(int constraintId);    // Convert Driving to Driven

    // Entity lookup helpers (moved to public section)

    // Entity creation
    void startEntity(const QPointF& pos);
    void updateEntity(const QPointF& pos);
    void finishEntity();
    void cancelEntity();
    int nextEntityId();
    /// Decompose a compound entity (Rectangle, Parallelogram) into lines + constraints + group.
    /// Returns true if decomposition occurred. Populates compoundCmd for undo.
    bool decomposeCompoundEntity(const SketchEntity& pendingEntity,
                                 const QVector<QPair<QString, double>>& lockedDims,
                                 sketch::UndoCommand& compoundCmd);

    // Undo/redo single-command helpers (used by Compound undo)
    void undoSingleCommand(const sketch::UndoCommand& cmd);
    void redoSingleCommand(const sketch::UndoCommand& cmd);

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
    LengthUnit m_displayUnit = LengthUnit::Millimeters;  ///< Display units for dimensions
    SketchPlane m_plane = SketchPlane::XY;
    QVector3D m_planeOrigin = {0, 0, 0};  ///< Plane center in absolute 3D coords

    // Tool state
    SketchTool m_activeTool = SketchTool::Select;
    bool m_isDrawing = false;
    bool m_sketchSelected = true;  ///< Whether the sketch itself is selected (for Escape progression)
    QVector<QPointF> m_previewPoints;
    QPointF m_currentMouseWorld;
    QPointF m_drawStartPos;          ///< Screen position when drawing started
    bool m_wasDragged = false;       ///< True if mouse moved significantly during draw

    // Line creation modes
    enum class LineMode { TwoPoint, Horizontal, Vertical, Tangent, Construction };
    LineMode m_lineMode = LineMode::TwoPoint;

    // Arc creation modes
    enum class ArcMode { ThreePoint, CenterStartEnd, StartEndRadius, Tangent };
    ArcMode m_arcMode = ArcMode::ThreePoint;

    // Circle creation modes
    enum class CircleMode { CenterRadius, TwoPoint, ThreePoint, TwoTangent, ThreeTangent };
    CircleMode m_circleMode = CircleMode::CenterRadius;
    QVector<int> m_tangentTargets;  ///< Entity IDs for tangent targets (circle/arc)

    // Rectangle creation modes
    enum class RectMode { Corner, Center, ThreePoint, Parallelogram };
    RectMode m_rectMode = RectMode::Corner;

    // Corner rect rotation state (when both W+H are locked)
    bool m_rectBothLocked = false;       // true once both dims locked
    double m_rectLockRefAngle = 0.0;     // mouse angle at the moment both locked
    double m_rectLockWidthAngle = 0.0;   // initial width direction (0 or π)
    double m_rectLockHeightAngle = 0.0;  // initial height direction (π/2 or -π/2)

    // Polygon creation modes
    enum class PolygonMode { Inscribed, Circumscribed, Freeform };
    PolygonMode m_polygonMode = PolygonMode::Inscribed;

    // Slot creation modes
    enum class SlotMode { CenterToCenter, Overall, ArcRadius, ArcEnds };
    SlotMode m_slotMode = SlotMode::CenterToCenter;
    bool m_arcSlotFlipped = false;  // For > 180 degree arc slots (Shift key)

    // Inline dimension input during entity creation
    QVector<DimFieldDef> m_dimFields;       ///< Available dimension fields for current tool/stage
    QVector<DimFieldState> m_dimStates;     ///< Per-field input state
    int m_dimActiveIndex = -1;              ///< Currently active field (-1 = none)
    QVector<QPair<QString, double>> m_dimLockedForConstraints;  ///< Accumulates across stages
    ParameterEngine* m_paramEngine = nullptr;  ///< Expression evaluator for formula input in dim fields

    void initDimFields();                   ///< Populate fields based on tool/mode/stage
    void clearDimFields();                  ///< Reset all dim input state
    double getLockedDim(int fieldIndex) const;  ///< Returns locked value or -1.0
    void createLockedConstraints(int entityId); ///< Create constraints from locked dims
    void prefillDimField(int fieldIndex);   ///< Pre-fill field with current value, select all
    void drawDimInputField(QPainter& painter, const QPointF& position,
                           int fieldIndex, double rotation = 0.0);

    // Inline constraint value editing (replaces QInputDialog)
    bool     m_inlineEditActive       = false;   ///< Currently editing a constraint label
    int      m_inlineEditConstraintId = -1;      ///< Which constraint is being edited
    QString  m_inlineEditBuffer;                 ///< Text typed so far
    int      m_inlineEditCursorPos    = 0;       ///< Cursor position within buffer
    bool     m_inlineEditSelectAll    = false;   ///< Entire text is selected
    double   m_inlineEditOriginalValue = 0.0;    ///< For revert on Escape
    bool     m_inlineEditIsAngle      = false;   ///< Angle vs length constraint
    bool     m_inlineEditIsCreation   = false;   ///< True = just-created (Escape deletes)

    void beginInlineConstraintEdit(int constraintId, bool isCreation);
    void commitInlineConstraintEdit();
    void cancelInlineConstraintEdit();
    void drawInlineConstraintEdit(QPainter& painter, const QPointF& position,
                                   const QString& prefix = QString());

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
    int m_enteredGroupId = -1;          ///< Currently "entered" group (-1 = none)

    // Entity being created
    SketchEntity m_pendingEntity;

    // Constraints
    QVector<SketchConstraint> m_constraints;
    int m_nextConstraintId = 1;
    int m_selectedConstraintId = -1;

    // D-key quick-add constraint type cycling (TAB to switch)
    int m_dKeyTypeIndex = 0;              ///< Current index into available constraint types
    QString m_dKeyTypeHint;               ///< Overlay hint text (e.g. "Radius", "Diameter")

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
    int m_fixedHandleIndex = -1;     ///< Index of fixed handle for arc slot resize (-1 = none)
    QPointF m_dragStartWorld;        ///< World position when drag started
    QPointF m_dragHandleOriginal;    ///< Original handle position before drag
    QPointF m_dragHandleOriginal2;   ///< Second point for circles (radius point)
    double m_dragOriginalRadius = 0; ///< Original radius for circles/arcs
    sketch::Entity m_dragOriginalEntity; ///< Full entity snapshot before drag (for undo)
    QPointF m_lastRawMouseWorld;     ///< Last raw (unsnapped) mouse position
    bool m_shiftWasPressed = false;  ///< Track Shift state for snap-to-grid during drag
    bool m_ctrlWasPressed = false;   ///< Track Ctrl state for axis constraint during drag

    /// Axis lock for Ctrl+drag constraint
    enum class SnapAxis { None, X, Y };
    SnapAxis m_snapAxis = SnapAxis::None;  ///< Locked axis during Ctrl+drag

    // Entity snap state
    bool m_snapToEntities = true;           ///< Enable entity snap points
    std::optional<SnapPoint> m_activeSnap;  ///< Currently active snap point (if any)
    double m_entitySnapTolerance = 10.0;    ///< Snap tolerance in pixels

    // Angle snap state (Ctrl key during line drawing)
    bool m_angleSnapActive = false;         ///< Whether angle snap is currently active
    double m_snappedAngle = 0.0;            ///< The angle we snapped to (degrees)

    // Handle hit testing
    int hitTestHandle(const QPointF& worldPos) const;

    /// Hit-test handles across all entities in the primary entity's group.
    /// Returns the point index via handleIdx and the owning entity ID via
    /// entityId.  Returns true if a handle was hit.
    bool hitTestGroupHandle(const QPointF& worldPos, int& entityId, int& handleIdx) const;

    // Tangent circle helpers — use library result types directly
    using TangentCircle = geometry::TangentCircleResult;
    TangentCircle calculate2TangentCircle(const SketchEntity& e1, const SketchEntity& e2,
                                          const QPointF& hint) const;
    TangentCircle calculate3TangentCircle(const SketchEntity& e1, const SketchEntity& e2,
                                          const SketchEntity& e3) const;

    // Tangent arc helper — uses library result type directly
    using TangentArc = geometry::TangentArcResult;
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

    // Undo/Redo support
    sketch::UndoStack m_libUndoStack{100};

    /// Update undo/redo action availability
    void updateUndoRedoState();
};

}  // namespace hobbycad

#endif  // HOBBYCAD_SKETCHCANVAS_H
