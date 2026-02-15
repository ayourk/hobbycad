// =====================================================================
//  src/libhobbycad/sketch/export.cpp — Sketch export implementation
// =====================================================================
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include <hobbycad/sketch/export.h>
#include <hobbycad/sketch/queries.h>
#include <hobbycad/geometry/types.h>

#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QtMath>

namespace hobbycad {
namespace sketch {

// =====================================================================
//  SVG Export
// =====================================================================

namespace {

QString entityToSVGPath(const Entity& entity, double scale)
{
    QString path;

    switch (entity.type) {
    case EntityType::Point:
        // Points rendered as small circles
        if (!entity.points.isEmpty()) {
            double x = entity.points[0].x() * scale;
            double y = -entity.points[0].y() * scale;  // SVG Y is inverted
            path = QString("M %1 %2 m -1 0 a 1 1 0 1 0 2 0 a 1 1 0 1 0 -2 0")
                   .arg(x).arg(y);
        }
        break;

    case EntityType::Line:
        if (entity.points.size() >= 2) {
            double x1 = entity.points[0].x() * scale;
            double y1 = -entity.points[0].y() * scale;
            double x2 = entity.points[1].x() * scale;
            double y2 = -entity.points[1].y() * scale;
            path = QString("M %1 %2 L %3 %4").arg(x1).arg(y1).arg(x2).arg(y2);
        }
        break;

    case EntityType::Circle:
        if (!entity.points.isEmpty()) {
            double cx = entity.points[0].x() * scale;
            double cy = -entity.points[0].y() * scale;
            double r = entity.radius * scale;
            // SVG circle as two arcs
            path = QString("M %1 %2 A %3 %3 0 1 0 %4 %2 A %3 %3 0 1 0 %1 %2")
                   .arg(cx - r).arg(cy).arg(r).arg(cx + r);
        }
        break;

    case EntityType::Arc:
        if (!entity.points.isEmpty()) {
            double cx = entity.points[0].x();
            double cy = entity.points[0].y();
            double r = entity.radius;
            double startRad = qDegreesToRadians(entity.startAngle);
            double endRad = qDegreesToRadians(entity.startAngle + entity.sweepAngle);

            double x1 = (cx + r * qCos(startRad)) * scale;
            double y1 = -(cy + r * qSin(startRad)) * scale;
            double x2 = (cx + r * qCos(endRad)) * scale;
            double y2 = -(cy + r * qSin(endRad)) * scale;

            int largeArc = qAbs(entity.sweepAngle) > 180 ? 1 : 0;
            int sweep = entity.sweepAngle > 0 ? 0 : 1;  // Inverted due to Y flip

            path = QString("M %1 %2 A %3 %3 0 %4 %5 %6 %7")
                   .arg(x1).arg(y1)
                   .arg(r * scale)
                   .arg(largeArc).arg(sweep)
                   .arg(x2).arg(y2);
        }
        break;

    case EntityType::Rectangle:
        if (entity.points.size() >= 2) {
            double x1 = entity.points[0].x() * scale;
            double y1 = -entity.points[0].y() * scale;
            double x2 = entity.points[1].x() * scale;
            double y2 = -entity.points[1].y() * scale;
            path = QString("M %1 %2 L %3 %2 L %3 %4 L %1 %4 Z")
                   .arg(x1).arg(y1).arg(x2).arg(y2);
        }
        break;

    case EntityType::Polygon:
        if (!entity.points.isEmpty()) {
            path = QString("M %1 %2")
                   .arg(entity.points[0].x() * scale)
                   .arg(-entity.points[0].y() * scale);
            for (int i = 1; i < entity.points.size(); ++i) {
                path += QString(" L %1 %2")
                        .arg(entity.points[i].x() * scale)
                        .arg(-entity.points[i].y() * scale);
            }
            path += " Z";
        }
        break;

    case EntityType::Ellipse:
        if (!entity.points.isEmpty()) {
            double cx = entity.points[0].x() * scale;
            double cy = -entity.points[0].y() * scale;
            double rx = entity.majorRadius * scale;
            double ry = entity.minorRadius * scale;
            // Ellipse as two arcs
            path = QString("M %1 %2 A %3 %4 0 1 0 %5 %2 A %3 %4 0 1 0 %1 %2")
                   .arg(cx - rx).arg(cy).arg(rx).arg(ry).arg(cx + rx);
        }
        break;

    case EntityType::Spline:
        // Approximate as polyline
        if (!entity.points.isEmpty()) {
            path = QString("M %1 %2")
                   .arg(entity.points[0].x() * scale)
                   .arg(-entity.points[0].y() * scale);
            for (int i = 1; i < entity.points.size(); ++i) {
                path += QString(" L %1 %2")
                        .arg(entity.points[i].x() * scale)
                        .arg(-entity.points[i].y() * scale);
            }
        }
        break;

    case EntityType::Slot:
        // Tessellate slot
        {
            QVector<QPointF> points = tessellate(entity, 0.5);
            if (!points.isEmpty()) {
                path = QString("M %1 %2")
                       .arg(points[0].x() * scale)
                       .arg(-points[0].y() * scale);
                for (int i = 1; i < points.size(); ++i) {
                    path += QString(" L %1 %2")
                            .arg(points[i].x() * scale)
                            .arg(-points[i].y() * scale);
                }
            }
        }
        break;

    case EntityType::Text:
        // Text not supported in path export
        break;
    }

    return path;
}

}  // anonymous namespace

QString sketchToSVG(
    const QVector<Entity>& entities,
    const QVector<Constraint>& constraints,
    const SVGExportOptions& options)
{
    // Calculate bounds
    geometry::BoundingBox bounds = sketchBounds(entities);
    if (!bounds.valid) {
        bounds = geometry::BoundingBox(0, 0, 100, 100);
    }

    double scale = options.scale;
    double margin = options.margin * scale;

    double width = (bounds.maxX - bounds.minX) * scale + 2 * margin;
    double height = (bounds.maxY - bounds.minY) * scale + 2 * margin;

    // Offset to center sketch in viewBox
    double offsetX = -bounds.minX * scale + margin;
    double offsetY = bounds.maxY * scale + margin;  // Y inverted

    QString svg;
    QTextStream out(&svg);

    // SVG header
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out << QString("<svg xmlns=\"http://www.w3.org/2000/svg\" "
                   "width=\"%1mm\" height=\"%2mm\" "
                   "viewBox=\"0 0 %1 %2\">\n")
           .arg(width).arg(height);

    // Style definitions
    out << "  <defs>\n";
    out << QString("    <style>\n"
                   "      .entity { stroke: %1; stroke-width: %2; fill: %3; }\n"
                   "      .construction { stroke: %4; stroke-dasharray: 4 2; }\n"
                   "    </style>\n")
           .arg(options.strokeColor)
           .arg(options.strokeWidth)
           .arg(options.fillColor)
           .arg(options.constructionColor);
    out << "  </defs>\n";

    // Transform group to handle coordinate system
    out << QString("  <g transform=\"translate(%1 %2)\">\n").arg(offsetX).arg(offsetY);

    // Entities
    for (const Entity& entity : entities) {
        QString className = entity.isConstruction ? "entity construction" : "entity";

        // Handle text entities separately
        if (entity.type == EntityType::Text && !entity.points.isEmpty()) {
            double x = entity.points[0].x() * scale;
            double y = -entity.points[0].y() * scale;  // Y inverted
            double fontSize = entity.fontSize * scale;

            QString fontStyle;
            if (!entity.fontFamily.isEmpty()) {
                fontStyle += QString(" font-family=\"%1\"").arg(entity.fontFamily);
            }
            if (entity.fontBold) {
                fontStyle += " font-weight=\"bold\"";
            }
            if (entity.fontItalic) {
                fontStyle += " font-style=\"italic\"";
            }
            QString transform;
            if (qAbs(entity.textRotation) > 0.01) {
                transform = QString(" transform=\"rotate(%1 %2 %3)\"")
                    .arg(-entity.textRotation).arg(x).arg(y);
            }

            out << QString("    <text class=\"%1\" x=\"%2\" y=\"%3\" font-size=\"%4\"%5%6>%7</text>\n")
                   .arg(className).arg(x).arg(y).arg(fontSize)
                   .arg(fontStyle).arg(transform).arg(entity.text.toHtmlEscaped());
            continue;
        }

        QString pathData = entityToSVGPath(entity, scale);
        if (pathData.isEmpty()) continue;

        out << QString("    <path class=\"%1\" d=\"%2\"/>\n")
               .arg(className).arg(pathData);
    }

    // Dimension text (if enabled)
    if (options.includeDimensions) {
        for (const Constraint& c : constraints) {
            if (!c.labelVisible) continue;

            QString label;
            switch (c.type) {
            case ConstraintType::Distance:
            case ConstraintType::Radius:
            case ConstraintType::Diameter:
                label = QString::number(c.value, 'f', 2);
                break;
            case ConstraintType::Angle:
                label = QString::number(c.value, 'f', 1) + QStringLiteral("\u00B0");
                break;
            default:
                continue;
            }

            double x = c.labelPosition.x() * scale;
            double y = -c.labelPosition.y() * scale;
            out << QString("    <text x=\"%1\" y=\"%2\" font-size=\"3\" "
                           "text-anchor=\"middle\">%3</text>\n")
                   .arg(x).arg(y).arg(label);
        }
    }

    out << "  </g>\n";
    out << "</svg>\n";

    return svg;
}

bool exportSketchToSVG(
    const QVector<Entity>& entities,
    const QVector<Constraint>& constraints,
    const QString& filePath,
    const SVGExportOptions& options)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream out(&file);
    out << sketchToSVG(entities, constraints, options);
    return true;
}

// =====================================================================
//  DXF Export
// =====================================================================

namespace {

void writeDXFHeader(QTextStream& out)
{
    out << "0\nSECTION\n2\nHEADER\n";
    out << "9\n$ACADVER\n1\nAC1014\n";  // AutoCAD R14 format
    out << "9\n$INSUNITS\n70\n4\n";     // Millimeters
    out << "0\nENDSEC\n";

    // Tables section (minimal)
    out << "0\nSECTION\n2\nTABLES\n";
    out << "0\nTABLE\n2\nLAYER\n70\n2\n";
    out << "0\nLAYER\n2\n0\n70\n0\n62\n7\n6\nCONTINUOUS\n";
    out << "0\nLAYER\n2\nCONSTRUCTION\n70\n0\n62\n5\n6\nDASHED\n";
    out << "0\nENDTAB\n";
    out << "0\nENDSEC\n";
}

void writeDXFEntity(QTextStream& out, const Entity& entity, const DXFExportOptions& options)
{
    QString layer = entity.isConstruction ? options.constructionLayer : options.layerName;
    int color = entity.isConstruction ? options.constructionColorIndex : options.colorIndex;

    switch (entity.type) {
    case EntityType::Point:
        if (!entity.points.isEmpty()) {
            out << "0\nPOINT\n";
            out << "8\n" << layer << "\n";
            out << "62\n" << color << "\n";
            out << "10\n" << entity.points[0].x() << "\n";
            out << "20\n" << entity.points[0].y() << "\n";
            out << "30\n0\n";
        }
        break;

    case EntityType::Line:
        if (entity.points.size() >= 2) {
            out << "0\nLINE\n";
            out << "8\n" << layer << "\n";
            out << "62\n" << color << "\n";
            out << "10\n" << entity.points[0].x() << "\n";
            out << "20\n" << entity.points[0].y() << "\n";
            out << "30\n0\n";
            out << "11\n" << entity.points[1].x() << "\n";
            out << "21\n" << entity.points[1].y() << "\n";
            out << "31\n0\n";
        }
        break;

    case EntityType::Circle:
        if (!entity.points.isEmpty()) {
            out << "0\nCIRCLE\n";
            out << "8\n" << layer << "\n";
            out << "62\n" << color << "\n";
            out << "10\n" << entity.points[0].x() << "\n";
            out << "20\n" << entity.points[0].y() << "\n";
            out << "30\n0\n";
            out << "40\n" << entity.radius << "\n";
        }
        break;

    case EntityType::Arc:
        if (!entity.points.isEmpty()) {
            out << "0\nARC\n";
            out << "8\n" << layer << "\n";
            out << "62\n" << color << "\n";
            out << "10\n" << entity.points[0].x() << "\n";
            out << "20\n" << entity.points[0].y() << "\n";
            out << "30\n0\n";
            out << "40\n" << entity.radius << "\n";
            out << "50\n" << entity.startAngle << "\n";
            out << "51\n" << (entity.startAngle + entity.sweepAngle) << "\n";
        }
        break;

    case EntityType::Ellipse:
        if (!entity.points.isEmpty()) {
            out << "0\nELLIPSE\n";
            out << "8\n" << layer << "\n";
            out << "62\n" << color << "\n";
            out << "10\n" << entity.points[0].x() << "\n";
            out << "20\n" << entity.points[0].y() << "\n";
            out << "30\n0\n";
            // Major axis endpoint relative to center
            out << "11\n" << entity.majorRadius << "\n";
            out << "21\n0\n";
            out << "31\n0\n";
            // Ratio of minor to major
            out << "40\n" << (entity.minorRadius / entity.majorRadius) << "\n";
            out << "41\n0\n";           // Start parameter
            out << "42\n6.283185\n";    // End parameter (2*PI)
        }
        break;

    case EntityType::Rectangle:
    case EntityType::Polygon:
    case EntityType::Slot:
    case EntityType::Spline:
        // Use LWPOLYLINE for complex shapes
        if (options.usePolylines) {
            QVector<QPointF> points = tessellate(entity, 0.5);
            if (!points.isEmpty()) {
                out << "0\nLWPOLYLINE\n";
                out << "8\n" << layer << "\n";
                out << "62\n" << color << "\n";
                out << "90\n" << points.size() << "\n";
                out << "70\n1\n";  // Closed polyline
                for (const QPointF& p : points) {
                    out << "10\n" << p.x() << "\n";
                    out << "20\n" << p.y() << "\n";
                }
            }
        }
        break;

    case EntityType::Text:
        if (!entity.points.isEmpty()) {
            out << "0\nTEXT\n";
            out << "8\n" << layer << "\n";
            out << "62\n" << color << "\n";
            out << "10\n" << entity.points[0].x() << "\n";
            out << "20\n" << entity.points[0].y() << "\n";
            out << "30\n0\n";
            out << "40\n" << entity.fontSize << "\n";  // Text height
            if (qAbs(entity.textRotation) > 0.01) {
                out << "50\n" << entity.textRotation << "\n";  // Rotation angle
            }
            out << "1\n" << entity.text << "\n";
        }
        break;
    }
}

}  // anonymous namespace

QString sketchToDXF(
    const QVector<Entity>& entities,
    const DXFExportOptions& options)
{
    QString dxf;
    QTextStream out(&dxf);

    writeDXFHeader(out);

    // Entities section
    out << "0\nSECTION\n2\nENTITIES\n";

    for (const Entity& entity : entities) {
        writeDXFEntity(out, entity, options);
    }

    out << "0\nENDSEC\n";
    out << "0\nEOF\n";

    return dxf;
}

bool exportSketchToDXF(
    const QVector<Entity>& entities,
    const QString& filePath,
    const DXFExportOptions& options)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream out(&file);
    out << sketchToDXF(entities, options);
    return true;
}

