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
#include "dimensioninput.h"
#include "constraintrenderer.h"
#include "snapengine.h"
#include "entityrenderer.h"
#include "sketchtheme.h"

#include <hobbycad/project.h>
#include <hobbycad/geometry/utils.h>
#include <hobbycad/sketch/background.h>
#include <hobbycad/sketch/entity.h>
#include <hobbycad/sketch/group.h>
#include <hobbycad/sketch/snap.h>
#include <hobbycad/sketch/inference.h>
#include <hobbycad/sketch/solver.h>
#include <hobbycad/sketch/handles.h>
#include <hobbycad/sketch/undo.h>
#include <hobbycad/sketch/transform.h>
#include <hobbycad/units.h>

#include <QWidget>

#include <memory>
#include <cstdio>   // FILE (debugLogFile)
#include <vector>
#include <QPointF>
#include <QVector>
#include <QVector3D>
#include <QKeySequence>
#include <functional>
#include <QHash>
#include <map>

#include <optional>

class QContextMenuEvent;
class QMenu;

namespace hobbycad {

class SketchToolHandler;

class ParameterEngine;  // Forward declaration (defined in parameters.h)

// Use types from project.h for consistency
// SketchEntityType and SketchPlane are defined in hobbycad/project.h

// DimFieldDef / DimFieldState now live in dimensioninput.h.

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

/// A parametric constraint (GUI version: inherits library Constraint, adds selection state)
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
// GUI uses the library types directly; no GUI-specific undo types needed.

class SketchCanvas : public QWidget, public DimensionInputHost {
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

    /// 2D/3D sketch mode (the toolbar checkmark). 2D is the default: points are
    /// pinned to the plane and the UI hides the off-plane coordinate; 3D reveals
    /// it. Storage is 3D either way (a point's z is its off-plane w).
    void setSketchMode(bool threeD);
    bool sketchMode() const { return m_is3D; }

    /// Heads/tails: draw from the far side of the plane. A pure view flip:
    /// the stored plane-local (u,v) coordinates never change; the canvas just
    /// mirrors u left<->right on screen (so +u renders to the left) and the
    /// positive axis follows its rendered side. Persisted per sketch.
    void setFlipView(bool flipped);
    bool isFlipped() const { return m_flipView; }

    /// Grid settings
    void setGridVisible(bool visible);
    bool isGridVisible() const { return m_showGrid; }
    void setGridSpacing(double spacing);
    double gridSpacing() const { return m_gridSpacing; }
    void setSnapToGrid(bool snap);
    bool snapToGrid() const { return m_snapToGrid; }

    /// Show the on-canvas hint that trails the cursor for drawing tools
    /// (preference "preferences/showCursorHints", default on). (Aaron)
    void setShowCursorHints(bool show);
    bool showCursorHints() const { return m_showCursorHints; }

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

    /// Restore a sketch's full contents (entities, constraints and groups)
    /// and re-seed the id counters.  setEntities() deliberately drops
    /// constraints; use this when reopening a saved sketch for editing.
    void setSketchContents(const QVector<SketchEntity>& entities,
                           const QVector<SketchConstraint>& constraints,
                           const QVector<SketchGroup>& groups);

    /// Append imported geometry (e.g. from DXF) to the CURRENT sketch, giving
    /// each entity a fresh id and recording one undo step per entity. Existing
    /// geometry is kept. Returns the number of entities added.
    int addImportedEntities(const QVector<SketchEntity>& entities);

    /// Get selected entity (or nullptr if none) - returns primary selection
    SketchEntity* selectedEntity();

    /// Apply an external edit to one point of an entity: set its coordinate,
    /// record one undo, and re-solve keeping that point fixed. Returns false if
    /// the entity or point index does not exist. Used by the properties panel's
    /// editable coordinate fields.
    bool applyPointEdit(int entityId, int pointIndex, const Point3& p);
    const SketchEntity* selectedEntity() const;

    /// Get selected constraint ID (-1 if none)
    int selectedConstraintId() const { return m_selectedConstraintId; }

    /// Select a constraint by id (-1 clears). Mirrors what clicking a
    /// constraint glyph on the canvas does, so external panels such as the
    /// constraint explorer can drive selection without duplicating the
    /// bookkeeping.
    // Staged placement constraints: apply locked dimension values for one
    // tool/mode at the current stage. Called from BOTH mousePressEvent and
    // mouseReleaseEvent; both call sites are required (click vs drag-through
    // placement). See the note above the definitions.

    // --- Tool creation sub-modes ------------------------------------
    // Public because per-tool handlers (gui/tools/) must name them.
    // They describe the TOOL, not canvas internals.
    enum class LineMode { TwoPoint, Horizontal, Vertical, Tangent, Construction };
    enum class ArcMode { ThreePoint, CenterStartEnd, StartEndRadius, Tangent };
    enum class CircleMode { CenterRadius, TwoPoint, ThreePoint, TwoTangent, ThreeTangent };
    enum class RectMode { Corner, Center, ThreePoint, Parallelogram };
    enum class PolygonMode { Inscribed, Circumscribed, Freeform };
    enum class SlotMode { CenterToCenter, Overall, ArcRadius, ArcEnds };

    /// How a click is interpreted. Everything below the input layer is
    /// shared between the two; only click semantics differ.
    enum class InteractionMode {
        PlacementFirst,     ///< Snap and type dimensions as you place.
        DrawThenConstrain,  ///< Place roughly, then constrain.
    };

    InteractionMode interactionMode() const { return m_interactionMode; }

    /// Switch modes. Refused mid-entity: the two modes disagree about what
    /// a click means, so changing it halfway through placing something
    /// would reinterpret clicks already made. Returns false if refused.
    bool setInteractionMode(InteractionMode mode);

    // --- Tool handler support ---------------------------------------
    // Narrow accessors so a SketchToolHandler can do its job without being a
    // friend of this class. Kept deliberately small: every addition here is a
    // piece of canvas internals a handler now depends on.
    int  previewPointCount() const { return m_previewPoints.size(); }
    void addDimField(const QString& label, bool isAngle);
    /// Status-bar prompt from the active handler; empty if none.
    QString currentToolHint() const;
    /// Locked value of dimension field `index`, or -1.0 when not locked.
    double lockedDim(int index) const { return getLockedDim(index); }
    /// The >180-degree "long way round" toggle (Shift during placement).
    bool arcSlotFlipped() const { return m_arcSlotFlipped; }
    /// Entity currently being placed.
    const SketchEntity& pendingEntity() const { return m_pendingEntity; }
    /// Points placed so far this entity.
    QPointF previewPoint(int i) const { return m_previewPoints.at(i); }
    const QVector<QPointF>& previewPoints() const { return m_previewPoints; }
    /// Active sub-mode for tools that have one.
    ArcMode arcMode() const { return m_arcMode; }
    void setArcMode(ArcMode m) { m_arcMode = m; }
    /// Whether any tangent target entity has been picked.
    bool hasTangentTargets() const { return !m_tangentTargets.isEmpty(); }
    /// How many tangent target entities have been picked so far.
    int tangentTargetCount() const { return m_tangentTargets.size(); }
    CircleMode circleMode() const { return m_circleMode; }
    void setCircleMode(CircleMode m) { m_circleMode = m; }
    RectMode rectMode() const { return m_rectMode; }
    void setRectMode(RectMode m) { m_rectMode = m; }
    PolygonMode polygonMode() const { return m_polygonMode; }
    void setPolygonMode(PolygonMode m) { m_polygonMode = m; }
    SlotMode slotMode() const { return m_slotMode; }
    void setSlotMode(SlotMode m) { m_slotMode = m; }
    LineMode lineMode() const { return m_lineMode; }
    void setLineMode(LineMode m) { m_lineMode = m; }

    // Rectangle rotation-lock state. This is per-TOOL state that still lives
    // on the canvas because startEntity() and mouseMoveEvent() also touch it.
    // TODO: move these members into RectangleToolHandler when Rectangle is
    // migrated in Phase 2, step 5 of the migration order in
    // HobbyCAD-outside/plan-sketchcanvas.md ("as each tool moves, move its
    // state off the canvas into the handler"). Not started; Rectangle is not
    // yet a Phase-2 tool, so this state legitimately stays on the canvas.
    /// True when every dimension field currently shown is locked.
    bool allDimFieldsLocked() const;
    /// True when an entity snap point is currently in effect.
    bool hasActiveSnap() const { return m_snapEngine.hasActiveSnap(); }
    /// Forget the entity snap indicator; call this after moving the tracked
    /// cursor somewhere the snap point no longer is.
    void dropActiveSnap() { m_snapEngine.clearActiveSnap(); }
    /// The unsnapped cursor position in world coordinates. Constraint
    /// projection has to start from the raw mouse, not from the snapped
    /// value, or the projection compounds with the snap.
    QPointF rawMouseWorld() const;

