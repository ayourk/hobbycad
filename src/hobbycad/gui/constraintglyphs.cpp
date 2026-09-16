// =====================================================================
//  src/hobbycad/gui/constraintglyphs.cpp — constraint badge shapes
//  Repository: ayourk/hobbycad
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================

#include "constraintglyphs.h"

#include <QBrush>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QPolygonF>
#include <QRectF>
#include <QtMath>
#include <QtGlobal>

namespace hobbycad {

using ConstraintType = sketch::ConstraintType;

void drawConstraintGlyph(QPainter& painter, ConstraintType type, const QRectF& r,
                         const QColor& chipFill)
{
    const QPointF c = r.center();
    const double w = r.width();
    const double h = r.height();

    switch (type) {
    case ConstraintType::Horizontal:
        painter.drawLine(QPointF(r.left(), c.y()), QPointF(r.right(), c.y()));
        break;
    case ConstraintType::Vertical:
        painter.drawLine(QPointF(c.x(), r.top()), QPointF(c.x(), r.bottom()));
        break;
    case ConstraintType::Parallel:
        // Two strokes at a true 45 degrees, offset along the diagonal, as
        // Fusion and Onshape both draw it. The previous pair ran at about
        // 68 degrees, which reads as "steep" rather than "parallel".
        painter.drawLine(QPointF(r.left() + w * 0.05, r.bottom() - h * 0.30),
                         QPointF(r.left() + w * 0.70, r.top() + h * 0.05));
        painter.drawLine(QPointF(r.left() + w * 0.30, r.bottom() - h * 0.05),
                         QPointF(r.left() + w * 0.95, r.top() + h * 0.30));
        break;
    case ConstraintType::Perpendicular: {
        // Baseline, an upright meeting it off-center, and the square that
        // marks the right angle (the Onshape and SolidWorks form).
        // Without the square this is just a T.
        //
        // The square is sized so its TOP meets the MIDDLE of the upright.
        // At a quarter of that it survived a 64px render and disappeared
        // in a 16px badge, which is the size that actually matters.
        const double baseY = r.bottom() - h * 0.10;
        const double upTop = r.top() + h * 0.04;
        const double upX   = r.left() + w / 3.0;    // a third in, two thirds beyond
        const double midY  = (baseY + upTop) * 0.5;
        const double sq    = baseY - midY;      // square, so side == height

        // Base runs the full width: it has to read as the line being met,
        // not as the bottom edge of the square.
        painter.drawLine(QPointF(r.left(), baseY), QPointF(r.right(), baseY));
        painter.drawLine(QPointF(upX, baseY), QPointF(upX, upTop));
        painter.drawLine(QPointF(upX + sq, baseY), QPointF(upX + sq, midY));
        painter.drawLine(QPointF(upX, midY), QPointF(upX + sq, midY));
        break;
    }
    case ConstraintType::Coincident: {
        // FreeCAD's form, at the size a badge can actually hold: a LINE
        // with a filled point on it, a triangle pointing UP at that point,
        // and the ring being brought onto it.
        //
        // The triangle is the reason this reads as an instruction rather
        // than a statement, so it stays. What went is FreeCAD's dashed
        // leader between the triangle and the ring: four bands stacked in
        // nine pixels at badge weight fuse into a single blob, three fit.
        // The arrow points UP: the free ring travels onto the point that
        // already lies on the line, not the other way round.
        const QPen savedPen = painter.pen();
        const double lineY = r.top() + h * 0.18;

        painter.drawLine(QPointF(r.left(), lineY), QPointF(r.right(), lineY));

        painter.setPen(Qt::NoPen);
        painter.setBrush(savedPen.color());
        painter.drawEllipse(QPointF(c.x(), lineY), w * 0.14, h * 0.14);

        // Half again the size it was, kept centered between the point and
        // the ring so the gaps either side survive.
        const double apexY = r.top() + h * 0.365;
        const double baseY = r.top() + h * 0.605;   // 0.24h tall, was 0.16h
        const double ah    = w * 0.195;             // was 0.13w
        QPolygonF head;
        head << QPointF(c.x(), apexY)
             << QPointF(c.x() - ah, baseY)
             << QPointF(c.x() + ah, baseY);
        painter.drawPolygon(head);

        painter.setBrush(Qt::NoBrush);

        // FreeCAD's dashed leader, restored: it is what carries the eye
        // from the ring to the point it is being brought onto. It was
        // dropped when the badge had room for three elements; at twenty
        // pixels there is room for four.
        QPen leader = savedPen;
        leader.setStyle(Qt::DotLine);
        leader.setWidthF(savedPen.widthF() * 0.7);
        painter.setPen(leader);
        painter.drawLine(QPointF(c.x(), baseY + h * 0.04),
                         QPointF(c.x(), r.bottom() - h * 0.30));

        painter.setPen(savedPen);
        painter.drawEllipse(QPointF(c.x(), r.bottom() - h * 0.14),
                            w * 0.13, h * 0.13);
        break;
    }

    case ConstraintType::Tangent: {
        // The line runs at 45 degrees and touches the circle, rather than
        // sitting flat across its top: a horizontal line against a circle
        // reads as "resting on", where the point of the glyph is that the
        // two meet at exactly one place whatever the angle.
        const double k  = 0.70710678;           // cos/sin 45
        const double cr = 0.30 * qMin(w, h);
        const QPointF cc(c.x() - w * 0.06, c.y() + h * 0.06);
        painter.drawEllipse(cc, cr, cr);

        // Touch point is one radius along the line's normal; for a line
        // running up-right that normal points down-right.
        const QPointF t(cc.x() + cr * k, cc.y() + cr * k);
        const double half = w * 0.52;
        painter.drawLine(QPointF(t.x() - half * k, t.y() + half * k),
                         QPointF(t.x() + half * k, t.y() - half * k));
        break;
    }
    case ConstraintType::Equal: {
        // Twice the stroke weight of the other glyphs. An equals sign made
        // of hairlines reads as two stray lines; the whole point is that
        // the two bars are unmistakably a matched pair.
        QPen thin = painter.pen();
        QPen thick = thin;
        thick.setWidthF(thin.widthF() * 2.0);
        painter.setPen(thick);
        painter.drawLine(QPointF(r.left(), c.y() - h * 0.22),
                         QPointF(r.right(), c.y() - h * 0.22));
        painter.drawLine(QPointF(r.left(), c.y() + h * 0.22),
                         QPointF(r.right(), c.y() + h * 0.22));
        painter.setPen(thin);
        break;
    }
    case ConstraintType::Midpoint: {
        const QPen savedPen = painter.pen();

        // Onshape's dot between equal flanks, with Fusion's triangle above
        // it, a hybrid of the two. Open rather than filled: coincident's
        // solid triangle is an instruction to move something, this one
        // only points out which point is the middle.
        //
        // The whole line sits slightly below center to leave room for the
        // triangle at the size coincident uses. Centering it and shrinking
        // the triangle instead was the other option; keeping one triangle
        // size across both glyphs matters more than a centered line.
        // A little further down again: the taller triangle below would
        // otherwise put its apex through the top of the box.
        // Far enough down that the taller triangle's apex, plus the half
        // pen width it spreads upward, stays inside the drawing rect.
        const double midY = c.y() + h * 0.12;
        const QPointF mid(c.x(), midY);

        const double dotR  = w * 0.12;
        const double haloR = w * 0.25;
        const double gap   = haloR - dotR;      // the gap either side
        const double penHalf = savedPen.widthF() * 0.5;

        painter.drawLine(QPointF(r.left(), midY),
                         QPointF(r.left() + w * 0.36, midY));
        painter.drawLine(QPointF(r.right() - w * 0.36, midY),
                         QPointF(r.right(), midY));

        if (chipFill.isValid()) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(chipFill);
            painter.drawEllipse(mid, haloR, haloR);
        }
        painter.setPen(Qt::NoPen);
        painter.setBrush(savedPen.color());
        painter.drawEllipse(mid, dotR, dotR);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(savedPen);

        // Triangle base stands off the dot by the SAME gap the flanks do,
        // so the spacing reads as one measurement in both directions. The
        // pen's half width is taken off because the base is stroked and
        // spreads toward the dot.
        // Narrower and taller than coincident's: two pixels off the base
        // and two onto the height, at the scale these are drawn for
        // review. Being a different shape from coincident's is useful in
        // itself: one is an instruction, the other a label.
        const double triH = h * 0.2855;         // was 0.24
        const double triW = w * 0.1723;         // was 0.195
        const double baseY = midY - dotR - gap - penHalf;
        QPolygonF marker;
        marker << QPointF(c.x(), baseY - triH)
               << QPointF(c.x() - triW, baseY)
               << QPointF(c.x() + triW, baseY);
        painter.drawPolygon(marker);
        break;
    }

    case ConstraintType::Symmetric: {
        // Two arrowheads facing a center line, and that line is DASHED,
        // the same way construction geometry is drawn, because that is
        // what a symmetry axis is: a line you mirror about, not an edge of
        // the part. SolidWorks draws its symmetry badge the same way.
        QPen solidPen = painter.pen();
        QPen axisPen = solidPen;
        // Lighter than the arrowheads: at full glyph weight the dashes are
        // chunky enough to read as a solid bar, which is the opposite of
        // what a construction line should look like.
        axisPen.setWidthF(solidPen.widthF() * 0.6);
        painter.setPen(axisPen);

        // Drawn as explicit long-short-long segments rather than with a
        // dash STYLE. A dashed pen starts its pattern at one end, so where
        // the gaps fall depends on the length and the two ends come out
        // uneven, on the one glyph whose whole subject is symmetry.
        // Long-short-long is also how a centerline is drawn in drafting,
        // which is exactly what a symmetry axis is.
        const double longLen = h * 0.30;
        const double halfMid = h * 0.07;
        painter.drawLine(QPointF(c.x(), r.top()),
                         QPointF(c.x(), r.top() + longLen));
        painter.drawLine(QPointF(c.x(), c.y() - halfMid),
                         QPointF(c.x(), c.y() + halfMid));
        painter.drawLine(QPointF(c.x(), r.bottom() - longLen),
                         QPointF(c.x(), r.bottom()));
        painter.setPen(solidPen);
        const double a = w * 0.26;   // arrow depth
        const double t = h * 0.22;   // half-height of the head
        // Clearance between an arrow tip and the axis. At 0.12 the tips
        // nearly touched the dashes and the three read as one connected
        // mark; the gap is what says the arrows point AT the axis from
        // either side. 0.18 is as far out as they go: 0.18 + 0.26 of
        // depth plus half the pen still lands inside the box.
        const double gap = w * 0.18;
        painter.drawLine(QPointF(c.x() - gap, c.y()),
                         QPointF(c.x() - gap - a, c.y() - t));
        painter.drawLine(QPointF(c.x() - gap, c.y()),
                         QPointF(c.x() - gap - a, c.y() + t));
        painter.drawLine(QPointF(c.x() + gap, c.y()),
                         QPointF(c.x() + gap + a, c.y() - t));
        painter.drawLine(QPointF(c.x() + gap, c.y()),
                         QPointF(c.x() + gap + a, c.y() + t));
        break;
    }
    case ConstraintType::Concentric:
        painter.drawEllipse(c, w * 0.45, h * 0.45);
        painter.drawEllipse(c, w * 0.18, h * 0.18);
        break;
    case ConstraintType::Collinear: {
        // Fusion's form: a longer segment carrying an arrowhead, a gap,
        // then a shorter segment continuing the same line. The arrow is
        // what says "these belong on one line" rather than merely showing
        // two strokes that happen to align.
        const QPointF a1(r.left() + w * 0.04, r.bottom() - h * 0.04);
        const QPointF a2(r.left() + w * 0.52, r.bottom() - h * 0.52);
        painter.drawLine(a1, a2);
        // arrowhead at a2, pointing up-right along the 45 degree run
        const double ah = w * 0.17;
        painter.drawLine(a2, QPointF(a2.x() - ah, a2.y()));
        painter.drawLine(a2, QPointF(a2.x(), a2.y() + ah));
        // gap, then the shorter continuation
        painter.drawLine(QPointF(r.left() + w * 0.70, r.bottom() - h * 0.70),
                         QPointF(r.right() - w * 0.04, r.top() + h * 0.04));
        break;
    }
    case ConstraintType::PointOnLine: {
        // FreeCAD's point-on-object reading: the point sits ON the object,
        // filled, rather than hovering beside it as a ring.
        //
        // The line runs diagonally on purpose. Horizontal, it was a dot on
        // a rule and near enough identical to midpoint and to equal at
        // badge size; on the diagonal it is unmistakably its own glyph,
        // and the short cross-stroke through the point says the point is
        // pinned to that line rather than merely touching it.
        const QPen savedPen = painter.pen();
        // Top-left to bottom-right at a true 45 degrees. Equal offsets on
        // both axes, so the slope is exactly 1 whatever the box. Falling
        // rather than rising also sets it apart from parallel, collinear
        // and tangent, which all run the other way.
        // Short enough that the dot stays the subject: the line is there
        // to say WHAT the point is on, not to fill the box.
        const double d = qMin(w, h) * 0.32;
        const QPointF a(c.x() - d, c.y() - d);
        const QPointF b(c.x() + d, c.y() + d);
        // Heavier than the default stroke: this line is the OBJECT the
        // point is held to, and giving it weight is what separates the two
        // roles in the glyph.
        QPen objectPen = savedPen;
        objectPen.setWidthF(savedPen.widthF() * 1.25);
        painter.setPen(objectPen);
        painter.drawLine(a, b);
        painter.setPen(savedPen);

        // A cross-stroke through the point was tried and dropped: at
        // badge size it turned the point into a small star and the glyph
        // read as a blob on a diagonal. FreeCAD's is just a filled point
        // sitting on the object, and that is enough.
        const QPointF mid((a.x() + b.x()) * 0.5, (a.y() + b.y()) * 0.5);

        // An OPEN dot, filled with the chip color rather than left hollow:
        // the object line runs underneath it, and an unfilled ring would
        // show that line straight through its middle. Open also keeps this
        // distinct from coincident, whose point on a line is solid.
        painter.setBrush(chipFill.isValid() ? QBrush(chipFill) : QBrush(Qt::NoBrush));
        painter.setPen(savedPen);
        // Same radius as point-on-circle's solid point, so the two read
        // as the same kind of thing (one open, one closed) rather than
        // as two different sizes of dot.
        painter.drawEllipse(mid, w * 0.15, h * 0.15);
        painter.setBrush(Qt::NoBrush);
        break;
    }
    case ConstraintType::PointOnCircle:
        // The dot must sit ON the arc. Previously it was placed at the top
        // of the bounding rect, which the 30-150 degree arc never reaches,
        // so the glyph showed a point floating off its own curve.
        painter.drawArc(r.toRect(), 30 * 16, 120 * 16);
        painter.setBrush(painter.pen().color());
        painter.drawEllipse(QPointF(c.x(), c.y() - h * 0.5), w * 0.15, h * 0.15);
        painter.setBrush(Qt::NoBrush);
        break;
    case ConstraintType::FixedPoint: {
        // Padlock. The arc ends at its own horizontal diameter, which by
        // default sat well above the body and left the lock looking OPEN.
        const QRectF body(r.left() + w * 0.2, c.y(), w * 0.6, h * 0.5);
        painter.drawRect(body);

        // The shackle is positioned so the arc's OWN ends land on the body:
        // its horizontal diameter sits exactly at the body's top edge.
        // Drawing legs down to the body instead leaves a visible joint and
        // the shackle reads as two posts under a hoop.
        const double shH = h * 0.62;
        const QRectF shackle(r.left() + w * 0.3, body.top() - shH * 0.5,
                             w * 0.4, shH);
        painter.drawArc(shackle.toRect(), 0, 180 * 16);
        break;
    }
    case ConstraintType::FixedAngle: {
        // FreeCAD's internal-angle form: a vertex with two arms swinging
        // symmetrically ABOVE and BELOW the horizontal, and an arc spanning
        // between them. Without the arc it is a wedge, and a wedge is a
        // shape rather than a measurement.
        //
        // The angle is theirs, measured off the icon rather than guessed:
        // its arms path runs M 58,8 -> 6,31 -> 58,56, which is +23.9 and
        // -25.7 degrees about the horizontal.
        const double kArm = 25.0;
        const double rad  = qDegreesToRadians(kArm);
        const QPointF vertex(r.left() + w * 0.08, c.y());
        const double len = w * 0.86;
        const QPointF up(vertex.x() + len * qCos(rad),
                         vertex.y() - len * qSin(rad));
        const QPointF dn(vertex.x() + len * qCos(rad),
                         vertex.y() + len * qSin(rad));
        painter.drawLine(vertex, up);
        painter.drawLine(vertex, dn);

        // The arc is drawn lighter than the arms; they are the geometry
        // being constrained, the arc is only the annotation measuring
        // between them. At equal weight the arc competes with the arms and
        // the glyph reads as a solid wedge.
        const QPen armPen = painter.pen();
        QPen arcPen = armPen;
        arcPen.setWidthF(armPen.widthF() * 0.62);   // arms ~60% thicker
        painter.setPen(arcPen);

        const double ar = qMin(w, h) * 0.46;
        const QRectF arcBox(vertex.x() - ar, vertex.y() - ar, ar * 2, ar * 2);
        painter.drawArc(arcBox, static_cast<int>(-kArm * 16),
                        static_cast<int>(2 * kArm * 16));
        painter.setPen(armPen);
        break;
    }

    default:
        break;
    }
}

