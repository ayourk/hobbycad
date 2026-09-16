// =====================================================================
//  src/libhobbycad/sketch/export.cpp — Sketch export implementation
// =====================================================================
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include <hobbycad/sketch/export.h>
#include <hobbycad/units.h>
#include <hobbycad/sketch/queries.h>
#include <hobbycad/geometry/types.h>
#include <hobbycad/format.h>
#include <hobbycad/geometry/utils.h>

#include <cctype>
#include <cstdio>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <regex>
#include <sstream>
#include <string>
#include <vector>
#include <functional>

#include <hobbycad/math_constants.h>

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
            double startRad = degreesToRadians(entity.startAngle);
            double endRad = degreesToRadians(entity.startAngle + entity.sweepAngle);

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
    case EntityType::Parallelogram:
        if (Point2D c[4]; quadCorners(entity, c)) {
            // From the corners: a rotated rectangle or a parallelogram is not
            // the axis-aligned box of its first two points.
            path = hobbycad::format("M %g %g L %g %g L %g %g L %g %g Z",
                                    c[0].x * scale, -c[0].y * scale, c[1].x * scale, -c[1].y * scale,
                                    c[2].x * scale, -c[2].y * scale, c[3].x * scale, -c[3].y * scale);
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

    case EntityType::Dimension:
        // A dimension is an annotation the GUI draws, never stored geometry.
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

    // The ACI index (62) keeps the layer/construction color for viewers that
    // ignore true color; when the entity carries an explicit RGB, add a true-
    // color group (420) too, which overrides the index on readback.
    std::string colorGroup = "62\n" + std::to_string(color) + "\n";
    if (entity.color >= 0) colorGroup += "420\n" + std::to_string(entity.color) + "\n";

    const std::streampos before = out.tellp();
    switch (entity.type) {
    case EntityType::Point:
        if (!entity.points.empty()) {
            out << "0\nPOINT\n";
            out << "8\n" << layer << "\n";
            out << colorGroup;
            out << "10\n" << entity.points[0].x << "\n";
            out << "20\n" << entity.points[0].y << "\n";
            out << "30\n0\n";
        }
        break;

    case EntityType::Line:
        if (entity.points.size() >= 2) {
            out << "0\nLINE\n";
            out << "8\n" << layer << "\n";
            out << colorGroup;
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
            out << colorGroup;
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
            out << colorGroup;
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
            out << colorGroup;
            out << "10\n" << entity.points[0].x << "\n";
            out << "20\n" << entity.points[0].y << "\n";
            out << "30\n0\n";
            // Major axis endpoint relative to center, rotated by the ellipse angle
            const double th = degreesToRadians(entity.ellipseRotation);
            out << "11\n" << (entity.majorRadius * std::cos(th)) << "\n";
            out << "21\n" << (entity.majorRadius * std::sin(th)) << "\n";
            out << "31\n0\n";
            // Ratio of minor to major
            out << "40\n" << (entity.minorRadius / entity.majorRadius) << "\n";
            const double eStartRad = degreesToRadians(entity.ellipseStart);
            const double eEndRad = eStartRad + degreesToRadians(entity.ellipseSweep);
            out << "41\n" << eStartRad << "\n";   // Start parameter (radians)
            out << "42\n" << eEndRad << "\n";     // End parameter (radians)
        }
        break;

    case EntityType::Rectangle:
    case EntityType::Parallelogram:
    case EntityType::Polygon:
    case EntityType::Slot:
    case EntityType::Spline:
        {
            std::vector<Point2D> points = tessellate(entity, 0.5);
            // Closed only when the outline returns to its start: an open
            // spline flagged closed gained a chord between its ends. A closed
            // outline's repeated last vertex is dropped, since flag 70 closes it.
            const bool closed = points.size() > 2 && points.front() == points.back();
            if (closed) points.pop_back();
            if (points.size() < 2) break;
            if (options.usePolylines) {
                // LWPOLYLINE for complex shapes
                out << "0\nLWPOLYLINE\n";
                out << "8\n" << layer << "\n";
                out << colorGroup;
                out << "90\n" << points.size() << "\n";
                out << "70\n" << (closed ? 1 : 0) << "\n";
                for (const Point2D& p : points) {
                    out << "10\n" << p.x << "\n";
                    out << "20\n" << p.y << "\n";
                }
            } else {
                // Without polylines, one LINE per segment; the shape used to be
                // left out of the file entirely.
                const size_t n = points.size();
                const size_t segments = closed ? n : n - 1;
                for (size_t i = 0; i < segments; ++i) {
                    const Point2D& a = points[i];
                    const Point2D& b = points[(i + 1) % n];
                    out << "0\nLINE\n";
                    out << "8\n" << layer << "\n";
                    out << colorGroup;
                    out << "10\n" << a.x << "\n20\n" << a.y << "\n30\n0\n";
                    out << "11\n" << b.x << "\n21\n" << b.y << "\n31\n0\n";
                }
            }
        }
        break;

    case EntityType::Dimension:
        // A dimension is an annotation the GUI draws, never stored geometry.
        break;

    case EntityType::Text:
        if (!entity.points.empty()) {
            out << "0\nTEXT\n";
            out << "8\n" << layer << "\n";
            out << colorGroup;
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

    // OCS extrusion: when the sketch plane's normal is not +Z, tag the
    // entity (its stored coords are the OCS (u,v) of this normal, since
    // the plane basis equals arbitraryAxisBasis(normal)). A reader then
    // recovers the plane. +Z writes nothing, keeping flat XY files clean.
    const Vec3& n = options.extrusion;
    if (out.tellp() != before &&
        (std::fabs(n.x) > 1e-9f || std::fabs(n.y) > 1e-9f || std::fabs(n.z - 1.0f) > 1e-9f)) {
        out << "210\n" << n.x << "\n220\n" << n.y << "\n230\n" << n.z << "\n";
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

    double phiRad = degreesToRadians(phi);
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

    startAngle = radiansToDegrees(angle(1, 0, ux, uy));
    sweepAngle = radiansToDegrees(angle(ux, uy, vx, vy));

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
                    double angle = degreesToRadians(startAngle + t * sweepAngle);
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

    result.bounds = sketchBounds(result.entities);

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

    result.bounds = sketchBounds(result.entities);

    if (result.entityCount == 0) {
        result.errorMessage = "No supported elements found in SVG";
    }

    return result;
}

// =====================================================================
//  DXF Import moved to sketch/dxf_import.{h,cpp} (purpose-built parser)
// =====================================================================

// =====================================================================
//  Command script
// =====================================================================

namespace {

std::string joinStrings(const std::vector<std::string>& parts, char sep)
{
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) out += sep;
        out += parts[i];
    }
    return out;
}

std::string idList(const std::vector<int>& v)
{
    std::vector<std::string> s;
    s.reserve(v.size());
    for (int i : v) s.push_back(std::to_string(i));
    return joinStrings(s, ',');
}

std::string exactDouble(double v)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.17g", v);
    return buf;
}

}  // namespace