    /// Flip the arc/slot sweep to the other side (the Shift toggle).
    void toggleArcFlip() { m_arcSlotFlipped = !m_arcSlotFlipped; }
    /// Look up an entity by id; nullptr when not found.
    const SketchEntity* findEntity(int id) const { return entityById(id); }
    SketchEntity* findEntity(int id) { return entityById(id); }
    /// Record an entity as a tangent target for the tangent line/arc tools.
    void addTangentTarget(int id) { m_tangentTargets.append(id); }
    /// Entity under a world position, or -1. The single-click operation tools
    /// all start with this.
    int pick(const QPointF& worldPos) const { return hitTest(worldPos); }
    /// Show a transient message in the status bar (dialog-free feedback).
    void showStatus(const QString& msg, int timeoutMs = 4000)
    { emit statusMessage(msg, timeoutMs); }
    /// Mutable access to the entity being placed. Needed by handlers that
    /// adjust it outside the click sequence: the scroll wheel changes slot
    /// radius and polygon side count during placement.
    SketchEntity& pendingEntityRef() { return m_pendingEntity; }
    bool isDrawing() const { return m_isDrawing; }
    /// True when the pointer moved past the drag threshold since the current
    /// stage began. Distinguishes click placement from drag-through.
    bool wasDragged() const { return m_wasDragged; }

    // --- Operations a tool handler drives -----------------------------
    // These let a handler carry out a placement step without reaching into
    // canvas internals. They are the canvas's service API to its tools.
    //
    // NOTE: this list is the thing to watch. It grew roughly one entry per
    // tool through phase 1; if phase 2 pushes it much past this, replace the
    // whole accessor set with a context object handed to the handler rather
    // than continuing to widen SketchCanvas.
    QPointF snapToGeometry(const QPointF& worldPos) const { return m_snapEngine.snapPoint(worldPos); }
    /// Reset per-stage drag detection. Must be called at the START of every
    /// stage, not just the first: drag-through placement is detected per
    /// stage (see mouseMoveEvent's 5 px threshold).
    void beginDragDetection(const QPoint& screenPos);
    void appendPlacementPoint(const QPointF& worldPos);
    /// Begin placing a new entity of the active tool at a world position
    /// (the tool-facing name for startEntity).
    void beginPlacement(const QPointF& pos) { startEntity(pos); }
    void commitEntity() { finishEntity(); }
    /// Track the in-progress entity to a position (the tool-facing name for
    /// updateEntity, which now dispatches to the active handler).
    void trackEntity(const QPointF& pos) { updateEntity(pos); }
    /// Record the active snap for the point about to be committed, so the
    /// generic snap evaluation welds a Coincident if it landed on an object.
    /// Tools that commit a terminal point directly call this first. (Aaron)
    void recordEndpointSnap();

    // --- View state a preview needs ------------------------------------
    // With these the accessor set has settled into three recognizable
    // groups: VIEW state (below), PLACEMENT state (preview points, pending
    // entity, locked dims, modes, drag flag) and SERVICES (snap, append,
    // commit, refresh). If this is ever replaced by a context object, those
    // three groups are the natural split; do not add a fourth ad hoc.
    QPoint  toScreen(const QPointF& world) const { return worldToScreen(world); }
    QPointF toScreenF(const QPointF& world) const { return worldToScreenF(world); }
    /// Tangent-arc solve, needed by the Arc tool's tangent preview.
    using TangentArcResult = geometry::TangentArcResult;
    TangentArcResult tangentArcFor(const SketchEntity& target, const QPointF& tangentPoint,
                                   const QPointF& endPoint) const
    { return calculateTangentArc(target, tangentPoint, endPoint); }
    /// Tangent-circle solves, needed by the Circle tool's tangent modes.
    using TangentCircleResult = geometry::TangentCircleResult;
    TangentCircleResult tangentCircleFor(const SketchEntity& e1, const SketchEntity& e2,
                                         const QPointF& hint) const
    { return calculate2TangentCircle(e1, e2, hint); }
    TangentCircleResult tangentCircleFor(const SketchEntity& e1, const SketchEntity& e2,
                                         const SketchEntity& e3) const
    { return calculate3TangentCircle(e1, e2, e3); }
    /// Forget the picked tangent targets (a tangent placement has finished).
    void clearTangentTargets() { m_tangentTargets.clear(); }
    QPointF currentMouseWorld() const { return m_currentMouseWorld; }
    double  zoomFactor() const { return m_zoom; }
    QPointF viewCenter() const { return m_viewCenter; }  ///< View center, world coords
    /// Live value shown in dimension field `i` while dragging. Previews write
    /// this so the on-canvas dimension text tracks the mouse.
    void setDimFieldValue(int i, double v) { m_dimInput.setFieldValue(i, v); }
    int  dimFieldCount() const { return m_dimInput.fieldCount(); }
    int  activeDimField() const { return m_dimInput.activeIndex(); }
    /// Ctrl-held angle snapping, shown as an orange guide in some previews.
    bool   angleSnapActive() const { return m_snapEngine.angleSnapActive(); }
    double snappedAngle() const { return m_snapEngine.snappedAngle(); }
    /// Pick radius for hitting an existing entity, in world units at the
    /// current zoom. Freeform polygon uses it to decide "close enough to the
    /// first vertex to close the loop".
    double entitySnapTolerance() const { return m_snapEngine.entitySnapTolerance(); }
    /// Entity ids picked as tangent targets, in pick order.
    const QVector<int>& tangentTargets() const { return m_tangentTargets; }
    /// Shared preview chrome, used by several tools' previews.
    void paintPreviewDimension(QPainter& p, const QPoint& a, const QPoint& b, double v)
    { m_constraintRenderer.drawPreviewDimension(p, a, b, v); }
    void paintDimInputField(QPainter& p, const QPointF& pos, int field, double rot = 0.0)
    { m_dimInput.draw(p, pos, field, rot); }
    void paintDimensionLabel(QPainter& p, const QPointF& pos, double value)
    { m_constraintRenderer.drawDimensionLabel(p, pos, value); }
    void paintArcDimensionLabel(QPainter& p, const QPointF& pos,
                                double arcLength, double angleDeg)
    { m_constraintRenderer.drawArcDimensionLabel(p, pos, arcLength, angleDeg); }
    /// Re-derive the dimension fields for the new stage (and re-emit the hint).
    void refreshDimFields() { initDimFields(); update(); }

    void setSelectedConstraint(int constraintId);

    /// Delete a constraint by id, with full cleanup and an undo entry.
    /// Public so the constraint explorer can remove a constraint that has no
    /// pickable glyph on the canvas.
    void deleteConstraintById(int constraintId);

    /// Multi-selection support
    QVector<SketchEntity*> selectedEntities();
    QVector<const SketchEntity*> selectedEntities() const;
    QSet<int> selectedEntityIds() const { return m_selectedIds; }
    /// Individually selected points (entityId, pointIndex): endpoints picked
    /// for point-to-point constraints. Distinct from entity selection.
    const QVector<QPair<int,int>>& selectedPoints() const { return m_selectedPoints; }

    // ---- Bezier anchor properties (for the properties panel) ------------
    /// True + fills ids if exactly one splineBezier ANCHOR (control index 3k)
    /// is selected.
    bool selectedBezierAnchor(int& splineId, int& anchorIdx) const;
    /// Read an anchor's tangent angle (deg 0-360), in/out handle lengths, weight,
    /// and whether the spline is rational.
    bool bezierAnchorProps(int splineId, int anchorIdx, double& angleDeg,
                           double& inLen, double& outLen, double& weight,
                           bool& rational) const;
    /// Set an anchor's directed tangent angle (deg), rotating both handles
    /// (smooth), preserving lengths; re-solves.
    void setBezierAnchorAngle(int splineId, int anchorIdx, double angleDeg);
    /// Set one handle's length along its current direction; re-solves.
    void setBezierAnchorHandleLen(int splineId, int anchorIdx, bool outHandle, double len);
    /// Set an anchor's rational weight (all its control points); re-solves.
    void setBezierAnchorWeight(int splineId, int anchorIdx, double weight);
    /// Delete a Bezier fit point (anchor): removes the anchor + its handles
    /// (3 control points), fixes up constraint indices, re-solves. No-op if it
    /// would leave fewer than one segment.
    bool deleteBezierAnchor(int splineId, int anchorIdx);
    /// Insert a Bezier fit point on the segment nearest worldPos (de Casteljau
    /// split: the curve shape is preserved), fixes up constraint indices.
    bool insertBezierFitPoint(int splineId, const QPointF& worldPos);
    /// True if exactly two consecutive control points of one splineBezier are
    /// selected (a control-polygon leg); returns them with i0 < i1.
    bool selectedBezierLeg(int& splineId, int& i0, int& i1) const;
    /// Set a leg's length: moves the non-anchor endpoint along the leg direction
    /// (keeping the anchor, or i0 if neither is an anchor); re-solves.
    void setBezierLegLength(int splineId, int i0, int i1, double len);
    /// Toggle a Bezier spline open/closed. Closing appends the wrap segment's two
    /// handles (auto, Catmull-Rom); opening removes them. Re-solves.
    void toggleBezierClosed(int splineId);
    /// If exactly two non-parallel lines are selected, create an Angle dimension
    /// between them (seeded to the current angle, placed at their shared corner).
    /// Returns false (with a message) if not two lines, or if they are parallel.
    bool dimensionSelectedLinesAngle();
    /// While an Angle dimension's label is dragged, choose interior (label inside
    /// the corner) or exterior (outside) angle; sets value + supplementary to
    /// the current measurement of the chosen side (no geometry change).
    void setAngleSideFromLabel(SketchConstraint* c);
    bool isEntitySelected(int entityId) const { return m_selectedIds.contains(entityId); }
    int selectionCount() const { return m_selectedIds.size(); }