// =====================================================================
//  Group indicator (not a constraint; see the header)
// =====================================================================

void drawGroupGlyph(QPainter& painter, const QRectF& r)
{
    const double w = r.width();
    const double h = r.height();
    const QPen base = painter.pen();
    const double penW = base.widthF();

    painter.save();

    // ---- Corner brackets ------------------------------------------------
    // Lighter than the interior on purpose: they are the frame, not the
    // subject. At equal weight the eight bracket strokes outnumber the two
    // interior shapes and the glyph reads as a viewfinder with something
    // incidental inside it.
    QPen bracketPen = base;
    bracketPen.setWidthF(penW * 0.8);
    painter.setPen(bracketPen);

    const double arm = w * 0.22;
    const double x0 = r.left(), x1 = r.right();
    const double y0 = r.top(),  y1 = r.bottom();
    painter.drawLine(QPointF(x0, y0), QPointF(x0 + arm, y0));
    painter.drawLine(QPointF(x0, y0), QPointF(x0, y0 + arm));
    painter.drawLine(QPointF(x1, y0), QPointF(x1 - arm, y0));
    painter.drawLine(QPointF(x1, y0), QPointF(x1, y0 + arm));
    painter.drawLine(QPointF(x0, y1), QPointF(x0 + arm, y1));
    painter.drawLine(QPointF(x0, y1), QPointF(x0, y1 - arm));
    painter.drawLine(QPointF(x1, y1), QPointF(x1 - arm, y1));
    painter.drawLine(QPointF(x1, y1), QPointF(x1, y1 - arm));

    // ---- Interior: two members, one in front ----------------------------
    // Proportions are the centerlines of Tinkercad's icon-ungroup taken
    // within its 44..128 content box, so the overlap matches theirs rather
    // than approximating it.
    const QRectF box = r.adjusted(w * 0.20, h * 0.20, -w * 0.20, -h * 0.20);
    const double s = box.width();
    const QRectF square(box.left() + s * 0.036, box.top() + s * 0.036,
                        s * 0.738, s * 0.738);
    const QPointF center(box.left() + s * 0.607, box.top() + s * 0.607);
    const double radius = s * 0.345;

    // The square is cut where the circle covers it. Done by subtracting the
    // disc from a STROKED square rather than with setClipPath, because Qt
    // clip paths are aliased and the cut lands on the two most visible
    // junctions in the glyph. The disc is grown by half a pen width so the
    // square's stroke ends flush against the circle's outer edge, leaving
    // neither a gap nor an overlap.
    QPainterPath squarePath;
    squarePath.addRect(square);
    QPainterPathStroker stroker;
    stroker.setWidth(penW);
    stroker.setCapStyle(Qt::FlatCap);
    stroker.setJoinStyle(Qt::MiterJoin);
    QPainterPath disc;
    disc.addEllipse(center, radius + penW * 0.5, radius + penW * 0.5);
    painter.fillPath(stroker.createStroke(squarePath).subtracted(disc),
                     QBrush(base.color()));

    painter.setPen(base);
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(center, radius, radius);

    painter.restore();
}

