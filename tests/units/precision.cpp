// =====================================================================
//  tests/units/precision.cpp — display vs storage precision
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  Aaron settled these on 2026-02-16: decimal inches to 4 places, metric
//  to what a caliper reads (0.01 mm). And display precision is
//  deliberately NOT storage precision.
#include <hobbycad/units.h>
#include <cstdio>
#include <string>

using namespace hobbycad;
static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

int main() {
    std::printf("unit precision\n");

    check(unitDisplayPrecision(LengthUnit::Inches) == 4,
          "decimal inches show 4 places");
    check(unitDisplayPrecision(LengthUnit::Millimeters) == 3,
          "millimeters show 3: micrometer resolution, for metalwork");
    check(unitDisplayPrecision(LengthUnit::Millimeters)
              != unitDisplayPrecision(LengthUnit::Inches),
          "so the two units genuinely differ");

    // Metric units should express the SAME physical resolution, or the
    // displayed precision changes when a user switches units without
    // anything about the model changing.
    check(unitDisplayPrecision(LengthUnit::Centimeters) == 4, "cm: 0.0001 cm = 0.001 mm");
    check(unitDisplayPrecision(LengthUnit::Meters) == 6,      "m:  0.000001 m = 0.001 mm");

    // The formatter must ASK for the unit's precision. It previously took
    // formatDouble()'s default of 4 for everything, so this is the
    // assertion that catches the wiring being dropped again.
    const std::string mm = formatValueWithUnit(1.23456, LengthUnit::Millimeters);
    check(mm == "1.235 mm", "mm formats to 3 places");

    // Every metric unit must resolve the same physical distance, or the
    // displayed precision changes when the user switches units.
    check(formatValueWithUnit(0.001, LengthUnit::Millimeters) != "0 mm",
          "a micron is visible in mm");
    check(formatValueWithUnit(0.001, LengthUnit::Centimeters) != "0 cm",
          "and in cm");
    check(formatValueWithUnit(0.001, LengthUnit::Meters) != "0 m",
          "and in m: none of them round it away");
    const std::string in = formatValueWithUnit(25.4 * 1.23456, LengthUnit::Inches);
    check(in.rfind("1.2346", 0) == 0, "inches format to 4 places");

    // Display and storage are separate on purpose: storage keeps what the
    // model actually holds, display keeps what a person can act on.
    check(StoragePrecision > unitDisplayPrecision(LengthUnit::Inches),
          "storage precision exceeds every display precision");
    const std::string stored = formatStorageValue(1.23456789012345);
    check(stored.find("1.234567890") == 0,
          "storage keeps far more than the display would");

    // Round-tripping a value through display would LOSE data, which is
    // why an exported script uses storage precision, not display.
    check(formatValue(1.23456) != formatStorageValue(1.23456),
          "display and storage formatting genuinely differ");

    // ---- unit suffixes are case-insensitive, NOT SI multipliers --------
    // Aaron asked whether capitals mark thousands. They do not: the suffix
    // is lowercased before matching, so "M" and "m" both mean meters. There
    // is no mega/kilo prefix system, and adding one would collide: "M"
    // cannot mean both meters and mega.
    check(parseUnitSuffix("M")  == LengthUnit::Meters,      "capital M is meters");
    check(parseUnitSuffix("m")  == LengthUnit::Meters,      "so is lowercase m");
    check(parseUnitSuffix("MM") == LengthUnit::Millimeters, "MM is millimeters");
    check(parseUnitSuffix("In") == LengthUnit::Inches,      "In is inches");
    check(!isKnownUnitSuffix("km"), "there is no kilometer unit, so no k prefix");
    check(!isKnownUnitSuffix("qq"), "and an unknown suffix is not silently mm");

    std::printf("\n%s (%d failure(s))\n", failures ? "FAILURES" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