    // --- selection services, for tool handlers ---
    //
    // These are deliberately NARROWER than clearSelection()/selectEntity()
    // below: they touch only the id set and the primary id, and do not clear
    // the per-entity `selected` flags or the constraint selection. The
    // Constraint tool has always worked that way, so the two must stay
    // distinct until that difference is shown to be a bug rather than intent.
    /// Add one entity to the id set without pulling in its decomposition group.
    void selectAddIndividual(int entityId) { selectAdd(entityId); m_selectedId = entityId; }
    /// Empty the id set and forget the primary selection.
    void selectClearIds() { selectClear(); m_selectedId = -1; }
    /// Apply whichever geometric constraint the current selection supports.
    void applySelectionConstraint() { applyInferredConstraint(); }

    // --- dimension services, for the Dimension tool handler ---
    bool isCreatingConstraint() const { return m_isCreatingConstraint; }
    int constraintTargetCount() const { return m_constraintTargetEntities.size(); }

    /// The dimension type the tool will create next, and whether it will be a
    /// driving or a driven (reference) dimension. The Dimension tool exposes
    /// these through a right-click menu before the label is placed, so the
    /// user can pick radius vs diameter and driving vs driven up front rather
    /// than editing after the fact.
    ConstraintType pendingConstraintType() const { return m_pendingConstraintType; }
    void setPendingConstraintType(ConstraintType t) { m_pendingConstraintType = t; }
    bool pendingDimensionDriven() const { return m_pendingDimensionDriven; }
    void setPendingDimensionDriven(bool driving) { m_pendingDimensionDriven = driving; }
    /// True once a dimension is armed with a complete target set, so the next
    /// click places its label (the arm-then-place model that leaves room for
    /// the right-click radius/diameter and driven options in between).
    bool dimensionReadyToPlace() const { return m_dimensionReadyToPlace; }
    /// The entity id of the first (or only) dimension target, or -1 if none is
    /// armed yet. Lets the tool ask whether a radius/diameter choice applies.
    int firstConstraintTargetId() const {
        return m_constraintTargetEntities.isEmpty() ? -1 : m_constraintTargetEntities.first();
    }
    void clearConstraintTargets();
    /// Seed the targets from one entity: a line becomes a Distance dimension,
    /// a circle or arc a Radius one. Returns false when the entity supports
    /// neither, in which case the caller falls back to the two-entity path.
    bool beginSingleEntityDimension(int entityId);
    /// Accumulate one target for the two-entity (angle) workflow. The second
    /// target also settles which constraint type is pending.
    void addConstraintTarget(int entityId, const QPointF& worldPos);
    /// Commit the pending dimension at a label position, seeded with the value
    /// the geometry currently has, and open it for inline editing.
    void placeDimensionLabel(const QPointF& labelPos);
    void clearSelection();
    /// Select an entity.  When individualOnly is false (default) the
    /// entire decomposition group is selected; when true only the single
    /// entity is selected (Alt+click behavior).
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

    /// Enter a group; subsequent clicks select individual members.
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

    /// Toggle construction on the current selection (or the in-progress
    /// entity while drawing). Undoable. Bound to sketch.construction (X).
    void toggleSelectedConstruction();

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
        int entityId1 = 0;
        int entityId2 = 0;
        QPointF point;
        double param1 = 0.0;  ///< Parameter along entity 1 (0-1 for lines, angle for circles/arcs)
        double param2 = 0.0;  ///< Parameter along entity 2
    };
    QVector<Intersection> findAllIntersections() const;

    /// Trim entity at click point (removes segment between intersections)
    bool trimEntityAt(int entityId, const QPointF& clickPoint,
                      bool deleteIfNoIntersection = true);

    /// Extend entity to nearest intersection or boundary
    bool extendEntityTo(int entityId, const QPointF& clickPoint);

    /// Split entity at all intersections with other entities
    QVector<int> splitEntityAtIntersections(int entityId);

    /// Split entity at a specific point
    QVector<int> splitEntityAt(int entityId, const QPointF& splitPoint);

    /// Split entity at a given set of points (e.g. its crossings with a chosen
    /// partner). Lines cut into segments; a circle cuts into arcs whose sweeps
    /// sum to 360 (two or more points required); an arc cuts into sub-arcs.
    QVector<int> splitEntityAtPoints(int entityId, const QVector<QPointF>& points);

    /// Smart split: split an entity only at the two intersections that
    /// bracket @a clickPoint (nearest before and after along the entity).
    /// Returns IDs of the new segments, or empty if nothing was split.
    QVector<int> splitEntityNearClick(int entityId, const QPointF& clickPoint);

    /// Shared apply-path for the split tools: replace @a entityId with the
    /// given pieces as ONE undoable compound, drop constraints that named the
    /// original, and add a Coincident at each junction so the pieces stay
    /// joined. Returns the new piece IDs.
    QVector<int> applySplitPieces(int entityId,
                                  const std::vector<sketch::Entity>& newLibEntities,
                                  const QVector<QPointF>& junctionPoints,
                                  const QString& desc);

    /// Append library-computed constraints (from sketch::computeCutConstraints)
    /// to the model, recording each as an addConstraint into @a subs.
    void recordAddedConstraints(const std::vector<sketch::Constraint>& cs,
                                std::vector<sketch::UndoCommand>& subs);

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
    void applyConcentricConstraint();
    void applyCollinearConstraint();
    void applyFixConstraint();          ///< Fix/Unfix the selection (Fusion Fix)
    void autoConstrainSketch();         ///< Geometric auto-constrain of the whole sketch
    bool applyTypedConstraint(ConstraintType t) { return applyConstraintToSelection(t); }
    /// Tool-first constraint mode: pick a type, then click geometry to apply it
    /// (the tool stays active). -1 = off (the tool infers instead).
    void setConstraintToolType(int type);
    int  constraintToolType() const { return m_toolConstraintType; }
    /// Public point pick + add, for the Constraint tool's per-type mode.
    bool pickAnyPoint(const QPointF& world, int& entityId, int& pointIndex) const
        { return hitTestAnyPoint(world, entityId, pointIndex); }
    /// Hit-test a Bezier control-polygon leg (a segment between two consecutive
    /// control points) of a spline that is showing its decorations. Returns the
    /// two control-point indices so the leg's length can be dimensioned.
    bool hitTestBezierLeg(const QPointF& worldPos, int& entityId, int& i0, int& i1) const;
    /// Nearest circle or arc to a world point (by |dist-to-center - radius|), or
    /// -1 if the sketch has none. Lets the tangent-line tool pick a target even
    /// when the click is not on the circle/arc.
    int nearestCircleOrArc(const QPointF& worldPos) const;
    /// Hit-test the red off-segment tangent-contact marker; returns the
    /// circle's entity id, or -1. Lets the user select that dot (see
    /// hitTestTangentContact in the .cpp).
    int hitTestTangentContact(const QPointF& worldPos) const;
    /// Geometric midpoint of a line (avg of endpoints) or arc (perimeter point
    /// at its angular mid, start+sweep/2). Returns false for other types.
    bool entityMidpoint(const SketchEntity& e, QPointF& out) const;
    /// Hit-test the derived midpoint grip of a line/arc; returns the entity id
    /// whose midpoint is under the cursor, or -1. Grips show only on hover.
    int hitTestMidpoint(const QPointF& worldPos) const;
    /// Solve a HANDLE drag while holding every OTHER entity rigid, so dragging
    /// one handle cannot let the solver collapse an under-constrained partner
    /// (e.g. a tangent arc shrinking to nothing). Uses per-param drag weights
    /// when libslvs exposes them, otherwise temporarily HARD-FIXes the far
    /// points (as the body-drag path does). The grabbed entity reshapes freely;
    /// points welded to the dragged handle follow it.
    void solveHandleDragStabilized(int dragEntityId, int dragPointIndex,
                                   const QPointF& dragPos);
    /// Diagnostic: dump entity points, radii, DOF and constraint counts to the
    /// debug log (used to trace the tangent-arc drag-collapse). A no-op unless
    /// debug logging is enabled via the environment (see debugLogFile()).
    void dbgLog(const char* where);
    /// Resolve the debug log file (opened once) from the environment; nullptr
    /// when disabled, so dbgLog() is a no-op in shipping builds. See the .cpp.
    static FILE* debugLogFile();
    void addSelectedPoint(int entityId, int pointIndex)
        { selectPoint(entityId, pointIndex, /*addToSelection=*/true, /*toggle=*/false); }

    // Multi-selection operations
    /// Delete all selected entities
    void deleteSelectedEntities();