void drawPivotGlyph(QPainter& painter, const QRectF& r, bool withArcArrow)
{
    painter.save();
    const QPen pen = painter.pen();
    const double s = qMin(r.width(), r.height());
    const QPointF c = r.center();

    // Six-pointed star as one 12-vertex polygon: outer tips at 90 + 60k
    // degrees (tip 0 straight up), inner vertices between them. 0.55 keeps
    // the points sharp at 16 px; two overlapping triangles would give 0.577.
    const double outer = withArcArrow ? 0.30 * s : 0.48 * s;
    const double inner = 0.55 * outer;
    QPolygonF star;
    for (int i = 0; i < 12; ++i) {
        const double a = qDegreesToRadians(90.0 + 30.0 * i);
        const double rad = (i % 2 == 0) ? outer : inner;
        star << QPointF(c.x() + rad * qCos(a), c.y() - rad * qSin(a));
    }
    painter.drawPolygon(star);

    if (withArcArrow) {
        // The hemisphere OVER the star: from 3 o'clock across the top to
        // 9 o'clock, heads at both ends pointing down. Screen y grows
        // downward, so "up" is -y throughout.
        const double R = 0.44 * s;
        painter.setBrush(Qt::NoBrush);
        painter.drawArc(QRectF(c.x() - R, c.y() - R, 2 * R, 2 * R), 0 * 16, 180 * 16);
        auto head = [&](double deg) {
            const double a = qDegreesToRadians(deg);
            const QPointF radial(qCos(a), -qSin(a));          // outward from the center
            const QPointF down(0.0, 1.0);                       // both heads point down
            const QPointF end = c + radial * R;
            QPolygonF tri;
            tri << end + down * (0.14 * s) << end + radial * (0.055 * s) << end - radial * (0.055 * s);
            painter.setBrush(pen.color());
            painter.drawPolygon(tri);
        };
        head(0.0);
        head(180.0);
    }

    painter.restore();
}

}  // namespace hobbycad
