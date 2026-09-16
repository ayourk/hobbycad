// =====================================================================
//  tests/project/measurements.cpp — sketch::resolveMeasurement: a field
//  read like a Parameters dialog expression, with units.
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#include <hobbycad/sketch/parsing.h>
#include <cmath>
#include <cstdio>
#include <map>

using namespace hobbycad;
using namespace hobbycad::sketch;

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}
static bool near(double a, double b) { return std::fabs(a - b) < 1e-6; }

int main() {
    std::printf("measurements\n");
    const std::map<std::string, double> params{{"w", 10.0}, {"r", 4.0}};
    double v = 0.0;
    check(resolveMeasurement("10.00 mm", MeasureKind::Length, LengthUnit::Millimeters, params, v) && near(v, 10.0),
          "a cell as displayed, with its unit");
    check(resolveMeasurement("2 in", MeasureKind::Length, LengthUnit::Millimeters, params, v) && near(v, 50.8),
          "an inch suffix converts to millimeters");
    check(resolveMeasurement("2", MeasureKind::Length, LengthUnit::Inches, params, v) && near(v, 50.8),
          "a bare length is in the display unit");
    check(resolveMeasurement("w/2", MeasureKind::Length, LengthUnit::Millimeters, params, v) && near(v, 5.0),
          "a bare formula over parameters, like the Parameters dialog");
    check(resolveMeasurement("(w + r) * 2 mm", MeasureKind::Length, LengthUnit::Inches, params, v) && near(v, 28.0),
          "an expression with a unit ignores the display unit");
    check(resolveMeasurement("1e3", MeasureKind::Length, LengthUnit::Millimeters, params, v) && near(v, 1000.0),
          "exponent notation reads as a number");
    check(resolveMeasurement("90.0\xc2\xb0", MeasureKind::Angle, LengthUnit::Millimeters, params, v) && near(v, 90.0),
          "the degree sign the cells show");
    check(resolveMeasurement("45 deg", MeasureKind::Angle, LengthUnit::Millimeters, params, v) && near(v, 45.0),
          "deg suffix");
    check(resolveMeasurement("0.5 rad", MeasureKind::Angle, LengthUnit::Millimeters, params, v) && near(v, 28.647889756541161),
          "radians convert to degrees");
    check(!resolveMeasurement("2 in", MeasureKind::Angle, LengthUnit::Millimeters, params, v), "a length in an angle field is refused");
    check(!resolveMeasurement("45 deg", MeasureKind::Length, LengthUnit::Millimeters, params, v), "an angle in a length field is refused");
    check(resolveMeasurement("6", MeasureKind::Count, LengthUnit::Millimeters, params, v) && near(v, 6.0), "a count");
    check(!resolveMeasurement("6 mm", MeasureKind::Count, LengthUnit::Millimeters, params, v), "a count takes no unit");
    check(!resolveMeasurement("abc", MeasureKind::Length, LengthUnit::Millimeters, params, v), "an unknown name does not evaluate");
    check(!resolveMeasurement("", MeasureKind::Length, LengthUnit::Millimeters, params, v), "empty refused");
    Point2D p;
    check(resolveMeasuredPoint("(10.00, 20.00) mm", LengthUnit::Inches, params, p) && near(p.x, 10.0) && near(p.y, 20.0),
          "a point cell as displayed, the unit applying to both");
    check(resolveMeasuredPoint("1, 2", LengthUnit::Inches, params, p) && near(p.x, 25.4) && near(p.y, 50.8),
          "a bare pair is in the display unit");
    check(resolveMeasuredPoint("(w, r*2)", LengthUnit::Millimeters, params, p) && near(p.x, 10.0) && near(p.y, 8.0),
          "formulas in a point");
    check(!resolveMeasuredPoint("(1, 2, 3)", LengthUnit::Millimeters, params, p), "three parts refused");
    if (failures == 0) std::printf("project measurements: ALL PASS\n");
    else std::printf("project measurements: %d FAIL\n", failures);
    return failures ? 1 : 0;
}