    /// Sketch clipboard (works across sketches in this session): copy/cut the
    /// current selection, paste a fresh offset copy. Internal constraints (all
    /// of whose entities are in the selection) come along; cross-entity links
    /// to entities outside the selection are dropped on paste.
    void copySelection();
    void cutSelection();
    bool pasteClipboard();
    bool hasClipboard() const { return !m_clipEntities.isEmpty(); }

    /// Current parameter values, used to re-evaluate expression-backed
    /// dimensions at solve time so a parameter edit flows into the geometry.
    /// The host (MainWindow) pushes these when parameters change.
    void setParameterValues(const std::map<std::string, double>& values)
    {
        m_parameterValues = values;
    }

    /// Transform selected entities (move, copy, rotate, scale, mirror)
    void transformSelectedEntities(TransformType type);

    /// Commit a transform of the current selection through the shared core:
    /// one compound undo covering members, constraints, added reference
    /// geometry, copies and their group, and the group's pivot. Returns the
    /// core's result; on refusal nothing changed and `refusal` says why.
    sketch::GroupTransformResult applyTransform(const sketch::GroupTransformParams& params, bool createCopy = false);
    /// The selected ids as the library's member list.
    std::vector<int> selectedMemberIds() const;
    /// The group id when the selection is exactly one whole group, else -1.
    int selectedWholeGroupId() const;
    /// Select every member of a group (so it reads as a whole-group selection).
    void selectGroup(int groupId);
    /// If screenPos is on the group indicator glyph, return its group id, else -1.
    int hitTestGroupGlyph(const QPoint& screenPos) const;

    // --- Transform section support (the properties panel drives these) -------
    /// What the canvas is waiting for a click for, on behalf of the panel.
    enum class TransformPick { None, Pivot, FromPoint, ToPoint, PointOnSelection, MirrorA, MirrorB, FreeMove, ReferencePoint };
    /// Show the transformed selection as a faded ghost; nothing live changes.
    sketch::GroupTransformResult previewTransform(const sketch::GroupTransformParams& params, bool createCopy = false);
    void clearTransformPreview();
    bool hasTransformPreview() const { return !m_transformPreview.isEmpty(); }
    /// The pivot the next transform is about: the group's stored pivot, else
    /// the geometric center, else wherever the user put it this time.
    QPointF transformPivot() const;
    bool transformPivotStored() const;
    void setTransformPivot(const QPointF& world);       ///< Transient: takes effect at Apply
    void resetTransformPivotToCenter();                 ///< Back to the geometric center; a stored pivot is cleared at Apply
    void setGroupPivot(int groupId, const std::optional<QPointF>& pivot); ///< Standalone edit, one modifyGroup undo
    bool renameGroup(int groupId, const QString& name);   ///< One modifyGroup undo; false when empty or unchanged
    void setGroupLocked(int groupId, bool locked);         ///< One modifyGroup undo (enforcement is separate work)
    const SketchGroup* groupById(int groupId) const;
    /// True if the entity is a member of a locked group (or a locked ancestor).
    bool isEntityLocked(int entityId) const;
    void beginTransformPick(TransformPick pick);
    void cancelTransformPick();
    TransformPick transformPick() const { return m_transformPick; }
    void setTransformGlyph(bool visible, bool withArc);
    QPointF freeMoveDelta() const { return m_freeMoveDelta; }
    double freeMoveAngle() const { return m_freeMoveAngle; }
    void setFreeMove(const QPointF& delta, double angleDeg);   ///< From the panel's spins

    /// Align selected entities
    void alignSelectedEntities(AlignmentType type);

    /// Group selected entities
    int groupSelectedEntities();

    /// Sweep a width along the single selected line or arc.
    ///
    /// The selected entity is the PATH, not the result: it stays put as the
    /// construction centerline while the swept sides, end caps, half-width
    /// guides and their constraints are built around it, all in one group.
    /// The result is ordinary geometry, not a compound object: the same
    /// thing the CLI's "sweep" command produces, through the same library
    /// call.
    void sweepSelectedPath();

    /// Ungroup a group
    void ungroupEntities(int groupId);

    /// Split all selected entities at their mutual intersections
    void splitSelectedAtIntersections();

    /// Offset an entity by a distance
    /// Creates a parallel copy of the entity at the specified distance
    /// direction is determined by clickPos relative to entity
    void offsetEntity(int entityId, double distance, const QPointF& clickPos);
    /// Re-derive every associative offset copy from its parent (called after a solve).
    void updateAssociativeOffsets();
    void updateSlotsFromPaths();   ///< re-derive path-following slots after a solve (dirty-checked)
    void updateProjectedEntities();

    /// Resolve a projection source that lives in another sketch. The host
    /// (MainWindow, which owns the Project) sets this; without it, only
    /// same-sketch sources resolve. Fills out the source entity, its plane and
    /// offset, and returns true on success.
    using ProjectionResolver = std::function<bool(int sketchId, int entityId,
                                                  SketchEntity& outSource,
                                                  SketchPlane& outPlane,
                                                  double& outOffset)>;
    void setProjectionResolver(ProjectionResolver r) { m_projectionResolver = std::move(r); }
    ProjectionResolver m_projectionResolver;

    /// A projectable source entity in ANOTHER sketch, for the Project tool's
    /// picker (the canvas cannot draw or hit-test foreign geometry, so sources
    /// are chosen from a list the host supplies).
    struct ProjectionSource {
        int sketchId = -1;
        int entityId = -1;
        QString sketchName;
        QString label;      // e.g. "Sketch1 / line 3"
    };
    using ProjectionSourceLister = std::function<std::vector<ProjectionSource>()>;
    void setProjectionSourceLister(ProjectionSourceLister l) { m_projectionSourceLister = std::move(l); }
    std::vector<ProjectionSource> availableProjectionSources() const {
        return m_projectionSourceLister ? m_projectionSourceLister()
                                        : std::vector<ProjectionSource>{};
    }
    ProjectionSourceLister m_projectionSourceLister;

    /// Create a projected child of (sourceSketchId, sourceEntityId) in this
    /// sketch: resolves the source via the resolver, projects it onto this
    /// plane, appends it as reference geometry, and records one undo. Returns
    /// the new entity id, or -1 if the source cannot be resolved or is not yet
    /// projectable (a circle/arc projects to an ellipse and is deferred).
    int createProjection(int sourceSketchId, int sourceEntityId);

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

    /// Degrees of freedom left after the last solve; -1 when unknown.
    int sketchDof() const { return m_sketchDof; }
    /// The sketch's constraint state as one value. Prefer this over testing
    /// sketchDof() against magic numbers.
    sketch::SketchState sketchState() const { return m_sketchState; }

    /// Which constraints could be removed to clear a redundancy.
    ///
    /// Returns CANDIDATES, not a culprit: in a mutually dependent set,
    /// removing any one member clears it, so all are returned. Costs one
    /// solve per constraint, so this is an ON-DEMAND action; never call it
    /// from a repaint or an edit. Empty unless the sketch is actually
    /// over-constrained.
    QVector<int> findRedundantConstraints() const;

    /// Mark constraints the redundancy search named as candidates, so the
    /// canvas can show WHERE the over-constraint is rather than only
    /// listing it. Pass an empty list to clear.
    ///
    /// Driven (reference) constraints are ignored even if passed. The
    /// solver already excludes them from the search, so this is a second
    /// line rather than the guarantee, but the color is what a person
    /// acts on, and pointing at a reference dimension would send them to
    /// delete the one thing that cannot be the problem.
    void setRedundantCandidates(const QVector<int>& ids);
    const QVector<int>& redundantCandidates() const
    {
        return m_redundantCandidates;
    }

    /// Re-evaluate and re-publish the constraint state. Needed when entering
    /// a NEW sketch: nothing is loaded and no constraint changes, so without
    /// this the state would sit at Unknown and the status bar would show
    /// nothing until the user's first edit.
    void refreshConstraintState() { solveConstraints(); }

    /// True when the last solve left zero degrees of freedom, i.e. the
    /// sketch is fully constrained.  Entities are drawn in
    /// fullyConstrainedColor() while this holds.
    bool isSketchFullyConstrained() const { return m_sketchFullyConstrained; }

    QColor fullyConstrainedColor() const { return m_fullyConstrainedColor; }
    void setFullyConstrainedColor(const QColor& c) { m_fullyConstrainedColor = c; update(); }

    // ---- Theme (light / dark) ------------------------------------
    // The canvas paints with QPainter, which QSS cannot reach, so the
    // sketch colors come from a SketchTheme the renderers read. Auto
    // follows the application palette (or a "hobbycad_dark_theme"
    // qApp property, set by main.cpp when a dark widget theme loads).
    enum class ThemeMode { Auto, Light, Dark };

    /// Selection filter: restrict what a click/box grabs. All = point-priority
    /// then curve (default); PointsOnly / CurvesOnly restrict to one kind.
    enum class SelectFilter { All, PointsOnly, CurvesOnly };
    void setSelectFilter(SelectFilter f) { m_selectFilter = f; }
    SelectFilter selectFilter() const { return m_selectFilter; }
    const SketchTheme& theme() const { return m_theme; }
    void setThemeMode(ThemeMode m) { m_themeMode = m; applyTheme(); }
    ThemeMode themeMode() const { return m_themeMode; }
    void applyTheme();          // recompute m_theme from mode + context
    bool isDarkContext() const; // true when the app context is dark

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

