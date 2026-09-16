// =====================================================================
//  tests/theme/theme.cpp — SketchTheme light() is the historical palette,
//  dark() is a genuine dark variant
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  Two things must hold or the theme refactor silently changed appearance:
//    * light() reproduces the exact QColor literals the renderers used
//      before there was a palette (a regression guard: if someone edits a
//      light value by mistake, this fails),
//    * dark() is actually dark and actually different (a dark ground, a
//      light fully-constrained color, and distinct geometry hues), so the
//      dark theme is not accidentally a copy of light.
#include <cstdio>
#include "sketchtheme.h"

using hobbycad::SketchTheme;
static int fails = 0;
static void ck(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++fails;
}
static double lum(const QColor& c) {
    return (0.299 * c.red() + 0.587 * c.green() + 0.114 * c.blue()) / 255.0;
}

int main() {
    const SketchTheme L = SketchTheme::light();
    const SketchTheme D = SketchTheme::dark();

    // ---- light() == the historical literals (regression guard) ---------
    ck(L.background        == QColor(240, 240, 240), "light background 240");
    ck(L.grid              == QColor(200, 200, 200), "light grid 200");
    ck(L.normalGeometry    == QColor(0, 120, 215),   "light normal blue");
    ck(L.fullyConstrained  == QColor(0, 0, 0),       "light fully-constrained black");
    ck(L.inconsistent      == QColor(200, 0, 0),     "light inconsistent red");
    ck(L.projectedGeometry == QColor(148, 90, 200),  "light projected violet");
    ck(L.constructionNormal== QColor(180, 100, 50),  "light construction");
    ck(L.constructionSelected == QColor(255, 140, 0),"light construction selected");
    ck(L.centerlineNormal  == QColor(70, 150, 160),  "light centerline");
    ck(L.snap              == QColor(255, 140, 0),   "light snap orange");
    ck(L.inference         == QColor(0, 160, 90),    "light inference green");
    ck(L.selectionFallback == QColor(90, 90, 90),    "light selection fallback");
    ck(L.labelText         == QColor(0, 0, 0),       "light label text black");
    ck(L.labelFill         == QColor(255, 255, 255), "light label fill white");
    ck(L.axisX             == QColor(255, 0, 0),     "light X axis red");
    ck(L.axisZ             == QColor(0, 0, 255),     "light Z axis blue");
    ck(L.pivotFill         == QColor(255, 200, 0),   "light pivot fill");

    // ---- dark() is genuinely dark and genuinely different --------------
    ck(lum(D.background) < 0.25,                 "dark background is dark");
    ck(lum(D.background) < lum(L.background),    "dark bg darker than light bg");
    ck(lum(D.fullyConstrained) > 0.7,            "dark fully-constrained is light");
    ck(lum(D.labelFill) < 0.4,                   "dark label fill is dark");
    ck(lum(D.labelText) > 0.7,                   "dark label text is light");
    ck(lum(D.origin) > 0.7,                      "dark origin dot is light (visible)");
    ck(lum(D.pointDotFill) < 0.4,               "dark point dot fill is dark ground");
    ck(D.normalGeometry    != L.normalGeometry,  "dark normal differs from light");
    ck(D.projectedGeometry != L.projectedGeometry, "dark projected differs");
    ck(D.background        != L.background,       "dark bg differs");
    // brightness lift: the driving blue should be lighter on dark
    ck(lum(D.normalGeometry) > lum(L.normalGeometry), "dark normal is brighter blue");

    if (fails == 0) std::printf("theme: ALL PASS\n");
    else            std::printf("theme: %d FAILURE(S)\n", fails);
    return fails ? 1 : 0;
}
