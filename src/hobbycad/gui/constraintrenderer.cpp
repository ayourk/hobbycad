// =====================================================================
//  src/hobbycad/gui/constraintrenderer.cpp
// =====================================================================
//
//  The constraint-rendering subsystem, extracted from SketchCanvas: it
//  draws every constraint on the sketch: dimension lines and their
//  labels, the geometric-constraint glyph badges (the shapes themselves
//  come from constraintglyphs.h), angle arcs, the group indicator. It lays
//  out label positions to avoid overlaps, and hit-tests the glyph badges.
//
//  It holds a back-reference to its SketchCanvas for the geometry it needs
//  (coordinate transforms, entity/constraint lookup, display unit,
//  selection and visibility state) and is a friend of it. The per-frame
//  scratch state (label nudges, glyph chip rects, glyph slot counts) lives
//  here now, not on the canvas.
//
//  Part of HobbyCAD.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include "constraintrenderer.h"
#include <hobbycad/geometry/utils.h>
#include "sketchcanvas.h"
#include "constraintglyphs.h"

#include <hobbycad/units.h>
#include <hobbycad/sketch/constraint.h>

#include <QPainter>
#include <QFontMetricsF>
#include <QLineF>
#include <cmath>

namespace hobbycad {

void ConstraintRenderer::drawPreviewDimension(QPainter& painter, const QPoint& p1, const QPoint& p2, double value)
{
    // Draw dimension label below the preview line during entity creation
    // Uses same style as instruction text (blue text on white background, no border)
    painter.save();

    // Calculate midpoint and line angle
    QPointF midPoint = (QPointF(p1) + QPointF(p2)) / 2.0;
    double dx = p2.x() - p1.x();
    double dy = p2.y() - p1.y();
    double angle = radiansToDegrees(std::atan2(dy, dx));

    // Keep text readable - flip if pointing left
    bool flipped = (angle > 90 || angle < -90);
    if (flipped) {
        angle += 180;
    }

    // Format the dimension value with unit conversion and suffix
    QString dimText = QString::fromStdString(formatValueWithUnit(value, m_canvas.m_displayUnit));

    // Black text on solid white background (opaque so preview geometry doesn't bleed)
    painter.setPen(m_canvas.m_theme.labelText);
    QFont font = painter.font();
    font.setPointSize(9);
    painter.setFont(font);

    QFontMetrics fm(font);
    QRect textRect = fm.boundingRect(dimText);

    // Position below the line (positive Y offset in rotated coords)
    painter.translate(midPoint);
    painter.rotate(angle);

    // Offset below the line
    QPointF offset(0, textRect.height() + 6);
    QRectF labelRect(-textRect.width() / 2.0 - 3, offset.y() - textRect.height(),
                     textRect.width() + 6, textRect.height() + 2);

    painter.fillRect(labelRect, m_canvas.m_theme.labelFill);

    // Draw the text
    painter.drawText(labelRect, Qt::AlignCenter, dimText);

    painter.restore();
}

void ConstraintRenderer::drawDimensionLabel(QPainter& painter, const QPointF& position, double value)
{
    // Draw a dimension label at a specific position
    // Black text on solid white background
    painter.save();

    QString dimText = QString::fromStdString(formatValueWithUnit(value, m_canvas.m_displayUnit));
    painter.setPen(m_canvas.m_theme.labelText);
    QFont font = painter.font();
    font.setPointSize(9);
    painter.setFont(font);

    QFontMetrics fm(font);
    QRect textRect = fm.boundingRect(dimText);

    QRectF labelRect(position.x() - textRect.width() / 2.0 - 3,
                     position.y() - textRect.height() / 2.0 - 1,
                     textRect.width() + 6, textRect.height() + 2);

    painter.fillRect(labelRect, m_canvas.m_theme.labelFill);
    painter.drawText(labelRect, Qt::AlignCenter, dimText);

    painter.restore();
}

void ConstraintRenderer::drawArcDimensionLabel(QPainter& painter, const QPointF& position, double arcLength, double angleDeg)
{
    // Draw a two-line dimension label showing arc length and angle in degrees
    // Black text on solid white background
    painter.save();

    QString line1 = QString::fromStdString(formatValueWithUnit(arcLength, m_canvas.m_displayUnit));
    QString line2 = QString::fromStdString(formatAngle(std::abs(angleDeg)));

    painter.setPen(m_canvas.m_theme.labelText);
    QFont font = painter.font();
    font.setPointSize(9);
    painter.setFont(font);

    QFontMetrics fm(font);
    int line1Width = fm.horizontalAdvance(line1);
    int line2Width = fm.horizontalAdvance(line2);
    int maxWidth = std::max(line1Width, line2Width);
    int lineHeight = fm.height();
    int totalHeight = lineHeight * 2 + 2;

    QRectF labelRect(position.x() - maxWidth / 2.0 - 4,
                     position.y() - totalHeight / 2.0 - 2,
                     maxWidth + 8, totalHeight + 4);

    painter.fillRect(labelRect, m_canvas.m_theme.labelFill);

    // Draw first line (arc length with unit)
    QRectF line1Rect(labelRect.x(), labelRect.y() + 2, labelRect.width(), lineHeight);
    painter.drawText(line1Rect, Qt::AlignCenter, line1);

    // Draw second line (angle with degree symbol)
    QRectF line2Rect(labelRect.x(), labelRect.y() + lineHeight + 2, labelRect.width(), lineHeight);
    painter.drawText(line2Rect, Qt::AlignCenter, line2);

    painter.restore();
}

void ConstraintRenderer::resolveConstraintLabelOverlaps()
{
    m_labelNudgeOffsets.clear();

    // Collect screen-space bounding rects for all visible dimensional constraint labels
    struct LabelInfo {
        int constraintId = 0;
        QRectF screenRect;
    };
    QVector<LabelInfo> labels;

    // Use a consistent font for measurement
    QFont font = this->m_canvas.font();
    font.setPointSize(9);
    QFontMetricsF fm(font);

    for (const SketchConstraint& c : m_canvas.m_constraints) {
        if (!c.enabled || !c.labelVisible) continue;

        // Only dimensional constraints have labels that can collide
        if (c.type != ConstraintType::Distance && c.type != ConstraintType::Radius
            && c.type != ConstraintType::Diameter && c.type != ConstraintType::Angle
            && c.type != ConstraintType::FixedAngle)
            continue;

        // Estimate text content and size
        QString text;
        if (c.type == ConstraintType::Angle || c.type == ConstraintType::FixedAngle)
            text = QString::fromStdString(formatAngle(c.value));
        else if (c.type == ConstraintType::Radius)
            text = QStringLiteral("R") + QString::fromStdString(formatValueWithUnit(c.value, m_canvas.m_displayUnit));
        else if (c.type == ConstraintType::Diameter)
            text = QStringLiteral("Ø") + QString::fromStdString(formatValueWithUnit(c.value, m_canvas.m_displayUnit));
        else
            text = QString::fromStdString(formatValueWithUnit(c.value, m_canvas.m_displayUnit));

        if (!c.isDriving) text = QStringLiteral("(") + text + QStringLiteral(")");

        double textW = fm.horizontalAdvance(text) + 4.0;
        double textH = fm.height() + 2.0;

        // Compute screen-space center of label
        QPointF labelCenter = m_canvas.worldToScreen(c.labelPosition).toPointF();

        QRectF rect(labelCenter.x() - textW / 2.0,
                    labelCenter.y() - textH / 2.0,
                    textW, textH);
        labels.append({c.id, rect});
    }

    // Greedy displacement: run up to 3 passes to resolve overlaps
    for (int pass = 0; pass < 3; ++pass) {
        bool anyOverlap = false;
        for (int i = 0; i < labels.size(); ++i) {
            for (int j = i + 1; j < labels.size(); ++j) {
                if (!labels[i].screenRect.intersects(labels[j].screenRect))
                    continue;

                anyOverlap = true;
                QPointF ci = labels[i].screenRect.center();
                QPointF cj = labels[j].screenRect.center();
                QPointF delta = cj - ci;
                double dist = geometry::length(delta);
                if (dist < 1.0) delta = QPointF(0, -1); // default: push apart vertically
                else delta /= dist; // normalize

                // Compute overlap amount
                double overlapX = std::min(labels[i].screenRect.right(), labels[j].screenRect.right())
                                - std::max(labels[i].screenRect.left(), labels[j].screenRect.left());
                double overlapY = std::min(labels[i].screenRect.bottom(), labels[j].screenRect.bottom())
                                - std::max(labels[i].screenRect.top(), labels[j].screenRect.top());
                double nudgeAmount = std::min(overlapX, overlapY) / 2.0 + 2.0;

                QPointF nudge = delta * nudgeAmount;
                labels[i].screenRect.translate(-nudge);
                labels[j].screenRect.translate(nudge);

                // Accumulate nudge offsets
                m_labelNudgeOffsets[labels[i].constraintId] += (-nudge);
                m_labelNudgeOffsets[labels[j].constraintId] += nudge;
            }
        }
        if (!anyOverlap) break;
    }

    // Expose the final (nudged) label rects so glyph chips can avoid them.
    m_labelRects.clear();
    for (const auto& l : labels) m_labelRects.append(l.screenRect.toAlignedRect());
}

// Find a free screen slot for a glyph chip: prefer just down-right of the
// anchor, else fan out through a growing ring of 8 directions until the slot
// clears every already-occupied rect (other chips, dimension labels,
// coincidence markers). Keeps clusters tidy and avoids covering other glyphs.
// [Aaron glyph rule 2026-09-09] Full geometry-avoidance ("don't hide the
// drawing") is a follow-on; this handles glyph-vs-glyph overlap.
static QRect findFreeGlyphSlot(const QPoint& sp, int size,
                               const QVector<QRect>& occupied,
                               const QVector<QRect>& geometry)
{
    // Hard rule: a chip may never overlap another placed glyph (chips, labels,
    // markers, the pivot star, the snap indicator). Soft rule: among the slots
    // that clear every hard glyph, prefer the one covering the least drawing and
    // nearest the anchor, so chips fan into empty canvas without flying off it.
    auto hardFree = [&](const QRect& r) {
        for (const QRect& o : occupied) if (r.intersects(o)) return false;
        return true;
    };
    auto geomOverlap = [&](const QRect& r) {
        int n = 0;
        for (const QRect& g : geometry) if (r.intersects(g)) ++n;
        return n;
    };
    const QRect first(sp.x() + 8, sp.y() + 8, size, size);
    QRect best; bool have = false; double bestScore = 1e18;
    auto consider = [&](const QRect& r) {
        if (!hardFree(r)) return;
        const double dx = r.x() - first.x(), dy = r.y() - first.y();
        const double score = geomOverlap(r) * 1.0e6 + (dx * dx + dy * dy);
        if (score < bestScore) { bestScore = score; best = r; have = true; }
    };
    consider(first);
    static const double deg[] = { 0, -45, 45, -90, 90, -135, 135, 180 };
    for (int radius = size + 4; radius <= size * 8; radius += size + 4) {
        for (double a : deg) {
            const double rad = a * 3.14159265358979 / 180.0;
            consider(QRect(int(sp.x() + 8 + radius * std::cos(rad)),
                           int(sp.y() + 8 + radius * std::sin(rad)), size, size));
        }
    }
    return have ? best : first;   // last resort: overlap rather than fly off-screen
}

void ConstraintRenderer::drawConstraints(QPainter& painter)
{
    // Resolve label overlaps before drawing
    resolveConstraintLabelOverlaps();

    // Rebuild glyph chip layout for this frame
    m_constraintGlyphRects.clear();
    m_placedGlyphChips.clear();
    for (const QRect& r : m_labelRects) m_placedGlyphChips.append(r);  // chips avoid labels
    for (const QRect& r : m_reservedGlyphRects) m_placedGlyphChips.append(r);  // pivot/snap (hard)

    for (const SketchConstraint& constraint : m_canvas.m_constraints) {
        if (!constraint.enabled) continue;

        // A point-to-point coincident is badge-less by design (an untied
        // endpoint carries a white dot, so a dot-less point already reads as
        // joined). Its optional "Coincidence markers" indicator is handled
        // here, BEFORE the show-toggle gates, so it is INDEPENDENT of
        // "Show Constraints": enabling just that option shows the markers.
        // [Aaron 2026-09-09] Point-to-CURVE coincidence is PointOnLine/
        // PointOnCircle (not this), so it keeps its glyph via drawConstraint.
        if (constraint.type == ConstraintType::Coincident
            && constraint.entityIds.size() == 2
            && constraint.pointIndices.size() == 2) {
            if (m_canvas.m_showCoincidenceMarkers) {
                drawCoincidenceMarker(painter, constraint);
                const SketchEntity* ce = m_canvas.entityById(constraint.entityIds[0]);
                if (ce) {
                    const int ci = constraint.pointIndices[0];
                    if (ci >= 0 && ci < static_cast<int>(ce->points.size())) {
                        const QPoint mp = m_canvas.worldToScreen(ce->points[ci]);
                        m_placedGlyphChips.append(QRect(mp.x() - 6, mp.y() - 6, 12, 12));
                    }
                }
            }
            continue;
        }
        // labelVisible only hides dimensional labels; geometric glyph
        // chips are always shown (createGeometricConstraint sets
        // labelVisible = false since there is no text label to place).
        if (!constraint.labelVisible
            && sketch::isDimensionalConstraint(constraint.type)) continue;

        // "Show Constraints" hides the geometric glyphs only. Dimensions
        // keep their labels: they carry numbers, and clearing the canvas to
        // read those numbers is the usual reason for reaching for this.
        if (!m_canvas.m_showConstraints
            && !sketch::isDimensionalConstraint(constraint.type)) continue;

        // "Show Dimensions" is the mirror toggle: it hides the dimensional
        // labels and leaves the geometric glyphs alone.
        if (!m_canvas.m_showDimensions
            && sketch::isDimensionalConstraint(constraint.type)) continue;


        drawConstraint(painter, constraint);
    }

    // Draw inline constraint edit overlay
    if (m_canvas.m_inlineEditActive) {
        const SketchConstraint* ec = m_canvas.constraintById(m_canvas.m_inlineEditConstraintId);
        if (ec && ec->enabled) {
            auto pos = computeConstraintLabelPosition(*ec);
            if (pos.found)
                drawInlineConstraintEdit(painter, pos.textCenter, pos.prefix);
        }
    }
}

void ConstraintRenderer::drawCoincidenceMarker(QPainter& painter, const SketchConstraint& constraint)
{
    const SketchEntity* e = m_canvas.entityById(constraint.entityIds[0]);
    if (!e) return;
    const int idx = constraint.pointIndices[0];
    if (idx < 0 || idx >= static_cast<int>(e->points.size())) return;
    const QPointF sp = m_canvas.worldToScreen(e->points[idx]).toPointF();
    const QColor col = constraint.selected ? m_canvas.m_theme.constraintSelected
                                           : m_canvas.m_theme.constraintDriving;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    // A small ring with a filled center reads as "these points are one",
    // distinct from the white dot that marks an UNconstrained endpoint.
    painter.setPen(QPen(col, 1.5));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(sp, 5.0, 5.0);
    painter.setPen(Qt::NoPen);
    painter.setBrush(col);
    painter.drawEllipse(sp, 1.6, 1.6);
    painter.restore();
}

void ConstraintRenderer::drawConstraint(QPainter& painter, const SketchConstraint& constraint)
{
    QColor constraintColor;
    int penWidth = 1;
    const bool isRedundantCandidate =
        constraint.isDriving && m_canvas.m_redundantCandidates.contains(constraint.id);

    if (!constraint.isDriving) {
        constraintColor = m_canvas.m_theme.constraintReference;  // Driven (reference) dimensions
    } else if (isRedundantCandidate) {
        // Magenta, not red: red already means "this constraint failed", and a
        // redundancy candidate has not failed; it is satisfied, and so is
        // every other one. What is wrong is the SET, which is why more than
        // one is usually marked and why the person chooses which to remove.
        constraintColor = m_canvas.m_theme.constraintRedundant;
        penWidth = 2;
    } else if (constraint.satisfied) {
        constraintColor = m_canvas.m_theme.constraintDriving;    // satisfied driving constraints
    } else {
        constraintColor = m_canvas.m_theme.constraintFailed;                 // failed constraints
    }

    QPen pen(constraintColor, penWidth);
    if (constraint.selected) {
        pen.setColor(m_canvas.m_theme.constraintSelected);  // selected
        pen.setWidth(2);
    }
    painter.setPen(pen);

    switch (constraint.type) {
    case ConstraintType::Distance:
        drawDistanceConstraint(painter, constraint);
        break;
    case ConstraintType::Radius:
    case ConstraintType::Diameter:
        drawRadialConstraint(painter, constraint);
        break;
    case ConstraintType::Angle:
        drawAngleConstraint(painter, constraint);
        break;
    case ConstraintType::FixedAngle:
        drawFixedAngleConstraint(painter, constraint);
        break;
    case ConstraintType::Horizontal:
    case ConstraintType::Vertical:
    case ConstraintType::Parallel:
    case ConstraintType::Perpendicular:
    case ConstraintType::Coincident:
    case ConstraintType::Equal:
    case ConstraintType::Tangent:
    case ConstraintType::Midpoint:
    case ConstraintType::Symmetric:
    case ConstraintType::Concentric:
    case ConstraintType::Collinear:
    case ConstraintType::PointOnLine:
    case ConstraintType::PointOnCircle:
    case ConstraintType::FixedPoint:
        drawGeometricConstraint(painter, constraint);
        break;
    default:
        break;
    }
}

void ConstraintRenderer::drawDistanceConstraint(QPainter& painter, const SketchConstraint& constraint)
{
    if (constraint.entityIds.size() < 2) return;

    // Get constraint endpoints (geometry points being dimensioned)
    QPointF p1, p2;
    if (!m_canvas.getConstraintEndpoints(constraint, p1, p2)) return;

    QPointF sp1 = m_canvas.worldToScreen(p1).toPointF();
    QPointF sp2 = m_canvas.worldToScreen(p2).toPointF();
    QPointF labelCenter = m_canvas.worldToScreen(constraint.labelPosition).toPointF();

    // ---- Dimension line direction and offset ----------------------------
    QPointF along = sp2 - sp1;
    double len = geometry::length(along);
    if (len < 1.0) return;

    QPointF dir = along / len;                      // unit direction
    QPointF perp(-dir.y(), dir.x());                // perpendicular (outward)

    // Angle of the dimension line (for rotating text)
    double angleDeg = radiansToDegrees(std::atan2(along.y(), along.x()));
    // Flip so text is always readable (never upside-down)
    if (angleDeg > 90.0)  angleDeg -= 180.0;
    if (angleDeg < -90.0) angleDeg += 180.0;

    // Project the label position onto the perpendicular to get the offset
    QPointF labelDelta = labelCenter - sp1;
    double offset = labelDelta.x() * perp.x() + labelDelta.y() * perp.y();

    // Projected endpoints on the dimension line (parallel, offset from geometry)
    QPointF d1 = sp1 + perp * offset;
    QPointF d2 = sp2 + perp * offset;

    // ---- Extension (witness) lines --------------------------------------
    double extOvershoot = 3.0;
    double extGap = 2.0;
    QPen extPen(painter.pen().color(), 1, Qt::SolidLine);
    painter.setPen(extPen);

    QPointF extDir = (offset >= 0) ? perp : -perp;
    double absOffset = std::abs(offset);
    if (absOffset > extGap + 1.0) {
        painter.drawLine(sp1 + extDir * extGap, sp1 + extDir * (absOffset + extOvershoot));
        painter.drawLine(sp2 + extDir * extGap, sp2 + extDir * (absOffset + extOvershoot));
    }

    // ---- Value text -----------------------------------------------------
    QString text = QString::fromStdString(formatValueWithUnit(constraint.value, m_canvas.m_displayUnit));
    if (!constraint.isDriving) {
        text = QStringLiteral("(") + text + QStringLiteral(")");
    }
    QFontMetricsF fm(painter.font());
    double textWidth = fm.horizontalAdvance(text);
    double textHeight = fm.height();

    QPointF dimMid = (d1 + d2) / 2.0;
    // Apply label collision nudge offset
    auto nudgeIt = m_labelNudgeOffsets.find(constraint.id);
    if (nudgeIt != m_labelNudgeOffsets.end())
        dimMid += *nudgeIt;
    double halfText = textWidth / 2.0 + 3.0;  // padding
    bool textFits = (halfText * 2.0 < len);

    QPen dimPen(painter.pen().color(), 1.5, Qt::SolidLine);
    painter.setPen(dimPen);

    if (textFits) {
        // ---- NORMAL: text between ticks ----------------------------------
        // Dimension line with gap for text
        QPointF gapStart = dimMid - dir * halfText;
        QPointF gapEnd   = dimMid + dir * halfText;
        painter.drawLine(d1, gapStart);
        painter.drawLine(gapEnd, d2);

        // Perpendicular tick terminators at each end
        double tickSize = 5.0;
        painter.drawLine(d1 - perp * tickSize, d1 + perp * tickSize);
        painter.drawLine(d2 - perp * tickSize, d2 + perp * tickSize);

        // Rotated text centerd in the gap (suppressed during inline edit)
        if (!(m_canvas.m_inlineEditActive && constraint.id == m_canvas.m_inlineEditConstraintId)) {
            painter.save();
            painter.translate(dimMid);
            painter.rotate(angleDeg);

            QRectF textRect(-textWidth / 2.0 - 2,
                            -textHeight / 2.0 - 1,
                             textWidth + 4, textHeight + 2);
            painter.fillRect(textRect, m_canvas.m_theme.labelFill);
            painter.drawText(textRect, Qt::AlignCenter, text);
            painter.restore();
        }
    } else {
        // ---- COMPACT: inward arrows, text outside ------------------------
        // Two small filled arrowheads pointing inward (toward each other)
        double arrowLen = 8.0;
        double arrowHalf = 3.0;

        // Arrow at d1 pointing toward d2
        QPointF a1Tip  = d1;
        QPointF a1Base = d1 + dir * arrowLen;
        QPolygonF arrow1;
        arrow1 << a1Tip
               << (a1Base + perp * arrowHalf)
               << (a1Base - perp * arrowHalf);
        painter.setBrush(painter.pen().color());
        painter.drawPolygon(arrow1);

        // Arrow at d2 pointing toward d1
        QPointF a2Tip  = d2;
        QPointF a2Base = d2 - dir * arrowLen;
        QPolygonF arrow2;
        arrow2 << a2Tip
               << (a2Base + perp * arrowHalf)
               << (a2Base - perp * arrowHalf);
        painter.drawPolygon(arrow2);
        painter.setBrush(Qt::NoBrush);

        // Short leader line extending outward from d2, then text
        double leaderLen = 12.0;
        double textGap = 4.0;
        QPointF leaderEnd = d2 + dir * leaderLen;
        painter.drawLine(d2, leaderEnd);

        // Rotated text placed outside, past the leader (suppressed during inline edit)
        if (!(m_canvas.m_inlineEditActive && constraint.id == m_canvas.m_inlineEditConstraintId)) {
            QPointF textPos = leaderEnd + dir * (textWidth / 2.0 + textGap);
            painter.save();
            painter.translate(textPos);
            painter.rotate(angleDeg);

            QRectF textRect(-textWidth / 2.0 - 2,
                            -textHeight / 2.0 - 1,
                             textWidth + 4, textHeight + 2);
            painter.fillRect(textRect, m_canvas.m_theme.labelFill);
            painter.drawText(textRect, Qt::AlignCenter, text);
            painter.restore();
        }
    }
}

void ConstraintRenderer::drawRadialConstraint(QPainter& painter, const SketchConstraint& constraint)
{
    if (constraint.entityIds.empty()) return;

    const SketchEntity* entity = m_canvas.entityById(constraint.entityIds[0]);
    if (!entity || (entity->type != SketchEntityType::Circle && entity->type != SketchEntityType::Arc)) {
        return;
    }

    if (entity->points.empty()) return;

    QPointF sc = m_canvas.worldToScreen(entity->points[0]).toPointF();
    QPointF labelPt = m_canvas.worldToScreen(constraint.labelPosition).toPointF();
    double radiusPx = entity->radius * m_canvas.m_zoom;

    // Direction from center toward label (= toward first vertex)
    QPointF along = labelPt - sc;
    double alongLen = geometry::length(along);
    if (alongLen < geometry::kDegenerateLen) return;

    QPointF dir = along / alongLen;
    QPointF perp(-dir.y(), dir.x());

    // Edge point on circle
    QPointF edgePt = sc + dir * radiusPx;

    // Angle for text rotation
    double angleDeg = radiansToDegrees(std::atan2(dir.y(), dir.x()));
    if (angleDeg > 90.0)  angleDeg -= 180.0;
    if (angleDeg < -90.0) angleDeg += 180.0;

    // Value text
    QString prefix = (constraint.type == ConstraintType::Radius) ? QStringLiteral("R") : QStringLiteral("Ø");
    QString text = prefix + QString::fromStdString(formatValueWithUnit(constraint.value, m_canvas.m_displayUnit));
    if (!constraint.isDriving) {
        text = QStringLiteral("(") + text + QStringLiteral(")");
    }

    QFontMetricsF fm(painter.font());
    double textWidth = fm.horizontalAdvance(text);
    double textHeight = fm.height();
    double halfText = textWidth / 2.0 + 3.0;
    bool textFits = (halfText * 2.0 < radiusPx);

    QPointF dimMid = (sc + edgePt) / 2.0;
    // Apply label collision nudge offset
    auto nudgeItR = m_labelNudgeOffsets.find(constraint.id);
    if (nudgeItR != m_labelNudgeOffsets.end())
        dimMid += *nudgeItR;

    if (textFits) {
        // ---- NORMAL: text inside, dashed line with gap for label -----------
        QPointF gapStart = dimMid - dir * halfText;
        QPointF gapEnd   = dimMid + dir * halfText;

        QPen dashPen(painter.pen().color(), 1, Qt::DashLine);
        painter.setPen(dashPen);
        painter.drawLine(sc.toPoint(), gapStart.toPoint());
        painter.drawLine(gapEnd.toPoint(), edgePt.toPoint());

        // Perpendicular tick terminators at center and edge
        QPen tickPen(painter.pen().color(), 1.5, Qt::SolidLine);
        painter.setPen(tickPen);
        double tickSize = 5.0;
        painter.drawLine((sc - perp * tickSize).toPoint(), (sc + perp * tickSize).toPoint());
        painter.drawLine((edgePt - perp * tickSize).toPoint(), (edgePt + perp * tickSize).toPoint());

        // Rotated text centerd in the gap (suppressed during inline edit)
        if (!(m_canvas.m_inlineEditActive && constraint.id == m_canvas.m_inlineEditConstraintId)) {
            painter.save();
            painter.translate(dimMid);
            painter.rotate(angleDeg);

            QRectF textRect(-textWidth / 2.0 - 2,
                            -textHeight / 2.0 - 1,
                             textWidth + 4, textHeight + 2);
            painter.fillRect(textRect, m_canvas.m_theme.labelFill);
            painter.setPen(QPen(painter.pen().color(), 1, Qt::SolidLine));
            painter.drawText(textRect, Qt::AlignCenter, text);
            painter.restore();
        }
    } else {
        // ---- COMPACT: inward arrows, text outside circle -------------------
        double arrowLen = 8.0;
        double arrowHalf = 3.0;

        // Dashed line from center to edge
        QPen dashPen(painter.pen().color(), 1, Qt::DashLine);
        painter.setPen(dashPen);
        painter.drawLine(sc.toPoint(), edgePt.toPoint());

        // Arrow at center pointing toward edge (3px gap from center point)
        double arrowGap = 3.0;
        QPen arrowPen(painter.pen().color(), 1.5, Qt::SolidLine);
        painter.setPen(arrowPen);
        QPointF a1Tip = sc + dir * arrowGap;
        QPointF a1Base = a1Tip + dir * arrowLen;
        QPolygonF arrow1;
        arrow1 << a1Tip << (a1Base + perp * arrowHalf) << (a1Base - perp * arrowHalf);
        painter.setBrush(painter.pen().color());
        painter.drawPolygon(arrow1);

        // Arrow at edge pointing toward center (3px gap from edge point)
        QPointF a2Tip = edgePt - dir * arrowGap;
        QPointF a2Base = a2Tip - dir * arrowLen;
        QPolygonF arrow2;
        arrow2 << a2Tip << (a2Base + perp * arrowHalf) << (a2Base - perp * arrowHalf);
        painter.drawPolygon(arrow2);
        painter.setBrush(Qt::NoBrush);

        // Leader line extending outward from edge, then text
        double leaderLen = 12.0;
        double textGap = 4.0;
        QPointF leaderEnd = edgePt + dir * leaderLen;
        painter.drawLine(edgePt.toPoint(), leaderEnd.toPoint());

        // Rotated text placed outside, past the leader (suppressed during inline edit)
        if (!(m_canvas.m_inlineEditActive && constraint.id == m_canvas.m_inlineEditConstraintId)) {
            QPointF textPos = leaderEnd + dir * (textWidth / 2.0 + textGap);
            painter.save();
            painter.translate(textPos);
            painter.rotate(angleDeg);

            QRectF textRect(-textWidth / 2.0 - 2,
                            -textHeight / 2.0 - 1,
                             textWidth + 4, textHeight + 2);
            painter.fillRect(textRect, m_canvas.m_theme.labelFill);
            painter.setPen(QPen(painter.pen().color(), 1, Qt::SolidLine));
            painter.drawText(textRect, Qt::AlignCenter, text);
            painter.restore();
        }
    }
}

namespace {
// An angular dimension's arc as a polyline (QPainter::drawArc works in 1/16
// degree integers). Angles are radians in the math convention; screen y is
// negated on the way out. Returns the points so the caller can add arrowheads.
QVector<QPointF> drawAngleArc(QPainter& painter, const QPointF& originScreen, double arcRadius,
                              double startAngle, double sweepAngle)
{
    QPen arcPen(painter.pen().color(), 1.5, Qt::SolidLine);
    painter.setPen(arcPen);
    const int segments = 40;
    QVector<QPointF> arcPoints;
    arcPoints.reserve(segments + 1);
    for (int i = 0; i <= segments; ++i) {
        double t = static_cast<double>(i) / segments;
        double angle = startAngle + sweepAngle * t;
        arcPoints.append(QPointF(originScreen.x() + arcRadius * std::cos(angle),
                                 originScreen.y() - arcRadius * std::sin(angle)));
    }
    for (int i = 0; i < arcPoints.size() - 1; ++i) {
        painter.drawLine(arcPoints[i], arcPoints[i + 1]);
    }
    return arcPoints;   // the arrowheads sit on its ends
}
}  // namespace

// The value label of an angular dimension, nudged clear of its neighbors and
// suppressed while it is being edited inline.
void ConstraintRenderer::drawAngleLabel(QPainter& painter, const SketchConstraint& constraint, QPointF textCenter)
{
    auto nudgeIt = m_labelNudgeOffsets.find(constraint.id);
    if (nudgeIt != m_labelNudgeOffsets.end())
        textCenter += *nudgeIt;

    QString text = QString::fromStdString(formatAngle(constraint.value));
    if (!constraint.isDriving) {
        text = QStringLiteral("(") + text + QStringLiteral(")");
    }

    QFontMetricsF fm(painter.font());
    double textW = fm.horizontalAdvance(text);
    double textH = fm.height();
    if (!(m_canvas.m_inlineEditActive && constraint.id == m_canvas.m_inlineEditConstraintId)) {
        QRectF textRect(textCenter.x() - textW / 2.0 - 2,
                        textCenter.y() - textH / 2.0 - 1,
                        textW + 4, textH + 2);

        painter.fillRect(textRect, m_canvas.m_theme.labelFill);
        painter.drawText(textRect, Qt::AlignCenter, text);
    }
}

void ConstraintRenderer::drawAngleConstraint(QPainter& painter, const SketchConstraint& constraint)
{
    if (constraint.entityIds.size() < 2) return;

    const SketchEntity* e1 = m_canvas.entityById(constraint.entityIds[0]);
    const SketchEntity* e2 = m_canvas.entityById(constraint.entityIds[1]);

    if (!e1 || !e2 || e1->type != SketchEntityType::Line || e2->type != SketchEntityType::Line) {
        return;
    }

    if (e1->points.size() < 2 || e2->points.size() < 2) return;

    // ---- Determine the anchor (vertex) point ----
    QPointF intersection;
    if (constraint.hasAnchorPoint()) {
        // Explicit anchor from the constraint
        intersection = constraint.anchorPoint;
    } else {
        // Compute the intersection of the two line rays
        QLineF line1(e1->points[0], e1->points[1]);
        QLineF line2(e2->points[0], e2->points[1]);
        QLineF::IntersectionType intersectType = line1.intersects(line2, &intersection);
        if (intersectType == QLineF::NoIntersection) {
            intersection = constraint.labelPosition;
        }
    }

    QPointF originScreen = m_canvas.worldToScreen(intersection).toPointF();

    // ---- Compute angles of the two lines in screen space ----------------
    // Note: screen Y is inverted, so negate Y for angle calculation
    QPointF s1a = m_canvas.worldToScreen(e1->points[0]).toPointF();
    QPointF s1b = m_canvas.worldToScreen(e1->points[1]).toPointF();
    QPointF s2a = m_canvas.worldToScreen(e2->points[0]).toPointF();
    QPointF s2b = m_canvas.worldToScreen(e2->points[1]).toPointF();

    // Use the ray direction from the anchor toward the far end of each line
    QPointF dir1 = s1b - s1a;
    QPointF dir2 = s2b - s2a;
    // Orient rays away from anchor (pick the end farther from anchor)
    if (QLineF(originScreen, s1a).length() > QLineF(originScreen, s1b).length())
        dir1 = s1a - s1b;
    if (QLineF(originScreen, s2a).length() > QLineF(originScreen, s2b).length())
        dir2 = s2a - s2b;

    const double a1 = std::atan2(-dir1.y(), dir1.x());   // negate Y for math coords
    const double a2 = std::atan2(-dir2.y(), dir2.x());

    // Draw the arc in the sector the LABEL sits in, bounded by the two lines
    // (or their extensions). The four ray directions (a1, a1+pi, a2, a2+pi)
    // split the plane into four sectors of sizes theta and 180-theta; the label
    // picks one, and the arc spans exactly that sector, so it always portrays
    // the value being shown (interior or exterior), never a reflex.
    const QPointF labelScr = m_canvas.worldToScreen(constraint.labelPosition).toPointF();
    const double al = std::atan2(-(labelScr.y() - originScreen.y()),
                                  labelScr.x() - originScreen.x());
    auto normPi = [](double a){ while (a > M_PI) a -= 2.0*M_PI; while (a < -M_PI) a += 2.0*M_PI; return a; };
    const double rays[4] = { a1, a1 + M_PI, a2, a2 + M_PI };
    double cw = -2.0 * M_PI, ccw = 2.0 * M_PI;   // nearest boundary each side of the label
    for (double r : rays) {
        const double d = normPi(r - al);
        if (d >= -geometry::kZeroEps && d < ccw) ccw = d;
        if (d <=  geometry::kZeroEps && d > cw)  cw  = d;
    }
    double startAngle = al + cw;
    double sweepAngle = ccw - cw;
    if (sweepAngle < 1e-6) sweepAngle = std::abs(normPi(a2 - a1));   // degenerate guard

    // ---- Arc radius (screen pixels) --------------------------------------
    // Follow the label's distance from the vertex (Fusion): drag the label out
    // to grow the arc, in to shrink it. Clamp to a sensible minimum.
    double arcRadius = geometry::lineLength(originScreen, labelScr);
    if (arcRadius < 18.0) arcRadius = 18.0;

    // ---- Draw the arc ----------------------------------------------------
    const QVector<QPointF> arcPoints = drawAngleArc(painter, originScreen, arcRadius, startAngle, sweepAngle);

    // ---- Arrowheads at both ends of the arc ------------------------------
    double arrowSize = 7.0;
    // Both heads point OUTWARD along the arc (away from each other), toward the
    // bounding lines (the standard angular-dimension look). Screen tangent of
    // P(theta)=(Ox+r cos, Oy-r sin) is (-sin,-cos) for increasing theta.
    const double s = (sweepAngle > 0 ? 1.0 : -1.0);
    {   // start end: outward = decreasing-theta = +s*(sin, cos)
        double angle = startAngle;
        QPointF tangent(std::sin(angle) * s, std::cos(angle) * s);
        drawArrow(painter, arcPoints.first(), tangent, arrowSize);
    }
    {   // end end: outward = increasing-theta = +s*(-sin, -cos)
        double angle = startAngle + sweepAngle;
        QPointF tangent(-std::sin(angle) * s, -std::cos(angle) * s);
        drawArrow(painter, arcPoints.last(), tangent, arrowSize);
    }

    // ---- Extension lines from intersection to arc ends -------------------
    // Skip extension lines for sweep-angle constraints (construction lines serve as visual guides)
    bool isSweep = false;
    for (const auto& g : m_canvas.m_groups) {
        if (m_canvas.isSweepAngleGroup(g.id) && g.containsConstraint(constraint.id)) {
            isSweep = true;
            break;
        }
    }
    if (!isSweep) {
        QPen extPen(painter.pen().color(), 1, Qt::SolidLine);
        painter.setPen(extPen);
        double extOvershoot = 5.0;
        QPointF radDir1 = arcPoints.first() - originScreen;
        double radLen1 = geometry::length(radDir1);
        if (radLen1 > 1.0) {
            QPointF rn1 = radDir1 / radLen1;
            painter.drawLine(originScreen, arcPoints.first() + rn1 * extOvershoot);
        }
        QPointF radDir2 = arcPoints.last() - originScreen;
        double radLen2 = geometry::length(radDir2);
        if (radLen2 > 1.0) {
            QPointF rn2 = radDir2 / radLen2;
            painter.drawLine(originScreen, arcPoints.last() + rn2 * extOvershoot);
        }
    }

    // ---- Value text at midpoint of arc -----------------------------------
    double midAngle = startAngle + sweepAngle / 2.0;
    double textRadius = arcRadius + 14.0;
    QPointF textCenter(originScreen.x() + textRadius * std::cos(midAngle),
                       originScreen.y() - textRadius * std::sin(midAngle));

    // Apply label collision nudge offset
    drawAngleLabel(painter, constraint, textCenter);
}

void ConstraintRenderer::drawFixedAngleConstraint(QPainter& painter, const SketchConstraint& constraint)
{
    if (constraint.entityIds.empty()) return;

    const SketchEntity* e = m_canvas.entityById(constraint.entityIds[0]);
    if (!e || e->type != SketchEntityType::Line) return;
    if (e->points.size() < 2) return;

    // ---- Determine the anchor point ----
    // Use explicit anchor if set, otherwise use the line's start point
    QPointF anchor;
    if (constraint.hasAnchorPoint()) {
        anchor = constraint.anchorPoint;
    } else {
        anchor = e->points[0];
    }

    QPointF originScreen = m_canvas.worldToScreen(anchor).toPointF();

    // ---- Compute angles in screen space ----
    // Horizontal reference direction: pointing right in screen space
    double aHoriz = 0.0;   // horizontal = 0 radians in math coords

    // Line direction (from anchor toward far end)
    QPointF sa = m_canvas.worldToScreen(e->points[0]).toPointF();
    QPointF sb = m_canvas.worldToScreen(e->points[1]).toPointF();
    QPointF dir = sb - sa;
    // Orient ray away from anchor
    if (QLineF(originScreen, sa).length() > QLineF(originScreen, sb).length())
        dir = sa - sb;

    double aLine = std::atan2(-dir.y(), dir.x());   // negate Y for math coords

    // Sweep from horizontal to line direction
    double sweep = aLine - aHoriz;
    sweep = hobbycad::geometry::wrapSweepRad(sweep);

    double startAngle = aHoriz;
    double sweepAngle = sweep;

    // ---- Arc radius (screen pixels) ----
    double arcRadius = 35.0;

    // ---- Draw the arc ----
    const QVector<QPointF> arcPoints = drawAngleArc(painter, originScreen, arcRadius, startAngle, sweepAngle);

    // ---- Arrowhead at the line end of the arc ----
    double arrowSize = 7.0;
    {
        double angle = startAngle + sweepAngle;
        double tx = std::sin(angle) * (sweepAngle > 0 ? 1 : -1);
        double ty = std::cos(angle) * (sweepAngle > 0 ? 1 : -1);
        QPointF tip = arcPoints.last();
        QPointF tangent(tx, ty);
        drawArrow(painter, tip, tangent, arrowSize);
    }

    // ---- Short horizontal reference tick at the arc start ----
    QPen extPen(painter.pen().color(), 1, Qt::DashLine);
    painter.setPen(extPen);
    double refLen = arcRadius + 10.0;
    painter.drawLine(originScreen, QPointF(originScreen.x() + refLen, originScreen.y()));

    // ---- Extension line from anchor toward line end ----
    extPen.setStyle(Qt::SolidLine);
    painter.setPen(extPen);
    double extOvershoot = 5.0;
    QPointF radDir = arcPoints.last() - originScreen;
    double radLen = geometry::length(radDir);
    if (radLen > 1.0) {
        QPointF rn = radDir / radLen;
        painter.drawLine(originScreen, arcPoints.last() + rn * extOvershoot);
    }

    // ---- Value text at midpoint of arc ----
    double midAngle = startAngle + sweepAngle / 2.0;
    double textRadius = arcRadius + 14.0;
    QPointF textCenter(originScreen.x() + textRadius * std::cos(midAngle),
                       originScreen.y() - textRadius * std::sin(midAngle));

    // Apply label collision nudge offset
    drawAngleLabel(painter, constraint, textCenter);
}

void ConstraintRenderer::drawGeometricConstraint(QPainter& painter, const SketchConstraint& constraint)
{
    if (constraint.entityIds.empty()) return;

    // Constraint types whose glyph anchors at a specific referenced point
    const bool pointAnchored =
        constraint.type == ConstraintType::Coincident ||
        constraint.type == ConstraintType::Midpoint ||
        constraint.type == ConstraintType::PointOnLine ||
        constraint.type == ConstraintType::PointOnCircle ||
        constraint.type == ConstraintType::FixedPoint ||
        constraint.type == ConstraintType::Symmetric;

    // Constraints that mark a single shared location draw one glyph;
    // relational constraints (parallel, equal, ...) draw one per entity
    const bool singleGlyph =
        constraint.type == ConstraintType::Coincident ||
        constraint.type == ConstraintType::Concentric ||
        constraint.type == ConstraintType::Midpoint ||
        constraint.type == ConstraintType::PointOnLine ||
        constraint.type == ConstraintType::PointOnCircle ||
        constraint.type == ConstraintType::FixedPoint;

    const int glyphCount = singleGlyph ? 1
                                       : static_cast<int>(constraint.entityIds.size());

    for (int i = 0; i < glyphCount; ++i) {
        const SketchEntity* ent = m_canvas.entityById(constraint.entityIds[i]);
        if (!ent) continue;

        // ---- Anchor point in world coordinates ---------------------------
        QPointF anchor;
        bool haveAnchor = false;

        const int ptIdx = (i < static_cast<int>(constraint.pointIndices.size()))
                              ? constraint.pointIndices[i] : -1;
        if (pointAnchored && ptIdx >= 0
                && ptIdx < static_cast<int>(ent->points.size())) {
            anchor = ent->points[ptIdx];
            haveAnchor = true;
        } else {
            switch (ent->type) {
            case SketchEntityType::Line:
                if (ent->points.size() >= 2) {
                    anchor = (QPointF(ent->points[0]) + QPointF(ent->points[1])) / 2.0;
                    haveAnchor = true;
                }
                break;
            case SketchEntityType::Circle:
                if (!ent->points.empty()) {
                    const double a = M_PI / 4.0;   // NE perimeter point
                    anchor = QPointF(ent->points[0].x + ent->radius * std::cos(a),
                                     ent->points[0].y + ent->radius * std::sin(a));
                    haveAnchor = true;
                }
                break;
            case SketchEntityType::Arc:
                if (!ent->points.empty()) {
                    const double mid = degreesToRadians(ent->startAngle + ent->sweepAngle / 2.0);
                    anchor = QPointF(ent->points[0].x + ent->radius * std::cos(mid),
                                     ent->points[0].y + ent->radius * std::sin(mid));
                    haveAnchor = true;
                }
                break;
            default:
                if (!ent->points.empty()) {
                    anchor = ent->points[0];
                    haveAnchor = true;
                }
                break;
            }
        }
        if (!haveAnchor) continue;

        // ---- Chip placement (screen space, stacked per LOCATION) ---------
        // 20, not 16. At 16 with 3.5 of padding the glyph had nine
        // pixels to work in against a 1.2px pen, which is about three
        // legible elements: enough for parallel or equal, not enough for
        // coincident, whose meaning needs a line, a point, an arrow and
        // the ring it joins. 20 with 3 of padding gives fourteen.
        const int chip = 20;

        const QPoint sp = m_canvas.worldToScreen(anchor);

        // Place the chip just off its anchor, then bump it right until it
        // clears EVERY chip already placed this pass, so no two constraint
        // glyphs overlap, however many constraints cluster at one spot (a slot
        // end carries coincident + two perpendiculars + point-on-line + the
        // equal-width chips) (Aaron). Each cluster fans into a tidy row beside
        // the point it describes; distant clusters never interfere.
        QRect chipRect = findFreeGlyphSlot(sp, chip, m_placedGlyphChips, m_geometryRects);
        m_placedGlyphChips.append(chipRect);

        // ---- Leader line ---------------------------------------------------
        // When the declutter fanned the chip away from its default slot (just
        // down-right of the anchor), draw a thin faint leader from the anchor to
        // the chip so it stays legible which point the constraint describes.
        // [Aaron glyph rule 2026-09-09]
        {
            const QPoint defTL(sp.x() + 8, sp.y() + 8);
            if ((chipRect.topLeft() - defTL).manhattanLength() > chip) {
                painter.save();
                QColor lc = painter.pen().color();
                lc.setAlpha(110);
                painter.setPen(QPen(lc, 1.0));
                painter.setBrush(Qt::NoBrush);
                painter.drawLine(sp, chipRect.center());
                painter.restore();
            }
        }

        // ---- Chip background + border -------------------------------------
        painter.save();
        QColor fill = m_canvas.palette().base().color();
        fill.setAlpha(230);
        painter.setPen(QPen(painter.pen().color(),
                            constraint.selected ? 2.0 : 1.0));
        painter.setBrush(fill);
        painter.drawRoundedRect(chipRect, 3, 3);

        // ---- Icon ----------------------------------------------------------
        QPen iconPen(painter.pen().color(), 1.2);
        iconPen.setCapStyle(Qt::RoundCap);
        painter.setPen(iconPen);
        painter.setBrush(Qt::NoBrush);
        drawConstraintGlyphIcon(painter, constraint.type,
                                QRectF(chipRect).adjusted(3.0, 3.0, -3.0, -3.0));
        painter.restore();

        // 1px slop makes the small chips easier to click
        m_constraintGlyphRects.append({constraint.id,
                                       chipRect.adjusted(-1, -1, 1, 1)});
    }
}

void ConstraintRenderer::drawConstraintGlyphIcon(QPainter& painter,
                                           ConstraintType type,
                                           const QRectF& r)
{
    // Shapes live in constraintglyphs.cpp so they can be drawn without a
    // canvas; see that file for why they are paths and not SVG assets.
    drawConstraintGlyph(painter, type, r);
}

int ConstraintRenderer::hitTestConstraintGlyph(const QPoint& screenPos) const
{
    // Topmost chip wins (drawn last)
    for (int i = m_constraintGlyphRects.size() - 1; i >= 0; --i) {
        if (m_constraintGlyphRects[i].second.contains(screenPos))
            return m_constraintGlyphRects[i].first;
    }
    return -1;
}

void ConstraintRenderer::drawArrow(QPainter& painter, const QPointF& pos, const QPointF& dir, double size)
{
    // Draw simple arrow at position pointing in direction
    QPointF perpDir(-dir.y(), dir.x());

    QPointF arrowTip = pos;
    QPointF arrowLeft = arrowTip - dir * size + perpDir * (size / 2.0);
    QPointF arrowRight = arrowTip - dir * size - perpDir * (size / 2.0);

    painter.drawLine(arrowTip, arrowLeft);
    painter.drawLine(arrowTip, arrowRight);
}

ConstraintRenderer::ConstraintLabelPosition
ConstraintRenderer::computeConstraintLabelPosition(const SketchConstraint& c) const
{
    ConstraintLabelPosition result;

    if (c.type == ConstraintType::Distance) {
        QPointF p1, p2;
        if (!m_canvas.getConstraintEndpoints(c, p1, p2)) return result;

        QPointF sp1 = m_canvas.worldToScreen(p1).toPointF();
        QPointF sp2 = m_canvas.worldToScreen(p2).toPointF();
        QPointF labelCenterPt = m_canvas.worldToScreen(c.labelPosition).toPointF();

        QPointF along = sp2 - sp1;
        double len = geometry::length(along);
        if (len < 1.0) return result;

        QPointF dir = along / len;
        QPointF perp(-dir.y(), dir.x());

        QPointF labelDelta = labelCenterPt - sp1;
        double offset = labelDelta.x() * perp.x() + labelDelta.y() * perp.y();

        QPointF d1 = sp1 + perp * offset;
        QPointF d2 = sp2 + perp * offset;

        // Check if text fits inside (compact mode check)
        QString text = QString::fromStdString(formatValueWithUnit(c.value, m_canvas.m_displayUnit));
        if (!c.isDriving) text = QStringLiteral("(") + text + QStringLiteral(")");
        QFontMetricsF fm(m_canvas.font());
        double textWidth = fm.horizontalAdvance(text);
        double halfText = textWidth / 2.0 + 3.0;
        bool textFits = (halfText * 2.0 < len);

        if (textFits) {
            result.textCenter = (d1 + d2) / 2.0;
        } else {
            // Compact mode: text is outside past d2
            double leaderLen = 12.0;
            double textGap = 4.0;
            QPointF leaderEnd = d2 + dir * leaderLen;
            result.textCenter = leaderEnd + dir * (textWidth / 2.0 + textGap);
        }
        result.found = true;

    } else if (c.type == ConstraintType::Angle) {
        if (c.entityIds.size() < 2) return result;
        const SketchEntity* ae1 = m_canvas.entityById(c.entityIds[0]);
        const SketchEntity* ae2 = m_canvas.entityById(c.entityIds[1]);
        if (!ae1 || !ae2
                || ae1->type != SketchEntityType::Line
                || ae2->type != SketchEntityType::Line
                || ae1->points.size() < 2 || ae2->points.size() < 2)
            return result;

        QPointF intersection;
        if (c.hasAnchorPoint()) {
            intersection = c.anchorPoint;
        } else {
            QLineF l1(ae1->points[0], ae1->points[1]);
            QLineF l2(ae2->points[0], ae2->points[1]);
            if (l1.intersects(l2, &intersection) == QLineF::NoIntersection)
                intersection = c.labelPosition;
        }
        QPointF originScr = m_canvas.worldToScreen(intersection).toPointF();

        QPointF s1a = m_canvas.worldToScreen(ae1->points[0]).toPointF();
        QPointF s1b = m_canvas.worldToScreen(ae1->points[1]).toPointF();
        QPointF s2a = m_canvas.worldToScreen(ae2->points[0]).toPointF();
        QPointF s2b = m_canvas.worldToScreen(ae2->points[1]).toPointF();
        QPointF dir1 = s1b - s1a;
        QPointF dir2 = s2b - s2a;
        if (QLineF(originScr, s1a).length() > QLineF(originScr, s1b).length())
            dir1 = s1a - s1b;
        if (QLineF(originScr, s2a).length() > QLineF(originScr, s2b).length())
            dir2 = s2a - s2b;

        double a1 = std::atan2(-dir1.y(), dir1.x());
        double a2 = std::atan2(-dir2.y(), dir2.x());
        double sweep = a2 - a1;
        sweep = hobbycad::geometry::wrapSweepRad(sweep);
        if (c.supplementary) {
            if (sweep > 0) sweep -= 2.0 * M_PI;
            else sweep += 2.0 * M_PI;
        }

        double midAngle = a1 + sweep / 2.0;
        double textRadius = 35.0 + 14.0;
        result.textCenter = QPointF(originScr.x() + textRadius * std::cos(midAngle),
                                    originScr.y() - textRadius * std::sin(midAngle));

        auto nudgeIt = m_labelNudgeOffsets.find(c.id);
        if (nudgeIt != m_labelNudgeOffsets.end())
            result.textCenter += *nudgeIt;
        result.found = true;

    } else if (c.type == ConstraintType::FixedAngle) {
        // Single-entity fixed angle (angle from horizontal)
        if (c.entityIds.empty()) return result;
        const SketchEntity* e = m_canvas.entityById(c.entityIds[0]);
        if (!e || e->type != SketchEntityType::Line || e->points.size() < 2)
            return result;

        QPointF anchor = c.hasAnchorPoint() ? QPointF(c.anchorPoint) : QPointF(e->points[0]);
        QPointF originScr = m_canvas.worldToScreen(anchor).toPointF();

        QPointF sa = m_canvas.worldToScreen(e->points[0]).toPointF();
        QPointF sb = m_canvas.worldToScreen(e->points[1]).toPointF();
        QPointF dir = sb - sa;
        if (QLineF(originScr, sa).length() > QLineF(originScr, sb).length())
            dir = sa - sb;

        double aLine = std::atan2(-dir.y(), dir.x());
        double sweep = aLine;  // sweep from horizontal (0) to line angle
        sweep = hobbycad::geometry::wrapSweepRad(sweep);

        double midAngle = sweep / 2.0;
        double textRadius = 35.0 + 14.0;
        result.textCenter = QPointF(originScr.x() + textRadius * std::cos(midAngle),
                                    originScr.y() - textRadius * std::sin(midAngle));

        auto nudgeIt = m_labelNudgeOffsets.find(c.id);
        if (nudgeIt != m_labelNudgeOffsets.end())
            result.textCenter += *nudgeIt;
        result.found = true;

    } else if (c.type == ConstraintType::Radius || c.type == ConstraintType::Diameter) {
        result.prefix = (c.type == ConstraintType::Radius)
            ? QStringLiteral("R") : QStringLiteral("Ø");
        if (c.entityIds.empty()) return result;
        const SketchEntity* re = m_canvas.entityById(c.entityIds[0]);
        if (!re || (re->type != SketchEntityType::Circle && re->type != SketchEntityType::Arc))
            return result;
        if (re->points.empty()) return result;

        QPointF sc = m_canvas.worldToScreen(re->points[0]).toPointF();
        QPointF labelPt = m_canvas.worldToScreen(c.labelPosition).toPointF();
        double radiusPx = re->radius * m_canvas.m_zoom;

        QPointF along = labelPt - sc;
        double alongLen = geometry::length(along);
        if (alongLen < geometry::kDegenerateLen) return result;

        QPointF dir = along / alongLen;
        QPointF edgePt = sc + dir * radiusPx;
        QPointF dimMid = (sc + edgePt) / 2.0;

        auto nudgeItR = m_labelNudgeOffsets.find(c.id);
        if (nudgeItR != m_labelNudgeOffsets.end())
            dimMid += *nudgeItR;

        // Check if text fits inside (compact mode check)
        QString rtext = result.prefix + QString::fromStdString(formatValueWithUnit(c.value, m_canvas.m_displayUnit));
        if (!c.isDriving) rtext = QStringLiteral("(") + rtext + QStringLiteral(")");
        QFontMetricsF fmR(m_canvas.font());
        double rtextWidth = fmR.horizontalAdvance(rtext);
        double rhalfText = rtextWidth / 2.0 + 3.0;
        bool rtextFits = (rhalfText * 2.0 < radiusPx);

        if (rtextFits) {
            result.textCenter = dimMid;
        } else {
            double leaderLen = 12.0;
            double textGap = 4.0;
            QPointF leaderEnd = edgePt + dir * leaderLen;
            result.textCenter = leaderEnd + dir * (rtextWidth / 2.0 + textGap);
        }
        result.found = true;

    } else {
        // Geometric constraints: use raw label position
        result.textCenter = m_canvas.worldToScreen(c.labelPosition).toPointF();
        result.found = true;
    }

    return result;
}

void ConstraintRenderer::drawInlineConstraintEdit(QPainter& painter, const QPointF& position,
                                             const QString& prefix)
{
    painter.save();

    QFont font = painter.font();
    font.setPointSize(9);
    painter.setFont(font);
    QFontMetricsF fm(font);

    QString displayText;
    QColor bgColor;
    QColor textColor;
    QColor borderColor;

    if (m_canvas.m_inlineEditSelectAll) {
        // Show formatted value with blue selection highlight
        const SketchConstraint* c = m_canvas.constraintById(m_canvas.m_inlineEditConstraintId);
        double val = c ? c->value : m_canvas.m_inlineEditOriginalValue;
        if (m_canvas.m_inlineEditIsAngle) {
            displayText = QString::fromStdString(hobbycad::formatValue(val));
        } else {
            displayText = QString::fromStdString(
                hobbycad::formatValue(hobbycad::mmToUnit(val, m_canvas.m_displayUnit)));
        }
        bgColor = m_canvas.m_theme.dimEditSelBg;
        textColor = m_canvas.m_theme.dimEditSelText;
        borderColor = m_canvas.m_theme.dimEditSelBorder;
    } else if (!m_canvas.m_inlineEditBuffer.isEmpty()) {
        // Typing mode: yellow with cursor
        int safePos = qBound(0, m_canvas.m_inlineEditCursorPos, m_canvas.m_inlineEditBuffer.length());
        displayText = m_canvas.m_inlineEditBuffer;
        displayText.insert(safePos, QStringLiteral("\u2502"));
        bgColor = m_canvas.m_theme.dimEditTypingBg;
        textColor = m_canvas.m_theme.dimEditTypingText;
        borderColor = m_canvas.m_theme.dimEditTypingBorder;
    } else {
        // Empty buffer (shouldn't normally happen)
        displayText = QStringLiteral("0");
        bgColor = m_canvas.m_theme.dimEditEmptyBg;
        textColor = m_canvas.m_theme.dimEditTypingText;
        borderColor = m_canvas.m_theme.dimEditEmptyBorder;
    }

    if (!prefix.isEmpty()) {
        displayText = prefix + displayText;
    }

    QRectF textBounds = fm.boundingRect(displayText);

    painter.translate(position);

    QRectF labelRect(-textBounds.width() / 2.0 - 5,
                     -textBounds.height() / 2.0 - 2,
                     textBounds.width() + 10,
                     textBounds.height() + 4);
    painter.fillRect(labelRect, bgColor);

    if (borderColor.isValid()) {
        painter.setPen(QPen(borderColor, 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(labelRect);
    }

    painter.setPen(textColor);
    painter.drawText(labelRect, Qt::AlignCenter, displayText);

    painter.restore();
}

}  // namespace hobbycad
