// =====================================================================
//  src/hobbycad/gui/tools/placementtoolhandler.cpp
// =====================================================================
//
//  Part of HobbyCAD.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include "placementtoolhandler.h"

#include "../placementpainter.h"
#include "../sketchcanvas.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>

namespace hobbycad {

namespace {

// placementKind() casts a canvas mode to CreationMode; the orders must agree.
template <typename Mode>
constexpr bool sameAs(Mode mode, CreationMode creation)
{
    return static_cast<int>(mode) == static_cast<int>(creation);
}
using C = SketchCanvas;
static_assert(sameAs(C::LineMode::Construction, CreationMode::LineConstruction)
              && sameAs(C::LineMode::Tangent, CreationMode::LineTangent));
static_assert(sameAs(C::RectMode::Parallelogram, CreationMode::RectParallelogram)
              && sameAs(C::RectMode::Center, CreationMode::RectCenter));
static_assert(sameAs(C::CircleMode::ThreeTangent, CreationMode::CircleThreeTangent)
              && sameAs(C::CircleMode::TwoPoint, CreationMode::CircleTwoPoint));
static_assert(sameAs(C::ArcMode::Tangent, CreationMode::ArcTangent)
              && sameAs(C::ArcMode::CenterStartEnd, CreationMode::ArcCenterStartEnd));
static_assert(sameAs(C::SlotMode::ArcEnds, CreationMode::SlotArcEnds)
              && sameAs(C::SlotMode::Overall, CreationMode::SlotOverall));
static_assert(sameAs(C::PolygonMode::Freeform, CreationMode::PolygonFreeform)
              && sameAs(C::PolygonMode::Circumscribed, CreationMode::PolygonCircumscribed));
static_assert(sameAs(C::EllipseMode::Endpoints, CreationMode::EllipseEndpointsArc)
              && sameAs(C::EllipseMode::Arc, CreationMode::EllipseArc));
static_assert(sameAs(C::SplineMode::Conic, CreationMode::SplineConic)
              && sameAs(C::SplineMode::FitPoints, CreationMode::SplineFitPoints));

QString translated(const char* text)
{
    return QCoreApplication::translate(sketch::placementContext(), text);
}

/// The clicks a placement reads from the entity being placed: the ones
/// placed, not the cursor slot after them.
std::size_t placedClicks(const SketchCanvas& canvas)
{
    return static_cast<std::size_t>(canvas.previewPointCount());
}

}  // namespace

sketch::PlacementInput PlacementToolHandler::input(const SketchCanvas& canvas,
                                                   const QPointF& cursor) const
{
    sketch::PlacementInput in;
    const SketchEntity& pending = canvas.pendingEntity();
    for (std::size_t i = 0; i < placedClicks(canvas) && i < pending.points.size(); ++i) {
        in.clicks.push_back(pending.points[i]);
    }
    in.cursor = cursor;
    in.locks = sketch::stageLocks(canvas.dimInputState());
    in.flipped = canvas.arcSlotFlipped();
    in.semicircle = QGuiApplication::queryKeyboardModifiers() & Qt::ControlModifier;
    in.slotRadius = pending.radius;
    in.sides = pending.sides;
    for (int id : canvas.tangentTargets()) {
        if (const SketchEntity* e = canvas.findEntity(id)) in.targets.push_back(*e);
    }
    in.turn = m_turn;
    in.closeDistance = canvas.entitySnapTolerance() / canvas.zoomFactor();
    return in;
}

sketch::PlacementStage PlacementToolHandler::stage(const SketchCanvas& canvas) const
{
    sketch::PlacementStage s;
    s.placed = canvas.previewPointCount();
    s.targets = canvas.tangentTargetCount();
    s.chainsArc = canvas.tangentArcChainAvailable();
    s.canFinish = s.placed >= 2;
    return s;
}

sketch::PlacementPreview PlacementToolHandler::preview(const SketchCanvas& canvas) const
{
    // The point still being placed is the cursor slot, where the locks put
    // it, when the placement keeps one.
    const sketch::PlacementKind k = kind(canvas);
    const auto& pending = canvas.pendingEntity().points;
    QPointF cursor = canvas.currentMouseWorld();
    if (sketch::placementClicks(k) > 0 && pending.size() > placedClicks(canvas)) {
        cursor = QPointF(pending[placedClicks(canvas)]);
    }
    return sketch::placementPreview(k, input(canvas, cursor));
}

bool PlacementToolHandler::initDimFields(SketchCanvas& canvas)
{
    for (sketch::DimField field : sketch::placementFields(kind(canvas), stage(canvas))) {
        canvas.addDimField(field);
    }
    return true;
}

QString PlacementToolHandler::hint(const SketchCanvas& canvas) const
{
    QString text = translated(sketch::placementPrompt(kind(canvas), stage(canvas)));
    if (text.contains(QLatin1String("%1"))) text = text.arg(canvas.tangentTargetCount());
    return text;
}

QString PlacementToolHandler::cursorHint(const SketchCanvas& canvas) const
{
    if (const char* text = sketch::placementCursorPrompt(kind(canvas), stage(canvas))) {
        return translated(text);
    }
    return SketchToolHandler::cursorHint(canvas);
}

bool PlacementToolHandler::supportsAngleSnap(const SketchCanvas& canvas) const
{
    return sketch::placementAngleSnaps(kind(canvas));
}

bool PlacementToolHandler::chainsFromLastPoint(const SketchCanvas& canvas) const
{
    return sketch::placementChains(kind(canvas));
}

bool PlacementToolHandler::canSwitchModeWhileDrawing(const SketchCanvas& canvas,
                                                     int modeValue) const
{
    return sketch::placementCanSwitch(kind(canvas), static_cast<CreationMode>(modeValue));
}

QPointF PlacementToolHandler::placedCursor(SketchCanvas& canvas, const QPointF& world,
                                           bool keepSnap) const
{
    const sketch::PlacementInput in = input(canvas, world);
    std::vector<Point2D> clicks;
    const QPointF at(sketch::placementCursor(kind(canvas), in, canvas.rawMouseWorld(), keepSnap,
                                             &clicks));
    // A lock that moves a placed click moves it on the canvas too.
    auto& pending = canvas.pendingEntityRef().points;
    for (std::size_t i = 0; i < clicks.size() && i < pending.size(); ++i) {
        const Point2D was = in.clicks[i];
        if (clicks[i].x != was.x || clicks[i].y != was.y) pending[i] = clicks[i];
    }
    return at;
}

bool PlacementToolHandler::constrainCursor(SketchCanvas& canvas, QPointF& world, bool altHeld)
{
    // The cursor follows the mode's path (an axis, a tangent, a perimeter);
    // the locks move the point being placed, not the cursor.
    if (!canvas.isDrawing() || canvas.previewPointCount() < 1) return false;
    sketch::PlacementInput in = input(canvas, world);
    in.locks.clear();
    const QPointF at(sketch::placementCursor(kind(canvas), in, canvas.rawMouseWorld(),
                                             canvas.hasActiveSnap() && !altHeld));
    if (at == world) return false;
    world = at;
    return true;
}

bool PlacementToolHandler::updateEntity(SketchCanvas& canvas, const QPointF& pos)
{
    // An open-ended placement's points are its clicks; nothing tracks.
    if (canvas.pendingEntity().points.empty()) return true;
    if (sketch::placementClicks(kind(canvas)) == 0) return true;
    canvas.setCursorSlot(placedCursor(canvas, pos, canvas.hasActiveSnap()));
    return true;
}

bool PlacementToolHandler::drawPreview(SketchCanvas& canvas, QPainter& painter)
{
    paintPlacementPreview(canvas, painter, preview(canvas));
    return true;
}

bool PlacementToolHandler::normalize(SketchCanvas& canvas, SketchEntity& entity, bool& valid)
{
    // The click order -> stored layout translation is the library's, the
    // rule the preview used: a wrong layout renders identically and shows
    // only when the solver or a drag reads the points.
    const sketch::PlacementKind k = kind(canvas);
    if (entity.type != sketch::placementEntityType(k)) return false;
    sketch::PlacementInput in = input(canvas, canvas.currentMouseWorld());
    in.clicks.assign(entity.points.begin(), entity.points.end());
    const std::size_t needed = static_cast<std::size_t>(sketch::placementClicks(k));
    if (needed > 0 && in.clicks.size() > needed) in.clicks.resize(needed);
    valid = sketch::placementEntity(k, in, static_cast<sketch::Entity&>(entity));
    return true;
}

bool PlacementToolHandler::beginEntity(SketchCanvas& canvas, SketchEntity& entity)
{
    m_turn.reset();   // a turn belongs to one rectangle
    entity.type = sketch::placementEntityType(kind(canvas));
    return true;
}

bool PlacementToolHandler::keyPress(SketchCanvas& canvas, QKeyEvent* event)
{
    // Shift flips the arc being placed the long way round, once there is
    // one to flip. The canvas resets the flip at every entity start.
    if (!canvas.isDrawing() || event->key() != Qt::Key_Shift) return false;
    if (!sketch::placementFlippable(kind(canvas), stage(canvas))) return false;
    canvas.toggleArcFlip();
    canvas.update();
    return true;
}

void PlacementToolHandler::dimFieldsChanged(SketchCanvas& canvas)
{
    // A corner rectangle turns once both sides are pinned; the turn is taken
    // from the cursor at that moment, once.
    if (!sketch::placementTurnsWhenLocked(kind(canvas)) || m_turn) return;
    if (canvas.dimFieldCount() < 2 || !canvas.allDimFieldsLocked()
        || canvas.previewPointCount() < 1) {
        return;
    }
    m_turn = sketch::captureLockedTurn(canvas.previewPoint(0), canvas.currentMouseWorld());
}

void PlacementToolHandler::placeClick(SketchCanvas& canvas, const QPointF& world)
{
    const sketch::PlacementKind k = kind(canvas);
    const QPointF snapped = canvas.snapToGeometry(world);
    const QPointF at(sketch::placementClick(k, input(canvas, snapped), snapped));
    canvas.appendStagedPoint(at);
    const int needed = sketch::placementClicks(k);
    if (needed > 0 && canvas.previewPointCount() >= needed) {
        canvas.commitEntity();
    } else {
        canvas.refreshDimFields();   // the next stage's fields and prompt
    }
}

bool PlacementToolHandler::stagedPress(SketchCanvas& canvas, QMouseEvent* event,
                                       const QPointF& world)
{
    // The first click still starts the entity through the canvas.
    if (!canvas.isDrawing()) return false;
    canvas.beginDragDetection(event->pos());   // per STAGE, not per entity
    placeClick(canvas, world);
    return true;
}

bool PlacementToolHandler::stagedRelease(SketchCanvas& canvas, const QPointF& world)
{
    if (!canvas.isDrawing()) return false;
    if (canvas.wasDragged()) placeClick(canvas, world);
    return true;
}

}  // namespace hobbycad
