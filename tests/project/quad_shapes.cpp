// =====================================================================
//  tests/project/quad_shapes.cpp — four-sided entities (a rotated
//  rectangle's four corners, a parallelogram) through queries, box
//  selection, profiles and export.
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#include <hobbycad/sketch/entity.h>
#include <hobbycad/sketch/constraint.h>
#include <hobbycad/sketch/queries.h>
#include <hobbycad/sketch/profiles.h>
#include <hobbycad/sketch/export.h>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace hobbycad;
using namespace hobbycad::sketch;

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}
static bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }
// The value of the first `code` group inside the first `entity` in a DXF, or
// "" when there is none. Searching the whole file for "70\n1" matched the same
// group codes in the header and tables, so a flag forced to closed passed.
static std::string groupAfter(const std::string& dxf, const std::string& entity,
                              const std::string& code) {
    std::vector<std::string> lines;
    size_t start = 0;
    for (size_t nl; (nl = dxf.find('\n', start)) != std::string::npos; start = nl + 1) {
        lines.push_back(dxf.substr(start, nl - start));
    }
    for (size_t i = 0; i + 1 < lines.size(); i += 2) {
        if (lines[i] != "0" || lines[i + 1] != entity) continue;
        for (size_t j = i + 2; j + 1 < lines.size() && lines[j] != "0"; j += 2) {
            if (lines[j] == code) return lines[j + 1];
        }
        return {};
    }
    return {};
}
static int count(const std::string& hay, const std::string& needle) {
    int n = 0;
    for (size_t at = hay.find(needle); at != std::string::npos; at = hay.find(needle, at + needle.size())) ++n;
    return n;
}

int main() {
    std::printf("quad shapes\n");

    // Base 4, height 3, leaning right: area 12, slanted sides sqrt(13).
    Entity para; para.id = 1; para.type = EntityType::Parallelogram;
    para.points = {Point3{0, 0, 0}, Point3{4, 0, 0}, Point3{6, 3, 0}, Point3{2, 3, 0}};
    // A unit-diagonal square turned 45 degrees: side sqrt(2).
    Entity rot; rot.id = 2; rot.type = EntityType::Rectangle;
    rot.points = {Point3{0, 0, 0}, Point3{1, 1, 0}, Point3{0, 2, 0}, Point3{-1, 1, 0}};
    Entity box; box.id = 3; box.type = EntityType::Rectangle;
    box.points = {Point3{0, 0, 0}, Point3{3, 2, 0}};

    Point2D c[4];
    check(quadCorners(para, c) && c[2] == Point2D(6, 3), "quadCorners returns a parallelogram's corners");
    check(quadCorners(box, c) && c[1] == Point2D(3, 0) && c[3] == Point2D(0, 2),
          "quadCorners expands a two-point rectangle");
    Entity line; line.type = EntityType::Line; line.points = {Point3{0, 0, 0}, Point3{1, 0, 0}};
    check(!quadCorners(line, c), "quadCorners refuses other entities");

    const double paraPerimeter = 8 + 2 * std::sqrt(13.0);
    check(near(entityLength(para), paraPerimeter), "parallelogram length is its perimeter");
    check(near(entityLength(rot), 4 * std::sqrt(2.0)), "rotated rectangle length follows its corners");
    check(near(entityLength(box), 10), "axis-aligned rectangle length unchanged");

    const auto outline = tessellate(para);
    check(outline.size() == 5 && outline.front() == outline.back() && outline[2] == Point2D(6, 3),
          "tessellate closes the parallelogram outline");

    check(pointAtParameter(para, 0) == Point2D(0, 0), "parameter 0 is the first corner");
    const Point2D mid = pointAtParameter(para, (4 + std::sqrt(13.0) / 2) / paraPerimeter);
    check(near(mid.x, 5) && near(mid.y, 1.5), "parameter walks the slanted side");

    check(entityIntersectsRect(para, Rect2D(4.9, 1.4, 0.2, 0.2)), "box selection crosses a slanted side");
    check(!entityIntersectsRect(para, Rect2D(10, 10, 1, 1)), "a distant box misses");
    check(entityEnclosedByRect(para, Rect2D(-1, -1, 8, 5)), "window selection encloses the parallelogram");
    check(!entityEnclosedByRect(para, Rect2D(-1, -1, 6, 5)), "window selection needs the far corner");

    const auto profiles = detectProfiles({para});
    check(profiles.size() == 1 && near(std::fabs(profiles.front().area), 12),
          "a parallelogram is a closed profile of area 12");

    const std::string svg = sketchToSVG({para});
    check(svg.find("L 6 -3 L 2 -3 Z") != std::string::npos, "SVG path runs through the corners");

    const std::string dxf = sketchToDXF({para});
    check(count(dxf, "LWPOLYLINE") == 1 && groupAfter(dxf, "LWPOLYLINE", "90") == "4"
          && groupAfter(dxf, "LWPOLYLINE", "70") == "1", "DXF polyline: four vertices, closed");
    DXFExportOptions lines; lines.usePolylines = false;
    check(count(sketchToDXF({para}, lines), "0\nLINE\n") == 4, "DXF without polylines writes four LINEs");

    Entity spline; spline.id = 4; spline.type = EntityType::Spline;
    spline.points = {Point3{0, 0, 0}, Point3{1, 1, 0}, Point3{2, 0, 0}, Point3{3, 1, 0}};
    const std::string open = sketchToDXF({spline});
    check(count(open, "LWPOLYLINE") == 1 && groupAfter(open, "LWPOLYLINE", "70") == "0",
          "an open spline is not flagged closed");

    int skipped = -1;
    const auto script = sketchToScript("q", {para, box}, {}, {}, 4, &skipped);
    std::string joined;
    for (const auto& s : script) joined += s + "\n";
    check(skipped == 0 && count(joined, "line from") == 4 && count(joined, "rectangle from") == 1,
          "script replays a parallelogram as four lines, a box as a rectangle");
    check(joined.find("line from 2,3 to 0,0") != std::string::npos, "the last line closes the outline");

    check(isAngularConstraint(ConstraintType::TangentAngle), "tangent angle is angular");
    check(isDimensionalConstraint(ConstraintType::TangentAngle), "tangent angle is dimensional");

    std::printf("%s\n", failures ? "FAILURES" : "all passed");
    return failures ? 1 : 0;
}
