// =====================================================================
//  src/hobbycad/gui/tools/ellipsetoolhandler.cpp — Ellipse tool handler
// =====================================================================
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#include "ellipsetoolhandler.h"
#include "../sketchcanvas.h"

#include <hobbycad/geometry/utils.h>
#include <hobbycad/sketch/queries.h>

#include <QCoreApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPointF>

#include <cmath>
#include <initializer_list>
#include <vector>

namespace hobbycad {

namespace {

using hobbycad::sketch::EllipsePlacement;

/// The library placement the canvas's ellipse mode stands for.
EllipsePlacement placementOf(const SketchCanvas& canvas)
{
    switch (canvas.ellipseMode()) {
    case SketchCanvas::EllipseMode::ThreePoint: return EllipsePlacement::ThreePoint;
    case SketchCanvas::EllipseMode::Arc:        return EllipsePlacement::Arc;
    case SketchCanvas::EllipseMode::SpanRise:   return EllipsePlacement::SpanRise;
    case SketchCanvas::EllipseMode::Corner:     return EllipsePlacement::Corner;
    case SketchCanvas::EllipseMode::Endpoints:  return EllipsePlacement::Endpoints;
    default:                                    return EllipsePlacement::CenterAxes;
    }
}

std::vector<Point2D> toClicks(const SketchEntity& e, std::size_t atMost = 99)
{
    std::vector<Point2D> clicks;
    for (std::size_t i = 0; i < e.points.size() && i < atMost; ++i) {
        clicks.push_back(Point2D(e.points[i]));
    }
    return clicks;
}

/// The entity the pending points (placed clicks plus the cursor slot)
/// describe right now, through the one library rule the commit uses.
bool ghostFor(SketchCanvas& canvas, sketch::Entity& out)
{
    return sketch::ellipseFromPlacement(placementOf(canvas), toClicks(canvas.pendingEntity()),
                                        canvas.arcSlotFlipped(), out);
}

/// Elliptical Arc only: the ellipse the first three PLACED clicks fix.
bool fixedEllipse(SketchCanvas& canvas, sketch::Entity& out)
{
    if (canvas.ellipseMode() != SketchCanvas::EllipseMode::Arc) return false;
    if (canvas.previewPointCount() < 3 || canvas.pendingEntity().points.size() < 3) {
        return false;
    }
    return sketch::ellipseFromPlacement(EllipsePlacement::CenterAxes,
                                        toClicks(canvas.pendingEntity(), 3), false, out);
}

/// Where `world` lands on the perimeter of `e`: at the parameter the click
/// would be read at, so the constrained cursor and the click agree.
QPointF onPerimeter(const sketch::Entity& e, const QPointF& world)
{
    return sketch::ellipsePointAtParamDeg(e, sketch::ellipseParamDeg(e, world));
}

/// The point a radius-type lock measures from at this stage: the center
/// for Center + Axes and Corner, the first click for 3-Point's span, the
/// span's midpoint for Span + Rise's rise.
QPointF lockOrigin(const SketchCanvas& canvas, const SketchEntity& e, int slot)
{
    if (e.points.empty()) return QPointF();
    const EllipsePlacement m = placementOf(canvas);
    const bool midpointCenter =
        (m == EllipsePlacement::ThreePoint || m == EllipsePlacement::SpanRise);
    if (midpointCenter && slot >= 2 && e.points.size() >= 2) {
        return geometry::lineMidpoint(Point2D(e.points[0]), Point2D(e.points[1]));
    }
    return QPointF(e.points[0]);
}

/// The radius of `e` that is not `known`, an axis the placement already
/// fixed. A second axis longer than the first becomes the major one, so
/// the stage's value can be either field.
double otherRadius(const sketch::Entity& e, double known)
{
    return std::fabs(e.majorRadius - known) < geometry::kZeroEps ? e.minorRadius
                                                                 : e.majorRadius;
}

/// Where a stage's typed-value field sits: below the middle of the
/// dimension it types, in screen space.
QPointF dimFieldAnchor(const SketchCanvas& canvas, const QPointF& fromWorld,
                       const QPointF& toWorld)
{
    return QPointF(geometry::lineMidpoint(QPointF(canvas.toScreen(fromWorld)),
                                          QPointF(canvas.toScreen(toWorld))))
         + QPointF(0, 18);
}

/// The hint for stage `placed`: one entry per stage, the last repeating.
const char* stageText(std::initializer_list<const char*> stages, int placed)
{
    const int last = static_cast<int>(stages.size()) - 1;
    return *(stages.begin() + (placed < last ? placed : last));
}

/// Place the next click at `world` and commit once the placement has all
/// its clicks. A click places on press; a drag through a stage places on
/// release (coding_standards 12.2), so both call this.
void placeClick(SketchCanvas& canvas, const QPointF& world)
{
    QPointF at = canvas.snapToGeometry(world);
    if (canvas.ellipseMode() == SketchCanvas::EllipseMode::Arc
        && canvas.previewPointCount() >= 3) {
        // The arc's start and end ride the perimeter of the ellipse already
        // fixed (Aaron, 2026-09-16), never an entity snap somewhere else.
        sketch::Entity fixed;
        if (fixedEllipse(canvas, fixed)) at = onPerimeter(fixed, world);
    }
    canvas.appendStagedPoint(at);

    const int needed = sketch::ellipsePlacementClicks(placementOf(canvas));
    if (canvas.previewPointCount() >= needed) {
        canvas.commitEntity();
    } else {
        canvas.refreshDimFields();
    }
}

}  // namespace

bool EllipseToolHandler::initDimFields(SketchCanvas& canvas)
{
    // One field per stage, named for what that stage fixes. Onshape types
    // the two axes in turn and this mirrors that, rather than offering a
    // number with no meaning yet. Endpoints has no typed stage: its radii
    // are solved, not chosen.
    const int placed = canvas.previewPointCount();
    const char* label = nullptr;
    bool angle = false;
    switch (placementOf(canvas)) {
    case EllipsePlacement::CenterAxes:
    case EllipsePlacement::ThreePoint:
    case EllipsePlacement::Arc:
        angle = placed >= 3;   // the arc's start and sweep
        if (placed == 1)      label = QT_TRANSLATE_NOOP("hobbycad::SketchCanvas", "Major Radius");
        else if (placed == 2) label = QT_TRANSLATE_NOOP("hobbycad::SketchCanvas", "Minor Radius");
        else if (placed == 3) label = QT_TRANSLATE_NOOP("hobbycad::SketchCanvas", "Arc Start");
        else if (placed >= 4) label = QT_TRANSLATE_NOOP("hobbycad::SketchCanvas", "Arc Sweep");
        break;
    case EllipsePlacement::SpanRise:
        if (placed == 1)      label = QT_TRANSLATE_NOOP("hobbycad::SketchCanvas", "Span");
        else if (placed == 2) label = QT_TRANSLATE_NOOP("hobbycad::SketchCanvas", "Rise");
        break;
    case EllipsePlacement::Corner:
        if (placed == 1)      label = QT_TRANSLATE_NOOP("hobbycad::SketchCanvas", "First Axis");
        else if (placed == 2) label = QT_TRANSLATE_NOOP("hobbycad::SketchCanvas", "Second Axis");
        break;
    case EllipsePlacement::Endpoints:
        break;
    }
    if (label) {
        canvas.addDimField(QCoreApplication::translate("hobbycad::SketchCanvas", label), angle);
    }
    return true;
}

QString EllipseToolHandler::hint(const SketchCanvas& canvas) const
{
    const int placed = canvas.previewPointCount();
    const char* s = "";
    switch (placementOf(canvas)) {
    case EllipsePlacement::CenterAxes:
    case EllipsePlacement::Arc:
        s = stageText({
            QT_TRANSLATE_NOOP("hobbycad::SketchCanvas", "Ellipse: click the center"),
            QT_TRANSLATE_NOOP(
                "hobbycad::SketchCanvas",
                "Ellipse: click the major-axis end, or type a radius"),
            QT_TRANSLATE_NOOP(
                "hobbycad::SketchCanvas",
                "Ellipse: click a point the ellipse passes through, or type the minor radius"),
            QT_TRANSLATE_NOOP(
                "hobbycad::SketchCanvas",
                "Elliptical arc: click where the arc starts"),
            QT_TRANSLATE_NOOP(
                "hobbycad::SketchCanvas",
                "Elliptical arc: click where the arc ends (Shift: the long way around)"),
        }, placed);
        break;
    case EllipsePlacement::ThreePoint:
        s = stageText({
            QT_TRANSLATE_NOOP("hobbycad::SketchCanvas",
                              "Ellipse: click one end of the major axis"),
            QT_TRANSLATE_NOOP("hobbycad::SketchCanvas",
                              "Ellipse: click the other end of the major axis"),
            QT_TRANSLATE_NOOP(
                "hobbycad::SketchCanvas",
                "Ellipse: click a point the ellipse passes through, or type the minor radius"),
        }, placed);
        break;
    case EllipsePlacement::SpanRise:
        s = stageText({
            QT_TRANSLATE_NOOP("hobbycad::SketchCanvas",
                              "Span + Rise arc: click one end of the span"),
            QT_TRANSLATE_NOOP("hobbycad::SketchCanvas",
                              "Span + Rise arc: click the other end of the span"),
            QT_TRANSLATE_NOOP("hobbycad::SketchCanvas",
                              "Span + Rise arc: click the apex (the rise), or type it"),
        }, placed);
        break;
    case EllipsePlacement::Corner:
        s = stageText({
            QT_TRANSLATE_NOOP("hobbycad::SketchCanvas", "Corner arc: click the corner"),
            QT_TRANSLATE_NOOP("hobbycad::SketchCanvas",
                              "Corner arc: click where the arc meets the first leg"),
            QT_TRANSLATE_NOOP("hobbycad::SketchCanvas",
                              "Corner arc: click where the arc meets the second leg"),
        }, placed);
        break;
    case EllipsePlacement::Endpoints:
        s = stageText({
            QT_TRANSLATE_NOOP("hobbycad::SketchCanvas",
                              "Endpoints arc: click the first point on the curve"),
            QT_TRANSLATE_NOOP("hobbycad::SketchCanvas",
                              "Endpoints arc: click the second point on the curve"),
            QT_TRANSLATE_NOOP("hobbycad::SketchCanvas", "Endpoints arc: click the center"),
            QT_TRANSLATE_NOOP(
                "hobbycad::SketchCanvas",
                "Endpoints arc: click to set the axis direction (Shift: the long way around)"),
        }, placed);
        break;
    }
    return QCoreApplication::translate("hobbycad::SketchCanvas", s);
}

bool EllipseToolHandler::drawPreview(SketchCanvas& canvas, QPainter& painter)
{
    const int placed = canvas.previewPointCount();
    if (placed == 0) return true;
    const EllipsePlacement mode = placementOf(canvas);
    const bool arcMode = (mode == EllipsePlacement::Arc);

    // Build the entity the clicks so far describe, with the cursor as the
    // point still being placed, through the SAME library rule normalize()
    // commits with. The preview then shows exactly what will be stored.
    sketch::Entity ghost;
    if (!ghostFor(canvas, ghost)) {
        // Nothing to show yet (or, for Endpoints, no ellipse through the two
        // points with that center and axis): the placed clicks and a rubber
        // line from the last one to the cursor.
        canvas.paintPlacedClicks(painter);
        return true;
    }
    const auto& pend = canvas.pendingEntity().points;

    // Elliptical Arc, once the ellipse is fixed: while the START is being
    // chosen no curve is drawn (Aaron, 2026-09-16: hide the ghost; the next
    // click starts the arc ghost), only the fixed points and the cursor,
    // which constrainCursor() keeps on the perimeter.
    const bool choosingStart = arcMode && placed == 3;
    // The arc ghost proper is drawn solid; sizing ghosts stay dashed.
    bool solid = false;
    switch (mode) {
    case EllipsePlacement::Arc:       solid = placed >= 4; break;
    case EllipsePlacement::SpanRise:  solid = placed >= 2; break;
    case EllipsePlacement::Corner:    solid = placed >= 2; break;
    case EllipsePlacement::Endpoints: solid = placed >= 3; break;
    default: break;
    }
    if (!choosingStart) {
        if (mode == EllipsePlacement::Endpoints) {
            // The whole ellipse the axis click is choosing, faint and dashed,
            // under the solid arc between the two endpoints.
            sketch::Entity whole = ghost;
            whole.ellipseStart = 0.0;
            whole.ellipseSweep = 360.0;
            canvas.strokeWorldPolyline(painter, sketch::tessellate(whole, 96), false);
        }
        canvas.strokeWorldPolyline(painter, sketch::tessellate(ghost, 96), solid);
    }
    if (arcMode && placed >= 3) {
        // The cursor as it sits on the perimeter, so the restriction is seen.
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(canvas.toScreen(canvas.currentMouseWorld()), 4, 4);
    }

    const QPointF c(ghost.points[0]);
    const QPointF cursor = canvas.currentMouseWorld();

    // Markers. For the whole-ellipse placements: center and the two axis
    // ends. The stored +minor end is canonical (a quarter turn counter-
    // clockwise from the +major end), but the marker belongs on the side the
    // cursor is on while the minor is chosen, and on the placed third
    // click's side from then on, so it never jumps (Aaron, 2026-09-16). For
    // the arc placements: the clicks themselves, which are the arc's own
    // points.
    painter.setBrush(QColor(0, 120, 215));
    if (mode == EllipsePlacement::CenterAxes || mode == EllipsePlacement::ThreePoint
        || arcMode) {
        const QPointF majorEnd(ghost.points[1]);
        QPointF minorMark(ghost.points[2]);
        if (placed >= 2) {
            const QPointF ref = (placed == 2 || pend.size() < 3) ? cursor : QPointF(pend[2]);
            if (geometry::cross(majorEnd - c, ref - c) < 0.0) minorMark = c - (minorMark - c);
        }
        painter.drawEllipse(canvas.toScreen(c), 3, 3);
        painter.drawEllipse(canvas.toScreen(majorEnd), 3, 3);
        painter.drawEllipse(canvas.toScreen(minorMark), 3, 3);
        painter.setBrush(Qt::NoBrush);

        // The live dimension for the current stage.
        if (canvas.dimFieldCount() > 0) {
            double value = ghost.ellipseSweep;
            QPointF far = cursor;
            if (placed == 1) {
                value = ghost.majorRadius;
                far = majorEnd;
            } else if (placed == 2) {
                value = ghost.minorRadius;
                far = minorMark;
            } else if (placed == 3) {
                value = ghost.ellipseStart;
            }
            canvas.setDimFieldValue(0, value);
            if (canvas.activeDimField() >= 0) {
                canvas.paintDimInputField(painter, dimFieldAnchor(canvas, c, far), 0);
            } else if (placed <= 2) {
                canvas.paintPreviewDimension(painter, canvas.toScreen(c), canvas.toScreen(far),
                                             value);
            }
        }
        return true;
    }

    // Arc placements: the placed clicks, the ghost's center, and the stage's
    // live dimension where one exists.
    canvas.paintPlacedClicks(painter, false);
    painter.drawEllipse(canvas.toScreen(c), 3, 3);

    if (canvas.dimFieldCount() > 0) {
        double value = 0.0;
        QPointF from = c;
        if (mode == EllipsePlacement::SpanRise) {
            if (placed == 1) {
                from = QPointF(pend[0]);
                value = geometry::length(cursor - from);                    // the span
            } else {
                // The rise is the radius across the span, whichever axis it
                // became (a rise taller than the half span swaps them).
                const double halfSpan =
                    geometry::lineLength(QPointF(pend[0]), QPointF(pend[1])) / 2.0;
                value = otherRadius(ghost, halfSpan);
            }
        } else if (mode == EllipsePlacement::Corner) {
            if (placed == 1) {
                value = geometry::length(cursor - c);                       // the first leg
            } else {
                // The second leg is whichever axis the first leg is not.
                value = otherRadius(ghost, geometry::lineLength(QPointF(pend[1]), c));
            }
        }
        canvas.setDimFieldValue(0, value);
        if (canvas.activeDimField() >= 0) {
            canvas.paintDimInputField(painter, dimFieldAnchor(canvas, from, cursor), 0);
        } else {
            canvas.paintPreviewDimension(painter, canvas.toScreen(from),
                                         canvas.toScreen(cursor), value);
        }
    }
    return true;
}

bool EllipseToolHandler::applyCreationMode(SketchCanvas& canvas, int modeValue)
{
    switch (modeValue) {
    case 1:  canvas.setEllipseMode(SketchCanvas::EllipseMode::ThreePoint); break;
    case 2:  canvas.setEllipseMode(SketchCanvas::EllipseMode::Arc);        break;
    case 3:  canvas.setEllipseMode(SketchCanvas::EllipseMode::SpanRise);   break;
    case 4:  canvas.setEllipseMode(SketchCanvas::EllipseMode::Corner);     break;
    case 5:  canvas.setEllipseMode(SketchCanvas::EllipseMode::Endpoints);  break;
    default: canvas.setEllipseMode(SketchCanvas::EllipseMode::CenterAxes); break;
    }
    return true;
}

bool EllipseToolHandler::mousePress(SketchCanvas& canvas, QMouseEvent* event,
                                    const QPointF& world)
{
    // The first click still starts the entity through the canvas, exactly as
    // the staged arc and circle modes do; this stages every click after it.
    if (!canvas.isDrawing()) return false;
    canvas.beginDragDetection(event->pos());   // per STAGE, not per entity
    placeClick(canvas, world);
    return true;
}

bool EllipseToolHandler::mouseRelease(SketchCanvas& canvas, QMouseEvent*, const QPointF& world)
{
    // A click was placed on press. A drag through the stage places the next
    // point here (coding_standards 12.2). Either way the release is
    // consumed: the canvas's shared release path would otherwise finish a
    // dragged entity as a two-point tool, and a second click with a little
    // travel committed a two-point ellipse before the third click came.
    if (!canvas.isDrawing()) return false;
    if (canvas.wasDragged()) placeClick(canvas, world);
    return true;
}

bool EllipseToolHandler::constrainCursor(SketchCanvas& canvas, QPointF& world, bool /*altHeld*/)
{
    // Elliptical Arc, choosing the start and end: the cursor is held to the
    // perimeter of the ellipse the first three clicks fixed, read at the
    // curve's parameter exactly as the click will be. Alt does not release
    // it: a start or end anywhere but on the curve means nothing.
    sketch::Entity fixed;
    if (!fixedEllipse(canvas, fixed)) return false;
    world = onPerimeter(fixed, world);
    return true;
}

bool EllipseToolHandler::keyPress(SketchCanvas& canvas, QKeyEvent* event)
{
    // Same gesture as the circular arcs: Shift flips between the short and
    // the long way around, only once there is an arc to flip. The canvas
    // resets the flip at every entity start.
    if (!canvas.isDrawing() || event->key() != Qt::Key_Shift) return false;
    const EllipsePlacement m = placementOf(canvas);
    const int placed = canvas.previewPointCount();
    const bool flippable = (m == EllipsePlacement::Arc && placed >= 4)
                        || (m == EllipsePlacement::Endpoints && placed >= 3);
    if (!flippable) return false;
    canvas.toggleArcFlip();
    canvas.update();
    return true;
}

bool EllipseToolHandler::updateEntity(SketchCanvas& canvas, const QPointF& pos)
{
    const SketchEntity& e = canvas.pendingEntity();
    if (e.points.empty()) return true;

    // The cursor tracks the NEXT slot after the clicks already placed.
    // previewPointCount() counts placed clicks, so it IS that index.
    const int slot = canvas.previewPointCount();
    const EllipsePlacement m = placementOf(canvas);

    QPointF p = pos;
    const double locked = canvas.lockedDim(0);
    if (locked > 0) {
        const bool radiusStage = slot <= 2 && m != EllipsePlacement::Endpoints;
        const bool acrossFirstAxis =
            (m == EllipsePlacement::SpanRise || m == EllipsePlacement::Corner) && slot == 2
            && e.points.size() >= 2;
        if (radiusStage && acrossFirstAxis) {
            // A rise, or a corner's second leg, is measured PERPENDICULAR to
            // the first axis: place the tracked point at that distance on
            // the cursor's side of the axis.
            const QPointF o = lockOrigin(canvas, e, slot);
            const Point2D axis = Point2D(e.points[1]) - Point2D(e.points[0]);
            if (geometry::isPositiveLength(geometry::length(axis))) {
                const QPointF n = geometry::perpendicular(geometry::normalize(axis));
                const double side = geometry::dot(pos - o, n) < 0.0 ? -1.0 : 1.0;
                p = o + n * (locked * side);
            }
        } else if (radiusStage) {
            const QPointF from = lockOrigin(canvas, e, slot);
            if (geometry::isPositiveLength(geometry::length(pos - from))) {
                p = geometry::applyPolarLock(from, pos, locked, -1.0);
            }
        } else if (m == EllipsePlacement::Arc && slot >= 3) {
            // Stages 4 and 5 lock an ANGLE, not a radius: the tracked point is
            // placed at that parameter on the ellipse the first three clicks
            // fixed. A polar lock here would read the angle as a distance.
            sketch::Entity fixed;
            if (fixedEllipse(canvas, fixed)) {
                double paramDeg = locked;
                if (slot == 4 && e.points.size() >= 4) {
                    // Sweep is measured from the start already placed.
                    paramDeg = sketch::ellipseParamDeg(fixed, Point2D(e.points[3])) + locked;
                }
                p = sketch::ellipsePointAtParamDeg(fixed, paramDeg);
            }
        }
    }

    canvas.setCursorSlot(p);
    return true;
}

bool EllipseToolHandler::normalize(SketchCanvas& canvas, SketchEntity& entity, bool& valid)
{
    if (entity.type != SketchEntityType::Ellipse) {
        return false;   // not ours; fall through to the canvas
    }
    // The click order -> stored layout translation lives in the library
    // (sketch::ellipseFromPlacement) because a wrong layout renders
    // identically on screen and is invisible until the solver or a drag
    // reads the points. Keeping it here would put it beyond the headless
    // tests.
    valid = sketch::ellipseFromPlacement(placementOf(canvas), toClicks(entity),
                                         canvas.arcSlotFlipped(), entity);
    return true;
}

bool EllipseToolHandler::beginEntity(SketchCanvas& canvas, SketchEntity& entity)
{
    (void)canvas;
    entity.type = SketchEntityType::Ellipse;
    return true;
}

}  // namespace hobbycad