    /// True while chaining line segments off a previous LINE, so a press-drag
    /// from the chain point would sweep a tangent arc (F-3). Used by the Line
    /// tool's hint.
    bool tangentArcChainAvailable() const {
        const SketchEntity* p = entityById(m_chainFromEntityId);
        return p && p->type == SketchEntityType::Line;
    }

signals:
    /// Emitted when the sketch's constrained state changes, so the
    /// window can surface "fully constrained" and the DOF count.
    /// The sketch's constraint state changed. `dof` is a real count only
    /// when sketchStateHasDof(state); otherwise it is -1. Carrying the state
    /// rather than a bool keeps over-constrained and inconsistent distinct,
    /// which a "fully constrained yes/no" cannot express.
    void sketchConstraintStateChanged(hobbycad::sketch::SketchState state, int dof);

    /// Emitted when an entity is selected or deselected
    void selectionChanged(int entityId);
    void sketchModeChanged(bool threeD);  ///< 2D/3D toggle changed
    void flipViewChanged(bool flipped);   ///< heads/tails view flip changed

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

    /// Per-stage prompt for the status bar ("click the end point, ...").
    /// Emitted whenever the active tool or placement stage changes. Empty
    /// string means the tool offers no hint (not yet migrated to a handler).
    void toolHintChanged(const QString& hint);
    /// Transient status-bar message (shown instead of a modal dialog).
    void statusMessage(const QString& msg, int timeoutMs);

    /// Emitted when background image changes
    void backgroundImageChanged(const sketch::BackgroundImage& bg);

    /// Emitted when background edit mode changes
    void backgroundEditModeChanged(bool enabled);

    /// Emitted when a calibration point is picked on the background
    void calibrationPointPicked(const QPointF& sketchCoords);

    /// Emitted when an entity is selected for calibration alignment
    void calibrationEntitySelected(int entityId, double angle);

    /// Transform section: a context-menu transform wants the panel expanded
    /// with this TransformType; the pivot moved (stored = it is the group's);
    /// a pick finished (TransformPick as int, snapped world point); the free-move
    /// manipulator changed; Enter asked for Apply; Escape canceled.
    void transformSectionRequested(int transformType);
    void transformPivotChanged(const QPointF& world, bool stored);
    void transformPickCompleted(int pick, const QPointF& world);
    void freeMoveChanged(const QPointF& delta, double angleDeg);
    void transformApplyRequested();
    void transformCanceled();

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
    void changeEvent(QEvent* event) override;  // re-apply theme on palette change

private:
    // Snap types from the library (aliased for convenience)
    using SnapType = sketch::SnapType;
    using SnapPoint = sketch::SnapPoint;

    // Coordinate transforms
    QPointF screenToWorld(const QPoint& screen) const;
    QPoint worldToScreen(const QPointF& world) const;
    QPointF worldToScreenF(const QPointF& world) const;

    // Drawing helpers
    void drawGrid(QPainter& painter);
    void drawAxes(QPainter& painter);
    void drawBackgroundImage(QPainter& painter);
    void drawPreview(QPainter& painter);

    // Constraint drawing helpers

    /// White dots on endpoints that are not constrained to other geometry.
    ///
    /// Fusion's convention, and the answer to "is this sketch actually
    /// constrained, or does it just look right": an unconstrained endpoint
    /// is visibly marked, and a point-to-point coincidence shows nothing at
    /// all. Without it a sketch that merely LOOKS joined is
    /// indistinguishable from one that is.

    /// Mark a line-circle tangency whose touch point falls off the drawn
    /// segment, with a red dot on the circle's perimeter where the tangency
    /// actually happens.
    ///
    /// The constraint is satisfied and the picture still looks wrong: the
    /// segment can stop short of the circle entirely and touch only where
    /// its extension would reach. Nothing else on the canvas says so, and
    /// it cannot be constrained away ("between the endpoints" is an
    /// inequality and the solver takes equations), so it is reported.
    ///
    /// Drawn regardless of "Show Constraints". That toggle is for clearing
    /// clutter; this is a warning, and hiding warnings with the decoration
    /// is how they get missed.
    void drawOffSegmentTangents(QPainter& painter);

    // On-canvas overlay chrome, factored out of paintEvent (canvas-level, not
    // per-tool; distinct from the Phase-2 per-tool drawPreview migration).
    void drawMidpointGrips(QPainter& painter);    ///< hover triangle / selected dot
    void drawSlotAnchorGrips(QPainter& painter);  ///< reveal a slot's anchor points on hover
    void drawCursorHint(QPainter& painter);       ///< tool stage hint trailing the cursor
    void drawScaleBar(QPainter& painter);         ///< 2D scale bar (shares 3D logic)
    void drawEnteredGroupBox(QPainter& painter);  ///< dashed box around the entered group

public:
    /// Fusion's Sketch Palette carries a "Show Points" toggle, so this is
    /// not forced on. On by default: a sketch that looks joined and is not
    /// is worth noticing by default.
    bool showUnconstrainedPoints() const { return m_showUnconstrainedPoints; }
    void setShowUnconstrainedPoints(bool on)
    {
        m_showUnconstrainedPoints = on;
        update();
    }

    /// Curvature comb overlay on Bezier splines (off by default).
    bool isCurvatureCombVisible() const { return m_showCurvatureComb; }
    void setCurvatureCombVisible(bool on) { m_showCurvatureComb = on; update(); }

    /// Fusion's Sketch Palette carries "Show Constraints" separately from
    /// "Show Dimensions", and so does this: it hides the geometric glyph
    /// chips and leaves dimensional labels alone. Hiding both together
    /// would take the numbers with it, which is usually the reason someone
    /// wanted the canvas cleared in the first place.
    bool showConstraints() const { return m_showConstraints; }
    void setShowConstraints(bool on)
    {
        m_showConstraints = on;
        update();
    }

    /// Sub-toggle of "Show Constraints": draw a small marker at point-to-point
    /// coincidences (which otherwise render nothing, Fusion-style). Off by
    /// default; only visible when Show Constraints is also on.
    bool showCoincidenceMarkers() const { return m_showCoincidenceMarkers; }
    void setShowCoincidenceMarkers(bool on) { m_showCoincidenceMarkers = on; update(); }

    /// The mirror of "Show Constraints": hides dimensional labels (the
    /// numbers) while leaving the geometric glyph chips in place. Fusion's
    /// Sketch Palette keeps the two separate, and so does this.
    bool showDimensions() const { return m_showDimensions; }
    void setShowDimensions(bool on)
    {
        m_showDimensions = on;
        update();
    }

    /// Sketch Palette filters: hide construction geometry, or projected
    /// (reference) geometry, without deleting it. Both default on.
    bool showConstruction() const { return m_showConstruction; }
    void setShowConstruction(bool on) { m_showConstruction = on; update(); }
    bool showProjected() const { return m_showProjected; }
    void setShowProjected(bool on) { m_showProjected = on; update(); }

    /// Set the centerline linetype on one entity (mirror of setEntityConstruction).
    void setEntityCenterline(int entityId, bool isCenterline);

private:
    bool m_showConstruction = true;
    bool m_showProjected = true;
    bool m_showUnconstrainedPoints = true;
    bool m_showCurvatureComb = false;
    bool m_showConstraints = true;
    bool m_showCoincidenceMarkers = false;
    QRect m_groupGlyphRect;              ///< screen rect of the group indicator glyph (for hit-testing)
    int  m_groupGlyphGroupId = -1;       ///< group id the glyph represents this paint, or -1
    bool m_showDimensions = true;
    QVector<int> m_redundantCandidates;


    /// True when some constraint ties this entity's point to something.

    // Constraint helpers
    void createConstraint(ConstraintType type, double value, const QPointF& labelPos,
                          bool skipOverConstrainCheck = false,
                          bool startEditing = false,
                          bool driving = true,
                          bool supplementary = false);
    void createGeometricConstraint(ConstraintType type);  // For non-dimensional constraints

    /// Selection mutation. Everything that changes the selection goes through
    /// these three so m_selectedIds and m_selectionOrder stay consistent.
    void selectAdd(int entityId);
    void selectRemove(int entityId);
    void selectClear();

    /// The current selection in the order it was made.
    std::vector<int> selectedEntityList() const;

    /// Apply `type` to the current selection.
    ///
    /// Returns true if a constraint was created. When the selection does not
    /// suit the constraint this explains what is actually needed rather than
    /// saying the feature is unfinished.
    bool applyConstraintToSelection(ConstraintType type);
    /// Fix (or unfix) the selected entities in place: a user-driven
    /// FixedPoint on every point of the selection, like Fusion's Fix/UnFix
    /// and Onshape's Fix (shift+j). One compound undo. If any selected point
    /// is already fixed, the whole selection is unfixed instead.
    void fixSelectedEntities();
    bool selectionHasFixedPoint() const;

