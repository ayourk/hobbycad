// =====================================================================
//  src/hobbycad/gui/tools/splinetoolhandler.cpp
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#include "splinetoolhandler.h"
#include "../sketchcanvas.h"
#include <QCoreApplication>
#include <optional>
#include <hobbycad/geometry/utils.h>
#include <hobbycad/sketch/entity.h>
#include <hobbycad/sketch/bezier.h>
#include <hobbycad/sketch/operations.h>
#include <hobbycad/sketch/queries.h>      // tessellate (the conic ghost)

#include <cmath>
#include <vector>
#include <QtMath>
#include <QLineF>
#include <QPen>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QKeyEvent>

namespace hobbycad {

namespace {

using SplineMode = SketchCanvas::SplineMode;

bool isConic(const SketchCanvas& canvas)
{
    return canvas.splineMode() == SplineMode::Conic;
}

/// The Bezier family: the pen, the weighted pen, and the conic, which is
/// stored as one rational Bezier segment.
bool isBezier(const SketchCanvas& canvas)
{
    return canvas.splineMode() != SplineMode::FitPoints;
}

bool isRational(const SketchCanvas& canvas)
{
    return canvas.splineMode() == SplineMode::Rational || isConic(canvas);
}

std::vector<Point2D> toPoints(const std::vector<Point3>& pts)
{
    std::vector<Point2D> out;
    out.reserve(pts.size());
    for (const Point3& p : pts) out.push_back(Point2D(p));
    return out;
}

/// A conic's rho from its clicks: start, end, apex, then the point that
/// sets rho (a click, or the cursor slot); 0.5, a parabola, until then.
double conicRhoOf(const std::vector<Point2D>& p)
{
    return p.size() >= 4 ? sketch::conicRhoFromPoint(p[0], p[1], p[2], p[3]) : 0.5;
}

/// The conic the pending points describe. The one library rule
/// (sketch::conicFromRho) serves the ghost and the commit.
bool conicGhost(const SketchCanvas& canvas, sketch::Entity& out)
{
    const std::vector<Point2D> p = toPoints(canvas.pendingEntity().points);
    if (canvas.previewPointCount() < 3 || p.size() < 3) return false;
    return sketch::conicFromRho(canvas.pendingEntity().id, p[0], p[1], p[2], conicRhoOf(p),
                                out);
}

/// Where the rho-setting point lands for a cursor at `world`: the shoulder
/// on the segment from the chord's midpoint to the apex, which is ON the
/// curve.
QPointF conicShoulderFor(const SketchCanvas& canvas, const QPointF& world)
{
    const std::vector<Point2D> p = toPoints(canvas.pendingEntity().points);
    const double rho = sketch::conicRhoFromPoint(p[0], p[1], p[2], world);
    return sketch::conicShoulder(p[0], p[1], p[2], rho);
}

/// Place the conic's next click at `world`, committing at the fourth. A
/// click places on press; a drag through a stage places on release
/// (coding_standards 12.2), so both call this.
void placeConicClick(SketchCanvas& canvas, const QPointF& world)
{
    QPointF at = canvas.snapToGeometry(world);
    if (canvas.previewPointCount() >= 3) at = conicShoulderFor(canvas, at);
    canvas.appendStagedPoint(at);
    if (canvas.previewPointCount() >= 4) {
        canvas.commitEntity();
    } else {
        canvas.update();
    }
}

}  // namespace

QString SplineToolHandler::hint(const SketchCanvas& canvas) const
{
    const char* s;
    if (isConic(canvas)) {
        const int placed = canvas.previewPointCount();
        if (placed < 1) {
            s = QT_TRANSLATE_NOOP("hobbycad::SketchCanvas", "Conic arc: click the start");
        } else if (placed < 2) {
            s = QT_TRANSLATE_NOOP("hobbycad::SketchCanvas", "Conic arc: click the end");
        } else if (placed < 3) {
            s = QT_TRANSLATE_NOOP(
                "hobbycad::SketchCanvas",
                "Conic arc: click the apex, where the two end tangents meet");
        } else {
            s = QT_TRANSLATE_NOOP(
                "hobbycad::SketchCanvas",
                "Conic arc: slide to set rho (0.5 parabola, less elliptical, "
                "more hyperbolic), click to place");
        }
    } else if (isBezier(canvas)) {
        s = m_anchors.empty()
            ? QT_TRANSLATE_NOOP(
                  "hobbycad::SketchCanvas",
                  "Bezier: click for a corner, click-drag to pull tangent handles")
            : QT_TRANSLATE_NOOP(
                  "hobbycad::SketchCanvas",
                  "Bezier: click/drag to add anchors; Enter, Esc, or right-click to finish");
    } else {
        s = (canvas.previewPointCount() < 2)
            ? QT_TRANSLATE_NOOP("hobbycad::SketchCanvas", "Spline: click to add fit points")
            : QT_TRANSLATE_NOOP(
                  "hobbycad::SketchCanvas",
                  "Spline: click to add points; Enter, Esc, or right-click to finish");
    }
    return QCoreApplication::translate("hobbycad::SketchCanvas", s);
}

QString SplineToolHandler::cursorHint(const SketchCanvas& canvas) const
{
    if (isConic(canvas)) {
        return canvas.previewPointCount() >= 3 ? tr("(Slide to set rho, click to place)")
                                               : tr("(Click to place)");
    }
    const bool pen = isBezier(canvas);
    const bool canFinish = pen ? (m_anchors.size() >= 2) : (canvas.previewPointCount() >= 2);
    return canFinish
        ? tr("(Right-click to finish)")
        : (pen ? tr("(Click to add points, drag for handles)") : tr("(Click to add points)"));
}

bool SplineToolHandler::finishesOnRightClick(const SketchCanvas& canvas) const
{
    return !isConic(canvas);
}

bool SplineToolHandler::applyCreationMode(SketchCanvas& canvas, int modeValue)
{
    // The toolbar's order: 0 Bezier pen, 1 Catmull-Rom, 2 Rational (weighted)
    // Bezier, 3 Conic Arc (Rho), a rational Bezier from four staged clicks.
    switch (modeValue) {
    case 1:  canvas.setSplineMode(SplineMode::FitPoints);     break;
    case 2:  canvas.setSplineMode(SplineMode::Rational);      break;
    case 3:  canvas.setSplineMode(SplineMode::Conic);         break;
    default: canvas.setSplineMode(SplineMode::ControlPoints); break;
    }
    return true;
}

bool SplineToolHandler::keyPress(SketchCanvas& canvas, QKeyEvent* event)
{
    if (!canvas.isDrawing()) return false;
    // Enter, Escape, and right-click all FINISH, keeping the placed anchors,
    // like a line chain (Aaron: consistency). commitEntity() keeps a valid
    // spline (>= 2 anchors) and drops an incomplete one, so Escape no longer
    // silently discards good work.
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter
        || event->key() == Qt::Key_Escape) {
        canvas.commitEntity();
        return true;
    }
    return false;
}

