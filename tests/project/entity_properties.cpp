// =====================================================================
//  tests/project/entity_properties.cpp — sketch::setEntityNumber / Point /
//  Text: the property rules every front end shares.
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#include <hobbycad/sketch/properties.h>
#include <hobbycad/sketch/entity.h>
#include <cmath>
#include <cstdio>

using namespace hobbycad;
using namespace hobbycad::sketch;

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}
static bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

int main() {
    std::printf("entity properties\n");

    Entity circle; circle.type = EntityType::Circle; circle.radius = 5;
    circle.points = {Point3{0, 0, 0}, Point3{5, 0, 0}};
    check(setEntityNumber(circle, "radius", 8).changed && near(circle.radius, 8)
          && near(circle.points[1].x, 8), "radius rescales a circle about its center");
    check(setEntityNumber(circle, "diameter", 4).changed && near(circle.radius, 2), "diameter sets half");
    check(setEntityNumber(circle, "radius", 0).problem == PropertyProblem::NotPositive, "zero radius refused");
    check(setEntityNumber(circle, "radius", NAN).problem == PropertyProblem::NotANumber, "NaN refused");
    check(setEntityNumber(circle, "bogus", 1).problem == PropertyProblem::UnknownProperty, "unknown name refused");

    Entity line; line.type = EntityType::Line;
    line.points = {Point3{0, 0, 0}, Point3{3, 4, 0}};
    check(setEntityNumber(line, "length", 10).changed && near(line.points[1].x, 6) && near(line.points[1].y, 8),
          "length keeps the direction and the first point");
    Entity dot; dot.type = EntityType::Line; dot.points = {Point3{1, 1, 0}, Point3{1, 1, 0}};
    check(setEntityNumber(dot, "length", 5).problem == PropertyProblem::Degenerate, "a zero-length line has no direction");

    Entity rect; rect.type = EntityType::Rectangle;
    rect.points = {Point3{10, 10, 0}, Point3{2, 3, 0}};
    check(setEntityNumber(rect, "width", 6).changed && near(rect.points[1].x, 4), "width moves the far corner on its side");
    check(setEntityNumber(rect, "height", 6).changed && near(rect.points[1].y, 4), "height likewise");

    Entity poly; poly.type = EntityType::Polygon; poly.sides = 6;
    check(setEntityNumber(poly, "sides", 2).problem == PropertyProblem::OutOfRange, "sides below 3 refused");
    check(setEntityNumber(poly, "sides", 8).changed && poly.sides == 8, "sides set");

    Entity arc; setArcFromAngles(arc, Point2D(0, 0), 10, 0, 90);
    check(near(arc.points[1].x, 10) && near(arc.points[2].y, 10), "setArcFromAngles writes both ends");
    check(setEntityNumber(arc, "sweepAngle", 180).changed && near(arc.points[2].x, -10), "sweep resyncs the end");
    check(nearestArcEndIndex(arc, Point2D(-9, 1)) == 2, "nearest arc end");

    Entity text; text.type = EntityType::Text; text.text = "ab"; text.fontSize = 5;
    text.points = {Point3{0, 0, 0}, Point3{0, 0, 0}};
    check(setEntityNumber(text, "textRotation", 90).changed && near(text.points[1].y, 10) && near(text.points[1].x, 0),
          "text rotation moves the handle (two font sizes up)");
    check(setEntityText(text, "a much longer caption").changed && text.points[1].y > 10, "a longer caption pushes the handle out");
    check(setEntityPoint(text, 5, Point2D(1, 1)).problem == PropertyProblem::NoSuchPoint, "bad point index refused");
    PropertyEdit pe = setEntityPoint(line, 0, Point2D(1, 2));
    check(pe.changed && pe.editedPointIndex == 0 && near(line.points[0].x, 1), "point edit reports its index");

    if (failures == 0) std::printf("project entity_properties: ALL PASS\n");
    else std::printf("project entity_properties: %d FAIL\n", failures);
    return failures ? 1 : 0;
}