    /// Infer and apply the most appropriate constraint for the current
    /// selection, using the library's suggestion rules.
    void applyInferredConstraint();
    void editConstraintValue(int constraintId);
    void finishConstraintCreation();
    void solveConstraints();
    void refreshConstrainedFlags();  // Recompute entity.constrained from remaining constraints
    void updateDrivenDimensions();   // Update Driven dimension values from geometry
    void updateConstraintLabelPositions(); // Reposition labels to track geometry after solving
    ConstraintType detectConstraintType(int entityId1, int entityId2) const;
    bool getConstraintEndpoints(const SketchConstraint& constraint, QPointF& p1, QPointF& p2) const;
    QString describeConstraint(int constraintId) const;  // Human-readable constraint description

    // Constraint search helpers
    SketchConstraint* findDrivingConstraint(int entityId, ConstraintType type);
    const SketchConstraint* findDrivingConstraint(int entityId, ConstraintType type) const;

    // Constraint target setup helpers
    void setConstraintTargetsForLine(int entityId, const QPointF& p1, const QPointF& p2);
    void setConstraintTargetsForRadial(int entityId, const QPointF& center);

    // Sweep-angle construction line helpers
    void syncSweepAngleConstructionLines(const SketchEntity& arc);
    int findSweepAngleGroupForArc(int arcId) const;

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

    /// Decompose a SIMPLE linear slot into offset sides + round caps +
    /// constraints + a group (via sketch::decomposeSweep), keeping the
    /// centerline as construction geometry, the same treatment rectangles
    /// get. Arc slots and path-slots stay first-class (return false). Adds the
    /// geometry to the canvas and returns the compound undo command. (Aaron)
    /// Build a centerline-driven slot: a construction centerline (Line/Arc) the
    /// slot follows via pathEntityIds, grouped with it. The slot re-derives from
    /// the centerline on each solve (updateSlotsFromPaths). Replaces decomposing
    /// on create; decomposeSlotEntity is now only the "Explode" action. (Aaron)
    bool createCenterlineSlot(SketchEntity& slot, sketch::UndoCommand& compoundCmd);

    /// Make a multi-segment (chain / loop / branching tree) slot from the
    /// currently-selected lines and arcs: they become the construction
    /// centerline and the slot follows their swept outline. Returns false with
    /// a status message when the selection is not a sweepable path.
    bool createTreeSlotFromSelection();

    bool decomposeSlotEntity(const SketchEntity& slot,
                             sketch::UndoCommand& compoundCmd);

    // Undo/redo single-command helpers (used by Compound undo)
    void undoSingleCommand(const sketch::UndoCommand& cmd);
    void redoSingleCommand(const sketch::UndoCommand& cmd);

    // Handle dragging helpers
    void applyCtrlSnapToHandle();

    // Handle drag: geometry lives in libhobbycad (sketch/handles.h);
    // these cover the GUI/model side of a drag.
    void applyHandleDrag(SketchEntity& sel, int handleIndex,
                         const QPointF& finalPos, bool ctrlPressed,
                         bool shiftPressed = false, bool altPressed = false);
    void moveCircleDimensionLabels(const SketchEntity& sel, int handleIndex,
                                   const sketch::HandleDragResult& drag);
    void propagateCoincidentNeighbors(const SketchEntity& sel,
                                       const QPointF& prevPos,
                                       const QPointF& newPos);
    /// After a handle drag lands on another point (a snap), tie them with a
    /// Coincident constraint: the join a drag-onto-snap implies, mirroring
    /// what entity creation does. No-op if the point did not land exactly on
    /// another, or the two are already coincident. Returns true if one added.
    bool createCoincidenceOnDrag(int entityId, int handleIndex);

    /// After a drag that opened a circle (one 360-degree arc with both ends at
    /// the cut), tie the end left in place to any entity sitting there. The end
    /// that ties is the one NOT dragged, so it is resolved here from the drag
    /// direction rather than at split time. Returns true if a tie was added.
    bool tieOpenedArcEndOnDrag(int entityId, int draggedHandle);
    /// Record the active snap for the endpoint just placed, so a line
    /// finished (or closed) on an existing point gets its Coincident.
    /// The 2-point line path sets its end via updateEntity(), which,
    /// unlike appendPlacementPoint(), does not record the snap.
    QPointF axisLockedSnapPoint(const QPointF& worldPos) const;

    // View state
    QPointF m_viewCenter = {0, 0};  ///< Center of view in world coords
    double m_zoom = 1.0;             ///< Pixels per world unit
    double m_viewRotation = 0.0;     ///< View rotation in degrees (CW positive)
    double m_gridSpacing = 10.0;     ///< Grid spacing in world units
    bool m_showGrid = true;
    bool m_snapToGrid = false;  ///< Off by default, toggle via View menu
    bool m_showCursorHints = true;  ///< Cursor-trailing tool hint (pref, default on)
    LengthUnit m_displayUnit = LengthUnit::Millimeters;  ///< Display units for dimensions
    SketchPlane m_plane = SketchPlane::XY;
    bool m_is3D = false;  ///< 2D (default) vs 3D sketch mode (toolbar checkmark)
    bool m_flipView = false;  ///< heads/tails: view/draw from the far side (u mirrored)
    QVector3D m_planeOrigin = {0, 0, 0};  ///< Plane center in absolute 3D coords

    // Tool state
    SketchTool m_activeTool = SketchTool::Select;
    bool m_isDrawing = false;
    bool m_sketchSelected = true;  ///< Whether the sketch itself is selected (for Escape progression)
    QVector<QPointF> m_previewPoints;

    /// Which snap placed each point of the entity being drawn, keyed by
    /// point index. Draw-then-constrain turns these into constraints when
    /// the entity finishes; placement-first ignores them.
    QVector<QPair<int, sketch::SnapPoint>> m_placedSnaps;
    QHash<int, QVector<QPointF>> m_slotPathCache;  ///< slot id -> last path point snapshot (dirty check)

    /// The single evaluation applied to every placed sketch point: for each
    /// point that landed on an existing object (a deliberate snap recorded in
    /// m_placedSnaps), weld a Coincident there; a point in free space gets
    /// none. Fusion applies constraints this way as curves are drawn. Pass an
    /// entity id in excludeTarget to suppress a weld onto that one entity:
    /// the tangent tool passes its target curve, since tangency already governs
    /// that contact. Runs over ALL points of the finished entities, not just
    /// the first. (Aaron)
    void createSnapConstraints(const QVector<int>& newEntityIds,
                               int excludeTarget = -1);

    /// Build+append a constraint with the standard field defaults, mark its
    /// entities constrained, and return the undo command (push it directly or
    /// into a compound). The one home for the constraint boilerplate. (audit)
    sketch::UndoCommand makeConstraint(ConstraintType type,
                                       std::vector<int> entityIds,
                                       std::vector<int> pointIndices = {},
                                       double value = 0.0,
                                       bool driving = true,
                                       bool labelVisible = false);
    /// Which of entityIds carries a point at pos (within eps), matched by
    /// position; skipId is ignored. False if none. (audit)
    bool findOwnerPointAt(const QVector<int>& entityIds, const QPointF& pos,
                          double eps, int skipId,
                          int& outEntityId, int& outPointIndex) const;
    /// Weld the new entity's line endpoints to any existing line endpoint that
    /// lands within a snap tolerance but was not explicitly snapped, so close
    /// corners auto-coincide (a triangle drawn freehand closes into a region).
    void createProximityCoincidences(int newEntityId);
    /// Drop constraints whose entities no longer exist (e.g. left behind after
    /// undo). Keeps the solver from re-logging "entity not in solver" forever.
    void pruneOrphanedConstraints();

    /// Pop the Dimension tool's pre-placement options (radius/diameter when
    /// the armed target is radial, driving/driven always) at a screen point.
    void showDimensionOptionsMenu(const QPoint& screenPos);

    /// Alignment inferences (horizontal/vertical/parallel/perpendicular) for
    /// the segment currently being drawn, recomputed every mouse move while a
    /// straight segment is in progress. Drawn as dashed guides and turned
    /// into real constraints on the entity just finished. Empty when nothing
    /// is inferred or inference is suppressed (Alt, a live point snap, or a
    /// direction-constraining tool mode).
    /// Turn the inferences active at commit (held by the snap engine) into
    /// real constraints on the line just finished; clears them afterward.
    void createInferredConstraints(const QVector<int>& newEntityIds);

    /// Line-chaining tangent-arc gesture (Fusion F-3). While chaining line
    /// segments, a plain click continues the line but a press-drag from the
    /// chain point sweeps a tangent arc off the previous line. To tell click
    /// from drag the chained finish is deferred to release; m_chainFromEntityId
    /// names the previous segment (the one the arc is tangent to).
    bool m_lineChainPressActive = false;
    int  m_chainFromEntityId = -1;
    /// Commit the current chained segment as an arc tangent to the previous
    /// line, then continue chaining from the arc's far end.
    void commitTangentArcSegment();
    /// Draw the dashed tangent-arc preview during a chain drag; true if drawn.
    bool drawTangentArcPreview(QPainter& painter);
    QPointF m_currentMouseWorld;
    QPointF m_drawStartPos;          ///< Screen position when drawing started
    bool m_wasDragged = false;       ///< True if mouse moved significantly during draw

    // Line creation modes
    LineMode m_lineMode = LineMode::TwoPoint;

