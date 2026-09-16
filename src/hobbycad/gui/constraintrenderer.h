// =====================================================================
//  src/hobbycad/gui/constraintrenderer.h — constraint-rendering subsystem
// =====================================================================
//
//  Draws every constraint on a sketch: dimension lines and their labels,
//  the geometric-constraint glyph badges, angle arcs, and the group
//  indicator; lays out label positions so they do not overlap; and
//  hit-tests the glyph badges for selection.
//
//  Extracted from SketchCanvas so the canvas no longer owns this whole
//  subsystem. It keeps a back-reference to its canvas for the geometry it
//  needs (coordinate transforms, entity/constraint lookup, display unit,
//  selection and visibility state) and is a friend of it. The glyph SHAPES
//  live in constraintglyphs.h; this class places and paints the badges
//  around them.
//
//  Part of HobbyCAD.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_GUI_CONSTRAINTRENDERER_H
#define HOBBYCAD_GUI_CONSTRAINTRENDERER_H

#include <hobbycad/sketch/constraint.h>

#include <QHash>
#include <QVector>
#include <QPair>
#include <QPointF>
#include <QRect>
#include <QString>

class QPainter;
class QPoint;
class QRectF;

namespace hobbycad {

class SketchCanvas;
struct SketchConstraint;

/// The constraint-rendering subsystem for one SketchCanvas.
class ConstraintRenderer {
public:
    explicit ConstraintRenderer(SketchCanvas& canvas) : m_canvas(canvas) {}

    /// Where a constraint's dimension label sits, in screen space.
    struct ConstraintLabelPosition {
        QPointF textCenter;   ///< screen-space label center
        QString prefix;       ///< "R", "Ø", or ""
        bool found = false;
    };

    // ---- Entry points (called by the canvas / tools) --------------------
    /// Paint every visible constraint for this frame.
    void drawConstraints(QPainter& painter);
    /// Constraint id whose glyph badge is under `screenPos`, or -1.
    int hitTestConstraintGlyph(const QPoint& screenPos) const;
    /// Screen-space label placement for one constraint (shared with hit-testing).
    ConstraintLabelPosition computeConstraintLabelPosition(const SketchConstraint& c) const;

    // Preview-dimension chrome used by the tool handlers' previews.
    void drawPreviewDimension(QPainter& painter, const QPoint& p1, const QPoint& p2, double value);
    void drawDimensionLabel(QPainter& painter, const QPointF& position, double value);
    void drawArcDimensionLabel(QPainter& painter, const QPointF& position,
                               double arcLength, double angleDeg);

    /// Chip/label/marker rects this renderer placed on the last drawConstraints
    /// pass, so glyphs drawn afterward (e.g. the group badge) can dodge them.
    /// [Aaron glyph rule 2026-09-09: no glyph overlaps another]
    const QVector<QRect>& placedGlyphRects() const { return m_placedGlyphChips; }

    /// Rects that constraint chips must NOT overlap (hard): the pivot star and
    /// the active snap indicator, supplied by the canvas before drawConstraints.
    void setReservedGlyphRects(const QVector<QRect>& r) { m_reservedGlyphRects = r; }
    /// Screen rects of the sketch geometry (soft): chips prefer slots that do
    /// not cover the drawing, but never fly off-screen to avoid it.
    void setGeometryRects(const QVector<QRect>& r) { m_geometryRects = r; }

private:
    void resolveConstraintLabelOverlaps();
    void drawConstraint(QPainter& painter, const SketchConstraint& constraint);
    void drawCoincidenceMarker(QPainter& painter, const SketchConstraint& constraint);
    void drawDistanceConstraint(QPainter& painter, const SketchConstraint& constraint);
    void drawRadialConstraint(QPainter& painter, const SketchConstraint& constraint);
    void drawAngleConstraint(QPainter& painter, const SketchConstraint& constraint);
    void drawFixedAngleConstraint(QPainter& painter, const SketchConstraint& constraint);
    void drawAngleLabel(QPainter& painter, const SketchConstraint& constraint, QPointF textCenter);
    void drawGeometricConstraint(QPainter& painter, const SketchConstraint& constraint);
    void drawConstraintGlyphIcon(QPainter& painter, sketch::ConstraintType type, const QRectF& r);
    void drawArrow(QPainter& painter, const QPointF& pos, const QPointF& dir, double size);
    void drawInlineConstraintEdit(QPainter& painter, const QPointF& position,
                                  const QString& prefix = QString());

    SketchCanvas& m_canvas;   ///< non-owning back-reference

    // Per-frame scratch state (rebuilt each drawConstraints()).
    QHash<int, QPointF> m_labelNudgeOffsets;        ///< constraint id → label nudge to clear overlaps
    QVector<QPair<int, QRect>> m_constraintGlyphRects;  ///< constraint id → screen-space chip rect
    QVector<QRect> m_placedGlyphChips;              ///< chip rects placed this pass (overlap avoidance)
    QVector<QRect> m_reservedGlyphRects;            ///< hard: pivot star + snap indicator
    QVector<QRect> m_geometryRects;                 ///< soft: sketch geometry bboxes
    QVector<QRect> m_labelRects;                    ///< final dimension-label rects this pass (glyphs avoid them)
};

}  // namespace hobbycad

#endif  // HOBBYCAD_GUI_CONSTRAINTRENDERER_H