void SplineToolHandler::cancel(SketchCanvas&)
{
    m_anchors.clear();
    m_manual.clear();
    m_dragging = false;
    m_hasDrag = false;
}

namespace {
// Give every non-manual anchor smooth handles (Catmull-Rom -> Bezier). Shared by
// the live placement and the rubber-band preview.
}  // namespace

void SplineToolHandler::recomputeAutoHandles()
{
    sketch::autoBezierHandles(m_anchors, m_manual);
}

bool SplineToolHandler::beginEntity(SketchCanvas& canvas, SketchEntity& entity)
{
    entity.type = SketchEntityType::Spline;
    entity.splineBezier = isBezier(canvas);
    entity.splineRational = isRational(canvas);
    if (entity.splineBezier) {
        m_anchors.clear();
        m_manual.clear();
        m_dragging = false;
        m_hasDrag = false;
    }
    return true;
}

bool SplineToolHandler::updateEntity(SketchCanvas& canvas, const QPointF& pos)
{
    if (isConic(canvas)) {
        // The cursor tracks the slot AFTER the placed clicks, so the ghost
        // and the rho stage see it as the point still being placed.
        if (!canvas.pendingEntity().points.empty()) canvas.setCursorSlot(pos);
        return true;
    }
    // Points are placed on press/release; movement only updates the preview.
    return true;
}

bool SplineToolHandler::normalize(SketchCanvas& canvas, SketchEntity& entity, bool& valid)
{
    if (entity.type != SketchEntityType::Spline) return false;
    if (isConic(canvas)) {
        // Commits one exact rational cubic segment with the rho stored on it
        // (conicFromRho), the same rule the ghost was drawn with.
        const std::vector<Point2D> pts = toPoints(entity.points);
        sketch::Entity conic;
        valid = pts.size() >= 3
             && sketch::conicFromRho(entity.id, pts[0], pts[1], pts[2], conicRhoOf(pts), conic);
        if (valid) static_cast<sketch::Entity&>(entity) = conic;
        return true;
    }
    if (entity.splineBezier) {
        // Build the cubic control polygon from the authored anchors.
        const std::vector<Point2D> poly = sketch::bezierControlPolygon(m_anchors);
        entity.points.clear();
        entity.points.reserve(poly.size());
        for (const Point2D& p : poly) entity.points.push_back({ p.x, p.y, 0.0 });
        if (entity.splineRational)
            entity.weights = sketch::bezierControlPolygonWeights(m_anchors);  // all 1 until edited
        valid = m_anchors.size() >= 2;   // at least one segment
        return true;
    }
    valid = entity.points.size() >= 2;   // Catmull-Rom needs >= 2 fit points
    return true;
}