// =====================================================================
//  SVG Import
// =====================================================================

namespace {

/// SVG path command types
enum class PathCommand {
    MoveTo,         // M, m
    LineTo,         // L, l
    HorizontalLine, // H, h
    VerticalLine,   // V, v
    CurveTo,        // C, c (cubic bezier)
    SmoothCurve,    // S, s
    QuadCurve,      // Q, q (quadratic bezier)
    SmoothQuad,     // T, t
    Arc,            // A, a
    ClosePath       // Z, z
};

/// Parse a number from SVG path data
double parseNumber(const QString& data, int& pos)
{
    // Skip whitespace and commas
    while (pos < data.length() &&
           (data[pos].isSpace() || data[pos] == ',')) {
        ++pos;
    }

    if (pos >= data.length()) return 0.0;

    int start = pos;

    // Handle sign
    if (data[pos] == '-' || data[pos] == '+') {
        ++pos;
    }

    // Integer part
    while (pos < data.length() && data[pos].isDigit()) {
        ++pos;
    }

    // Decimal part
    if (pos < data.length() && data[pos] == '.') {
        ++pos;
        while (pos < data.length() && data[pos].isDigit()) {
            ++pos;
        }
    }

    // Exponent
    if (pos < data.length() && (data[pos] == 'e' || data[pos] == 'E')) {
        ++pos;
        if (pos < data.length() && (data[pos] == '-' || data[pos] == '+')) {
            ++pos;
        }
        while (pos < data.length() && data[pos].isDigit()) {
            ++pos;
        }
    }

    return data.mid(start, pos - start).toDouble();
}

/// Parse a flag (0 or 1) for arc commands
int parseFlag(const QString& data, int& pos)
{
    while (pos < data.length() &&
           (data[pos].isSpace() || data[pos] == ',')) {
        ++pos;
    }
    if (pos < data.length() && (data[pos] == '0' || data[pos] == '1')) {
        return data[pos++].digitValue();
    }
    return 0;
}

/// Convert SVG arc parameters to center parameterization
void svgArcToCenterParams(
    double x1, double y1,           // Start point
    double rx, double ry,           // Radii
    double phi,                     // X-axis rotation (degrees)
    int largeArc, int sweep,        // Flags
    double x2, double y2,           // End point
    double& cx, double& cy,         // Output: center
    double& startAngle,             // Output: start angle (degrees)
    double& sweepAngle)             // Output: sweep angle (degrees)
{
    // Handle degenerate cases
    if (qAbs(x1 - x2) < 1e-10 && qAbs(y1 - y2) < 1e-10) {
        cx = x1; cy = y1;
        startAngle = 0; sweepAngle = 0;
        return;
    }

    // Ensure radii are positive
    rx = qAbs(rx);
    ry = qAbs(ry);

    if (rx < 1e-10 || ry < 1e-10) {
        // Treat as line
        cx = (x1 + x2) / 2;
        cy = (y1 + y2) / 2;
        startAngle = 0;
        sweepAngle = 0;
        return;
    }

    double phiRad = qDegreesToRadians(phi);
    double cosPhi = qCos(phiRad);
    double sinPhi = qSin(phiRad);

    // Step 1: Compute (x1', y1')
    double dx = (x1 - x2) / 2.0;
    double dy = (y1 - y2) / 2.0;
    double x1p = cosPhi * dx + sinPhi * dy;
    double y1p = -sinPhi * dx + cosPhi * dy;

    // Step 2: Compute (cx', cy')
    double rxSq = rx * rx;
    double rySq = ry * ry;
    double x1pSq = x1p * x1p;
    double y1pSq = y1p * y1p;

    // Check if radii are large enough
    double lambda = x1pSq / rxSq + y1pSq / rySq;
    if (lambda > 1.0) {
        double sqrtLambda = qSqrt(lambda);
        rx *= sqrtLambda;
        ry *= sqrtLambda;
        rxSq = rx * rx;
        rySq = ry * ry;
    }

    double num = rxSq * rySq - rxSq * y1pSq - rySq * x1pSq;
    double denom = rxSq * y1pSq + rySq * x1pSq;

    double sq = qMax(0.0, num / denom);
    double coef = qSqrt(sq) * ((largeArc == sweep) ? -1 : 1);

    double cxp = coef * rx * y1p / ry;
    double cyp = -coef * ry * x1p / rx;

    // Step 3: Compute (cx, cy)
    double mx = (x1 + x2) / 2.0;
    double my = (y1 + y2) / 2.0;
    cx = cosPhi * cxp - sinPhi * cyp + mx;
    cy = sinPhi * cxp + cosPhi * cyp + my;

    // Step 4: Compute angles
    auto angle = [](double ux, double uy, double vx, double vy) {
        double dot = ux * vx + uy * vy;
        double len = qSqrt(ux*ux + uy*uy) * qSqrt(vx*vx + vy*vy);
        double ang = qAcos(qBound(-1.0, dot / len, 1.0));
        if (ux * vy - uy * vx < 0) ang = -ang;
        return ang;
    };

    double ux = (x1p - cxp) / rx;
    double uy = (y1p - cyp) / ry;
    double vx = (-x1p - cxp) / rx;
    double vy = (-y1p - cyp) / ry;

    startAngle = qRadiansToDegrees(angle(1, 0, ux, uy));
    sweepAngle = qRadiansToDegrees(angle(ux, uy, vx, vy));

    if (!sweep && sweepAngle > 0) {
        sweepAngle -= 360;
    } else if (sweep && sweepAngle < 0) {
        sweepAngle += 360;
    }
}

/// Approximate cubic bezier with line segments
QVector<QPointF> approximateCubicBezier(
    const QPointF& p0, const QPointF& p1,
    const QPointF& p2, const QPointF& p3,
    double tolerance)
{
    QVector<QPointF> result;

    // Simple recursive subdivision
    std::function<void(QPointF, QPointF, QPointF, QPointF, int)> subdivide;
    subdivide = [&](QPointF a, QPointF b, QPointF c, QPointF d, int depth) {
        if (depth > 10) {
            result.append(d);
            return;
        }

        // Check flatness
        double dx = d.x() - a.x();
        double dy = d.y() - a.y();
        double d2 = qAbs((b.x() - d.x()) * dy - (b.y() - d.y()) * dx);
        double d3 = qAbs((c.x() - d.x()) * dy - (c.y() - d.y()) * dx);

        if ((d2 + d3) * (d2 + d3) < tolerance * (dx*dx + dy*dy)) {
            result.append(d);
            return;
        }

        // Subdivide
        QPointF ab = (a + b) / 2;
        QPointF bc = (b + c) / 2;
        QPointF cd = (c + d) / 2;
        QPointF abc = (ab + bc) / 2;
        QPointF bcd = (bc + cd) / 2;
        QPointF abcd = (abc + bcd) / 2;

        subdivide(a, ab, abc, abcd, depth + 1);
        subdivide(abcd, bcd, cd, d, depth + 1);
    };

    result.append(p0);
    subdivide(p0, p1, p2, p3, 0);
    return result;
}

/// Approximate quadratic bezier with line segments
QVector<QPointF> approximateQuadBezier(
    const QPointF& p0, const QPointF& p1, const QPointF& p2,
    double tolerance)
{
    // Convert to cubic bezier
    QPointF c1 = p0 + 2.0/3.0 * (p1 - p0);
    QPointF c2 = p2 + 2.0/3.0 * (p1 - p2);
    return approximateCubicBezier(p0, c1, c2, p2, tolerance);
}

}  // anonymous namespace