    // Arc creation modes
    ArcMode m_arcMode = ArcMode::ThreePoint;

    // Circle creation modes
    CircleMode m_circleMode = CircleMode::CenterRadius;
    QVector<int> m_tangentTargets;  ///< Entity IDs for tangent targets (circle/arc)

    // Rectangle creation modes
    RectMode m_rectMode = RectMode::Corner;

    // Corner rect rotation state (when both W+H are locked)


    // Polygon creation modes
    PolygonMode m_polygonMode = PolygonMode::Inscribed;

    // Slot creation modes
    SlotMode m_slotMode = SlotMode::CenterToCenter;
    bool m_arcSlotFlipped = false;  // For > 180 degree arc slots (Shift key)

    // Inline dimension input during entity creation
    // Per-tool handlers. Tools without an entry fall through to the
    // existing switch statements; that is what makes the migration
    // incremental. See gui/tools/sketchtoolhandler.h.
    std::vector<std::unique_ptr<SketchToolHandler>> m_toolHandlers;

    /// Draw-then-constrain handlers, consulted first in that mode. A tool
    /// with no entry here falls back to its placement-first handler, so
    /// the mode is usable long before every tool has one.
    std::vector<std::unique_ptr<SketchToolHandler>> m_drawConstrainHandlers;
    InteractionMode m_interactionMode = InteractionMode::PlacementFirst;

    SketchToolHandler* handlerFor(SketchTool tool) const;
    SketchToolHandler* activeHandler() const { return handlerFor(m_activeTool); }

    DimensionInput m_dimInput;              ///< In-progress dimension fields (state, typing, render)
    friend class ConstraintRenderer;
    ConstraintRenderer m_constraintRenderer;  ///< Constraint/dimension rendering + glyph hit-testing
    friend class SnapEngine;
    SnapEngine m_snapEngine;                ///< GUI snapping + inference (query, state, overlays)
    friend class EntityRenderer;
    EntityRenderer m_entityRenderer;        ///< Committed-entity rendering (drawEntity + handles + free dots)
    ParameterEngine* m_paramEngine = nullptr;  ///< Expression evaluator (dim fields + inline constraint edits)

    void initDimFields();                   ///< Populate fields based on tool/mode/stage
    void clearDimFields();                  ///< Reset all dim input state
    double getLockedDim(int fieldIndex) const;  ///< Returns locked value or -1.0
    void createLockedConstraints(int entityId); ///< Create constraints from locked dims
    void prefillDimField(int fieldIndex) { m_dimInput.prefill(fieldIndex); }

    // ---- DimensionInputHost -------------------------------------------
    bool dimChainsFromLastPoint() const override;
    void dimAfterLock() override;
    void dimReapplyPreview() override;
    void dimRepaint() override { update(); }

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
    QVector<QPair<int,int>> m_selectedPoints;  ///< Individually selected points (entityId,pointIndex)
    int m_hoverMidpointEntity = -1;    ///< line/arc whose midpoint grip is under the cursor (hover only)
    int m_hoverSlotEntity = -1;        ///< slot whose anchor points are revealed on hover
    QPair<int, int> m_selectedSlotAnchor { -1, -1 };  ///< (slot id, anchor index) persisted as a white dot
    int m_selectedMidpointEntity = -1; ///< line/arc whose midpoint is currently selected (drawn white)
    // Point-press arming: a press on an endpoint selects it on release, or
    // becomes a handle drag if the cursor moves first (mirrors body-drag arming).
    int  m_toolConstraintType = -1;   ///< tool-first constraint mode (ConstraintType or -1)
    SelectFilter m_selectFilter = SelectFilter::All;
    bool m_pointPressArmed = false;
    int  m_pointPressEntity = -1;
    int  m_pointPressIndex = -1;
    QPoint m_pointPressScreen;
    Qt::KeyboardModifiers m_pointPressMods = Qt::NoModifier;
    QVector<SketchEntity> m_clipEntities;      ///< Sketch clipboard: copied entities
    QVector<SketchConstraint> m_clipConstraints;///< Sketch clipboard: internal constraints
    std::map<std::string, double> m_parameterValues; ///< Params for expression re-eval
    /// The same selection in the order the user made it.
    ///
    /// A QSet cannot answer "which did they pick first", and several
    /// constraints are order-dependent: Midpoint means *this point* on *that
    /// line*, and Symmetric means *these two* about *that axis*. Inferring a
    /// constraint from an unordered selection would silently pick an
    /// arbitrary role assignment. Kept alongside the set rather than
    /// replacing it so membership tests stay O(1); selectAdd/selectRemove/
    /// selectClear are the only things allowed to touch either, so the two
    /// cannot drift apart.
    QVector<int> m_selectionOrder;

    // Groups
    QVector<SketchGroup> m_groups;
    int m_nextGroupId = 1;

    // Constrained-state feedback (from the last solve)
    int m_sketchDof = -1;                 ///< DOF remaining; -1 = unknown
    sketch::SketchState m_sketchState = sketch::SketchState::Unknown;
    bool m_sketchFullyConstrained = false;
    std::vector<std::pair<int, int>> m_freePoints;  ///< (entityId,pointIndex) still free (solver truth)
    bool m_freePointsValid = false;       ///< true iff libslvs reports free points (else use heuristic)
    /// Whole-sketch color at DOF 0. Black to match Fusion and Onshape,
    /// where black is what "fully defined" looks like.
    QColor m_fullyConstrainedColor{0, 0, 0};
    SketchTheme m_theme = SketchTheme::light();
    ThemeMode m_themeMode = ThemeMode::Auto;
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
    bool m_pendingDimensionDriven = true;   ///< Next dimension driving vs driven
    bool m_dimensionReadyToPlace = false;   ///< Armed target set; next click places
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
    // Opening a full circle (one 360-degree arc) by dragging one end: the
    // fixed end stays at the cut angle; the sweep shrinks from 360, and the
    // dragged endpoint may swap at the inflection. See openFullArcByDrag.
    bool m_openingFullArc = false;     ///< This drag is opening a full arc
    double m_openArcPrevSweep = 0.0;   ///< Previous frame's sweep (continuity; +/-360 seeds)
    double m_openArcFixedAngle = 0.0;  ///< Angle (deg) of the endpoint left at the cut
    int m_openArcDraggedIndex = 1;     ///< Endpoint the drag now controls (1 or 2)
    QVector<SketchEntity> m_dragOriginalGroupEntities;       ///< Every member of the dragged entity's group, before the drag
    QVector<SketchConstraint> m_dragOriginalGroupConstraints; ///< The group's constraints (label positions move too), before the drag
    void syncGroupMembership(const SketchGroup& group, int groupIdOrMinusOne); ///< Set or clear Entity::groupId on the group's members
    void dropStaleEnteredGroup();                              ///< Leave the entered group if it no longer exists
    /// Run the shared transform pipeline on scratch copies of the sketch.
    /// `createCopy` clones the selected set first and transforms the clones.
    /// Fills the out-params and returns the core's result; nothing live changes.
    sketch::GroupTransformResult runTransformScratch(const sketch::GroupTransformParams& params, bool createCopy,
                                                     std::vector<sketch::Entity>& ents,
                                                     std::vector<sketch::Constraint>& cons,
                                                     sketch::CloneSetResult& clones,
                                                     std::vector<int>& targetIds) const;
    QPointF m_lastRawMouseWorld;     ///< Last raw (unsnapped) mouse position
    bool m_shiftWasPressed = false;  ///< Track Shift state for snap-to-grid during drag
    bool m_ctrlWasPressed = false;   ///< Track Ctrl state for axis constraint during drag

    /// Axis lock for Ctrl+drag constraint
    enum class SnapAxis { None, X, Y };
    SnapAxis m_snapAxis = SnapAxis::None;  ///< Locked axis during Ctrl+drag

    // Snap/inference transient state and the snap queries live in m_snapEngine.

    // Handle hit testing
    int hitTestHandle(const QPointF& worldPos) const;
    /// Nearest endpoint of ANY entity within tolerance; returns entity+index.
    bool hitTestAnyPoint(const QPointF& worldPos, int& entityId, int& pointIndex) const;
    /// Add/toggle/replace a point in the point selection (see selectEntity).
    void selectPoint(int entityId, int pointIndex, bool addToSelection, bool toggle);

    /// Hit-test handles across all entities in the primary entity's group.
    /// Returns the point index via handleIdx and the owning entity ID via
    /// entityId.  Returns true if a handle was hit.
    bool hitTestGroupHandle(const QPointF& worldPos, int& entityId, int& handleIdx) const;

    // Tangent circle helpers: use library result types directly
    using TangentCircle = geometry::TangentCircleResult;
    TangentCircle calculate2TangentCircle(const SketchEntity& e1, const SketchEntity& e2,
                                          const QPointF& hint) const;
    TangentCircle calculate3TangentCircle(const SketchEntity& e1, const SketchEntity& e2,
                                          const SketchEntity& e3) const;

    // Tangent arc helper: uses library result type directly
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