bool SplineToolHandler::mousePress(SketchCanvas& canvas, QMouseEvent* event,
                                   const QPointF& world)
{
    if (event && event->button() != Qt::LeftButton) return false;
    if (isConic(canvas)) {
        // Four clicks staged on PRESS, the ellipse tool's way.
        if (event) canvas.beginDragDetection(event->pos());   // per stage, not per entity
        if (!canvas.isDrawing()) {
            canvas.beginPlacement(canvas.snapToGeometry(world));
            canvas.update();
        } else {
            placeConicClick(canvas, world);
        }
        return true;
    }
    if (!isBezier(canvas)) return false;             // fit-points uses the release path

    const QPointF a = canvas.snapToGeometry(world);
    if (!canvas.isDrawing()) {
        canvas.beginPlacement(a);   // startEntity -> beginEntity clears m_anchors
    }
    sketch::BezierAnchor anc;
    anc.pos = { a.x(), a.y() };      // smooth auto handles by default; a drag overrides
    m_anchors.push_back(anc);
    m_manual.push_back(false);
    m_anchorPos = a;
    m_dragging = true;
    m_hasDrag = false;
    recomputeAutoHandles();          // 2 points already reveal handles + an arc
    canvas.update();
    return true;
}

bool SplineToolHandler::mouseMove(SketchCanvas& canvas, QMouseEvent*,
                                  const QPointF& world)
{
    if (isConic(canvas)) return false;               // the canvas's move path -> updateEntity()
    if (!isBezier(canvas) || !m_dragging || m_anchors.empty()) return false;
    const QPointF d = world - m_anchorPos;
    const double len = geometry::length(d);
    sketch::BezierAnchor& a = m_anchors.back();
    if (len > geometry::kDegenerateLen) {
        m_hasDrag = true;
        if (!m_manual.empty()) m_manual.back() = true;   // dragged: keep this handle
        // Angle = tangent (G1); length = per-side curvature. Symmetric pull:
        // out handle leads the next segment, in handle mirrors it. The first
        // anchor (a path start) still shows both handles so it can be arced.
        a.hasOut = true;
        a.outHandle = { m_anchorPos.x() + d.x(), m_anchorPos.y() + d.y() };
        a.hasIn = true;
        a.inHandle = { m_anchorPos.x() - d.x(), m_anchorPos.y() - d.y() };
    } else {
        // No drag: fall back to smooth auto handles for this anchor.
        if (!m_manual.empty()) m_manual.back() = false;
        m_hasDrag = false;
        recomputeAutoHandles();
    }
    canvas.update();
    return true;
}

bool SplineToolHandler::mouseRelease(SketchCanvas& canvas, QMouseEvent*,
                                     const QPointF& world)
{
    if (!canvas.isDrawing()) return false;
    // Conic: a click was placed on press; a drag through the stage places the
    // next point here (coding_standards 12.2). The release is consumed
    // either way, so the canvas's click-drag finish cannot end it early.
    if (isConic(canvas)) {
        if (canvas.wasDragged()) placeConicClick(canvas, world);
        return true;
    }
    if (!isBezier(canvas)) {
        // Catmull-Rom: each release adds a fit point; right-click finishes.
        canvas.appendPlacementPoint(canvas.snapToGeometry(world));
        canvas.update();
        return true;
    }
    // Pen: the anchor was placed on press; the drag (if any) set its handles.
    m_dragging = false;
    canvas.update();
    return true;
}

bool SplineToolHandler::constrainCursor(SketchCanvas& canvas, QPointF& world, bool /*altHeld*/)
{
    // Conic, choosing rho: the cursor rides the segment from the chord's
    // midpoint to the apex; the shoulder it lands on is ON the curve. Alt
    // does not release it: rho is a fraction of that line, nothing else.
    if (!isConic(canvas) || !canvas.isDrawing()) return false;
    if (canvas.previewPointCount() < 3 || canvas.pendingEntity().points.size() < 3) {
        return false;
    }
    world = conicShoulderFor(canvas, world);
    return true;
}