SVGImportResult importSVGPath(
    const QString& svgPathData,
    int startId,
    const SVGImportOptions& options)
{
    SVGImportResult result;
    result.success = false;

    if (svgPathData.isEmpty()) {
        result.errorMessage = QStringLiteral("Empty path data");
        return result;
    }

    QVector<QPointF> currentPath;
    QPointF currentPoint(0, 0);
    QPointF startPoint(0, 0);
    QPointF lastControl(0, 0);
    int nextId = startId;
    double scale = options.scale;
    double ySign = options.flipY ? -1.0 : 1.0;

    auto transformPoint = [&](const QPointF& p) -> QPointF {
        return QPointF(p.x() * scale + options.offset.x(),
                       p.y() * ySign * scale + options.offset.y());
    };

    auto flushPath = [&]() {
        if (currentPath.size() >= 2) {
            // Create line segments
            for (int i = 0; i < currentPath.size() - 1; ++i) {
                Entity line = createLine(nextId++,
                                         transformPoint(currentPath[i]),
                                         transformPoint(currentPath[i+1]));
                result.entities.append(line);
            }
        }
        currentPath.clear();
    };

    int pos = 0;
    QChar lastCommand = 'M';
    bool relative = false;

    while (pos < svgPathData.length()) {
        // Skip whitespace
        while (pos < svgPathData.length() && svgPathData[pos].isSpace()) {
            ++pos;
        }
        if (pos >= svgPathData.length()) break;

        QChar cmd = svgPathData[pos];

        // Check if it's a command letter
        if (cmd.isLetter()) {
            lastCommand = cmd;
            relative = cmd.isLower();
            ++pos;
        } else {
            // Repeat last command (implicit)
            cmd = lastCommand;
            relative = cmd.isLower();
        }

        QChar cmdUpper = cmd.toUpper();

        if (cmdUpper == 'M') {
            flushPath();
            double x = parseNumber(svgPathData, pos);
            double y = parseNumber(svgPathData, pos);
            if (relative) {
                currentPoint += QPointF(x, y);
            } else {
                currentPoint = QPointF(x, y);
            }
            startPoint = currentPoint;
            currentPath.append(currentPoint);
            lastCommand = relative ? 'l' : 'L';  // Subsequent coords are LineTo

        } else if (cmdUpper == 'L') {
            double x = parseNumber(svgPathData, pos);
            double y = parseNumber(svgPathData, pos);
            if (relative) {
                currentPoint += QPointF(x, y);
            } else {
                currentPoint = QPointF(x, y);
            }
            currentPath.append(currentPoint);

        } else if (cmdUpper == 'H') {
            double x = parseNumber(svgPathData, pos);
            if (relative) {
                currentPoint.setX(currentPoint.x() + x);
            } else {
                currentPoint.setX(x);
            }
            currentPath.append(currentPoint);

        } else if (cmdUpper == 'V') {
            double y = parseNumber(svgPathData, pos);
            if (relative) {
                currentPoint.setY(currentPoint.y() + y);
            } else {
                currentPoint.setY(y);
            }
            currentPath.append(currentPoint);

        } else if (cmdUpper == 'C') {
            double x1 = parseNumber(svgPathData, pos);
            double y1 = parseNumber(svgPathData, pos);
            double x2 = parseNumber(svgPathData, pos);
            double y2 = parseNumber(svgPathData, pos);
            double x = parseNumber(svgPathData, pos);
            double y = parseNumber(svgPathData, pos);

            QPointF p1(x1, y1), p2(x2, y2), p3(x, y);
            if (relative) {
                p1 += currentPoint;
                p2 += currentPoint;
                p3 += currentPoint;
            }

            QVector<QPointF> bezierPoints = approximateCubicBezier(
                currentPoint, p1, p2, p3, options.tolerance);
            for (int i = 1; i < bezierPoints.size(); ++i) {
                currentPath.append(bezierPoints[i]);
            }

            lastControl = p2;
            currentPoint = p3;

        } else if (cmdUpper == 'S') {
            double x2 = parseNumber(svgPathData, pos);
            double y2 = parseNumber(svgPathData, pos);
            double x = parseNumber(svgPathData, pos);
            double y = parseNumber(svgPathData, pos);

            // First control point is reflection of last control
            QPointF p1 = currentPoint * 2 - lastControl;
            QPointF p2(x2, y2), p3(x, y);
            if (relative) {
                p2 += currentPoint;
                p3 += currentPoint;
            }

            QVector<QPointF> bezierPoints = approximateCubicBezier(
                currentPoint, p1, p2, p3, options.tolerance);
            for (int i = 1; i < bezierPoints.size(); ++i) {
                currentPath.append(bezierPoints[i]);
            }

            lastControl = p2;
            currentPoint = p3;

        } else if (cmdUpper == 'Q') {
            double x1 = parseNumber(svgPathData, pos);
            double y1 = parseNumber(svgPathData, pos);
            double x = parseNumber(svgPathData, pos);
            double y = parseNumber(svgPathData, pos);

            QPointF p1(x1, y1), p2(x, y);
            if (relative) {
                p1 += currentPoint;
                p2 += currentPoint;
            }

            QVector<QPointF> bezierPoints = approximateQuadBezier(
                currentPoint, p1, p2, options.tolerance);
            for (int i = 1; i < bezierPoints.size(); ++i) {
                currentPath.append(bezierPoints[i]);
            }

            lastControl = p1;
            currentPoint = p2;

        } else if (cmdUpper == 'T') {
            double x = parseNumber(svgPathData, pos);
            double y = parseNumber(svgPathData, pos);

            QPointF p1 = currentPoint * 2 - lastControl;
            QPointF p2(x, y);
            if (relative) {
                p2 += currentPoint;
            }

            QVector<QPointF> bezierPoints = approximateQuadBezier(
                currentPoint, p1, p2, options.tolerance);
            for (int i = 1; i < bezierPoints.size(); ++i) {
                currentPath.append(bezierPoints[i]);
            }

            lastControl = p1;
            currentPoint = p2;

        } else if (cmdUpper == 'A') {
            double rx = parseNumber(svgPathData, pos);
            double ry = parseNumber(svgPathData, pos);
            double xAxisRotation = parseNumber(svgPathData, pos);
            int largeArc = parseFlag(svgPathData, pos);
            int sweep = parseFlag(svgPathData, pos);
            double x = parseNumber(svgPathData, pos);
            double y = parseNumber(svgPathData, pos);

            QPointF endPoint(x, y);
            if (relative) {
                endPoint += currentPoint;
            }

            if (!options.convertArcsToLines && qAbs(rx - ry) < 0.001) {
                // Circular arc - create Arc entity
                flushPath();

                double cx, cy, startAngle, sweepAngle;
                svgArcToCenterParams(
                    currentPoint.x(), currentPoint.y(),
                    rx, ry, xAxisRotation, largeArc, sweep,
                    endPoint.x(), endPoint.y(),
                    cx, cy, startAngle, sweepAngle);

                if (qAbs(sweepAngle) > 0.01) {
                    // Flip angles if Y is flipped
                    if (options.flipY) {
                        startAngle = -startAngle;
                        sweepAngle = -sweepAngle;
                    }

                    Entity arc = createArc(nextId++,
                                           transformPoint(QPointF(cx, cy)),
                                           rx * scale,
                                           startAngle,
                                           sweepAngle);
                    result.entities.append(arc);
                }

                currentPoint = endPoint;
                currentPath.append(currentPoint);
            } else {
                // Approximate arc with line segments
                double cx, cy, startAngle, sweepAngle;
                svgArcToCenterParams(
                    currentPoint.x(), currentPoint.y(),
                    rx, ry, xAxisRotation, largeArc, sweep,
                    endPoint.x(), endPoint.y(),
                    cx, cy, startAngle, sweepAngle);

                int segments = qMax(8, static_cast<int>(
                    qAbs(sweepAngle) / 360.0 * 32));

                for (int i = 1; i <= segments; ++i) {
                    double t = static_cast<double>(i) / segments;
                    double angle = qDegreesToRadians(startAngle + t * sweepAngle);
                    double px = cx + rx * qCos(angle);
                    double py = cy + ry * qSin(angle);
                    currentPath.append(QPointF(px, py));
                }

                currentPoint = endPoint;
            }

        } else if (cmdUpper == 'Z') {
            // Close path
            if (!currentPath.isEmpty() && currentPoint != startPoint) {
                currentPath.append(startPoint);
            }
            flushPath();
            currentPoint = startPoint;
        }
    }

    flushPath();

    result.success = true;
    result.entityCount = result.entities.size();

    // Calculate bounds
    for (const Entity& e : result.entities) {
        result.bounds.include(e.boundingBox());
    }

    return result;
}

