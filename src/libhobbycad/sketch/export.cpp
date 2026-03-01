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
#include <hobbycad/format.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <regex>
#include <sstream>
#include <string>
#include <vector>
#include <functional>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace hobbycad {
namespace sketch {

// =====================================================================
//  SVG Export
// =====================================================================

namespace {

std::string entityToSVGPath(const Entity& entity, double scale)
{
    std::string path;

    switch (entity.type) {
    case EntityType::Point:
        // Points rendered as small circles
        if (!entity.points.empty()) {
            double x = entity.points[0].x * scale;
            double y = -entity.points[0].y * scale;  // SVG Y is inverted
            path = hobbycad::format("M %g %g m -1 0 a 1 1 0 1 0 2 0 a 1 1 0 1 0 -2 0",
                                    x, y);
        }
        break;

    case EntityType::Line:
        if (entity.points.size() >= 2) {
            double x1 = entity.points[0].x * scale;
            double y1 = -entity.points[0].y * scale;
            double x2 = entity.points[1].x * scale;
            double y2 = -entity.points[1].y * scale;
            path = hobbycad::format("M %g %g L %g %g", x1, y1, x2, y2);
        }
        break;

    case EntityType::Circle:
        if (!entity.points.empty()) {
            double cx = entity.points[0].x * scale;
            double cy = -entity.points[0].y * scale;
            double r = entity.radius * scale;
            // SVG circle as two arcs
            path = hobbycad::format("M %g %g A %g %g 0 1 0 %g %g A %g %g 0 1 0 %g %g",
                                    cx - r, cy, r, r, cx + r, cy, r, r, cx - r, cy);
        }
        break;

    case EntityType::Arc:
        if (!entity.points.empty()) {
            double cx = entity.points[0].x;
            double cy = entity.points[0].y;
            double r = entity.radius;
            double startRad = entity.startAngle * M_PI / 180.0;
            double endRad = (entity.startAngle + entity.sweepAngle) * M_PI / 180.0;

            double x1 = (cx + r * std::cos(startRad)) * scale;
            double y1 = -(cy + r * std::sin(startRad)) * scale;
            double x2 = (cx + r * std::cos(endRad)) * scale;
            double y2 = -(cy + r * std::sin(endRad)) * scale;

            int largeArc = std::abs(entity.sweepAngle) > 180 ? 1 : 0;
            int sweep = entity.sweepAngle > 0 ? 0 : 1;  // Inverted due to Y flip

            path = hobbycad::format("M %g %g A %g %g 0 %d %d %g %g",
                                    x1, y1, r * scale, r * scale,
                                    largeArc, sweep, x2, y2);
        }
        break;

    case EntityType::Rectangle:
        if (entity.points.size() >= 2) {
            double x1 = entity.points[0].x * scale;
            double y1 = -entity.points[0].y * scale;
            double x2 = entity.points[1].x * scale;
            double y2 = -entity.points[1].y * scale;
            path = hobbycad::format("M %g %g L %g %g L %g %g L %g %g Z",
                                    x1, y1, x2, y1, x2, y2, x1, y2);
        }
        break;

    case EntityType::Polygon:
        if (!entity.points.empty()) {
            path = hobbycad::format("M %g %g",
                                    entity.points[0].x * scale,
                                    -entity.points[0].y * scale);
            for (size_t i = 1; i < entity.points.size(); ++i) {
                path += hobbycad::format(" L %g %g",
                                         entity.points[i].x * scale,
                                         -entity.points[i].y * scale);
            }
            path += " Z";
        }
        break;

    case EntityType::Ellipse:
        if (!entity.points.empty()) {
            double cx = entity.points[0].x * scale;
            double cy = -entity.points[0].y * scale;
            double rx = entity.majorRadius * scale;
            double ry = entity.minorRadius * scale;
            // Ellipse as two arcs
            path = hobbycad::format("M %g %g A %g %g 0 1 0 %g %g A %g %g 0 1 0 %g %g",
                                    cx - rx, cy, rx, ry, cx + rx, cy, rx, ry, cx - rx, cy);
        }
        break;

    case EntityType::Spline:
        // Approximate as polyline
        if (!entity.points.empty()) {
            path = hobbycad::format("M %g %g",
                                    entity.points[0].x * scale,
                                    -entity.points[0].y * scale);
            for (size_t i = 1; i < entity.points.size(); ++i) {
                path += hobbycad::format(" L %g %g",
                                         entity.points[i].x * scale,
                                         -entity.points[i].y * scale);
            }
        }
        break;

    case EntityType::Slot:
        // Tessellate slot
        {
            std::vector<Point2D> points = tessellate(entity, 0.5);
            if (!points.empty()) {
                path = hobbycad::format("M %g %g",
                                        points[0].x * scale,
                                        -points[0].y * scale);
                for (size_t i = 1; i < points.size(); ++i) {
                    path += hobbycad::format(" L %g %g",
                                             points[i].x * scale,
                                             -points[i].y * scale);
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

// Simple XML/HTML escape for text content
std::string htmlEscape(const std::string& s)
{
    std::string result;
    result.reserve(s.size());
    for (char c : s) {
        switch (c) {
        case '&':  result += "&amp;"; break;
        case '<':  result += "&lt;"; break;
        case '>':  result += "&gt;"; break;
        case '"':  result += "&quot;"; break;
        case '\'': result += "&#39;"; break;
        default:   result += c; break;
        }
    }
    return result;
}

}  // anonymous namespace

std::string sketchToSVG(
    const std::vector<Entity>& entities,
    const std::vector<Constraint>& constraints,
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

    std::ostringstream out;

    // SVG header
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out << hobbycad::format("<svg xmlns=\"http://www.w3.org/2000/svg\" "
                            "width=\"%gmm\" height=\"%gmm\" "
                            "viewBox=\"0 0 %g %g\">\n",
                            width, height, width, height);

    // Style definitions
    out << "  <defs>\n";
    out << hobbycad::format("    <style>\n"
                            "      .entity { stroke: %s; stroke-width: %g; fill: %s; }\n"
                            "      .construction { stroke: %s; stroke-dasharray: 4 2; }\n"
                            "    </style>\n",
                            options.strokeColor.c_str(),
                            options.strokeWidth,
                            options.fillColor.c_str(),
                            options.constructionColor.c_str());
    out << "  </defs>\n";

    // Transform group to handle coordinate system
    out << hobbycad::format("  <g transform=\"translate(%g %g)\">\n", offsetX, offsetY);

    // Entities
    for (const Entity& entity : entities) {
        std::string className = entity.isConstruction ? "entity construction" : "entity";

        // Handle text entities separately
        if (entity.type == EntityType::Text && !entity.points.empty()) {
            double x = entity.points[0].x * scale;
            double y = -entity.points[0].y * scale;  // Y inverted
            double fontSize = entity.fontSize * scale;

            std::string fontStyle;
            if (!entity.fontFamily.empty()) {
                fontStyle += hobbycad::format(" font-family=\"%s\"", entity.fontFamily.c_str());
            }
            if (entity.fontBold) {
                fontStyle += " font-weight=\"bold\"";
            }
            if (entity.fontItalic) {
                fontStyle += " font-style=\"italic\"";
            }
            std::string transform;
            if (std::abs(entity.textRotation) > 0.01) {
                transform = hobbycad::format(" transform=\"rotate(%g %g %g)\"",
                                             -entity.textRotation, x, y);
            }

            out << hobbycad::format("    <text class=\"%s\" x=\"%g\" y=\"%g\" font-size=\"%g\"%s%s>%s</text>\n",
                                    className.c_str(), x, y, fontSize,
                                    fontStyle.c_str(), transform.c_str(),
                                    htmlEscape(entity.text).c_str());
            continue;
        }

        std::string pathData = entityToSVGPath(entity, scale);
        if (pathData.empty()) continue;

        out << hobbycad::format("    <path class=\"%s\" d=\"%s\"/>\n",
                                className.c_str(), pathData.c_str());
    }

    // Dimension text (if enabled)
    if (options.includeDimensions) {
        for (const Constraint& c : constraints) {
            if (!c.labelVisible) continue;

            std::string label;
            switch (c.type) {
            case ConstraintType::Distance:
            case ConstraintType::Radius:
            case ConstraintType::Diameter:
                label = hobbycad::format("%.2f", c.value);
                break;
            case ConstraintType::Angle:
                label = hobbycad::format("%.1f\u00B0", c.value);
                break;
            default:
                continue;
            }

            double x = c.labelPosition.x * scale;
            double y = -c.labelPosition.y * scale;
            out << hobbycad::format("    <text x=\"%g\" y=\"%g\" font-size=\"3\" "
                                    "text-anchor=\"middle\">%s</text>\n",
                                    x, y, label.c_str());
        }
    }

    out << "  </g>\n";
    out << "</svg>\n";

    return out.str();
}

bool exportSketchToSVG(
    const std::vector<Entity>& entities,
    const std::vector<Constraint>& constraints,
    const std::string& filePath,
    const SVGExportOptions& options)
{
    std::ofstream file(filePath);
    if (!file) {
        return false;
    }

    file << sketchToSVG(entities, constraints, options);
    return true;
}

// =====================================================================
//  DXF Export
// =====================================================================

namespace {

void writeDXFHeader(std::ostream& out)
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

void writeDXFEntity(std::ostream& out, const Entity& entity, const DXFExportOptions& options)
{
    const std::string& layer = entity.isConstruction ? options.constructionLayer : options.layerName;
    int color = entity.isConstruction ? options.constructionColorIndex : options.colorIndex;

    switch (entity.type) {
    case EntityType::Point:
        if (!entity.points.empty()) {
            out << "0\nPOINT\n";
            out << "8\n" << layer << "\n";
            out << "62\n" << color << "\n";
            out << "10\n" << entity.points[0].x << "\n";
            out << "20\n" << entity.points[0].y << "\n";
            out << "30\n0\n";
        }
        break;

    case EntityType::Line:
        if (entity.points.size() >= 2) {
            out << "0\nLINE\n";
            out << "8\n" << layer << "\n";
            out << "62\n" << color << "\n";
            out << "10\n" << entity.points[0].x << "\n";
            out << "20\n" << entity.points[0].y << "\n";
            out << "30\n0\n";
            out << "11\n" << entity.points[1].x << "\n";
            out << "21\n" << entity.points[1].y << "\n";
            out << "31\n0\n";
        }
        break;

    case EntityType::Circle:
        if (!entity.points.empty()) {
            out << "0\nCIRCLE\n";
            out << "8\n" << layer << "\n";
            out << "62\n" << color << "\n";
            out << "10\n" << entity.points[0].x << "\n";
            out << "20\n" << entity.points[0].y << "\n";
            out << "30\n0\n";
            out << "40\n" << entity.radius << "\n";
        }
        break;

    case EntityType::Arc:
        if (!entity.points.empty()) {
            out << "0\nARC\n";
            out << "8\n" << layer << "\n";
            out << "62\n" << color << "\n";
            out << "10\n" << entity.points[0].x << "\n";
            out << "20\n" << entity.points[0].y << "\n";
            out << "30\n0\n";
            out << "40\n" << entity.radius << "\n";
            out << "50\n" << entity.startAngle << "\n";
            out << "51\n" << (entity.startAngle + entity.sweepAngle) << "\n";
        }
        break;

    case EntityType::Ellipse:
        if (!entity.points.empty()) {
            out << "0\nELLIPSE\n";
            out << "8\n" << layer << "\n";
            out << "62\n" << color << "\n";
            out << "10\n" << entity.points[0].x << "\n";
            out << "20\n" << entity.points[0].y << "\n";
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
            std::vector<Point2D> points = tessellate(entity, 0.5);
            if (!points.empty()) {
                out << "0\nLWPOLYLINE\n";
                out << "8\n" << layer << "\n";
                out << "62\n" << color << "\n";
                out << "90\n" << points.size() << "\n";
                out << "70\n1\n";  // Closed polyline
                for (const Point2D& p : points) {
                    out << "10\n" << p.x << "\n";
                    out << "20\n" << p.y << "\n";
                }
            }
        }
        break;

    case EntityType::Text:
        if (!entity.points.empty()) {
            out << "0\nTEXT\n";
            out << "8\n" << layer << "\n";
            out << "62\n" << color << "\n";
            out << "10\n" << entity.points[0].x << "\n";
            out << "20\n" << entity.points[0].y << "\n";
            out << "30\n0\n";
            out << "40\n" << entity.fontSize << "\n";  // Text height
            if (std::abs(entity.textRotation) > 0.01) {
                out << "50\n" << entity.textRotation << "\n";  // Rotation angle
            }
            out << "1\n" << entity.text << "\n";
        }
        break;
    }
}

}  // anonymous namespace

std::string sketchToDXF(
    const std::vector<Entity>& entities,
    const DXFExportOptions& options)
{
    std::ostringstream out;

    writeDXFHeader(out);

    // Entities section
    out << "0\nSECTION\n2\nENTITIES\n";

    for (const Entity& entity : entities) {
        writeDXFEntity(out, entity, options);
    }

    out << "0\nENDSEC\n";
    out << "0\nEOF\n";

    return out.str();
}

bool exportSketchToDXF(
    const std::vector<Entity>& entities,
    const std::string& filePath,
    const DXFExportOptions& options)
{
    std::ofstream file(filePath);
    if (!file) {
        return false;
    }

    file << sketchToDXF(entities, options);
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
double parseNumber(const std::string& data, int& pos)
{
    int len = static_cast<int>(data.length());

    // Skip whitespace and commas
    while (pos < len &&
           (data[pos] == ' ' || data[pos] == '\t' || data[pos] == '\n' ||
            data[pos] == '\r' || data[pos] == ',')) {
        ++pos;
    }

    if (pos >= len) return 0.0;

    int start = pos;

    // Handle sign
    if (data[pos] == '-' || data[pos] == '+') {
        ++pos;
    }

    // Integer part
    while (pos < len && data[pos] >= '0' && data[pos] <= '9') {
        ++pos;
    }

    // Decimal part
    if (pos < len && data[pos] == '.') {
        ++pos;
        while (pos < len && data[pos] >= '0' && data[pos] <= '9') {
            ++pos;
        }
    }

    // Exponent
    if (pos < len && (data[pos] == 'e' || data[pos] == 'E')) {
        ++pos;
        if (pos < len && (data[pos] == '-' || data[pos] == '+')) {
            ++pos;
        }
        while (pos < len && data[pos] >= '0' && data[pos] <= '9') {
            ++pos;
        }
    }

    return std::stod(data.substr(start, pos - start));
}

/// Parse a flag (0 or 1) for arc commands
int parseFlag(const std::string& data, int& pos)
{
    int len = static_cast<int>(data.length());

    while (pos < len &&
           (data[pos] == ' ' || data[pos] == '\t' || data[pos] == '\n' ||
            data[pos] == '\r' || data[pos] == ',')) {
        ++pos;
    }
    if (pos < len && (data[pos] == '0' || data[pos] == '1')) {
        return data[pos++] - '0';
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
    if (std::abs(x1 - x2) < 1e-10 && std::abs(y1 - y2) < 1e-10) {
        cx = x1; cy = y1;
        startAngle = 0; sweepAngle = 0;
        return;
    }

    // Ensure radii are positive
    rx = std::abs(rx);
    ry = std::abs(ry);

    if (rx < 1e-10 || ry < 1e-10) {
        // Treat as line
        cx = (x1 + x2) / 2;
        cy = (y1 + y2) / 2;
        startAngle = 0;
        sweepAngle = 0;
        return;
    }

    double phiRad = phi * M_PI / 180.0;
    double cosPhi = std::cos(phiRad);
    double sinPhi = std::sin(phiRad);

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
        double sqrtLambda = std::sqrt(lambda);
        rx *= sqrtLambda;
        ry *= sqrtLambda;
        rxSq = rx * rx;
        rySq = ry * ry;
    }

    double num = rxSq * rySq - rxSq * y1pSq - rySq * x1pSq;
    double denom = rxSq * y1pSq + rySq * x1pSq;

    double sq = std::max(0.0, num / denom);
    double coef = std::sqrt(sq) * ((largeArc == sweep) ? -1 : 1);

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
        double len = std::sqrt(ux*ux + uy*uy) * std::sqrt(vx*vx + vy*vy);
        double ang = std::acos(std::clamp(dot / len, -1.0, 1.0));
        if (ux * vy - uy * vx < 0) ang = -ang;
        return ang;
    };

    double ux = (x1p - cxp) / rx;
    double uy = (y1p - cyp) / ry;
    double vx = (-x1p - cxp) / rx;
    double vy = (-y1p - cyp) / ry;

    startAngle = angle(1, 0, ux, uy) * 180.0 / M_PI;
    sweepAngle = angle(ux, uy, vx, vy) * 180.0 / M_PI;

    if (!sweep && sweepAngle > 0) {
        sweepAngle -= 360;
    } else if (sweep && sweepAngle < 0) {
        sweepAngle += 360;
    }
}

/// Approximate cubic bezier with line segments
std::vector<Point2D> approximateCubicBezier(
    const Point2D& p0, const Point2D& p1,
    const Point2D& p2, const Point2D& p3,
    double tolerance)
{
    std::vector<Point2D> result;

    // Simple recursive subdivision
    std::function<void(Point2D, Point2D, Point2D, Point2D, int)> subdivide;
    subdivide = [&](Point2D a, Point2D b, Point2D c, Point2D d, int depth) {
        if (depth > 10) {
            result.push_back(d);
            return;
        }

        // Check flatness
        double dx = d.x - a.x;
        double dy = d.y - a.y;
        double d2 = std::abs((b.x - d.x) * dy - (b.y - d.y) * dx);
        double d3 = std::abs((c.x - d.x) * dy - (c.y - d.y) * dx);

        if ((d2 + d3) * (d2 + d3) < tolerance * (dx*dx + dy*dy)) {
            result.push_back(d);
            return;
        }

        // Subdivide
        Point2D ab = (a + b) / 2;
        Point2D bc = (b + c) / 2;
        Point2D cd = (c + d) / 2;
        Point2D abc = (ab + bc) / 2;
        Point2D bcd = (bc + cd) / 2;
        Point2D abcd = (abc + bcd) / 2;

        subdivide(a, ab, abc, abcd, depth + 1);
        subdivide(abcd, bcd, cd, d, depth + 1);
    };

    result.push_back(p0);
    subdivide(p0, p1, p2, p3, 0);
    return result;
}

/// Approximate quadratic bezier with line segments
std::vector<Point2D> approximateQuadBezier(
    const Point2D& p0, const Point2D& p1, const Point2D& p2,
    double tolerance)
{
    // Convert to cubic bezier
    Point2D c1 = p0 + 2.0/3.0 * (p1 - p0);
    Point2D c2 = p2 + 2.0/3.0 * (p1 - p2);
    return approximateCubicBezier(p0, c1, c2, p2, tolerance);
}

}  // anonymous namespace

SVGImportResult importSVGPath(
    const std::string& svgPathData,
    int startId,
    const SVGImportOptions& options)
{
    SVGImportResult result;
    result.success = false;

    if (svgPathData.empty()) {
        result.errorMessage = "Empty path data";
        return result;
    }

    std::vector<Point2D> currentPath;
    Point2D currentPoint(0, 0);
    Point2D startPoint(0, 0);
    Point2D lastControl(0, 0);
    int nextId = startId;
    double scale = options.scale;
    double ySign = options.flipY ? -1.0 : 1.0;

    auto transformPoint = [&](const Point2D& p) -> Point2D {
        return Point2D(p.x * scale + options.offset.x,
                       p.y * ySign * scale + options.offset.y);
    };

    auto flushPath = [&]() {
        if (currentPath.size() >= 2) {
            // Create line segments
            for (size_t i = 0; i < currentPath.size() - 1; ++i) {
                Entity line = createLine(nextId++,
                                         transformPoint(currentPath[i]),
                                         transformPoint(currentPath[i+1]));
                result.entities.push_back(line);
            }
        }
        currentPath.clear();
    };

    int pos = 0;
    int len = static_cast<int>(svgPathData.length());
    char lastCommand = 'M';
    bool relative = false;

    while (pos < len) {
        // Skip whitespace
        while (pos < len && (svgPathData[pos] == ' ' || svgPathData[pos] == '\t' ||
               svgPathData[pos] == '\n' || svgPathData[pos] == '\r')) {
            ++pos;
        }
        if (pos >= len) break;

        char cmd = svgPathData[pos];

        // Check if it's a command letter
        if ((cmd >= 'A' && cmd <= 'Z') || (cmd >= 'a' && cmd <= 'z')) {
            lastCommand = cmd;
            relative = (cmd >= 'a' && cmd <= 'z');
            ++pos;
        } else {
            // Repeat last command (implicit)
            cmd = lastCommand;
            relative = (cmd >= 'a' && cmd <= 'z');
        }

        char cmdUpper = (cmd >= 'a' && cmd <= 'z') ? (cmd - 32) : cmd;

        if (cmdUpper == 'M') {
            flushPath();
            double x = parseNumber(svgPathData, pos);
            double y = parseNumber(svgPathData, pos);
            if (relative) {
                currentPoint += Point2D(x, y);
            } else {
                currentPoint = Point2D(x, y);
            }
            startPoint = currentPoint;
            currentPath.push_back(currentPoint);
            lastCommand = relative ? 'l' : 'L';  // Subsequent coords are LineTo

        } else if (cmdUpper == 'L') {
            double x = parseNumber(svgPathData, pos);
            double y = parseNumber(svgPathData, pos);
            if (relative) {
                currentPoint += Point2D(x, y);
            } else {
                currentPoint = Point2D(x, y);
            }
            currentPath.push_back(currentPoint);

        } else if (cmdUpper == 'H') {
            double x = parseNumber(svgPathData, pos);
            if (relative) {
                currentPoint.x += x;
            } else {
                currentPoint.x = x;
            }
            currentPath.push_back(currentPoint);

        } else if (cmdUpper == 'V') {
            double y = parseNumber(svgPathData, pos);
            if (relative) {
                currentPoint.y += y;
            } else {
                currentPoint.y = y;
            }
            currentPath.push_back(currentPoint);

        } else if (cmdUpper == 'C') {
            double x1 = parseNumber(svgPathData, pos);
            double y1 = parseNumber(svgPathData, pos);
            double x2 = parseNumber(svgPathData, pos);
            double y2 = parseNumber(svgPathData, pos);
            double x = parseNumber(svgPathData, pos);
            double y = parseNumber(svgPathData, pos);

            Point2D p1(x1, y1), p2(x2, y2), p3(x, y);
            if (relative) {
                p1 += currentPoint;
                p2 += currentPoint;
                p3 += currentPoint;
            }

            std::vector<Point2D> bezierPoints = approximateCubicBezier(
                currentPoint, p1, p2, p3, options.tolerance);
            for (size_t i = 1; i < bezierPoints.size(); ++i) {
                currentPath.push_back(bezierPoints[i]);
            }

            lastControl = p2;
            currentPoint = p3;

        } else if (cmdUpper == 'S') {
            double x2 = parseNumber(svgPathData, pos);
            double y2 = parseNumber(svgPathData, pos);
            double x = parseNumber(svgPathData, pos);
            double y = parseNumber(svgPathData, pos);

            // First control point is reflection of last control
            Point2D p1 = currentPoint * 2 - lastControl;
            Point2D p2(x2, y2), p3(x, y);
            if (relative) {
                p2 += currentPoint;
                p3 += currentPoint;
            }

            std::vector<Point2D> bezierPoints = approximateCubicBezier(
                currentPoint, p1, p2, p3, options.tolerance);
            for (size_t i = 1; i < bezierPoints.size(); ++i) {
                currentPath.push_back(bezierPoints[i]);
            }

            lastControl = p2;
            currentPoint = p3;

        } else if (cmdUpper == 'Q') {
            double x1 = parseNumber(svgPathData, pos);
            double y1 = parseNumber(svgPathData, pos);
            double x = parseNumber(svgPathData, pos);
            double y = parseNumber(svgPathData, pos);

            Point2D p1(x1, y1), p2(x, y);
            if (relative) {
                p1 += currentPoint;
                p2 += currentPoint;
            }

            std::vector<Point2D> bezierPoints = approximateQuadBezier(
                currentPoint, p1, p2, options.tolerance);
            for (size_t i = 1; i < bezierPoints.size(); ++i) {
                currentPath.push_back(bezierPoints[i]);
            }

            lastControl = p1;
            currentPoint = p2;

        } else if (cmdUpper == 'T') {
            double x = parseNumber(svgPathData, pos);
            double y = parseNumber(svgPathData, pos);

            Point2D p1 = currentPoint * 2 - lastControl;
            Point2D p2(x, y);
            if (relative) {
                p2 += currentPoint;
            }

            std::vector<Point2D> bezierPoints = approximateQuadBezier(
                currentPoint, p1, p2, options.tolerance);
            for (size_t i = 1; i < bezierPoints.size(); ++i) {
                currentPath.push_back(bezierPoints[i]);
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

            Point2D endPoint(x, y);
            if (relative) {
                endPoint += currentPoint;
            }

            if (!options.convertArcsToLines && std::abs(rx - ry) < 0.001) {
                // Circular arc - create Arc entity
                flushPath();

                double cx, cy, startAngle, sweepAngle;
                svgArcToCenterParams(
                    currentPoint.x, currentPoint.y,
                    rx, ry, xAxisRotation, largeArc, sweep,
                    endPoint.x, endPoint.y,
                    cx, cy, startAngle, sweepAngle);

                if (std::abs(sweepAngle) > 0.01) {
                    // Flip angles if Y is flipped
                    if (options.flipY) {
                        startAngle = -startAngle;
                        sweepAngle = -sweepAngle;
                    }

                    Entity arc = createArc(nextId++,
                                           transformPoint(Point2D(cx, cy)),
                                           rx * scale,
                                           startAngle,
                                           sweepAngle);
                    result.entities.push_back(arc);
                }

                currentPoint = endPoint;
                currentPath.push_back(currentPoint);
            } else {
                // Approximate arc with line segments
                double cx, cy, startAngle, sweepAngle;
                svgArcToCenterParams(
                    currentPoint.x, currentPoint.y,
                    rx, ry, xAxisRotation, largeArc, sweep,
                    endPoint.x, endPoint.y,
                    cx, cy, startAngle, sweepAngle);

                int segments = std::max(8, static_cast<int>(
                    std::abs(sweepAngle) / 360.0 * 32));

                for (int i = 1; i <= segments; ++i) {
                    double t = static_cast<double>(i) / segments;
                    double angle = (startAngle + t * sweepAngle) * M_PI / 180.0;
                    double px = cx + rx * std::cos(angle);
                    double py = cy + ry * std::sin(angle);
                    currentPath.push_back(Point2D(px, py));
                }

                currentPoint = endPoint;
            }

        } else if (cmdUpper == 'Z') {
            // Close path
            if (!currentPath.empty() && currentPoint != startPoint) {
                currentPath.push_back(startPoint);
            }
            flushPath();
            currentPoint = startPoint;
        }
    }

    flushPath();

    result.success = true;
    result.entityCount = static_cast<int>(result.entities.size());

    // Calculate bounds
    for (const Entity& e : result.entities) {
        result.bounds.include(e.boundingBox());
    }

    return result;
}

SVGImportResult importSVGFile(
    const std::string& filePath,
    int startId,
    const SVGImportOptions& options)
{
    std::ifstream file(filePath);
    if (!file) {
        SVGImportResult result;
        result.success = false;
        result.errorMessage = "Cannot open file: " + filePath;
        return result;
    }

    std::string content((std::istreambuf_iterator<char>(file)), {});
    return importSVGString(content, startId, options);
}

SVGImportResult importSVGString(
    const std::string& svgContent,
    int startId,
    const SVGImportOptions& options)
{
    SVGImportResult result;
    result.success = false;

    // Simple regex-based extraction of path data
    // For full SVG support, would need a proper XML parser
    std::regex pathRegex(R"(<path[^>]*\sd=[\"']([^\"']+)[\"'])");
    std::regex circleRegex(R"(<circle[^>]*\scx=[\"']([^\"']+)[\"'][^>]*\scy=[\"']([^\"']+)[\"'][^>]*\sr=[\"']([^\"']+)[\"'])");
    std::regex rectRegex(R"(<rect[^>]*\sx=[\"']([^\"']+)[\"'][^>]*\sy=[\"']([^\"']+)[\"'][^>]*\swidth=[\"']([^\"']+)[\"'][^>]*\sheight=[\"']([^\"']+)[\"'])");
    std::regex lineRegex(R"(<line[^>]*\sx1=[\"']([^\"']+)[\"'][^>]*\sy1=[\"']([^\"']+)[\"'][^>]*\sx2=[\"']([^\"']+)[\"'][^>]*\sy2=[\"']([^\"']+)[\"'])");

    int nextId = startId;
    double scale = options.scale;
    double ySign = options.flipY ? -1.0 : 1.0;

    auto transformPoint = [&](const Point2D& p) -> Point2D {
        return Point2D(p.x * scale + options.offset.x,
                       p.y * ySign * scale + options.offset.y);
    };

    // Extract paths
    {
        auto it = std::sregex_iterator(svgContent.begin(), svgContent.end(), pathRegex);
        auto end = std::sregex_iterator();
        for (; it != end; ++it) {
            std::string pathData = (*it)[1].str();

            SVGImportResult pathResult = importSVGPath(pathData, nextId, options);
            if (pathResult.success) {
                result.entities.insert(result.entities.end(),
                                       pathResult.entities.begin(),
                                       pathResult.entities.end());
                nextId += pathResult.entityCount;
            }
        }
    }

    // Extract circles
    {
        auto it = std::sregex_iterator(svgContent.begin(), svgContent.end(), circleRegex);
        auto end = std::sregex_iterator();
        for (; it != end; ++it) {
            double cx = std::stod((*it)[1].str());
            double cy = std::stod((*it)[2].str());
            double r = std::stod((*it)[3].str());

            Entity circle = createCircle(nextId++,
                                         transformPoint(Point2D(cx, cy)),
                                         r * scale);
            result.entities.push_back(circle);
        }
    }

    // Extract rectangles
    {
        auto it = std::sregex_iterator(svgContent.begin(), svgContent.end(), rectRegex);
        auto end = std::sregex_iterator();
        for (; it != end; ++it) {
            double x = std::stod((*it)[1].str());
            double y = std::stod((*it)[2].str());
            double w = std::stod((*it)[3].str());
            double h = std::stod((*it)[4].str());

            Entity rect = createRectangle(nextId++,
                                          transformPoint(Point2D(x, y)),
                                          transformPoint(Point2D(x + w, y + h)));
            result.entities.push_back(rect);
        }
    }

    // Extract lines
    {
        auto it = std::sregex_iterator(svgContent.begin(), svgContent.end(), lineRegex);
        auto end = std::sregex_iterator();
        for (; it != end; ++it) {
            double x1 = std::stod((*it)[1].str());
            double y1 = std::stod((*it)[2].str());
            double x2 = std::stod((*it)[3].str());
            double y2 = std::stod((*it)[4].str());

            Entity line = createLine(nextId++,
                                     transformPoint(Point2D(x1, y1)),
                                     transformPoint(Point2D(x2, y2)));
            result.entities.push_back(line);
        }
    }

    result.success = true;
    result.entityCount = static_cast<int>(result.entities.size());

    // Calculate bounds
    for (const Entity& e : result.entities) {
        result.bounds.include(e.boundingBox());
    }

    if (result.entityCount == 0) {
        result.errorMessage = "No supported elements found in SVG";
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
    std::string value;
};

/// Helper to trim whitespace from a string
static std::string trimString(const std::string& s)
{
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

/// Split a string by a delimiter character
static std::vector<std::string> splitString(const std::string& s, char delim)
{
    std::vector<std::string> result;
    std::istringstream stream(s);
    std::string item;
    while (std::getline(stream, item, delim)) {
        result.push_back(item);
    }
    return result;
}

/// Read next group code/value pair from DXF content
bool readDXFPair(const std::vector<std::string>& lines, int& lineIndex, DXFPair& pair)
{
    if (lineIndex + 1 >= static_cast<int>(lines.size())) return false;

    try {
        pair.code = std::stoi(trimString(lines[lineIndex]));
    } catch (...) {
        return false;
    }

    pair.value = trimString(lines[lineIndex + 1]);
    lineIndex += 2;
    return true;
}

/// Skip to next entity or section in DXF
void skipToNext(const std::vector<std::string>& lines, int& lineIndex)
{
    DXFPair pair;
    while (lineIndex < static_cast<int>(lines.size())) {
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
Entity parseDXFLine(const std::vector<std::string>& lines, int& lineIndex, int id,
                    double scale, const Point2D& offset)
{
    double x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    DXFPair pair;

    while (lineIndex < static_cast<int>(lines.size())) {
        int savedIndex = lineIndex;
        if (!readDXFPair(lines, lineIndex, pair)) break;

        if (pair.code == 0) {
            lineIndex = savedIndex;
            break;
        }

        switch (pair.code) {
        case 10: x1 = std::stod(pair.value) * scale + offset.x; break;
        case 20: y1 = std::stod(pair.value) * scale + offset.y; break;
        case 11: x2 = std::stod(pair.value) * scale + offset.x; break;
        case 21: y2 = std::stod(pair.value) * scale + offset.y; break;
        }
    }

    return createLine(id, Point2D(x1, y1), Point2D(x2, y2));
}

/// Parse a CIRCLE entity
Entity parseDXFCircle(const std::vector<std::string>& lines, int& lineIndex, int id,
                      double scale, const Point2D& offset)
{
    double cx = 0, cy = 0, r = 0;
    DXFPair pair;

    while (lineIndex < static_cast<int>(lines.size())) {
        int savedIndex = lineIndex;
        if (!readDXFPair(lines, lineIndex, pair)) break;

        if (pair.code == 0) {
            lineIndex = savedIndex;
            break;
        }

        switch (pair.code) {
        case 10: cx = std::stod(pair.value) * scale + offset.x; break;
        case 20: cy = std::stod(pair.value) * scale + offset.y; break;
        case 40: r = std::stod(pair.value) * scale; break;
        }
    }

    return createCircle(id, Point2D(cx, cy), r);
}

/// Parse an ARC entity
Entity parseDXFArc(const std::vector<std::string>& lines, int& lineIndex, int id,
                   double scale, const Point2D& offset)
{
    double cx = 0, cy = 0, r = 0;
    double startAngle = 0, endAngle = 360;
    DXFPair pair;

    while (lineIndex < static_cast<int>(lines.size())) {
        int savedIndex = lineIndex;
        if (!readDXFPair(lines, lineIndex, pair)) break;

        if (pair.code == 0) {
            lineIndex = savedIndex;
            break;
        }

        switch (pair.code) {
        case 10: cx = std::stod(pair.value) * scale + offset.x; break;
        case 20: cy = std::stod(pair.value) * scale + offset.y; break;
        case 40: r = std::stod(pair.value) * scale; break;
        case 50: startAngle = std::stod(pair.value); break;
        case 51: endAngle = std::stod(pair.value); break;
        }
    }

    // DXF arcs are always CCW, angles in degrees
    double sweep = endAngle - startAngle;
    if (sweep <= 0) sweep += 360;

    return createArc(id, Point2D(cx, cy), r, startAngle, sweep);
}

/// Parse an ELLIPSE entity
Entity parseDXFEllipse(const std::vector<std::string>& lines, int& lineIndex, int id,
                       double scale, const Point2D& offset)
{
    double cx = 0, cy = 0;
    double majorX = 1, majorY = 0;  // Major axis endpoint relative to center
    double ratio = 1.0;              // Minor/major ratio
    DXFPair pair;

    while (lineIndex < static_cast<int>(lines.size())) {
        int savedIndex = lineIndex;
        if (!readDXFPair(lines, lineIndex, pair)) break;

        if (pair.code == 0) {
            lineIndex = savedIndex;
            break;
        }

        switch (pair.code) {
        case 10: cx = std::stod(pair.value) * scale + offset.x; break;
        case 20: cy = std::stod(pair.value) * scale + offset.y; break;
        case 11: majorX = std::stod(pair.value) * scale; break;
        case 21: majorY = std::stod(pair.value) * scale; break;
        case 40: ratio = std::stod(pair.value); break;
        }
    }

    double majorRadius = std::sqrt(majorX * majorX + majorY * majorY);
    double minorRadius = majorRadius * ratio;

    return createEllipse(id, Point2D(cx, cy), majorRadius, minorRadius);
}

/// Parse a POINT entity
Entity parseDXFPoint(const std::vector<std::string>& lines, int& lineIndex, int id,
                     double scale, const Point2D& offset)
{
    double x = 0, y = 0;
    DXFPair pair;

    while (lineIndex < static_cast<int>(lines.size())) {
        int savedIndex = lineIndex;
        if (!readDXFPair(lines, lineIndex, pair)) break;

        if (pair.code == 0) {
            lineIndex = savedIndex;
            break;
        }

        switch (pair.code) {
        case 10: x = std::stod(pair.value) * scale + offset.x; break;
        case 20: y = std::stod(pair.value) * scale + offset.y; break;
        }
    }

    return createPoint(id, Point2D(x, y));
}

/// Parse a LWPOLYLINE entity
std::vector<Entity> parseDXFLWPolyline(const std::vector<std::string>& lines, int& lineIndex, int& nextId,
                                    double scale, const Point2D& offset)
{
    std::vector<Entity> entities;
    std::vector<Point2D> vertices;
    std::vector<double> bulges;
    bool closed = false;
    DXFPair pair;

    double currentX = 0, currentY = 0, currentBulge = 0;
    bool hasVertex = false;

    while (lineIndex < static_cast<int>(lines.size())) {
        int savedIndex = lineIndex;
        if (!readDXFPair(lines, lineIndex, pair)) break;

        if (pair.code == 0) {
            lineIndex = savedIndex;
            break;
        }

        switch (pair.code) {
        case 70:  // Flags
            closed = (std::stoi(pair.value) & 1) != 0;
            break;
        case 10:  // X coordinate
            if (hasVertex) {
                vertices.push_back(Point2D(currentX, currentY));
                bulges.push_back(currentBulge);
                currentBulge = 0;
            }
            currentX = std::stod(pair.value) * scale + offset.x;
            hasVertex = true;
            break;
        case 20:  // Y coordinate
            currentY = std::stod(pair.value) * scale + offset.y;
            break;
        case 42:  // Bulge
            currentBulge = std::stod(pair.value);
            break;
        }
    }

    // Add last vertex
    if (hasVertex) {
        vertices.push_back(Point2D(currentX, currentY));
        bulges.push_back(currentBulge);
    }

    if (vertices.size() < 2) return entities;

    // Convert to lines and arcs
    int numSegments = closed ? static_cast<int>(vertices.size()) : static_cast<int>(vertices.size()) - 1;
    for (int i = 0; i < numSegments; ++i) {
        int nextIdx = (i + 1) % static_cast<int>(vertices.size());
        double bulge = bulges[i];

        if (std::abs(bulge) < 1e-10) {
            // Straight line
            entities.push_back(createLine(nextId++, vertices[i], vertices[nextIdx]));
        } else {
            // Arc (bulge = tan(angle/4))
            Point2D p1 = vertices[i];
            Point2D p2 = vertices[nextIdx];
            Point2D mid = (p1 + p2) / 2;
            Point2D chord = p2 - p1;
            double chordLen = std::sqrt(chord.x * chord.x + chord.y * chord.y);

            // Perpendicular direction
            Point2D perp(-chord.y / chordLen, chord.x / chordLen);

            // Sagitta (distance from chord midpoint to arc)
            double sagitta = bulge * chordLen / 2;

            // Center of arc
            double radius = (chordLen * chordLen / 4 + sagitta * sagitta) / (2 * std::abs(sagitta));
            double centerDist = radius - std::abs(sagitta);
            if (bulge < 0) centerDist = -centerDist;

            Point2D center = mid + perp * centerDist;

            // Calculate angles
            double startAngle = std::atan2(p1.y - center.y, p1.x - center.x) * 180.0 / M_PI;
            double endAngle = std::atan2(p2.y - center.y, p2.x - center.x) * 180.0 / M_PI;

            double sweep = endAngle - startAngle;
            if (bulge > 0) {
                if (sweep < 0) sweep += 360;
            } else {
                if (sweep > 0) sweep -= 360;
            }

            entities.push_back(createArc(nextId++, center, radius, startAngle, sweep));
        }
    }

    return entities;
}

/// Parse a SPLINE entity (approximate as polyline)
std::vector<Entity> parseDXFSpline(const std::vector<std::string>& lines, int& lineIndex, int& nextId,
                                double scale, const Point2D& offset, double tolerance)
{
    std::vector<Entity> entities;
    std::vector<Point2D> controlPoints;
    std::vector<Point2D> fitPoints;
    int degree = 3;
    DXFPair pair;

    while (lineIndex < static_cast<int>(lines.size())) {
        int savedIndex = lineIndex;
        if (!readDXFPair(lines, lineIndex, pair)) break;

        if (pair.code == 0) {
            lineIndex = savedIndex;
            break;
        }

        static double tempX = 0, tempY = 0;
        static bool isFitPoint = false;

        switch (pair.code) {
        case 71: degree = std::stoi(pair.value); break;
        case 10:  // Control point X
            tempX = std::stod(pair.value) * scale + offset.x;
            isFitPoint = false;
            break;
        case 20:  // Control point Y
            tempY = std::stod(pair.value) * scale + offset.y;
            if (!isFitPoint) {
                controlPoints.push_back(Point2D(tempX, tempY));
            }
            break;
        case 11:  // Fit point X
            tempX = std::stod(pair.value) * scale + offset.x;
            isFitPoint = true;
            break;
        case 21:  // Fit point Y
            tempY = std::stod(pair.value) * scale + offset.y;
            fitPoints.push_back(Point2D(tempX, tempY));
            break;
        }
    }

    // Use fit points if available, otherwise control points
    const std::vector<Point2D>& points = fitPoints.empty() ? controlPoints : fitPoints;

    if (points.size() >= 2) {
        // Create as a spline entity if we have control points
        if (!controlPoints.empty()) {
            Entity spline;
            spline.id = nextId++;
            spline.type = EntityType::Spline;
            spline.points = controlPoints;
            entities.push_back(spline);
        } else {
            // Approximate as line segments
            for (size_t i = 0; i < points.size() - 1; ++i) {
                entities.push_back(createLine(nextId++, points[i], points[i + 1]));
            }
        }
    }

    return entities;
}

/// Parse TEXT or MTEXT entity (as text annotation)
Entity parseDXFText(const std::vector<std::string>& lines, int& lineIndex, int id,
                    double scale, const Point2D& offset)
{
    double x = 0, y = 0;
    double textHeight = 12.0;   // Default height in mm (DXF group 40)
    double rotation = 0.0;      // Rotation angle in degrees (DXF group 50)
    std::string textContent;
    std::string styleName;          // DXF text style name (group 7)
    DXFPair pair;

    while (lineIndex < static_cast<int>(lines.size())) {
        int savedIndex = lineIndex;
        if (!readDXFPair(lines, lineIndex, pair)) break;

        if (pair.code == 0) {
            lineIndex = savedIndex;
            break;
        }

        switch (pair.code) {
        case 10: x = std::stod(pair.value) * scale + offset.x; break;
        case 20: y = std::stod(pair.value) * scale + offset.y; break;
        case 40: textHeight = std::stod(pair.value) * scale; break;  // Text height
        case 50: rotation = std::stod(pair.value); break;            // Rotation angle
        case 7: styleName = pair.value; break;                       // Text style name
        case 1: textContent = pair.value; break;                     // TEXT content
        case 3: textContent += pair.value; break;                    // MTEXT continuation
        }
    }

    // Create text with parsed properties
    // Note: DXF text styles would need TABLES section parsing for full font info
    // For now, leave fontFamily empty (use default) and just import size/rotation
    return createText(id, Point2D(x, y), textContent, std::string(), textHeight,
                      false, false, rotation);
}

/// Case-insensitive string comparison helper
static bool containsCaseInsensitive(const std::vector<std::string>& vec, const std::string& str)
{
    for (const auto& item : vec) {
        if (item.size() != str.size()) continue;
        bool match = true;
        for (size_t i = 0; i < item.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(item[i])) !=
                std::tolower(static_cast<unsigned char>(str[i]))) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

/// Convert string to uppercase
static std::string toUpper(const std::string& s)
{
    std::string result = s;
    for (auto& c : result) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return result;
}

/// Check if string starts with a prefix (case-sensitive)
static bool startsWith(const std::string& s, const std::string& prefix)
{
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

}  // anonymous namespace

DXFImportResult importDXFString(
    const std::string& dxfContent,
    int startId,
    const DXFImportOptions& options)
{
    DXFImportResult result;
    result.success = false;

    if (dxfContent.empty()) {
        result.errorMessage = "Empty DXF content";
        return result;
    }

    std::vector<std::string> lines = splitString(dxfContent, '\n');
    int lineIndex = 0;
    int nextId = startId;

    std::string currentLayer;
    bool inEntitiesSection = false;

    DXFPair pair;
    while (lineIndex < static_cast<int>(lines.size())) {
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
            std::string entityType = toUpper(pair.value);

            // Read layer for filtering
            int peekIndex = lineIndex;
            DXFPair peekPair;
            currentLayer = "0";
            while (peekIndex < static_cast<int>(lines.size()) - 10) {
                if (readDXFPair(lines, peekIndex, peekPair)) {
                    if (peekPair.code == 0) break;
                    if (peekPair.code == 8) {
                        currentLayer = peekPair.value;
                        break;
                    }
                }
            }

            // Check layer filter
            if (!options.layerFilter.empty() &&
                !containsCaseInsensitive(options.layerFilter, currentLayer)) {
                skipToNext(lines, lineIndex);
                continue;
            }

            // Check construction layer filter
            if (options.ignoreConstructionLayers) {
                std::string layerUpper = toUpper(currentLayer);
                if (layerUpper == "DEFPOINTS" || layerUpper == "CONSTRUCTION" ||
                    startsWith(layerUpper, "CONSTR")) {
                    skipToNext(lines, lineIndex);
                    continue;
                }
            }

            // Track layers found
            if (!containsCaseInsensitive(result.layers, currentLayer)) {
                result.layers.push_back(currentLayer);
            }

            // Parse entity by type
            if (entityType == "LINE") {
                result.entities.push_back(parseDXFLine(lines, lineIndex, nextId++,
                                                     options.scale, options.offset));
            }
            else if (entityType == "CIRCLE") {
                result.entities.push_back(parseDXFCircle(lines, lineIndex, nextId++,
                                                       options.scale, options.offset));
            }
            else if (entityType == "ARC") {
                result.entities.push_back(parseDXFArc(lines, lineIndex, nextId++,
                                                    options.scale, options.offset));
            }
            else if (entityType == "ELLIPSE") {
                result.entities.push_back(parseDXFEllipse(lines, lineIndex, nextId++,
                                                        options.scale, options.offset));
            }
            else if (entityType == "POINT") {
                result.entities.push_back(parseDXFPoint(lines, lineIndex, nextId++,
                                                      options.scale, options.offset));
            }
            else if (entityType == "LWPOLYLINE") {
                auto polyEntities = parseDXFLWPolyline(lines, lineIndex, nextId,
                                                       options.scale, options.offset);
                result.entities.insert(result.entities.end(),
                                       polyEntities.begin(), polyEntities.end());
            }
            else if (entityType == "POLYLINE") {
                // Old-style polyline - similar to LWPOLYLINE but different structure
                // For now, skip to SEQEND
                while (lineIndex < static_cast<int>(lines.size())) {
                    if (readDXFPair(lines, lineIndex, pair)) {
                        if (pair.code == 0 && pair.value == "SEQEND") break;
                    }
                }
            }
            else if (entityType == "SPLINE") {
                auto splineEntities = parseDXFSpline(lines, lineIndex, nextId,
                                                       options.scale, options.offset,
                                                       options.splineTolerance);
                result.entities.insert(result.entities.end(),
                                       splineEntities.begin(), splineEntities.end());
            }
            else if (entityType == "TEXT" || entityType == "MTEXT") {
                result.entities.push_back(parseDXFText(lines, lineIndex, nextId++,
                                                     options.scale, options.offset));
            }
            else if (entityType == "INSERT" && options.importBlocks) {
                // Block reference - would need to expand from BLOCKS section
                // For now, just record the block name
                while (lineIndex < static_cast<int>(lines.size())) {
                    if (readDXFPair(lines, lineIndex, pair)) {
                        if (pair.code == 0) {
                            lineIndex -= 2;  // Back up
                            break;
                        }
                        if (pair.code == 2 && !containsCaseInsensitive(result.blocks, pair.value)) {
                            result.blocks.push_back(pair.value);
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
    result.entityCount = static_cast<int>(result.entities.size());

    // Calculate bounds
    for (const Entity& e : result.entities) {
        result.bounds.include(e.boundingBox());
    }

    if (result.entityCount == 0) {
        result.errorMessage = "No supported entities found in DXF";
    }

    return result;
}

DXFImportResult importDXFFile(
    const std::string& filePath,
    int startId,
    const DXFImportOptions& options)
{
    DXFImportResult result;

    std::ifstream file(filePath);
    if (!file) {
        result.errorMessage = "Cannot open file: " + filePath;
        return result;
    }

    std::string content((std::istreambuf_iterator<char>(file)), {});

    return importDXFString(content, startId, options);
}

}  // namespace sketch
}  // namespace hobbycad
