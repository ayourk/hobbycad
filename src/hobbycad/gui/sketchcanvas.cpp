// =====================================================================
//  src/hobbycad/gui/sketchcanvas.cpp — 2D Sketch canvas widget
// =====================================================================

#include "sketchcanvas.h"
#include "screenmath.h"
#include <hobbycad/units.h>
#include <cstdio>

#include <set>
#include "constraintglyphs.h"

#include "tools/arctoolhandler.h"
#include "tools/circletoolhandler.h"
#include "tools/constrainttoolhandler.h"
#include "tools/dimensiontoolhandler.h"
#include "tools/texttoolhandler.h"
#include "tools/polygontoolhandler.h"
#include "tools/rectangletoolhandler.h"
#include "tools/optoolhandlers.h"
#include "tools/simpletoolhandlers.h"
#include "tools/slottoolhandler.h"
#include "tools/splinetoolhandler.h"
#include "tools/ellipsetoolhandler.h"
#include "tools/pointtoolhandler.h"
#include "tools/linetoolhandler.h"
#include "tools/drawconstrainhandlers.h"
#include "tools/sketchtoolhandler.h"
#include "bindingsdialog.h"
#include "sketchsolver.h"
#include "sketchutils.h"

#include <hobbycad/parameters.h>
#include <hobbycad/sketch/decomposition.h>
#include <hobbycad/sketch/slotpath.h>
#include <hobbycad/sketch/export.h>
#include <hobbycad/sketch/profiles.h>
#include <hobbycad/sketch/queries.h>
#include <hobbycad/sketch/handles.h>
#include <hobbycad/sketch/patterns.h>
#include <hobbycad/sketch/operations.h>
#include <hobbycad/geometry/utils.h>
#include <hobbycad/sketch/bezier.h>
#include <hobbycad/sketch/align.h>
#include <hobbycad/geometry/intersections.h>

#include <QApplication>
#include <QEvent>
#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>
#include <QFileDialog>
#include <QInputDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "colorpicker.h"
#include <QMessageBox>
#include <QCursor>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QMenu>
#include <QtMath>

#include <algorithm>

namespace hobbycad {

namespace {
// --- On-canvas chrome geometry (all device pixels) --------------------------
// Cursor-trailing tool hint: the arrow cursor's hotspot is its top-left point
// and it extends down by roughly the theme cursor size, so the hint drops below
// that lower tip. kDefaultCursorSizePx is the X default used when XCURSOR_SIZE
// is unset; kCursorHintGapPx is the clearance between the cursor's lower edge
// and the hint's first line.
constexpr int kDefaultCursorSizePx = 24;
constexpr int kCursorHintGapPx     = 16;

// 2D scale bar layout. The px LENGTH thresholds are shared with the 3D bar in
// units.h (kScaleBar*Px); these are the 2D-only placement offsets.
constexpr int kScaleBarBottomMargin = 26;   // baseline above the widget bottom
constexpr int kScaleBarInset        = 10;   // left inset of the bar
constexpr int kScaleBarTickPx       = 6;    // end-tick height (above the line)

// Midpoint grips.
constexpr double kMidpointHitTolPx   = 8.0; // hover pick tolerance
constexpr int    kMidpointDotRadiusPx = 4;  // selected white dot radius

// On-canvas pick tolerances and layout offsets, in SCREEN PIXELS (each call
// site divides by m_zoom to reach world units). Co-located so pick
// sensitivity is tunable in one place; values are the historical per-site
// literals, left as-is (a maintainer may choose to unify them later).
constexpr double kPointPickTolPx       = 7.0;  // hitTestAnyPoint: any point
constexpr double kBezierLegPickTolPx   = 6.0;  // hitTestAnyPoint: bezier leg
constexpr double kProximityCoincTolPx  = 8.0;  // auto-coincidence proximity
constexpr double kDrawSnapSearchPx     = 10.0; // drawing-time snap search
constexpr double kEntityPickTolPx      = 5.0;  // hitTestEntity: curve/segment
constexpr double kHandlePickTolPx      = 6.0;  // hitTestHandle
constexpr double kGroupHandlePickTolPx = 6.0;  // hitTestGroupHandle
constexpr double kHitPadPx             = 6.0;  // bounds/selection hit padding
constexpr double kMinRubberBandPx      = 2.0;  // ignore sub-pixel drag rects
constexpr double kAngleLabelOffsetPx   = 30.0; // angle-dimension label offset
constexpr double kRadiusLabelOffsetPx  = 15.0; // radius/diameter label offset
constexpr double kBgHandleSizePx       = 10.0; // background-image resize handle
constexpr double kDrawDragThresholdPx  = 5.0;  // drawing: motion that counts as a drag

// Position-match epsilon shared by the snap/weld coincidence paths: two points
// closer than this (world units) are treated as the same placed point.
constexpr double kSnapWeldEps = 1e-6;
}  // namespace

SketchCanvas::SketchCanvas(QWidget* parent)
    : QWidget(parent)
    , m_constraintRenderer(*this)
    , m_snapEngine(*this)
    , m_entityRenderer(*this)
{
    // One handler per tool; the canvas dispatches to the active one.
    m_toolHandlers.push_back(std::make_unique<LineToolHandler>());
    // Draw-then-constrain variants. Point and Spline have none on purpose:
    // neither offers a typed dimension or an angle snap, so a variant would
    // differ in nothing. Every other tool falls back to its handler above.
    m_drawConstrainHandlers.push_back(std::make_unique<LineDrawConstrainHandler>());
    m_drawConstrainHandlers.push_back(std::make_unique<RectangleDrawConstrainHandler>());
    m_drawConstrainHandlers.push_back(std::make_unique<SlotDrawConstrainHandler>());
    m_drawConstrainHandlers.push_back(std::make_unique<CircleDrawConstrainHandler>());
    m_drawConstrainHandlers.push_back(std::make_unique<ArcDrawConstrainHandler>());
    m_drawConstrainHandlers.push_back(std::make_unique<PolygonDrawConstrainHandler>());
    m_drawConstrainHandlers.push_back(std::make_unique<EllipseDrawConstrainHandler>());
    m_toolHandlers.push_back(std::make_unique<ArcToolHandler>());
    m_toolHandlers.push_back(std::make_unique<PointToolHandler>());
    m_toolHandlers.push_back(std::make_unique<CircleToolHandler>());
    m_toolHandlers.push_back(std::make_unique<EllipseToolHandler>());
    m_toolHandlers.push_back(std::make_unique<RectangleToolHandler>());
    m_toolHandlers.push_back(std::make_unique<PolygonToolHandler>());
    m_toolHandlers.push_back(std::make_unique<SlotToolHandler>());
    m_toolHandlers.push_back(std::make_unique<SplineToolHandler>());
    m_toolHandlers.push_back(std::make_unique<ConstraintToolHandler>());
    m_toolHandlers.push_back(std::make_unique<DimensionToolHandler>());
    m_toolHandlers.push_back(std::make_unique<TextToolHandler>());
    for (auto& h : makeSimpleToolHandlers()) {
        m_toolHandlers.push_back(std::move(h));
    }
    for (auto& h : makeOperationToolHandlers()) {
        m_toolHandlers.push_back(std::move(h));
    }

    setObjectName(QStringLiteral("SketchCanvas"));
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);

    // Background and all sketch colors come from the active theme.
    setAutoFillBackground(true);
    applyTheme();

    // Initial zoom: 5 pixels per unit (so 10mm = 50 pixels)
    m_zoom = 5.0;

    // Expression evaluator for formula input in dimension fields
    m_paramEngine = new ParameterEngine();

    // Dimension input subsystem (fields, typing, on-canvas render).
    m_dimInput.setHost(this);
    m_dimInput.setParameterEngine(m_paramEngine);
    m_dimInput.setDisplayUnit(m_displayUnit);

    // Cursor-trailing tool hints: on unless the user turned them off. Read once
    // here; MainWindow::applyPreferences pushes live changes via the setter.
    m_showCursorHints = QSettings()
        .value(QStringLiteral("preferences/showCursorHints"), true).toBool();

    // Load key bindings from settings
    loadKeyBindings();
}

void SketchCanvas::setShowCursorHints(bool show)
{
    if (m_showCursorHints == show) return;
    m_showCursorHints = show;
    update();
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
        cancelTransformPick();
        clearTransformPreview();

        // Cancel any in-progress constraint creation
        if (m_isCreatingConstraint) {
            finishConstraintCreation();
        }

        // Cancel any in-progress drawing when switching tools
        m_isDrawing = false;

        // Clear tangent targets when switching tools
        m_tangentTargets.clear();

        // Leave any entered group when switching tools
        if (m_enteredGroupId >= 0)
            m_enteredGroupId = -1;

        m_activeTool = tool;
        m_toolConstraintType = -1;   // a manual tool switch leaves per-type mode

        // "Select, then choose the constraint tool": if geometry is already
        // selected when the Constraint tool is picked, apply the inferred
        // constraint to it now instead of waiting for fresh clicks.
        if (tool == SketchTool::Constraint
            && (m_selectedPoints.size() >= 2 || selectionCount() >= 2)) {
            applyInferredConstraint();
        }

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

    // Announce the tool's first-stage hint immediately, before any click.
    // initDimFields() only runs on stage transitions, so without this the
    // status bar would stay silent until the user had already clicked.
    emit toolHintChanged(currentToolHint());
}

void SketchCanvas::setCreationMode(CreationMode mode)
{
    // Map CreationMode to internal mode enums
    // Note: CreationMode values overlap because different tools reuse value 0
    // The mode must be interpreted based on the active tool

    int modeValue = static_cast<int>(mode);

    // Switching mode mid-entity normally cancels it, because the points
    // already placed were placed to mean something the new mode may read
    // differently. A tool that knows the switch is harmless says so.
    bool keepDrawing = false;
    if (m_isDrawing) {
        if (SketchToolHandler* h = activeHandler()) {
            keepDrawing = h->canSwitchModeWhileDrawing(*this, modeValue);
        }
    }
    if (!keepDrawing) {
        m_tangentTargets.clear();
        if (m_isDrawing) {
            cancelEntity();
        }
    }

    // Each tool maps its creation sub-modes in its own handler's
    // applyCreationMode(); a tool with no sub-modes simply declines. There is
    // no legacy per-tool switch any more.
    if (SketchToolHandler* h = activeHandler()) {
        h->applyCreationMode(*this, modeValue);
    }
}

void SketchCanvas::setSketchPlane(SketchPlane plane)
{
    m_plane = plane;
    update();
}

bool SketchCanvas::isEntityLocked(int entityId) const
{
    const SketchEntity* e = entityById(entityId);
    if (!e || e->groupId < 0) return false;
    std::vector<sketch::Group> gs;
    gs.reserve(m_groups.size());
    for (const SketchGroup& g : m_groups)
        gs.push_back(static_cast<const sketch::Group&>(g));
    return sketch::isGroupChainLocked(e->groupId, gs);
}

bool SketchCanvas::applyPointEdit(int entityId, int pointIndex, const Point3& p)
{
    SketchEntity* entity = entityById(entityId);
    if (!entity || pointIndex < 0 || pointIndex >= entity->points.size())
        return false;
    if (isEntityLocked(entityId)) {
        emit toolHintChanged(
            tr("This entity is in a locked group; unlock the group to edit it."));
        return false;
    }
    const SketchEntity oldEntity = *entity;
    entity->points[pointIndex] = p;
    pushUndoCommand(sketch::UndoCommand::modifyEntity(
        oldEntity, *entity, "Edit coordinate"));
    // Keep the edited point fixed where placed while the rest re-solves.
    notifyEntityPointChanged(entityId, pointIndex);
    return true;
}

void SketchCanvas::setSketchMode(bool threeD)
{
    if (m_is3D == threeD) return;
    m_is3D = threeD;
    update();
    emit sketchModeChanged(m_is3D);
    // Re-emit the current selection so the properties panel relabels its
    // coordinate fields and reveals/hides the off-plane one immediately.
    emit selectionChanged(m_selectedId);
}

void SketchCanvas::setFlipView(bool flipped)
{
    if (m_flipView == flipped) return;
    m_flipView = flipped;
    update();
    emit flipViewChanged(m_flipView);
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
    m_dimInput.setDisplayUnit(unit);
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
    return entityById(m_selectedId);
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
    ++m_selectionRevision;
    for (auto& e : m_entities) {
        e.selected = false;
    }
    m_selectedId = -1;
    m_selectedPoints.clear();
    m_selectedMidpointEntity = -1;
    m_hoverMidpointEntity = -1;
    m_hoverSlotEntity = -1;
    m_selectedSlotAnchor = { -1, -1 };
    selectClear();

    // Also clear constraint selection
    for (auto& c : m_constraints) {
        c.selected = false;
    }
    m_selectedConstraintId = -1;

    // Clear fixed handle
    m_fixedHandleIndex = -1;

    // Reset D-key constraint type cycling
    m_dKeyTypeIndex = 0;
    m_dKeyTypeHint.clear();

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
    // When inside a group, don't expand: we want individual selection
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
            selectAdd(entity.id);
            // Keep the clicked entity as the primary selection (the panel shows
            // the primary's properties); adopt a sibling only when there is no
            // primary yet.
            if (m_selectedId < 0) m_selectedId = entity.id;
        }
    }
}

// -----------------------------------------------------------------------
//  Enter / Leave group
// -----------------------------------------------------------------------
void SketchCanvas::enterGroup(int groupId)
{
    ++m_selectionRevision;
    // Verify the group exists
    bool found = false;
    for (const SketchGroup& g : m_groups) {
        if (g.id == groupId) { found = true; break; }
    }
    if (!found) return;

    m_enteredGroupId = groupId;

    // Clear current selection; user will click individual members next
    clearSelection();
    update();
}

void SketchCanvas::leaveGroup()
{
    ++m_selectionRevision;
    if (m_enteredGroupId < 0) return;

    int prevGroup = m_enteredGroupId;
    m_enteredGroupId = -1;

    // Select the whole group again so the user sees what they were in
    for (auto& entity : m_entities) {
        if (entity.groupId == prevGroup) {
            entity.selected = true;
            selectAdd(entity.id);
            m_selectedId = entity.id;
        }
    }

    emit selectionChanged(m_selectedId);
    update();
}

void SketchCanvas::setConstraintToolType(int type)
{
    clearSelection();
    setActiveTool(SketchTool::Constraint);   // resets m_toolConstraintType to -1
    m_toolConstraintType = type;             // then arm the chosen type
    update();
}

bool SketchCanvas::hitTestAnyPoint(const QPointF& worldPos, int& entityId, int& pointIndex) const
{
    const double tol = kPointPickTolPx / m_zoom;
    double best = tol;
    bool found = false;
    for (const auto& e : m_entities) {
        // Only points that are meaningful constraint targets: real endpoints /
        // centers. Text and dimension entities are skipped.
        if (e.type == SketchEntityType::Text || e.type == SketchEntityType::Dimension) continue;
        for (int i = 0; i < e.points.size(); ++i) {
            const double d = QLineF(QPointF(e.points[i]), worldPos).length();
            if (d < best) { best = d; entityId = e.id; pointIndex = i; found = true; }
        }
    }
    return found;
}

bool SketchCanvas::hitTestBezierLeg(const QPointF& worldPos, int& entityId,
                                    int& i0, int& i1) const
{
    const double tol = kBezierLegPickTolPx / m_zoom;
    double best = tol;
    bool found = false;
    const SketchEntity* primary = selectedEntity();
    for (const auto& e : m_entities) {
        if (e.type != SketchEntityType::Spline || !e.splineBezier) continue;
        bool showing = (primary && primary->id == e.id);
        if (!showing)
            for (const auto& pr : m_selectedPoints) if (pr.first == e.id) { showing = true; break; }
        if (!showing) continue;
        const int n = static_cast<int>(e.points.size());
        for (int i = 0; i + 1 < n; ++i) {
            const QPointF a(e.points[i]), b(e.points[i + 1]);
            const QPointF proj = geometry::closestPointOnSegment(worldPos, a, b);
            const double d = QLineF(proj, worldPos).length();
            if (d < best) { best = d; entityId = e.id; i0 = i; i1 = i + 1; found = true; }
        }
    }
    return found;
}

void SketchCanvas::selectPoint(int entityId, int pointIndex, bool addToSelection, bool toggle)
{
    ++m_selectionRevision;
    const QPair<int,int> pt(entityId, pointIndex);
    if (!addToSelection && !toggle) {
        // Plain click: replace everything with just this point.
        clearSelection();
        m_selectedPoints.clear();
        m_selectedPoints.append(pt);
    } else if (toggle) {
        const int idx = m_selectedPoints.indexOf(pt);
        if (idx >= 0) m_selectedPoints.remove(idx);
        else          m_selectedPoints.append(pt);
    } else {  // add-only (Shift)
        if (!m_selectedPoints.contains(pt)) m_selectedPoints.append(pt);
    }
    emit selectionChanged(m_selectedId);
    update();
}

bool SketchCanvas::selectedBezierAnchor(int& splineId, int& anchorIdx) const
{
    if (m_selectedPoints.size() != 1) return false;
    const auto sp = m_selectedPoints[0];
    const SketchEntity* e = entityById(sp.first);
    if (!e || e->type != SketchEntityType::Spline || !e->splineBezier) return false;
    if (sp.second < 0 || sp.second >= static_cast<int>(e->points.size())) return false;
    if (sp.second % 3 != 0) return false;
    splineId = sp.first; anchorIdx = sp.second;
    return true;
}

bool SketchCanvas::bezierAnchorProps(int splineId, int a, double& angleDeg,
                                     double& inLen, double& outLen, double& weight,
                                     bool& rational) const
{
    const SketchEntity* e = entityById(splineId);
    sketch::BezierAnchorInfo info;
    if (!e || !sketch::bezierAnchorInfo(*e, a, info)) return false;
    angleDeg = info.angleDeg; inLen = info.inLen; outLen = info.outLen;
    weight = info.weight; rational = info.rational;
    return true;
}

// The Bezier edits below are the library's (sketch/bezier.h); the canvas
// owns the entity lookup, the undo record, selection and the redraw.
void SketchCanvas::setBezierAnchorAngle(int splineId, int a, double angleDeg)
{
    SketchEntity* e = entityById(splineId);
    if (!e) return;
    const SketchEntity oldE = *e;
    if (!sketch::setBezierAnchorAngle(*e, a, angleDeg)) return;
    pushUndoCommand(sketch::UndoCommand::modifyEntity(oldE, *e));
    solveConstraints(); update();
}

void SketchCanvas::setBezierAnchorHandleLen(int splineId, int a, bool outHandle, double len)
{
    SketchEntity* e = entityById(splineId);
    if (!e) return;
    const SketchEntity oldE = *e;
    if (!sketch::setBezierAnchorHandleLength(*e, a, outHandle, len)) return;
    pushUndoCommand(sketch::UndoCommand::modifyEntity(oldE, *e));
    solveConstraints(); update();
}

void SketchCanvas::setBezierAnchorWeight(int splineId, int a, double weight)
{
    SketchEntity* e = entityById(splineId);
    if (!e) return;
    const SketchEntity oldE = *e;
    if (!sketch::setBezierAnchorWeight(*e, a, weight)) return;
    pushUndoCommand(sketch::UndoCommand::modifyEntity(oldE, *e));
    solveConstraints(); update();
}

// Constraints naming this spline's control points follow an insertion or a
// removal; the rule is the library's, the container is the canvas's.
static void remapSplineConstraints(QVector<SketchConstraint>& cons, int splineId, int lo, int count)
{
    for (auto it = cons.begin(); it != cons.end();) {
        if (sketch::remapSplinePointIndices(*it, splineId, lo, count)) it = cons.erase(it);
        else ++it;
    }
}

bool SketchCanvas::deleteBezierAnchor(int splineId, int a)
{
    SketchEntity* e = entityById(splineId);
    if (!e) return false;
    const SketchEntity oldE = *e;
    const int lo = sketch::deleteBezierAnchor(*e, a);
    if (lo < 0) return false;
    pushUndoCommand(sketch::UndoCommand::modifyEntity(oldE, *e));
    remapSplineConstraints(m_constraints, splineId, lo, 3);
    m_selectedPoints.clear();
    m_profilesCacheDirty = true;
    solveConstraints(); update();
    return true;
}

bool SketchCanvas::insertBezierFitPoint(int splineId, const QPointF& worldPos)
{
    SketchEntity* e = entityById(splineId);
    if (!e) return false;
    const SketchEntity oldE = *e;
    const int at = sketch::insertBezierFitPoint(*e, worldPos);
    if (at < 0) return false;
    pushUndoCommand(sketch::UndoCommand::modifyEntity(oldE, *e));
    remapSplineConstraints(m_constraints, splineId, at, -3);
    m_profilesCacheDirty = true;
    solveConstraints(); update();
    return true;
}

bool SketchCanvas::selectedBezierLeg(int& splineId, int& i0, int& i1) const
{
    if (m_selectedPoints.size() != 2) return false;
    const auto a = m_selectedPoints[0];
    const auto b = m_selectedPoints[1];
    if (a.first != b.first) return false;
    const SketchEntity* e = entityById(a.first);
    if (!e || e->type != SketchEntityType::Spline || !e->splineBezier) return false;
    int lo = a.second, hi = b.second;
    if (lo > hi) std::swap(lo, hi);
    if (hi - lo != 1) return false;
    if (lo < 0 || hi >= static_cast<int>(e->points.size())) return false;
    splineId = a.first; i0 = lo; i1 = hi;
    return true;
}

void SketchCanvas::setBezierLegLength(int splineId, int i0, int i1, double len)
{
    SketchEntity* e = entityById(splineId);
    if (!e) return;
    const SketchEntity oldE = *e;
    if (!sketch::setBezierLegLength(*e, i0, i1, len)) return;
    pushUndoCommand(sketch::UndoCommand::modifyEntity(oldE, *e));
    solveConstraints(); update();
}

void SketchCanvas::toggleBezierClosed(int splineId)
{
    SketchEntity* e = entityById(splineId);
    if (!e) return;
    const SketchEntity oldE = *e;
    if (!sketch::toggleBezierClosed(*e)) return;
    pushUndoCommand(sketch::UndoCommand::modifyEntity(oldE, *e));
    m_profilesCacheDirty = true;
    solveConstraints(); update();
}

bool SketchCanvas::dimensionSelectedLinesAngle()
{
    const std::vector<int> ids = selectedEntityList();
    if (ids.size() != 2) {
        showStatus(tr("Select two lines to dimension the angle between them."));
        return false;
    }
    const SketchEntity* la = entityById(ids[0]);
    const SketchEntity* lb = entityById(ids[1]);
    sketch::AngleDimension dim;
    const auto problem = (la && lb) ? sketch::angleDimensionBetweenLines(*la, *lb, dim)
                                    : sketch::AngleDimensionProblem::NotTwoLines;
    switch (problem) {
    case sketch::AngleDimensionProblem::NotTwoLines:
        showStatus(tr("The angle dimension needs two line segments."));
        return false;
    case sketch::AngleDimensionProblem::Degenerate:
        return false;
    case sketch::AngleDimensionProblem::Parallel:
        showStatus(tr("These lines are parallel; there is no angle to dimension."));
        return false;
    case sketch::AngleDimensionProblem::None:
        break;
    }
    const double value = dim.value;
    const bool supp = dim.supplementary;
    QPointF labelPos = QPointF(dim.vertex) + QPointF(0, -12);
    if (dim.hasBisector)
        labelPos = QPointF(dim.vertex) + QPointF(dim.bisector) * (kAngleLabelOffsetPx / m_zoom);
    m_constraintTargetEntities.clear(); m_constraintTargetPoints.clear();
    m_constraintTargetEntities.append(ids[0]);
    m_constraintTargetEntities.append(ids[1]);
    createConstraint(ConstraintType::Angle, value, labelPos,
                     false, true, true, supp);
    m_constraintTargetEntities.clear();
    return true;
}

void SketchCanvas::setAngleSideFromLabel(SketchConstraint* c)
{
    if (!c || c->type != ConstraintType::Angle || c->entityIds.size() < 2) return;
    const SketchEntity* e1 = entityById(c->entityIds[0]);
    const SketchEntity* e2 = entityById(c->entityIds[1]);
    if (!e1 || !e2 || e1->type != SketchEntityType::Line || e2->type != SketchEntityType::Line
        || e1->points.size() < 2 || e2->points.size() < 2) return;
    QPointF vertex;
    if (c->hasAnchorPoint()) vertex = c->anchorPoint;
    else {
        QLineF l1(e1->points[0], e1->points[1]), l2(e2->points[0], e2->points[1]);
        if (l1.intersects(l2, &vertex) == QLineF::NoIntersection) vertex = c->labelPosition;
    }
    double value = 0.0;
    bool supplementary = false;
    if (!sketch::angleFromLabelSide(*e1, *e2, vertex, c->labelPosition, value, supplementary)) return;
    c->value = value;
    c->supplementary = supplementary;
}

void SketchCanvas::createProximityCoincidences(int newEntityId)
{
    const double tol = kProximityCoincTolPx / m_zoom;
    const std::vector<sketch::EndpointPair> welds = sketch::proximityWelds(
        toLibraryEntities(m_entities), toLibraryConstraints(m_constraints), newEntityId, tol);
    std::vector<sketch::UndoCommand> subs;
    for (const sketch::EndpointPair& w : welds) {
        subs.push_back(makeConstraint(ConstraintType::Coincident,
                                      { w.entityA, w.entityB }, { w.pointA, w.pointB }));
    }
    if (!subs.empty())
        pushUndoCommand(sketch::UndoCommand::compound(subs, "Weld corners"));
}

void SketchCanvas::pruneOrphanedConstraints()
{
    auto exists = [&](int id) {
        if (id == sketch::kSketchOriginEntity) return true;   // origin sentinel
        for (const auto& e : m_entities) if (e.id == id) return true;
        return false;
    };
    const int before = m_constraints.size();
    m_constraints.erase(
        std::remove_if(m_constraints.begin(), m_constraints.end(),
            [&](const SketchConstraint& c) {
                for (int id : c.entityIds) if (!exists(id)) return true;
                return false;
            }),
        m_constraints.end());
    if (m_constraints.size() != before) {
        // Drop those ids from any group's constraint list too.
        for (SketchGroup& g : m_groups) {
            g.constraintIds.erase(
                std::remove_if(g.constraintIds.begin(), g.constraintIds.end(),
                    [this](int cid) {
                        return std::none_of(m_constraints.begin(), m_constraints.end(),
                            [cid](const SketchConstraint& c){ return c.id == cid; });
                    }),
                g.constraintIds.end());
        }
    }
}

int SketchCanvas::nearestCircleOrArc(const QPointF& worldPos) const
{
    return sketch::nearestCircleOrArc(toLibraryEntities(m_entities), worldPos);
}

int SketchCanvas::hitTestTangentContact(const QPointF& worldPos) const
{
    // The red dot drawn by drawOffSegmentTangents() sits on a circle's
    // perimeter where a line ALREADY tangent to it (a Tangent constraint)
    // would touch if the too-short segment were extended. It is a live paint
    // marker, not an entity, so a plain hitTest never returns it. Clicking it
    // selects the underlying circle, so the user can then pick a line endpoint
    // and apply Coincident to pin the endpoint onto the circle (Aaron).
    const QPointF clickScr(worldToScreen(worldPos));
    const double tolPx = 8.0;
    for (const SketchConstraint& c : m_constraints) {
        if (!c.enabled || c.type != ConstraintType::Tangent
            || c.entityIds.size() < 2) continue;
        const SketchEntity* e1 = entityById(c.entityIds[0]);
        const SketchEntity* e2 = entityById(c.entityIds[1]);
        if (!e1 || !e2) continue;
        const SketchEntity* line =
            (e1->type == SketchEntityType::Line) ? e1
          : (e2->type == SketchEntityType::Line) ? e2 : nullptr;
        const SketchEntity* circle =
            (e1->type == SketchEntityType::Circle) ? e1
          : (e2->type == SketchEntityType::Circle) ? e2 : nullptr;
        if (!line || !circle) continue;
        auto pt = sketch::offSegmentTangentPoint(*line, *circle);
        if (!pt) continue;
        const QPointF scr(worldToScreen(QPointF(pt->x, pt->y)));
        if (QLineF(clickScr, scr).length() <= tolPx)
            return circle->id;
    }
    return -1;
}

bool SketchCanvas::entityMidpoint(const SketchEntity& e, QPointF& out) const
{
    if ((e.type == SketchEntityType::Line && e.points.size() >= 2) ||
        (e.type == SketchEntityType::Arc && e.points.size() >= 3)) {
        out = sketch::pointAtParameter(e, 0.5);
        return true;
    }
    return false;
}

int SketchCanvas::hitTestMidpoint(const QPointF& worldPos) const
{
    const QPointF clickScr(worldToScreen(worldPos));
    const double tolPx = kMidpointHitTolPx;
    int best = -1; double bestD = tolPx;
    for (const SketchEntity& e : m_entities) {
        QPointF mid;
        if (!entityMidpoint(e, mid)) continue;
        const QPointF scr(worldToScreen(mid));
        const double d = QLineF(clickScr, scr).length();
        if (d <= bestD) { bestD = d; best = e.id; }
    }
    return best;
}

void SketchCanvas::solveHandleDragStabilized(int dragEntityId, int dragPointIndex,
                                             const QPointF& dragPos)
{
    const std::vector<std::pair<int, int>> dragged{{dragEntityId, dragPointIndex}};
    // Only stabilize points the SOLVER registers a handle for, so we never weight
    // or FixedPoint-pin a point it cannot resolve (that logged
    // "FixedPoint ...: failed to resolve point handle, skipping"). Circles
    // register only their center; decomposed/derived entities register none of
    // their own points.
    auto registersPoint = [](SketchEntityType t, int i) -> bool {
        switch (t) {
        case SketchEntityType::Line:   return i == 0 || i == 1;
        case SketchEntityType::Arc:    return i >= 0 && i <= 2;
        case SketchEntityType::Circle: return i == 0;   // center only
        case SketchEntityType::Point:  return i == 0;
        case SketchEntityType::Spline: return true;     // control points
        default:                       return false;
        }
    };
    // Do NOT stabilize points that belong to the SAME group as the dragged
    // entity: a decomposed shape (slot, sweep) carries its own internal
    // constraints (tangent, coincident, equal), so its other points must move
    // as those require; hard-fixing them all pins the shape and makes the
    // tangent unsatisfiable (DOF 0, "inconsistent"), which is the drag that
    // moved then snapped back. Loose geometry (groupId < 0, e.g. a single
    // tangent arc) is still stabilized, preserving the original anti-collapse
    // fix. (Aaron)
    const SketchEntity* dragEnt = entityById(dragEntityId);
    const int dragGroup = dragEnt ? dragEnt->groupId : -1;
    auto sameGroup = [&](const SketchEntity& e) {
        return dragGroup >= 0 && e.groupId == dragGroup;
    };
#if defined(SLVS_HAS_DRAG_WEIGHTS)
    // Per-param drag weights: hold the far points stiff (they yield only as the
    // constraints require) while the grabbed handle moves.
    std::vector<std::pair<int, int>> farPts;
    for (const auto& e : m_entities) {
        if (e.id == dragEntityId) continue;   // the grabbed entity reshapes freely
        if (sameGroup(e)) continue;           // same decomposed shape: let its constraints govern
        for (int i = 0; i < static_cast<int>(e.points.size()); ++i) {
            if (!registersPoint(e.type, i)) continue;
            if (QLineF(QPointF(e.points[i]), dragPos).length() < geometry::kDegenerateLen) continue;
            farPts.push_back({e.id, i});
        }
    }
    // Opening a full circle: also weight the arc's OWN end left at the cut
    // (normally skipped as the dragged entity) so it resists drifting.
    if (m_openingFullArc && dragEnt && dragEnt->type == SketchEntityType::Arc) {
        farPts.push_back({ dragEntityId, (m_openArcDraggedIndex == 1) ? 2 : 1 });
    }
    m_dragWeightPoints = farPts;
    m_dragWeightStiffness = 400.0;
    solveConstraintsDragging(dragged);
#else
    // No per-param weights compiled in here (the slvs macro is not visible in
    // this TU): temporarily HARD-FIX the far, non-welded points so the grabbed
    // handle moves and the rest stays put; an under-constrained tangent arc
    // then CANNOT be pulled to a collapsed radius. Removed the instant the solve
    // returns, so the persisted sketch DOF is never changed. Mirrors the
    // body-drag stabilization. (Aaron)
    std::vector<int> tempIds;
    for (const auto& e : m_entities) {
        if (e.id == dragEntityId) continue;
        if (sameGroup(e)) continue;           // same decomposed shape: let its constraints govern
        for (int i = 0; i < static_cast<int>(e.points.size()); ++i) {
            if (!registersPoint(e.type, i)) continue;
            if (QLineF(QPointF(e.points[i]), dragPos).length() < geometry::kDegenerateLen) continue;
            appendTempFixedPoint(e.id, i, tempIds);
        }
    }
    // Opening a full circle: also pin the arc's OWN end left at the cut.
    if (m_openingFullArc && dragEnt && dragEnt->type == SketchEntityType::Arc) {
        SketchConstraint fx;
        fx.id = -1000000 - static_cast<int>(tempIds.size());
        fx.type = ConstraintType::FixedPoint;
        fx.entityIds = { dragEntityId };
        fx.pointIndices = { (m_openArcDraggedIndex == 1) ? 2 : 1 };
        fx.isDriving = true; fx.enabled = true; fx.satisfied = true;
        m_constraints.append(fx);
        tempIds.push_back(fx.id);
    }
    solveConstraintsDragging(dragged);
    if (!tempIds.empty())
        m_constraints.erase(
            std::remove_if(m_constraints.begin(), m_constraints.end(),
                [](const SketchConstraint& c) { return c.id <= -1000000; }),
            m_constraints.end());
#endif
}

FILE* SketchCanvas::debugLogFile()
{
    // A permanent diagnostic facility, but OFF unless the environment asks for
    // it, so an installed build never writes stray files into the user's
    // working directory. Resolution, in order (evaluated once):
    //   HOBBYCAD_DEBUG_LOG=<path>  explicit log file (also enables logging)
    //   HOBBYCAD_DEBUG=1           enable at a default path: build/DEBUG.log
    //                              when launched from a dev tree (a writable
    //                              ./build exists, the project convention),
    //                              otherwise <cache>/HobbyCAD/debug.log
    //   neither set                disabled -> nullptr, every dbgLog() a no-op
    // build-dev.sh exports HOBBYCAD_DEBUG=1 so dev runs log to build/DEBUG.log
    // exactly as before, with no filesystem side effect in production.
    static FILE* f = []() -> FILE* {
        QString path = qEnvironmentVariable("HOBBYCAD_DEBUG_LOG");
        if (path.isEmpty()) {
            if (qEnvironmentVariableIntValue("HOBBYCAD_DEBUG") <= 0)
                return nullptr;
            if (QFileInfo(QStringLiteral("build")).isDir()) {
                path = QStringLiteral("build/DEBUG.log");
            } else {
                const QString dir = QStandardPaths::writableLocation(
                    QStandardPaths::CacheLocation);
                if (dir.isEmpty()) return nullptr;
                QDir().mkpath(dir);
                path = dir + QStringLiteral("/debug.log");
            }
        }
        FILE* fp = std::fopen(path.toLocal8Bit().constData(), "w");
        if (fp)
            std::fprintf(stderr, "HobbyCAD: debug log -> %s\n",
                         path.toLocal8Bit().constData());
        return fp;
    }();
    return f;
}

void SketchCanvas::dbgLog(const char* where)
{
    FILE* f = debugLogFile();
    if (!f) return;
    auto typeName = [](SketchEntityType t) -> const char* {
        switch (t) {
        case SketchEntityType::Point:         return "Point";
        case SketchEntityType::Line:          return "Line";
        case SketchEntityType::Rectangle:     return "Rectangle";
        case SketchEntityType::Parallelogram: return "Parallelogram";
        case SketchEntityType::Circle:        return "Circle";
        case SketchEntityType::Arc:           return "Arc";
        case SketchEntityType::Spline:        return "Spline";
        case SketchEntityType::Polygon:       return "Polygon";
        case SketchEntityType::Slot:          return "Slot";
        case SketchEntityType::Ellipse:       return "Ellipse";
        case SketchEntityType::Text:          return "Text";
        default:                              return "Entity";
        }
    };
    auto roleName = [](const SketchEntity& e, int i) -> const char* {
        switch (e.type) {
        case SketchEntityType::Line:    return i == 0 ? "start" : "end";
        case SketchEntityType::Arc:     return i == 0 ? "center" : (i == 1 ? "start" : "end");
        case SketchEntityType::Circle:  return i == 0 ? "center" : "quad";
        case SketchEntityType::Ellipse: return i == 0 ? "center" : (i == 1 ? "major" : "minor");
        case SketchEntityType::Polygon: return i == 0 ? "center" : "radius";
        case SketchEntityType::Rectangle:
        case SketchEntityType::Parallelogram: return "corner";
        case SketchEntityType::Slot:    return i < 2 ? "axis-end" : "extent";
        case SketchEntityType::Spline:
            if (e.splineBezier) return (i % 3 == 0) ? "anchor" : "handle";
            return "fit-pt";
        default:                        return "pt";
        }
    };
    std::fprintf(f, "== %s ==\n", where);
    for (const auto& e : m_entities) {
        std::fprintf(f, "  %s id=%d%s:\n", typeName(e.type), e.id,
                     e.isConstruction ? " (construction)" : "");
        for (int i = 0; i < static_cast<int>(e.points.size()); ++i) {
            // Sketch points are 2D in the plane; z is the plane-local 0.
            std::fprintf(f, "    [%d] %-8s x=%.4f y=%.4f z=%.4f\n",
                         i, roleName(e, i), e.points[i].x, e.points[i].y, 0.0);
        }
        if (e.type == SketchEntityType::Arc || e.type == SketchEntityType::Circle) {
            const double rr = (e.points.size() >= 2)
                ? geometry::lineLength(e.points[0], e.points[1])
                : e.radius;
            std::fprintf(f, "    radius=%.4f (field=%.4f) start=%.2f sweep=%.2f\n",
                         rr, e.radius, e.startAngle, e.sweepAngle);
        }
    }
    int nTangent = 0;
    for (const auto& c : m_constraints)
        if (c.type == ConstraintType::Tangent) ++nTangent;
    std::fprintf(f, "  constraints=%d (tangent=%d) freePtsValid=%d nFree=%zu dragWeightPts=%zu\n\n",
                 static_cast<int>(m_constraints.size()), nTangent,
                 static_cast<int>(m_freePointsValid), m_freePoints.size(),
                 m_dragWeightPoints.size());
    std::fflush(f);
}

void SketchCanvas::selectEntity(int entityId, bool addToSelection,
                                bool individualOnly)
{
    ++m_selectionRevision;
    if (!addToSelection) {
        // Clear existing selection
        for (auto& e : m_entities) {
            e.selected = false;
        }
        selectClear();

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
            selectClear();
            m_selectedId = -1;
        }

        // Determine whether this click should be individual
        bool isIndividual = individualOnly || (m_enteredGroupId >= 0);

        if (addToSelection && entity->selected) {
            // Ctrl+click on already selected entity: deselect it and
            // its group siblings (unless individual mode).
            entity->selected = false;
            selectRemove(entityId);
            if (!isIndividual && entity->groupId >= 0) {
                for (auto& e : m_entities) {
                    if (e.groupId == entity->groupId) {
                        e.selected = false;
                        selectRemove(e.id);
                    }
                }
            }
            // Update primary selection
            if (m_selectedId == entityId) {
                m_selectedId = m_selectedIds.isEmpty() ? -1 : *m_selectedIds.begin();
            }
        } else {
            entity->selected = true;
            selectAdd(entityId);
            m_selectedId = entityId;  // Primary selection is the last clicked
            // Expand to group siblings unless in individual mode
            if (!isIndividual) {
                expandSelectionToGroups();
            }
        }
    }

    // Reset D-key constraint type cycling on selection change
    m_dKeyTypeIndex = 0;
    m_dKeyTypeHint.clear();

    emit selectionChanged(m_selectedId);
    update();
}

void SketchCanvas::selectEntitiesInRect(const QRectF& rect, bool crossing, bool addToSelection)
{
    ++m_selectionRevision;
    if (!addToSelection) {
        // Clear existing selection
        for (auto& e : m_entities) {
            e.selected = false;
        }
        selectClear();
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
            selectAdd(entity.id);
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

    // Entities joined end to end (points and rectangle corners included).
    constexpr double kChainJoinTol = 0.01;
    for (int id : sketch::findConnectedChain(startEntityId,
                                             toLibraryEntities(m_entities),
                                             kChainJoinTol)) {
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

    // Library names for every type; translation stays with this caller.
    QString typeName = tr(sketch::constraintTypeName(c->type));

    // For dimensional constraints, include the value
    QString description = typeName;
    switch (c->type) {
    case ConstraintType::Distance:
        description += QStringLiteral(" = ") + QString::fromStdString(formatValueWithUnit(c->value, m_displayUnit));
        break;
    case ConstraintType::Radius:
        description += QStringLiteral(" R") + QString::fromStdString(formatValueWithUnit(c->value, m_displayUnit));
        break;
    case ConstraintType::Diameter:
        description += QStringLiteral(" Ø") + QString::fromStdString(formatValueWithUnit(c->value, m_displayUnit));
        break;
    case ConstraintType::Angle:
    case ConstraintType::FixedAngle:
        description += QStringLiteral(" = ") + QString::fromStdString(formatAngle(c->value));
        break;
    default:
        break;
    }

    // Add entity information if available
    if (!c->entityIds.empty()) {
        QStringList entityNames;
        for (int entityId : c->entityIds) {
            const SketchEntity* entity = entityById(entityId);
            if (entity) {
                const QString entityType = tr(sketch::entityTypeName(entity->type));
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

void SketchCanvas::setSketchContents(const QVector<SketchEntity>& entities,
                                     const QVector<SketchConstraint>& constraints,
                                     const QVector<SketchGroup>& groups)
{
    m_entities = entities;
    m_constraints = constraints;
    m_groups = groups;
    m_enteredGroupId = -1;
    m_selectedId = -1;
    m_selectedConstraintId = -1;

    // Re-seed every id counter past the highest restored id, or newly
    // created objects would collide with the ones just loaded.
    int maxEntityId = 0;
    for (const SketchEntity& e : entities)
        if (e.id > maxEntityId) maxEntityId = e.id;
    m_nextId = maxEntityId + 1;

    int maxConstraintId = 0;
    for (const SketchConstraint& c : constraints)
        if (c.id > maxConstraintId) maxConstraintId = c.id;
    m_nextConstraintId = maxConstraintId + 1;

    int maxGroupId = 0;
    for (const SketchGroup& g : groups)
        if (g.id > maxGroupId) maxGroupId = g.id;
    m_nextGroupId = maxGroupId + 1;

    m_profilesCacheDirty = true;
    solveConstraints();
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

    // Bounding box of every entity's full extent (arcs, ellipses, slots and
    // splines included), from the library's per-entity boxes.
    hobbycad::geometry::BoundingBox bounds;
    for (const auto& e : m_entities) bounds.include(e.boundingBox());
    if (!bounds.valid) {
        resetView();
        return;
    }
    const double minX = bounds.minX, maxX = bounds.maxX;
    const double minY = bounds.minY, maxY = bounds.maxY;

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
    m_viewRotation = hobbycad::geometry::wrapSweepDeg(m_viewRotation);
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
    if (m_flipView) rx = -rx;  // invert the heads/tails u mirror

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
    if (m_flipView) wx = -wx;  // heads/tails: mirror u left<->right

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
    if (m_flipView) wx = -wx;  // heads/tails: mirror u left<->right

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



// Pre-click preview dot for entity creation tools (before the first click),
// at the snapped or tangent-projected cursor position.
void SketchCanvas::drawToolPreviewDot(QPainter& painter)
{
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
                            double snapDist = kDrawSnapSearchPx / m_zoom;
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
            cursorWorld = m_snapEngine.snapPoint(cursorWorld);
        }

        // Draw the preview dot
        QPoint screenPoint = worldToScreen(cursorWorld);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 120, 215));
        painter.drawEllipse(screenPoint, 5, 5);
    }
}

    // Tell the constraint renderer which glyphs the chips must dodge (the pivot
    // star and the snap indicator: hard) and where the drawing is (soft), so
    // chips fan into empty canvas without hiding it. [Aaron glyph placement rule]
void SketchCanvas::updateConstraintRendererLayoutRects()
{
    QVector<QRect> reserved;
    if (m_snapEngine.hasActiveSnap()
        && (m_isDrawing || m_isDraggingHandle
            || m_transformPick != TransformPick::None || m_transformPivotDragging)) {
        const auto& spos = m_snapEngine.activeSnap().value().position;
        const QPoint c = worldToScreen(QPointF(spos.x, spos.y));
        reserved.append(QRect(c.x() - 9, c.y() - 9, 18, 18));
    }
    if (m_transformGlyphVisible && !m_selectedIds.isEmpty()) {
        const int k = m_transformGlyphArc ? kTransformPivotRotatePx
                                          : kTransformPivotGlyphPx;
        const QPoint c = worldToScreen(m_transformPivot);
        reserved.append(QRect(c.x() - k / 2, c.y() - k / 2, k, k));
    }
    m_constraintRenderer.setReservedGlyphRects(reserved);

    QVector<QRect> geom;
    for (const auto& e : m_entities) {
        if (e.points.empty()) continue;
        double minx = 1e18, miny = 1e18, maxx = -1e18, maxy = -1e18;
        for (const auto& p : e.points) {
            minx = std::min(minx, p.x); maxx = std::max(maxx, p.x);
            miny = std::min(miny, p.y); maxy = std::max(maxy, p.y);
        }
        const QPoint a = worldToScreen(QPointF(minx, miny));
        const QPoint b = worldToScreen(QPointF(maxx, maxy));
        geom.append(QRect(QPoint(std::min(a.x(), b.x()), std::min(a.y(), b.y())),
                          QPoint(std::max(a.x(), b.x()), std::max(a.y(), b.y()))));
    }
    m_constraintRenderer.setGeometryRects(geom);
}

// Selection handles for a whole selected group: one handle per unique corner
// across every member (the FixedPoint pivot in red), plus the group badge.
void SketchCanvas::drawGroupSelectionHandles(QPainter& painter, const SketchEntity& sel)
{
    // Collect unique points across all group entities and
    // identify which one is the FixedPoint (pivot) handle.
    QVector<QPointF> uniquePts;
    QPointF pivotPt;
    bool hasPivot = false;
    const double eps2 = geometry::kCoincidentTol;

    // Find the FixedPoint anchor position
    for (const auto& c : m_constraints) {
        if (c.type != ConstraintType::FixedPoint || !c.enabled
            || c.entityIds.empty())
            continue;
        const SketchEntity* fpEnt = entityById(c.entityIds[0]);
        if (!fpEnt || fpEnt->groupId != sel.groupId) continue;
        int pi = hobbycad::valueAt(c.pointIndices, 0, 0);
        if (pi < fpEnt->points.size()) {
            pivotPt = fpEnt->points[pi];
            hasPivot = true;
            break;
        }
    }

    for (const auto& e : m_entities) {
        if (e.groupId != sel.groupId) continue;
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

    for (const QPointF& pt : uniquePts) {
        QPoint p = worldToScreen(pt);
        bool isPivot = false;
        if (hasPivot) {
            double dx = pt.x() - pivotPt.x();
            double dy = pt.y() - pivotPt.y();
            isPivot = (dx * dx + dy * dy < eps2);
        }
        if (isPivot) {
            painter.setPen(QPen(QColor(200, 40, 40), 1));
            painter.setBrush(QColor(255, 180, 180));
        } else {
            painter.setPen(QPen(QColor(0, 120, 215), 1));
            painter.setBrush(Qt::white);
        }
        painter.drawRect(p.x() - 4, p.y() - 4, 8, 8);
    }

    // "You clicked a member of a group." The handles above already
    // show the group's extent, but nothing says WHY handles appeared
    // on entities the user never clicked; this does. It fires only
    // outside the group (m_enteredGroupId < 0) because once you have
    // entered one, individual members are exactly what you expect to
    // be selecting.
    if (!uniquePts.isEmpty()) {
        QRect box;
        for (const QPointF& pt : uniquePts) {
            const QRect p1(worldToScreen(pt), QSize(1, 1));
            box = box.isNull() ? p1 : box.united(p1);
        }
        // 32, not the badge row's 20. Measured on a size ladder:
        // below about 30 the corner brackets stop reading as
        // brackets and the interior fills in. It can afford the
        // room: it is one indicator for a whole selection, not
        // one chip per constraint competing for a slot.
        const int kBadge = 32;
        // Placed outside the group's bounding box, up and to the
        // left, so it never lands on one of the handles just drawn.
        // Clamped so a group running off the top-left corner does
        // not push its own indicator out of the viewport.
        QRect ggRect(qMax(2, box.left() - kBadge - 6),
                     qMax(2, box.top() - kBadge - 6),
                     kBadge, kBadge);
        // Cross-renderer declutter: the group badge is drawn after the
        // constraint chips, so it can see their placed rects and step
        // clear of them (nudging further up-left, away from the group).
        // [Aaron glyph rule 2026-09-09: no glyph overlaps another]
        {
            const QVector<QRect>& chips =
                m_constraintRenderer.placedGlyphRects();
            auto clashes = [&](const QRect& r) {
                for (const QRect& g : chips)
                    if (r.intersects(g)) return true;
                return false;
            };
            for (int tries = 0; tries < 8 && clashes(ggRect); ++tries) {
                if (ggRect.top() > kBadge + 4)
                    ggRect.translate(0, -(kBadge + 4));   // step upward
                else
                    ggRect.translate(-(kBadge + 4), 0);   // then leftward
                if (ggRect.left() < 2) { ggRect.moveLeft(2); break; }
            }
        }
        const int bx = ggRect.left();
        const int by = ggRect.top();

        painter.save();
        QPen groupPen(QColor(0, 120, 215), 1.2);
        groupPen.setCapStyle(Qt::RoundCap);
        groupPen.setJoinStyle(Qt::MiterJoin);
        painter.setPen(groupPen);
        painter.setBrush(Qt::NoBrush);
        hobbycad::drawGroupGlyph(painter,
                                 QRectF(bx, by, kBadge, kBadge));
        painter.restore();
        m_groupGlyphRect = QRect(bx, by, kBadge, kBadge);
        m_groupGlyphGroupId = sel.groupId;   // clickable: selects the group
    }
}

// D-key constraint type hint (shown after TAB cycling).
void SketchCanvas::drawDKeyHint(QPainter& painter)
{
    QFont hintFont = painter.font();
    hintFont.setPointSize(11);
    hintFont.setBold(true);
    painter.setFont(hintFont);
    QString hintText = tr("D: %1").arg(m_dKeyTypeHint);
    QFontMetrics fm(hintFont);
    int textWidth = fm.horizontalAdvance(hintText);
    int x = (width() - textWidth) / 2;
    int y = 30;
    // Background pill
    QRect bgRect(x - 6, y - fm.ascent() - 2, textWidth + 12, fm.height() + 4);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 160));
    painter.drawRoundedRect(bgRect, 4, 4);
    painter.setPen(QColor(255, 255, 100));
    painter.drawText(x, y, hintText);
    painter.setFont(font());  // Restore default
    painter.setPen(Qt::darkGray);
}

// Window selection rectangle: blue solid (window), green dashed (crossing).
void SketchCanvas::drawWindowSelectionRect(QPainter& painter)
{
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

void SketchCanvas::paintEvent(QPaintEvent* /*event*/)
{
    m_groupGlyphGroupId = -1;   // recomputed below if a group indicator is drawn
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
        // Sketch Palette filters: skip construction / projected geometry when
        // hidden. The selected entity always draws, so a hidden item can still
        // be seen while it is being worked on.
        if (!e.selected) {
            if (!m_showConstruction && e.isConstruction) continue;
            if (!m_showProjected && e.projectionSourceId >= 0) continue;
        }
        m_entityRenderer.drawEntity(painter, e);
    }

    // Highlight individually selected points (endpoints picked for a
    // point-to-point constraint): a filled square in the selection color.
    if (!m_selectedPoints.isEmpty()) {
        painter.setPen(QPen(m_theme.constraintSelected, 2));
        painter.setBrush(m_theme.constraintSelected);
        for (const auto& pr : m_selectedPoints) {
            const SketchEntity* e = entityById(pr.first);
            if (!e || pr.second < 0 || pr.second >= e->points.size()) continue;
            const QPoint sp = worldToScreen(e->points[pr.second]);
            painter.drawRect(sp.x() - 4, sp.y() - 4, 8, 8);
        }
        painter.setBrush(Qt::NoBrush);
    }

    // Draw preview of entity being created
    if (m_isDrawing) {
        // While dragging off a line-chain point, preview the tangent arc the
        // release will commit, in place of the straight rubber-band line.
        if (!(m_lineChainPressActive && m_wasDragged && drawTangentArcPreview(painter))) {
            drawPreview(painter);
        }
    }

    // Dashed guides and glyphs for the alignment inferred for the current
    // segment (horizontal/vertical/parallel/perpendicular).
    if (m_isDrawing && m_snapEngine.hasActiveInferences()) {
        m_snapEngine.drawInferenceGuides(painter);
    }

    // Draw pre-click preview dot for entity creation tools (before first click)
    if (!m_isDrawing && m_activeTool != SketchTool::Select) {
        drawToolPreviewDot(painter);
    }

    // Draw active snap point indicator
    // Transform preview: the selection where it WOULD be, faded.
    if (!m_transformPreview.isEmpty()) {
        painter.save();
        painter.setOpacity(0.4);
        for (const auto& ghost : m_transformPreview) m_entityRenderer.drawEntity(painter, ghost);
        painter.restore();
    }
    if (m_snapEngine.hasActiveSnap() && (m_isDrawing || m_isDraggingHandle || m_transformPick != TransformPick::None || m_transformPivotDragging)) {
        m_snapEngine.drawSnapIndicator(painter, m_snapEngine.activeSnap().value());
    }
    if (m_transformGlyphVisible && !m_selectedIds.isEmpty()) {
        ensureTransformStateCurrent();
        if (m_transformPick == TransformPick::FreeMove) drawFreeMoveHandles(painter);
        drawTransformPivot(painter);
    }

    updateConstraintRendererLayoutRects();

    // Draw constraints (dimensions)
    m_constraintRenderer.drawConstraints(painter);
    m_entityRenderer.drawCurvatureComb(painter);
    m_entityRenderer.drawUnconstrainedPoints(painter);
    drawMidpointGrips(painter);
    drawSlotAnchorGrips(painter);
    drawOffSegmentTangents(painter);

    // Draw selection handles
    if (auto* sel = selectedEntity()) {
        // If the primary entity is part of a group and the whole group is
        // selected, draw handles for all unique corner points across every
        // entity in the group (e.g. 4 corners of a decomposed rectangle).
        if (sel->groupId >= 0 && m_enteredGroupId < 0) {
            drawGroupSelectionHandles(painter, *sel);
        } else {
            m_entityRenderer.drawSelectionHandles(painter, *sel);
        }
    }

    // Bezier splines with a selected CONTROL POINT (not the primary selection)
    // still show their control polygon + handles (highlighted inside).
    const SketchEntity* primarySel = selectedEntity();
    for (const auto& e : m_entities) {
        if (e.type != SketchEntityType::Spline || !e.splineBezier) continue;
        if (primarySel && e.id == primarySel->id) continue;
        bool hasSelPt = false;
        for (const auto& pr : m_selectedPoints) if (pr.first == e.id) { hasSelPt = true; break; }
        if (hasSelPt) m_entityRenderer.drawSelectionHandles(painter, e);
    }

    // Draw snap constraint guides during modifier+drag
    // Show when: Shift held for snap, or Ctrl held with axis constraint
    if (m_isDraggingHandle && (m_shiftWasPressed || (m_ctrlWasPressed && m_snapAxis != SnapAxis::None))) {
        m_snapEngine.drawSnapGuides(painter);
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

    // Draw D-key constraint type hint (shown after TAB cycling)
    if (!m_dKeyTypeHint.isEmpty()) {
        drawDKeyHint(painter);
    }

    // Draw coordinates at cursor
    painter.drawText(10, height() - 10,
                     QStringLiteral("(%1, %2)")
                         .arg(m_currentMouseWorld.x(), 0, 'f', 2)
                         .arg(m_currentMouseWorld.y(), 0, 'f', 2));

    drawCursorHint(painter);

    drawScaleBar(painter);

    drawEnteredGroupBox(painter);

    // Draw window selection rectangle
    if (m_isWindowSelecting) {
        drawWindowSelectionRect(painter);
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
    painter.setPen(QPen(m_theme.grid, 1));

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

void SketchCanvas::drawMidpointGrips(QPainter& painter)
{
    // Midpoint grips: a hover marker (only while hovering, select mode) and a
    // white dot for a selected midpoint (like an unconstrained end). (Aaron)
    {
        auto midOf = [&](int id, QPointF& out) -> bool {
            const SketchEntity* e = entityById(id);
            return e && entityMidpoint(*e, out);
        };
        QPointF mp;
        if (m_selectedMidpointEntity >= 0 && midOf(m_selectedMidpointEntity, mp)) {
            const QPoint sp = worldToScreen(mp);
            painter.save();
            painter.setPen(QPen(m_theme.pointDotPen, 1));
            painter.setBrush(m_theme.pointDotFill);
            painter.drawEllipse(QPointF(sp), kMidpointDotRadiusPx, kMidpointDotRadiusPx);
            painter.restore();
        }
        if (m_hoverMidpointEntity >= 0
            && m_hoverMidpointEntity != m_selectedMidpointEntity
            && midOf(m_hoverMidpointEntity, mp)) {
            const QPoint sp = worldToScreen(mp);
            painter.save();
            painter.setPen(QPen(m_theme.pointDotPen, 1));
            painter.setBrush(m_theme.pointDotFill);
            QPolygonF tri;                          // Fusion-style midpoint triangle
            tri << QPointF(sp.x(),     sp.y() - 5)
                << QPointF(sp.x() - 5, sp.y() + 4)
                << QPointF(sp.x() + 5, sp.y() + 4);
            painter.drawPolygon(tri);
            painter.restore();
        }
    }
}

void SketchCanvas::drawSlotAnchorGrips(QPainter& painter)
{
    // Reveal a slot's anchor points while hovering it (select mode), each drawn
    // with its own snap-type icon via the shared snap indicator, so a cap
    // center is a circle, a line-end a square, a side midpoint a triangle, not a
    // uniform triangle (Aaron). A clicked anchor persists as a white dot.
    if (m_hoverSlotEntity >= 0) {
        if (const SketchEntity* slot = entityById(m_hoverSlotEntity))
            for (const auto& sp : sketch::slotAnchorPoints(*slot))
                m_snapEngine.drawSnapIndicator(painter, sp);
    }
    if (m_selectedSlotAnchor.first >= 0) {
        if (const SketchEntity* slot = entityById(m_selectedSlotAnchor.first)) {
            const auto anchors = sketch::slotAnchorPoints(*slot);
            const int idx = m_selectedSlotAnchor.second;
            if (idx >= 0 && idx < static_cast<int>(anchors.size())) {
                const QPoint sp = worldToScreen(QPointF(anchors[idx].position));
                painter.save();
                painter.setPen(QPen(m_theme.pointDotPen, 1));
                painter.setBrush(m_theme.pointDotFill);
                painter.drawEllipse(QPointF(sp), kMidpointDotRadiusPx, kMidpointDotRadiusPx);
                painter.restore();
            }
        }
    }
}

void SketchCanvas::drawCursorHint(QPainter& painter)
{
    // On-canvas hint near the cursor for the active tool at its current stage.
    // Drawn in this always-run overlay pass so it shows even before the first
    // point (drawPreview is gated on isDrawing); tools provide it via
    // cursorHint(). Skipped for the Select tool: a hint trailing every idle
    // selection is just noise. Supports multiple lines (split on '\n'). Matches
    // the arc tool's on-canvas hint style. (Aaron)
    if (m_showCursorHints && m_activeTool != SketchTool::Select) {
        if (SketchToolHandler* h = activeHandler()) {
            const QString msg = h->cursorHint(*this);
            if (!msg.isEmpty()) {
                const QPoint c = worldToScreen(m_currentMouseWorld);
                const QFontMetrics fm(painter.font());
                painter.save();
                painter.setPen(QColor(80, 80, 80));
                // Centered below the cursor and clear of its lower tip (Aaron).
                // The arrow cursor's hotspot is its top-left point; it extends
                // down by roughly the theme cursor size, so base the drop on
                // that (XCURSOR_SIZE overrides; X default is 24px when unset)
                // plus a small gap and the font ascent so the first line sits
                // fully below the cursor. Multi-line stacks downward.
                int cursorPx = qEnvironmentVariableIntValue("XCURSOR_SIZE");
                if (cursorPx <= 0) cursorPx = kDefaultCursorSizePx;
                const int y0 = c.y() + cursorPx + kCursorHintGapPx + fm.ascent();
                int y = y0;
                const QStringList lines = msg.split(QLatin1Char('\n'));
                for (const QString& ln : lines) {
                    painter.drawText(c.x() - fm.horizontalAdvance(ln) / 2, y, ln);
                    y += fm.height();
                }
                painter.restore();
            }
        }
    }
}

void SketchCanvas::drawScaleBar(QPainter& painter)
{
    // Scale bar (bottom-left, just above the coords). Shares the 3D viewport
    // scale bar's logic: niceNumber snapping (1-2-5), the same 75px target /
    // 180px max / 20px min, and formatScaleBarLabel's adaptive unit, so the 2D
    // and 3D bars read identically (Aaron). Rescales as you zoom.
    if (m_zoom > geometry::kZeroEps) {
        const double worldPerPx = 1.0 / m_zoom;                 // mm per pixel
        double worldLen = hobbycad::niceNumber(worldPerPx * hobbycad::kScaleBarTargetPx);
        double barPx = worldLen / worldPerPx;
        while (barPx > hobbycad::kScaleBarMaxPx && worldLen > 1e-3) {
            worldLen = hobbycad::niceNumberBelow(worldLen);
            barPx = worldLen / worldPerPx;
        }
        if (barPx < hobbycad::kScaleBarMinPx) barPx = hobbycad::kScaleBarMinPx;
        if (std::isfinite(barPx) && barPx < width()) {
            const QString label = QString::fromStdString(
                hobbycad::formatScaleBarLabel(worldLen, m_displayUnit));
            const int y  = height() - kScaleBarBottomMargin;
            const int x0 = kScaleBarInset;
            const int x1 = kScaleBarInset + static_cast<int>(barPx + 0.5);
            painter.save();
            painter.setPen(QPen(Qt::darkGray, 1));
            painter.drawLine(x0, y, x1, y);
            painter.drawLine(x0, y - kScaleBarTickPx, x0, y);   // end ticks
            painter.drawLine(x1, y - kScaleBarTickPx, x1, y);
            painter.drawText(x1 + 6, y + 4, label);
            painter.restore();
        }
    }
}

void SketchCanvas::drawEnteredGroupBox(QPainter& painter)
{
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
                !e.points.empty()) {
                QPointF c = e.points[0];
                groupBounds = groupBounds.united(
                    QRectF(c.x() - e.radius, c.y() - e.radius,
                           e.radius * 2, e.radius * 2));
            }
        }
        if (!first) {
            // Add padding in world units
            double pad = kHitPadPx / m_zoom;
            groupBounds.adjust(-pad, -pad, pad, pad);

            QPointF tl = worldToScreen(groupBounds.topLeft());
            QPointF br = worldToScreen(groupBounds.bottomRight());
            QRectF screenRect = QRectF(tl, br).normalized();

            // Slightly different shade: dashed border with translucent fill
            painter.setPen(QPen(QColor(0, 120, 215, 160), 1.5, Qt::DashLine));
            painter.setBrush(QColor(0, 120, 215, 15));
            painter.drawRect(screenRect);

            // Label in top-left corner
            QString groupName;
            for (const SketchGroup& g : m_groups) {
                if (g.id == m_enteredGroupId) { groupName = QString::fromStdString(g.name); break; }
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
}

void SketchCanvas::drawAxes(QPainter& painter)
{
    // The two in-plane axes are colored by which world axis they are for on
    // this plane (X red, Y green, Z blue): so a YZ sketch shows Y and Z, an XZ
    // sketch X and Z, etc. Custom planes keep the default red/green.
    const hobbycad::PlaneAxisLabels ax = hobbycad::planeAxisLabels(m_plane);
    auto axisColor = [this](const char* label, const QColor& fallback) -> QColor {
        switch (label[0]) {
        case 'X': return m_theme.axisX;
        case 'Y': return m_theme.axisY;
        case 'Z': return m_theme.axisZ;
        default:  return fallback;   // U/V on a custom plane
        }
    };

    // In-plane axis 1 (horizontal, u)
    painter.setPen(QPen(axisColor(ax.u, m_theme.axisX), 2));
    QPoint origin = worldToScreen({0, 0});
    QPoint xEnd = worldToScreen({50, 0});
    painter.drawLine(origin, xEnd);

    // In-plane axis 2 (vertical, v)
    painter.setPen(QPen(axisColor(ax.v, m_theme.axisY), 2));
    QPoint yEnd = worldToScreen({0, 50});
    painter.drawLine(origin, yEnd);

    // Origin dot
    painter.setBrush(m_theme.origin);
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(origin, 4, 4);
}


void SketchCanvas::drawPreview(QPainter& painter)
{
    QPen pen(m_theme.preview, 2, Qt::DashLine);
    // A tool that previews in a different color says so itself.
    if (SketchToolHandler* h = activeHandler()) {
        h->previewPen(*this, pen);
    }
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    // The active tool paints its own preview. This MUST come after the
    // shared pen/brush setup above: the handler inherits that state, and
    // dispatching before it left the brush stale: a circle preview
    // rendered as a filled black disc.
    if (SketchToolHandler* h = activeHandler()) {
        if (h->drawPreview(*this, painter)) return;
    }

}







// ---- Constraint Drawing Functions ----




void SketchCanvas::drawOffSegmentTangents(QPainter& painter)
{
    painter.save();
    for (const SketchConstraint& c : m_constraints) {
        if (!c.enabled || c.type != ConstraintType::Tangent) continue;
        if (c.entityIds.size() < 2) continue;

        const SketchEntity* e1 = entityById(c.entityIds[0]);
        const SketchEntity* e2 = entityById(c.entityIds[1]);
        if (!e1 || !e2) continue;

        // Either order; the query rejects any pair that is not line+circle.
        auto pt = sketch::offSegmentTangentPoint(*e1, *e2);
        if (!pt) pt = sketch::offSegmentTangentPoint(*e2, *e1);
        if (!pt) continue;

        const QPoint sp = worldToScreen(QPointF(pt->x, pt->y));
        painter.setPen(QPen(m_theme.tangentMarker, 1.0));
        painter.setBrush(m_theme.tangentMarker);
        painter.drawEllipse(sp, 4, 4);
    }
    painter.restore();
}













// Left press, Transform section: the star, a pick, or the free-move
// manipulator owns the click before any tool or selection logic. Returns
// true when it consumed the press.
bool SketchCanvas::handleTransformPress(QMouseEvent* event, const QPointF& worldPos)
{
    if (m_transformGlyphVisible && !m_selectedIds.isEmpty()) {
        ensureTransformStateCurrent();
        if (transformStarHit(event->pos())) {
            // press ON the star: drag it; it keeps its exact position until the mouse moves
            m_transformPivotDragging = true;
            m_transformPick = TransformPick::None;
            setCursor(Qt::ClosedHandCursor);
            update();
            return true;
        }
        switch (m_transformPick) {
        case TransformPick::Pivot: {
            m_transformPivot = m_snapEngine.snapPoint(worldPos);        // place on press, so a plain click works
            m_transformPivotUserSet = true; m_transformPivotCleared = false;
            m_transformPivotDragging = true;
            emit transformPivotChanged(m_transformPivot, false);
            setCursor(Qt::ClosedHandCursor);
            update();
            return true;
        }
        case TransformPick::FromPoint: case TransformPick::ToPoint: case TransformPick::PointOnSelection:
        case TransformPick::MirrorA: case TransformPick::MirrorB: case TransformPick::ReferencePoint: {
            const QPointF p = m_snapEngine.snapPoint(worldPos);
            const TransformPick done = m_transformPick;
            m_transformPick = TransformPick::None;
            setCursor(Qt::ArrowCursor);
            emit transformPickCompleted(int(done), p);
            update();
            return true;
        }
        case TransformPick::FreeMove: {
            if (freeMoveRingHit(event->pos())) {
                m_freeMoveHandle = FreeMoveHandle::Ring;
                const QPointF v = worldPos - m_transformPivot;
                m_freeMoveStartAngle = qRadiansToDegrees(qAtan2(v.y(), v.x())) - m_freeMoveAngle;
                setCursor(Qt::ClosedHandCursor);
                return true;
            }
            if (freeMoveBodyHit(worldPos)) {
                m_freeMoveHandle = FreeMoveHandle::Body;
                m_freeMoveStartWorld = worldPos - m_freeMoveDelta;
                setCursor(Qt::ClosedHandCursor);
                return true;
            }
            break;   // a click elsewhere is a selection change, which resets the section
        }
        case TransformPick::None: break;
        }
    }
    return false;
}

// Left press in a background-calibration mode (entity pick for alignment,
// point pick for scale). Returns true when it consumed the press.
bool SketchCanvas::handleCalibrationPress(const QPointF& worldPos)
{
    // Handle calibration entity selection mode - select line for alignment
    if (m_calibrationEntitySelectionMode) {
        int hitId = hitTest(worldPos);
        if (hitId >= 0) {
            const SketchEntity* entity = entityById(hitId);
            // Only allow lines (including construction lines) for alignment
            if (entity && entity->type == SketchEntityType::Line) {
                double angle = getEntityAngle(hitId);
                emit calibrationEntitySelected(hitId, angle);
                return true;
            }
        }
        // Click didn't hit a valid entity - ignore
        return true;
    }

    // Handle background calibration mode - pick points for scale calibration
    if (m_backgroundCalibrationMode && m_backgroundImage.enabled) {
        // Check if click is within background bounds
        if (m_backgroundImage.containsPoint(worldPos)) {
            emit calibrationPointPicked(worldPos);
            return true;
        }
    }
    return false;
}

// Left press in background edit mode: a handle starts a drag (true); a press
// elsewhere leaves the mode and falls through (false).
bool SketchCanvas::handleBackgroundEditPress(const QPointF& worldPos)
{
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
            return true;
        }
        // Clicking outside background exits edit mode
        setBackgroundEditMode(false);
    }
    return false;
}

// Select tool press on a handle (group-aware, so any corner of a decomposed
// rectangle is draggable): arms a point-press. Returns true when it did.
bool SketchCanvas::pressSelectsHandle(QMouseEvent* event, const QPointF& worldPos)
{
    int handleEntityId = -1, handleIdx = -1;
    bool handleHit = hitTestGroupHandle(worldPos, handleEntityId, handleIdx);
    // Don't allow handle dragging on sweep-angle construction lines
    // (but allow it for the arc entity itself, which is also in the group)
    if (handleHit && handleEntityId >= 0) {
        const SketchEntity* he = entityById(handleEntityId);
        if (he && he->type == SketchEntityType::Line) {
            for (const auto& g : m_groups) {
                if (isSweepAngleGroup(g.id) && g.containsEntity(handleEntityId)) {
                    handleHit = false;
                    break;
                }
            }
        }
    }
    if (handleHit && handleIdx >= 0) {
        if (handleEntityId != m_selectedId) {
            m_selectedId = handleEntityId;
        }
        // Arm a point-press: a no-move click SELECTS this control point
        // (needed to pick a Bezier anchor when its spline is selected);
        // a drag converts to a handle drag in mouseMoveEvent.
        m_pointPressArmed  = true;
        m_pointPressEntity = handleEntityId;
        m_pointPressIndex  = handleIdx;
        m_pointPressScreen = event->pos();
        m_pointPressMods   = event->modifiers();
        beginDragDetection(event->pos());
        return true;
    }
    return false;
}

// Select tool press on a constraint label or glyph chip: selects it, or
// starts a label drag when it was already selected. Returns true when hit.
bool SketchCanvas::pressSelectsConstraint(QMouseEvent* event, const QPointF& worldPos)
{
    int constraintId = hitTestConstraintLabel(worldPos);
    bool glyphHit = false;
    if (constraintId < 0) {
        constraintId = m_constraintRenderer.hitTestConstraintGlyph(event->pos());
        glyphHit = (constraintId >= 0);
    }
    if (constraintId >= 0) {
        // If already selected, start dragging the label
        // (glyph chips are not draggable)
        if (!glyphHit && constraintId == m_selectedConstraintId) {
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
            selectClear();

            // Deselect old constraint
            for (auto& c : m_constraints) {
                c.selected = (c.id == constraintId);
            }
            m_selectedConstraintId = constraintId;
            emit selectionChanged(-1);  // Deselect entity
            emit constraintSelectionChanged(constraintId);
            update();
        }
        return true;
    }
    return false;
}

// Select tool press on a point-level target (endpoint, Bezier leg, tangent
// contact dot, slot anchor, midpoint grip), which take precedence over the
// entity pick. Returns true when one was hit.
bool SketchCanvas::pressSelectsPoint(QMouseEvent* event, const QPointF& worldPos)
{
    // Point pick takes precedence over entity pick (unless the filter
    // is Curves-only): a press on an endpoint arms a point selection
    // (committed on release) or, if the cursor moves first, a handle
    // drag of that point.
    if (m_selectFilter != SelectFilter::CurvesOnly) {
        int pe = -1, pi = -1;
        if (hitTestAnyPoint(worldPos, pe, pi)) {
            m_pointPressArmed  = true;
            m_pointPressEntity = pe;
            m_pointPressIndex  = pi;
            m_pointPressScreen = event->pos();
            m_pointPressMods   = event->modifiers();
            beginDragDetection(event->pos());
            return true;
        }
    }

    // A click on a Bezier control-polygon leg selects its two endpoints,
    // so a Distance dimension on them constrains that handle's length.
    if (m_selectFilter != SelectFilter::CurvesOnly) {
        int le = -1, a = -1, b = -1;
        if (hitTestBezierLeg(worldPos, le, a, b)) {
            selectPoint(le, a, false, false);
            selectPoint(le, b, true,  false);
            update();
            return true;
        }
    }

    // The red tangent-contact dot selects its circle, so the user can
    // then pick a line endpoint and Coincident-pin it onto the circle
    // (see hitTestTangentContact). Curves are selectable here; only a
    // points-only filter suppresses it.
    if (m_selectFilter != SelectFilter::PointsOnly) {
        const int tcCircle = hitTestTangentContact(worldPos);
        if (tcCircle >= 0) {
            const bool extend =
                event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier);
            if (!extend) m_selectedPoints.clear();
            selectEntity(tcCircle, extend);
            showStatus(tr("Tangent contact selected: Ctrl-click a line "
                          "endpoint, then Coincident to pin it onto the circle."));
            update();
            return true;
        }
    }

    // A midpoint grip (line or arc), shown only on hover, selects the
    // derived midpoint as a point, drawn white like an unconstrained
    // end. Ctrl/Shift extend so it can pair with a target for a Midpoint
    // constraint. (Aaron)
    // Slot anchor point: click one to persist it as a white dot (a snap
    // anchor, not a constraint). (Aaron)
    {
        const int hitId = pick(worldPos);
        const SketchEntity* e = (hitId >= 0) ? entityById(hitId) : nullptr;
        if (e && e->type == SketchEntityType::Slot) {
            const auto anchors = sketch::slotAnchorPoints(*e);
            const QPointF clickScr = worldToScreen(worldPos);
            int best = -1; double bestD = kMidpointHitTolPx;
            for (int i = 0; i < static_cast<int>(anchors.size()); ++i) {
                const QPointF scr = worldToScreen(QPointF(anchors[i].position));
                const double d = QLineF(clickScr, scr).length();
                if (d <= bestD) { bestD = d; best = i; }
            }
            if (best >= 0) {
                m_selectedSlotAnchor = { e->id, best };
                update();
                return true;
            }
        }
    }
    if (m_selectFilter != SelectFilter::CurvesOnly) {
        const int midId = hitTestMidpoint(worldPos);
        if (midId >= 0) {
            const bool ctrl  = event->modifiers() & Qt::ControlModifier;
            const bool shift = event->modifiers() & Qt::ShiftModifier;
            if (!ctrl && !shift) clearSelection();
            m_selectedMidpointEntity =
                (ctrl && m_selectedMidpointEntity == midId) ? -1 : midId;
            emit selectionChanged(m_selectedId);
            update();
            return true;
        }
    }
    return false;
}

// Select tool press with no point-level hit: select/drag the entity under
// the cursor, or start a window selection on empty space.
void SketchCanvas::pressSelectsEntityOrWindow(QMouseEvent* event, const QPointF& worldPos)
{
    // Hit test for entity selection
    int hitId = hitTest(worldPos);
    // Points-only: curves are not selectable, so a non-point click is
    // treated as empty (starts a rubber band / clears).
    if (m_selectFilter == SelectFilter::PointsOnly) hitId = -1;
    // Selection modifiers: Ctrl = toggle (add if new, remove if already
    // selected); Shift = add-only (never deselect). Plain click replaces.
    // Shift is otherwise free in the Select tool; its snap role only
    // applies while drawing or dragging.
    const bool ctrlHeld  = event->modifiers() & Qt::ControlModifier;
    const bool shiftHeld = event->modifiers() & Qt::ShiftModifier;
    const bool extendSel = ctrlHeld || shiftHeld;

    if (hitId >= 0) {
        // Clicked on an entity
        // Deselect constraints
        for (auto& c : m_constraints) {
            c.selected = false;
        }
        m_selectedConstraintId = -1;

        // Group expansion is handled by enter-group mode now:
        //   normal click = whole group; double-click = enter group
        const SketchEntity* hit = entityById(hitId);
        if (shiftHeld && !ctrlHeld && hit && hit->selected) {
            // Add-only on an already-selected entity: keep it, do nothing.
        } else {
            // Ctrl toggles; Shift (new) and plain click add/replace.
            selectEntity(hitId, extendSel);
        }

        // Direct drag (Fusion/Onshape): the same press that selected
        // the entity can drag it. Near a handle of what is now
        // selected, drag that handle; otherwise arm a body drag that
        // starts once the cursor moves past the drag threshold.
        if (!extendSel) {
            int dhEnt = -1, dhIdx = -1;
            if (hitTestGroupHandle(worldPos, dhEnt, dhIdx) && dhEnt == hitId && dhIdx >= 0) {
                beginHandleDrag(dhEnt, dhIdx, worldPos, event->modifiers());
            } else {
                m_bodyDragArmed = true;
                m_bodyDragEntityId = hitId;
                m_bodyDragPressWorld = worldPos;
                m_bodyDragLastWorld = worldPos;
                m_bodyDragPressScreen = event->pos();
            }
        }
    } else {
        // Clicked on empty space

        // If inside a group, leave it first
        if (m_enteredGroupId >= 0) {
            leaveGroup();
            // Don't start window selection; just leave the group
            return;
        }

        // Start window selection
        m_isWindowSelecting = true;
        m_windowSelectStart = worldPos;
        m_windowSelectEnd = worldPos;
        m_windowSelectCrossing = false;  // Will be determined by drag direction

        // Clear selection unless Ctrl or Shift held (both keep it)
        if (!extendSel) {
            clearSelection();
        }
    }
}

// Left press with the Select tool.
void SketchCanvas::handleSelectToolPress(QMouseEvent* event, const QPointF& worldPos)
{
    // First check if clicking on a handle; use group-aware test
    // so any corner of a decomposed rectangle is draggable.
    if (pressSelectsHandle(event, worldPos)) return;

    // Clicking the group indicator glyph selects the whole group.
    {
        int ggid = hitTestGroupGlyph(event->pos());
        if (ggid >= 0) { selectGroup(ggid); return; }
    }

    // Check if clicking on a constraint label or glyph chip first
    if (pressSelectsConstraint(event, worldPos)) return;

    // If the sketch was deselected (Save/Discard bar visible),
    // any click on the canvas re-engages the sketch.
    if (!m_sketchSelected) {
        m_sketchSelected = true;
        emit selectionChanged(-1);  // re-engage sketch
        // Fall through to normal selection handling below
    }

    // Point-level picks take precedence over the entity pick.
    if (pressSelectsPoint(event, worldPos)) return;

    pressSelectsEntityOrWindow(event, worldPos);
}

// Left press with a drawing or editing tool active.
void SketchCanvas::handleDrawToolPress(QMouseEvent* event, const QPointF& worldPos)
{
    // Check if already drawing (click-click mode: second click to finish)
    if (m_isDrawing) {
        // Staged tools add their points on click: the arc-slot modes
        // (SlotToolHandler), the 3-point and parallelogram rectangles
        // (RectangleToolHandler), the 3-point circle (CircleToolHandler) and
        // the 3-point / center-start-end / start-end-radius arcs
        // (ArcToolHandler). Each handler declines when it is not its turn
        // (a pure guard, no state change) and we fall through to the shared
        // two-point path below.
        if (SketchToolHandler* h = activeHandler()) {
            if (h->mousePress(*this, event, worldPos)) return;
        }

        // For two-point tools, second click finishes the entity
        // (Arc and Spline have their own multi-click logic in mouseReleaseEvent)
        // (Arc slots and 3-point rectangles are handled above and return early)
        // (Point finishes on release, not second click)
        // Note: Tangent arc is a two-click tool (tangent point + end point)
        // Note: Tangent line needs special handling to constrain endpoint
        const bool isMultiClickTool =
            activeHandler() && activeHandler()->isMultiClick(*this);

        // Line chaining, tangent-arc gesture (F-3): once a line segment
        // has been placed, the NEXT segment is deferred to release so a
        // press-drag off the chain point can sweep a tangent arc while a
        // plain click still lays a straight segment. Only the Line tool,
        // only while chaining (m_chainFromEntityId is set).
        if (!isMultiClickTool && m_activeTool == SketchTool::Line
            && m_chainFromEntityId >= 0 && !m_previewPoints.isEmpty()) {
            beginDragDetection(event->pos());
            m_lineChainPressActive = true;
            return;
        }

        if (!isMultiClickTool) {
            // Update the endpoint to the constrained position and finish
            // m_currentMouseWorld already has angle snap and other constraints applied
            updateEntity(m_currentMouseWorld);
            recordEndpointSnap();   // so an endpoint on a snap gets its Coincident
            finishEntity();
            return;
        }
    }

    // Tools whose first click is a real entity point (the 3-point arc
    // modes and the 3-point circle) start the entity outright instead
    // of going through the shared two-point path.
    if (!m_isDrawing && activeHandler()
        && activeHandler()->beginsOnFirstClick(*this)) {
        beginDragDetection(event->pos());
        startEntity(m_snapEngine.snapPoint(worldPos));
        return;
    }

    if (activeHandler() && activeHandler()->mousePress(*this, event, worldPos)) {
        // The handler consumed the click: circle tangent-target selection
        // (gui/tools/circletoolhandler.cpp) or one of the single-click editing
        // operations, trim, extend, split, offset, fillet, chamfer, both
        // patterns and project (gui/tools/optoolhandlers.{h,cpp}).
    } else {
        // Start drawing normally.
        //
        // Guarded on !m_isDrawing: a multi-click tool whose press
        // adds no point (the freeform polygon adds its vertices on
        // RELEASE) falls all the way down here on every click after
        // the first, and startEntity() clears the point list. Without
        // the guard each click restarted the entity, so a freeform
        // polygon never accumulated more than one vertex and was
        // discarded as invalid; it drew nothing at all.
        if (!m_isDrawing) {
            beginDragDetection(event->pos());
            startEntity(m_snapEngine.snapPoint(worldPos));
        } else {
            beginDragDetection(event->pos());
        }
    }
}

void SketchCanvas::mousePressEvent(QMouseEvent* event)
{
    // Click-away commits inline constraint edit
    if (m_inlineEditActive) {
        QPointF worldPos = screenToWorld(event->pos());
        int hitCid = hitTestConstraintLabel(worldPos);
        if (hitCid != m_inlineEditConstraintId) {
            commitInlineConstraintEdit();
        }
    }

    QPointF worldPos = screenToWorld(event->pos());
    m_lastMousePos = event->pos();

    if (event->button() == Qt::MiddleButton) {
        m_isPanning = true;
        setCursor(Qt::ClosedHandCursor);
        return;
    }

    // Right-click to finish multi-click entities (spline, freeform polygon)
    if (event->button() == Qt::RightButton) {
        // Dimension tool: right-click before placing the label offers the
        // radius/diameter and driving/driven choices up front (Fusion F-31),
        // rather than forcing an edit after the dimension exists.
        if (m_activeTool == SketchTool::Dimension && m_isCreatingConstraint
            && !m_constraintTargetEntities.isEmpty()) {
            showDimensionOptionsMenu(event->pos());
            m_suppressNextContextMenu = true;   // Qt still delivers contextMenuEvent
            return;
        }
        // Right-click == finish/end for every drawing tool: multi-click tools
        // commit, a line chain ends keeping its committed segments, an incomplete
        // fixed entity is discarded.
        if (m_isDrawing) {
            if (activeHandler() && activeHandler()->finishesOnRightClick(*this)) {
                finishEntity();
            } else {
                const bool chaining = activeHandler()
                    && activeHandler()->chainsFromLastPoint(*this);
                cancelEntity();
                m_chainFromEntityId = -1;
                if (chaining) emit toolHintChanged(currentToolHint());
            }
            m_suppressNextContextMenu = true;
            return;
        }
        return;
    }

    if (event->button() == Qt::LeftButton) {
        // Transform section: the star, a pick, or the free-move manipulator
        // owns the click before any tool or selection logic.
        if (handleTransformPress(event, worldPos)) return;
        // Background calibration and edit modes come next.
        if (handleCalibrationPress(worldPos)) return;
        if (handleBackgroundEditPress(worldPos)) return;

        // The Constraint tool works on what is already selected: click an
        // entity to add it to the selection, and apply as soon as the
        // selection supports a constraint. Before this, the tool set a
        // cursor and a status hint and did nothing else: every
        // apply*Constraint() function in this class had no caller at all,
        // which is why geometric constraints were unreachable from the UI.
        if (m_activeTool == SketchTool::Constraint) {
            if (activeHandler()) {
                activeHandler()->mousePress(*this, event, worldPos);
            }
            return;
        }

        if (m_activeTool == SketchTool::Select) {
            handleSelectToolPress(event, worldPos);
        } else {
            handleDrawToolPress(event, worldPos);
        }
    }
}

// mouseMoveEvent preamble: midpoint-grip and slot-anchor hover state (select mode).
void SketchCanvas::updateHoverGrips(const QPointF& worldPos)
{
    // Midpoint grips appear only while hovering them in select mode.
    {
        const int hov = (m_activeTool == SketchTool::Select && !m_isDrawing)
                            ? hitTestMidpoint(worldPos) : -1;
        if (hov != m_hoverMidpointEntity) { m_hoverMidpointEntity = hov; update(); }
    }

    // Slot anchor points reveal while hovering a slot in select mode.
    {
        int hovSlot = -1;
        if (m_activeTool == SketchTool::Select && !m_isDrawing
                && !m_isDraggingHandle && !m_isDraggingBody) {
            const int hitId = pick(worldPos);
            if (const SketchEntity* e = (hitId >= 0) ? entityById(hitId) : nullptr)
                if (e->type == SketchEntityType::Slot) hovSlot = hitId;
        }
        if (hovSlot != m_hoverSlotEntity) { m_hoverSlotEntity = hovSlot; update(); }
    }
}

// mouseMoveEvent preamble: entity/grid snap, Ctrl angle snap, the tool's cursor
// constraint, and drawing-time inference; sets m_currentMouseWorld.
void SketchCanvas::applySnapAndInference(const QPointF& worldPos, bool ctrlHeld, bool altHeld)
{
    // Alt disables entity snapping temporarily
    // Otherwise entity snapping is always active
    m_snapEngine.clearAngleSnap();
    if (altHeld) {
        // Free form - no entity snap
        m_currentMouseWorld = worldPos;
        // Still apply grid snap if enabled
        if (m_snapToGrid) {
            double snappedX = qRound(m_currentMouseWorld.x() / m_gridSpacing) * m_gridSpacing;
            double snappedY = qRound(m_currentMouseWorld.y() / m_gridSpacing) * m_gridSpacing;
            m_currentMouseWorld = {snappedX, snappedY};
        }
        m_snapEngine.clearActiveSnap();
    } else {
        // Entity snapping active
        m_currentMouseWorld = m_snapEngine.snapPoint(worldPos);
    }

    // Ctrl enables angle snapping (45° increments) during line-based entity creation
    // Skip for constrained line modes (Horizontal, Vertical, Tangent) - they have their own constraints
    if (ctrlHeld && m_isDrawing && !m_previewPoints.isEmpty()) {
        if (activeHandler() && activeHandler()->supportsAngleSnap(*this)) {
            // Calculate the angle-snapped position
            QPointF startPoint = m_previewPoints[0];
            QPointF snappedPos = m_snapEngine.snapToAngle(startPoint, m_currentMouseWorld);

            // Get the direction of the snapped angle ray
            QPointF rayDir = snappedPos - startPoint;

            if (geometry::length(rayDir) > 0.001) {
                // Check if the entity snap point lies exactly on the angle ray
                if (geometry::pointOnRay(m_currentMouseWorld, startPoint, rayDir) &&
                    m_snapEngine.hasActiveSnap() && !altHeld) {
                    // Snap point is on the angle ray - keep it
                } else {
                    // Snap point not on ray - use the angle-snapped position
                    m_currentMouseWorld = snappedPos;
                    m_snapEngine.clearActiveSnap();
                }
            } else {
                m_currentMouseWorld = snappedPos;
            }
        }
    }

    // Whatever the active tool's mode constrains the cursor to (a
    // horizontal or vertical axis, a tangent ray, a tangent arc path), the
    // tool applies here. A tool that moves the cursor invalidates the entity
    // snap indicator, so the canvas drops it.
    bool cursorConstrained = false;
    if (m_isDrawing && activeHandler()
        && activeHandler()->constrainCursor(*this, m_currentMouseWorld, altHeld)) {
        m_snapEngine.clearActiveSnap();
        cursorConstrained = true;
    }

    // Geometric inference while drawing straight segments: nudge the moving
    // end onto a horizontal or vertical axis, or parallel/perpendicular to an
    // existing line, and remember that alignment so it becomes a real
    // constraint when the segment is committed (Fusion's inference lines).
    // Suppressed by Alt, by a tool mode that already fixes the direction
    // (H/V/tangent), by the Ctrl angle snap, and by a live point snap: a
    // snap onto real geometry always wins over an axis alignment.
    m_snapEngine.clearInferences();
    if (!altHeld && m_isDrawing && !cursorConstrained && !m_snapEngine.angleSnapActive()
        && !m_lineChainPressActive
        && !m_previewPoints.isEmpty()
        && activeHandler() && activeHandler()->supportsAngleSnap(*this)
        && !(m_snapEngine.hasActiveSnap()
             && m_snapEngine.activeSnap()->type != sketch::SnapType::Nearest)) {
        const QPointF p0 = m_previewPoints.last();
        m_currentMouseWorld = m_snapEngine.computeInferences(p0, m_currentMouseWorld);
    }
}

// mouseMoveEvent preamble: broadcast the 2D and plane-projected 3D cursor position.
void SketchCanvas::emitCursorPositions()
{
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
}

// mouseMoveEvent guard: middle-drag viewport pan. Returns true if it consumed the move.
bool SketchCanvas::handlePanMove(QMouseEvent* event)
{
    if (m_isPanning) {
        QPoint delta = event->pos() - m_lastMousePos;
        m_viewCenter.rx() -= delta.x() / m_zoom;
        m_viewCenter.ry() += delta.y() / m_zoom;
        m_lastMousePos = event->pos();
        update();
        return true;
    }
    return false;
}

// mouseMoveEvent guard: rubber-band (window/crossing) selection rectangle.
bool SketchCanvas::handleWindowSelectMove(const QPointF& worldPos)
{
    if (m_isWindowSelecting) {
        // Update window selection rectangle
        m_windowSelectEnd = worldPos;
        // Determine if crossing mode (right-to-left drag)
        m_windowSelectCrossing = (m_windowSelectEnd.x() < m_windowSelectStart.x());
        update();
        return true;
    }
    return false;
}

// mouseMoveEvent guard: dragging the transform pivot star.
bool SketchCanvas::handleTransformPivotDragMove()
{
    if (m_transformPivotDragging) {
        m_transformPivot = m_currentMouseWorld;          // already snapped (Alt = raw + grid)
        m_transformPivotUserSet = true; m_transformPivotCleared = false;
        emit transformPivotChanged(m_transformPivot, false);
        update();
        return true;
    }
    return false;
}

// mouseMoveEvent guard: free-move body translate (Ctrl = axis lock) or ring rotate
// (Ctrl = 15-degree steps).
bool SketchCanvas::handleFreeMoveDragMove(QMouseEvent* event, const QPointF& worldPos)
{
    if (m_freeMoveHandle != FreeMoveHandle::None) {
        if (m_freeMoveHandle == FreeMoveHandle::Body) {
            QPointF d = worldPos - m_freeMoveStartWorld;
            if (event->modifiers() & Qt::ControlModifier) { if (std::fabs(d.x()) >= std::fabs(d.y())) d.setY(0); else d.setX(0); }
            m_freeMoveDelta = d;
        } else {
            const QPointF v = worldPos - m_transformPivot;
            double a = qRadiansToDegrees(qAtan2(v.y(), v.x())) - m_freeMoveStartAngle;
            if (event->modifiers() & Qt::ControlModifier) a = qRound(a / 15.0) * 15.0;
            while (a > 180.0) a -= 360.0;
            while (a <= -180.0) a += 360.0;
            m_freeMoveAngle = a;
        }
        emit freeMoveChanged(m_freeMoveDelta, m_freeMoveAngle);
        update();
        return true;
    }
    return false;
}

// mouseMoveEvent hover: cursor hint over the transform star/ring/body (no state change).
void SketchCanvas::updateTransformGlyphCursor(QMouseEvent* event, const QPointF& worldPos)
{
    if (m_transformGlyphVisible && !m_selectedIds.isEmpty() && !(event->buttons() & Qt::LeftButton)) {
        if (transformStarHit(event->pos())) setCursor(Qt::OpenHandCursor);
        else if (m_transformPick == TransformPick::FreeMove)
            setCursor(freeMoveRingHit(event->pos()) || freeMoveBodyHit(worldPos) ? Qt::OpenHandCursor : Qt::ArrowCursor);
        else if (m_transformPick != TransformPick::None) setCursor(Qt::CrossCursor);
    }
}

// mouseMoveEvent guard: background-image move/resize by handle (aspect-lock aware).
bool SketchCanvas::handleBackgroundDragMove(const QPointF& worldPos)
{
    if (m_bgDragHandle != BackgroundHandle::None) {
        double dx = worldPos.x() - m_bgDragStartWorld.x();
        double dy = worldPos.y() - m_bgDragStartWorld.y();

        switch (m_bgDragHandle) {
        case BackgroundHandle::Move:
            m_backgroundImage.position.x = m_bgOriginalPosition.x() + dx;
            m_backgroundImage.position.y = m_bgOriginalPosition.y() + dy;
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
                m_backgroundImage.position.x = m_bgOriginalPosition.x() + dx;
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
                m_backgroundImage.position.y = m_bgOriginalPosition.y() + dy;
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
                m_backgroundImage.position.x = m_bgOriginalPosition.x() + dx;
                m_backgroundImage.position.y = m_bgOriginalPosition.y() + dy;
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
                m_backgroundImage.position.y = m_bgOriginalPosition.y() + dy;
            }
            break;
        }

        case BackgroundHandle::Left: {
            double newWidth = m_bgOriginalWidth - dx;
            if (newWidth > 1) {
                m_backgroundImage.width = newWidth;
                m_backgroundImage.position.x = m_bgOriginalPosition.x() + dx;
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
        return true;
    }
    return false;
}

// mouseMoveEvent hover: cursor over background handles while in background edit mode.
void SketchCanvas::updateBackgroundEditCursor(const QPointF& worldPos)
{
    if (m_backgroundEditMode && m_backgroundImage.enabled) {
        BackgroundHandle handle = hitTestBackgroundHandle(worldPos);
        updateCursorForBackgroundHandle(handle);
    }
}

// mouseMoveEvent guard: drag a constraint label; radius/diameter labels snap to the
// circle's perimeter-point angles.
bool SketchCanvas::handleConstraintLabelDragMove(const QPointF& worldPos)
{
    if (m_isDraggingConstraintLabel) {
        // Drag the constraint label
        SketchConstraint* constraint = constraintById(m_selectedConstraintId);
        if (constraint) {
            QPointF delta = worldPos - m_dragStartWorld;
            QPointF newPos = m_constraintLabelOriginal + delta;

            // Snap radius/diameter labels to perimeter point angles on circles
            if ((constraint->type == ConstraintType::Radius
                 || constraint->type == ConstraintType::Diameter)
                && !constraint->entityIds.empty()) {
                const SketchEntity* ent = entityById(constraint->entityIds[0]);
                if (ent && ent->type == SketchEntityType::Circle
                    && ent->points.size() >= 2) {
                    QPointF center = ent->points[0];
                    QPointF toLabel = newPos - center;
                    double labelAngle = std::atan2(toLabel.y(), toLabel.x());
                    double labelDist = geometry::length(toLabel);
                    if (labelDist > geometry::kDegenerateLen) {
                        constexpr double snapThresholdRad = degreesToRadians(10.0);
                        bool snapped = false;
                        int numPerim = static_cast<int>(ent->points.size()) - 1;
                        for (int pi = 1; pi <= numPerim; ++pi) {
                            QPointF toP = QPointF(ent->points[pi]) - center;
                            double pAngle = std::atan2(toP.y(), toP.x());
                            double diff = labelAngle - pAngle;
                            // Normalize to [-pi, pi]
                            diff = hobbycad::geometry::wrapSweepRad(diff);
                            if (std::abs(diff) < snapThresholdRad) {
                                // Snap to this endpoint's angle
                                labelAngle = pAngle;
                                newPos = geometry::polarPoint(center, labelDist, pAngle);
                                snapped = true;
                                break;
                            }
                        }
                        // Store the label angle on driving constraints
                        if (constraint->isDriving) {
                            constraint->labelAngle = labelAngle;
                        }
                    }
                }
            }

            constraint->labelPosition = newPos;
            if (constraint->type == ConstraintType::Angle) setAngleSideFromLabel(constraint);
            update();
        }
        return true;
    }
    return false;
}

// mouseMoveEvent guard: a pressed point that moves past the drag threshold becomes a
// handle drag of that point rather than a selection.
bool SketchCanvas::promotePointPressToHandleDrag(QMouseEvent* event, const QPointF& worldPos)
{
    if (m_pointPressArmed && (event->buttons() & Qt::LeftButton)) {
        if ((event->pos() - m_pointPressScreen).manhattanLength() > QApplication::startDragDistance()) {
            m_pointPressArmed = false;
            // Movement means "drag this endpoint", not "select it": select the
            // owning entity and begin a handle drag of that point.
            if (entityById(m_pointPressEntity)) {
                selectEntity(m_pointPressEntity, false);
                beginHandleDrag(m_pointPressEntity, m_pointPressIndex, worldPos, event->modifiers());
            }
        }
        return true;
    }
    return false;
}

// mouseMoveEvent: arm a body drag once past the drag threshold. Deliberately falls
// through so the same event runs the first drag frame (see handleBodyDragMove).
void SketchCanvas::armBodyDragIfPastThreshold(QMouseEvent* event)
{
    if (m_bodyDragArmed && (event->buttons() & Qt::LeftButton)) {
        if ((event->pos() - m_bodyDragPressScreen).manhattanLength() > QApplication::startDragDistance()) {
            m_bodyDragArmed = false;
            if (entityById(m_bodyDragEntityId)) {
                m_isDraggingBody = true;
                m_dragSnapshotEntities = m_entities;
                m_dragSnapshotConstraints = m_constraints;
                setCursor(Qt::SizeAllCursor);
            }
        }
    } else if (m_bodyDragArmed) {
        m_bodyDragArmed = false;
    }
}

// mouseMoveEvent guard: whole-entity body drag (slot-centerline redirect; far points
// held via drag weights or temporary fixes so the grabbed element deforms).
bool SketchCanvas::handleBodyDragMove(const QPointF& worldPos)
{
    if (m_isDraggingBody) {
        // Body drag translates the entity by the raw cursor motion (no
        // snapping: Onshape's "normal drag does not snap"); the solver then
        // moves the rest as little as the constraints require, holding all
        // of the entity's points as the dragged ones.
        SketchEntity* ent = entityById(m_bodyDragEntityId);
        // A path-following slot is derived: body-drag its CENTERLINE instead
        // (delta translation preserves the cursor offset for free), and the slot
        // re-derives to follow; otherwise it slides off its centerline (Aaron).
        if (ent && ent->type == SketchEntityType::Slot && ent->pathEntityIds.size() == 1) {
            SketchEntity* cl = entityById(ent->pathEntityIds[0]);
            if (cl && !cl->points.empty()) {
                const QPointF delta = worldPos - m_bodyDragLastWorld;
                m_bodyDragLastWorld = worldPos;
                for (auto& p : cl->points) p = QPointF(p) + delta;
                std::vector<std::pair<int, int>> dragged;
                for (int i = 0; i < static_cast<int>(cl->points.size()); ++i)
                    dragged.push_back({cl->id, i});
                solveConstraintsDragging(dragged);
                updateSlotsFromPaths();   // re-derive live so the slot follows during the drag (the phantom)
                update();
                return true;
            }
        }
        if (ent) {
            const QPointF delta = worldPos - m_bodyDragLastWorld;
            m_bodyDragLastWorld = worldPos;
            // Original (pre-delta) positions of the dragged entity's points, to
            // recognize the corners it shares (coincident) with its neighbors.
            std::vector<QPointF> draggedOrig;
            draggedOrig.reserve(ent->points.size());
            for (auto& p : ent->points) draggedOrig.push_back(QPointF(p));
            for (auto& p : ent->points) p = QPointF(p) + delta;

            std::vector<std::pair<int, int>> dragged;
            for (int i = 0; i < ent->points.size(); ++i) dragged.push_back({ent->id, i});
            // Deforming body-drag (Fusion-style): the grabbed element absorbs the
            // deformation (its length is free) instead of shoving distant geometry.
            // INTERIM implementation: temporarily HARD-FIX the rest of the
            // sketch's points during this solve so the far corner stays put,
            // EXCEPT the corners SHARED (coincident) with the dragged element,
            // which must follow. The temp fixes are removed the instant the solve
            // returns, so the PERSISTED sketch DOF is never changed (nothing is
            // serialized or undone). A proper tunable "soft weight" on the far
            // points is the follow-on (per-param drag weights in forked libslvs).
            const double eps = kSnapWeldEps;
#if defined(SLVS_HAS_DRAG_WEIGHTS)
            // PROPER (patch 0009): give the far, non-shared points a high drag
            // stiffness so they resist strongly but tunably: the grabbed
            // element deforms and the far corner stays, without a hard fix and
            // without ever changing the persisted DOF. Corners SHARED
            // (coincident) with the dragged element are left free to follow.
            std::vector<std::pair<int, int>> farPts;
            for (const auto& e : m_entities) {
                if (e.id == ent->id) continue;
                for (int i = 0; i < e.points.size(); ++i) {
                    const QPointF pp(e.points[i]);
                    bool sharedWithDragged = false;
                    for (const QPointF& o : draggedOrig)
                        if (QLineF(pp, o).length() < eps) { sharedWithDragged = true; break; }
                    if (!sharedWithDragged) farPts.push_back({e.id, i});
                }
            }
            m_dragWeightPoints = farPts;
            // Hold the far points far harder than the grabbed element (whose
            // dragged default behaves like stiffness 20), so they stay put.
            m_dragWeightStiffness = 400.0;
            solveConstraintsDragging(dragged);
#else
            // INTERIM (linked libslvs has no per-param drag weights): temporarily
            // HARD-FIX the far, non-shared points for this solve so the far corner
            // stays; removed the instant the solve returns, so the PERSISTED
            // sketch DOF is never changed.
            std::vector<int> tempIds;
            for (const auto& e : m_entities) {
                if (e.id == ent->id) continue;
                for (int i = 0; i < e.points.size(); ++i) {
                    const QPointF pp(e.points[i]);
                    bool sharedWithDragged = false;
                    for (const QPointF& o : draggedOrig)
                        if (QLineF(pp, o).length() < eps) { sharedWithDragged = true; break; }
                    if (sharedWithDragged) continue;
                    appendTempFixedPoint(e.id, i, tempIds);
                }
            }
            solveConstraintsDragging(dragged);
            if (!tempIds.empty())
                m_constraints.erase(
                    std::remove_if(m_constraints.begin(), m_constraints.end(),
                        [](const SketchConstraint& c) { return c.id <= -1000000; }),
                    m_constraints.end());
#endif
            emit entityDragging(ent->id);
            update();
        }
        return true;
    }
    return false;
}

// Handle drag, shared first step: the snapped/axis-locked target for the
// grabbed point, floored by keepHandleApart so no drag collapses an edge.
QPointF SketchCanvas::computeHandleFinalPos(const SketchEntity* sel, const QPointF& worldPos,
                                            bool shiftPressed, bool ctrlPressed)
{
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
            finalPos = m_snapEngine.snapPoint(worldPos);
        }

        // Do not let a drag drive an edge to zero. Applied here, after
        // snapping and before any entity-specific branch below, because
        // this is the one place every handle drag passes through.
        //
        // A collapsed edge cannot be undone by dragging back out: a
        // zero-length line has no direction, so its horizontal/vertical
        // constraints go slack and the coincidents hold the corners
        // together. The rectangle becomes a point that can only be
        // translated, and the solver reports success the whole time.
        //
        // The floor follows the zoom rather than being a fixed world
        // distance, so the smallest edge you can make is always still
        // visible and grabbable at the zoom you are working at.
        {
            QVector<QPointF> obstacles;
            auto addPoints = [&](const SketchEntity& e, bool skipDragged) {
                for (int i = 0; i < e.points.size(); ++i) {
                    if (skipDragged && i == m_dragHandleIndex) continue;
                    obstacles.append(QPointF(e.points[i]));
                }
            };
            addPoints(*sel, /*skipDragged=*/true);
            if (sel->groupId >= 0) {
                for (const auto& e : m_entities)
                    if (e.groupId == sel->groupId && e.id != sel->id)
                        addPoints(e, /*skipDragged=*/false);
            }
            std::vector<hobbycad::Point2D> others;
            others.reserve(obstacles.size());
            for (const QPointF& p : obstacles)
                others.push_back(hobbycad::Point2D{p.x(), p.y()});

            const hobbycad::Point2D guarded = sketch::keepHandleApart(
                hobbycad::Point2D{finalPos.x(), finalPos.y()}, others,
                hobbycad::Point2D{m_dragHandleOriginal.x(),
                                  m_dragHandleOriginal.y()},
                sketch::minHandleSeparation(m_zoom));
            finalPos = QPointF(guarded.x, guarded.y);
        }
    return finalPos;
}

// Handle drag: circle (shared geometry via sketch::dragEntityHandle; the
// radius/diameter label follow-along stays here because it needs m_constraints).
// Handle drag: line. Solves inline as a dragged-point solve (so the shared
// tail's type gate skips it) and keeps a distance label's perpendicular offset.
void SketchCanvas::dragLineHandle(SketchEntity* sel, const QPointF& finalPos)
{
        // Dragged-point solve. The grabbed endpoint goes where the
        // cursor is; the solver is told it is the dragged point and
        // moves everything else as little as the constraints require
        // (Onshape/Fusion drag). No group special cases: a rigid
        // group move is the Transform section's Free Move.
        SketchConstraint* distConstraint = findDrivingConstraint(sel->id, ConstraintType::Distance);
        double labelPerpOffset = 0.0;
        if (distConstraint && distConstraint->labelVisible) {
            QPointF along = QPointF(sel->points[1]) - QPointF(sel->points[0]);
            double len = geometry::length(along);
            if (len > geometry::kDegenerateLen) {
                QPointF perp = geometry::perpendicular(geometry::normalize(along));
                QPointF mid = (QPointF(sel->points[0]) + QPointF(sel->points[1])) / 2.0;
                QPointF rel = QPointF(distConstraint->labelPosition) - mid;
                labelPerpOffset = rel.x() * perp.x() + rel.y() * perp.y();
            }
        }
        const QPointF prevPos = sel->points[m_dragHandleIndex];
        sel->points[m_dragHandleIndex] = finalPos;
        // Points that sat exactly on the grabbed one (snapped
        // coincidents) start the solve at the new position too.
        const double coinEps = geometry::kCoincidentTol;
        for (auto& e : m_entities) {
            if (e.id == sel->id) continue;
            for (auto& p : e.points) {
                const double dx = p.x - prevPos.x(), dy = p.y - prevPos.y();
                if (dx * dx + dy * dy < coinEps) p = finalPos;
            }
        }
        if (!m_constraints.isEmpty()) {
            // Hold other geometry rigid so swinging this endpoint around
            // can't collapse a tangent partner (e.g. an arc). (Aaron)
            solveHandleDragStabilized(sel->id, m_dragHandleIndex, finalPos);
        }
        if (distConstraint) {
            distConstraint = findDrivingConstraint(sel->id, ConstraintType::Distance);
            if (distConstraint && distConstraint->labelVisible) {
                QPointF along = QPointF(sel->points[1]) - QPointF(sel->points[0]);
                double len = geometry::length(along);
                if (len > geometry::kDegenerateLen) {
                    QPointF perp = geometry::perpendicular(geometry::normalize(along));
                    QPointF mid = (QPointF(sel->points[0]) + QPointF(sel->points[1])) / 2.0;
                    distConstraint->labelPosition = mid + perp * labelPerpOffset;
                }
            }
        }
}


void SketchCanvas::mouseMoveEvent(QMouseEvent* event)
{
    QPointF worldPos = screenToWorld(event->pos());
    updateHoverGrips(worldPos);
    bool ctrlHeld = event->modifiers() & Qt::ControlModifier;
    bool altHeld = event->modifiers() & Qt::AltModifier;

    // A tool that wants raw move events even when nothing is being drawn (the
    // Trim tool's drag-through) gets first refusal here. Only such a handler
    // returns true; every drawing tool declines and falls through unchanged.
    if (activeHandler() && activeHandler()->mouseMove(*this, event, worldPos)) {
        return;
    }

    applySnapAndInference(worldPos, ctrlHeld, altHeld);
    emitCursorPositions();

    // Priority-ordered interaction guards. Order is load-bearing: each
    // returns true when it owned this move (and has already repainted).
    if (handlePanMove(event)) return;
    if (handleWindowSelectMove(worldPos)) return;
    if (handleTransformPivotDragMove()) return;
    if (handleFreeMoveDragMove(event, worldPos)) return;
    updateTransformGlyphCursor(event, worldPos);
    if (handleBackgroundDragMove(worldPos)) return;
    updateBackgroundEditCursor(worldPos);
    if (handleConstraintLabelDragMove(worldPos)) return;
    if (promotePointPressToHandleDrag(event, worldPos)) return;
    armBodyDragIfPastThreshold(event);          // falls through by design
    if (handleBodyDragMove(worldPos)) return;

    if (m_isDraggingHandle) {
        // Move the handle point of the selected entity
        SketchEntity* sel = selectedEntity();
        // A path-following slot is DERIVED from its centerline, so dragging one
        // of its handles must move the CENTERLINE (whose point i the slot's
        // point i mirrors); the slot then re-derives to follow. Without this the
        // slot's own points move while the centerline stays put and the two
        // detach (Aaron: "move the slot, centerline stays in place").
        if (sel && sel->type == SketchEntityType::Slot && sel->pathEntityIds.size() == 1
                && m_dragHandleIndex >= 0) {
            SketchEntity* cl = entityById(sel->pathEntityIds[0]);
            if (cl && m_dragHandleIndex < static_cast<int>(cl->points.size())) {
                const QPointF finalPos = m_snapEngine.snapPoint(worldPos);
                solveHandleDragStabilized(cl->id, m_dragHandleIndex, finalPos);
                updateSlotsFromPaths();   // re-derive live so the slot follows during the drag (the phantom)
                m_lastRawMouseWorld = worldPos;
                update();
                return;
            }
        }
        if (sel && m_dragHandleIndex >= 0 && m_dragHandleIndex < sel->points.size()) {
            m_lastRawMouseWorld = worldPos;
            bool shiftPressed = (event->modifiers() & Qt::ShiftModifier);
            bool ctrlPressed = (event->modifiers() & Qt::ControlModifier);

            // Snapped / axis-locked target for the grabbed point, floored so the
            // drag cannot collapse an edge (see computeHandleFinalPos).
            QPointF finalPos = computeHandleFinalPos(sel, worldPos, shiftPressed, ctrlPressed);

            // A line is a dragged-point SOLVE (its own path); opening a full
            // arc is a stateful gesture kept here; everything else is the
            // library's handle drag, with the locks and modes resolved in
            // applyHandleDrag (the same call the Ctrl-snap path makes).
            if (sel->type == SketchEntityType::Line && sel->points.size() == 2) {
                dragLineHandle(sel, finalPos);
            } else if (sel->type == SketchEntityType::Arc && sel->points.size() >= 3
                       && m_openingFullArc && m_dragHandleIndex != 0
                       && sel->tangentEntityId < 0) {
                // Opening the full circle: shrink from 360, keep the sign,
                // stay under 360, swapping the dragged end at the inflection.
                const QPointF center = sel->points[0];
                const sketch::ArcOpenResult res = sketch::openFullArcByDrag(
                    center, sel->radius, finalPos,
                    m_openArcFixedAngle, m_openArcDraggedIndex, m_openArcPrevSweep);
                sel->startAngle = res.startAngle;
                sel->sweepAngle = res.sweepAngle;
                sketch::resyncArcEndpoints(*sel);
                m_openArcPrevSweep = res.sweepAngle;
                m_openArcDraggedIndex = res.draggedIndex;
                syncSweepAngleConstructionLines(*sel);
            } else {
                applyHandleDrag(*sel, m_dragHandleIndex, finalPos, ctrlPressed,
                                shiftPressed, altHeld);
            }

            // Non-line entities: run solver normally
            // (Lines use temporary FixedPoint pins above.)
            // Skip solver for tangent arcs during handle drag: the solver
            // doesn't know about the tangency relationship and would move
            // the center/tangent-point off the entity.  The solver still
            // runs on mouse-release (after the drag ends).
            if (sel && sel->type != SketchEntityType::Line
                    && !m_constraints.isEmpty()
                    && !(sel->type == SketchEntityType::Arc
                         && sel->tangentEntityId >= 0)) {
                // Anchor the dragged handle (like the line handle-drag path) so
                // the solver makes a MINIMAL move from the current geometry
                // rather than an unanchored re-solve. Unanchored, an
                // under-constrained tangent arc had nothing holding its size, so
                // the solver could collapse the radius toward zero (arc
                // "disappears") or shrink the partner line. (Aaron)
                if (m_dragHandleIndex >= 0
                        && m_dragHandleIndex < static_cast<int>(sel->points.size())) {
                    solveHandleDragStabilized(sel->id, m_dragHandleIndex,
                                              QPointF(sel->points[m_dragHandleIndex]));
                } else {
                    solveConstraints();
                }
            }

            // Emit real-time property update
            if (m_selectedId >= 0) {
                emit entityDragging(m_selectedId);
            }

            // If the dragged entity is a slot's centerline (dragged directly,
            // not via the slot's own grip), the slot must re-derive live too;
            // otherwise its phantom lags the centerline until release (Aaron).
            // Dirty-checked, so a drag that touches no path pays nothing.
            updateSlotsFromPaths();

            update();
        }
        return;
    }

    if (m_isDrawing) {
        updateEntity(m_currentMouseWorld);

        // When in pre-fill selectAll mode, mouse movement keeps live tracking.
        // Once the user starts typing (selectAll becomes false), don't interfere;
        // otherwise mouse tremor during keyboard input resets their typed value.
        // (selectAll=true is maintained as a no-op; selectAll=false is left alone.)

        // Check if we've moved enough to consider this a drag (5 pixels threshold)
        if (!m_wasDragged) {
            QPointF delta = event->pos() - m_drawStartPos;
            if (delta.manhattanLength() > kDrawDragThresholdPx) {
                m_wasDragged = true;
            }
        }
    }

    // Select-mode hover: the cursor is the plain arrow whether or not a handle
    // is under it (the former hit test only ever chose Arrow either way).
    if (m_activeTool == SketchTool::Select && !m_isPanning && !m_isDraggingHandle)
        setCursor(Qt::ArrowCursor);

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
        // An armed point-press that never moved is a point SELECTION.
        if (m_pointPressArmed) {
            m_pointPressArmed = false;
            const bool ctrl  = m_pointPressMods & Qt::ControlModifier;
            const bool shift = m_pointPressMods & Qt::ShiftModifier;
            selectPoint(m_pointPressEntity, m_pointPressIndex,
                        /*addToSelection=*/shift, /*toggle=*/ctrl);
            return;
        }

        // A non-drawing tool that took the press (the Trim drag-through) ends
        // its gesture here; drawing tools finish through the m_isDrawing path
        // below, so this only fires when nothing is being drawn.
        if (!m_isDrawing && activeHandler()
            && activeHandler()->mouseRelease(*this, event, screenToWorld(event->pos()))) {
            return;
        }

        // Deferred line-chaining finish (F-3): a drag off the chain point
        // sweeps a tangent arc against the previous line; a plain click lays a
        // straight segment.
        if (m_lineChainPressActive) {
            m_lineChainPressActive = false;
            updateEntity(m_currentMouseWorld);
            const SketchEntity* prev = entityById(m_chainFromEntityId);
            if (m_wasDragged && prev && prev->type == SketchEntityType::Line) {
                commitTangentArcSegment();
            } else {
                recordEndpointSnap();   // closing on an existing point -> Coincident
                finishEntity();
            }
            return;
        }
        // Finish background drag
        if (m_transformPivotDragging) {
            m_transformPivotDragging = false;    // it stays where the last move (or the press) put it
            setCursor(transformStarHit(event->pos()) ? Qt::OpenHandCursor : (m_transformPick != TransformPick::None ? Qt::CrossCursor : Qt::ArrowCursor));
            update();
            return;
        }
        if (m_freeMoveHandle != FreeMoveHandle::None) {
            m_freeMoveHandle = FreeMoveHandle::None;   // the preview stays until Apply
            setCursor(Qt::OpenHandCursor);
            update();
            return;
        }
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
            if (selRect.width() > kMinRubberBandPx / m_zoom && selRect.height() > kMinRubberBandPx / m_zoom) {
                // Ctrl or Shift both keep the existing selection (rubber-band
                // adds); plain drag replaces.
                bool keepSelection = (event->modifiers() & Qt::ControlModifier)
                                   || (event->modifiers() & Qt::ShiftModifier);
                selectEntitiesInRect(selRect, m_windowSelectCrossing, keepSelection);
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

        m_bodyDragArmed = false;
        if (m_isDraggingBody) {
            m_isDraggingBody = false;
            setCursor(Qt::ArrowCursor);
            std::vector<sketch::UndoCommand> subs;
            for (const SketchEntity& before : m_dragSnapshotEntities) {
                const SketchEntity* now = entityById(before.id);
                if (!now) continue;
                if (now->points != before.points || now->radius != before.radius
                    || now->startAngle != before.startAngle || now->sweepAngle != before.sweepAngle)
                    subs.push_back(sketch::UndoCommand::modifyEntity(before, *now, "Move"));
            }
            for (const SketchConstraint& before : m_dragSnapshotConstraints) {
                const SketchConstraint* now = constraintById(before.id);
                if (now && now->labelPosition != before.labelPosition)
                    subs.push_back(sketch::UndoCommand::modifyConstraint(before, *now, "Move label"));
            }
            if (subs.size() == 1) pushUndoCommand(subs.front());
            else if (!subs.empty()) pushUndoCommand(sketch::UndoCommand::compound(subs, "Move"));
            m_dragSnapshotEntities.clear();
            m_dragSnapshotConstraints.clear();
            solveConstraints();
            if (m_bodyDragEntityId >= 0) emit entityModified(m_bodyDragEntityId);
            m_bodyDragEntityId = -1;
            return;
        }
        if (m_isDraggingHandle) {
            // Finish handle drag - emit modified signal
            const int draggedHandle = m_dragHandleIndex;   // before the reset below
            m_isDraggingHandle = false;
            m_dragHandleIndex = -1;
            m_snapAxis = SnapAxis::None;  // Reset axis lock
            m_shiftWasPressed = false;
            m_ctrlWasPressed = false;
            setCursor(Qt::ArrowCursor);

            // Record undo command for the drag if geometry changed.
            // A group drag changed every member: record them all in one
            // compound so a single Ctrl+Z restores the whole group.
            if (!m_dragOriginalGroupEntities.isEmpty()) {
                std::vector<sketch::UndoCommand> subs;
                for (const SketchEntity& before : m_dragOriginalGroupEntities) {
                    const SketchEntity* now = entityById(before.id);
                    if (!now) continue;
                    if (now->points != before.points || now->radius != before.radius ||
                        now->startAngle != before.startAngle || now->sweepAngle != before.sweepAngle)
                        subs.push_back(sketch::UndoCommand::modifyEntity(before, *now, "Move group member"));
                }
                for (const SketchConstraint& before : m_dragOriginalGroupConstraints) {
                    const SketchConstraint* now = constraintById(before.id);
                    if (now && now->labelPosition != before.labelPosition)
                        subs.push_back(sketch::UndoCommand::modifyConstraint(before, *now, "Move group label"));
                }
                if (!subs.empty())
                    pushUndoCommand(sketch::UndoCommand::compound(subs, "Move group"));
                m_dragOriginalGroupEntities.clear();
                m_dragOriginalGroupConstraints.clear();
                if (m_selectedId >= 0) emit entityModified(m_selectedId);
            } else if (m_selectedId >= 0) {
                SketchEntity* entity = entityById(m_selectedId);
                if (entity) {
                    sketch::Entity current = *entity;
                    if (current.points != m_dragOriginalEntity.points ||
                        current.radius != m_dragOriginalEntity.radius ||
                        current.startAngle != m_dragOriginalEntity.startAngle ||
                        current.sweepAngle != m_dragOriginalEntity.sweepAngle) {
                        pushUndoCommand(sketch::UndoCommand::modifyEntity(
                            m_dragOriginalEntity, current, "Resize"));
                    }
                    // If the dragged point landed on another point (via snap),
                    // join them with a Coincident, the constraint the join
                    // implies. Added before the solve below so it is enforced.
                    createCoincidenceOnDrag(m_selectedId, draggedHandle);
                    // Opening a circle (one 360-degree arc with both ends at the
                    // cut) by dragging one end away: the end left in place ties
                    // to whatever entity sits at the cut. The moving end may have
                    // swapped during the drag, so use the current dragged index
                    // (its twin is the end that stayed at the cut).
                    if (m_openingFullArc)
                        tieOpenedArcEndOnDrag(m_selectedId, m_openArcDraggedIndex);
                    m_openingFullArc = false;
                }
                emit entityModified(m_selectedId);
            }
            // Re-solve constraints so dimensions are enforced after resize
            solveConstraints();

            // Re-establish tangency for tangent arcs after solver.
            // The solver enforces constraints (e.g. locked radius) but has
            // no concept of tangency, so the center can drift off the
            // perpendicular to the tangent entity.  Fix: keep solver's
            // radius & sweep, re-project tangent point onto the entity,
            // and reposition center = tanPt + radius * normal.
            if (m_selectedId >= 0) {
                SketchEntity* arc = entityById(m_selectedId);
                if (arc && arc->type == SketchEntityType::Arc
                        && arc->tangentEntityId >= 0
                        && arc->points.size() >= 3) {
                    reestablishTangency(*arc);
                }
            }

            return;
        }

        if (m_isDrawing) {
            // Every tool-specific release path now lives in its handler:
            // Point commits here, the staged modes (3-point arc, arc slot,
            // 3-point and parallelogram rectangle, 3-point circle) place
            // their next point, freeform polygon adds a vertex, and tangent
            // arc deliberately consumes the release without finishing.
            if (activeHandler()
                && activeHandler()->mouseRelease(*this, event,
                                                 screenToWorld(event->pos()))) {
                return;
            }

            // Two-point tools (Line, Rectangle, Circle, Slot, Ellipse,
            // Polygon) accept both click-drag and click-click. A drag ends
            // the entity here; a plain click leaves it in progress so the
            // second press finishes it.
            if (m_wasDragged) {
                finishEntity();
            }
        }
    }
}

void SketchCanvas::wheelEvent(QWheelEvent* event)
{
    // The active tool handles the wheel; if it declines, fall through to zoom.
    if (SketchToolHandler* h = activeHandler()) {
        if (h->wheel(*this, event)) {
            update();
            event->accept();
            return;
        }
    }

    // (The per-tool wheel switch that used to live here is gone: Slot and
    // Polygon now handle the wheel in their tool handlers, which are asked
    // first, above.)


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
    QPointF snapped = m_snapEngine.snapPoint(worldPos);

    if (m_snapAxis == SnapAxis::None)
        return snapped;

    geometry::Axis axis = (m_snapAxis == SnapAxis::X) ? geometry::Axis::X : geometry::Axis::Y;
    return geometry::constrainToAxis(snapped, m_dragHandleOriginal, axis);
}


// =====================================================================
//  Handle drag glue
//
//  The geometry itself lives in libhobbycad (sketch/handles.h).  These
//  helpers cover what is genuinely GUI/model state: resolving driving
//  constraints, keeping dimension labels attached, and propagating a
//  moved point to coincident neighbors in the same group.
// =====================================================================

/// Reposition Radius/Diameter labels so they follow their circle.
void SketchCanvas::moveCircleDimensionLabels(
        const SketchEntity& sel, int handleIndex,
        const sketch::HandleDragResult& drag)
{
    const QPointF oldCenter(drag.oldCenter);
    const QPointF newCenter(drag.newCenter);
    const QPointF centerDelta = newCenter - oldCenter;

    for (auto& c : m_constraints) {
        if (!c.labelVisible) continue;
        if (c.type != ConstraintType::Radius
            && c.type != ConstraintType::Diameter) continue;

        for (int eid : c.entityIds) {
            if (eid != sel.id) continue;

            if (sel.points.size() == 2 && handleIndex >= 1) {
                // Center-radius circle, perimeter dragged: the label keeps its
                // distance from the center and turns to the perimeter point.
                const QPointF dir = QPointF(sel.points[1]) - newCenter;
                const double len = geometry::length(dir);
                double labelDist = geometry::length(QPointF(c.labelPosition) - newCenter);
                if (labelDist < geometry::kDegenerateLen) labelDist = sel.radius / 2.0;
                if (len > geometry::kDegenerateLen) {
                    const double newAngle = std::atan2(dir.y(), dir.x());
                    c.labelPosition = geometry::polarPoint(newCenter, labelDist, newAngle);
                    c.labelAngle = newAngle;
                }
            } else if (drag.diameterRotation) {
                // The circle rotated about its fixed endpoint, so swing
                // the label through the same angle instead of translating.
                const QPointF oldOffset = QPointF(c.labelPosition) - oldCenter;
                const double labelDist = geometry::length(oldOffset);
                const double oldLabelAngle =
                    std::atan2(oldOffset.y(), oldOffset.x());

                const int fixedIdx = (handleIndex == 1) ? 2 : 1;
                const QPointF oldFixedDir =
                    QPointF(sel.points[fixedIdx]) - oldCenter;
                const QPointF newFixedDir =
                    QPointF(sel.points[fixedIdx]) - newCenter;
                const double angleDelta =
                    std::atan2(newFixedDir.y(), newFixedDir.x())
                    - std::atan2(oldFixedDir.y(), oldFixedDir.x());

                const double a = oldLabelAngle + angleDelta;
                c.labelPosition = geometry::polarPoint(newCenter, labelDist, a);
                c.labelAngle = a;
            } else {
                c.labelPosition += centerDelta;
            }
            break;
        }
    }
}

/// Move coincident points of sibling entities in the same group so the
/// shape cannot open at a shared corner.
void SketchCanvas::propagateCoincidentNeighbors(
        const SketchEntity& sel, const QPointF& prevPos, const QPointF& newPos)
{
    if (sel.groupId < 0) return;

    const double coinEps = geometry::kCoincidentTol;
    for (auto& e : m_entities) {
        if (e.groupId != sel.groupId || e.id == sel.id) continue;
        for (std::size_t pi = 0; pi < e.points.size(); ++pi) {
            const double dx = e.points[pi].x - prevPos.x();
            const double dy = e.points[pi].y - prevPos.y();
            if (dx * dx + dy * dy < coinEps)
                e.points[pi] = newPos;
        }
    }
}

/// Resolve driving constraints, run the library handle-drag geometry,
/// then apply the GUI-side consequences.
void SketchCanvas::applyHandleDrag(SketchEntity& sel, int handleIndex,
                                   const QPointF& finalPos, bool ctrlPressed,
                                   bool shiftPressed, bool altPressed)
{
    sketch::HandleDragLocks locks;
    if (shiftPressed) locks.slotEndMode = sketch::HandleDragLocks::SlotEndMode::ResizeAboutOther;
    else if (altPressed) locks.slotEndMode = sketch::HandleDragLocks::SlotEndMode::FreeResize;
    locks.fixedHandleIndex = m_fixedHandleIndex;
    if (sel.type == SketchEntityType::Arc && sel.tangentEntityId >= 0)
        locks.tangentHost = entityById(sel.tangentEntityId);

    if (const SketchConstraint* rc =
            findDrivingConstraint(sel.id, ConstraintType::Radius)) {
        locks.radius = rc->value;
    } else if (const SketchConstraint* dc =
            findDrivingConstraint(sel.id, ConstraintType::Diameter)) {
        locks.radius = dc->value / 2.0;
    }

    if (sel.type == SketchEntityType::Arc && handleIndex != 0) {
        const int sweepGid = findSweepAngleGroupForArc(sel.id);
        if (sweepGid >= 0) {
            for (const auto& g : m_groups) {
                if (g.id != sweepGid) continue;
                for (int cid : g.constraintIds) {
                    const SketchConstraint* c = constraintById(cid);
                    if (c && c->type == ConstraintType::Angle
                            && c->isDriving && c->enabled) {
                        locks.sweepAngle = c->value;
                        break;
                    }
                }
                break;
            }
        }
    }

    if (ctrlPressed) locks.angleSnapIncrement = M_PI / 4.0;

    const sketch::HandleDragResult drag =
        sketch::dragEntityHandle(sel, handleIndex, finalPos, locks);
    if (!drag.changed) return;

    if (sel.type == SketchEntityType::Circle) {
        // A center-radius circle's perimeter drag moves no center but its
        // label follows the perimeter point; see moveCircleDimensionLabels.
        const bool perimFollow = sel.points.size() == 2 && handleIndex >= 1;
        if (drag.centerMoved || perimFollow) moveCircleDimensionLabels(sel, handleIndex, drag);
    } else if (sel.type == SketchEntityType::Arc && sel.points.size() >= 3) {
        syncSweepAngleConstructionLines(sel);
    }

    if (drag.usedFallback) {
        propagateCoincidentNeighbors(sel, QPointF(drag.previousHandlePos),
                                      finalPos);
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
            finalPos = m_snapEngine.snapPoint(m_lastRawMouseWorld);
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

    // Geometry now lives in libhobbycad; this layer owns snapping,
    // constraint lookup, label placement and group propagation.
    applyHandleDrag(*sel, m_dragHandleIndex, finalPos, m_ctrlWasPressed);

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
            if (m_isDrawing && !m_dimInput.empty()) {
                keyPressEvent(ke);   // route to our handler
                return true;         // consumed
            }
        }
    }
    return QWidget::event(event);
}

// keyPressEvent while an inline constraint value edit is active: every key
// is consumed here.
void SketchCanvas::handleInlineEditKey(QKeyEvent* event)
{
    int key = event->key();

    // Helper: replace entire buffer if selectAll, otherwise insert at cursor
    auto replaceOrInsert = [&](QChar ch) {
        if (m_inlineEditSelectAll) {
            m_inlineEditBuffer = QString(ch);
            m_inlineEditCursorPos = 1;
            m_inlineEditSelectAll = false;
        } else {
            m_inlineEditBuffer.insert(m_inlineEditCursorPos, ch);
            m_inlineEditCursorPos++;
        }
    };

    // Printable characters valid in expressions
    QString text = event->text();
    if (!text.isEmpty()) {
        QChar ch = text[0];
        if (ch.isDigit() || ch.isLetter() || ch == QLatin1Char('_') ||
            ch == QLatin1Char('.') || ch == QLatin1Char('-') || ch == QLatin1Char('+') ||
            ch == QLatin1Char('*') || ch == QLatin1Char('/') || ch == QLatin1Char('^') ||
            ch == QLatin1Char('(') || ch == QLatin1Char(')') || ch == QLatin1Char(',') ||
            ch == QLatin1Char(' ') || ch == QLatin1Char('%') ||
            ch == QChar(0x00B0) ||    // ° degree sign
            ch == QLatin1Char('\'') || ch == QLatin1Char('"') ||
            ch == QChar(0x2032) || ch == QChar(0x2033)) {
            replaceOrInsert(ch);
            update();
            return;
        }
    }
    if (key == Qt::Key_Backspace) {
        if (m_inlineEditSelectAll) {
            m_inlineEditBuffer.clear();
            m_inlineEditCursorPos = 0;
            m_inlineEditSelectAll = false;
        } else if (m_inlineEditCursorPos > 0) {
            m_inlineEditBuffer.remove(m_inlineEditCursorPos - 1, 1);
            m_inlineEditCursorPos--;
        }
        update();
        return;
    }
    if (key == Qt::Key_Delete) {
        if (m_inlineEditSelectAll) {
            m_inlineEditBuffer.clear();
            m_inlineEditCursorPos = 0;
            m_inlineEditSelectAll = false;
        } else if (m_inlineEditCursorPos < m_inlineEditBuffer.length()) {
            m_inlineEditBuffer.remove(m_inlineEditCursorPos, 1);
        }
        update();
        return;
    }
    if (key == Qt::Key_Left) {
        if (m_inlineEditSelectAll) {
            m_inlineEditCursorPos = 0;
            m_inlineEditSelectAll = false;
        } else if (m_inlineEditCursorPos > 0) {
            m_inlineEditCursorPos--;
        }
        update();
        return;
    }
    if (key == Qt::Key_Right) {
        if (m_inlineEditSelectAll) {
            m_inlineEditSelectAll = false;
        } else if (m_inlineEditCursorPos < m_inlineEditBuffer.length()) {
            m_inlineEditCursorPos++;
        }
        update();
        return;
    }
    if (key == Qt::Key_Home) {
        m_inlineEditSelectAll = false;
        m_inlineEditCursorPos = 0;
        update();
        return;
    }
    if (key == Qt::Key_End) {
        m_inlineEditSelectAll = false;
        m_inlineEditCursorPos = m_inlineEditBuffer.length();
        update();
        return;
    }
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        commitInlineConstraintEdit();
        return;
    }
    if (key == Qt::Key_Escape) {
        cancelInlineConstraintEdit();
        return;
    }
    // Ctrl+A = select all
    if (key == Qt::Key_A && (event->modifiers() & Qt::ControlModifier)) {
        m_inlineEditSelectAll = true;
        update();
        return;
    }
    // Consume all other keys while inline edit is active
    return;
}

// keyPressEvent, Escape: the cancel cascade (transform gesture, drawing,
// tool, entered group, constraint, selection, sketch), one step per press.
void SketchCanvas::handleEscapeKey()
{
    if (m_transformPick != TransformPick::None || m_transformPivotDragging
        || m_freeMoveHandle != FreeMoveHandle::None || !m_transformPreview.isEmpty()) {
        // Step 0 of the cascade: cancelling a transform gesture must not
        // also drop the selection the transform was about.
        cancelTransformPick();
        clearTransformPreview();
        m_freeMoveDelta = QPointF(); m_freeMoveAngle = 0.0;
        emit transformCanceled();
        emit toolHintChanged(tr("Transform canceled; selection kept"));
        update();
        return;
    }
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
        emit selectionChanged(-1);  // Update properties panel
    } else if (!m_selectedIds.isEmpty()) {
        // Already in Select mode with entity selected - deselect all entities
        for (auto& e : m_entities) e.selected = false;
        m_selectedId = -1;
        selectClear();
        emit selectionChanged(-1);
    } else if (m_sketchSelected) {
        // No entity selected, but sketch is selected - show exit bar
        // but stay in the sketch so the user can return
        m_sketchSelected = false;
        emit sketchDeselected();
        emit exitRequested();   // shows Save/Discard bar
    } else {
        // Sketch already deselected: pressing Escape again returns
        // to the sketch instead of being stuck at the Save/Discard bar
        m_sketchSelected = true;
        emit selectionChanged(-1);  // re-engage sketch
    }
    update();
}

// keyPressEvent, Delete/Backspace: the selected constraint, else the
// selected entities (confirmed when there are several).
void SketchCanvas::deleteSelectionKey()
{
    if (m_selectedConstraintId >= 0) {
        deleteConstraintById(m_selectedConstraintId);
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
                return;
            }
        }

        // One deletion path: deleteSelectedEntities() records a single
        // compound and re-solves. (This branch and the Delete QAction
        // used to delete two different ways.)
        deleteSelectedEntities();
    }
}

// keyPressEvent, Tab: cycle the constraint type the D key will add.
void SketchCanvas::cycleDimensionTypeHint(QKeyEvent* event)
{
    if (m_selectedId >= 0) {
        SketchEntity* sel = selectedEntity();
        if (sel) {
            // Build list of available constraint types for this entity
            QStringList typeNames;
            if (sel->type == SketchEntityType::Line) {
                typeNames << tr("Distance");
            } else if (sel->type == SketchEntityType::Circle
                       || sel->type == SketchEntityType::Arc) {
                typeNames << tr("Radius") << tr("Diameter");
            }
            if (typeNames.size() > 1) {
                m_dKeyTypeIndex = (m_dKeyTypeIndex + 1) % typeNames.size();
                m_dKeyTypeHint = typeNames[m_dKeyTypeIndex];
                update();
            }
        }
        event->accept();
        return;
    }
}

// keyPressEvent, D: dimension the selection outright when its type is
// unambiguous (two points, two lines, a line, a circle or arc), else arm the
// Dimension tool.
void SketchCanvas::quickDimensionKey()
{
    // Two selected points (a Bezier leg) -> Distance dimension.
    if (m_selectedPoints.size() == 2) {
        const auto pa = m_selectedPoints[0];
        const auto pb = m_selectedPoints[1];
        const SketchEntity* ea = entityById(pa.first);
        const SketchEntity* eb = entityById(pb.first);
        if (ea && eb && pa.second < ea->points.size() && pb.second < eb->points.size()) {
            const QPointF A(ea->points[pa.second]), B(eb->points[pb.second]);
            const double cur = QLineF(A, B).length();
            m_constraintTargetEntities.clear(); m_constraintTargetPoints.clear();
            m_constraintTargetEntities.append(pa.first); m_constraintTargetPoints.append(A);
            m_constraintTargetEntities.append(pb.first); m_constraintTargetPoints.append(B);
            const QPointF labelPos = (A + B) / 2.0 + QPointF(0, -10);
            createConstraint(ConstraintType::Distance, cur, labelPos, false, true);
            m_constraintTargetEntities.clear(); m_constraintTargetPoints.clear();
            return;
        }
    }
    // Two selected lines -> Angle dimension.
    if (selectedEntityList().size() == 2 && dimensionSelectedLinesAngle()) return;
    // If an entity is selected, immediately add the appropriate
    // constraint.  TAB cycles the type (e.g. Radius ↔ Diameter).
    if (m_selectedId >= 0) {
        SketchEntity* sel = selectedEntity();
        if (!sel) return;

        // --- Line → Distance ---
        if (sel->type == SketchEntityType::Line && sel->points.size() == 2) {
            if (!findDrivingConstraint(sel->id, ConstraintType::Distance)) {
                double currentLen = QLineF(sel->points[0], sel->points[1]).length();
                setConstraintTargetsForLine(sel->id, sel->points[0], sel->points[1]);

                QPointF mid = (sel->points[0] + sel->points[1]) / 2.0;
                QPointF along = sel->points[1] - sel->points[0];
                double len = geometry::length(along);
                QPointF perp = (len > geometry::kDegenerateLen)
                    ? QPointF(geometry::perpendicular(geometry::normalize(along)))
                    : QPointF(0, -1);
                QPointF labelPos = mid + perp * 10.0;

                createConstraint(ConstraintType::Distance, currentLen, labelPos,
                                 /*skipOverConstrainCheck=*/false, /*startEditing=*/true);

                m_dKeyTypeIndex = 0;
                m_dKeyTypeHint.clear();
                return;
            }
        }

        // --- Circle/Arc → Radius or Diameter (TAB toggles) ---
        if ((sel->type == SketchEntityType::Circle
             || sel->type == SketchEntityType::Arc)
            && !sel->points.empty()) {
            if (!findDrivingConstraint(sel->id, ConstraintType::Radius)
                && !findDrivingConstraint(sel->id, ConstraintType::Diameter)) {
                // Index 0 = Radius (default), 1 = Diameter
                ConstraintType ctype = (m_dKeyTypeIndex == 1)
                    ? ConstraintType::Diameter
                    : ConstraintType::Radius;
                bool isDiameter = (ctype == ConstraintType::Diameter);

                double currentValue = isDiameter
                    ? sel->radius * 2.0
                    : sel->radius;

                setConstraintTargetsForRadial(sel->id, sel->points[0]);

                QPointF labelPos = sel->points[0];
                if (sel->points.size() >= 2) {
                    // Place label in direction of p1, at the perimeter
                    QPointF dir = sel->points[1] - sel->points[0];
                    double dirLen = geometry::length(dir);
                    if (dirLen > geometry::kDegenerateLen)
                        labelPos = QPointF(sel->points[0]) + geometry::normalize(dir) * sel->radius;
                } else {
                    labelPos = QPointF(sel->points[0]) + QPointF(sel->radius, 0);
                }

                createConstraint(ctype, currentValue, labelPos,
                                 /*skipOverConstrainCheck=*/false, /*startEditing=*/true);

                m_dKeyTypeIndex = 0;
                m_dKeyTypeHint.clear();
                return;
            }
        }
    }
    setActiveTool(SketchTool::Dimension);
}

void SketchCanvas::keyPressEvent(QKeyEvent* event)
{
    // ---- Inline constraint value editing (on existing constraint labels) ----
    if (m_inlineEditActive) {
        handleInlineEditKey(event);
        return;
    }

    // ---- Inline dimension input routing (during entity creation) ----
    // The dimension-field input subsystem owns typing, expression evaluation,
    // locking, Tab cycling, and Escape/unlock. It returns true when it
    // consumed the key; an empty Enter on a chaining tool (and a bare Escape
    // with nothing to clear) returns false so the cases below can end the
    // chain or cancel the entity.
    if (m_isDrawing && m_dimInput.activeIndex() >= 0) {
        if (m_dimInput.handleKey(event)) return;
    }

    // Modifier keys that mean something to the tool being drawn: Shift
    // flips an arc or arc slot, Ctrl refreshes the Start+End+Radius preview.
    // Each tool answers for itself in gui/tools/.
    if (m_isDrawing && activeHandler()
        && activeHandler()->keyPress(*this, event)) {
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
    if (matchesBinding(QStringLiteral("sketch.trim"), event)) {
        setActiveTool(SketchTool::Trim); return;
    }
    if (matchesBinding(QStringLiteral("sketch.offset"), event)) {
        setActiveTool(SketchTool::Offset); return;
    }
    if (matchesBinding(QStringLiteral("sketch.fillet"), event)) {
        setActiveTool(SketchTool::Fillet); return;
    }
    if (matchesBinding(QStringLiteral("sketch.construction"), event)) {
        toggleSelectedConstruction();
        return;
    }

    switch (event->key()) {
    case Qt::Key_Escape:
        handleEscapeKey();
        break;

    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (!m_transformPreview.isEmpty() || m_transformPick == TransformPick::FreeMove) { emit transformApplyRequested(); return; }
        // End a line chain: discard the rubber-band segment, keep the
        // committed ones (Fusion review C3). Escape does the same.
        if (m_isDrawing && activeHandler() && activeHandler()->chainsFromLastPoint(*this)) {
            cancelEntity();
            emit toolHintChanged(currentToolHint());   // stage hint back to "click the start point"
            break;
        }
        break;

    case Qt::Key_Home:
        if (m_transformGlyphVisible && !m_selectedIds.isEmpty()) { resetTransformPivotToCenter(); return; }
        break;

    case Qt::Key_Delete:
        {   // A selected Bezier anchor deletes that fit point, not the whole spline.
            int aS = -1, aA = -1;
            if (selectedBezierAnchor(aS, aA)) { deleteBezierAnchor(aS, aA); break; }
        }
        cancelTransformPick();
        clearTransformPreview();
    case Qt::Key_Backspace:
        deleteSelectionKey();
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
    case Qt::Key_Tab:
        // Cycle constraint type for D-key quick-add
        cycleDimensionTypeHint(event);
        break;

    case Qt::Key_D:
        quickDimensionKey();
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
        } else {
            QWidget::keyPressEvent(event);  // Let Ctrl+Y (Redo) propagate
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
        } else {
            QWidget::keyPressEvent(event);  // Let Ctrl+Z (Undo) / Ctrl+Shift+Z (Redo) propagate
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

// Collect group names that an entity belongs to
QStringList SketchCanvas::groupNamesForEntity(int eid) const
{
    QStringList names;
    for (const SketchGroup& g : m_groups) {
        if (g.containsEntity(eid))
            names.append(QString::fromStdString(g.name));
    }
    return names;
}

    // Collect the distinct set of group IDs that the current selection
    // (or a single entity) belongs to
QSet<int> SketchCanvas::groupIdsForSelection(const QSet<int>& ids) const
{
    QSet<int> gids;
    for (const SketchGroup& g : m_groups) {
        for (int eid : ids) {
            if (g.containsEntity(eid)) {
                gids.insert(g.id);
                break;
            }
        }
    }
    return gids;
}

    // Adds the "Member of Group(s):" info label at the top of a menu.
    // Grayed-out when no group membership exists.
void SketchCanvas::addGroupInfoLabel(QMenu& menu, const QSet<int>& entityIds)
{
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
    infoAction->setEnabled(false);  // always grayed, informational only
    menu.addSeparator();
}

    // Adds a Constrain submenu offering exactly the geometric constraints
    // the library says apply to the current selection.
    //
    // Used for BOTH the single- and multi-selection menus. It was in the
    // multi-selection one alone at first, which meant the single case
    // (select one line, ask to make it horizontal) silently had no entry,
    // and that is the case people reach for first.
void SketchCanvas::addConstrainMenu(QMenu& menu)
{
    std::vector<ConstraintType> opts;
    const std::vector<int> selIds = selectedEntityList();
    if (selIds.size() >= 2) {
        const SketchEntity* a = entityById(selIds[0]);
        const SketchEntity* b = entityById(selIds[1]);
        if (a && b) opts = sketch::suggestConstraints(*a, *b);
    } else if (selIds.size() == 1) {
        if (const SketchEntity* only = entityById(selIds[0]))
            opts = sketch::suggestConstraints(*only);
    }
    // Dimensional ones need a value, which is the Dimension tool's job.
    opts.erase(std::remove_if(opts.begin(), opts.end(),
                              [](ConstraintType t) {
                                  return !sketch::isGeometricConstraint(t);
                              }),
               opts.end());
    if (opts.empty()) return;

    menu.addSeparator();
    QMenu* constrainMenu = menu.addMenu(tr("Constrain"));
    for (ConstraintType t : opts) {
        QAction* act = constrainMenu->addAction(
            QString::fromUtf8(sketch::constraintTypeName(t)));
        connect(act, &QAction::triggered, this, [this, t]() {
            applyConstraintToSelection(t);
        });
    }
    constrainMenu->addSeparator();
    QAction* fixAct = constrainMenu->addAction(
        selectionHasFixedPoint() ? tr("Unfix") : tr("Fix"));
    connect(fixAct, &QAction::triggered, this, [this]() { fixSelectedEntities(); });
}

    // Adds Transform and Align submenus (used for both single + multi)
void SketchCanvas::addTransformAlignMenus(QMenu& menu)
{
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
}

// Context menu, multi-segment slot: when two or more lines/arcs are selected,
// offer to make a slot that follows them as a path (chain, loop, or
// branching tree). Returns true when it showed a menu.
bool SketchCanvas::showPathSlotContextMenu(const QPoint& globalPos)
{
    int curveCount = 0;
    for (const SketchEntity* e : selectedEntities())
        if (e && (e->type == SketchEntityType::Line
                  || e->type == SketchEntityType::Arc))
            ++curveCount;
    if (curveCount >= 2) {
        QMenu menu(this);
        QAction* mk = menu.addAction(tr("Create Slot from Path"));
        connect(mk, &QAction::triggered, this,
                [this]() { createTreeSlotFromSelection(); });
        menu.exec(globalPos);
        return true;
    }
    return false;
}

// Context menu, Bezier: Insert/Delete Fit Point and Open/Close Spline.
// Returns true when it showed a menu.
bool SketchCanvas::showBezierContextMenu(const SketchEntity* sel, const QPointF& worldPos, const QPoint& globalPos)
{
    int aS = -1, aA = -1;
    const bool anchorSel = selectedBezierAnchor(aS, aA);
    const SketchEntity* bsel =
        (sel && sel->type == SketchEntityType::Spline && sel->splineBezier) ? sel
        : (anchorSel ? entityById(aS) : nullptr);
    if (bsel) {
        QMenu menu(this);
        const int sid = bsel->id;
        const QPointF wp = worldPos;
        QAction* ins = menu.addAction(tr("Insert Fit Point"));
        connect(ins, &QAction::triggered, this,
                [this, sid, wp]() { insertBezierFitPoint(sid, wp); });
        QAction* oc = menu.addAction(bsel->splineClosed ? tr("Open Spline")
                                                        : tr("Close Spline"));
        connect(oc, &QAction::triggered, this,
                [this, sid]() { toggleBezierClosed(sid); });
        if (anchorSel) {
            QAction* del = menu.addAction(tr("Delete Fit Point"));
            connect(del, &QAction::triggered, this,
                    [this, aS, aA]() { deleteBezierAnchor(aS, aA); });
        }
        menu.exec(globalPos);
        return true;
    }
    return false;
}

// Context menu, arc-slot endpoint handle: fix/unfix the point for resize.
// Returns true when it showed a menu.
bool SketchCanvas::showSlotHandleContextMenu(const SketchEntity* sel, const QPointF& worldPos, const QPoint& globalPos)
{
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

        menu.exec(globalPos);
        return true;
    }
    return false;
}

// Context menu on a constraint label: driving/driven, edit value, delete.
// Returns true when it showed a menu.
bool SketchCanvas::showConstraintContextMenu(int constraintId, const QPoint& globalPos)
{
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
            deleteConstraintById(constraintId);
        });

        menu.exec(globalPos);
        return true;
    }
    return false;
}

// Context menu when the right-clicked entity is one of several selected.
void SketchCanvas::showMultiSelectionContextMenu(const QPoint& globalPos)
{
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

    // Construction geometry toggle for all: whichever direction applies, both
    // when the selection is mixed.
    auto addMakeAll = [&](bool toConstruction) {
        QAction* act = menu.addAction(toConstruction
            ? tr("Make All Construction Geometry (%1)").arg(count)
            : tr("Make All Normal Geometry (%1)").arg(count));
        connect(act, &QAction::triggered, this, [this, toConstruction]() {
            for (int id : m_selectedIds) {
                SketchEntity* ent = entityById(id);
                if (ent) ent->isConstruction = toConstruction;
            }
            m_profilesCacheDirty = true;
            emit selectionChanged(m_selectedId);
            update();
        });
    };
    if (allConstruction || !allNormal) addMakeAll(false);   // all construction, or mixed
    if (allNormal || !allConstruction) addMakeAll(true);    // all normal, or mixed

    // --- Per-entity color (0xRRGGBB; -1 = default / by layer) ---
    {
        bool anyColored = false;
        int shared = -2;   // -2 = not yet seen, -1 = mixed
        for (int id : m_selectedIds) {
            const SketchEntity* ent = entityById(id);
            if (!ent) continue;
            if (ent->color >= 0) anyColored = true;
            if (shared == -2) shared = ent->color;
            else if (shared != ent->color) shared = -1;
        }
        QAction* setColorAction = menu.addAction(tr("Set Color... (%1)").arg(count));
        connect(setColorAction, &QAction::triggered, this, [this, shared]() {
            QDialog dlg(this);
            dlg.setWindowTitle(tr("Entity Color"));
            auto* lay = new QVBoxLayout(&dlg);
            auto* picker = new ColorPicker(&dlg);
            if (shared >= 0)
                picker->setColor(QColor((shared >> 16) & 0xFF, (shared >> 8) & 0xFF, shared & 0xFF));
            picker->anchorPrevious();
            lay->addWidget(picker);
            auto* buttons = new QDialogButtonBox(
                QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
            auto* defaultBtn = buttons->addButton(tr("Default (by layer)"),
                                                  QDialogButtonBox::ResetRole);
            lay->addWidget(buttons);
            connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
            connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
            // Reset (Default) closes with a sentinel so the caller clears color.
            bool toDefault = false;
            connect(defaultBtn, &QPushButton::clicked, &dlg, [&dlg, &toDefault]() {
                toDefault = true; dlg.accept();
            });
            if (dlg.exec() != QDialog::Accepted) return;
            int packed = -1;
            if (!toDefault) {
                const QColor c = picker->color();
                packed = (c.red() << 16) | (c.green() << 8) | c.blue();
            }
            for (int id : m_selectedIds) {
                if (SketchEntity* ent = entityById(id)) ent->color = packed;
            }
            emit selectionChanged(m_selectedId);
            update();
        });
        if (anyColored) {
            QAction* clearColorAction = menu.addAction(tr("Clear Color (%1)").arg(count));
            connect(clearColorAction, &QAction::triggered, this, [this]() {
                for (int id : m_selectedIds) {
                    if (SketchEntity* ent = entityById(id)) ent->color = -1;
                }
                emit selectionChanged(m_selectedId);
                update();
            });
        }
    }

    // --- Sweep along the selected path ---
    // Only for a single line or arc: decomposeSweep() offsets one
    // element and caps its two ends, so a longer path would need its
    // offsets trimmed at each joint, which it does not do yet.
    if (m_selectedIds.size() == 1) {
        const SketchEntity* sel = entityById(*m_selectedIds.begin());
        if (sel && (sel->type == SketchEntityType::Line
                    || sel->type == SketchEntityType::Arc)) {
            menu.addSeparator();
            QAction* sweepAction = menu.addAction(tr("Sweep Along This Path..."));
            sweepAction->setToolTip(
                tr("Sweep a width along this line or arc, leaving the path "
                   "itself as the construction centerline"));
            connect(sweepAction, &QAction::triggered, this, [this]() {
                sweepSelectedPath();
            });
        }
    }

    menu.addSeparator();

    // --- Transform / Align (shared helper) ---
    addConstrainMenu(menu);
    addTransformAlignMenus(menu);

    menu.addSeparator();

    // --- Group / Ungroup ---
    QAction* groupAction = menu.addAction(tr("Group (%1 entities)").arg(count));
    connect(groupAction, &QAction::triggered, this, [this]() {
        groupSelectedEntities();
    });

    // Enter Group / Ungroup: show for every group that has at least
    // one selected member
    QSet<int> selGroupIds = groupIdsForSelection(m_selectedIds);
    if (!selGroupIds.isEmpty()) {
        for (int gid : selGroupIds) {
            const SketchGroup* grp = groupById(gid);
            if (!grp) continue;

            QAction* enterAction = menu.addAction(
                tr("Enter Group \"%1\"").arg(QString::fromStdString(grp->name)));
            connect(enterAction, &QAction::triggered, this, [this, gid]() {
                enterGroup(gid);
            });

            QAction* ungroupAction = menu.addAction(
                tr("Ungroup \"%1\"").arg(QString::fromStdString(grp->name)));
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

    // Rejoin: only enabled when all selected are collinear lines
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

    menu.exec(globalPos);
}

// Context menu on a single entity. Returns true when it showed a menu.
bool SketchCanvas::showEntityContextMenu(int entityId, const QPointF& worldPos, const QPoint& globalPos)
{
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
                if (ent->isConstruction) ent->isCenterline = false;
                m_profilesCacheDirty = true;
                emit entityModified(entityId);
                update();
            }
        });

        // Centerline linetype toggle (a dash-dot reference axis; lines
        // only, matching how symmetry and revolve axes are drawn).
        if (entity->type == SketchEntityType::Line) {
            QAction* centerlineAction = menu.addAction(
                entity->isCenterline ? tr("Make Normal Geometry")
                                     : tr("Make Centerline"));
            connect(centerlineAction, &QAction::triggered, this, [this, entityId]() {
                SketchEntity* ent = entityById(entityId);
                if (ent) {
                    ent->isCenterline = !ent->isCenterline;
                    if (ent->isCenterline) ent->isConstruction = false;
                    m_profilesCacheDirty = true;
                    emit entityModified(entityId);
                    update();
                }
            });
        }

        menu.addSeparator();

        // --- Transform / Align (same as multi, operates on selection) ---
        // Ensure this entity is selected so the transform functions work
        if (!m_selectedIds.contains(entityId)) {
            selectEntity(entityId);
        }
        addConstrainMenu(menu);
        addTransformAlignMenus(menu);

        menu.addSeparator();

        // --- Enter Group / Leave Group / Ungroup ---
        if (m_enteredGroupId >= 0) {
            // Already inside a group: offer Leave Group
            QString gName;
            for (const SketchGroup& g : m_groups) {
                if (g.id == m_enteredGroupId) { gName = QString::fromStdString(g.name); break; }
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
                const SketchGroup* grp = groupById(gid);
                if (!grp) continue;

                // Only show Enter Group when not already inside it
                if (m_enteredGroupId != gid) {
                    QAction* enterAction = menu.addAction(
                        tr("Enter Group \"%1\"").arg(QString::fromStdString(grp->name)));
                    connect(enterAction, &QAction::triggered, this, [this, gid]() {
                        enterGroup(gid);
                    });
                }

                QAction* ungroupAction = menu.addAction(
                    tr("Ungroup \"%1\"").arg(QString::fromStdString(grp->name)));
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
                selectRemove(entityId);
                emit selectionChanged(-1);
            }
            m_profilesCacheDirty = true;
            update();
        });

        menu.exec(globalPos);
        return true;
    }
    return false;
}

// Context menu on empty canvas: export options.
void SketchCanvas::showCanvasContextMenu(const QPoint& globalPos)
{
    QMenu menu(this);

    QAction* exportDXFAction = menu.addAction(tr("Export as DXF..."));
    connect(exportDXFAction, &QAction::triggered, this, [this]() {
        QString filePath = QFileDialog::getSaveFileName(
            this, tr("Export DXF File"), QString(),
            tr("DXF Files (*.dxf);;All Files (*)"));
        if (filePath.isEmpty()) return;
        if (!filePath.toLower().endsWith(QLatin1String(".dxf")))
            filePath += QStringLiteral(".dxf");

        std::vector<sketch::Entity> entities;
        entities.reserve(static_cast<size_t>(m_entities.size()));
        for (const auto& e : m_entities)
            entities.push_back(static_cast<const sketch::Entity&>(e));

        sketch::DXFExportOptions options;
        // DXF OCS: keep the sketch plane through a round-trip, as the menu
        // export paths do.
        options.extrusion = hobbycad::planeBasisFor(sketchPlane()).normal;
        if (!sketch::exportSketchToDXF(entities, filePath.toStdString(), options)) {
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

        std::vector<sketch::Entity> entities;
        entities.reserve(static_cast<size_t>(m_entities.size()));
        for (const auto& e : m_entities)
            entities.push_back(static_cast<const sketch::Entity&>(e));

        std::vector<sketch::Constraint> constraints;
        for (const auto& c : m_constraints) {
            constraints.push_back(toLibraryConstraint(c));
        }

        sketch::SVGExportOptions options;
        if (!sketch::exportSketchToSVG(entities, constraints, filePath.toStdString(), options)) {
            QMessageBox::critical(this, tr("Export Failed"),
                tr("Failed to export SVG file."));
        }
    });

    menu.exec(globalPos);
}

void SketchCanvas::contextMenuEvent(QContextMenuEvent* event)
{
    // While a transform pick or drag owns the canvas the menu stays closed:
    // its own Transform items would act on the same selection mid-step.
    // Escape first, then right-click.
    if (m_suppressNextContextMenu) {
        m_suppressNextContextMenu = false;
        event->accept();
        return;
    }
    if (m_transformPick != TransformPick::None || m_transformPivotDragging || m_freeMoveHandle != FreeMoveHandle::None) {
        event->accept();
        return;
    }
    QPointF worldPos = screenToWorld(event->pos());
    const QPoint globalPos = event->globalPos();

    // Multi-segment slot: when two or more lines/arcs are selected, offer to
    // make a slot that follows them as a path (chain, loop, or branching tree).
    if (showPathSlotContextMenu(globalPos)) return;

    const SketchEntity* sel = selectedEntity();

    // Bezier: right-click offers Insert/Delete Fit Point and Open/Close Spline.
    if (showBezierContextMenu(sel, worldPos, globalPos)) return;

    // Right-click on an endpoint handle of an arc slot
    if (sel && sel->type == SketchEntityType::Slot && sel->points.size() >= 3
        && showSlotHandleContextMenu(sel, worldPos, globalPos)) {
        return;
    }

    // Check if right-clicking on a constraint label
    int constraintId = hitTestConstraintLabel(worldPos);
    if (constraintId >= 0 && showConstraintContextMenu(constraintId, globalPos)) return;

    // Check if right-clicking on an entity: the multi-selection menu when it
    // is one of several selected, otherwise the single-entity menu.
    int entityId = hitTest(worldPos);
    if (entityId >= 0 && m_selectedIds.size() > 1 && m_selectedIds.contains(entityId)) {
        showMultiSelectionContextMenu(globalPos);
        return;
    }
    if (entityId >= 0 && showEntityContextMenu(entityId, worldPos, globalPos)) return;

    // No specific item clicked - show general sketch menu with export options
    if (!m_entities.isEmpty()) {
        showCanvasContextMenu(globalPos);
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
            if (!info.conflictingConstraintIds.empty()) {
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

void SketchCanvas::copySelection()
{
    m_clipEntities.clear();
    m_clipConstraints.clear();
    if (m_selectedIds.isEmpty()) return;

    const QSet<int>& sel = m_selectedIds;
    for (const SketchEntity& e : m_entities)
        if (sel.contains(e.id)) m_clipEntities.append(e);

    // Only constraints wholly inside the selection travel with it; one that
    // reaches outside would dangle on paste.
    for (const SketchConstraint& c : m_constraints) {
        if (c.entityIds.empty()) continue;
        bool allIn = true;
        for (int id : c.entityIds) if (!sel.contains(id)) { allIn = false; break; }
        if (allIn) m_clipConstraints.append(c);
    }
}

void SketchCanvas::cutSelection()
{
    if (m_selectedIds.isEmpty()) return;
    copySelection();
    deleteSelectedEntities();   // its own compound undo
}

bool SketchCanvas::pasteClipboard()
{
    if (m_clipEntities.isEmpty()) return false;

    // A small offset so the pasted copy sits beside the original rather than
    // exactly on top of it.
    const double off = (m_gridSpacing > 0.0 ? m_gridSpacing : 10.0);
    const double dx = off, dy = off;

    // Pass 1: allocate fresh ids for every copied entity.
    QHash<int,int> idMap;
    for (const SketchEntity& src : m_clipEntities)
        idMap.insert(src.id, nextEntityId());

    auto remap = [&idMap](int& ref) {
        if (ref >= 0) ref = idMap.value(ref, -1);  // drop links outside the paste
    };

    std::vector<sketch::UndoCommand> subs;
    QVector<int> pastedIds;

    // Pass 2: build and add each entity with remapped ids/links and offset.
    for (const SketchEntity& src : m_clipEntities) {
        SketchEntity e = src;
        e.id = idMap.value(src.id);
        e.groupId = -1;        // paste ungrouped
        e.selected = false;
        for (auto& pt : e.points) { pt.x += dx; pt.y += dy; }
        remap(e.offsetParentId);
        remap(e.projectionSourceId);
        remap(e.tangentEntityId);
        for (auto& pid : e.pathEntityIds) remap(pid);
        m_entities.append(e);
        subs.push_back(sketch::UndoCommand::addEntity(e, "Paste"));
        pastedIds.append(e.id);
        emit entityCreated(e.id);
    }

    // Constraints: remap their entity ids, offset their labels.
    for (const SketchConstraint& src : m_clipConstraints) {
        SketchConstraint c = src;
        c.id = m_nextConstraintId++;
        c.selected = false;
        bool ok = true;
        for (int& eid : c.entityIds) {
            const int mapped = idMap.value(eid, -1);
            if (mapped < 0) { ok = false; break; }
            eid = mapped;
        }
        if (!ok) continue;
        c.labelPosition = QPointF(c.labelPosition) + QPointF(dx, dy);
        m_constraints.append(c);
        subs.push_back(sketch::UndoCommand::addConstraint(c, "Paste"));
    }

    if (subs.empty()) return false;
    pushUndoCommand(sketch::UndoCommand::compound(subs, "Paste"));

    // Select what was pasted, so it can be dragged into place immediately.
    clearSelection();
    for (int id : pastedIds) selectEntity(id, true);

    m_profilesCacheDirty = true;
    solveConstraints();
    update();
    return true;
}


void SketchCanvas::deleteSelectedEntities()
{
    if (m_selectedIds.isEmpty()) return;

    QSet<int> toDelete = m_selectedIds;

    // A locked group's members cannot be deleted.
    for (int id : m_selectedIds)
        if (isEntityLocked(id)) toDelete.remove(id);
    if (toDelete.isEmpty()) {
        emit toolHintChanged(
            tr("The selection is in a locked group; unlock it to delete."));
        return;
    }

    // Expand selection to include sweep-angle construction line entities
    QSet<int> expanded;
    for (int id : toDelete) {
        expanded.insert(id);
        int gid = findSweepAngleGroupForArc(id);
        if (gid >= 0) {
            for (const auto& g : m_groups) {
                if (g.id == gid) {
                    for (int eid : g.entityIds) expanded.insert(eid);
                    break;
                }
            }
        }
    }
    toDelete = expanded;

    // Record everything the delete removes as ONE compound, so a single
    // Ctrl+Z restores it all (a whole group came back one entity at a time
    // before). Constraints first, then entities, then any emptied group.
    std::vector<sketch::UndoCommand> subs;
    for (const SketchConstraint& c : m_constraints) {
        for (int id : c.entityIds)
            if (toDelete.contains(id)) { subs.push_back(sketch::UndoCommand::deleteConstraint(c, "Delete")); break; }
    }
    for (const SketchEntity& e : m_entities)
        if (toDelete.contains(e.id)) subs.push_back(sketch::UndoCommand::deleteEntity(e, "Delete"));
    for (const SketchGroup& g : m_groups) {
        bool emptied = !g.entityIds.empty();
        for (int id : g.entityIds) if (!toDelete.contains(id)) { emptied = false; break; }
        if (emptied) subs.push_back(sketch::UndoCommand::deleteGroup(g, "Delete"));
    }
    if (subs.size() == 1) pushUndoCommand(subs.front());
    else if (!subs.empty()) pushUndoCommand(sketch::UndoCommand::compound(subs, "Delete"));

    // The cascade (constraints naming them, slot links, group membership,
    // emptied groups) is the library's, shared with the CLI.
    sketch::deleteEntities(m_entities, m_constraints, m_groups,
                           std::vector<int>(toDelete.begin(), toDelete.end()));

    m_selectedId = -1;
    selectClear();
    m_profilesCacheDirty = true;

    // Removing geometry changes the degrees of freedom just as adding it
    // does, and it may drop constraints along with the entities, so the
    // published state must be refreshed here too.
    solveConstraints();

    emit selectionChanged(-1);
    update();
}

std::vector<int> SketchCanvas::selectedMemberIds() const
{
    std::vector<int> members;
    for (int id : m_selectedIds) members.push_back(id);
    return members;
}

void SketchCanvas::selectGroup(int groupId)
{
    if (groupId < 0) return;
    bool first = true;
    for (const auto& e : m_entities) {
        if (e.groupId != groupId) continue;
        selectEntity(e.id, /*addToSelection*/!first, /*individualOnly*/true);
        first = false;
    }
    if (!first) update();
}

int SketchCanvas::hitTestGroupGlyph(const QPoint& screenPos) const
{
    if (m_groupGlyphGroupId >= 0 && m_groupGlyphRect.contains(screenPos))
        return m_groupGlyphGroupId;
    return -1;
}

int SketchCanvas::selectedWholeGroupId() const
{
    if (m_selectedIds.isEmpty()) return -1;
    int gid = -2;
    for (int id : m_selectedIds) {
        const SketchEntity* e = entityById(id);
        if (!e) return -1;
        if (gid == -2) gid = e->groupId;
        else if (e->groupId != gid) return -1;
    }
    if (gid < 0) return -1;
    for (const SketchGroup& g : m_groups) {
        if (g.id != gid) continue;
        for (int eid : g.entityIds)
            if (!m_selectedIds.contains(eid)) return -1;   // a partial group is not the group
        return gid;
    }
    return -1;
}

sketch::GroupTransformResult SketchCanvas::runTransformScratch(const sketch::GroupTransformParams& params, bool createCopy,
                                                               std::vector<sketch::Entity>& ents,
                                                               std::vector<sketch::Constraint>& cons,
                                                               sketch::CloneSetResult& clones,
                                                               std::vector<int>& targetIds) const
{
    // The scratch run is the library's (shared with the CLI's transform);
    // the selection is the canvas's.
    const std::vector<int> members = selectedMemberIds();
    if (members.empty()) {
        sketch::GroupTransformResult res;
        res.refusal = "nothing selected";
        return res;
    }
    sketch::TransformScratch sc = sketch::transformSet(
        std::vector<sketch::Entity>(m_entities.begin(), m_entities.end()),
        std::vector<sketch::Constraint>(m_constraints.begin(), m_constraints.end()),
        members, params, createCopy, m_nextId, m_nextConstraintId);
    ents = std::move(sc.entities);
    cons = std::move(sc.constraints);
    clones = std::move(sc.clones);
    targetIds = std::move(sc.targetIds);
    return sc.result;
}

sketch::GroupTransformResult SketchCanvas::applyTransform(const sketch::GroupTransformParams& params, bool createCopy)
{
    // One backend for every transform, shared with the CLI: transformSet works
    // it out on copies and commitTransform solves, refuses or writes it. The
    // canvas supplies the selection and the pivot gesture, records what the
    // report says as ONE undo command, and repaints.
    const std::vector<int> members = selectedMemberIds();
    if (members.empty()) {
        sketch::GroupTransformResult refused;
        refused.refusal = "nothing selected";
        return refused;
    }
    const sketch::TransformScratch scratch = sketch::transformSet(
        std::vector<sketch::Entity>(m_entities.begin(), m_entities.end()),
        std::vector<sketch::Constraint>(m_constraints.begin(), m_constraints.end()),
        members, params, createCopy, m_nextId, m_nextConstraintId);
    if (!scratch.result.applied) return scratch.result;

    // The group the set belongs to, if it is exactly one whole group: added
    // reference geometry joins it, copies form a sibling group, and its
    // stored pivot moves with it. Not a whole group: reference geometry still
    // joins a common group when every member has the same one.
    sketch::TransformCommitOptions opts;
    const int wholeGroup = selectedWholeGroupId();
    opts.wholeGroup = wholeGroup >= 0;
    opts.homeGroupId = wholeGroup;
    if (opts.homeGroupId < 0) {
        bool oneGroup = true; int gid = -2;
        for (int id : members) {
            const SketchEntity* e = entityById(id);
            if (!e) continue;
            if (gid == -2) gid = e->groupId; else if (e->groupId != gid) oneGroup = false;
        }
        opts.homeGroupId = (oneGroup && gid >= 0) ? gid : -1;
    }
    if (m_transformPivotCleared) {
        opts.pivot = sketch::PivotUpdate::Clear;
    } else if (m_transformPivotUserSet) {
        opts.pivot = sketch::PivotUpdate::Set;
        opts.pivotPoint = Point2D(m_transformPivot.x(), m_transformPivot.y());
    }

    const sketch::TransformCommit commit = sketch::commitTransform(
        m_entities, m_constraints, m_groups, scratch, createCopy, params, opts,
        [this]() { return m_nextGroupId++; });
    if (!commit.applied) {
        sketch::GroupTransformResult refused = scratch.result;
        refused.applied = false;
        refused.refusal = commit.refusal;
        return refused;
    }

    std::vector<sketch::UndoCommand> subs;
    for (const auto& change : commit.modifiedEntities)
        subs.push_back(sketch::UndoCommand::modifyEntity(change.first, change.second, "Transform"));
    for (const auto& change : commit.modifiedConstraints)
        subs.push_back(sketch::UndoCommand::modifyConstraint(change.first, change.second, "Transform"));
    for (const auto& e : commit.addedEntities) {
        if (e.id >= m_nextId) m_nextId = e.id + 1;
        const bool clone = hobbycad::contains(commit.cloneEntityIds, e.id);
        subs.push_back(sketch::UndoCommand::addEntity(e, clone ? "Copy" : "Reference line"));
    }
    for (const auto& c : commit.addedConstraints) {
        if (c.id >= m_nextConstraintId) m_nextConstraintId = c.id + 1;
        bool clone = false;
        for (const auto& k : scratch.clones.constraints) if (k.id == c.id) { clone = true; break; }
        subs.push_back(sketch::UndoCommand::addConstraint(c, clone ? "Copy" : "Reference pin"));
    }
    if (commit.copyGroupMade)
        subs.push_back(sketch::UndoCommand::addGroup(commit.copyGroup, "Copy group"));
    if (commit.pivotChanged)
        subs.push_back(sketch::UndoCommand::modifyGroup(commit.groupBefore, commit.groupAfter, "Group pivot"));
    if (!subs.empty())
        pushUndoCommand(sketch::UndoCommand::compound(subs, createCopy ? "Copy" : "Transform"));

    for (const auto& e : commit.addedEntities)
        if (!hobbycad::contains(commit.cloneEntityIds, e.id)) emit entityCreated(e.id);
    if (createCopy) {
        // Select the copies, as Copy always did.
        clearSelection();
        for (int id : commit.cloneEntityIds) selectEntity(id, true);
        for (int id : commit.cloneEntityIds) emit entityCreated(id);
    } else {
        for (int id : scratch.result.changedEntityIds) emit entityModified(id);
    }
    m_profilesCacheDirty = true;
    clearTransformPreview();
    m_freeMoveDelta = QPointF(); m_freeMoveAngle = 0.0;
    ++m_selectionRevision;            // the pivot is re-derived from the (possibly new) stored value
    ensureTransformStateCurrent();
    solveConstraints();
    update();
    return scratch.result;
}

QRectF SketchCanvas::worldBoundsOf(const QVector<int>& ids) const
{
    hobbycad::geometry::BoundingBox b;
    for (int id : ids) {
        if (const SketchEntity* e = entityById(id)) b.include(e->boundingBox());
    }
    if (!b.valid) return QRectF();
    return QRectF(b.minX, b.minY, b.width(), b.height());
}

QRectF SketchCanvas::selectionWorldRect() const
{
    return worldBoundsOf(QVector<int>(m_selectedIds.begin(), m_selectedIds.end()));
}

void SketchCanvas::resetTransformStateForSelection()
{
    m_transformStateRevision = m_selectionRevision;
    m_transformPick = TransformPick::None;
    m_transformPivotDragging = false;
    m_freeMoveHandle = FreeMoveHandle::None;
    m_freeMoveDelta = QPointF(); m_freeMoveAngle = 0.0;
    m_transformPreview.clear();
    m_transformPivotUserSet = false;
    m_transformPivotCleared = false;
    m_transformPivotGroupId = selectedWholeGroupId();
    m_transformPivotStored = false;
    std::vector<sketch::Entity> ents(m_entities.begin(), m_entities.end());
    if (m_transformPivotGroupId >= 0) {
        for (const SketchGroup& g : m_groups) {
            if (g.id != m_transformPivotGroupId) continue;
            const Point2D pv = sketch::effectivePivot(g, ents);
            m_transformPivot = QPointF(pv.x, pv.y);
            m_transformPivotStored = g.hasPivot;
            break;
        }
    } else {
        const Point2D c = sketch::memberCenter(ents, selectedMemberIds());
        m_transformPivot = QPointF(c.x, c.y);
    }
    emit transformPivotChanged(m_transformPivot, m_transformPivotStored);
}

void SketchCanvas::ensureTransformStateCurrent()
{
    if (m_transformStateRevision != m_selectionRevision) resetTransformStateForSelection();
}

QPointF SketchCanvas::transformPivot() const
{
    const_cast<SketchCanvas*>(this)->ensureTransformStateCurrent();
    return m_transformPivot;
}

bool SketchCanvas::transformPivotStored() const
{
    const_cast<SketchCanvas*>(this)->ensureTransformStateCurrent();
    return m_transformPivotStored;
}

void SketchCanvas::setTransformPivot(const QPointF& world)
{
    ensureTransformStateCurrent();
    m_transformPivot = world;
    m_transformPivotUserSet = true;
    m_transformPivotCleared = false;
    emit transformPivotChanged(m_transformPivot, false);
    update();
}

void SketchCanvas::resetTransformPivotToCenter()
{
    ensureTransformStateCurrent();
    std::vector<sketch::Entity> ents(m_entities.begin(), m_entities.end());
    const Point2D c = sketch::memberCenter(ents, selectedMemberIds());
    m_transformPivot = QPointF(c.x, c.y);
    m_transformPivotUserSet = false;
    m_transformPivotCleared = m_transformPivotStored;   // only meaningful when the group stores one
    m_transformPivotStored = false;
    emit transformPivotChanged(m_transformPivot, false);
    update();
}

void SketchCanvas::setGroupPivot(int groupId, const std::optional<QPointF>& pivot)
{
    for (auto& g : m_groups) {
        if (g.id != groupId) continue;
        const SketchGroup before = g;
        g.hasPivot = pivot.has_value();
        if (pivot) g.pivot = {pivot->x(), pivot->y()};
        if (g.hasPivot != before.hasPivot || g.pivot.x != before.pivot.x || g.pivot.y != before.pivot.y)
            pushUndoCommand(sketch::UndoCommand::modifyGroup(before, g, "Group pivot"));
        break;
    }
    ++m_selectionRevision;
    ensureTransformStateCurrent();
    update();
}

const SketchGroup* SketchCanvas::groupById(int groupId) const
{
    for (const auto& g : m_groups) if (g.id == groupId) return &g;
    return nullptr;
}

bool SketchCanvas::renameGroup(int groupId, const QString& name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) return false;
    for (auto& g : m_groups) {
        if (g.id != groupId) continue;
        if (QString::fromStdString(g.name) == trimmed) return false;
        const SketchGroup before = g;
        g.name = trimmed.toStdString();
        pushUndoCommand(sketch::UndoCommand::modifyGroup(before, g, "Rename group"));
        update();
        return true;
    }
    return false;
}

void SketchCanvas::setGroupLocked(int groupId, bool locked)
{
    for (auto& g : m_groups) {
        if (g.id != groupId || g.locked == locked) continue;
        const SketchGroup before = g;
        g.locked = locked;
        pushUndoCommand(sketch::UndoCommand::modifyGroup(before, g, locked ? "Lock group" : "Unlock group"));
        update();
        return;
    }
}

void SketchCanvas::setTransformGlyph(bool visible, bool withArc)
{
    m_transformGlyphVisible = visible;
    m_transformGlyphArc = withArc;
    if (!visible) { cancelTransformPick(); clearTransformPreview(); }
    update();
}

void SketchCanvas::beginTransformPick(TransformPick pick)
{
    ensureTransformStateCurrent();
    if (m_isDrawing) cancelEntity();
    if (m_backgroundCalibrationMode) setBackgroundCalibrationMode(false);
    if (m_calibrationEntitySelectionMode) setCalibrationEntitySelectionMode(false);
    m_transformPick = pick;
    m_transformPivotDragging = false;
    m_freeMoveHandle = FreeMoveHandle::None;
    setCursor(pick == TransformPick::FreeMove ? Qt::OpenHandCursor : Qt::CrossCursor);
    switch (pick) {
    case TransformPick::Pivot:            emit toolHintChanged(tr("Pivot: click to place the star or drag it (snaps apply, Alt = free); Home = center; Escape cancels")); break;
    case TransformPick::FromPoint:        emit toolHintChanged(tr("Point to point: click the point to move FROM (snaps apply); Escape cancels")); break;
    case TransformPick::ToPoint:          emit toolHintChanged(tr("Point to point: click the point to move TO (snaps apply); Escape cancels")); break;
    case TransformPick::PointOnSelection: emit toolHintChanged(tr("Point to position: click the point on the selection that should land on the target; Escape cancels")); break;
    case TransformPick::MirrorA:          emit toolHintChanged(tr("Mirror line: click its first point (snaps apply); Escape cancels")); break;
    case TransformPick::MirrorB:          emit toolHintChanged(tr("Mirror line: click its second point (snaps apply); Escape cancels")); break;
    case TransformPick::ReferencePoint:   emit toolHintChanged(tr("Relative to: click the reference point the target is measured from (snaps apply); Escape cancels")); break;
    case TransformPick::FreeMove:         emit toolHintChanged(tr("Free move: drag the selection to move it, drag the ring to turn it about the star (Ctrl = axis lock / 15-degree steps); Enter applies, Escape cancels")); break;
    case TransformPick::None: break;
    }
    update();
}

void SketchCanvas::cancelTransformPick()
{
    if (m_transformPick == TransformPick::None && !m_transformPivotDragging && m_freeMoveHandle == FreeMoveHandle::None) return;
    m_transformPick = TransformPick::None;
    m_transformPivotDragging = false;
    m_freeMoveHandle = FreeMoveHandle::None;
    m_snapEngine.clearActiveSnap();
    setCursor(Qt::ArrowCursor);
    update();
}

void SketchCanvas::setFreeMove(const QPointF& delta, double angleDeg)
{
    m_freeMoveDelta = delta;
    m_freeMoveAngle = angleDeg;
    update();
}

sketch::GroupTransformResult SketchCanvas::previewTransform(const sketch::GroupTransformParams& params, bool createCopy)
{
    ensureTransformStateCurrent();
    std::vector<sketch::Entity> ents; std::vector<sketch::Constraint> cons;
    sketch::CloneSetResult clones; std::vector<int> targetIds;
    const sketch::GroupTransformResult res = runTransformScratch(params, createCopy, ents, cons, clones, targetIds);
    m_transformPreview.clear();
    if (res.applied) {
        for (int id : targetIds) {
            for (const auto& e : ents) if (e.id == id) { SketchEntity g(e); g.selected = false; m_transformPreview.append(g); }
        }
        for (const auto& a : res.addedEntities) { SketchEntity g(a); g.selected = false; m_transformPreview.append(g); }
    }
    update();
    return res;
}

void SketchCanvas::clearTransformPreview()
{
    if (m_transformPreview.isEmpty()) return;
    m_transformPreview.clear();
    update();
}

bool SketchCanvas::transformStarHit(const QPoint& screen) const
{
    if (!m_transformGlyphVisible || m_selectedIds.isEmpty()) return false;
    return QLineF(QPointF(worldToScreen(m_transformPivot)), QPointF(screen)).length() <= kTransformPivotHitPx;
}

bool SketchCanvas::freeMoveRingHit(const QPoint& screen) const
{
    const double d = QLineF(QPointF(worldToScreen(m_transformPivot)), QPointF(screen)).length();
    return std::fabs(d - kFreeMoveRingPx) <= kFreeMoveRingHitPx;
}

bool SketchCanvas::freeMoveBodyHit(const QPointF& world) const
{
    const int id = hitTest(world);
    if (id >= 0 && m_selectedIds.contains(id)) return true;
    QRectF r = selectionWorldRect().translated(m_freeMoveDelta);
    const double pad = kHitPadPx / m_zoom;
    r.adjust(-pad, -pad, pad, pad);
    return r.contains(world);
}

void SketchCanvas::drawTransformPivot(QPainter& painter)
{
    const int k = m_transformGlyphArc ? kTransformPivotRotatePx : kTransformPivotGlyphPx;
    const QPoint c = worldToScreen(m_transformPivot);
    const QRectF r(c.x() - k / 2.0, c.y() - k / 2.0, k, k);
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen halo(m_theme.labelHalo, 3.5);
    halo.setJoinStyle(Qt::RoundJoin); halo.setCapStyle(Qt::RoundCap);
    painter.setPen(halo); painter.setBrush(Qt::NoBrush);
    hobbycad::drawPivotGlyph(painter, r, m_transformGlyphArc);
    QPen ink(m_theme.pivotInk, 1.2);
    ink.setJoinStyle(Qt::MiterJoin);
    painter.setPen(ink); painter.setBrush(m_theme.pivotFill);   // like the sun
    hobbycad::drawPivotGlyph(painter, r, m_transformGlyphArc);
    painter.restore();
}

void SketchCanvas::drawFreeMoveHandles(QPainter& painter)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QPoint c = worldToScreen(m_transformPivot);
    QPen ring(QColor(0, 120, 215), 1.2, Qt::DashLine);
    painter.setPen(ring); painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(QPointF(c), double(kFreeMoveRingPx), double(kFreeMoveRingPx));
    // the knob sits on the ring at the current angle (screen y is down)
    const double a = qDegreesToRadians(m_freeMoveAngle);
    const QPointF knob(c.x() + kFreeMoveRingPx * qCos(a), c.y() - kFreeMoveRingPx * qSin(a));
    painter.setPen(QPen(QColor(0, 120, 215), 1)); painter.setBrush(Qt::white);
    painter.drawEllipse(knob, 5.0, 5.0);
    // the body affordance: the selection's box, where it currently previews
    QRectF w = selectionWorldRect().translated(m_freeMoveDelta);
    if (!w.isNull()) {
        const QPoint a1 = worldToScreen(w.topLeft()), b1 = worldToScreen(w.bottomRight());
        painter.setPen(QPen(QColor(0, 120, 215), 1, Qt::DashLine)); painter.setBrush(Qt::NoBrush);
        painter.drawRect(QRect(a1, b1).normalized());
    }
    painter.restore();
}

void SketchCanvas::transformSelectedEntities(TransformType type)
{
    // The context menu no longer transforms anything itself. It opens the
    // Transform section of the Sketch properties panel with the matching move
    // type; nothing changes until the user presses Apply there. The former
    // QInputDialog path and its unused bounding-box sweep are gone.
    if (m_selectedIds.isEmpty()) return;
    emit transformSectionRequested(int(type));
}

void SketchCanvas::alignSelectedEntities(AlignmentType type)
{
    if (m_selectedIds.size() < 2) return;

    // Group the selection into ITEMS: a whole group is ONE item (all its
    // members move together), an ungrouped entity is its own item, so a group
    // stays rigid and alignment never tears it apart. [Aaron 2026-09-09]
    using sketch::AlignItem;
    std::vector<AlignItem> items;
    QSet<int> usedGroups, usedIds;

    for (int id : m_selectedIds) {
        const SketchEntity* e = entityById(id);
        if (!e) continue;
        AlignItem item;
        if (e->groupId >= 0) {
            if (usedGroups.contains(e->groupId)) continue;   // group already an item
            usedGroups.insert(e->groupId);
            for (const auto& m : m_entities)
                if (m.groupId == e->groupId) item.ids.push_back(m.id);
        } else {
            if (usedIds.contains(id)) continue;
            item.ids.push_back(id);
        }
        for (int mid : item.ids)
            if (const SketchEntity* m = entityById(mid)) item.bounds.include(m->boundingBox());
        if (!item.bounds.valid) continue;
        for (int mid : item.ids) usedIds.insert(mid);
        items.push_back(item);
    }
    if (items.size() < 2) return;

    // Snapshot member entities for one compound undo.
    QHash<int, SketchEntity> before;
    for (const AlignItem& it : items)
        for (int id : it.ids)
            if (const SketchEntity* e = entityById(id)) before.insert(id, *e);

    // The offsets are the library's; applying them and recording undo is ours.
    const std::vector<Point2D> offsets = sketch::alignOffsets(items, type);
    QSet<int> movedIds;
    for (size_t i = 0; i < items.size(); ++i) {
        const Point2D off = offsets[i];
        if (off.x == 0.0 && off.y == 0.0) continue;
        for (int id : items[i].ids) {
            SketchEntity* e = entityById(id);
            if (!e) continue;
            for (auto& pt : e->points) pt += off;
            movedIds.insert(id);
        }
    }

    // One compound undo (Align was previously not undoable).
    std::vector<sketch::UndoCommand> subs;
    for (int id : movedIds) {
        if (!before.contains(id)) continue;
        SketchEntity* now = entityById(id);
        if (now) subs.push_back(sketch::UndoCommand::modifyEntity(before.value(id), *now, "Align"));
    }
    if (subs.size() == 1) pushUndoCommand(subs.front());
    else if (!subs.empty()) pushUndoCommand(sketch::UndoCommand::compound(subs, "Align"));

    m_profilesCacheDirty = true;
    solveConstraints();
    update();
}

void SketchCanvas::sweepSelectedPath()
{
    if (m_selectedIds.size() != 1) return;
    SketchEntity* path = entityById(*m_selectedIds.begin());
    if (!path) return;
    if (path->type != SketchEntityType::Line
        && path->type != SketchEntityType::Arc) {
        return;
    }

    bool ok = false;
    const double width = QInputDialog::getDouble(
        this, tr("Sweep Along Path"), tr("Width:"),
        10.0, 0.0001, 100000.0, 3, &ok);
    if (!ok) return;
    const double halfWidth = width / 2.0;

    // Same refusal the CLI makes, and the same limit: a half-width EQUAL to
    // the radius is the 180-degree case and builds (the inner side is the
    // center); only past it would the inner edge pass through the center.
    if (path->type == SketchEntityType::Arc && halfWidth > path->radius) {
        QMessageBox::warning(
            this, tr("Sweep Along Path"),
            tr("A width of %1 does not fit on an arc of radius %2: the inner "
               "edge would pass through the center. Width can be at most %3.")
                .arg(width).arg(path->radius).arg(path->radius * 2.0));
        return;
    }

    const QStringList styles{tr("Round"), tr("Flat")};
    const QString style = QInputDialog::getItem(
        this, tr("Sweep Along Path"), tr("Ends:"), styles, 0, false, &ok);
    if (!ok) return;
    const sketch::SweepEndStyle ends = (style == styles.at(1))
        ? sketch::SweepEndStyle::Flat
        : sketch::SweepEndStyle::Round;

    const int pathId = path->id;
    // The commit is the library's (applySweep), shared with the CLI's
    // "sweep"; this records it for undo.
    const sketch::SweepApplied result = sketch::applySweep(
        m_entities, m_constraints, m_groups, pathId, halfWidth, ends,
        [this]() { return nextEntityId(); },
        [this]() { return m_nextConstraintId++; },
        m_nextGroupId++);
    if (!result.success) {
        QMessageBox::warning(this, tr("Sweep Along Path"),
                             tr("That sweep could not be built."));
        return;
    }

    std::vector<sketch::UndoCommand> subs;
    for (const auto& e : result.entities) subs.push_back(sketch::UndoCommand::addEntity(e));
    for (const auto& c : result.constraints) subs.push_back(sketch::UndoCommand::addConstraint(c));
    if (result.pathConverted) {
        if (const SketchEntity* after = entityById(pathId))
            subs.push_back(sketch::UndoCommand::modifyEntity(result.pathBefore, *after));
    }
    subs.push_back(sketch::UndoCommand::addGroup(result.group));
    pushUndoCommand(sketch::UndoCommand::compound(subs, "Sweep"));

    m_selectedIds.clear();
    m_selectedId = -1;
    m_profilesCacheDirty = true;
    emit selectionChanged(-1);
    update();
}

int SketchCanvas::groupSelectedEntities()
{
    if (m_selectedIds.size() < 2) return -1;

    SketchGroup group;
    group.id = m_nextGroupId++;
    group.name = tr("Group %1").arg(group.id).toStdString();
    {
        auto vals = m_selectedIds.values();
        group.entityIds = std::vector<int>(vals.begin(), vals.end());
    }

    // Include constraints whose referenced entities are all within the
    // selection; they logically belong to this group.
    for (const auto& c : m_constraints) {
        bool allInside = !c.entityIds.empty();
        for (int eid : c.entityIds) {
            if (!m_selectedIds.contains(eid)) {
                allInside = false;
                break;
            }
        }
        if (allInside)
            group.constraintIds.push_back(c.id);
    }

    // Set groupId on each member entity
    for (int eid : group.entityIds) {
        SketchEntity* ent = entityById(eid);
        if (ent) ent->groupId = group.id;
    }

    m_groups.append(group);
    pushUndoCommand(sketch::UndoCommand::addGroup(group, "Group"));
    update();
    return group.id;
}

void SketchCanvas::ungroupEntities(int groupId)
{
    // Clear groupId on member entities before removing the group
    for (const SketchGroup& g : m_groups) {
        if (g.id == groupId) {
            pushUndoCommand(sketch::UndoCommand::deleteGroup(g, "Ungroup"));
            for (int eid : g.entityIds) {
                SketchEntity* ent = entityById(eid);
                if (ent && ent->groupId == groupId)
                    ent->groupId = -1;
            }
            break;
        }
    }
    if (m_enteredGroupId == groupId) m_enteredGroupId = -1;

    m_groups.erase(
        std::remove_if(m_groups.begin(), m_groups.end(),
                       [groupId](const SketchGroup& g) { return g.id == groupId; }),
        m_groups.end());
    update();
}

void SketchCanvas::syncGroupMembership(const SketchGroup& group, int groupIdOrMinusOne)
{
    // Entity::groupId is the back-pointer that selection expansion and group
    // drags key off; Group::entityIds is the list. Keep them in step whenever
    // a group appears or disappears through undo/redo.
    for (int eid : group.entityIds) {
        SketchEntity* ent = entityById(eid);
        if (!ent) continue;
        if (groupIdOrMinusOne < 0) {
            if (ent->groupId == group.id) ent->groupId = -1;
        } else {
            ent->groupId = groupIdOrMinusOne;
        }
    }
}

void SketchCanvas::dropStaleEnteredGroup()
{
    if (m_enteredGroupId < 0) return;
    for (const SketchGroup& g : m_groups)
        if (g.id == m_enteredGroupId) return;
    m_enteredGroupId = -1;
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
    // Build a set of entity IDs that belong to sweep-angle groups.
    // These construction lines are implementation details and should
    // not be directly selectable; clicks on them are handled by the
    // constraint hit-test path instead.  Only skip Line entities (the
    // construction lines), NOT the arc entity that is also in the group.
    std::unordered_set<int> sweepAngleEntityIds;
    for (const auto& g : m_groups) {
        if (isSweepAngleGroup(g.id)) {
            for (int eid : g.entityIds) {
                const SketchEntity* e = entityById(eid);
                if (e && e->type == SketchEntityType::Line)
                    sweepAngleEntityIds.insert(eid);
            }
        }
    }

    // Test in reverse order (top-most first)
    for (int i = m_entities.size() - 1; i >= 0; --i) {
        if (sweepAngleEntityIds.count(m_entities[i].id))
            continue;  // skip sweep-angle construction lines
        if (hitTestEntity(m_entities[i], worldPos)) {
            return m_entities[i].id;
        }
    }
    return -1;
}

bool SketchCanvas::hitTestEntity(const SketchEntity& entity, const QPointF& worldPos) const
{
    const double tolerance = kEntityPickTolPx / m_zoom;  // 5 pixels in world units

    // Text needs QFont/QFontMetrics and zoom (genuinely GUI-specific)
    if (entity.type == SketchEntityType::Text)
        return hitTestTextEntity(entity, worldPos, tolerance);

    // All other entity types delegate to the library's containsPoint()
    return entity.containsPoint(worldPos, tolerance);
}

bool SketchCanvas::hitTestTextEntity(const SketchEntity& entity, const QPointF& worldPos, double /*tolerance*/) const
{
    if (entity.points.empty()) return false;

    // Construct font matching drawEntity to get accurate metrics
    QFont font;
    if (!entity.fontFamily.empty()) {
        font.setFamily(QString::fromStdString(entity.fontFamily));
    }
    // fontSize is in mm; scale by zoom for screen metrics, then convert back to world
    double scaledSize = entity.fontSize * m_zoom;
    font.setPointSizeF(qMax(6.0, scaledSize));
    font.setBold(entity.fontBold);
    font.setItalic(entity.fontItalic);

    QFontMetricsF fm(font);
    QRectF textBounds = fm.boundingRect(QString::fromStdString(entity.text));
    // Convert screen-space bounds back to world-space dimensions
    double worldWidth = textBounds.width() / m_zoom;
    double worldHeight = textBounds.height() / m_zoom;

    QPointF pos = entity.points[0];
    // Text baseline is at pos; in world coords (Y-up) the text extends
    // upward (positive Y) from the baseline.
    QRectF worldRect(pos.x(), pos.y(), worldWidth, worldHeight);

    if (qAbs(entity.textRotation) > 0.01) {
        // For rotated text, transform the test point into the text's local frame
        double rad = qDegreesToRadians(entity.textRotation);
        double cosR = std::cos(rad);
        double sinR = std::sin(rad);
        double dx = worldPos.x() - pos.x();
        double dy = worldPos.y() - pos.y();
        // Rotate worldPos into text-local coordinates (inverse rotation)
        QPointF localPos(dx * cosR + dy * sinR, -dx * sinR + dy * cosR);
        QRectF localRect(0, 0, worldWidth, worldHeight);
        return localRect.contains(localPos);
    } else {
        return worldRect.contains(worldPos);
    }
}

bool SketchCanvas::entityIntersectsRect(const SketchEntity& entity, const QRectF& rect) const
{
    return sketch::entityIntersectsRect(entity, rect);
}

bool SketchCanvas::entityEnclosedByRect(const SketchEntity& entity, const QRectF& rect) const
{
    return sketch::entityEnclosedByRect(entity, rect);
}

int SketchCanvas::hitTestHandle(const QPointF& worldPos) const
{
    // Only test handles on selected entity
    const SketchEntity* sel = selectedEntity();
    if (!sel) return -1;

    const double tolerance = kHandlePickTolPx / m_zoom;  // 6 pixels in world units

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

    const double tolerance = kGroupHandlePickTolPx / m_zoom;

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
    m_placedSnaps.clear();
    m_previewPoints.append(pos);
    if (m_snapEngine.hasActiveSnap()) {
        m_placedSnaps.append({0, *m_snapEngine.activeSnap()});
    }
    m_arcSlotFlipped = false;  // Reset flip state for new arc slot
    clearDimFields();  // Reset dim input for new entity

    m_pendingEntity = SketchEntity();
    m_pendingEntity.id = nextEntityId();
    m_pendingEntity.points.push_back(pos);

    // Every tool types its own pending entity.
    if (SketchToolHandler* h = activeHandler()) {
        h->beginEntity(*this, m_pendingEntity);
    }

    // Initialize inline dimension fields for the new entity
    if (m_isDrawing) {
        initDimFields();
    }
}

void SketchCanvas::updateEntity(const QPointF& pos)
{
    if (!m_isDrawing) return;

    // The active tool updates the pending entity in its handler.
    if (SketchToolHandler* h = activeHandler()) {
        h->updateEntity(*this, pos);
    }
}

void SketchCanvas::showDimensionOptionsMenu(const QPoint& screenPos)
{
    QMenu menu(this);

    // Radius vs diameter only makes sense for a circle or an arc.
    const SketchEntity* target = entityById(firstConstraintTargetId());
    const bool radial = target && (target->type == SketchEntityType::Circle
                                    || target->type == SketchEntityType::Arc);
    if (radial) {
        QAction* radius = menu.addAction(tr("Radius"));
        radius->setCheckable(true);
        radius->setChecked(m_pendingConstraintType == ConstraintType::Radius);
        connect(radius, &QAction::triggered, this, [this]() {
            m_pendingConstraintType = ConstraintType::Radius;
            update();
        });
        QAction* diameter = menu.addAction(tr("Diameter"));
        diameter->setCheckable(true);
        diameter->setChecked(m_pendingConstraintType == ConstraintType::Diameter);
        connect(diameter, &QAction::triggered, this, [this]() {
            m_pendingConstraintType = ConstraintType::Diameter;
            update();
        });
        menu.addSeparator();
    }

    QAction* driven = menu.addAction(tr("Driven (Reference)"));
    driven->setCheckable(true);
    driven->setChecked(!m_pendingDimensionDriven);
    connect(driven, &QAction::triggered, this, [this](bool on) {
        m_pendingDimensionDriven = !on;   // checked = driven = not driving
    });

    menu.exec(mapToGlobal(screenPos));
}

void SketchCanvas::recordEndpointSnap()
{
    // createSnapConstraints() matches by position, so the index is nominal;
    // what matters is that the endpoint's active snap (e.g. onto the first
    // point when closing a triangle) is in m_placedSnaps for finishEntity().
    if (m_snapEngine.hasActiveSnap())
        m_placedSnaps.append({ 0, *m_snapEngine.activeSnap() });
}

bool SketchCanvas::createCoincidenceOnDrag(int entityId, int handleIndex)
{
    const SketchEntity* dragged = entityById(entityId);
    if (!dragged || handleIndex < 0 || handleIndex >= static_cast<int>(dragged->points.size()))
        return false;
    const QPointF pos(dragged->points[handleIndex]);
    const double eps = kSnapWeldEps;   // only when the point landed EXACTLY on another (i.e. it snapped)

    // Dragged onto the sketch origin -> ground it there.
    if (QLineF(pos, QPointF(0, 0)).length() <= eps) {
        for (const auto& ex : m_constraints) {
            if (ex.type == ConstraintType::Coincident && ex.entityIds.size() == 2
                && ex.entityIds[0] == entityId && ex.pointIndices.size() == 2
                && ex.pointIndices[0] == handleIndex
                && ex.entityIds[1] == sketch::kSketchOriginEntity)
                return false;   // already grounded
        }
        pushUndoCommand(makeConstraint(ConstraintType::Coincident,
                                       { entityId, sketch::kSketchOriginEntity },
                                       { handleIndex, 0 }));
        return true;
    }

    for (const SketchEntity& other : m_entities) {
        if (other.id == entityId) continue;   // same-entity self-join is not a coincidence
        for (int i = 0; i < static_cast<int>(other.points.size()); ++i) {
            if (QLineF(QPointF(other.points[i]), pos).length() > eps) continue;

            // Skip if these two points are already tied by a Coincident.
            for (const auto& ex : m_constraints) {
                if (ex.type != ConstraintType::Coincident) continue;
                if (ex.entityIds.size() != 2 || ex.pointIndices.size() != 2) continue;
                const bool a = ex.entityIds[0] == entityId && ex.pointIndices[0] == handleIndex
                            && ex.entityIds[1] == other.id  && ex.pointIndices[1] == i;
                const bool b = ex.entityIds[1] == entityId && ex.pointIndices[1] == handleIndex
                            && ex.entityIds[0] == other.id  && ex.pointIndices[0] == i;
                if (a || b) return false;
            }

            pushUndoCommand(makeConstraint(ConstraintType::Coincident,
                                           { entityId, other.id }, { handleIndex, i }));
            return true;   // one join per drag
        }
    }
    return false;
}

// Opening a circle produces one 360-degree arc whose two ends start on top of
// each other at the cut (both unwelded). When the user drags one end away, the
// end LEFT IN PLACE is the one that should catch any entity sitting at the cut.
// That choice depends on the drag direction, so it is resolved here at release,
// not at split time where the two ends overlap and neither has moved. Reuses the
// shared library rule (point-on-object, or Coincident to a vertex / point
// entity) so the tie matches trim/split elsewhere. (Needs GUI runtime check.)
bool SketchCanvas::tieOpenedArcEndOnDrag(int entityId, int draggedHandle)
{
    SketchEntity* arc = entityById(entityId);
    if (!arc || arc->type != SketchEntityType::Arc || arc->points.size() < 3)
        return false;
    if (draggedHandle != 1 && draggedHandle != 2) return false;   // endpoints only
    const int twin = (draggedHandle == 1) ? 2 : 1;
    if (static_cast<int>(m_dragOriginalEntity.points.size()) < 3) return false;

    // Fire only when this drag OPENED a coincident pair: the two ends started
    // together and the grabbed one has now been pulled off the twin.
    const QPointF beforeDragged(m_dragOriginalEntity.points[draggedHandle]);
    const QPointF beforeTwin(m_dragOriginalEntity.points[twin]);
    if (QLineF(beforeDragged, beforeTwin).length() > kSnapWeldEps) return false;
    const QPointF twinPos(arc->points[twin]);
    if (QLineF(QPointF(arc->points[draggedHandle]), twinPos).length() <= kSnapWeldEps)
        return false;   // nothing separated: not an opening drag

    // Pin the twin (still at the cut) to whatever entity sits there.
    std::vector<sketch::Entity> pieceLib{ hobbycad::toLibraryEntity(*arc) }, others;
    for (const SketchEntity& e : m_entities)
        if (e.id != entityId) others.push_back(hobbycad::toLibraryEntity(e));
    const std::vector<sketch::Constraint> ties = sketch::computeCutConstraints(
        pieceLib, others, {{ twinPos.x(), twinPos.y() }},
        [this]() { return m_nextConstraintId++; });
    if (ties.empty()) return false;

    std::vector<sketch::UndoCommand> subs;
    recordAddedConstraints(ties, subs);
    pushCompoundOrSingle(subs, "Constrain opened arc");
    m_profilesCacheDirty = true;
    return true;
}

// Build a constraint, append it, mark its (real) entities constrained, and
// return the undo command so the caller pushes it directly or into a compound
// batch. The single place the SketchConstraint field defaults live, replacing a
// dozen verbatim copies of the id/flags/append/mark trailer that had begun to
// drift. Sentinel ids (e.g. the origin) that entityById cannot resolve are
// simply skipped when marking. (audit)
sketch::UndoCommand SketchCanvas::makeConstraint(ConstraintType type,
                                                 std::vector<int> entityIds,
                                                 std::vector<int> pointIndices,
                                                 double value, bool driving,
                                                 bool labelVisible)
{
    SketchConstraint c;
    c.id = m_nextConstraintId++;
    c.type = type;
    c.entityIds = entityIds;
    c.pointIndices = std::move(pointIndices);
    c.value = value;
    c.isDriving = driving;
    c.enabled = true;
    c.satisfied = true;
    c.labelVisible = labelVisible;
    m_constraints.append(c);
    for (int id : entityIds)
        if (SketchEntity* e = entityById(id)) e->constrained = true;
    return sketch::UndoCommand::addConstraint(c);
}

// Find which of the given entities carries a point at pos (within eps), matching
// by POSITION, the identity that survives entity decomposition. skipId is not
// considered (a point never joins to itself). (audit)
bool SketchCanvas::findOwnerPointAt(const QVector<int>& entityIds,
                                    const QPointF& pos, double eps, int skipId,
                                    int& outEntityId, int& outPointIndex) const
{
    for (int id : entityIds) {
        if (id == skipId) continue;
        const SketchEntity* e = entityById(id);
        if (!e) continue;
        if (e->type == SketchEntityType::Slot) continue;  // slots are derived: not a weld target
        for (int i = 0; i < static_cast<int>(e->points.size()); ++i) {
            if (QLineF(QPointF(e->points[i]), pos).length() <= eps) {
                outEntityId = id;
                outPointIndex = i;
                return true;
            }
        }
    }
    outEntityId = -1;
    outPointIndex = -1;
    return false;
}

void SketchCanvas::createSnapConstraints(const QVector<int>& newEntityIds,
                                         int excludeTarget)
{
    if (m_placedSnaps.isEmpty() || newEntityIds.isEmpty()) return;

    // Match by POSITION rather than by point index. A rectangle or polygon
    // is decomposed on finish, so the point index the snap was recorded
    // against belongs to an entity that no longer exists, but the corner
    // it placed is still there, on whichever line now owns it. Position is
    // the one thing that survives decomposition, so it is what we look up.
    const double eps = kSnapWeldEps;

    std::vector<sketch::UndoCommand> subs;

    for (const auto& [placedIndex, snap] : m_placedSnaps) {
        Q_UNUSED(placedIndex);

        // Snapped to the sketch origin: ground that point to (0,0). The origin
        // is not a real entity, so it uses the origin sentinel rather than a
        // target entity lookup.
        if (snap.type == sketch::SnapType::Origin) {
            const QPointF snapPos(snap.position.x, snap.position.y);
            int ownerId = -1, ownerIndex = -1;
            if (!findOwnerPointAt(newEntityIds, snapPos, eps, -1,
                                  ownerId, ownerIndex)) continue;
            subs.push_back(makeConstraint(ConstraintType::Coincident,
                                          { ownerId, sketch::kSketchOriginEntity },
                                          { ownerIndex, 0 }));
            continue;
        }

        if (snap.entityId < 0) continue;

        // The tangent tool excludes its target curve: tangency governs that
        // contact, so a snap onto it must not also become a Coincident.
        if (snap.entityId == excludeTarget) continue;

        const SketchEntity* target = entityById(snap.entityId);
        if (!target) continue;

        const auto implied = sketch::constraintForSnap(snap.type, target->type);
        if (!implied) continue;

        const QPointF snapPos(snap.position.x, snap.position.y);

        // Which produced entity carries the point that was placed here (never
        // the snap target itself).
        int ownerId = -1, ownerIndex = -1;
        if (!findOwnerPointAt(newEntityIds, snapPos, eps, snap.entityId,
                              ownerId, ownerIndex)) continue;

        const int targetIndex = sketch::nearestPointIndex(*target, snap.position);
        if (targetIndex < 0) continue;

        subs.push_back(makeConstraint(*implied, {ownerId, snap.entityId},
                                      {ownerIndex, targetIndex}));
    }

    m_placedSnaps.clear();
    if (subs.empty()) return;

    pushUndoCommand(sketch::UndoCommand::compound(subs, "Snap constraints"));
    solveConstraints();
}

void SketchCanvas::createInferredConstraints(const QVector<int>& newEntityIds)
{
    if (m_snapEngine.activeInferences().empty() || newEntityIds.isEmpty()) {
        m_snapEngine.clearInferences();
        return;
    }

    // The inference is about the line just drawn: the first new entity that
    // is a Line (the line tool produces exactly one).
    int lineId = -1;
    for (int id : newEntityIds) {
        const SketchEntity* e = entityById(id);
        if (e && e->type == SketchEntityType::Line) { lineId = id; break; }
    }
    if (lineId < 0) { m_snapEngine.clearInferences(); return; }

    std::vector<sketch::UndoCommand> subs;
    for (const auto& inf : m_snapEngine.activeInferences()) {
        SketchConstraint c;
        c.type = inf.constraint;
        c.isDriving = true;
        c.enabled = true;
        c.satisfied = true;
        c.labelVisible = false;
        c.value = 0.0;
        if (inf.constraint == ConstraintType::Parallel
            || inf.constraint == ConstraintType::Perpendicular) {
            if (inf.refEntityId == lineId || !entityById(inf.refEntityId)) continue;
            c.entityIds = {lineId, inf.refEntityId};
        } else {
            c.entityIds = {lineId};   // Horizontal / Vertical
        }

        // Never add an alignment the line already carries.
        bool duplicate = false;
        for (const auto& ex : m_constraints) {
            if (ex.type == c.type && ex.entityIds == c.entityIds) { duplicate = true; break; }
        }
        if (duplicate) continue;

        // Honor the drawing rather than fight it: if this alignment would
        // over-constrain the sketch, leave it off; the geometry is already
        // aligned, and a conflicting constraint helps no one.
        if (SketchSolver::isAvailable()) {
            SketchSolver solver;
            if (solver.checkOverConstrain(m_entities, m_constraints, c).wouldOverConstrain)
                continue;
        }

        c.id = m_nextConstraintId++;
        m_constraints.append(c);
        subs.push_back(sketch::UndoCommand::addConstraint(c));
        if (SketchEntity* e = entityById(lineId)) e->constrained = true;
        if (inf.refEntityId >= 0)
            if (SketchEntity* r = entityById(inf.refEntityId)) r->constrained = true;
    }

    m_snapEngine.clearInferences();
    if (subs.empty()) return;

    pushUndoCommand(sketch::UndoCommand::compound(subs, "Inferred constraints"));
    solveConstraints();
}


void SketchCanvas::finishEntity()
{
    // Set when the committed entity can be chained from; see the end.
    int chainFromId = -1;
    QPointF chainFrom;
    // A fresh finish clears the tangent-arc chain link; the chaining block
    // below re-arms it when this segment is one that can be continued.
    m_chainFromEntityId = -1;

    if (!m_isDrawing) return;

    // Validate entity
    bool valid = false;

    // A tangent-mode line's target must be captured BEFORE normalize(), which
    // clears the tangent targets; used below to build the Tangent constraint.
    int pendingTangentTarget = -1;
    if (m_activeTool == SketchTool::Line
        && m_lineMode == LineMode::Tangent
        && !m_tangentTargets.isEmpty())
        pendingTangentTarget = m_tangentTargets.first();

    // Every tool normalizes its own point layout and reports whether the
    // result is worth committing.
    if (SketchToolHandler* h = activeHandler()) {
        h->normalize(*this, m_pendingEntity, valid);
    }

    if (valid) {
        // Carry any remaining locked fields into the constraint list.
        m_dimInput.flushLocked();

        // --- Decomposition path for compound entities (Rectangle, Parallelogram) ---
        sketch::UndoCommand compoundCmd;
        if (decomposeCompoundEntity(m_pendingEntity, m_dimInput.lockedForConstraints(), compoundCmd)
            || createCenterlineSlot(m_pendingEntity, compoundCmd)) {
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
            m_dimInput.clearLocked();

            // Snaps recorded against the compound entity still apply: the
            // corner they placed now belongs to one of the lines below it.
            {
                QVector<int> produced;
                for (int eid : group.entityIds) produced.append(eid);
                createSnapConstraints(produced);
            }
        } else {
            // --- Normal (non-decomposable) entity path ---
            m_entities.append(m_pendingEntity);
            m_profilesCacheDirty = true;

            // Push undo command for entity creation
            pushUndoCommand(sketch::UndoCommand::addEntity(m_pendingEntity));

            emit entityCreated(m_pendingEntity.id);

            // Auto-create constraints from locked dimension values
            if (!m_dimInput.lockedForConstraints().isEmpty()) {
                createLockedConstraints(m_pendingEntity.id);
            }

            // A point placed on a snap becomes a CONSTRAINT, not merely
            // matching coordinates, in BOTH modes. Fusion: "If you snap
            // to a specific point, the logical constraints are
            // automatically added to the sketch." It is not an interaction
            // style, it is what snapping means; without it a sketch looks
            // joined and is not.
            // A tangent-mode line always gets its Tangent constraint (the
            // solver's touch-point construction IS the tangency, and it now
            // sticks; the endpoints stay free to slide). Its point placements
            // are then evaluated the SAME way every time (Aaron): a point
            // clicked in free space creates no coincident, a point clicked onto
            // a primitive or point welds there, exactly what a deliberate snap
            // records. The ONE exception is the tangent target curve itself: a
            // snap onto it is never turned into a coincident, because tangency
            // already governs that contact (Aaron: "tangent line forces
            // coincident which isn't required"). So we keep the deliberate
            // snaps, drop only the ones aimed at the target curve, and skip the
            // proximity auto-weld so a free-space click stays free.
            const bool isTangentLine =
                (m_activeTool == SketchTool::Line
                 && m_lineMode == LineMode::Tangent
                 && m_pendingEntity.type == SketchEntityType::Line
                 && pendingTangentTarget >= 0
                 && entityById(pendingTangentTarget) != nullptr);
            if (isTangentLine) {
                SketchConstraint tc;
                tc.id = m_nextConstraintId++;
                tc.type = ConstraintType::Tangent;
                tc.entityIds = { m_pendingEntity.id, pendingTangentTarget };
                tc.value = 0.0;
                tc.isDriving = true;
                tc.enabled = true;
                tc.satisfied = true;
                tc.labelVisible = false;
                m_constraints.append(tc);
                pushUndoCommand(sketch::UndoCommand::addConstraint(tc));
                if (SketchEntity* le = entityById(m_pendingEntity.id)) le->constrained = true;
                if (SketchEntity* te = entityById(pendingTangentTarget)) te->constrained = true;
                emit constraintCreated(tc.id);

                // Every placed point of the tangent line is evaluated the same
                // generic way (a deliberate snap onto other geometry welds a
                // Coincident, a free-space click welds nothing), except a snap
                // onto the tangent target itself, which is excluded here.
                createSnapConstraints({m_pendingEntity.id}, pendingTangentTarget);
            } else {
                createSnapConstraints({m_pendingEntity.id});
                createProximityCoincidences(m_pendingEntity.id);   // weld close corners
            }

            // Turn the alignment inferred while drawing (horizontal, vertical,
            // parallel, perpendicular) into real constraints on this line.
            createInferredConstraints({m_pendingEntity.id});

            // Remember where to continue from, before m_pendingEntity is
            // reused for the next segment.
            if (!m_pendingEntity.points.empty()) {
                chainFromId = m_pendingEntity.id;
                chainFrom = QPointF(m_pendingEntity.points.back());
            }
        }
    }

    m_snapEngine.clearInferences();
    m_isDrawing = false;
    m_previewPoints.clear();
    clearDimFields();

    // ADDING GEOMETRY CHANGES THE DEGREES OF FREEDOM, so the constraint state
    // has to be re-published even though no constraint was touched. Without
    // this the reported state goes stale the moment anything is drawn: a
    // sketch with a fresh line still claimed to be "Empty".
    //
    // This costs a solve per committed entity. That is the same cost already
    // paid on every constraint add/delete, and correctness of the displayed
    // state is worth more than avoiding it; revisit only if profiling on a
    // large sketch says otherwise.
    solveConstraints();

    // Polyline chaining: continue from the end just placed, and record the
    // join the same way a snapped point is recorded, so the next segment
    // finishes with a real Coincident rather than merely starting at the
    // same coordinates.
    if (chainFromId >= 0) {
        if (SketchToolHandler* h = activeHandler()) {
            if (h->chainsFromLastPoint(*this)) {
                // Remember what we are chaining from so a drag off the next
                // point can sweep a tangent arc against it (F-3).
                m_chainFromEntityId = chainFromId;
                startEntity(chainFrom);
                // Re-arm drag detection for the NEW segment. Without this the
                // release of the very click that ended the previous segment
                // still counts as a drag (the pointer having traveled far
                // from where that stroke began) and immediately finishes
                // the segment just started, with one point in it. HobbyCAD
                // accepts click-click-click AND press-drag-release, so the
                // per-stage drag state has to start clean for each segment.
                beginDragDetection(mapFromGlobal(QCursor::pos()));
                m_placedSnaps.append({0, sketch::SnapPoint{
                    hobbycad::Point2D(chainFrom.x(), chainFrom.y()),
                    sketch::SnapType::Endpoint,
                    chainFromId}});
            }
        }
    }

    update();
}

void SketchCanvas::commitTangentArcSegment()
{
    const SketchEntity* prevLine = entityById(m_chainFromEntityId);
    if (!prevLine || prevLine->type != SketchEntityType::Line
        || prevLine->points.size() < 2 || m_previewPoints.isEmpty()) {
        finishEntity();   // cannot build an arc here: lay a straight segment
        return;
    }

    const QPointF tangentPoint = m_previewPoints[0];      // shared chain point
    const QPointF endPoint = m_currentMouseWorld;
    const TangentArcResult ta = tangentArcFor(*prevLine, tangentPoint, endPoint);
    if (!ta.valid) { finishEntity(); return; }

    // Build the arc entity from the tangent solve, the same [center, start,
    // end] layout the Arc tool's tangent mode uses. Reuse the id already
    // allocated for this (now-abandoned) chained line segment.
    SketchEntity arc;
    arc.id = m_pendingEntity.id;
    sketch::setArcFromAngles(arc, ta.center, ta.radius, ta.startAngle, ta.sweepAngle);

    // Which arc endpoint is the shared (tangent) one, and which line endpoint
    // it meets, so the join Coincident names the right points.
    const int sharedArcIdx = sketch::nearestArcEndIndex(arc, tangentPoint);
    const int lineIdx =
        (QLineF(QPointF(prevLine->points[0]), tangentPoint).length()
         <= QLineF(QPointF(prevLine->points[1]), tangentPoint).length()) ? 0 : 1;
    const int prevLineId = prevLine->id;

    m_entities.append(arc);

    // Join the arc to the line (Coincident at the shared point) and hold the
    // tangency (Tangent, realized as endpoint tangency by the solver). Two
    // real constraints keep the arc parametric, no re-derivation link needed.
    SketchConstraint join;
    join.id = m_nextConstraintId++;
    join.type = ConstraintType::Coincident;
    join.entityIds = { arc.id, prevLineId };
    join.pointIndices = { sharedArcIdx, lineIdx };
    join.isDriving = true; join.enabled = true; join.satisfied = true; join.labelVisible = false;
    m_constraints.append(join);

    SketchConstraint tangent;
    tangent.id = m_nextConstraintId++;
    tangent.type = ConstraintType::Tangent;
    tangent.entityIds = { arc.id, prevLineId };
    tangent.isDriving = true; tangent.enabled = true; tangent.satisfied = true; tangent.labelVisible = false;
    m_constraints.append(tangent);

    std::vector<sketch::UndoCommand> subs;
    subs.push_back(sketch::UndoCommand::addEntity(arc, "Tangent Arc"));
    subs.push_back(sketch::UndoCommand::addConstraint(join, "Tangent Arc"));
    subs.push_back(sketch::UndoCommand::addConstraint(tangent, "Tangent Arc"));
    pushUndoCommand(sketch::UndoCommand::compound(subs, "Tangent Arc"));

    emit entityCreated(arc.id);
    emit constraintCreated(join.id);      // keep listeners (constraint list, DOF
    emit constraintCreated(tangent.id);   // badge) in sync, as the tangent-LINE path does
    if (SketchEntity* a = entityById(arc.id)) a->constrained = true;
    if (SketchEntity* l = entityById(prevLineId)) l->constrained = true;
    m_profilesCacheDirty = true;

    // The deferred line segment is abandoned (never appended); drop it and
    // continue chaining from the arc's far endpoint.
    m_isDrawing = false;
    m_previewPoints.clear();
    clearDimFields();
    solveConstraints();

    const QPointF farPoint = arc.points[(sharedArcIdx == 1) ? 2 : 1];
    if (SketchToolHandler* h = activeHandler()) {
        if (h->chainsFromLastPoint(*this)) {
            m_chainFromEntityId = arc.id;   // next drag would be tangent to the arc
            startEntity(farPoint);
            beginDragDetection(mapFromGlobal(QCursor::pos()));
            m_placedSnaps.append({0, sketch::SnapPoint{
                hobbycad::Point2D(farPoint.x(), farPoint.y()),
                sketch::SnapType::Endpoint, arc.id}});
        }
    }
    update();
}

bool SketchCanvas::drawTangentArcPreview(QPainter& painter)
{
    const SketchEntity* prevLine = entityById(m_chainFromEntityId);
    if (!prevLine || prevLine->type != SketchEntityType::Line
        || m_previewPoints.isEmpty())
        return false;
    const TangentArcResult ta =
        tangentArcFor(*prevLine, m_previewPoints[0], m_currentMouseWorld);
    if (!ta.valid) return false;

    // Sample the arc into a screen-space polyline: robust to the y-flip and
    // sweep direction without angle bookkeeping.
    painter.save();
    QPen pen(m_theme.preview, 2, Qt::DashLine);
    pen.setCosmetic(true);
    painter.setPen(pen);
    QPainterPath path;
    const int N = 40;
    for (int i = 0; i <= N; ++i) {
        const double a = qDegreesToRadians(ta.startAngle + ta.sweepAngle * (double(i) / N));
        const QPointF w(ta.center.x + ta.radius * qCos(a),
                        ta.center.y + ta.radius * qSin(a));
        const QPointF sc = worldToScreenF(w);
        if (i == 0) path.moveTo(sc); else path.lineTo(sc);
    }
    painter.drawPath(path);
    painter.restore();
    return true;
}

void SketchCanvas::cancelEntity()
{
    m_isDrawing = false;
    m_lineChainPressActive = false;
    m_chainFromEntityId = -1;
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

    // Convert locked dims to library types
    std::vector<std::pair<std::string, double>> libLockedDims;
    libLockedDims.reserve(static_cast<size_t>(lockedDims.size()));
    for (const auto& [label, value] : lockedDims) {
        libLockedDims.emplace_back(label.toStdString(), value);
    }

    // Convert groups to library types
    std::vector<sketch::Group> libGroups(m_groups.begin(), m_groups.end());

    // Call library decomposition
    auto result = sketch::decomposeEntity(
        pendingEntity, libLockedDims,
        [this]() { return nextEntityId(); },
        [this]() { return m_nextConstraintId++; },
        m_nextGroupId++, libGroups, typeName.toStdString(), isFreeform);

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
        if (result.group.containsEntity(e.id))
            e.groupId = result.group.id;
    }

    // Build compound undo command
    std::vector<sketch::UndoCommand> subs;
    for (const auto& e : result.entities)
        subs.push_back(sketch::UndoCommand::addEntity(e));
    for (const auto& c : result.constraints)
        subs.push_back(sketch::UndoCommand::addConstraint(c));
    subs.push_back(sketch::UndoCommand::addGroup(result.group));
    compoundCmd = sketch::UndoCommand::compound(subs, typeName.toStdString());

    m_profilesCacheDirty = true;
    return true;
}

bool SketchCanvas::createCenterlineSlot(SketchEntity& slot,
                                       sketch::UndoCommand& compoundCmd)
{
    // Centerline-driven slot (Aaron's pivot; mirror of the CLI addSlotCenterline).
    // A slot is a round profile swept along a centerline: build the centerline as
    // ordinary construction geometry, a Line for a linear slot (2 centers) or an
    // Arc for an arc slot (center,start,end); point the slot at it via
    // pathEntityId, and group the two. The slot then FOLLOWS the centerline by
    // re-derivation (updateSlotsFromPaths after each solve), not by a constraint;
    // the user constrains/drags the centerline. No decomposition; that is now
    // the separate "Explode" action.
    if (slot.type != SketchEntityType::Slot) return false;
    if (!slot.pathEntityIds.empty()) return false;   // already centerline-driven
    if (slot.points.size() < 2) return false;

    // The centerline itself (Line or Arc, construction) is the library's, shared
    // with the CLI's addSlotCenterline; only the id is the GUI's to give.
    SketchEntity path = hobbycad::toGuiEntity(sketch::makeSlotCenterline(slot));
    path.id = nextEntityId();
    slot.pathEntityIds = { path.id };

    m_entities.append(path);
    m_entities.append(slot);

    // Kind Slot, named by the slot's id: what the CLI makes and the script
    // exporter recognizes.
    SketchGroup group = sketch::makeSlotGroup(m_nextGroupId++, slot.id, {path.id});
    m_groups.append(group);
    for (auto& e : m_entities)
        if (group.containsEntity(e.id)) e.groupId = group.id;

    std::vector<sketch::UndoCommand> subs;
    subs.push_back(sketch::UndoCommand::addEntity(path));
    subs.push_back(sketch::UndoCommand::addEntity(slot));
    subs.push_back(sketch::UndoCommand::addGroup(group));
    compoundCmd = sketch::UndoCommand::compound(subs, "Slot");
    m_profilesCacheDirty = true;
    return true;
}

bool SketchCanvas::createTreeSlotFromSelection()
{
    // Make a MULTI-segment slot from the selected construction path: a chain,
    // a closed loop, or a branching tree of lines and arcs. The selected
    // segments become the centerline (marked construction), and the slot
    // follows their swept outline through every solve, the same pattern as the
    // simple slot, one complexity up. (Aaron: the Y/tree slot.)
    std::vector<int> pathIds;
    for (const SketchEntity* e : selectedEntities()) {
        if (!e) continue;
        if (e->type == SketchEntityType::Line || e->type == SketchEntityType::Arc)
            pathIds.push_back(e->id);
    }
    if (pathIds.size() < 2) {
        emit statusMessage(tr("Select at least two connected lines or arcs to "
                              "make a slot from a path."), 4000);
        return false;
    }

    const double halfWidth = 5.0;   // default; the width becomes editable in the
                                    // properties panel and by scroll later.

    // Validate the path is sweepable at this width before committing anything.
    const std::vector<sketch::Entity> libAll = hobbycad::toLibraryEntities(m_entities);
    const sketch::SlotPathInfo info = sketch::analyzeSlotPath(libAll, pathIds);
    if (!info.valid) {
        emit statusMessage(tr("These do not form a slot path: %1")
                           .arg(QString::fromStdString(info.reason)), 5000);
        return false;
    }
    const std::string problem = sketch::slotWidthProblem(info, halfWidth);
    if (!problem.empty()) {
        emit statusMessage(tr("A width of %1 does not fit this path: %2")
                           .arg(halfWidth * 2.0)
                           .arg(QString::fromStdString(problem)), 5000);
        return false;
    }

    std::vector<sketch::UndoCommand> subs;

    // The segments become the centerline: guides, not profile edges.
    for (int pid : pathIds) {
        SketchEntity* seg = entityById(pid);
        if (!seg || seg->isConstruction) continue;
        const SketchEntity before = *seg;
        seg->isConstruction = true;
        subs.push_back(sketch::UndoCommand::modifyEntity(
            hobbycad::toLibraryEntity(before), hobbycad::toLibraryEntity(*seg)));
    }

    SketchEntity slot;
    slot.id = nextEntityId();
    slot.type = SketchEntityType::Slot;
    slot.radius = halfWidth;               // half-width
    slot.pathEntityIds = pathIds;          // follows the whole path
    m_entities.append(slot);
    subs.push_back(sketch::UndoCommand::addEntity(hobbycad::toLibraryEntity(slot)));

    SketchGroup group = sketch::makeSlotGroup(m_nextGroupId++, slot.id, pathIds);
    m_groups.append(group);
    for (auto& e : m_entities)
        if (group.containsEntity(e.id)) e.groupId = group.id;
    subs.push_back(sketch::UndoCommand::addGroup(group));

    pushUndoCommand(sketch::UndoCommand::compound(subs, "Slot from path"));

    // Derive the outline now so it draws before the next solve.
    updateSlotsFromPaths();
    m_profilesCacheDirty = true;
    emit entityCreated(slot.id);
    update();
    return true;
}

bool SketchCanvas::decomposeSlotEntity(const SketchEntity& slot,
                                       sketch::UndoCommand& compoundCmd)
{
    // Simple linear slots only for now: a single straight centerline. Arc
    // slots decompose less cleanly (concentric sides) and path-slots must stay
    // whole to follow an arbitrary path, so both keep the first-class Slot.
    if (slot.type != SketchEntityType::Slot) return false;
    if (!slot.pathEntityIds.empty()) return false;     // path-slot: keep whole
    if (slot.points.size() != 2) return false;         // linear (2 centers) only
    const double halfWidth = slot.radius;
    if (!sketch::slotWidthIsPositive(2.0 * halfWidth)) return false;

    // The slot's centerline becomes a construction Line between its two
    // centers, the same role the path plays in "Sweep Along This Path".
    sketch::Entity centerline;
    centerline.id = nextEntityId();
    centerline.type = sketch::EntityType::Line;
    centerline.points = { slot.points[0], slot.points[1] };
    centerline.isConstruction = true;

    // The centerline goes in first; applySweep then adds the sides, caps and
    // constraints, groups them with it, and wires the back-links (the same
    // commit as Sweep Along Path and the CLI).
    m_entities.append(SketchEntity(centerline));
    const sketch::SweepApplied result = sketch::applySweep(
        m_entities, m_constraints, m_groups, centerline.id, halfWidth,
        sketch::SweepEndStyle::Round,
        [this]() { return nextEntityId(); },
        [this]() { return m_nextConstraintId++; },
        m_nextGroupId++);
    if (!result.success) {
        m_entities.removeLast();   // the centerline was only for the sweep
        return false;
    }

    // Constraints: the decomposition supplies them all (the corner
    // coincidents, the equal-width half-segment chain, and the one tie that
    // keeps the centerline the slot's centerline). No rigidity ties are added
    // here: matching the CLI "sweep", the slot is under-constrained by design
    // and the user adds dimensions by hand. Every extra tie tried before
    // (perpendicular radius segments, cap Tangent / PointOnLine / Equal)
    // over-constrained, conflicted under drag, or ballooned a cap.
    std::vector<sketch::UndoCommand> subs;
    subs.push_back(sketch::UndoCommand::addEntity(centerline));
    for (const auto& e : result.entities) subs.push_back(sketch::UndoCommand::addEntity(e));
    for (const auto& cc : result.constraints) subs.push_back(sketch::UndoCommand::addConstraint(cc));
    subs.push_back(sketch::UndoCommand::addGroup(result.group));
    compoundCmd = sketch::UndoCommand::compound(subs, "Slot");

    m_profilesCacheDirty = true;
    return true;
}

// ---- Inline dimension input helpers ------------------------------------

void SketchCanvas::initDimFields()
{
    // Drop the previous stage's fields (carrying locked values forward), let
    // the active tool populate the new ones, then build their input state.
    m_dimInput.reinitForNextStage();
    if (SketchToolHandler* h = activeHandler())
        h->initDimFields(*this);   // calls addDimField() -> m_dimInput.addField()
    m_dimInput.beginStates();
    emit toolHintChanged(currentToolHint());
}

void SketchCanvas::clearDimFields()
{
    m_dimInput.clearAll();
}

double SketchCanvas::getLockedDim(int fieldIndex) const
{
    return m_dimInput.getLocked(fieldIndex);
}

// ---- DimensionInputHost callbacks -----------------------------------
bool SketchCanvas::dimChainsFromLastPoint() const
{
    const SketchToolHandler* h = activeHandler();
    return h && h->chainsFromLastPoint(*this);
}

void SketchCanvas::dimAfterLock()
{
    // A value was just locked. Tools that derive state from "all fields
    // locked" capture it now (Rectangle's rotation reference), then the
    // preview is refreshed with the new constraint.
    if (SketchToolHandler* h = activeHandler())
        h->dimFieldsChanged(*this);
    updateEntity(m_currentMouseWorld);
}

void SketchCanvas::dimReapplyPreview()
{
    updateEntity(m_currentMouseWorld);
}

void SketchCanvas::createLockedConstraints(int entityId)
{
    const SketchEntity* entity = entityById(entityId);
    if (!entity) return;

    for (const auto& [label, value] : m_dimInput.lockedForConstraints()) {
        m_constraintTargetEntities.clear();
        m_constraintTargetPoints.clear();

        ConstraintType ctype;
        if (label == QStringLiteral("Radius") || label == QStringLiteral("Major Radius")) {
            ctype = ConstraintType::Radius;
            m_constraintTargetEntities.append(entityId);
        } else if (label == QStringLiteral("Diameter")) {
            ctype = ConstraintType::Diameter;
            m_constraintTargetEntities.append(entityId);
        } else if (label == QStringLiteral("Sweep Angle")) {
            // Create 2 construction lines (center→start, center→end) + Angle constraint + group
            if (entity->type == SketchEntityType::Arc && entity->points.size() >= 3) {
                const QPointF center(entity->points[0]);
                const QPointF startPt(entity->points[1]);
                const QPointF endPt(entity->points[2]);

                // Construction line 1: center → start
                SketchEntity line1;
                line1.id = nextEntityId();
                line1.type = SketchEntityType::Line;
                line1.points.push_back(center);
                line1.points.push_back(startPt);
                line1.isConstruction = true;

                // Construction line 2: center → end
                SketchEntity line2;
                line2.id = nextEntityId();
                line2.type = SketchEntityType::Line;
                line2.points.push_back(center);
                line2.points.push_back(endPt);
                line2.isConstruction = true;

                // Create Angle constraint between the two construction lines
                SketchConstraint angleC;
                angleC.id = m_nextConstraintId++;
                angleC.type = ConstraintType::Angle;
                angleC.entityIds = {line1.id, line2.id};
                angleC.value = value;
                angleC.isDriving = true;
                angleC.enabled = true;
                angleC.satisfied = true;
                angleC.labelVisible = true;
                angleC.anchorPoint = center;  // Explicit vertex at arc center
                angleC.supplementary = (std::abs(value) > 180.0);

                // Compute label position at midpoint of sweep arc
                double midAngleRad = qDegreesToRadians(
                    entity->startAngle + entity->sweepAngle / 2.0);
                double labelDist = entity->radius + kRadiusLabelOffsetPx / m_zoom;
                angleC.labelPosition = geometry::polarPoint(center, labelDist, midAngleRad);

                // The rig is a group of kind SweepAngle; the name is a label.
                int groupCount = 0;
                for (const auto& g : m_groups) {
                    if (sketch::isSweepAngleGroup(g)) groupCount++;
                }
                SketchGroup group;
                group.id = m_nextGroupId++;
                group.kind = sketch::GroupKind::SweepAngle;
                group.name = sketch::sweepAngleGroupName(groupCount + 1);
                group.entityIds = {entityId, line1.id, line2.id};
                group.constraintIds = {angleC.id};
                group.locked = true;

                // Set groupId on line entities
                line1.groupId = group.id;
                line2.groupId = group.id;

                // Add to state
                m_entities.append(line1);
                m_entities.append(line2);
                m_constraints.append(angleC);
                m_groups.append(group);

                // Build compound undo
                std::vector<sketch::UndoCommand> subs;
                subs.push_back(sketch::UndoCommand::addEntity(line1));
                subs.push_back(sketch::UndoCommand::addEntity(line2));
                subs.push_back(sketch::UndoCommand::addConstraint(angleC));
                subs.push_back(sketch::UndoCommand::addGroup(group));
                pushUndoCommand(sketch::UndoCommand::compound(subs, "Sweep Angle"));
            }
            continue;  // Skip the generic createConstraint call
        } else if (label.contains(QStringLiteral("Angle"))) {
            // A locked Angle field on a single line fixes that line's angle
            // from horizontal. FixedAngle is a one-entity constraint that the
            // solver realizes against an internal horizontal reference line.
            // (Angle between two picked lines is a separate two-entity flow.)
            if (entity->type == SketchEntityType::Line) {
                ctype = ConstraintType::FixedAngle;
                m_constraintTargetEntities.append(entityId);
            } else {
                continue;  // Non-line entity: no single-entity angle lock
            }
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

        // Position label near entity
        QPointF labelPos;
        if (!entity->points.empty()) {
            if ((ctype == ConstraintType::Radius || ctype == ConstraintType::Diameter)
                && entity->points.size() >= 2) {
                // Radius/Diameter: place label along direction from center to p1
                QPointF center = entity->points[0];
                QPointF dir = QPointF(entity->points[1]) - center;
                double dirLen = geometry::length(dir);
                if (dirLen > geometry::kDegenerateLen)
                    labelPos = center + geometry::normalize(dir) * (entity->radius / 2.0);
                else
                    labelPos = center + QPointF(entity->radius / 2.0, 0);
            } else if (entity->points.size() >= 2) {
                labelPos = QPointF((entity->points[0] + entity->points[1]) / 2.0) + QPointF(0, -10);
            } else {
                labelPos = QPointF(entity->points[0]) + QPointF(15, -15);
            }
        }

        // Skip the over-constrained check: the user explicitly locked
        // this dimension by typing a value and pressing Enter, so we
        // honor their intent and create the constraint directly.
        createConstraint(ctype, value, labelPos, /*skipOverConstrainCheck=*/true);
    }
}

// drawDimInputField() moved to DimensionInput::draw() (dimensioninput.cpp).

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

void SketchCanvas::setEntityCenterline(int entityId, bool isCenterline)
{
    SketchEntity* entity = entityById(entityId);
    if (entity) {
        entity->isCenterline = isCenterline;
        emit entityModified(entityId);
        update();
    }
}

void SketchCanvas::toggleSelectedConstruction()
{
    // While drawing, toggle the in-progress entity (like Fusion's X toggle)
    if (m_isDrawing) {
        m_pendingEntity.isConstruction = !m_pendingEntity.isConstruction;
        update();
        return;
    }

    if (m_selectedIds.isEmpty()) return;

    std::vector<sketch::UndoCommand> subs;
    for (int id : m_selectedIds) {
        SketchEntity* ent = entityById(id);
        if (!ent) continue;
        sketch::Entity before = *ent;
        ent->isConstruction = !ent->isConstruction;
        subs.push_back(sketch::UndoCommand::modifyEntity(before, *ent,
                                                         "Toggle construction"));
        emit entityModified(id);
    }
    if (subs.empty()) return;

    if (subs.size() == 1) {
        pushUndoCommand(subs[0]);
    } else {
        pushUndoCommand(sketch::UndoCommand::compound(subs, "Toggle construction"));
    }
    m_profilesCacheDirty = true;
    update();
}

void SketchCanvas::notifyEntityChanged(int entityId)
{
    solveConstraints();

    syncArcAfterSolve(entityId);

    emit entityModified(entityId);
    update();
}

void SketchCanvas::notifyEntityPointChanged(int entityId, int pointIndex)
{
    // Add a temporary FixedPoint constraint so the solver keeps this point
    // exactly where the user placed it, while adjusting other geometry.
    SketchConstraint tempFixed;
    tempFixed.id = m_nextConstraintId++;
    tempFixed.type = ConstraintType::FixedPoint;
    tempFixed.entityIds.push_back(entityId);
    tempFixed.pointIndices.push_back(pointIndex);
    tempFixed.enabled = true;
    tempFixed.isDriving = true;
    tempFixed.satisfied = true;

    m_constraints.append(tempFixed);
    solveConstraints();
    m_constraints.removeLast();  // Remove the temporary constraint

    syncArcAfterSolve(entityId);

    emit entityModified(entityId);
    update();
}

// ---- Sweep-angle construction line helpers ----

bool SketchCanvas::isSweepAngleGroup(int groupId) const
{
    for (const auto& g : m_groups) {
        if (g.id == groupId) return sketch::isSweepAngleGroup(g);
    }
    return false;
}

int SketchCanvas::findSweepAngleGroupForArc(int arcId) const
{
    for (const auto& g : m_groups) {
        if (sketch::isSweepAngleGroup(g) && g.containsEntity(arcId)) return g.id;
    }
    return -1;
}

void SketchCanvas::syncSweepAngleConstructionLines(const SketchEntity& arc)
{
    if (arc.type != SketchEntityType::Arc || arc.points.size() < 3) return;

    int gid = findSweepAngleGroupForArc(arc.id);
    if (gid < 0) return;

    // Find the group
    const SketchGroup* group = groupById(gid);
    if (!group) return;

    // Find the 2 construction line entities in the group
    QPointF center(arc.points[0]);
    QPointF startPt(arc.points[1]);
    QPointF endPt(arc.points[2]);

    int lineCount = 0;
    for (int eid : group->entityIds) {
        SketchEntity* e = entityById(eid);
        if (e && e->isConstruction && e->type == SketchEntityType::Line
                && e->points.size() >= 2) {
            if (lineCount == 0) {
                // First construction line: center → start
                e->points[0] = center;
                e->points[1] = startPt;
            } else {
                // Second construction line: center → end
                e->points[0] = center;
                e->points[1] = endPt;
            }
            ++lineCount;
            if (lineCount >= 2) break;
        }
    }

    // Update the Angle constraint's anchorPoint and labelPosition to follow the arc
    for (int cid : group->constraintIds) {
        SketchConstraint* c = constraintById(cid);
        if (c && c->type == ConstraintType::Angle) {
            c->anchorPoint = center;
            // Reposition label at midpoint of sweep arc
            double midAngleRad = qDegreesToRadians(arc.startAngle + arc.sweepAngle / 2.0);
            double labelDist = arc.radius + kRadiusLabelOffsetPx / m_zoom;
            c->labelPosition = geometry::polarPoint(center, labelDist, midAngleRad);
        }
    }
}

void SketchCanvas::reestablishTangency(SketchEntity& arc)
{
    if (arc.type != SketchEntityType::Arc
            || arc.tangentEntityId < 0
            || arc.points.size() < 3)
        return;

    const SketchEntity* tEnt = entityById(arc.tangentEntityId);
    if (!tEnt) return;

    auto result = sketch::reestablishTangency(
        toLibraryEntity(arc), toLibraryEntity(*tEnt));

    if (result.success) {
        arc.points = result.arc.points;
        arc.startAngle = result.arc.startAngle;
        // radius and sweepAngle preserved by library
    }
}

void SketchCanvas::ensureTextRotationHandle(SketchEntity& entity)
{
    if (entity.type != SketchEntityType::Text || entity.points.size() >= 2)
        return;
    double dist = std::max(entity.fontSize * 2.0,
                           entity.fontSize * static_cast<double>(entity.text.length()) * 0.6);
    double rad = qDegreesToRadians(entity.textRotation);
    QPointF anchor(entity.points[0]);
    entity.points.push_back({anchor.x() + dist * std::cos(rad),
                             anchor.y() + dist * std::sin(rad)});
}

void SketchCanvas::recomputeTextRotationHandle(SketchEntity& entity)
{
    sketch::resyncTextHandle(entity);
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
    if (e1.type == SketchEntityType::Line && e2.type == SketchEntityType::Line &&
        e1.points.size() >= 2 && e2.points.size() >= 2) {

        auto lineIntersect = geometry::infiniteLineIntersection(
            e1.points[0], e1.points[1], e2.points[0], e2.points[1]);
        if (!lineIntersect.intersects) return {};

        double radius = geometry::length(hint - lineIntersect.point);
        return geometry::circleTangentToTwoLines(
            e1.points[0], e1.points[1],
            e2.points[0], e2.points[1],
            radius, hint);
    }
    return {};
}

SketchCanvas::TangentCircle SketchCanvas::calculate3TangentCircle(
    const SketchEntity& e1, const SketchEntity& e2, const SketchEntity& e3) const
{
    if (e1.type == SketchEntityType::Line && e2.type == SketchEntityType::Line &&
        e3.type == SketchEntityType::Line && e1.points.size() >= 2 &&
        e2.points.size() >= 2 && e3.points.size() >= 2) {

        return geometry::circleTangentToThreeLines(
            e1.points[0], e1.points[1],
            e2.points[0], e2.points[1],
            e3.points[0], e3.points[1]);
    }
    return {};
}

SketchCanvas::TangentArc SketchCanvas::calculateTangentArc(
    const SketchEntity& tangentEntity, const QPointF& tangentPoint, const QPointF& endPoint) const
{
    // A line is its own tangent edge; a rectangle lends the edge nearest the
    // tangent point (library, segment distance).
    Point2D a, b;
    if (sketch::closestTangentHostEdge(tangentEntity, Point2D(tangentPoint), a, b)) {
        return geometry::arcTangentToLine(QPointF(a), QPointF(b), tangentPoint, endPoint);
    }

    return {};
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
                                     bool skipOverConstrainCheck, bool startEditing,
                                     bool driving, bool supplementary)
{
    // Which points on the entities are constrained: the nearest to each
    // click (selection state, so resolved here).
    std::vector<int> pointIndices;
    for (int i = 0; i < m_constraintTargetEntities.size(); ++i) {
        SketchEntity* entity = entityById(m_constraintTargetEntities[i]);
        if (entity && i < m_constraintTargetPoints.size())
            pointIndices.push_back(findNearestPointIndex(entity, m_constraintTargetPoints[i]));
    }
    const SketchEntity* first =
        m_constraintTargetEntities.empty() ? nullptr : entityById(m_constraintTargetEntities[0]);
    SketchConstraint constraint(sketch::makeDimensionConstraint(
        m_nextConstraintId++, type,
        std::vector<int>(m_constraintTargetEntities.begin(), m_constraintTargetEntities.end()),
        pointIndices, value, labelPos, driving, supplementary, first));

    // Check if this constraint would over-constrain the sketch
    // (skipped for auto-created constraints from locked dimension fields,
    //  since the user explicitly typed a value and pressed Enter)
    if (driving && !skipOverConstrainCheck && SketchSolver::isAvailable()) {
        SketchSolver solver;
        OverConstraintInfo overConstraintInfo = solver.checkOverConstrain(m_entities, m_constraints, constraint);

        if (overConstraintInfo.wouldOverConstrain) {
            // Build description of conflicting constraints
            QString conflictDetails;
            if (!overConstraintInfo.conflictingConstraintIds.empty()) {
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

            // Offer to create a Driven dimension instead. The two kinds of
            // over-constraint need different wording: a redundant dimension
            // does not conflict with anything, it just measures something
            // already determined, so saying "conflicts" would send the user
            // hunting for a disagreement that does not exist.
            const QString lead = overConstraintInfo.isRedundant
                ? tr("This dimension is already implied by the existing "
                     "constraints, so it would add nothing.")
                : tr("This dimension would over-constrain the sketch.");

            QMessageBox::StandardButton reply = QMessageBox::question(
                this,
                tr("Over-Constrained"),
                lead + conflictDetails +
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

    // Optionally enter inline edit mode immediately
    if (startEditing && constraint.isDriving)
        beginInlineConstraintEdit(constraint.id, /*isCreation=*/true);

    update();
}

ConstraintType SketchCanvas::detectConstraintType(int entityId1, int entityId2) const
{
    const SketchEntity* e1 = entityById(entityId1);
    const SketchEntity* e2 = entityById(entityId2);

    if (!e1 || !e2) return ConstraintType::Distance;
    return sketch::suggestDimensionType(*e1, *e2);
}

QPointF SketchCanvas::findClosestPointOnEntity(const SketchEntity* entity, const QPointF& worldPos) const
{
    if (!entity || entity->points.empty()) {
        return worldPos;
    }

    // Use library's closestPoint implementation
    sketch::Entity libEntity = hobbycad::toLibraryEntity(*entity);
    return libEntity.closestPoint(worldPos);
}

int SketchCanvas::findNearestPointIndex(const SketchEntity* entity, const QPointF& worldPos) const
{
    if (!entity || entity->points.empty()) return 0;
    return sketch::nearestPointIndex(*entity, worldPos);
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

        // Extra geometry hit-tests for angle arcs and radial dimension lines
        // (these hit-test the drawn geometry, not just the label text)
        if (c.type == ConstraintType::Angle && c.entityIds.size() >= 2) {
            const SketchEntity* ae1 = entityById(c.entityIds[0]);
            const SketchEntity* ae2 = entityById(c.entityIds[1]);
            if (ae1 && ae2
                    && ae1->type == SketchEntityType::Line
                    && ae2->type == SketchEntityType::Line
                    && ae1->points.size() >= 2 && ae2->points.size() >= 2) {
                QPointF intersection;
                if (c.hasAnchorPoint()) {
                    intersection = c.anchorPoint;
                } else {
                    QLineF l1(ae1->points[0], ae1->points[1]);
                    QLineF l2(ae2->points[0], ae2->points[1]);
                    if (l1.intersects(l2, &intersection) == QLineF::NoIntersection)
                        intersection = c.labelPosition;
                }
                QPointF originScr = worldToScreen(intersection).toPointF();
                QPointF s1a = worldToScreen(ae1->points[0]).toPointF();
                QPointF s1b = worldToScreen(ae1->points[1]).toPointF();
                QPointF s2a = worldToScreen(ae2->points[0]).toPointF();
                QPointF s2b = worldToScreen(ae2->points[1]).toPointF();
                QPointF dir1 = s1b - s1a;
                QPointF dir2 = s2b - s2a;
                if (QLineF(originScr, s1a).length() > QLineF(originScr, s1b).length())
                    dir1 = s1a - s1b;
                if (QLineF(originScr, s2a).length() > QLineF(originScr, s2b).length())
                    dir2 = s2a - s2b;
                const double a1 = std::atan2(-dir1.y(), dir1.x());
                const double a2 = std::atan2(-dir2.y(), dir2.x());
                // Match the DRAWN arc: bracket the label's sector (4 boundary
                // rays a1, a1+pi, a2, a2+pi), same as drawAngleConstraint, so the
                // clickable arc is exactly where the arc is rendered.
                const QPointF labelScr = worldToScreen(c.labelPosition).toPointF();
                const double al = std::atan2(-(labelScr.y() - originScr.y()),
                                              labelScr.x() - originScr.x());
                auto normPi = [](double a){ while (a > M_PI) a -= 2.0*M_PI; while (a < -M_PI) a += 2.0*M_PI; return a; };
                const double rays[4] = { a1, a1 + M_PI, a2, a2 + M_PI };
                double cw = -2.0*M_PI, ccw = 2.0*M_PI;
                for (double r : rays) {
                    const double d = normPi(r - al);
                    if (d >= -geometry::kZeroEps && d < ccw) ccw = d;
                    if (d <=  geometry::kZeroEps && d > cw)  cw  = d;
                }
                const double startAngle = al + cw;
                const double sweepAngle = ccw - cw;   // >= 0
                // Hit-test the arc where it is drawn: radius follows the label
                // distance from the vertex (matches drawAngleConstraint).
                double arcRadius = geometry::lineLength(originScr, labelScr);
                if (arcRadius < 18.0) arcRadius = 18.0;
                double arcTol = 8.0;
                QPointF delta = screenPos - originScr;
                double clickDist = geometry::length(delta);
                if (std::abs(clickDist - arcRadius) < arcTol) {
                    double rel = normPi(std::atan2(-delta.y(), delta.x()) - startAngle);
                    if (rel < 0) rel += 2.0 * M_PI;
                    if (rel <= sweepAngle + geometry::kZeroEps)
                        return c.id;
                }
            }
        } else if ((c.type == ConstraintType::Radius || c.type == ConstraintType::Diameter)
                   && !c.entityIds.empty()) {
            const SketchEntity* re = entityById(c.entityIds[0]);
            if (re && (re->type == SketchEntityType::Circle || re->type == SketchEntityType::Arc)
                && !re->points.empty()) {
                QPointF sc = worldToScreen(re->points[0]).toPointF();
                QPointF labelPt = worldToScreen(c.labelPosition).toPointF();
                double radiusPx = re->radius * m_zoom;
                QPointF along = labelPt - sc;
                double alongLen = geometry::length(along);
                if (alongLen >= geometry::kDegenerateLen) {
                    QPointF dir = geometry::normalize(along);
                    QPointF perp = geometry::perpendicular(dir);
                    QPointF edgePt = sc + dir * radiusPx;
                    // Hit-test the dimension line from center to edge
                    double lineTol = 8.0;
                    QPointF v = screenPos - sc;
                    double proj = v.x() * dir.x() + v.y() * dir.y();
                    double perpDist = std::abs(v.x() * perp.x() + v.y() * perp.y());
                    double segLen = QLineF(sc, edgePt).length();
                    if (perpDist < lineTol && proj >= -lineTol && proj <= segLen + lineTol)
                        return c.id;
                }
            }
        }

        // Hit-test the label text position
        auto pos = m_constraintRenderer.computeConstraintLabelPosition(c);
        if (!pos.found) continue;

        double dist = QLineF(pos.textCenter, screenPos).length();
        if (dist < tolerance)
            return c.id;
    }

    return -1;
}

void SketchCanvas::editConstraintValue(int constraintId)
{
    SketchConstraint* constraint = constraintById(constraintId);
    if (!constraint || !constraint->isDriving) return;

    // Delegate to inline constraint editing instead of modal dialog
    beginInlineConstraintEdit(constraintId, /*isCreation=*/false);
}

// setConstraintValue, sweep-angle Angle constraint: applied directly to the
// arc geometry. Returns true when it handled the edit.
bool SketchCanvas::applySweepAngleValue(SketchConstraint* constraint, const SketchConstraint& oldConstraint,
                                        int constraintId, double newValue)
{
    for (const auto& g : m_groups) {
        if (isSweepAngleGroup(g.id) && g.containsConstraint(constraintId)) {
            int arcId = -1;
            for (int eid : g.entityIds) {
                const SketchEntity* e = entityById(eid);
                if (e && e->type == SketchEntityType::Arc) { arcId = eid; break; }
            }
            if (arcId >= 0) {
                SketchEntity* arc = entityById(arcId);
                if (arc && arc->type == SketchEntityType::Arc
                        && arc->points.size() >= 3) {
                    const SketchEntity oldArc = *arc;
                    double newSweep = (arc->sweepAngle >= 0) ? newValue : -newValue;
                    arc->sweepAngle = newSweep;
                    double endRad = qDegreesToRadians(arc->startAngle + newSweep);
                    arc->points[2] = {
                        arc->points[0].x + arc->radius * std::cos(endRad),
                        arc->points[0].y + arc->radius * std::sin(endRad)};
                    reestablishTangency(*arc);
                    syncSweepAngleConstructionLines(*arc);
                    constraint->supplementary = (std::abs(newSweep) > 180.0);
                    constraint->anchorPoint = arc->points[0];

                    // Push compound undo: constraint + arc
                    pushConstraintAndEntityEdit(oldConstraint, *constraint, oldArc, *arc, "Edit Sweep Angle");
                }
            }
            emit constraintModified(constraintId);
            update();
            return true;
        }
    }
    return false;
}

// setConstraintValue, TangentAngle: re-seed the anchor's handle(s) to the new
// DIRECTED angle so the solver's parallel equation lands on the correct 0-360
// side, then solve. Returns true when it handled the edit.
bool SketchCanvas::applyTangentAngleValue(SketchConstraint* constraint, const SketchConstraint& oldConstraint,
                                          int constraintId, double newValue)
{
    SketchEntity* e = entityById(constraint->entityIds[0]);
    const int a = constraint->pointIndices.empty() ? -1 : constraint->pointIndices[0];
    const int n = e ? static_cast<int>(e->points.size()) : 0;
    if (e && e->type == SketchEntityType::Spline && e->splineBezier
        && a >= 0 && a < n && a % 3 == 0) {
        const SketchEntity oldEntity = *e;
        const QPointF anchor(e->points[a]);
        const double rad = qDegreesToRadians(newValue);
        const QPointF u(std::cos(rad), std::sin(rad));
        if (a + 1 < n) {
            const double L = QLineF(anchor, QPointF(e->points[a+1])).length();
            e->points[a+1] = anchor + u * L;
        }
        if (a - 1 >= 0) {
            const double L = QLineF(anchor, QPointF(e->points[a-1])).length();
            e->points[a-1] = anchor - u * L;
        }
        pushConstraintAndEntityEdit(oldConstraint, *constraint, oldEntity, *e, "Edit Tangent Angle");
        solveConstraints();
        emit constraintModified(constraintId);
        update();
        return true;
    }
    return false;
}

// setConstraintValue, Radius/Diameter applied to the geometry directly (a
// 2-point circle, a 3-point circle, a tangent arc): the solver only knows the
// center. Returns true when it handled the edit; false hands the value to
// the solver path.
bool SketchCanvas::applyRadialValueDirect(SketchConstraint* constraint, const SketchConstraint& oldConstraint,
                                          int constraintId, double newValue)
{
// Radius/Diameter on a 2-point circle: keep p1 fixed, move p2 and center.
// The solver only knows about the center so it would keep center fixed.
    SketchEntity* ent = entityById(constraint->entityIds[0]);
if (ent && ent->type == SketchEntityType::Circle
    && ent->points.size() == 3) {
    // 2-point circle: [center, p1, p2]
    const SketchEntity oldEntity = *ent;
    double newRadius = (constraint->type == ConstraintType::Diameter)
                       ? newValue / 2.0 : newValue;
    QPointF p1 = ent->points[1];
    QPointF p2 = ent->points[2];
    // Move p2 along the p1→p2 direction to achieve new diameter
    QPointF dir = p2 - p1;
    double len = geometry::length(dir);
    if (len > geometry::kDegenerateLen) {
        QPointF newP2 = p1 + dir * (newRadius * 2.0 / len);
        ent->points[2] = newP2;
        ent->points[0] = (p1 + newP2) / 2.0;  // center = midpoint
    }
    ent->radius = newRadius;

    std::string desc = (constraint->type == ConstraintType::Radius)
                       ? "Edit Radius" : "Edit Diameter";
    pushConstraintAndEntityEdit(oldConstraint, *constraint, oldEntity, *ent, desc);

    emit constraintModified(constraintId);
    update();
    return true;
}

// Radius/Diameter on a 3-point circle: keep p1 fixed, adjust center and
// reposition p2/p3 on the new circle (same angular direction from center).
if (ent && ent->type == SketchEntityType::Circle
    && ent->points.size() == 4) {
    // 3-point circle: [center, p1, p2, p3]
    const SketchEntity oldEntity = *ent;
    double newRadius = (constraint->type == ConstraintType::Diameter)
                       ? newValue / 2.0 : newValue;

    // Keep p1 fixed. Find new center on the line from p1 through old center,
    // at distance newRadius from p1.
    QPointF p1 = ent->points[1];
    QPointF oldCenter = ent->points[0];
    QPointF dir = oldCenter - p1;
    double dirLen = geometry::length(dir);
    QPointF newCenter;
    if (dirLen > geometry::kDegenerateLen) {
        newCenter = p1 + dir * (newRadius / dirLen);
    } else {
        newCenter = p1 + QPointF(newRadius, 0);
    }
    ent->points[0] = newCenter;
    ent->radius = newRadius;

    // Reposition p2 and p3: keep same angular direction from new center
    for (int i = 2; i <= 3; ++i) {
        QPointF ptDir = QPointF(ent->points[i]) - oldCenter;
        double ptLen = geometry::length(ptDir);
        if (ptLen > geometry::kDegenerateLen) {
            ent->points[i] = newCenter + ptDir * (newRadius / ptLen);
        }
    }

    std::string desc = (constraint->type == ConstraintType::Radius)
                       ? "Edit Radius" : "Edit Diameter";
    pushConstraintAndEntityEdit(oldConstraint, *constraint, oldEntity, *ent, desc);

    emit constraintModified(constraintId);
    update();
    return true;
}

// Radius/Diameter on a tangent arc: apply directly like sweep angle,
// because the solver treats arcs as circles and cannot update arc
// endpoints/angles after moving the center.
if (ent && ent->type == SketchEntityType::Arc
    && ent->points.size() >= 3) {
    const SketchEntity oldEntity = *ent;
    double newRadius = (constraint->type == ConstraintType::Diameter)
                       ? newValue / 2.0 : newValue;
    ent->radius = newRadius;
    reestablishTangency(*ent);
    syncSweepAngleConstructionLines(*ent);
    for (const auto& g : m_groups) {
        if (isSweepAngleGroup(g.id) && g.containsEntity(ent->id)) {
            for (int cid : g.constraintIds) {
                SketchConstraint* ac = constraintById(cid);
                if (ac && ac->type == ConstraintType::Angle)
                    ac->anchorPoint = ent->points[0];
            }
        }
    }

    // Push compound undo: constraint + entity
    std::string desc = (constraint->type == ConstraintType::Radius)
                       ? "Edit Radius" : "Edit Diameter";
    pushConstraintAndEntityEdit(oldConstraint, *constraint, oldEntity, *ent, desc);

    emit constraintModified(constraintId);
    update();
    return true;
}
    return false;
}

// setConstraintValue, solver path: pin one end of a dimensioned line (both
// ends for an angle) so the solver moves the other. Returns the pin count;
// the caller removes them (ids <= -999) after solving.
int SketchCanvas::addTemporaryValuePins(const SketchConstraint* constraint)
{
    int pinsAdded = 0;
    if (constraint->type == ConstraintType::Distance
        && !constraint->entityIds.empty()) {
        const SketchEntity* entity = entityById(constraint->entityIds[0]);
        if (entity && entity->type == SketchEntityType::Line
            && entity->points.size() == 2) {
            SketchConstraint pin;
            pin.id = -999;
            pin.type = ConstraintType::FixedPoint;
            pin.entityIds.push_back(entity->id);
            pin.pointIndices.push_back(0);
            pin.isDriving = true;
            pin.enabled = true;
            pin.satisfied = true;
            pin.labelVisible = false;
            m_constraints.append(pin);
            pinsAdded = 1;
        }
    } else if (constraint->type == ConstraintType::Angle
               && !constraint->entityIds.empty()) {
        const SketchEntity* e1 = entityById(constraint->entityIds[0]);
        if (e1 && e1->type == SketchEntityType::Line
            && e1->points.size() == 2) {
            for (int pi = 0; pi < 2; ++pi) {
                SketchConstraint pin;
                pin.id = -999 - pi;
                pin.type = ConstraintType::FixedPoint;
                pin.entityIds.push_back(e1->id);
                pin.pointIndices.push_back(pi);
                pin.isDriving = true;
                pin.enabled = true;
                pin.satisfied = true;
                pin.labelVisible = false;
                m_constraints.append(pin);
            }
            pinsAdded = 2;
        }
    }
    return pinsAdded;
}

void SketchCanvas::setConstraintValue(int constraintId, double newValue)
{
    SketchConstraint* constraint = constraintById(constraintId);
    if (!constraint || !constraint->isDriving) return;

    // Refuse a value that would destroy the geometry, here rather than
    // only in the widgets. The inline editor checks, the properties panel
    // did not, and the CLI is a third door onto the same field; a rule
    // enforced at one of them is not enforced. A zero length cannot be
    // undone by typing a bigger number afterwards: the shape is gone by
    // then, not merely small.
    if (!sketch::isValidConstraintValue(constraint->type, newValue)) return;

    if (qFuzzyCompare(newValue, constraint->value)) return;

    // Capture before-state for undo
    const SketchConstraint oldConstraint = *constraint;

    constraint->value = newValue;

    // Sweep-angle Angle constraint: applied directly to the arc geometry.
    if (constraint->type == ConstraintType::Angle
        && applySweepAngleValue(constraint, oldConstraint, constraintId, newValue)) {
        return;
    }

    // TangentAngle: re-seed the anchor's handle(s) to the new DIRECTED angle so
    // the solver's parallel equation lands on the correct 0-360 side, then solve.
    if (constraint->type == ConstraintType::TangentAngle
        && !constraint->entityIds.empty()
        && applyTangentAngleValue(constraint, oldConstraint, constraintId, newValue)) {
        return;
    }

    // Radius/Diameter on a 2-point circle, a 3-point circle or a tangent arc
    // is applied to the geometry directly; the solver only knows the center.
    if ((constraint->type == ConstraintType::Radius
         || constraint->type == ConstraintType::Diameter)
        && !constraint->entityIds.empty()
        && applyRadialValueDirect(constraint, oldConstraint, constraintId, newValue)) {
        return;
    }

    // For other constraint types, use solver with temporary pin
    int pinsAdded = addTemporaryValuePins(constraint);

    solveConstraints();

    if (pinsAdded > 0) {
        m_constraints.erase(
            std::remove_if(m_constraints.begin(), m_constraints.end(),
                           [](const SketchConstraint& c) { return c.id <= -999; }),
            m_constraints.end());
    }

    // Push undo for the constraint value change (solver path)
    // Re-find constraint pointer (may have shifted after erase)
    constraint = constraintById(constraintId);
    if (constraint) {
        std::string desc;
        switch (constraint->type) {
        case ConstraintType::Distance:  desc = "Edit Distance"; break;
        case ConstraintType::Angle:     desc = "Edit Angle"; break;
        case ConstraintType::FixedAngle:desc = "Edit Fixed Angle"; break;
        default:                        desc = "Edit Constraint"; break;
        }
        pushUndoCommand(sketch::UndoCommand::modifyConstraint(
            oldConstraint, *constraint, desc));
    }

    emit constraintModified(constraintId);
    update();
}

void SketchCanvas::setRedundantCandidates(const QVector<int>& ids)
{
    m_redundantCandidates.clear();
    for (int id : ids) {
        // The solver already refuses to offer a driven constraint: it
        // "never entered the system in the first place, so removing one
        // could not possibly change the result". This filter is therefore a
        // second line rather than the safeguard, and exists because the
        // coloring is what a person acts on: anything that did reach here
        // wrongly would send them to delete the one constraint that cannot
        // be the problem.
        for (const auto& c : m_constraints) {
            if (c.id == id && c.isDriving) {
                m_redundantCandidates.append(id);
                break;
            }
        }
    }
    update();
}

QVector<int> SketchCanvas::findRedundantConstraints() const
{
    QVector<int> out;
    if (!SketchSolver::isAvailable()) {
        return out;
    }
    // The library takes the Qt-free types; convert at the boundary as usual.
    std::vector<sketch::Entity> libEntities(m_entities.begin(), m_entities.end());
    std::vector<sketch::Constraint> libConstraints(m_constraints.begin(),
                                                   m_constraints.end());
    sketch::Solver solver;
    for (int id : solver.findRedundantConstraints(libEntities, libConstraints)) {
        out.append(id);
    }
    return out;
}

void SketchCanvas::solveConstraintsDragging(const std::vector<std::pair<int, int>>& draggedPoints)
{
    m_draggedPoints = draggedPoints;
    solveConstraints();
}

void SketchCanvas::beginHandleDrag(int entityId, int handleIdx, const QPointF& worldPos, Qt::KeyboardModifiers mods)
{
    // Projected geometry is driven by its source; it cannot be edited here.
    // Redirect the user to the source sketch rather than starting a drag.
    if (const SketchEntity* pe = entityById(entityId)) {
        if (pe->projectionSourceId >= 0) {
            emit toolHintChanged(
                tr("This is projected geometry, driven by its source sketch; "
                   "edit it in the source sketch, not here."));
            return;
        }
    }
    if (isEntityLocked(entityId)) {
        emit toolHintChanged(
            tr("This entity is in a locked group; unlock the group to edit it."));
        return;
    }
    if (entityId != m_selectedId) m_selectedId = entityId;
    m_isDraggingHandle = true;
    m_dragHandleIndex = handleIdx;
    m_dragStartWorld = worldPos;
    m_lastRawMouseWorld = worldPos;
    m_shiftWasPressed = (mods & Qt::ShiftModifier);
    m_ctrlWasPressed = (mods & Qt::ControlModifier);
    SketchEntity* sel = entityById(entityId);
    if (sel && handleIdx < sel->points.size()) {
        m_dragHandleOriginal = sel->points[handleIdx];
        if (sel->points.size() > 1) m_dragHandleOriginal2 = sel->points[1];
        m_dragOriginalRadius = sel->radius;
        m_dragOriginalEntity = *sel;
        // Opening a full circle: it was split into one 360-degree arc whose
        // two ends coincide at the cut. Grabbing an end and dragging shrinks
        // it from 360 (see the drag branch + openFullArcByDrag).
        m_openingFullArc = false;
        if (sel->type == SketchEntityType::Arc && sel->points.size() >= 3
            && (handleIdx == 1 || handleIdx == 2)
            && std::abs(std::abs(sel->sweepAngle) - 360.0) < 0.5
            && QLineF(QPointF(sel->points[1]), QPointF(sel->points[2])).length() <= kSnapWeldEps) {
            m_openingFullArc = true;
            m_openArcPrevSweep = sel->sweepAngle;   // +/- 360, seeds continuity
            m_openArcDraggedIndex = handleIdx;
            const auto& c = sel->points[0];
            m_openArcFixedAngle = radiansToDegrees(std::atan2(sel->points[1].y - c.y,
                                             sel->points[1].x - c.x));
        }
        // The solver may move any member of the group (and anything else the
        // constraints reach), so undo restores every member, not just the
        // one whose handle was grabbed.
        m_dragOriginalGroupEntities.clear();
        m_dragOriginalGroupConstraints.clear();
        if (sel->groupId >= 0) {
            for (const auto& e : m_entities)
                if (e.groupId == sel->groupId) m_dragOriginalGroupEntities.append(e);
            for (const auto& g : m_groups) {
                if (g.id != sel->groupId) continue;
                for (int cid : g.constraintIds)
                    if (const SketchConstraint* cc = constraintById(cid))
                        m_dragOriginalGroupConstraints.append(*cc);
                break;
            }
        }
    }
    setCursor(Qt::ArrowCursor);
}

void SketchCanvas::solveConstraints()
{
    // Parameters are the source of truth for any dimension entered as an
    // expression: re-evaluate those against the current parameter values
    // before solving, so a parameter change flows into the geometry (and
    // undoing the parameter flows back) on the next solve.
    if (!m_parameterValues.empty()) {
        for (SketchConstraint& c : m_constraints) {
            sketch::reevaluateConstraint(c, m_parameterValues);
        }
    }

    // Helper: publish the constrained state and repaint if it changed.
    auto publishState = [this](sketch::SketchState state, int dof) {
        // `dof` is only a real count for the solvable states; otherwise it is
        // whatever libslvs happened to leave behind, so it is not published.
        const int reportedDof = sketch::sketchStateHasDof(state) ? dof : -1;
        if (state != m_sketchState || reportedDof != m_sketchDof) {
            m_sketchState = state;
            m_sketchDof = reportedDof;
            m_sketchFullyConstrained = (state == sketch::SketchState::FullyConstrained);
            emit sketchConstraintStateChanged(state, reportedDof);
            update();
        }
    };

    // NOTE: there is deliberately no "no constraints, skip the solve"
    // shortcut here. A sketch with geometry and no constraints is SOLVED
    // (vacuously, there is nothing to violate); it is simply maximally
    // under-constrained, and the solver reports its real degree-of-freedom
    // count (4 for a lone line). The old shortcut published -1 for that case,
    // which is the same value used for "the solve failed", so a perfectly
    // healthy unconstrained sketch was indistinguishable from a broken one.

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
    if (!m_draggedPoints.empty()) solver.setDraggedPoints(m_draggedPoints);
#if defined(SLVS_HAS_DRAG_WEIGHTS)
    if (!m_dragWeightPoints.empty()) solver.setPointWeights(m_dragWeightPoints, m_dragWeightStiffness);
#endif
    SolveResult result = solver.solve(m_entities, m_constraints);
    m_draggedPoints.clear();
    m_dragWeightPoints.clear();

    // Under-constrained feedback from solver truth (when libslvs reports it).
    m_freePoints = result.freePoints;
    m_freePointsValid = result.freePointsValid;

    // The solver classifies the system; the canvas does not re-derive it.
    publishState(result.state, result.dof);

    if (result.success) {
        // Mark all driving constraints as satisfied
        for (SketchConstraint& c : m_constraints) {
            if (c.isDriving) {
                c.satisfied = true;
            }
        }

        // The solver only reads back center + radius for circles.
        // Reproject all perimeter points onto the solved circle so
        // handles stay consistent with the geometry.
        for (SketchEntity& ent : m_entities) {
            if (ent.type == SketchEntityType::Circle && ent.points.size() >= 2) {
                QPointF center = ent.points[0];
                double r = ent.radius;
                for (int i = 1; i < static_cast<int>(ent.points.size()); ++i) {
                    QPointF pt(ent.points[i]);
                    QPointF dir = pt - center;
                    double len = geometry::length(dir);
                    if (len > geometry::kZeroEps) {
                        ent.points[i] = center + dir * (r / len);
                    }
                }
            }
        }

        // Associative offsets follow their parent through the solve, the way
        // a slot follows its centerline: re-derive each offset copy from the
        // (now solved) parent geometry.
        updateAssociativeOffsets();
        updateSlotsFromPaths();
        updateProjectedEntities();

        // Update Driven dimension values to reflect actual geometry
        updateDrivenDimensions();

        // Recompute dimension label positions so they track the geometry
        // after the solver has moved entity points.
        updateConstraintLabelPositions();

        update();
    } else {
        // Mark failed constraints visually (drawn in red), no modal dialog.
        // The user can see which constraints are unsatisfied from the color,
        // and can edit or delete them.
        for (SketchConstraint& c : m_constraints) {
            c.satisfied = std::find(result.failedConstraintIds.begin(), result.failedConstraintIds.end(), c.id) == result.failedConstraintIds.end();
        }

        qWarning("Solver: %s (DOF %d, %d failed constraint(s))",
                 result.errorMessage.c_str(), result.dof,
                 static_cast<int>(result.failedConstraintIds.size()));

        update();
    }
}

// =====================================================================
//  Staged placement constraints
//
//  Apply the locked dimension values for one tool/mode at the current stage,
//  adjusting `snapped` in place.
//
//  These are each called from BOTH mousePressEvent and mouseReleaseEvent, and
//  BOTH call sites are required. A staged mode accepts two input styles:
//  clicking each point, and press-drag-release to drag THROUGH a stage
//  (detected per stage by m_wasDragged at a 5 px threshold). Press places a
//  point on click; release places the next one if the user dragged. Deleting
//  either call site silently removes one input style; the other keeps
//  working, so it looks correct in casual testing.
//
//  Keep these mode-scoped: they lift directly into per-tool handler classes
//  when the tool dispatch is refactored.
// =====================================================================

// =====================================================================
//  Tool handler registry
//
//  A tool with no handler falls through to the existing switch statements and
//  behaves exactly as before; that is what keeps the migration incremental.
// =====================================================================

void SketchCanvas::beginDragDetection(const QPoint& screenPos)
{
    m_drawStartPos = screenPos;
    m_wasDragged = false;
}

void SketchCanvas::clearConstraintTargets()
{
    m_constraintTargetEntities.clear();
    m_constraintTargetPoints.clear();
    m_dimensionReadyToPlace = false;
}

bool SketchCanvas::beginSingleEntityDimension(int entityId)
{
    const SketchEntity* entity = entityById(entityId);
    if (!entity) {
        return false;
    }
    if (entity->type == SketchEntityType::Line && entity->points.size() == 2) {
        setConstraintTargetsForLine(entityId, entity->points[0], entity->points[1]);
        m_pendingConstraintType = ConstraintType::Distance;
        m_dimensionReadyToPlace = true;
        return true;
    }
    if ((entity->type == SketchEntityType::Circle
         || entity->type == SketchEntityType::Arc)
        && !entity->points.empty()) {
        setConstraintTargetsForRadial(entityId, entity->points[0]);
        m_pendingConstraintType = ConstraintType::Radius;
        m_dimensionReadyToPlace = true;
        return true;
    }
    return false;
}

void SketchCanvas::addConstraintTarget(int entityId, const QPointF& worldPos)
{
    const SketchEntity* entity = entityById(entityId);
    m_constraintTargetEntities.append(entityId);
    m_constraintTargetPoints.append(findClosestPointOnEntity(entity, worldPos));

    if (m_constraintTargetEntities.size() == 2) {
        m_pendingConstraintType = detectConstraintType(m_constraintTargetEntities[0],
                                                       m_constraintTargetEntities[1]);
        m_dimensionReadyToPlace = true;
    }
}

void SketchCanvas::placeDimensionLabel(const QPointF& labelPos)
{
    // Seed the dimension with whatever the geometry measures right now, then
    // open it for editing. This was written out twice (once for the
    // single-entity shortcut and once for the three-click path), which is
    // why it lives here rather than in the tool handler.
    std::vector<const sketch::Entity*> targetEntities;
    targetEntities.reserve(m_constraintTargetEntities.size());
    for (int eid : m_constraintTargetEntities) {
        targetEntities.push_back(entityById(eid));
    }
    const double initialValue =
        sketch::calculateConstraintValue(m_pendingConstraintType, targetEntities);

    // Honor the pre-placement choice of driving vs driven (reference). A
    // driven dimension only measures, so it is not opened for editing.
    const bool driving = m_pendingDimensionDriven;
    createConstraint(m_pendingConstraintType, initialValue, labelPos,
                     /*skipOverConstrainCheck=*/false,
                     /*startEditing=*/driving, /*driving=*/driving);
    m_pendingDimensionDriven = true;   // reset to the default for the next one
    clearConstraintTargets();
}

QPointF SketchCanvas::rawMouseWorld() const
{
    return screenToWorld(mapFromGlobal(QCursor::pos()));
}

bool SketchCanvas::allDimFieldsLocked() const
{
    return m_dimInput.allLocked();
}

void SketchCanvas::appendPlacementPoint(const QPointF& worldPos)
{
    if (m_snapEngine.hasActiveSnap()) {
        m_placedSnaps.append(
            {static_cast<int>(m_pendingEntity.points.size()), *m_snapEngine.activeSnap()});
    }
    m_pendingEntity.points.push_back(worldPos);
    m_previewPoints.append(worldPos);
}

SketchToolHandler* SketchCanvas::handlerFor(SketchTool tool) const
{
    // Draw-then-constrain first, when that is the mode. A tool without one
    // falls back to its placement-first handler, so the mode works from the
    // first tool that has a variant rather than needing all of them.
    if (m_interactionMode == InteractionMode::DrawThenConstrain) {
        for (const auto& h : m_drawConstrainHandlers) {
            if (h->tool() == tool) return h.get();
        }
    }
    for (const auto& h : m_toolHandlers) {
        if (h->tool() == tool) return h.get();
    }
    return nullptr;
}

bool SketchCanvas::setInteractionMode(InteractionMode mode)
{
    if (mode == m_interactionMode) return true;
    // Mid-entity the two modes disagree about what the clicks already made
    // meant, so there is no correct way to reinterpret them.
    if (m_isDrawing) return false;

    m_interactionMode = mode;
    clearDimFields();
    initDimFields();
    update();
    return true;
}

void SketchCanvas::addDimField(const QString& label, bool isAngle)
{
    m_dimInput.addField(label, isAngle);
}

QString SketchCanvas::currentToolHint() const
{
    const SketchToolHandler* h = activeHandler();
    return h ? h->hint(*this) : QString();
}

void SketchCanvas::setSelectedConstraint(int constraintId)
{
    for (auto& c : m_constraints) {
        c.selected = (c.id == constraintId);
    }
    m_selectedConstraintId = constraintId;

    emit selectionChanged(-1);   // constraint selection clears entity selection
    emit constraintSelectionChanged(constraintId);
    update();
}

void SketchCanvas::deleteConstraintById(int constraintId)
{
    // Find the constraint before removing it (for undo)
    const SketchConstraint* found = nullptr;
    for (const auto& c : m_constraints) {
        if (c.id == constraintId) { found = &c; break; }
    }
    if (!found) return;

    // Check if this constraint belongs to a sweep-angle group
    // If so, also delete the construction line entities and clean up the group
    int sweepGroupId = -1;
    for (const auto& g : m_groups) {
        if (isSweepAngleGroup(g.id) && g.containsConstraint(constraintId)) {
            sweepGroupId = g.id;
            break;
        }
    }

    if (sweepGroupId >= 0) {
        // Build a compound undo for the whole sweep-angle group removal
        std::vector<sketch::UndoCommand> subs;

        // Push undo for constraint deletion
        subs.push_back(sketch::UndoCommand::deleteConstraint(*found));

        // Find and delete construction line entities in the group
        QSet<int> linesToDelete;
        for (const auto& g : m_groups) {
            if (g.id == sweepGroupId) {
                for (int eid : g.entityIds) {
                    SketchEntity* e = entityById(eid);
                    if (e && e->isConstruction && e->type == SketchEntityType::Line) {
                        subs.push_back(sketch::UndoCommand::deleteEntity(*e));
                        linesToDelete.insert(eid);
                    }
                }
                subs.push_back(sketch::UndoCommand::deleteGroup(g));
                break;
            }
        }

        pushUndoCommand(sketch::UndoCommand::compound(subs, "Delete Sweep Angle"));

        // Remove constraint
        m_constraints.erase(
            std::remove_if(m_constraints.begin(), m_constraints.end(),
                           [constraintId](const SketchConstraint& c) { return c.id == constraintId; }),
            m_constraints.end());

        // Remove construction line entities
        m_entities.erase(
            std::remove_if(m_entities.begin(), m_entities.end(),
                           [&linesToDelete](const SketchEntity& e) { return linesToDelete.contains(e.id); }),
            m_entities.end());

        // Remove the group
        m_groups.erase(
            std::remove_if(m_groups.begin(), m_groups.end(),
                           [sweepGroupId](const SketchGroup& g) { return g.id == sweepGroupId; }),
            m_groups.end());
    } else {
        // Simple constraint deletion: push undo, then remove
        pushUndoCommand(sketch::UndoCommand::deleteConstraint(*found));

        m_constraints.erase(
            std::remove_if(m_constraints.begin(), m_constraints.end(),
                           [constraintId](const SketchConstraint& c) { return c.id == constraintId; }),
            m_constraints.end());
    }

    if (m_selectedConstraintId == constraintId) {
        m_selectedConstraintId = -1;
    }

    refreshConstrainedFlags();
    solveConstraints();
    m_profilesCacheDirty = true;
    emit constraintDeleted(constraintId);
    update();
}

void SketchCanvas::refreshConstrainedFlags()
{
    // Delegate to library: computes which entities have driving constraints
    std::unordered_set<int> ids = sketch::getConstrainedEntityIds(
        toLibraryConstraints(m_constraints));
    for (SketchEntity& e : m_entities) {
        e.constrained = ids.count(e.id) > 0;
    }
}

void SketchCanvas::updateDrivenDimensions()
{
    auto findEntity = [this](int id) -> const sketch::Entity* { return entityById(id); };
    for (SketchConstraint& c : m_constraints) {
        if (c.isDriving) continue;
        c.value = sketch::computeDrivenValue(c, findEntity);
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
                double len = geometry::length(dir);
                if (len > geometry::kZeroEps) {
                    // Perpendicular offset (10 world units)
                    QPointF perp(-dir.y() / len, dir.x() / len);
                    c.labelPosition = mid + perp * 10.0;
                } else {
                    c.labelPosition = mid + QPointF(0, -10);
                }
            }
        } else if (c.type == ConstraintType::Radius ||
                   c.type == ConstraintType::Diameter) {
            // Recompute label along the arc's midpoint direction so it stays
            // visually stable when the arc slides along a tangent entity.
            if (!c.entityIds.empty()) {
                const SketchEntity* ent = entityById(c.entityIds[0]);
                if (ent && !ent->points.empty()) {
                    QPointF center = ent->points[0];
                    if (ent->type == SketchEntityType::Arc && ent->points.size() >= 3) {
                        // Position at half-radius along the arc's midpoint angle
                        double midAngleRad = qDegreesToRadians(
                            ent->startAngle + ent->sweepAngle / 2.0);
                        double labelDist = ent->radius / 2.0;
                        c.labelPosition = {
                            center.x() + labelDist * std::cos(midAngleRad),
                            center.y() + labelDist * std::sin(midAngleRad)};
                    } else if (ent->type == SketchEntityType::Circle) {
                        // If a stored label angle exists, reposition the label
                        // at that angle (preserving distance from center).
                        // This keeps the label stable through solver runs.
                        if (!std::isnan(c.labelAngle)) {
                            QPointF oldLabel = c.labelPosition;
                            QPointF offset = oldLabel - center;
                            double labelDist = geometry::length(offset);
                            if (labelDist < geometry::kDegenerateLen) labelDist = ent->radius / 2.0;
                            c.labelPosition = {
                                center.x() + labelDist * std::cos(c.labelAngle),
                                center.y() + labelDist * std::sin(c.labelAngle)};
                        }
                        // else: no stored angle; label stays where it is
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
    Point2D lp1, lp2;
    bool ok = sketch::getConstraintEndpoints(
        constraint,
        [this](int id) -> const sketch::Entity* { return entityById(id); },
        lp1, lp2);
    if (ok) {
        p1 = QPointF(lp1.x, lp1.y);
        p2 = QPointF(lp2.x, lp2.y);
    }
    return ok;
}

// ---- Constraint search helper ----

SketchConstraint* SketchCanvas::findDrivingConstraint(int entityId, ConstraintType type)
{
    for (SketchConstraint& c : m_constraints) {
        if (c.type == type && c.isDriving && c.enabled) {
            for (int eid : c.entityIds) {
                if (eid == entityId) return &c;
            }
        }
    }
    return nullptr;
}

const SketchConstraint* SketchCanvas::findDrivingConstraint(int entityId, ConstraintType type) const
{
    for (const SketchConstraint& c : m_constraints) {
        if (c.type == type && c.isDriving && c.enabled) {
            for (int eid : c.entityIds) {
                if (eid == entityId) return &c;
            }
        }
    }
    return nullptr;
}

// ---- Constraint target setup helpers ----

void SketchCanvas::setConstraintTargetsForLine(int entityId, const QPointF& p1, const QPointF& p2)
{
    m_constraintTargetEntities.clear();
    m_constraintTargetPoints.clear();
    m_constraintTargetEntities.append(entityId);
    m_constraintTargetEntities.append(entityId);
    m_constraintTargetPoints.append(p1);
    m_constraintTargetPoints.append(p2);
}

void SketchCanvas::setConstraintTargetsForRadial(int entityId, const QPointF& center)
{
    m_constraintTargetEntities.clear();
    m_constraintTargetPoints.clear();
    m_constraintTargetEntities.append(entityId);
    m_constraintTargetEntities.append(entityId);
    m_constraintTargetPoints.append(center);
    m_constraintTargetPoints.append(center);
}

// ---- Geometric Constraint Application ----

// ---- Selection bookkeeping ------------------------------------------

void SketchCanvas::selectAdd(int entityId)
{
    if (entityId < 0 || m_selectedIds.contains(entityId)) {
        return;
    }
    m_selectedIds.insert(entityId);      // NOLINT: the helper owns the set
    m_selectionOrder.append(entityId);
}

void SketchCanvas::selectRemove(int entityId)
{
    m_selectedIds.remove(entityId);      // NOLINT: the helper owns the set
    m_selectionOrder.removeAll(entityId);
}

void SketchCanvas::selectClear()
{
    m_selectedIds.clear();               // NOLINT: the helper owns the set
    m_selectionOrder.clear();
}

std::vector<int> SketchCanvas::selectedEntityList() const
{
    // m_selectionOrder is authoritative for order; fall back to the single
    // selection so a plain click still works where nothing multi-selected.
    if (!m_selectionOrder.isEmpty()) {
        return std::vector<int>(m_selectionOrder.begin(), m_selectionOrder.end());
    }
    if (m_selectedId >= 0) {
        return {m_selectedId};
    }
    return {};
}

/// Comma-separated constraint names, for telling the user what *would* work.
static QString constraintListText(const std::vector<ConstraintType>& types)
{
    QStringList names;
    for (ConstraintType t : types) {
        if (sketch::isGeometricConstraint(t)) {
            names << QString::fromUtf8(sketch::constraintTypeName(t));
        }
    }
    return names.isEmpty() ? SketchCanvas::tr("(none)") : names.join(QStringLiteral(", "));
}

// ---- Constraints from the current selection -------------------------


bool SketchCanvas::selectionHasFixedPoint() const
{
    for (int id : selectedEntityList())
        for (const SketchConstraint& c : m_constraints)
            if (c.type == ConstraintType::FixedPoint && !c.entityIds.empty()
                && c.entityIds[0] == id)
                return true;
    return false;
}

void SketchCanvas::fixSelectedEntities()
{
    const std::vector<int> ids = selectedEntityList();
    if (ids.empty()) return;
    std::vector<sketch::UndoCommand> subs;
    if (selectionHasFixedPoint()) {
        // Unfix: remove every FixedPoint on the selected entities.
        QSet<int> sel; for (int id : ids) sel.insert(id);
        QVector<SketchConstraint> keep; keep.reserve(m_constraints.size());
        for (const SketchConstraint& c : m_constraints) {
            if (c.type == ConstraintType::FixedPoint && !c.entityIds.empty()
                && sel.contains(c.entityIds[0])) {   // any point of a selected entity
                subs.push_back(sketch::UndoCommand::deleteConstraint(c, "Unfix"));
            } else {
                keep.push_back(c);
            }
        }
        m_constraints = keep;
    } else {
        // Fix: pin every point of every selected entity that is not already
        // pinned. libslvs takes one FixedPoint per point.
        for (int id : ids) {
            const SketchEntity* e = entityById(id);
            if (!e) continue;
            for (int pi = 0; pi < e->points.size(); ++pi) {
                bool already = false;
                for (const SketchConstraint& c : m_constraints)
                    if (sketch::isFixedPointOn(c, id, pi)) { already = true; break; }
                if (already) continue;
                const SketchConstraint fp(sketch::makeFixedPoint(m_nextConstraintId++, id, pi));
                m_constraints.append(fp);
                subs.push_back(sketch::UndoCommand::addConstraint(fp, "Fix"));
            }
        }
    }
    if (subs.size() == 1) pushUndoCommand(subs.front());
    else if (!subs.empty()) pushUndoCommand(sketch::UndoCommand::compound(subs, "Fix"));
    m_profilesCacheDirty = true;
    solveConstraints();
    emit constraintModified(-1);
    update();
}

bool SketchCanvas::applyConstraintToSelection(ConstraintType type)
{
    // Tangent-angle DIMENSION on a single Bezier spline anchor.
    if (type == ConstraintType::TangentAngle) {
        if (m_selectedPoints.size() != 1) {
            showStatus(tr("Select one Bezier spline anchor point, then apply Tangent Angle."));
            return false;
        }
        const auto sp = m_selectedPoints[0];
        SketchEntity* e = entityById(sp.first);
        if (!e || e->type != SketchEntityType::Spline || !e->splineBezier) {
            showStatus(tr("Tangent Angle applies to a Bezier spline anchor."));
            return false;
        }
        const int a = sp.second;
        const int n = static_cast<int>(e->points.size());
        if (a < 0 || a >= n || a % 3 != 0) {
            showStatus(tr("Select an ANCHOR point (every third control point), not a tangent handle."));
            return false;
        }
        const int lastCp = n - 1;
        const QPointF anchor(e->points[a]);
        const QPointF fwd = (a < lastCp) ? (QPointF(e->points[a+1]) - anchor)
                                         : (anchor - QPointF(e->points[a-1]));
        double cur = geometry::vectorAngle(fwd);
        if (cur < 0) cur += 360.0;
        bool ok = false;
        const double ang = QInputDialog::getDouble(this, tr("Tangent Angle"),
            tr("Directed tangent angle (0-360 deg):"), cur, 0.0, 360.0, 2, &ok);
        if (!ok) return false;
        const double rad = qDegreesToRadians(ang);
        const QPointF u(std::cos(rad), std::sin(rad));
        if (a + 1 < n) {
            const double L = QLineF(anchor, QPointF(e->points[a+1])).length();
            e->points[a+1] = anchor + u * L;
        }
        if (a - 1 >= 0) {
            const double L = QLineF(anchor, QPointF(e->points[a-1])).length();
            e->points[a-1] = anchor - u * L;
        }
        m_constraintTargetEntities.clear(); m_constraintTargetPoints.clear();
        m_constraintTargetEntities.append(sp.first);
        m_constraintTargetPoints.append(anchor);
        createConstraint(ConstraintType::TangentAngle, ang, anchor + QPointF(0, -12),
                         false, false);
        m_constraintTargetEntities.clear(); m_constraintTargetPoints.clear();
        m_selectedPoints.clear();
        return true;
    }

    // A point together with a circle or arc: Coincident means the point lies on
    // the PERIMETER (a Point-On-Circle constraint), NOT the center; this is the
    // Fusion/SolidWorks convention. To coincide a point with the CENTER, select
    // the center POINT itself and use a point-to-point coincidence. Handles both
    // selection styles: a picked point (an endpoint or the tangent-contact dot's
    // circle, Ctrl-clicked with a line endpoint) and a standalone Point entity.
    // Also the explicit Point-On-Circle constraint routes here. (Aaron)
    if (type == ConstraintType::Coincident || type == ConstraintType::PointOnCircle) {
        int circleId = -1;
        for (int id : selectedEntityList()) {
            const SketchEntity* e = entityById(id);
            if (e && (e->type == SketchEntityType::Circle
                      || e->type == SketchEntityType::Arc)) { circleId = id; break; }
        }
        int ptEntity = -1, ptIndex = -1;
        if (m_selectedPoints.size() == 1) {
            ptEntity = m_selectedPoints[0].first;
            ptIndex  = m_selectedPoints[0].second;
        } else if (m_selectedPoints.isEmpty()) {
            for (int id : selectedEntityList()) {
                const SketchEntity* e = entityById(id);
                if (e && e->type == SketchEntityType::Point) { ptEntity = id; ptIndex = 0; break; }
            }
        }
        if (circleId >= 0 && ptEntity >= 0 && ptEntity != circleId) {
            const SketchEntity* pe = entityById(ptEntity);
            const SketchEntity* ce = entityById(circleId);
            if (pe && ce && ptIndex >= 0 && ptIndex < static_cast<int>(pe->points.size())
                && !ce->points.empty()) {
                const QPointF pPos(pe->points[ptIndex]);
                m_constraintTargetEntities.clear();
                m_constraintTargetPoints.clear();
                m_constraintTargetEntities.append(ptEntity);
                m_constraintTargetPoints.append(pPos);
                m_constraintTargetEntities.append(circleId);
                m_constraintTargetPoints.append(QPointF(ce->points[0]));
                createConstraint(ConstraintType::PointOnCircle, 0.0,
                                 pPos + QPointF(0, -10),
                                 /*skipOverConstrainCheck=*/false,
                                 /*startEditing=*/false);
                m_constraintTargetEntities.clear();
                m_constraintTargetPoints.clear();
                m_selectedPoints.clear();
                return true;
            }
        }
    }

    // A selected midpoint grip (line/arc) plus one picked point: pin the point
    // to that midpoint. Lines use the Midpoint constraint (SLVS_C_AT_MIDPOINT);
    // arcs need the arc-aware midpoint constraint (patch 0019, pending); say so
    // rather than create a wrong line-style midpoint on an arc. (Aaron)
    if ((type == ConstraintType::Coincident || type == ConstraintType::Midpoint)
        && m_selectedMidpointEntity >= 0 && m_selectedPoints.size() == 1) {
        const SketchEntity* me = entityById(m_selectedMidpointEntity);
        const auto psel = m_selectedPoints[0];
        const SketchEntity* pe = entityById(psel.first);
        if (me && pe && psel.second < static_cast<int>(pe->points.size())
            && (me->type == SketchEntityType::Line
                || me->type == SketchEntityType::Arc)) {
            // Both use ConstraintType::Midpoint; the solver wrapper routes a LINE
            // to SLVS_C_AT_MIDPOINT and an ARC to SLVS_C_ARC_MIDPOINT (0019).
            const QPointF pPos(pe->points[psel.second]);
            QPointF mid; entityMidpoint(*me, mid);
            m_constraintTargetEntities.clear();
            m_constraintTargetPoints.clear();
            m_constraintTargetEntities.append(psel.first);
            m_constraintTargetPoints.append(pPos);
            m_constraintTargetEntities.append(m_selectedMidpointEntity);
            m_constraintTargetPoints.append(mid);
            createConstraint(ConstraintType::Midpoint, 0.0, mid + QPointF(0, -10),
                             /*skipOverConstrainCheck=*/false, /*startEditing=*/false);
            m_constraintTargetEntities.clear();
            m_constraintTargetPoints.clear();
            m_selectedMidpointEntity = -1;
            m_selectedPoints.clear();
            return true;
        }
    }

    // Two individually selected points -> a point-to-point constraint on those
    // exact endpoints (the "select points, then constrain" flow).
    if (m_selectedPoints.size() >= 2) {
        const auto a = m_selectedPoints[0];
        const auto b = m_selectedPoints[1];
        const SketchEntity* ea = entityById(a.first);
        const SketchEntity* eb = entityById(b.first);
        if (!ea || !eb || a.second >= ea->points.size() || b.second >= eb->points.size())
            return false;
        const QPointF pa(ea->points[a.second]);
        const QPointF pb(eb->points[b.second]);
        m_constraintTargetEntities.clear();
        m_constraintTargetPoints.clear();
        m_constraintTargetEntities.append(a.first); m_constraintTargetPoints.append(pa);
        m_constraintTargetEntities.append(b.first); m_constraintTargetPoints.append(pb);
        const QPointF labelPos = (pa + pb) / 2.0 + QPointF(0, -10);
        createConstraint(type, 0.0, labelPos, /*skipOverConstrainCheck=*/false, /*startEditing=*/false);
        m_constraintTargetEntities.clear();
        m_constraintTargetPoints.clear();
        m_selectedPoints.clear();   // consumed
        return true;
    }

    const std::vector<int> ids = selectedEntityList();
    const int needed = sketch::requiredEntityCount(type);
    const QString name = QString::fromUtf8(sketch::constraintTypeName(type));

    if (static_cast<int>(ids.size()) < needed) {
        showStatus(tr("%1 needs %n entity(s); %2 selected. Select them "
                      "(Ctrl to add), then apply.", "", needed)
                       .arg(name).arg(ids.size()));
        return false;
    }

    // Take the first `needed` in selection order: for Midpoint and Symmetric
    // the roles are positional, so the order the user clicked is the answer.
    std::vector<int> chosen(ids.begin(), ids.begin() + needed);

    // Check the library actually considers this combination meaningful,
    // rather than handing the solver something it will reject or, worse,
    // satisfy in a way the user did not intend.
    if (needed == 2) {
        const SketchEntity* a = entityById(chosen[0]);
        const SketchEntity* b = entityById(chosen[1]);
        if (a && b) {
            const std::vector<ConstraintType> ok =
                sketch::suggestConstraints(*a, *b);
            if (std::find(ok.begin(), ok.end(), type) == ok.end()) {
                showStatus(tr("%1 does not apply to those two entities. "
                              "Applicable here: %2")
                               .arg(name).arg(constraintListText(ok)));
                return false;
            }
        }
    }

    m_constraintTargetEntities.clear();
    for (int id : chosen) {
        m_constraintTargetEntities.append(id);
    }
    createGeometricConstraint(type);
    m_constraintTargetEntities.clear();
    return true;
}

void SketchCanvas::applyInferredConstraint()
{
    // Two selected points infer a Coincident (the point-to-point constraint).
    if (m_selectedPoints.size() >= 2) {
        applyConstraintToSelection(ConstraintType::Coincident);
        return;
    }
    const std::vector<int> ids = selectedEntityList();
    if (ids.empty()) {
        QMessageBox::information(this, tr("Constrain"),
            tr("Select one or two entities first, then apply a constraint."));
        return;
    }

    std::vector<ConstraintType> options;
    if (ids.size() >= 2) {
        const SketchEntity* a = entityById(ids[0]);
        const SketchEntity* b = entityById(ids[1]);
        if (a && b) {
            options = sketch::suggestConstraints(*a, *b);
        }
    } else if (const SketchEntity* only = entityById(ids[0])) {
        options = sketch::suggestConstraints(*only);
    }

    // Only geometric ones: a dimensional constraint needs a value, which is
    // the Dimension tool's job rather than this one's.
    options.erase(std::remove_if(options.begin(), options.end(),
                                 [](ConstraintType t) {
                                     return !sketch::isGeometricConstraint(t);
                                 }),
                  options.end());

    if (options.empty()) {
        QMessageBox::information(this, tr("Constrain"),
            tr("No geometric constraint applies to that selection."));
        return;
    }
    if (options.size() == 1) {
        applyConstraintToSelection(options.front());
        return;
    }

    // More than one is possible, so ask rather than guess. Guessing here is
    // what makes a constraint tool feel unpredictable.
    QMenu menu(this);
    for (ConstraintType t : options) {
        QAction* act = menu.addAction(
            QString::fromUtf8(sketch::constraintTypeName(t)));
        act->setData(static_cast<int>(t));
    }
    if (QAction* picked = menu.exec(QCursor::pos())) {
        applyConstraintToSelection(
            static_cast<ConstraintType>(picked->data().toInt()));
    }
}

void SketchCanvas::createGeometricConstraint(ConstraintType type)
{
    SketchConstraint constraint;
    constraint.id = m_nextConstraintId++;
    constraint.type = type;
    constraint.entityIds = std::vector<int>(m_constraintTargetEntities.begin(), m_constraintTargetEntities.end());
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
    applyConstraintToSelection(ConstraintType::Parallel);
}

void SketchCanvas::applyPerpendicularConstraint()
{
    applyConstraintToSelection(ConstraintType::Perpendicular);
}

void SketchCanvas::applyCoincidentConstraint()
{
    applyConstraintToSelection(ConstraintType::Coincident);
}

void SketchCanvas::applyTangentConstraint()
{
    applyConstraintToSelection(ConstraintType::Tangent);
}

void SketchCanvas::applyEqualConstraint()
{
    applyConstraintToSelection(ConstraintType::Equal);
}

void SketchCanvas::applyMidpointConstraint()
{
    applyConstraintToSelection(ConstraintType::Midpoint);
}

void SketchCanvas::applySymmetricConstraint()
{
    applyConstraintToSelection(ConstraintType::Symmetric);
}

void SketchCanvas::applyConcentricConstraint()
{
    applyConstraintToSelection(ConstraintType::Concentric);
}

void SketchCanvas::applyCollinearConstraint()
{
    applyConstraintToSelection(ConstraintType::Collinear);
}

void SketchCanvas::applyFixConstraint()
{
    fixSelectedEntities();
}

void SketchCanvas::autoConstrainSketch()
{
    std::vector<sketch::Entity> es = toLibraryEntities(m_entities);
    std::vector<sketch::Constraint> cs = toLibraryConstraints(m_constraints);
    int nextId = m_nextConstraintId;
    std::vector<sketch::Constraint> added = sketch::autoConstrain(es, cs, nextId);
    if (added.empty()) {
        emit toolHintChanged(tr("Auto Constrain: nothing to add."));
        return;
    }
    std::vector<sketch::UndoCommand> subs;
    for (const auto& c : added) {
        SketchConstraint gc(c);
        m_constraints.append(gc);
        subs.push_back(sketch::UndoCommand::addConstraint(gc));
        for (int eid : c.entityIds)
            if (SketchEntity* e = entityById(eid)) e->constrained = true;
    }
    m_nextConstraintId = nextId;
    pushUndoCommand(sketch::UndoCommand::compound(subs, "Auto Constrain"));
    m_profilesCacheDirty = true;
    solveConstraints();
    emit selectionChanged(m_selectedId);
    emit toolHintChanged(tr("Auto Constrain: added %1 constraint(s).").arg(added.size()));
    update();
}

// ============================================================================
// Trim / Extend / Split Operations
// ============================================================================

QVector<SketchCanvas::Intersection> SketchCanvas::findAllIntersections() const
{
    // Use library implementation via conversion helpers
    std::vector<sketch::Entity> libEntities = hobbycad::toLibraryEntities(m_entities);
    std::vector<sketch::Intersection> libIntersections = sketch::findAllIntersections(libEntities);
    return hobbycad::toGuiIntersections(libIntersections);
}

bool SketchCanvas::trimEntityAt(int entityId, const QPointF& clickPoint,
                                bool deleteIfNoIntersection)
{
    SketchEntity* entity = entityById(entityId);
    if (!entity) return false;
    const SketchEntity original = *entity;   // copy for undo before any change

    // Find the intersection points that lie on this entity.
    std::vector<sketch::Entity> libEntities = hobbycad::toLibraryEntities(m_entities);
    sketch::Entity libEntity = hobbycad::toLibraryEntity(*entity);
    std::vector<sketch::Intersection> allIntersections = sketch::findAllIntersections(libEntities);
    std::vector<Point2D> intersectionPoints =
        sketch::intersectionPointsTouching(allIntersections, entityId);

    // Gather the constraints that named the original: their removal is recorded
    // here, and the carryable ones are re-anchored onto the pieces below so a
    // cut re-homes what it can rather than dropping every one.
    std::vector<sketch::UndoCommand> subs;
    std::vector<sketch::Constraint> origRefs;
    for (const SketchConstraint& c : m_constraints) {
        for (int eid : c.entityIds) {
            if (eid == entityId) {
                subs.push_back(sketch::UndoCommand::deleteConstraint(c, "Trim"));
                origRefs.push_back(c);
                break;
            }
        }
    }

    std::vector<SketchEntity> pieces;
    if (intersectionPoints.empty()) {
        // Fusion: with no intersection to trim to, Trim deletes the geometry.
        // (No new pieces; the entity simply goes away.) A drag-through pass
        // asks NOT to delete here, so a mere brush across an unbounded curve
        // does not wipe it; only a deliberate click does.
        if (!deleteIfNoIntersection) return false;
    } else {
        const Point2D clickPt{clickPoint.x(), clickPoint.y()};
        const sketch::TrimResult result = sketch::trimEntity(
            libEntity, intersectionPoints, clickPt, [this]() { return m_nextId++; });
        if (!result.success) return false;   // could not resolve a segment: leave it be
        for (const sketch::Entity& ne : result.newEntities)
            pieces.push_back(hobbycad::toGuiEntity(ne));
    }

    // One compound: drop the referencing constraints, remove the original,
    // add whatever pieces remain. Push before mutating, as elsewhere.
    subs.push_back(sketch::UndoCommand::deleteEntity(original, "Trim"));
    for (const SketchEntity& piece : pieces)
        subs.push_back(sketch::UndoCommand::addEntity(piece, "Trim"));

    // Tie each trimmed endpoint onto the cutting edge (point-on-object). Same
    // library rule the CLI will use; boundaries are every other entity.
    {
        std::vector<sketch::Entity> pieceLib, others;
        for (const SketchEntity& p : pieces) pieceLib.push_back(hobbycad::toLibraryEntity(p));
        for (const SketchEntity& e : m_entities)
            if (e.id != entityId) others.push_back(hobbycad::toLibraryEntity(e));
        recordAddedConstraints(sketch::computeCutConstraints(
            pieceLib, others, intersectionPoints,
            [this]() { return m_nextConstraintId++; }), subs);
        // Carry the original's own constraints onto the surviving piece(s) where
        // they still hold, instead of dropping every constraint that named it.
        recordAddedConstraints(sketch::remapCutConstraints(
            origRefs, hobbycad::toLibraryEntity(original), pieceLib,
            [this]() { return m_nextConstraintId++; }), subs);
    }

    pushCompoundOrSingle(subs, "Trim");

    // Apply: remove the original entity and its referencing constraints.
    replaceEntityWithPieces(entityId, pieces);
    return true;
}

bool SketchCanvas::extendEntityTo(int entityId, const QPointF& clickPoint)
{
    SketchEntity* entity = entityById(entityId);
    if (!entity) return false;

    // Build boundary list, excluding self and construction entities
    std::vector<sketch::Entity> boundaries;
    for (const SketchEntity& other : m_entities) {
        if (other.id == entityId || other.isConstruction) continue;
        boundaries.push_back(toLibraryEntity(other));
    }

    auto result = sketch::extendEntity(
        toLibraryEntity(*entity), boundaries, /*extendEnd=*/-1, clickPoint);

    // Fail-safe (Fusion F-64): with no boundary in the extension direction,
    // there is nothing to extend to, so leave the geometry untouched. The
    // caller reports the quiet no-op; no dialog interrupts the flow.
    if (!result.success) return false;

    const SketchEntity before = *entity;
    entity->points = result.entity.points;

    // Extend is undoable: record the point change, and tie the newly-extended
    // endpoint onto the boundary curve it now meets (point-on-object) via the
    // shared library rule.
    std::vector<sketch::UndoCommand> subs;
    subs.push_back(sketch::UndoCommand::modifyEntity(before, *entity, "Extend"));
    {
        std::vector<sketch::Entity> pieceLib{ hobbycad::toLibraryEntity(*entity) }, others;
        for (const SketchEntity& e : m_entities)
            if (e.id != entityId) others.push_back(hobbycad::toLibraryEntity(e));
        std::vector<Point2D> junc;
        for (int i = 0; i < static_cast<int>(entity->points.size()); ++i) {
            const QPointF pnow(entity->points[i]);
            if (i < static_cast<int>(before.points.size())
                && QLineF(pnow, QPointF(before.points[i])).length() <= kSnapWeldEps)
                continue;   // this endpoint did not move
            junc.push_back({ pnow.x(), pnow.y() });
        }
        recordAddedConstraints(sketch::computeCutConstraints(
            pieceLib, others, junc, [this]() { return m_nextConstraintId++; }), subs);
    }
    pushCompoundOrSingle(subs, "Extend");
    m_profilesCacheDirty = true;
    emit entityModified(entityId);
    solveConstraints();
    update();
    return true;
}

void SketchCanvas::recordAddedConstraints(
        const std::vector<sketch::Constraint>& cs,
        std::vector<sketch::UndoCommand>& subs)
{
    for (const sketch::Constraint& c : cs) {
        SketchConstraint sc(c);
        subs.push_back(sketch::UndoCommand::addConstraint(sc));
        m_constraints.append(sc);
        for (int eid : sc.entityIds)
            if (SketchEntity* e = entityById(eid)) e->constrained = true;
    }
}

QVector<int> SketchCanvas::applySplitPieces(
        int entityId, const std::vector<sketch::Entity>& newLibEntities,
        const QVector<QPointF>& junctionPoints, const QString& desc)
{
    QVector<int> newIds;
    const SketchEntity* origPtr = entityById(entityId);
    if (!origPtr) return newIds;
    const SketchEntity original = *origPtr;      // copy for undo before any change
    const std::string tag = desc.toStdString();

    std::vector<SketchEntity> pieces;
    pieces.reserve(newLibEntities.size());
    for (const sketch::Entity& ne : newLibEntities) {
        pieces.push_back(hobbycad::toGuiEntity(ne));
        newIds.append(pieces.back().id);
    }

    // One compound so the whole split is a single undo step (previously split
    // mutated the model directly and could not be undone at all): record the
    // original's referencing constraints for removal (the carryable ones are
    // re-anchored onto the pieces below), remove it, add the pieces, then join
    // the pieces at each split point so the halves stay connected rather than
    // drifting apart.
    std::vector<sketch::UndoCommand> subs;
    std::vector<sketch::Constraint> origRefs;
    for (const SketchConstraint& c : m_constraints) {
        for (int eid : c.entityIds) {
            if (eid == entityId) {
                subs.push_back(sketch::UndoCommand::deleteConstraint(c, tag));
                origRefs.push_back(c);
                break;
            }
        }
    }
    subs.push_back(sketch::UndoCommand::deleteEntity(original, tag));
    for (const SketchEntity& piece : pieces)
        subs.push_back(sketch::UndoCommand::addEntity(piece, tag));

    // Join the pieces at each split point. The rule lives in the library so
    // the CLI can reuse it verbatim; a split passes no boundaries (join only).
    // A circle opened into one 360-degree arc keeps BOTH ends free: the tie to
    // an entity at the cut is decided later, at drag time (the end left in place
    // ties, the end dragged away stays free), not here where the two ends
    // overlap and neither has moved.
    {
        std::vector<sketch::Entity> pieceLib;
        for (const SketchEntity& p : pieces) pieceLib.push_back(hobbycad::toLibraryEntity(p));
        std::vector<Point2D> junc;
        for (const QPointF& q : junctionPoints) junc.push_back({ q.x(), q.y() });
        recordAddedConstraints(sketch::computeCutConstraints(
            pieceLib, {}, junc, [this]() { return m_nextConstraintId++; }), subs);
        // Carry the original's own constraints onto the pieces where they still
        // hold (point anchors follow their piece; line orientation replicates).
        recordAddedConstraints(sketch::remapCutConstraints(
            origRefs, hobbycad::toLibraryEntity(original), pieceLib,
            [this]() { return m_nextConstraintId++; }), subs);
    }

    pushCompoundOrSingle(subs, tag);

    // Apply.
    replaceEntityWithPieces(entityId, pieces);
    return newIds;
}

QVector<int> SketchCanvas::splitEntityAtIntersections(int entityId)
{
    QVector<int> newIds;

    SketchEntity* entity = entityById(entityId);
    if (!entity) return newIds;

    // Find all intersections for this entity using library
    std::vector<sketch::Entity> libEntities = hobbycad::toLibraryEntities(m_entities);
    sketch::Entity libEntity = hobbycad::toLibraryEntity(*entity);

    std::vector<sketch::Intersection> allIntersections = sketch::findAllIntersections(libEntities);

    // Extract intersection points for this entity
    std::vector<Point2D> intersectionPoints =
        sketch::intersectionPointsTouching(allIntersections, entityId);

    if (intersectionPoints.empty()) return newIds;

    // Use library split function
    sketch::SplitResult result = sketch::splitEntityAtIntersections(
        libEntity, intersectionPoints,
        [this]() { return m_nextId++; });

    if (!result.success) return newIds;

    QVector<QPointF> junctions;
    for (const Point2D& p : intersectionPoints) junctions.append(QPointF(p.x, p.y));
    return applySplitPieces(entityId, result.newEntities, junctions, tr("Split"));
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

    return applySplitPieces(entityId, result.newEntities,
                            QVector<QPointF>{ splitPoint }, tr("Split"));
}

QVector<int> SketchCanvas::splitEntityAtPoints(int entityId, const QVector<QPointF>& points)
{
    QVector<int> newIds;
    SketchEntity* entity = entityById(entityId);
    if (!entity || points.isEmpty()) return newIds;

    sketch::Entity libEntity = hobbycad::toLibraryEntity(*entity);
    std::vector<Point2D> pts;
    pts.reserve(points.size());
    for (const QPointF& p : points) pts.push_back({ p.x(), p.y() });

    sketch::SplitResult result = sketch::splitEntityAtIntersections(
        libEntity, pts, [this]() { return m_nextId++; });
    if (!result.success) return newIds;

    return applySplitPieces(entityId, result.newEntities, points, tr("Split"));
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

    // The nearest crossing on either side of the click: the library's rule.
    std::vector<sketch::Entity> libEntities = hobbycad::toLibraryEntities(m_entities);
    const std::vector<sketch::Intersection> allIntersections = sketch::findAllIntersections(libEntities);
    const std::vector<Point2D> splitPoints =
        sketch::bracketingSplitPoints(*entity, allIntersections, clickPoint);
    if (splitPoints.empty()) return newIds;

    // Use the library's multi-point split with only the bracketing points
    sketch::Entity libEntity = hobbycad::toLibraryEntity(*entity);
    sketch::SplitResult result = sketch::splitEntityAtIntersections(
        libEntity, splitPoints,
        [this]() { return m_nextId++; });

    if (!result.success) return newIds;

    QVector<QPointF> junctions;
    for (const Point2D& p : splitPoints) junctions.append(QPointF(p.x, p.y));
    return applySplitPieces(entityId, result.newEntities, junctions, tr("Split"));
}

// ---------------------------------------------------------------------------
//  Rejoin collinear segments back into a single line
// ---------------------------------------------------------------------------
int SketchCanvas::rejoinCollinearSegments()
{
    if (m_selectedIds.size() < 2) return -1;

    // Collect selected entities for library validation
    std::vector<sketch::Entity> selectedEntities;
    for (int id : m_selectedIds) {
        const SketchEntity* e = entityById(id);
        if (!e) {
            QMessageBox::warning(this, tr("Rejoin"),
                tr("Selected entity not found."));
            return -1;
        }
        selectedEntities.push_back(toLibraryEntity(*e));
    }

    // The library checks collinearity, contiguity, and that nothing else is
    // attached at an interior junction (rejoining would break it).
    int attachedId = -1;
    auto rejoin = sketch::validateCollinearRejoin(selectedEntities, toLibraryEntities(m_entities),
                                                  &attachedId);
    if (!rejoin.success) {
        QMessageBox::warning(this, tr("Rejoin"),
            tr(rejoin.errorMessage.c_str()));
        return -1;
    }

    // --- Perform the rejoin ---
    bool isConstruction = entityById(*m_selectedIds.begin())->isConstruction;

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
    merged.points = {rejoin.mergedStart, rejoin.mergedEnd};
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
    std::vector<sketch::Entity> libEntities = toLibraryEntities(m_entities);

    // Use library profile detection
    sketch::ProfileDetectionOptions options;
    options.excludeConstruction = true;
    options.maxProfiles = 100;
    options.polygonSegments = 32;

    std::vector<sketch::Profile> libProfiles = sketch::detectProfilesWithHoles(libEntities, options);

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


void SketchCanvas::updateAssociativeOffsets()
{
    for (SketchEntity& child : m_entities) {
        if (child.offsetParentId < 0) continue;
        const SketchEntity* parent = entityById(child.offsetParentId);
        if (!parent) continue;   // parent deleted: leave the copy frozen
        sketch::Entity libChild = hobbycad::toLibraryEntity(child);
        const sketch::Entity libParent = hobbycad::toLibraryEntity(*parent);
        if (sketch::updateOffsetFromParent(libChild, libParent)) {
            const SketchEntity updated = hobbycad::toGuiEntity(libChild);
            // Preserve identity and the offset link; refresh only geometry.
            child.type = updated.type;
            child.points = updated.points;
            child.radius = updated.radius;
            child.startAngle = updated.startAngle;
            child.sweepAngle = updated.sweepAngle;
        }
    }
    m_profilesCacheDirty = true;
}

void SketchCanvas::updateSlotsFromPaths()
{
    // A slot follows its centerline: after the solve moved the centerline, each
    // slot that names one re-derives its shape from where the path ended up.
    // This is the GUI counterpart of the CLI's post-solve pass, and the sibling
    // of updateAssociativeOffsets. Dirty-checked: a slot is only re-derived when
    // its path actually moved, so large sketches full of slots pay nothing on
    // a solve that did not touch them (Aaron). Cost of a re-derive scales with
    // the path's complexity, uniform for a single segment or a whole tree.
    for (SketchEntity& slot : m_entities) {
        if (slot.type != SketchEntityType::Slot) continue;

        // Multi-segment slot (a chain, loop, or branching tree, >1 segment):
        // its shape is the swept OUTLINE of the whole path, re-derived from every
        // segment. A single segment falls through to the capsule path below.
        if (slot.pathEntityIds.size() > 1) {
            QVector<QPointF> sig;
            bool missing = false;
            for (int pid : slot.pathEntityIds) {
                const SketchEntity* seg = entityById(pid);
                if (!seg) { missing = true; break; }
                for (const auto& pnt : seg->points) sig.append(QPointF(pnt));
                sig.append(QPointF(seg->radius, seg->startAngle));
                sig.append(QPointF(seg->sweepAngle, static_cast<double>(seg->type)));
            }
            if (missing) continue;   // a segment was deleted: leave the slot frozen
            sig.append(QPointF(slot.radius, static_cast<double>(slot.pathEntityIds.size())));
            auto cit = m_slotPathCache.find(slot.id);
            if (cit != m_slotPathCache.end() && cit.value() == sig) continue;
            m_slotPathCache.insert(slot.id, sig);

            sketch::Entity libSlot = hobbycad::toLibraryEntity(slot);
            const std::vector<sketch::Entity> libAll =
                hobbycad::toLibraryEntities(m_entities);
            if (sketch::updateSlotOutlineFromPaths(libSlot, libAll))
                slot.outlineCache = libSlot.outlineCache;
            continue;
        }

        if (slot.pathEntityIds.size() != 1) continue;
        const SketchEntity* path = entityById(slot.pathEntityIds[0]);
        if (!path) continue;   // path deleted: leave the slot frozen

        QVector<QPointF> sig;
        sig.reserve(static_cast<int>(path->points.size()) + 2);
        for (const auto& pnt : path->points) sig.append(QPointF(pnt));
        // An arc path's sweep/radius can change without its points moving, so
        // fold them into the dirty signature too; otherwise a re-derive that
        // only changes the sweep (e.g. crossing 180 degrees) is wrongly skipped.
        sig.append(QPointF(path->radius, path->startAngle));
        sig.append(QPointF(path->sweepAngle, path->arcFlipped ? 1.0 : 0.0));
        auto it = m_slotPathCache.find(slot.id);
        if (it != m_slotPathCache.end() && it.value() == sig) continue;  // unchanged
        m_slotPathCache.insert(slot.id, sig);

        sketch::Entity libSlot = hobbycad::toLibraryEntity(slot);
        const sketch::Entity libPath = hobbycad::toLibraryEntity(*path);
        if (sketch::updateSlotFromPath(libSlot, libPath)) {
            const SketchEntity updated = hobbycad::toGuiEntity(libSlot);
            slot.points = updated.points;
            slot.radius = updated.radius;
            slot.startAngle = updated.startAngle;
            slot.sweepAngle = updated.sweepAngle;
            slot.arcFlipped = updated.arcFlipped;   // carry >180 direction, else render takes the short way
        }
    }
    m_profilesCacheDirty = true;
}

int SketchCanvas::addImportedEntities(const QVector<SketchEntity>& entities)
{
    int added = 0;
    for (SketchEntity e : entities) {
        e.id = m_nextId++;               // a fresh id in this sketch
        m_entities.append(e);
        pushUndoCommand(sketch::UndoCommand::addEntity(e));
        emit entityCreated(e.id);
        ++added;
    }
    if (added > 0) {
        m_profilesCacheDirty = true;
        update();
    }
    return added;
}

int SketchCanvas::createProjection(int sourceSketchId, int sourceEntityId)
{
    // Resolve the source (it lives in another sketch); the host resolver hands
    // back the source entity, its plane and its offset.
    if (!m_projectionResolver) return -1;
    SketchEntity source;
    SketchPlane sp = SketchPlane::XY;
    double so = 0.0;
    if (!m_projectionResolver(sourceSketchId, sourceEntityId, source, sp, so))
        return -1;

    const hobbycad::PlaneBasis srcBasis = hobbycad::planeBasisFor(sp, so);
    const hobbycad::PlaneBasis tgtBasis = hobbycad::planeBasisFor(m_plane);

    const sketch::Entity libSource = hobbycad::toLibraryEntity(source);
    sketch::Entity libChild;
    const int newId = m_nextId++;
    if (!sketch::makeProjectionChild(libChild, libSource, sourceSketchId, newId,
                                     srcBasis, tgtBasis)) {
        return -1;   // unprojectable type (the id gap is harmless)
    }

    SketchEntity child = hobbycad::toGuiEntity(libChild);
    m_entities.append(child);
    pushUndoCommand(sketch::UndoCommand::addEntity(child));
    emit entityCreated(child.id);
    updateProjectedEntities();     // seed it immediately so it draws at once
    m_profilesCacheDirty = true;
    update();
    return child.id;
}

void SketchCanvas::updateProjectedEntities()
{
    // Re-derive each projected entity from its source at solve time. The source
    // usually lives in ANOTHER sketch on a differently-angled plane; the host's
    // resolver looks it up (entity + plane + offset). A same-sketch source
    // falls back to local lookup. The projection is source plane -> this plane,
    // so any relative angle between them (on any axis) is handled by the two
    // bases (see updateProjectionFromSource).
    const hobbycad::PlaneBasis tgtBasis = hobbycad::planeBasisFor(m_plane);
    for (SketchEntity& child : m_entities) {
        if (child.projectionSourceId < 0) continue;

        SketchEntity source;
        hobbycad::PlaneBasis srcBasis;
        bool resolved = false;
        if (child.projectionSourceSketchId >= 0 && m_projectionResolver) {
            SketchPlane sp = SketchPlane::XY; double so = 0.0;
            if (m_projectionResolver(child.projectionSourceSketchId,
                                     child.projectionSourceId, source, sp, so)) {
                srcBasis = hobbycad::planeBasisFor(sp, so);
                resolved = true;
            }
        }
        if (!resolved) {
            const SketchEntity* s = entityById(child.projectionSourceId);
            if (!s) continue;   // source unresolved: leave the projection frozen
            source = *s;
            srcBasis = tgtBasis;   // same sketch, same plane
        }

        sketch::Entity libChild = hobbycad::toLibraryEntity(child);
        const sketch::Entity libSource = hobbycad::toLibraryEntity(source);
        if (sketch::updateProjectionFromSource(libChild, libSource, srcBasis, tgtBasis)) {
            const SketchEntity updated = hobbycad::toGuiEntity(libChild);
            child.type = updated.type;
            child.points = updated.points;
        }
    }
    m_profilesCacheDirty = true;
}

void SketchCanvas::offsetEntity(int entityId, double distance, const QPointF& clickPos)
{
    SketchEntity* entity = entityById(entityId);
    if (!entity) return;

    // Use library offset function
    sketch::Entity libEntity = hobbycad::toLibraryEntity(*entity);
    sketch::OffsetResult result = sketch::offsetEntity(libEntity, distance, clickPos, m_nextId++);

    if (!result.success) return;

    // Convert result back to GUI entity, and record the associative link so a
    // later solve re-derives it from its parent (Fusion's associative offset).
    SketchEntity newEntity = hobbycad::toGuiEntity(result.entity);
    newEntity.offsetParentId = entityId;
    newEntity.offsetDistance = distance;
    newEntity.offsetSide = result.side;
    m_entities.append(newEntity);
    pushUndoCommand(sketch::UndoCommand::addEntity(newEntity));
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
            QString::fromStdString(result.errorMessage));
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
            QString::fromStdString(result.errorMessage));
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
    std::vector<sketch::Entity> sourceEntities;
    for (int id : m_selectedIds) {
        const SketchEntity* entity = entityById(id);
        if (entity) {
            sourceEntities.push_back(toLibraryEntity(*entity));
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
        QMessageBox::warning(this, tr("Pattern Error"), QString::fromStdString(result.errorMessage));
        return;
    }

    commitPatternEntities(result.entities, nextId);
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
    std::vector<sketch::Entity> sourceEntities;
    for (int id : m_selectedIds) {
        const SketchEntity* entity = entityById(id);
        if (entity) {
            sourceEntities.push_back(toLibraryEntity(*entity));
        }
    }

    // Set up pattern parameters
    sketch::CircPatternParams params;
    params.center = Point2D{centerX, centerY};
    params.count = count;
    params.totalAngle = totalAngle;

    // Use library to create pattern
    int nextId = m_nextId;
    sketch::CircPatternResult result = sketch::createCircularPattern(
        sourceEntities, params, [&nextId]() { return nextId++; });

    if (!result.success) {
        QMessageBox::warning(this, tr("Pattern Error"), QString::fromStdString(result.errorMessage));
        return;
    }

    commitPatternEntities(result.entities, nextId);
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
    if (enabled) cancelTransformPick();
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
    if (enabled) cancelTransformPick();
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
    return sketch::getEntityAngle(*entity);  // returns [0, 360)
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

    const double handleSize = kBgHandleSizePx / m_zoom;  // Handle size in world units

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

// One compound undo for an edit that changed a constraint and the entity it
// drives together (a radius, a sweep angle, a tangent angle).
void SketchCanvas::pushConstraintAndEntityEdit(const SketchConstraint& oldConstraint, const SketchConstraint& newConstraint,
                                               const SketchEntity& oldEntity, const SketchEntity& newEntity,
                                               const std::string& description)
{
    std::vector<sketch::UndoCommand> subs;
    subs.push_back(sketch::UndoCommand::modifyConstraint(oldConstraint, newConstraint));
    subs.push_back(sketch::UndoCommand::modifyEntity(oldEntity, newEntity));
    pushUndoCommand(sketch::UndoCommand::compound(subs, description));
}

// A lone command goes on the stack as itself; several become one compound.
void SketchCanvas::pushCompoundOrSingle(std::vector<sketch::UndoCommand>& subs, const std::string& description)
{
    if (subs.size() == 1) pushUndoCommand(subs.front());
    else pushUndoCommand(sketch::UndoCommand::compound(subs, description));
}

// After a solve moved an arc: keep a tangent arc tangent and its sweep-angle
// construction lines and Angle constraint in step with the geometry.
void SketchCanvas::syncArcAfterSolve(int entityId)
{
    SketchEntity* ent = entityById(entityId);
    if (ent && ent->type == SketchEntityType::Arc
            && ent->tangentEntityId >= 0
            && ent->points.size() >= 3) {
        reestablishTangency(*ent);
    }
    if (ent && ent->type == SketchEntityType::Arc) {
        const int gid = findSweepAngleGroupForArc(entityId);
        if (gid >= 0) {
            syncSweepAngleConstructionLines(*ent);
            if (const SketchGroup* g = groupById(gid)) {
                for (int cid : g->constraintIds) {
                    SketchConstraint* c = constraintById(cid);
                    if (c && c->type == ConstraintType::Angle) {
                        c->value = std::abs(ent->sweepAngle);
                        c->supplementary = (std::abs(ent->sweepAngle) > 180.0);
                        c->anchorPoint = ent->points[0];
                    }
                }
            }
        }
    }
}

// The common tail of undo() and redo(): re-solve, re-derive tangent arcs and
// sweep-angle construction lines, and tell the properties panel.
void SketchCanvas::finishUndoRedo(const sketch::UndoCommand& cmd)
{
    dropStaleEnteredGroup();

    updateUndoRedoState();
    pruneOrphanedConstraints();
    solveConstraints();

    for (auto& entity : m_entities) {
        if (entity.type == SketchEntityType::Arc
                && entity.tangentEntityId >= 0
                && entity.points.size() >= 3) {
            reestablishTangency(entity);
        }
    }
    // Restored arcs: the dashed helper lines and angle label follow the
    // restored geometry rather than staying at stale positions.
    for (auto& entity : m_entities) {
        if (entity.type == SketchEntityType::Arc
                && entity.points.size() >= 3) {
            syncSweepAngleConstructionLines(entity);
        }
    }

    emit entityModified(cmd.entity.id);
    update();
}

void SketchCanvas::finishUndoRedoMultiple()
{
    updateUndoRedoState();
    pruneOrphanedConstraints();
    solveConstraints();
    for (auto& entity : m_entities) {
        if (entity.type == SketchEntityType::Arc && entity.points.size() >= 3) {
            if (entity.tangentEntityId >= 0)
                reestablishTangency(entity);
            syncSweepAngleConstructionLines(entity);
        }
    }
    update();
}

// Undo of an add (or redo of a delete): the entity goes, and so does any
// selection state pointing at it.
void SketchCanvas::removeEntityForUndo(int entityId)
{
    for (int i = 0; i < m_entities.size(); ++i) {
        if (m_entities[i].id == entityId) {
            m_entities.removeAt(i);
            break;
        }
    }
    selectRemove(entityId);
    if (m_selectedId == entityId) {
        m_selectedId = -1;
    }
    m_profilesCacheDirty = true;
}

void SketchCanvas::removeConstraintForUndo(int constraintId)
{
    for (int i = 0; i < m_constraints.size(); ++i) {
        if (m_constraints[i].id == constraintId) {
            m_constraints.removeAt(i);
            break;
        }
    }
    if (m_selectedConstraintId == constraintId) {
        m_selectedConstraintId = -1;
    }
}

// The radius a driving Radius or Diameter constraint pins an entity to, or -1.
double SketchCanvas::lockedRadiusFor(int entityId) const
{
    for (const auto& c : m_constraints) {
        if ((c.type == ConstraintType::Radius
                || c.type == ConstraintType::Diameter)
                && c.isDriving && c.enabled
                && !c.entityIds.empty()
                && c.entityIds[0] == entityId) {
            return (c.type == ConstraintType::Diameter) ? c.value / 2.0 : c.value;
        }
    }
    return -1.0;
}

// Clamp an arc slot's sweep to the furthest the library allows at all
// (absoluteMaxArcSlotSweepDegrees: the caps may overlap and free the center
// piece, the case Aaron asked for; the drag limit was lifted on purpose to
// match the tool). Past it the START end is moved back onto the circle of
// `radius`.
void SketchCanvas::clampArcSlotSweep(SketchEntity* sel, const QPointF& center, double radius)
{
    const double maxSweep =
        degreesToRadians(sketch::absoluteMaxArcSlotSweepDegrees(radius, sel->radius));
    const double startAng = std::atan2(sel->points[1].y - center.y(),
                                       sel->points[1].x - center.x());
    const double endAng = std::atan2(sel->points[2].y - center.y(),
                                     sel->points[2].x - center.x());
    double sweep = endAng - startAng;
    if (sel->arcFlipped) sweep = hobbycad::geometry::oppositeSweepRad(sweep);
    else sweep = hobbycad::geometry::wrapSweepRad(sweep);
    if (std::abs(sweep) > maxSweep) {
        const double clampedSweep = (sweep > 0) ? maxSweep : -maxSweep;
        const double newStartAng = endAng - clampedSweep;
        sel->points[1] = center + QPointF(radius * std::cos(newStartAng),
                                          radius * std::sin(newStartAng));
    }
}

// The copies a pattern produced become canvas entities and the selection.
void SketchCanvas::commitPatternEntities(const std::vector<sketch::Entity>& entities, int nextId)
{
    QVector<int> newIds;
    for (const sketch::Entity& libEntity : entities) {
        SketchEntity guiEntity = toGuiEntity(libEntity);
        m_entities.append(guiEntity);
        newIds.append(guiEntity.id);
        emit entityCreated(guiEntity.id);
    }
    m_nextId = nextId;
    for (int id : newIds) {
        selectEntity(id, true);
    }
    m_profilesCacheDirty = true;
    update();
}

// Trim / extend / split: the original and every constraint that names it go,
// the pieces come in, and the sketch re-solves (its degrees of freedom changed).
void SketchCanvas::replaceEntityWithPieces(int entityId, const std::vector<SketchEntity>& pieces)
{
    m_entities.erase(std::remove_if(m_entities.begin(), m_entities.end(),
                     [entityId](const SketchEntity& e) { return e.id == entityId; }),
                     m_entities.end());
    m_constraints.erase(std::remove_if(m_constraints.begin(), m_constraints.end(),
                     [entityId](const SketchConstraint& c) {
                         for (int eid : c.entityIds) if (eid == entityId) return true;
                         return false;
                     }), m_constraints.end());
    for (const SketchEntity& piece : pieces) {
        m_entities.append(piece);
        emit entityCreated(piece.id);
    }
    m_profilesCacheDirty = true;
    solveConstraints();
    update();
}

// A temporary FixedPoint (reserved negative id) pinning one point for the
// duration of a drag solve; the caller removes them by id afterwards.
void SketchCanvas::appendTempFixedPoint(int entityId, int pointIndex, std::vector<int>& tempIds)
{
    SketchConstraint fx;
    fx.id = -1000000 - static_cast<int>(tempIds.size());   // reserved temp ids
    fx.type = ConstraintType::FixedPoint;
    fx.entityIds = { entityId };
    fx.pointIndices = { pointIndex };
    fx.isDriving = true; fx.enabled = true; fx.satisfied = true;
    m_constraints.append(fx);
    tempIds.push_back(fx.id);
}

void SketchCanvas::updateUndoRedoState()
{
    emit undoAvailabilityChanged(m_libUndoStack.canUndo());
    emit redoAvailabilityChanged(m_libUndoStack.canRedo());
    emit undoStackChanged();
}

QStringList SketchCanvas::undoDescriptions() const
{
    QStringList result;
    for (const auto& d : m_libUndoStack.undoDescriptions())
        result.append(QString::fromStdString(d));
    return result;
}

QStringList SketchCanvas::redoDescriptions() const
{
    QStringList result;
    for (const auto& d : m_libUndoStack.redoDescriptions())
        result.append(QString::fromStdString(d));
    return result;
}

void SketchCanvas::undoMultiple(int levels)
{
    if (levels <= 0) return;
    auto cmds = m_libUndoStack.undoMultiple(levels);
    for (const auto& cmd : cmds)
        undoSingleCommand(cmd);
    finishUndoRedoMultiple();
}

void SketchCanvas::redoMultiple(int levels)
{
    if (levels <= 0) return;
    auto cmds = m_libUndoStack.redoMultiple(levels);
    for (const auto& cmd : cmds)
        redoSingleCommand(cmd);
    finishUndoRedoMultiple();
}

void SketchCanvas::undoSingleCommand(const sketch::UndoCommand& cmd)
{
    switch (cmd.type) {
    case sketch::CommandType::AddEntity:
        // Undo add = delete the entity
        removeEntityForUndo(cmd.entity.id);
        break;

    case sketch::CommandType::DeleteEntity:
        // Undo delete = restore the entity
        m_entities.append(SketchEntity(cmd.entity));
        m_profilesCacheDirty = true;
        break;

    case sketch::CommandType::ModifyEntity:
        // Undo modify = restore previous geometry, preserving GUI-only fields
        for (int i = 0; i < m_entities.size(); ++i) {
            if (m_entities[i].id == cmd.entity.id) {
                int savedTangentId = m_entities[i].tangentEntityId;
                bool savedSelected = m_entities[i].selected;
                static_cast<sketch::Entity&>(m_entities[i]) = cmd.previousEntity;
                m_entities[i].tangentEntityId = savedTangentId;
                m_entities[i].selected = savedSelected;
                break;
            }
        }
        m_profilesCacheDirty = true;
        break;

    case sketch::CommandType::AddConstraint:
        // Undo add = delete the constraint
        removeConstraintForUndo(cmd.constraint.id);
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
        // Undo add = remove the group, and the members' back-pointers with it
        syncGroupMembership(cmd.group, -1);
        m_groups.erase(
            std::remove_if(m_groups.begin(), m_groups.end(),
                           [&cmd](const SketchGroup& g) { return g.id == cmd.group.id; }),
            m_groups.end());
        break;

    case sketch::CommandType::DeleteGroup:
        // Undo delete = restore the group and its members' back-pointers
        m_groups.append(cmd.group);
        syncGroupMembership(cmd.group, cmd.group.id);
        break;

    case sketch::CommandType::ModifyGroup:
        // Undo modify = restore previous group state (membership included)
        for (int i = 0; i < m_groups.size(); ++i) {
            if (m_groups[i].id == cmd.group.id) {
                syncGroupMembership(cmd.group, -1);
                m_groups[i] = cmd.previousGroup;
                syncGroupMembership(cmd.previousGroup, cmd.previousGroup.id);
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
        removeEntityForUndo(cmd.entity.id);
        break;

    case sketch::CommandType::ModifyEntity:
        // Redo modify = apply the modification again, preserving GUI-only fields
        for (int i = 0; i < m_entities.size(); ++i) {
            if (m_entities[i].id == cmd.entity.id) {
                int savedTangentId = m_entities[i].tangentEntityId;
                bool savedSelected = m_entities[i].selected;
                static_cast<sketch::Entity&>(m_entities[i]) = cmd.entity;
                m_entities[i].tangentEntityId = savedTangentId;
                m_entities[i].selected = savedSelected;
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
        removeConstraintForUndo(cmd.constraint.id);
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
        // Redo add = add the group back, members pointing at it again
        m_groups.append(cmd.group);
        syncGroupMembership(cmd.group, cmd.group.id);
        break;

    case sketch::CommandType::DeleteGroup:
        // Redo delete = remove the group again and clear the back-pointers
        syncGroupMembership(cmd.group, -1);
        m_groups.erase(
            std::remove_if(m_groups.begin(), m_groups.end(),
                           [&cmd](const SketchGroup& g) { return g.id == cmd.group.id; }),
            m_groups.end());
        break;

    case sketch::CommandType::ModifyGroup:
        // Redo modify = apply the modification again (membership included)
        for (int i = 0; i < m_groups.size(); ++i) {
            if (m_groups[i].id == cmd.group.id) {
                syncGroupMembership(cmd.previousGroup, -1);
                m_groups[i] = cmd.group;
                syncGroupMembership(cmd.group, cmd.group.id);
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
    finishUndoRedo(cmd);
}

void SketchCanvas::redo()
{
    if (!m_libUndoStack.canRedo()) return;

    sketch::UndoCommand cmd = m_libUndoStack.redo();
    redoSingleCommand(cmd);
    finishUndoRedo(cmd);
}

// ---- Inline constraint value editing ----------------------------------------

void SketchCanvas::beginInlineConstraintEdit(int constraintId, bool isCreation)
{
    // If already editing, commit current edit first
    if (m_inlineEditActive)
        commitInlineConstraintEdit();

    const SketchConstraint* c = constraintById(constraintId);
    if (!c) return;

    m_inlineEditActive = true;
    m_inlineEditConstraintId = constraintId;
    m_inlineEditIsCreation = isCreation;
    m_inlineEditOriginalValue = c->value;
    m_inlineEditIsAngle = (c->type == ConstraintType::Angle
                           || c->type == ConstraintType::FixedAngle);

    // Pre-fill buffer with formatted current value
    if (m_inlineEditIsAngle) {
        m_inlineEditBuffer = QString::fromStdString(hobbycad::formatValue(c->value));
    } else {
        m_inlineEditBuffer = QString::fromStdString(
            hobbycad::formatValue(hobbycad::mmToUnit(c->value, m_displayUnit)));
    }
    m_inlineEditCursorPos = m_inlineEditBuffer.length();
    m_inlineEditSelectAll = true;

    // Select this constraint
    m_selectedConstraintId = constraintId;

    update();
}

void SketchCanvas::commitInlineConstraintEdit()
{
    if (!m_inlineEditActive) return;

    double newValue = m_inlineEditOriginalValue;  // Default: keep original

    if (m_inlineEditSelectAll) {
        // User pressed Enter without typing: keep the original value
        // (the constraint was already created with this value)
    } else if (!m_inlineEditBuffer.isEmpty()) {
        // Parse the buffer
        std::string exprStdStr = m_inlineEditBuffer.toStdString();
        double parsed = 0.0;
        bool valid = false;

        if (m_inlineEditIsAngle) {
            double dmsResult;
            if (hobbycad::parseDMS(exprStdStr, dmsResult)) {
                parsed = dmsResult;
                valid = true;
            } else if (m_paramEngine) {
                double exprResult;
                if (m_paramEngine->evaluateExpression(exprStdStr, exprResult)) {
                    parsed = exprResult;
                    valid = true;
                }
            }
            if (!valid) {
                bool ok;
                parsed = m_inlineEditBuffer.toDouble(&ok);
                valid = ok;
            }
            // Angles can be negative for sweep angles, but not zero
            if (valid && qFuzzyIsNull(parsed)) valid = false;
        } else {
            if (m_paramEngine) {
                double exprResult;
                if (m_paramEngine->evaluateExpression(exprStdStr, exprResult, m_displayUnit)) {
                    parsed = hobbycad::unitToMm(exprResult, m_displayUnit);
                    valid = true;
                }
            }
            if (!valid) {
                parsed = hobbycad::parseValueWithUnit(exprStdStr, m_displayUnit);
                valid = (parsed > 0.0);
            }
            // Lengths must be positive
            if (valid && parsed <= 0.0) valid = false;
        }

        if (valid) {
            newValue = parsed;
        }
    }

    // Capture the source expression when it references a parameter, so the
    // dimension re-evaluates if that parameter later changes. A plain number
    // clears any prior parametric link. (Detected as in the CLI: the bare
    // form fails or differs from the parameter-aware form.)
    std::string exprToStore;
    if (!m_inlineEditSelectAll && !m_inlineEditBuffer.isEmpty()
        && !m_parameterValues.empty()) {
        const std::string b = m_inlineEditBuffer.toStdString();
        if (hobbycad::expressionUsesParameters(b, m_parameterValues)) exprToStore = b;
    }
    if (SketchConstraint* ec = constraintById(m_inlineEditConstraintId)) {
        ec->expression = exprToStore;   // set or clear the parametric link
    }

    // Apply if different from original
    if (!qFuzzyCompare(newValue, m_inlineEditOriginalValue)) {
        setConstraintValue(m_inlineEditConstraintId, newValue);
    }

    // Reset state
    m_inlineEditActive = false;
    m_inlineEditConstraintId = -1;
    m_inlineEditBuffer.clear();
    m_inlineEditCursorPos = 0;
    m_inlineEditSelectAll = false;
    m_inlineEditIsCreation = false;

    update();
}

void SketchCanvas::cancelInlineConstraintEdit()
{
    if (!m_inlineEditActive) return;

    if (m_inlineEditIsCreation) {
        // Delete the just-created constraint
        int cid = m_inlineEditConstraintId;
        m_constraints.erase(
            std::remove_if(m_constraints.begin(), m_constraints.end(),
                           [cid](const SketchConstraint& c) { return c.id == cid; }),
            m_constraints.end());
        solveConstraints();
    }
    // For editing existing: value was never changed from original (we only apply on commit)

    // Reset state
    m_inlineEditActive = false;
    m_inlineEditConstraintId = -1;
    m_inlineEditBuffer.clear();
    m_inlineEditCursorPos = 0;
    m_inlineEditSelectAll = false;
    m_inlineEditIsCreation = false;

    update();
}


// ---- Theme (light / dark) --------------------------------------------
bool SketchCanvas::isDarkContext() const
{
    if (m_themeMode == ThemeMode::Dark) return true;
    if (m_themeMode == ThemeMode::Light) return false;
    // Auto: an explicit app-wide flag wins (main.cpp sets it when a dark
    // widget theme is loaded), otherwise judge by the application window
    // color's luminance so an OS/Qt dark palette is followed automatically.
    const QVariant flag = qApp ? qApp->property("hobbycad_dark_theme") : QVariant();
    if (flag.isValid()) return flag.toBool();
    const QColor w = (qApp ? qApp->palette() : palette()).color(QPalette::Window);
    const double lum = (0.299 * w.red() + 0.587 * w.green() + 0.114 * w.blue()) / 255.0;
    return lum < 0.5;
}

void SketchCanvas::applyTheme()
{
    const bool dark = isDarkContext();
    m_theme = dark ? SketchTheme::dark() : SketchTheme::light();
    applySketchThemeOverrides(m_theme, dark);   // user recolors, if any
    // Keep the historical fully-constrained member in sync so existing
    // callers/accessors still work; renderers read the rest from m_theme.
    m_fullyConstrainedColor = m_theme.fullyConstrained;
    QPalette pal = palette();
    pal.setColor(QPalette::Window, m_theme.background);
    setPalette(pal);
    update();
}

void SketchCanvas::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    // Only react to APPLICATION palette/theme changes; reacting to our own
    // PaletteChange would recurse (applyTheme calls setPalette).
    if (m_themeMode == ThemeMode::Auto &&
        (event->type() == QEvent::ApplicationPaletteChange ||
         event->type() == QEvent::ThemeChange)) {
        applyTheme();
    }
}

}  // namespace hobbycad