SVGImportResult importSVGFile(
    const QString& filePath,
    int startId,
    const SVGImportOptions& options)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        SVGImportResult result;
        result.success = false;
        result.errorMessage = QString("Cannot open file: %1").arg(filePath);
        return result;
    }

    QString content = QString::fromUtf8(file.readAll());
    return importSVGString(content, startId, options);
}

SVGImportResult importSVGString(
    const QString& svgContent,
    int startId,
    const SVGImportOptions& options)
{
    SVGImportResult result;
    result.success = false;

    // Simple regex-based extraction of path data
    // For full SVG support, would need a proper XML parser
    QRegularExpression pathRegex(R"(<path[^>]*\sd=[\"']([^\"']+)[\"'])");
    QRegularExpression circleRegex(R"(<circle[^>]*\scx=[\"']([^\"']+)[\"'][^>]*\scy=[\"']([^\"']+)[\"'][^>]*\sr=[\"']([^\"']+)[\"'])");
    QRegularExpression rectRegex(R"(<rect[^>]*\sx=[\"']([^\"']+)[\"'][^>]*\sy=[\"']([^\"']+)[\"'][^>]*\swidth=[\"']([^\"']+)[\"'][^>]*\sheight=[\"']([^\"']+)[\"'])");
    QRegularExpression lineRegex(R"(<line[^>]*\sx1=[\"']([^\"']+)[\"'][^>]*\sy1=[\"']([^\"']+)[\"'][^>]*\sx2=[\"']([^\"']+)[\"'][^>]*\sy2=[\"']([^\"']+)[\"'])");

    int nextId = startId;
    double scale = options.scale;
    double ySign = options.flipY ? -1.0 : 1.0;

    auto transformPoint = [&](const QPointF& p) -> QPointF {
        return QPointF(p.x() * scale + options.offset.x(),
                       p.y() * ySign * scale + options.offset.y());
    };

    // Extract paths
    auto pathMatches = pathRegex.globalMatch(svgContent);
    while (pathMatches.hasNext()) {
        auto match = pathMatches.next();
        QString pathData = match.captured(1);

        SVGImportResult pathResult = importSVGPath(pathData, nextId, options);
        if (pathResult.success) {
            result.entities.append(pathResult.entities);
            nextId += pathResult.entityCount;
        }
    }

    // Extract circles
    auto circleMatches = circleRegex.globalMatch(svgContent);
    while (circleMatches.hasNext()) {
        auto match = circleMatches.next();
        double cx = match.captured(1).toDouble();
        double cy = match.captured(2).toDouble();
        double r = match.captured(3).toDouble();

        Entity circle = createCircle(nextId++,
                                     transformPoint(QPointF(cx, cy)),
                                     r * scale);
        result.entities.append(circle);
    }

    // Extract rectangles
    auto rectMatches = rectRegex.globalMatch(svgContent);
    while (rectMatches.hasNext()) {
        auto match = rectMatches.next();
        double x = match.captured(1).toDouble();
        double y = match.captured(2).toDouble();
        double w = match.captured(3).toDouble();
        double h = match.captured(4).toDouble();

        Entity rect = createRectangle(nextId++,
                                      transformPoint(QPointF(x, y)),
                                      transformPoint(QPointF(x + w, y + h)));
        result.entities.append(rect);
    }

    // Extract lines
    auto lineMatches = lineRegex.globalMatch(svgContent);
    while (lineMatches.hasNext()) {
        auto match = lineMatches.next();
        double x1 = match.captured(1).toDouble();
        double y1 = match.captured(2).toDouble();
        double x2 = match.captured(3).toDouble();
        double y2 = match.captured(4).toDouble();

        Entity line = createLine(nextId++,
                                 transformPoint(QPointF(x1, y1)),
                                 transformPoint(QPointF(x2, y2)));
        result.entities.append(line);
    }

    result.success = true;
    result.entityCount = result.entities.size();

    // Calculate bounds
    for (const Entity& e : result.entities) {
        result.bounds.include(e.boundingBox());
    }

    if (result.entityCount == 0) {
        result.errorMessage = QStringLiteral("No supported elements found in SVG");
    }

    return result;
}

