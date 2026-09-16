// =====================================================================
//  src/hobbycad/gui/entityrenderer.cpp
// =====================================================================
//
//  Sketch entity rendering, extracted from SketchCanvas: it draws each
//  committed entity (drawEntity's per-type switch), the per-entity
//  selection handles, and the unconstrained-endpoint dots. Tool-in-progress
//  previews, the grid, and transform/background overlays stay on the canvas;
//  those read live interaction state, not committed geometry.
//
//  Holds a back-reference to its SketchCanvas for the view transform and
//  the model/display state it reads (all read-only) and is a friend of it,
//  mirroring ConstraintRenderer and SnapEngine.
//
//  Part of HobbyCAD.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include "entityrenderer.h"
#include "slotpainter.h"
#include <hobbycad/geometry/utils.h>
#include "screenmath.h"
#include "sketchcanvas.h"

#include <hobbycad/units.h>
#include <hobbycad/sketch/operations.h>

#include <QPainter>
#include <QPainterPath>
#include <QPolygon>
#include <QLineF>
#include <cmath>
#include <set>
#include <utility>

namespace hobbycad {

void EntityRenderer::drawEntity(QPainter& painter, const SketchEntity& entity)
{
    // BLUE while the sketch is not fully constrained, BLACK once it is,
    // RED when the constraints contradict each other. This is the
    // convention Fusion and Onshape both use, in their own words:
    //
    //   Fusion  "When sketch geometry is fully constrained, it changes
    //            from its initial color to black"; "the blue lines ...
    //            are unconstrained".
    //   Onshape "Blue means underconstrained. Black means fully
    //            constrained. Red means a constraint problem."
    //
    // HobbyCAD previously had it inverted (black meant "nothing
    // constrains this yet" and green meant constrained), so anyone
    // arriving from either program read a sketch backwards.
    //
    // Note this is a property of the SKETCH, not of one entity: an entity
    // carrying a constraint is not itself "done" while the sketch can
    // still move. Per-entity feedback is the unconstrained-endpoint dots.
    // An explicit per-entity color (0xRRGGBB, e.g. from DXF or the color
    // picker) is honored, but never at the cost of the error signal: a broken
    // sketch still turns RED. So the precedence is projection, then red, then
    // the explicit color, then the under/fully-constrained convention (which
    // therefore governs only default-colored geometry). Construction and
    // centerline keep their dash STYLE below; their color shows through only
    // when the entity has no color of its own.
    const bool hasOwnColor = entity.color >= 0;
    const QColor ownColor((entity.color >> 16) & 0xFF, (entity.color >> 8) & 0xFF, entity.color & 0xFF);
    QColor base = m_canvas.m_theme.normalGeometry;
    if (entity.projectionSourceId >= 0) {
        // Projected reference geometry: a distinct base color so it reads as
        // geometry brought in from another sketch, not one of this sketch's own
        // primitives. It is driven by its source (fixed, 0 DOF), so the
        // constraint-state coloring below does not apply to it.
        base = m_canvas.m_theme.projectedGeometry;   // violet (reference)
    } else if (m_canvas.m_sketchState == sketch::SketchState::Inconsistent
        || m_canvas.m_sketchState == sketch::SketchState::Failed) {
        base = m_canvas.m_theme.inconsistent;
    } else if (hasOwnColor) {
        base = ownColor;
    } else if (m_canvas.m_sketchFullyConstrained && !entity.isConstruction) {
        base = m_canvas.m_fullyConstrainedColor;
    }

    QPen pen(base, 2);
    if (entity.selected) {
        // Selection thickens the stroke and lightens whatever the state
        // color is, rather than replacing it: selecting something must not
        // hide whether it is constrained. lighter() does nothing to black,
        // so that case is given an explicit gray.
        pen.setColor(base == m_canvas.m_theme.fullyConstrained ? m_canvas.m_theme.selectionFallback
                                                     : base.lighter(140));
        pen.setWidth(3);
    }

    // Construction geometry: dashed line, orange/brown color
    if (entity.isConstruction) {
        if (!hasOwnColor)
            pen.setColor(entity.selected ? m_canvas.m_theme.constructionSelected : m_canvas.m_theme.constructionNormal);
        pen.setStyle(Qt::DashLine);
    }

    // Centerline linetype: dash-dot, in a distinct teal so it reads as a
    // reference axis rather than construction. Checked after construction so
    // an entity flagged both draws as a centerline.
    if (entity.isCenterline) {
        if (!hasOwnColor)
            pen.setColor(entity.selected ? m_canvas.m_theme.centerlineSelected : m_canvas.m_theme.centerlineNormal);
        pen.setStyle(Qt::DashDotLine);
    }

    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    switch (entity.type) {
    case SketchEntityType::Point:
        if (!entity.points.empty()) {
            QPoint p = m_canvas.worldToScreen(entity.points[0]);
            painter.setBrush(pen.color());
            painter.drawEllipse(p, 4, 4);
        }
        break;

    case SketchEntityType::Line:
        if (entity.points.size() >= 2) {
            QPoint p1 = m_canvas.worldToScreen(entity.points[0]);
            QPoint p2 = m_canvas.worldToScreen(entity.points[1]);
            painter.drawLine(p1, p2);
        }
        break;

    case SketchEntityType::Rectangle:
        if (entity.points.size() >= 4) {
            // 4-point rotated rectangle (from 3-point mode)
            QPoint c1 = m_canvas.worldToScreen(entity.points[0]);
            QPoint c2 = m_canvas.worldToScreen(entity.points[1]);
            QPoint c3 = m_canvas.worldToScreen(entity.points[2]);
            QPoint c4 = m_canvas.worldToScreen(entity.points[3]);
            painter.drawLine(c1, c2);
            painter.drawLine(c2, c3);
            painter.drawLine(c3, c4);
            painter.drawLine(c4, c1);
        } else if (entity.points.size() >= 2) {
            // Standard axis-aligned rectangle (2 points = opposite corners)
            QPoint p1 = m_canvas.worldToScreen(entity.points[0]);
            QPoint p2 = m_canvas.worldToScreen(entity.points[1]);
            painter.drawRect(QRect(p1, p2).normalized());
        }
        break;

    case SketchEntityType::Parallelogram:
        if (entity.points.size() >= 4) {
            // 4-point parallelogram
            QPoint c1 = m_canvas.worldToScreen(entity.points[0]);
            QPoint c2 = m_canvas.worldToScreen(entity.points[1]);
            QPoint c3 = m_canvas.worldToScreen(entity.points[2]);
            QPoint c4 = m_canvas.worldToScreen(entity.points[3]);
            painter.drawLine(c1, c2);
            painter.drawLine(c2, c3);
            painter.drawLine(c3, c4);
            painter.drawLine(c4, c1);
        }
        break;

    case SketchEntityType::Circle:
        if (!entity.points.empty()) {
            QPointF centerF = m_canvas.worldToScreenF(entity.points[0]);
            double r = entity.radius * m_canvas.m_zoom;
            painter.drawEllipse(centerF, r, r);

            // Draw center point marker (small cross)
            painter.save();
            int crossSize = 4;
            painter.drawLine(QPointF(centerF.x() - crossSize, centerF.y()), QPointF(centerF.x() + crossSize, centerF.y()));
            painter.drawLine(QPointF(centerF.x(), centerF.y() - crossSize), QPointF(centerF.x(), centerF.y() + crossSize));

            // Draw perimeter point markers for clicked points (points[1+] are perimeter points)
            for (int i = 1; i < entity.points.size(); ++i) {
                QPointF pt = m_canvas.worldToScreenF(entity.points[i]);
                painter.drawLine(QPointF(pt.x() - crossSize, pt.y()), QPointF(pt.x() + crossSize, pt.y()));
                painter.drawLine(QPointF(pt.x(), pt.y() - crossSize), QPointF(pt.x(), pt.y() + crossSize));
            }
            painter.restore();
        }
        break;

    case SketchEntityType::Arc:
        if (!entity.points.empty()) {
            QPointF centerF = m_canvas.worldToScreenF(entity.points[0]);
            double r = entity.radius * m_canvas.m_zoom;
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
        if (!entity.outlineCache.empty()) {
            // Multi-segment (tree) slot: draw the cached swept OUTLINE polygon
            // (a closed CCW polygon from sketch::slotOutline, re-derived on
            // solve). Simple one-element slots fall through to the capsule
            // paths below, which keep their smooth arc caps.
            const auto& o = entity.outlineCache;
            QPainterPath path;
            path.moveTo(m_canvas.worldToScreenF(o[0]));
            for (size_t i = 1; i < o.size(); ++i)
                path.lineTo(m_canvas.worldToScreenF(o[i]));
            path.closeSubpath();
            painter.drawPath(path);
            break;
        }
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
            QPointF start = m_canvas.worldToScreen(startWorld);
            QPointF end = m_canvas.worldToScreen(endWorld);  // Now projected onto arc
            QPointF arcCenter = m_canvas.worldToScreen(arcCenterWorld);
            double screenHalfWidth = halfWidth * m_canvas.m_zoom;
            double screenInnerRadius = innerRadius * m_canvas.m_zoom;
            double screenOuterRadius = outerRadius * m_canvas.m_zoom;

            if (screenInnerRadius > 1 && screenOuterRadius > screenInnerRadius) {
                double startAngle = painterAngleDeg(arcCenter, start);
                double endAngle = painterAngleDeg(arcCenter, end);
                double sweepAngle = endAngle - startAngle;

                // Normalize sweep angle
                sweepAngle = hobbycad::geometry::wrapSweepDeg(sweepAngle);

                // Apply arc flip for > 180 degree arcs
                if (entity.arcFlipped) {
                    sweepAngle = hobbycad::geometry::oppositeSweepDeg(sweepAngle);
                }

                painter.drawPath(arcSlotOutlinePath(arcCenter, startAngle, endAngle, sweepAngle,
                                                     screenInnerRadius, screenOuterRadius, screenHalfWidth));

                // Draw construction line arc along centerline (connecting arc centers of slot ends)
                painter.save();
                QPen constructionPen(QColor(100, 100, 100, 180), 1, Qt::DashLine);
                painter.setPen(constructionPen);
                painter.setBrush(Qt::NoBrush);
                double screenArcRadius = arcRadius * m_canvas.m_zoom;
                QRectF centerlineRect(arcCenter.x() - screenArcRadius, arcCenter.y() - screenArcRadius,
                                      screenArcRadius * 2, screenArcRadius * 2);
                QPainterPath centerlinePath;
                centerlinePath.arcMoveTo(centerlineRect, startAngle);
                centerlinePath.arcTo(centerlineRect, startAngle, sweepAngle);
                painter.drawPath(centerlinePath);
                painter.restore();
            } else {
                // Arc radius too small for proper slot - draw centerline arc with endpoint circles
                double startAngle = painterAngleDeg(arcCenter, start);
                double endAngle = painterAngleDeg(arcCenter, end);
                double sweepAngle = endAngle - startAngle;
                sweepAngle = hobbycad::geometry::wrapSweepDeg(sweepAngle);

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
            QPointF p1 = m_canvas.worldToScreen(entity.points[0]);
            QPointF p2 = m_canvas.worldToScreen(entity.points[1]);
            double halfWidth = entity.radius * m_canvas.m_zoom;

            // Calculate the direction and perpendicular vectors
            QLineF centerLine(p1, p2);
            double len = centerLine.length();
            if (len < 0.001) break;  // Degenerate slot

            // Unit vectors along and perpendicular to the slot axis
            painter.drawPath(linearSlotOutlinePath(p1, p2, halfWidth));

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
        if (!entity.points.empty()) {
            if (entity.ellipseSweep >= 359.999) {
                // Full ellipse: smooth rotated draw. Screen Y grows downward,
                // so a CCW world angle is a CW screen rotation.
                QPointF centerF = m_canvas.worldToScreenF(entity.points[0]);
                double majorR = entity.majorRadius * m_canvas.m_zoom;
                double minorR = entity.minorRadius * m_canvas.m_zoom;
                painter.save();
                painter.translate(centerF);
                painter.rotate(-entity.ellipseRotation);
                painter.drawEllipse(QPointF(0, 0), majorR, minorR);
                painter.restore();
            } else {
                // Elliptical arc: tessellate the parameter range in world space
                // (honoring rotation) and map each point through worldToScreenF,
                // which handles zoom and any view flip.
                const double th = degreesToRadians(entity.ellipseRotation);
                const double ct = std::cos(th), st = std::sin(th);
                const double s0 = degreesToRadians(entity.ellipseStart);
                const double sw = degreesToRadians(entity.ellipseSweep);
                const Point2D c = entity.points[0].xy();
                const int segs = 96;
                QPolygonF poly;
                poly.reserve(segs + 1);
                for (int i = 0; i <= segs; ++i) {
                    const double pr = s0 + sw * (double(i) / segs);
                    const double lx = entity.majorRadius * std::cos(pr);
                    const double ly = entity.minorRadius * std::sin(pr);
                    poly << m_canvas.worldToScreenF(
                        Point2D(c.x + lx * ct - ly * st, c.y + lx * st + ly * ct));
                }
                painter.drawPolyline(poly);
            }
        }
        break;

    case SketchEntityType::Spline:
        if (entity.points.size() >= 2) {
            // Curve math lives in the library, which branches on Bezier vs
            // Catmull-Rom (de Casteljau vs interpolating). The GUI never
            // re-implements either; it strokes the tessellated polyline.
            const std::vector<hobbycad::Point3> tess =
                (entity.splineRational && entity.weights.size() == entity.points.size())
                    ? sketch::tessellateRationalSpline(entity.points, entity.weights, 16)
                    : sketch::tessellateSpline(entity.points, 16, entity.splineBezier, entity.splineClosed);
            if (tess.size() >= 2) {
                QPainterPath path;
                path.moveTo(m_canvas.worldToScreen(QPointF(tess[0].x, tess[0].y)));
                for (std::size_t i = 1; i < tess.size(); ++i)
                    path.lineTo(m_canvas.worldToScreen(QPointF(tess[i].x, tess[i].y)));
                painter.drawPath(path);
            }
        }
        break;

    case SketchEntityType::Text:
        if (!entity.points.empty()) {
            painter.save();
            QPoint p = m_canvas.worldToScreen(entity.points[0]);

            // Apply font properties
            QFont font = painter.font();
            if (!entity.fontFamily.empty()) {
                font.setFamily(QString::fromStdString(entity.fontFamily));
            }
            // Scale font size by zoom level (fontSize is in mm)
            double scaledSize = entity.fontSize * m_canvas.m_zoom;
            font.setPointSizeF(qMax(6.0, scaledSize));  // Minimum 6pt for readability
            font.setBold(entity.fontBold);
            font.setItalic(entity.fontItalic);
            painter.setFont(font);

            // Apply rotation if needed
            if (qAbs(entity.textRotation) > 0.01) {
                painter.translate(p);
                painter.rotate(-entity.textRotation);  // Negative for screen coords
                painter.drawText(QPoint(0, 0), QString::fromStdString(entity.text));
            } else {
                painter.drawText(p, QString::fromStdString(entity.text));
            }
            painter.restore();

            // Draw rotation arm line when selected
            if (entity.selected && entity.points.size() >= 2) {
                painter.save();
                QPen armPen(QColor(30, 160, 30, 160), 1, Qt::DashLine);
                painter.setPen(armPen);
                painter.drawLine(m_canvas.worldToScreen(entity.points[0]),
                                 m_canvas.worldToScreen(entity.points[1]));
                painter.restore();
            }
        }
        break;

    case SketchEntityType::Dimension:
        // Draw dimension line and text
        if (entity.points.size() >= 2) {
            QPoint p1 = m_canvas.worldToScreen(entity.points[0]);
            QPoint p2 = m_canvas.worldToScreen(entity.points[1]);
            painter.setPen(QPen(Qt::blue, 1));
            painter.drawLine(p1, p2);

            // Draw value at midpoint
            QPoint mid((p1.x() + p2.x()) / 2, (p1.y() + p2.y()) / 2 - 10);
            double dist = QLineF(entity.points[0], entity.points[1]).length();
            painter.drawText(mid, QString::fromStdString(formatValueWithUnit(dist, m_canvas.m_displayUnit)));
        }
        break;
    }
}

void EntityRenderer::drawSelectionHandles(QPainter& painter, const SketchEntity& entity)
{
    // Ensure legacy text entities have their rotation handle point
    if (entity.type == SketchEntityType::Text && entity.points.size() == 1)
        SketchCanvas::ensureTextRotationHandle(const_cast<SketchEntity&>(entity));

    // Bezier spline: control polygon (dashed) + amber anchors + blue tangent-
    // handle control points. Every third control point (0,3,6,...) is an anchor;
    // the others are the in/out tangent handles. Hit-testing stays generic, so
    // all control points remain grab-able for dragging.
    if (entity.splineBezier && entity.points.size() >= 4) {
        const int n = static_cast<int>(entity.points.size());
        painter.save();
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(m_canvas.m_theme.handleCenter, 1, Qt::DashLine));
        QPolygon poly;
        for (int i = 0; i < n; ++i) poly << m_canvas.worldToScreen(entity.points[i]);
        painter.drawPolyline(poly);
        const QColor anchorFill(230, 165, 0), anchorPen(138, 100, 0);
        for (int i = 0; i < n; ++i) {
            QPoint p = m_canvas.worldToScreen(entity.points[i]);
            bool selPt = false;
            for (const auto& pr : m_canvas.m_selectedPoints)
                if (pr.first == entity.id && pr.second == i) { selPt = true; break; }
            if (i % 3 == 0) {                       // anchor
                painter.setPen(QPen(anchorPen, 1));
                painter.setBrush(anchorFill);
                painter.drawEllipse(p, 5, 5);
            } else {                                // tangent-handle control point
                painter.setPen(QPen(m_canvas.m_theme.handleCenter, 1));
                painter.setBrush(m_canvas.m_theme.handleCenterFill);
                painter.drawRect(p.x() - 4, p.y() - 4, 8, 8);
            }
            if (selPt) {                            // selection ring
                painter.setPen(QPen(m_canvas.m_theme.selectionFallback, 2));
                painter.setBrush(Qt::NoBrush);
                painter.drawEllipse(p, 8, 8);
            }
        }
        painter.restore();
        return;
    }

    // Arc-based entities use color-coded handles:
    //   handle 0 = center (blue), handle 1 = sweep/angle (green), handle 2 = radius (red)
    bool isArcBased = (entity.type == SketchEntityType::Arc && entity.points.size() >= 3)
                   || (entity.type == SketchEntityType::Slot && entity.points.size() >= 3);
    // Text entities: handle 0 = anchor (blue), handle 1 = rotation (green)
    bool isTextEntity = (entity.type == SketchEntityType::Text && entity.points.size() >= 2);

    for (int i = 0; i < entity.points.size(); ++i) {
        QPoint p = m_canvas.worldToScreen(entity.points[i]);

        if ((isArcBased && i == 1) || (isTextEntity && i == 1)) {
            // Sweep/angle or rotation handle: GREEN
            painter.setPen(QPen(m_canvas.m_theme.handleAngle, 2));
            painter.setBrush(m_canvas.m_theme.handleAngleFill);
        } else if (isArcBased && i == 2) {
            // Radius handle: RED
            painter.setPen(QPen(m_canvas.m_theme.handleRadius, 2));
            painter.setBrush(m_canvas.m_theme.handleRadiusFill);
        } else {
            // Default handle: BLUE/WHITE
            painter.setPen(QPen(m_canvas.m_theme.handleCenter, 1));
            painter.setBrush(m_canvas.m_theme.handleCenterFill);
        }

        painter.drawRect(p.x() - 4, p.y() - 4, 8, 8);
    }
}

bool EntityRenderer::isPointConstrained(int entityId, int pointIndex) const
{
    for (const auto& c : m_canvas.m_constraints) {
        if (!c.enabled) continue;
        for (size_t k = 0; k < c.entityIds.size(); ++k) {
            if (c.entityIds[k] != entityId) continue;
            // A constraint naming the entity but no point applies to the
            // whole entity, which pins its points too.
            if (k >= c.pointIndices.size()) return true;
            if (c.pointIndices[k] == pointIndex) return true;
        }
    }
    return false;
}

void EntityRenderer::drawUnconstrainedPoints(QPainter& painter)
{
    // Fusion marks the end points of LINES, SPLINES and ARCS specifically, so
    // that is what is marked here rather than every point of every shape:
    // a circle's (or arc's) CENTER is not an endpoint, and dotting it would say
    // nothing useful, so an arc marks only its start/end, not point[0].
    if (!m_canvas.m_showUnconstrainedPoints) return;

    painter.save();
    painter.setPen(QPen(m_canvas.m_theme.pointDotPen, 1));
    painter.setBrush(m_canvas.m_theme.pointDotFill);

    // Prefer solver truth: a point is free iff the solver reports it in
    // m_canvas.m_freePoints (accurate through coincident substitution). Fall back to
    // the topological heuristic only when the linked libslvs cannot report
    // free params (m_canvas.m_freePointsValid == false).
    std::set<std::pair<int, int>> freeSet(m_canvas.m_freePoints.begin(), m_canvas.m_freePoints.end());
    for (const auto& e : m_canvas.m_entities) {
        const bool isArc = (e.type == SketchEntityType::Arc);
        if (e.type != SketchEntityType::Line
            && e.type != SketchEntityType::Spline
            && !isArc) {
            continue;
        }
        for (int i = 0; i < static_cast<int>(e.points.size()); ++i) {
            // An arc stores [center, start, end]; point[0] is the center, not
            // an endpoint, so skip it (start/end still get their dots).
            if (isArc && i == 0) continue;
            bool isFree = m_canvas.m_freePointsValid
                ? freeSet.count({e.id, i}) > 0
                : !isPointConstrained(e.id, i);
            if (!isFree) continue;
            painter.drawEllipse(m_canvas.toScreen(QPointF(e.points[i])), 3, 3);
        }
    }
    painter.restore();
}

void EntityRenderer::drawCurvatureComb(QPainter& painter)
{
    if (!m_canvas.m_showCurvatureComb) return;
    const int SAMPLES = 24;   // samples per cubic segment
    painter.save();
    for (const auto& e : m_canvas.m_entities) {
        if (e.type != SketchEntityType::Spline || !e.splineBezier) continue;
        const int n = static_cast<int>(e.points.size());
        const int nseg = e.splineClosed ? (n / 3) : ((n - 1) / 3);
        if (nseg < 1) continue;

        struct Sp { QPointF p; QPointF nrm; double k; };
        std::vector<Sp> sp;
        double maxk = 0.0;
        for (int s = 0; s < nseg; ++s) {
            const int b = 3 * s;
            const QPointF b0(e.points[b]), b1(e.points[b+1]),
                          b2(e.points[b+2]), b3(e.points[(b+3) % n]);
            const int lim = (s == nseg - 1) ? SAMPLES : SAMPLES - 1;   // no seam dup
            for (int i = 0; i <= lim; ++i) {
                const double t = static_cast<double>(i) / SAMPLES, u = 1.0 - t;
                const QPointF d1 = 3.0 * (u*u*(b1-b0) + 2.0*u*t*(b2-b1) + t*t*(b3-b2));
                const QPointF d2 = 6.0 * (u*(b2 - 2.0*b1 + b0) + t*(b3 - 2.0*b2 + b1));
                const double sp2 = d1.x()*d1.x() + d1.y()*d1.y();
                const double speed = std::sqrt(sp2);
                if (speed < geometry::kZeroEps) continue;
                const double k = (d1.x()*d2.y() - d1.y()*d2.x()) / (sp2 * speed);  // signed
                const QPointF nrm(-d1.y()/speed, d1.x()/speed);                    // unit normal
                const QPointF cp = b0*(u*u*u) + b1*(3*u*u*t) + b2*(3*u*t*t) + b3*(t*t*t);
                sp.push_back({ cp, nrm, k });
                maxk = std::max(maxk, std::abs(k));
            }
        }
        if (sp.size() < 2 || maxk < geometry::kZeroEps) continue;
        const double target = 36.0 / m_canvas.m_zoom;   // longest spine ~36 px
        const double scale = target / maxk;

        QPolygon env;
        env.reserve(static_cast<int>(sp.size()));
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(120, 190, 90), 1));   // spines: green
        for (const auto& q : sp) {
            const QPointF tip = q.p + q.nrm * (scale * q.k);   // signed -> flips at inflection
            painter.drawLine(m_canvas.worldToScreen(q.p), m_canvas.worldToScreen(tip));
            env << m_canvas.worldToScreen(tip);
        }
        painter.setPen(QPen(QColor(80, 150, 60), 1.4));  // envelope
        painter.drawPolyline(env);
    }
    painter.restore();
}

}  // namespace hobbycad
