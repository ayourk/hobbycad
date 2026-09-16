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

QString SplineToolHandler::hint(const SketchCanvas& canvas) const
{
    const char* s;
    if (m_bezierMode) {
        s = (m_anchors.empty())
            ? "Bezier: click for a corner, click-drag to pull tangent handles"
            : "Bezier: click/drag to add anchors; Enter, Esc, or right-click to finish";
    } else {
        s = (canvas.previewPointCount() < 2)
            ? "Spline: click to add fit points"
            : "Spline: click to add points; Enter, Esc, or right-click to finish";
    }
    return QCoreApplication::translate("hobbycad::SketchCanvas", s);
}

QString SplineToolHandler::cursorHint(const SketchCanvas& canvas) const
{
    const bool canFinish = m_bezierMode ? (m_anchors.size() >= 2)
                                        : (canvas.previewPointCount() >= 2);
    return canFinish
        ? tr("(Right-click to finish)")
        : (m_bezierMode ? tr("(Click to add points, drag for handles)")
                        : tr("(Click to add points)"));
}

bool SplineToolHandler::applyCreationMode(SketchCanvas&, int modeValue)
{
    // 0 = Bezier pen, 1 = Catmull-Rom, 2 = Rational (weighted) Bezier.
    m_bezierMode = (modeValue == 0 || modeValue == 2);
    m_rational   = (modeValue == 2);
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

bool SplineToolHandler::beginEntity(SketchCanvas&, SketchEntity& entity)
{
    entity.type = SketchEntityType::Spline;
    entity.splineBezier = m_bezierMode;
    entity.splineRational = m_rational;
    if (m_bezierMode) {
        m_anchors.clear();
        m_manual.clear();
        m_dragging = false;
        m_hasDrag = false;
    }
    return true;
}

bool SplineToolHandler::updateEntity(SketchCanvas&, const QPointF&)
{
    // Points are placed on press/release; movement only updates the preview.
    return true;
}

bool SplineToolHandler::normalize(SketchCanvas&, SketchEntity& entity, bool& valid)
{
    if (entity.type != SketchEntityType::Spline) return false;
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
    if (!m_bezierMode) return false;                 // fit-points uses the release path
    if (event && event->button() != Qt::LeftButton) return false;

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
    if (!m_bezierMode || !m_dragging || m_anchors.empty()) return false;
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
    if (!m_bezierMode) {
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

bool SplineToolHandler::drawPreview(SketchCanvas& canvas, QPainter& painter)
{
    if (m_bezierMode) {
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