// =====================================================================
//  DXF Import
// =====================================================================

namespace {

/// DXF group code and value pair
struct DXFPair {
    int code;
    QString value;
};

/// Read next group code/value pair from DXF content
bool readDXFPair(const QStringList& lines, int& lineIndex, DXFPair& pair)
{
    if (lineIndex + 1 >= lines.size()) return false;

    bool ok;
    pair.code = lines[lineIndex].trimmed().toInt(&ok);
    if (!ok) return false;

    pair.value = lines[lineIndex + 1].trimmed();
    lineIndex += 2;
    return true;
}

/// Skip to next entity or section in DXF
void skipToNext(const QStringList& lines, int& lineIndex)
{
    DXFPair pair;
    while (lineIndex < lines.size()) {
        int savedIndex = lineIndex;
        if (readDXFPair(lines, lineIndex, pair)) {
            if (pair.code == 0) {
                // Found next entity or section marker
                lineIndex = savedIndex;  // Restore so caller sees the 0 code
                return;
            }
        } else {
            ++lineIndex;
        }
    }
}

/// Parse a LINE entity
Entity parseDXFLine(const QStringList& lines, int& lineIndex, int id,
                    double scale, const QPointF& offset)
{
    double x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    DXFPair pair;

    while (lineIndex < lines.size()) {
        int savedIndex = lineIndex;
        if (!readDXFPair(lines, lineIndex, pair)) break;

        if (pair.code == 0) {
            lineIndex = savedIndex;
            break;
        }

        switch (pair.code) {
        case 10: x1 = pair.value.toDouble() * scale + offset.x(); break;
        case 20: y1 = pair.value.toDouble() * scale + offset.y(); break;
        case 11: x2 = pair.value.toDouble() * scale + offset.x(); break;
        case 21: y2 = pair.value.toDouble() * scale + offset.y(); break;
        }
    }

    return createLine(id, QPointF(x1, y1), QPointF(x2, y2));
}

/// Parse a CIRCLE entity
Entity parseDXFCircle(const QStringList& lines, int& lineIndex, int id,
                      double scale, const QPointF& offset)
{
    double cx = 0, cy = 0, r = 0;
    DXFPair pair;

    while (lineIndex < lines.size()) {
        int savedIndex = lineIndex;
        if (!readDXFPair(lines, lineIndex, pair)) break;

        if (pair.code == 0) {
            lineIndex = savedIndex;
            break;
        }

        switch (pair.code) {
        case 10: cx = pair.value.toDouble() * scale + offset.x(); break;
        case 20: cy = pair.value.toDouble() * scale + offset.y(); break;
        case 40: r = pair.value.toDouble() * scale; break;
        }
    }

    return createCircle(id, QPointF(cx, cy), r);
}

/// Parse an ARC entity
Entity parseDXFArc(const QStringList& lines, int& lineIndex, int id,
                   double scale, const QPointF& offset)
{
    double cx = 0, cy = 0, r = 0;
    double startAngle = 0, endAngle = 360;
    DXFPair pair;

    while (lineIndex < lines.size()) {
        int savedIndex = lineIndex;
        if (!readDXFPair(lines, lineIndex, pair)) break;

        if (pair.code == 0) {
            lineIndex = savedIndex;
            break;
        }

        switch (pair.code) {
        case 10: cx = pair.value.toDouble() * scale + offset.x(); break;
        case 20: cy = pair.value.toDouble() * scale + offset.y(); break;
        case 40: r = pair.value.toDouble() * scale; break;
        case 50: startAngle = pair.value.toDouble(); break;
        case 51: endAngle = pair.value.toDouble(); break;
        }
    }

    // DXF arcs are always CCW, angles in degrees
    double sweep = endAngle - startAngle;
    if (sweep <= 0) sweep += 360;

    return createArc(id, QPointF(cx, cy), r, startAngle, sweep);
}

/// Parse an ELLIPSE entity
Entity parseDXFEllipse(const QStringList& lines, int& lineIndex, int id,
                       double scale, const QPointF& offset)
{
    double cx = 0, cy = 0;
    double majorX = 1, majorY = 0;  // Major axis endpoint relative to center
    double ratio = 1.0;              // Minor/major ratio
    DXFPair pair;

    while (lineIndex < lines.size()) {
        int savedIndex = lineIndex;
        if (!readDXFPair(lines, lineIndex, pair)) break;

        if (pair.code == 0) {
            lineIndex = savedIndex;
            break;
        }

        switch (pair.code) {
        case 10: cx = pair.value.toDouble() * scale + offset.x(); break;
        case 20: cy = pair.value.toDouble() * scale + offset.y(); break;
        case 11: majorX = pair.value.toDouble() * scale; break;
        case 21: majorY = pair.value.toDouble() * scale; break;
        case 40: ratio = pair.value.toDouble(); break;
        }
    }

    double majorRadius = qSqrt(majorX * majorX + majorY * majorY);
    double minorRadius = majorRadius * ratio;

    return createEllipse(id, QPointF(cx, cy), majorRadius, minorRadius);
}

/// Parse a POINT entity
Entity parseDXFPoint(const QStringList& lines, int& lineIndex, int id,
                     double scale, const QPointF& offset)
{
    double x = 0, y = 0;
    DXFPair pair;

    while (lineIndex < lines.size()) {
        int savedIndex = lineIndex;
        if (!readDXFPair(lines, lineIndex, pair)) break;

        if (pair.code == 0) {
            lineIndex = savedIndex;
            break;
        }

        switch (pair.code) {
        case 10: x = pair.value.toDouble() * scale + offset.x(); break;
        case 20: y = pair.value.toDouble() * scale + offset.y(); break;
        }
    }

    return createPoint(id, QPointF(x, y));
}

/// Parse a LWPOLYLINE entity
QVector<Entity> parseDXFLWPolyline(const QStringList& lines, int& lineIndex, int& nextId,
                                    double scale, const QPointF& offset)
{
    QVector<Entity> entities;
    QVector<QPointF> vertices;
    QVector<double> bulges;
    bool closed = false;
    DXFPair pair;

    double currentX = 0, currentY = 0, currentBulge = 0;
    bool hasVertex = false;

    while (lineIndex < lines.size()) {
        int savedIndex = lineIndex;
        if (!readDXFPair(lines, lineIndex, pair)) break;

        if (pair.code == 0) {
            lineIndex = savedIndex;
            break;
        }

        switch (pair.code) {
        case 70:  // Flags
            closed = (pair.value.toInt() & 1) != 0;
            break;
        case 10:  // X coordinate
            if (hasVertex) {
                vertices.append(QPointF(currentX, currentY));
                bulges.append(currentBulge);
                currentBulge = 0;
            }
            currentX = pair.value.toDouble() * scale + offset.x();
            hasVertex = true;
            break;
        case 20:  // Y coordinate
            currentY = pair.value.toDouble() * scale + offset.y();
            break;
        case 42:  // Bulge
            currentBulge = pair.value.toDouble();
            break;
        }
    }

    // Add last vertex
    if (hasVertex) {
        vertices.append(QPointF(currentX, currentY));
        bulges.append(currentBulge);
    }

    if (vertices.size() < 2) return entities;

    // Convert to lines and arcs
    int numSegments = closed ? vertices.size() : vertices.size() - 1;
    for (int i = 0; i < numSegments; ++i) {
        int nextIdx = (i + 1) % vertices.size();
        double bulge = bulges[i];

        if (qAbs(bulge) < 1e-10) {
            // Straight line
            entities.append(createLine(nextId++, vertices[i], vertices[nextIdx]));
        } else {
            // Arc (bulge = tan(angle/4))
            QPointF p1 = vertices[i];
            QPointF p2 = vertices[nextIdx];
            QPointF mid = (p1 + p2) / 2;
            QPointF chord = p2 - p1;
            double chordLen = qSqrt(chord.x() * chord.x() + chord.y() * chord.y());

            // Perpendicular direction
            QPointF perp(-chord.y() / chordLen, chord.x() / chordLen);

            // Sagitta (distance from chord midpoint to arc)
            double sagitta = bulge * chordLen / 2;

            // Center of arc
            double radius = (chordLen * chordLen / 4 + sagitta * sagitta) / (2 * qAbs(sagitta));
            double centerDist = radius - qAbs(sagitta);
            if (bulge < 0) centerDist = -centerDist;

            QPointF center = mid + perp * centerDist;

            // Calculate angles
            double startAngle = qRadiansToDegrees(qAtan2(p1.y() - center.y(), p1.x() - center.x()));
            double endAngle = qRadiansToDegrees(qAtan2(p2.y() - center.y(), p2.x() - center.x()));

            double sweep = endAngle - startAngle;
            if (bulge > 0) {
                if (sweep < 0) sweep += 360;
            } else {
                if (sweep > 0) sweep -= 360;
            }

            entities.append(createArc(nextId++, center, radius, startAngle, sweep));
        }
    }

    return entities;
}

/// Parse a SPLINE entity (approximate as polyline)
QVector<Entity> parseDXFSpline(const QStringList& lines, int& lineIndex, int& nextId,
                                double scale, const QPointF& offset, double tolerance)
{
    QVector<Entity> entities;
    QVector<QPointF> controlPoints;
    QVector<QPointF> fitPoints;
    int degree = 3;
    DXFPair pair;

    while (lineIndex < lines.size()) {
        int savedIndex = lineIndex;
        if (!readDXFPair(lines, lineIndex, pair)) break;

        if (pair.code == 0) {
            lineIndex = savedIndex;
            break;
        }

        static double tempX = 0, tempY = 0;
        static bool isFitPoint = false;

        switch (pair.code) {
        case 71: degree = pair.value.toInt(); break;
        case 10:  // Control point X
            tempX = pair.value.toDouble() * scale + offset.x();
            isFitPoint = false;
            break;
        case 20:  // Control point Y
            tempY = pair.value.toDouble() * scale + offset.y();
            if (!isFitPoint) {
                controlPoints.append(QPointF(tempX, tempY));
            }
            break;
        case 11:  // Fit point X
            tempX = pair.value.toDouble() * scale + offset.x();
            isFitPoint = true;
            break;
        case 21:  // Fit point Y
            tempY = pair.value.toDouble() * scale + offset.y();
            fitPoints.append(QPointF(tempX, tempY));
            break;
        }
    }

    // Use fit points if available, otherwise control points
    const QVector<QPointF>& points = fitPoints.isEmpty() ? controlPoints : fitPoints;

    if (points.size() >= 2) {
        // Create as a spline entity if we have control points
        if (!controlPoints.isEmpty()) {
            Entity spline;
            spline.id = nextId++;
            spline.type = EntityType::Spline;
            spline.points = controlPoints;
            entities.append(spline);
        } else {
            // Approximate as line segments
            for (int i = 0; i < points.size() - 1; ++i) {
                entities.append(createLine(nextId++, points[i], points[i + 1]));
            }
        }
    }

    return entities;
}

/// Parse TEXT or MTEXT entity (as text annotation)
Entity parseDXFText(const QStringList& lines, int& lineIndex, int id,
                    double scale, const QPointF& offset)
{
    double x = 0, y = 0;
    double textHeight = 12.0;   // Default height in mm (DXF group 40)
    double rotation = 0.0;      // Rotation angle in degrees (DXF group 50)
    QString textContent;
    QString styleName;          // DXF text style name (group 7)
    DXFPair pair;

    while (lineIndex < lines.size()) {
        int savedIndex = lineIndex;
        if (!readDXFPair(lines, lineIndex, pair)) break;

        if (pair.code == 0) {
            lineIndex = savedIndex;
            break;
        }

        switch (pair.code) {
        case 10: x = pair.value.toDouble() * scale + offset.x(); break;
        case 20: y = pair.value.toDouble() * scale + offset.y(); break;
        case 40: textHeight = pair.value.toDouble() * scale; break;  // Text height
        case 50: rotation = pair.value.toDouble(); break;            // Rotation angle
        case 7: styleName = pair.value; break;                       // Text style name
        case 1: textContent = pair.value; break;                     // TEXT content
        case 3: textContent += pair.value; break;                    // MTEXT continuation
        }
    }

    // Create text with parsed properties
    // Note: DXF text styles would need TABLES section parsing for full font info
    // For now, leave fontFamily empty (use default) and just import size/rotation
    return createText(id, QPointF(x, y), textContent, QString(), textHeight,
                      false, false, rotation);
}

}  // anonymous namespace