    // Transform section state. NOT the group's FixedPoint "pivot" handle
    // (that is a constraint); this is the point transforms are about.
    TransformPick m_transformPick = TransformPick::None;
    bool m_transformGlyphVisible = false;
    bool m_transformGlyphArc = false;
    QPointF m_transformPivot;
    bool m_transformPivotStored = false;     ///< Came from Group::pivot
    bool m_transformPivotUserSet = false;    ///< Placed/typed since the last reset: stored on the group at Apply
    bool m_transformPivotCleared = false;    ///< Home/Center asked to unset the stored pivot at Apply
    int  m_transformPivotGroupId = -1;
    bool m_transformPivotDragging = false;
    unsigned m_selectionRevision = 0;        ///< Bumped by every selection change
    unsigned m_transformStateRevision = 0;   ///< Revision the transform state was derived for
    QVector<SketchEntity> m_transformPreview;
    enum class FreeMoveHandle { None, Body, Ring };
    FreeMoveHandle m_freeMoveHandle = FreeMoveHandle::None;
    QPointF m_freeMoveStartWorld;
    double  m_freeMoveStartAngle = 0.0;
    QPointF m_freeMoveDelta;
    double  m_freeMoveAngle = 0.0;
    static constexpr int kTransformPivotGlyphPx = 16;
    static constexpr int kTransformPivotRotatePx = 28;
    static constexpr int kTransformPivotHitPx = 10;
    static constexpr int kFreeMoveRingPx = 48;
    static constexpr int kFreeMoveRingHitPx = 6;
    void ensureTransformStateCurrent();       ///< Re-derive pivot/preview when the selection changed underneath
    // Dragging. A handle drag is a dragged-point solve: the grabbed point
    // follows the cursor and the solver moves everything else as little as
    // the constraints require. A body drag translates the whole entity and
    // hands the solver its first two points. Direct drag: a press on
    // unselected geometry selects it and, once the cursor moves, drags it.
    // mouseMoveEvent decomposition (Phase A): the always-run preamble side
    // effects and the priority-ordered interaction guards, each a verbatim
    // extraction. A bool helper returns true when it owned the move (and has
    // already repainted), so mouseMoveEvent returns immediately after it.
    void updateHoverGrips(const QPointF& worldPos);
    void applySnapAndInference(const QPointF& worldPos, bool ctrlHeld, bool altHeld);
    void emitCursorPositions();
    bool handlePanMove(QMouseEvent* event);
    bool handleWindowSelectMove(const QPointF& worldPos);
    bool handleTransformPivotDragMove();
    bool handleFreeMoveDragMove(QMouseEvent* event, const QPointF& worldPos);
    void updateTransformGlyphCursor(QMouseEvent* event, const QPointF& worldPos);
    bool handleBackgroundDragMove(const QPointF& worldPos);
    void updateBackgroundEditCursor(const QPointF& worldPos);
    bool handleConstraintLabelDragMove(const QPointF& worldPos);
    bool promotePointPressToHandleDrag(QMouseEvent* event, const QPointF& worldPos);
    void armBodyDragIfPastThreshold(QMouseEvent* event);   // falls through by design
    bool handleBodyDragMove(const QPointF& worldPos);

    // The handle-drag path: one shared final-position step, then the
    // library's drag (applyHandleDrag) for every type but a line, which is a
    // dragged-point solve of its own; the type-gated solve tail stays in
    // mouseMoveEvent.
    QPointF computeHandleFinalPos(const SketchEntity* sel, const QPointF& worldPos,
                                  bool shiftPressed, bool ctrlPressed);
    void dragLineHandle(SketchEntity* sel, const QPointF& finalPos);
    void beginHandleDrag(int entityId, int handleIdx, const QPointF& worldPos, Qt::KeyboardModifiers mods);
    void pushConstraintAndEntityEdit(const SketchConstraint& oldConstraint, const SketchConstraint& newConstraint,
                                     const SketchEntity& oldEntity, const SketchEntity& newEntity,
                                     const std::string& description);
    void pushCompoundOrSingle(std::vector<sketch::UndoCommand>& subs, const std::string& description);
    void syncArcAfterSolve(int entityId);
    void finishUndoRedo(const sketch::UndoCommand& cmd);
    void finishUndoRedoMultiple();
    void removeEntityForUndo(int entityId);
    void removeConstraintForUndo(int constraintId);
    double lockedRadiusFor(int entityId) const;
    void clampArcSlotSweep(SketchEntity* sel, const QPointF& center, double radius);
    void commitPatternEntities(const std::vector<sketch::Entity>& entities, int nextId);
    void replaceEntityWithPieces(int entityId, const std::vector<SketchEntity>& pieces);
    void appendTempFixedPoint(int entityId, int pointIndex, std::vector<int>& tempIds);

    // Event-handler decomposition: contextMenuEvent, mousePressEvent,
    // keyPressEvent, paintEvent and setConstraintValue are dispatchers; the
    // helpers below each own one branch of the old body.
    // contextMenuEvent
    bool showPathSlotContextMenu(const QPoint& globalPos);
    bool showBezierContextMenu(const SketchEntity* sel, const QPointF& worldPos, const QPoint& globalPos);
    bool showSlotHandleContextMenu(const SketchEntity* sel, const QPointF& worldPos, const QPoint& globalPos);
    bool showConstraintContextMenu(int constraintId, const QPoint& globalPos);
    void showMultiSelectionContextMenu(const QPoint& globalPos);
    bool showEntityContextMenu(int entityId, const QPointF& worldPos, const QPoint& globalPos);
    void showCanvasContextMenu(const QPoint& globalPos);
    QStringList groupNamesForEntity(int eid) const;
    QSet<int> groupIdsForSelection(const QSet<int>& ids) const;
    void addGroupInfoLabel(QMenu& menu, const QSet<int>& entityIds);
    void addConstrainMenu(QMenu& menu);
    void addTransformAlignMenus(QMenu& menu);
    // mousePressEvent
    bool handleTransformPress(QMouseEvent* event, const QPointF& worldPos);
    bool handleCalibrationPress(const QPointF& worldPos);
    bool handleBackgroundEditPress(const QPointF& worldPos);
    void handleSelectToolPress(QMouseEvent* event, const QPointF& worldPos);
    bool pressSelectsHandle(QMouseEvent* event, const QPointF& worldPos);
    bool pressSelectsConstraint(QMouseEvent* event, const QPointF& worldPos);
    bool pressSelectsPoint(QMouseEvent* event, const QPointF& worldPos);
    void pressSelectsEntityOrWindow(QMouseEvent* event, const QPointF& worldPos);
    void handleDrawToolPress(QMouseEvent* event, const QPointF& worldPos);
    // keyPressEvent
    void handleInlineEditKey(QKeyEvent* event);
    void handleEscapeKey();
    void deleteSelectionKey();
    void cycleDimensionTypeHint(QKeyEvent* event);
    void quickDimensionKey();
    // paintEvent
    void drawToolPreviewDot(QPainter& painter);
    void updateConstraintRendererLayoutRects();
    void drawGroupSelectionHandles(QPainter& painter, const SketchEntity& sel);
    void drawDKeyHint(QPainter& painter);
    void drawWindowSelectionRect(QPainter& painter);
    // setConstraintValue
    bool applySweepAngleValue(SketchConstraint* constraint, const SketchConstraint& oldConstraint,
                              int constraintId, double newValue);
    bool applyTangentAngleValue(SketchConstraint* constraint, const SketchConstraint& oldConstraint,
                                int constraintId, double newValue);
    bool applyRadialValueDirect(SketchConstraint* constraint, const SketchConstraint& oldConstraint,
                                int constraintId, double newValue);
    int addTemporaryValuePins(const SketchConstraint* constraint);
    void solveConstraintsDragging(const std::vector<std::pair<int, int>>& draggedPoints);
    std::vector<std::pair<int, int>> m_draggedPoints;   ///< Consumed by the next solveConstraints()
    std::vector<std::pair<int, int>> m_dragWeightPoints; ///< [0009] far points held with a stiffness next solve
    double m_dragWeightStiffness = 1.0;                  ///< [0009] resistance for m_dragWeightPoints
    bool m_suppressNextContextMenu = false;  ///< A right-click that finished a spline/polygon must not also open the menu
        bool m_bodyDragArmed = false;      ///< Press on an entity body; becomes a drag past the threshold
    bool m_isDraggingBody = false;
    int m_bodyDragEntityId = -1;
    QPointF m_bodyDragPressWorld;
    QPointF m_bodyDragLastWorld;
    QPoint m_bodyDragPressScreen;
    QVector<SketchEntity> m_dragSnapshotEntities;        ///< Whole sketch before a body drag, for one compound undo
    QVector<SketchConstraint> m_dragSnapshotConstraints;
    void resetTransformStateForSelection();
    bool transformStarHit(const QPoint& screen) const;
    bool freeMoveRingHit(const QPoint& screen) const;
    bool freeMoveBodyHit(const QPointF& world) const;
    QRectF selectionWorldRect() const;
    /// Union of the entities' library bounding boxes; a null rect when none.
    QRectF worldBoundsOf(const QVector<int>& ids) const;
    void drawTransformPivot(QPainter& painter);
    void drawFreeMoveHandles(QPainter& painter);
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