std::vector<std::string> sketchToScript(const std::string& name,
                                        const std::vector<Entity>& entities,
                                        const std::vector<Constraint>& constraints,
                                        const std::vector<Group>& groups,
                                        int precision, int* skippedOut)
{
    // Every line must be a command the parser accepts. That is not
    // automatic: this emitter once wrote "arc <pt> <pt> <pt>", which the
    // arc command rejects, and it tested arcs for three points when a CLI
    // arc has one, so arcs were both mis-spelled and silently skipped. The
    // round-trip test in tests/cli/ keeps that from recurring.
    std::vector<std::string> out;
    out.push_back("create sketch \"" + name + "\"");

    auto num = [precision](double v) { return formatDouble(v, precision); };
    auto pt = [&num](const Point2D& p) { return num(p.x) + "," + num(p.y); };

    int skipped = 0;
    for (const Entity& e : entities) {
        std::string line;
        switch (e.type) {
        case EntityType::Line:
            if (e.points.size() >= 2)
                line = "line from " + pt(e.points[0]) + " to " + pt(e.points[1]);
            break;
        case EntityType::Circle:
            if (!e.points.empty())
                line = "circle at " + pt(e.points[0]) + " radius " + num(e.radius);
            break;
        case EntityType::Rectangle:
        case EntityType::Parallelogram:
            if (e.type == EntityType::Rectangle && e.points.size() == 2) {
                line = "rectangle from " + pt(e.points[0]) + " to " + pt(e.points[1]);
            } else if (Point2D c[4]; quadCorners(e, c)) {
                // No command draws a rotated rectangle or a parallelogram, and
                // "rectangle from" would replay the axis-aligned box of two of
                // its corners. Four lines replay the outline itself.
                const std::string tail = e.isConstruction ? " construction" : "";
                for (int i = 0; i < 3; ++i)
                    out.push_back("  line from " + pt(c[i]) + " to " + pt(c[i + 1]) + tail);
                line = "line from " + pt(c[3]) + " to " + pt(c[0]);
            }
            break;
        case EntityType::Arc:
            // Center, radius and two angles: the form the arc command parses.
            if (!e.points.empty())
                line = "arc at " + pt(e.points[0]) + " radius " + num(e.radius) +
                       " angle " + num(e.startAngle) + " to " + num(e.startAngle + e.sweepAngle);
            break;
        case EntityType::Point:
            if (!e.points.empty())
                line = "point " + pt(e.points[0]);
            break;
        case EntityType::Polygon:
            if (!e.points.empty())
                line = "polygon at " + pt(e.points[0]) + " radius " + num(e.radius) +
                       " sides " + std::to_string(e.sides);
            break;
        case EntityType::Ellipse:
            if (!e.points.empty())
                line = "ellipse at " + pt(e.points[0]) + " major " + num(e.majorRadius) +
                       " minor " + num(e.minorRadius);
            break;
        case EntityType::Slot:
            // A slot that follows a path replays as "slot along": the path has
            // already been emitted above, and the geometric forms would build
            // a SECOND centerline on top of the one in the script.
            if (e.pathEntityIds.size() == 1) {
                line = "slot along " + std::to_string(e.pathEntityIds[0]) +
                       " width " + num(e.radius * 2.0);
            } else if (e.points.size() == 2) {
                line = "slot from " + pt(e.points[0]) + " to " + pt(e.points[1]) +
                       " width " + num(e.radius * 2.0);
            } else if (e.points.size() == 3) {
                // The cap-center form: it is what is stored, so nothing has to
                // survive a trip through angles and back.
                const Point2D c(e.points[0]), st(e.points[1]), en(e.points[2]);
                line = "slot arc at " + pt(c) + " radius " + num(geometry::lineLength(c, st)) +
                       " from " + pt(st) + " to " + pt(en) + " width " + num(e.radius * 2.0);
                // Two cap centers describe both ways round; without this the
                // long way replays as the short one.
                if (e.arcFlipped) line += " long";
            }
            break;
        case EntityType::Spline:
            if (e.splineBezier) {
                // As `bezier` (anchors + angle/length handles) so replay
                // recreates the handle-spline, not a Catmull-Rom reading of
                // the control points.
                const std::vector<Point2D> poly(e.points.begin(), e.points.end());
                const std::vector<BezierAnchor> anchors = bezierAnchorsFromControlPolygon(poly);
                if (!anchors.empty()) {
                    std::vector<std::string> toks;
                    for (const BezierAnchor& a : anchors) {
                        toks.push_back(num(a.pos.x) + "," + num(a.pos.y));
                        auto emitH = [&](const char* kw, BezierHandleSide side) {
                            double ang = 0.0, len = 0.0;
                            if (!anchorHandlePolar(a, side, ang, len)) return;   // coincident: a corner
                            toks.push_back(kw);
                            toks.push_back(num(ang));
                            toks.push_back(num(len));
                        };
                        emitH("in",  BezierHandleSide::In);
                        emitH("out", BezierHandleSide::Out);
                    }
                    line = "bezier " + joinStrings(toks, ' ');
                }
            } else if (e.points.size() >= 2) {
                std::vector<std::string> pts;
                for (const auto& p : e.points) pts.push_back(pt(p));
                line = "spline through " + joinStrings(pts, ' ');
            }
            break;
        case EntityType::Text: {
            if (e.points.empty() || e.text.empty()) break;
            // Quote unconditionally, and escape what would end the quote. A
            // caption containing a space replays as two arguments otherwise,
            // which is a wrong drawing rather than an error.
            std::string content;
            for (char ch : e.text) {
                if (ch == '\\' || ch == '"') content += '\\';
                content += ch;
            }
            line = "text \"" + content + "\" at " + pt(e.points[0]) + " size " + num(e.fontSize);
            if (e.textRotation != 0.0) line += " rotation " + num(e.textRotation);
            break;
        }
        default:
            break;
        }

        if (line.empty()) {
            // Say so rather than silently dropping it. A script that is
            // quietly incomplete is worse than one that admits a gap.
            ++skipped;
            continue;
        }
        // Construction geometry is a property of the entity, not a separate
        // type; dropping it would replay reference lines as real edges.
        if (e.isConstruction) line += " construction";
        out.push_back("  " + line);
    }
    if (skipped > 0) {
        out.push_back("  # " + std::to_string(skipped) +
                      " entity/entities have no CLI command yet and are NOT in this script");
    }

    // Constraints after ALL the geometry: every one names entities by id,
    // and an id has to exist before it can be constrained.
    for (const Constraint& c : constraints) {
        std::vector<std::string> refs;
        for (size_t i = 0; i < c.entityIds.size(); ++i) {
            refs.push_back(i < c.pointIndices.size()
                               ? std::to_string(c.entityIds[i]) + "." + std::to_string(c.pointIndices[i])
                               : std::to_string(c.entityIds[i]));
        }
        std::string typeName;
        for (const char* p = constraintTypeName(c.type); *p; ++p) {
            if (*p == ' ') continue;
            typeName += static_cast<char>(std::tolower(static_cast<unsigned char>(*p)));
        }
        std::string line = "  constrain " + typeName + " " + joinStrings(refs, ' ');
        if (isDimensionalConstraint(c.type)) line += " " + num(c.value);
        if (!c.isDriving) line += " reference";
        out.push_back(line);
    }

    // Groups last: every one names entities, constraints or other groups by
    // id. Ids are emitted explicitly rather than left to fall out of
    // creation order: a group's identity is referenced by its children's
    // parentGroupId and by name lookups, so it has to come back the same.
    for (const Group& g : groups) {
        // Skip the group a slot makes for itself (kind Slot, holding a slot
        // that follows a path): "slot along" recreates it on replay, and
        // emitting it too would collide on the name.
        bool autoSlotGroup = false;
        if (g.kind == GroupKind::Slot) {
            for (int eid : g.entityIds) {
                const Entity* e = findEntityById(entities, eid);
                if (e && e->type == EntityType::Slot && !e->pathEntityIds.empty()) {
                    autoSlotGroup = true;
                    break;
                }
            }
        }
        if (autoSlotGroup) continue;

        std::string line = "  group \"" + g.name + "\" id=" + std::to_string(g.id);
        if (!g.entityIds.empty())     line += " entities " + idList(g.entityIds);
        if (!g.constraintIds.empty()) line += " constraints " + idList(g.constraintIds);
        if (!g.childGroupIds.empty()) line += " groups " + idList(g.childGroupIds);
        if (g.locked) line += " locked";
        if (g.kind == GroupKind::SweepAngle) line += " sweep";
        if (g.hasPivot) line += " pivot " + exactDouble(g.pivot.x) + "," + exactDouble(g.pivot.y);
        out.push_back(line);
    }
    out.push_back("finish");
    if (skippedOut) *skippedOut = skipped;
    return out;
}

}  // namespace sketch
}  // namespace hobbycad
