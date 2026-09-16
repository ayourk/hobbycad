// =====================================================================
//  src/hobbycad/gui/constraintglyphs.h — canvas glyph shapes
//  Repository: ayourk/hobbycad
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//
//  The little shape inside a constraint badge, drawn as QPainter paths
//  rather than loaded from an icon file.
//
//  Procedural on purpose: the glyph takes its color from the pen, and the
//  pen already encodes whether the constraint is driving, satisfied,
//  failed, selected or a redundancy candidate. An SVG carries its own
//  baked colors and would need one copy per state, or a substitution pass
//  at load time.
//
//  Free function rather than a SketchCanvas member so that the shapes can
//  be rendered without a canvas, for a side-by-side comparison against
//  other CADs' icon sets, and so they can be tested.
//
// =====================================================================

#ifndef HOBBYCAD_CONSTRAINTGLYPHS_H
#define HOBBYCAD_CONSTRAINTGLYPHS_H

#include <hobbycad/sketch/constraint.h>

#include <QColor>

class QPainter;
class QRectF;

// ORIENTATION CARRIES MEANING. Point-on-line is the only glyph that runs
// top-left to bottom-right; parallel, collinear, tangent and fixed angle
// all rise left-to-right. That makes point-on-line identifiable by its
// angle alone, before any detail resolves, which is what matters at a
// twenty-pixel badge, where detail is the first thing to go. Do not
// "tidy" it to match the others.

namespace hobbycad {

/// Draw the glyph for `type` inside `r`, using the painter's current pen.
///
/// `chipFill` is the color behind the glyph, when there is one. A glyph
/// that needs a gap between touching parts can punch it with that color
/// instead of relying on empty space, which the badge does not have:
/// its drawing area is fourteen pixels across with a 1.2px pen, so a gap
/// specified as a fraction of the box closes as soon as the strokes
/// spread into it. Pass an invalid color when drawing on nothing.
void drawConstraintGlyph(QPainter& painter, sketch::ConstraintType type,
                         const QRectF& r, const QColor& chipFill = QColor());

/// Draw the "this belongs to a group" indicator inside `r`, using the
/// painter's current pen.
///
/// NOT a constraint, and deliberately not a ConstraintType: grouping is
/// not something the solver knows about, and an enum value nothing ever
/// constructs is dead weight that later reads as a gap to be filled.
/// This fires on SELECTION (when a click lands on an entity that is a
/// member of a group), so it lives outside that enum and outside the
/// badge chip row, and is free to draw at whatever size reads best.
///
/// Two halves, saying two different things. The interior is a square and
/// a circle, each keeping its own outline, which says the selection is
/// ONE MEMBER OF SEVERAL: the parts are still parts. (A single fused
/// outline, which is what Tinkercad's own group icon draws, would claim a
/// boolean merge; a HobbyCAD group holds separable primitives.) The four
/// corner brackets say how far the selection extends, and are drawn
/// lighter than the interior because they are the frame, not the subject.
///
/// THE CIRCLE IS WHOLE AND THE SQUARE STOPS WHERE IT MEETS IT, so the
/// square has no lower-right corner. That occlusion is the whole reason
/// the two read as two objects with one in front rather than as line
/// noise; drawing both complete makes the arc cut through the square's
/// interior and the glyph turns to mush below about 80px. Tinkercad and
/// KiCad both draw it this way. Do not "fix" the missing corner.
void drawGroupGlyph(QPainter& painter, const QRectF& r);

/// Transform pivot: a six-pointed star, ringed by a hemisphere arrow over
/// it (two heads pointing down at 3 and 9 o'clock) when `withArcArrow`,
/// which the canvas sets for Rotate and Free Move. The star's outline, the
/// arc and the heads take the pen's color; the star's interior takes the
/// painter's current brush, so Qt::NoBrush gives an outline star and a
/// solid brush a filled one (the canvas paints it yellow, like the sun).
/// With the arrow on, the star shrinks so it clears the arc; pass a larger
/// rect and the star inside stays the same size.
void drawPivotGlyph(QPainter& painter, const QRectF& r, bool withArcArrow);

}  // namespace hobbycad

#endif  // HOBBYCAD_CONSTRAINTGLYPHS_H
