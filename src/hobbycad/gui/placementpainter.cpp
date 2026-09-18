// =====================================================================
//  src/hobbycad/gui/placementpainter.cpp — drawing a placement preview
// =====================================================================
//
//  Part of HobbyCAD.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include "placementpainter.h"

#include "screenmath.h"
#include "sketchcanvas.h"

#include <hobbycad/geometry/utils.h>
#include <hobbycad/units.h>

#include <QCoreApplication>
#include <QFontMetricsF>
#include <QPainter>
#include <QPen>
#include <QPolygonF>

#include <algorithm>
#include <cmath>

namespace hobbycad {

namespace {

using sketch::IdleLabel;
using sketch::LabelPlace;
using sketch::PreviewMark;
using sketch::PreviewShape;
using sketch::PreviewStroke;

const QColor kPlaced(0, 120, 215);
const QColor kAccent(255, 140, 0);
const QColor kHandle(60, 120, 215);

QPointF screen(const SketchCanvas& canvas, const Point2D& p)
{
    return canvas.toScreenF(QPointF(p));
}

/// A text angle along `from`->`to` that never reads upside down.
double readableAngle(const QPointF& from, const QPointF& to)
{
    double angle = screenAngleDeg(from, to);
    if (angle > 90.0) angle -= 180.0;
    if (angle < -90.0) angle += 180.0;
    return angle;
}

QFont labelFont(const QPainter& painter)
{
    QFont font = painter.font();
    font.setPointSize(9);
    return font;
}

void paintPath(const SketchCanvas& canvas, QPainter& painter, const PreviewShape& s)
{
    QPolygonF poly;
    for (const Point2D& p : s.points) poly << screen(canvas, p);
    QPen pen = painter.pen();
    switch (s.stroke) {
    case PreviewStroke::Pen:          break;
    case PreviewStroke::Solid:        pen.setStyle(Qt::SolidLine); break;
    case PreviewStroke::Guide:        pen = QPen(QColor(128, 128, 128), 1, Qt::DashLine); break;
    case PreviewStroke::Faint:        pen = QPen(QColor(128, 128, 128, 110), 1, Qt::DashLine);
                                      break;
    case PreviewStroke::Construction: pen = QPen(QColor(180, 100, 50), 1, Qt::DashLine); break;
    case PreviewStroke::Target:       pen = QPen(kHandle, 2); break;
    case PreviewStroke::Accent:       pen = QPen(kAccent, 1); break;
    case PreviewStroke::Handle:       pen = QPen(kHandle, 1); break;
    }
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    if (s.closed) {
        painter.drawPolygon(poly);
    } else {
        painter.drawPolyline(poly);
    }
}

void paintMark(const SketchCanvas& canvas, QPainter& painter, const PreviewShape& s)
{
    const QPointF at = screen(canvas, s.points[0]);
    painter.setBrush(Qt::NoBrush);
    switch (s.mark) {
    case PreviewMark::Click:
        painter.setBrush(kPlaced);
        painter.drawEllipse(at, 3, 3);
        break;
    case PreviewMark::BigClick:
        painter.setBrush(kPlaced);
        painter.drawEllipse(at, 4, 4);
        break;
    case PreviewMark::Center:
        paintCenterCross(painter, at, 4);
        break;
    case PreviewMark::Snapped:
        paintCenterCross(painter, at, 6);
        painter.drawEllipse(at, 8, 8);
        break;
    case PreviewMark::Cursor:
        painter.drawEllipse(at, 4, 4);
        break;
    case PreviewMark::Corner:
        painter.setPen(QPen(kAccent, 1));
        painter.setBrush(kAccent);
        painter.drawEllipse(at, 4, 4);
        break;
    case PreviewMark::Tip:
        painter.setBrush(QColor(255, 100, 100));
        painter.drawEllipse(at, 2, 2);
        break;
    case PreviewMark::CloseLoop:
        painter.setBrush(QColor(0, 180, 100));
        painter.drawEllipse(at, 5, 5);
        break;
    case PreviewMark::Anchor:
    case PreviewMark::NextAnchor:
        painter.setPen(QPen(QColor(138, 100, 0), 1));
        if (s.mark == PreviewMark::Anchor) painter.setBrush(QColor(230, 165, 0));
        painter.drawEllipse(at, 4, 4);
        break;
    case PreviewMark::HandleEnd:
        painter.setPen(QPen(kHandle, 1));
        painter.setBrush(kHandle);
        painter.drawRect(QRectF(at.x() - 3, at.y() - 3, 6, 6));
        break;
    }
}

/// An angle's value by its mark, when no field is being typed in.
void paintAngleText(QPainter& painter, const QPointF& at, double degrees)
{
    const QString text = QString::fromStdString(formatAngle(degrees));
    painter.setPen(kAccent);
    const QFont font = labelFont(painter);
    painter.setFont(font);
    QRectF box = QFontMetricsF(font).boundingRect(text);
    box.moveCenter(at);
    box.adjust(-2, -1, 2, 1);
    painter.fillRect(box, QColor(255, 255, 255, 200));
    painter.drawText(box, Qt::AlignCenter, text);
}

void paintDimension(SketchCanvas& canvas, QPainter& painter, const PreviewShape& s)
{
    const QPointF a = screen(canvas, s.points[0]);
    const QPointF b = screen(canvas, s.points[1]);
    const QPointF mid = (a + b) / 2.0;
    const QPointF rowDrop(0.0, 20.0 * s.row);
    QPointF at = mid;
    double rotation = 0.0;
    switch (s.place) {
    case LabelPlace::Along:
        at = mid + QPointF(0.0, 18.0) + rowDrop;
        rotation = readableAngle(a, b);
        break;
    case LabelPlace::Below:
        at = mid + QPointF(0.0, 18.0) + rowDrop;
        break;
    case LabelPlace::Right:
        at = mid + QPointF(s.gap, 0.0);
        break;
    case LabelPlace::Across: {
        const QPointF perp(geometry::perpendicular(geometry::normalize(Point2D(b - a))));
        at = mid - perp * s.gap;
        rotation = readableAngle(a, b);
        break;
    }
    case LabelPlace::Outside: {
        const QPointF out = b - a;
        const double len = geometry::length(Point2D(out));
        if (len > geometry::kDegenerateLen) at = a + out * ((len + s.gap) / len);
        at += rowDrop;
        break;
    }
    case LabelPlace::Angle: {
        // The mark: an arc at the vertex between its two sides.
        const QPointF c = screen(canvas, s.points[2]);
        const double radius = std::min(30.0, std::min(geometry::length(Point2D(b - a)),
                                                      geometry::length(Point2D(c - a))) * 0.3);
        const double start = painterAngleDeg(a, b);
        const double sweep = geometry::wrapSweepDeg(painterAngleDeg(a, c) - start);
        painter.save();
        painter.setPen(QPen(kAccent, 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawArc(QRectF(a.x() - radius, a.y() - radius, radius * 2, radius * 2),
                        static_cast<int>(start * 16), static_cast<int>(sweep * 16));
        painter.restore();
        const double middle = degreesToRadians(start + sweep / 2.0);
        at = a + QPointF(std::cos(-middle), std::sin(-middle)) * (radius + 15.0);
        break;
    }
    }

    const bool typing = s.field >= 0 && canvas.activeDimField() >= 0
                     && s.field < canvas.dimFieldCount();
    if (typing) {
        canvas.paintDimInputField(painter, at, s.field, rotation);
        return;
    }
    switch (s.idle) {
    case IdleLabel::Line:
        canvas.paintPreviewDimension(painter, a.toPoint(), b.toPoint(), s.value);
        break;
    case IdleLabel::Value:
        canvas.paintDimensionLabel(painter, at, s.value);
        break;
    case IdleLabel::ValueAside:
        canvas.paintDimensionLabel(painter, mid + QPointF(10.0, -10.0), s.value);
        break;
    case IdleLabel::ArcValue:
        canvas.paintArcDimensionLabel(painter, at, s.value, s.sweep);
        break;
    case IdleLabel::AngleValue:
        paintAngleText(painter, at, s.value);
        break;
    case IdleLabel::None:
        break;
    }
}

void paintNote(const SketchCanvas& canvas, QPainter& painter, const PreviewShape& s)
{
    QStringList lines;
    for (const char* line : s.lines) {
        lines << QCoreApplication::translate(sketch::placementContext(), line);
    }
    if (lines.isEmpty()) return;

    if (s.noteStyle == sketch::NoteStyle::Plain) {
        // Gray lines centered under the anchor.
        const QFontMetricsF fm(painter.font());
        double width = 0.0;
        for (const QString& line : lines) width = std::max(width, fm.horizontalAdvance(line));
        const QPointF anchor = screen(canvas, s.points[0]);
        QPointF at(anchor.x() - width / 2.0, anchor.y() + s.gap);
        painter.setPen(QColor(80, 80, 80));
        for (const QString& line : lines) {
            painter.drawText(at, line);
            at.ry() += fm.height() + 2.0;
        }
        return;
    }

    const QFont font = labelFont(painter);
    painter.setFont(font);
    painter.setPen(kPlaced);
    const QFontMetricsF fm(font);
    if (s.noteAlong) {
        // Above the middle of its line, turned with it.
        const QPointF a = screen(canvas, s.points[0]);
        const QPointF b = screen(canvas, s.points[1]);
        QRectF box = fm.boundingRect(lines.front());
        painter.translate((a + b) / 2.0);
        painter.rotate(readableAngle(a, b));
        box.moveCenter(QPointF(0.0, -box.height() / 2.0 - 4.0));
        painter.fillRect(box.adjusted(-2, -1, 2, 1), QColor(255, 255, 255, 200));
        painter.drawText(box, Qt::AlignCenter, lines.front());
        return;
    }

    // Lines on a light box, centered under the anchor.
    std::vector<QRectF> boxes;
    double width = 0.0;
    double height = 0.0;
    for (const QString& line : lines) {
        boxes.push_back(fm.boundingRect(line));
        width = std::max(width, boxes.back().width());
        height += boxes.back().height() + 2.0;
    }
    const QPointF top = screen(canvas, s.points[0]) + QPointF(0.0, s.gap);
    painter.fillRect(QRectF(top.x() - width / 2.0 - 2.0, top.y(), width + 4.0, height),
                     QColor(255, 255, 255, 200));
    double y = top.y();
    for (int i = 0; i < lines.size(); ++i) {
        y += boxes[static_cast<std::size_t>(i)].height();
        painter.drawText(QPointF(top.x() - boxes[static_cast<std::size_t>(i)].width() / 2.0, y),
                         lines[i]);
        y += 2.0;
    }
}

}  // namespace

void paintPlacementPreview(SketchCanvas& canvas, QPainter& painter,
                           const sketch::PlacementPreview& preview)
{
    // The fields show the placement's live values.
    for (std::size_t i = 0; i < preview.fieldValues.size(); ++i) {
        canvas.setDimFieldValue(static_cast<int>(i), preview.fieldValues[i]);
    }
    for (const PreviewShape& s : preview.shapes) {
        if (s.points.empty()) continue;
        painter.save();
        switch (s.kind) {
        case PreviewShape::Kind::Path:
            paintPath(canvas, painter, s);
            break;
        case PreviewShape::Kind::Mark:
            paintMark(canvas, painter, s);
            break;
        case PreviewShape::Kind::Dimension:
            if (s.points.size() >= 2) paintDimension(canvas, painter, s);
            break;
        case PreviewShape::Kind::Note:
            paintNote(canvas, painter, s);
            break;
        }
        painter.restore();
    }
}

}  // namespace hobbycad