bool SplineToolHandler::drawPreview(SketchCanvas& canvas, QPainter& painter)
{
    if (isConic(canvas)) {
        const int placed = canvas.previewPointCount();
        if (!canvas.isDrawing() || placed == 0) return true;
        sketch::Entity ghost;
        const bool choosingRho = placed >= 3 && conicGhost(canvas, ghost);
        // Ends and apex being chosen: a rubber line from the last click.
        canvas.paintPlacedClicks(painter, !choosingRho);
        if (!choosingRho) return true;
        // Rho being chosen: the two tangent legs dashed, the curve solid,
        // the shoulder ringed where the cursor rides the apex line.
        const std::vector<Point2D> p = toPoints(canvas.pendingEntity().points);
        painter.drawLine(canvas.toScreen(p[0]), canvas.toScreen(p[2]));
        painter.drawLine(canvas.toScreen(p[2]), canvas.toScreen(p[1]));
        canvas.strokeWorldPolyline(painter, sketch::tessellate(ghost, 96), true);
        const Point2D sh = sketch::conicShoulder(p[0], p[1], p[2], conicRhoOf(p));
        painter.drawEllipse(canvas.toScreen(sh), 4, 4);
        return true;
    }
    if (isBezier(canvas)) {
        if (!canvas.isDrawing() || m_anchors.empty()) return true;
        // Rubber-band: while moving toward the next click (not dragging a handle),
        // append a provisional anchor at the cursor and re-smooth, so the curve
        // bends live to the cursor (Fusion's fit-point feel).
        std::vector<sketch::BezierAnchor> anchors = m_anchors;
        std::vector<bool> manual = m_manual;
        bool hasProvisional = false;
        if (!m_dragging) {
            const QPointF c = canvas.currentMouseWorld();
            sketch::BezierAnchor prov;
            prov.pos = { c.x(), c.y() };
            anchors.push_back(prov);
            manual.push_back(false);
            sketch::autoBezierHandles(anchors, manual);
            hasProvisional = true;
        }
        const std::vector<Point2D> poly = sketch::bezierControlPolygon(anchors);
        if (poly.size() >= 4) {
            std::vector<Point3> ctrl;
            ctrl.reserve(poly.size());
            for (const Point2D& p : poly) ctrl.push_back({ p.x, p.y, 0.0 });
            const std::vector<Point3> tess = sketch::tessellateSpline(ctrl, 16, true);
            if (tess.size() >= 2) {
                QPainterPath path;
                path.moveTo(canvas.toScreen(QPointF(tess[0].x, tess[0].y)));
                for (std::size_t i = 1; i < tess.size(); ++i)
                    path.lineTo(canvas.toScreen(QPointF(tess[i].x, tess[i].y)));
                painter.drawPath(path);
            }
        }
        // Anchors (amber) + tangent handles (blue); the provisional cursor anchor
        // (last, when present) is drawn hollow.
        for (std::size_t ai = 0; ai < anchors.size(); ++ai) {
            const sketch::BezierAnchor& a = anchors[ai];
            const bool provisional = hasProvisional && ai + 1 == anchors.size();
            const QPointF ap = canvas.toScreen(QPointF(a.pos.x, a.pos.y));
            painter.setPen(QPen(QColor(60, 120, 215), 1));
            if (a.hasOut) {
                const QPointF h = canvas.toScreen(QPointF(a.outHandle.x, a.outHandle.y));
                painter.drawLine(ap, h);
                painter.setBrush(QColor(60, 120, 215));
                painter.drawRect(QRectF(h.x() - 3, h.y() - 3, 6, 6));
            }
            if (a.hasIn) {
                const QPointF h = canvas.toScreen(QPointF(a.inHandle.x, a.inHandle.y));
                painter.drawLine(ap, h);
                painter.setBrush(QColor(60, 120, 215));
                painter.drawRect(QRectF(h.x() - 3, h.y() - 3, 6, 6));
            }
            painter.setPen(QPen(QColor(138, 100, 0), 1));
            painter.setBrush(provisional ? QBrush(Qt::NoBrush) : QBrush(QColor(230, 165, 0)));
            painter.drawEllipse(ap, 4, 4);
        }
        return true;
    }

    // ---- Fit-points (Catmull-Rom) preview ----
    if (canvas.previewPointCount() != 0) {
        QVector<QPointF> allPoints = canvas.previewPoints();
        allPoints.append(canvas.currentMouseWorld());
        QVector<QPointF> screenPoints;
        for (const QPointF& wp : allPoints) screenPoints.append(canvas.toScreen(wp));

        QPainterPath path;
        path.moveTo(screenPoints[0]);
        if (screenPoints.size() == 2) {
            path.lineTo(screenPoints[1]);
        } else {
            for (int i = 0; i < screenPoints.size() - 1; ++i) {
                QPointF p0, p1, p2, p3;
                p1 = screenPoints[i];
                p2 = screenPoints[i + 1];
                p0 = (i == 0) ? p1 : screenPoints[i - 1];
                p3 = (i == screenPoints.size() - 2) ? p2 : screenPoints[i + 2];
                const QPointF c1 = p1 + (p2 - p0) / 6.0;
                const QPointF c2 = p2 - (p3 - p1) / 6.0;
                path.cubicTo(c1, c2, p2);
            }
        }
        painter.drawPath(path);
        painter.setBrush(QColor(0, 120, 215));
        for (const QPointF& sp : screenPoints) painter.drawEllipse(sp, 3, 3);
    }
    return true;
}

}  // namespace hobbycad