DXFImportResult importDXFString(
    const QString& dxfContent,
    int startId,
    const DXFImportOptions& options)
{
    DXFImportResult result;
    result.success = false;

    if (dxfContent.isEmpty()) {
        result.errorMessage = QStringLiteral("Empty DXF content");
        return result;
    }

    QStringList lines = dxfContent.split('\n');
    int lineIndex = 0;
    int nextId = startId;

    QString currentLayer;
    bool inEntitiesSection = false;

    DXFPair pair;
    while (lineIndex < lines.size()) {
        if (!readDXFPair(lines, lineIndex, pair)) {
            ++lineIndex;
            continue;
        }

        // Track sections
        if (pair.code == 0 && pair.value == "SECTION") {
            if (readDXFPair(lines, lineIndex, pair) && pair.code == 2) {
                inEntitiesSection = (pair.value == "ENTITIES");
            }
            continue;
        }

        if (pair.code == 0 && pair.value == "ENDSEC") {
            inEntitiesSection = false;
            continue;
        }

        if (pair.code == 0 && pair.value == "EOF") {
            break;
        }

        if (!inEntitiesSection) continue;

        // Parse entities
        if (pair.code == 0) {
            QString entityType = pair.value.toUpper();

            // Read layer for filtering
            int peekIndex = lineIndex;
            DXFPair peekPair;
            currentLayer = "0";
            while (peekIndex < lines.size() - 10) {
                if (readDXFPair(lines, peekIndex, peekPair)) {
                    if (peekPair.code == 0) break;
                    if (peekPair.code == 8) {
                        currentLayer = peekPair.value;
                        break;
                    }
                }
            }

            // Check layer filter
            if (!options.layerFilter.isEmpty() &&
                !options.layerFilter.contains(currentLayer, Qt::CaseInsensitive)) {
                skipToNext(lines, lineIndex);
                continue;
            }

            // Check construction layer filter
            if (options.ignoreConstructionLayers) {
                QString layerUpper = currentLayer.toUpper();
                if (layerUpper == "DEFPOINTS" || layerUpper == "CONSTRUCTION" ||
                    layerUpper.startsWith("CONSTR")) {
                    skipToNext(lines, lineIndex);
                    continue;
                }
            }

            // Track layers found
            if (!result.layers.contains(currentLayer)) {
                result.layers.append(currentLayer);
            }

            // Parse entity by type
            if (entityType == "LINE") {
                result.entities.append(parseDXFLine(lines, lineIndex, nextId++,
                                                     options.scale, options.offset));
            }
            else if (entityType == "CIRCLE") {
                result.entities.append(parseDXFCircle(lines, lineIndex, nextId++,
                                                       options.scale, options.offset));
            }
            else if (entityType == "ARC") {
                result.entities.append(parseDXFArc(lines, lineIndex, nextId++,
                                                    options.scale, options.offset));
            }
            else if (entityType == "ELLIPSE") {
                result.entities.append(parseDXFEllipse(lines, lineIndex, nextId++,
                                                        options.scale, options.offset));
            }
            else if (entityType == "POINT") {
                result.entities.append(parseDXFPoint(lines, lineIndex, nextId++,
                                                      options.scale, options.offset));
            }
            else if (entityType == "LWPOLYLINE") {
                result.entities.append(parseDXFLWPolyline(lines, lineIndex, nextId,
                                                           options.scale, options.offset));
            }
            else if (entityType == "POLYLINE") {
                // Old-style polyline - similar to LWPOLYLINE but different structure
                // For now, skip to SEQEND
                while (lineIndex < lines.size()) {
                    if (readDXFPair(lines, lineIndex, pair)) {
                        if (pair.code == 0 && pair.value == "SEQEND") break;
                    }
                }
            }
            else if (entityType == "SPLINE") {
                result.entities.append(parseDXFSpline(lines, lineIndex, nextId,
                                                       options.scale, options.offset,
                                                       options.splineTolerance));
            }
            else if (entityType == "TEXT" || entityType == "MTEXT") {
                result.entities.append(parseDXFText(lines, lineIndex, nextId++,
                                                     options.scale, options.offset));
            }
            else if (entityType == "INSERT" && options.importBlocks) {
                // Block reference - would need to expand from BLOCKS section
                // For now, just record the block name
                while (lineIndex < lines.size()) {
                    if (readDXFPair(lines, lineIndex, pair)) {
                        if (pair.code == 0) {
                            lineIndex -= 2;  // Back up
                            break;
                        }
                        if (pair.code == 2 && !result.blocks.contains(pair.value)) {
                            result.blocks.append(pair.value);
                        }
                    }
                }
            }
            else {
                // Unknown entity type - skip
                skipToNext(lines, lineIndex);
            }
        }
    }

    result.success = true;
    result.entityCount = result.entities.size();

    // Calculate bounds
    for (const Entity& e : result.entities) {
        result.bounds.include(e.boundingBox());
    }

    if (result.entityCount == 0) {
        result.errorMessage = QStringLiteral("No supported entities found in DXF");
    }

    return result;
}

DXFImportResult importDXFFile(
    const QString& filePath,
    int startId,
    const DXFImportOptions& options)
{
    DXFImportResult result;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        result.errorMessage = QStringLiteral("Cannot open file: %1").arg(filePath);
        return result;
    }

    QTextStream in(&file);
    QString content = in.readAll();

    return importDXFString(content, startId, options);
}

}  // namespace sketch
}  // namespace hobbycad
