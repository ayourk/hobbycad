// =====================================================================
//  src/hobbycad/gui/full/scalebarwidget.cpp — 2D scale bar overlay
// =====================================================================
//
//  Renders a horizontal scale bar with ticks and labels at the
//  bottom-left of the viewport using AIS_Canvas2D primitives.
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include "scalebarwidget.h"

#include <cmath>
#include <cstdio>

namespace {
    // Layout constants (in pixel units).
    constexpr double kFontHeight = 18.0;
    constexpr double kTickH      = 12.0;   // end tick total height
    constexpr double kLineWidth  =  2.0;
    constexpr double kTextGap    =  6.0;   // gap between text and tick
    constexpr int    kMaxBarPx   = 180;     // max bar pixel length
    constexpr int    kTargetPx   =  75;     // target bar pixel length

    // Colors.
    Quantity_Color barColor() {
        return Quantity_Color(0.0, 0.0, 0.0, Quantity_TOC_RGB);
    }
    Quantity_Color shadowColor() {
        return Quantity_Color(0.0, 0.0, 0.0, Quantity_TOC_RGB);
    }
}

namespace hobbycad {

// ---- Constructor ----------------------------------------------------

ScaleBarWidget::ScaleBarWidget()
    : AIS_Canvas2D(Aspect_TOTP_LEFT_LOWER, 20, 20)
{
}

// ---- setUnitSystem --------------------------------------------------

void ScaleBarWidget::setUnitSystem(UnitSystem units)
{
    m_unitSystem = units;
}

// ---- updateScale ----------------------------------------------------

void ScaleBarWidget::updateScale()
{
    if (m_view.IsNull()) return;

    double worldPerPixel = m_view->Convert(1);  // mm per pixel
    if (worldPerPixel <= 0.0) return;

    double rawWorld = worldPerPixel * kTargetPx;

    // Snap to a "nice" round number.
    m_worldLength = niceNumber(rawWorld);
    m_pixelLength = m_worldLength / worldPerPixel;

    // If bar would be too wide, step down.
    while (m_pixelLength > kMaxBarPx && m_worldLength > 0.001) {
        m_worldLength = niceNumberBelow(m_worldLength);
        m_pixelLength = m_worldLength / worldPerPixel;
    }

    // Floor at minimum visible size.
    if (m_pixelLength < 20.0) m_pixelLength = 20.0;

    buildLabel();
}

// ---- buildLabel -----------------------------------------------------

void ScaleBarWidget::buildLabel()
{
    char buf[64];

    // Convert internal mm to display units and format
    double value = m_worldLength;
    const char* unitStr = "mm";

    switch (m_unitSystem) {
    case UnitSystem::Millimeters:
        // Already in mm, but show m for large values
        if (value >= 1000.0) {
            value /= 1000.0;
            unitStr = "m";
        } else if (value < 1.0) {
            value *= 1000.0;
            unitStr = "um";
        }
        break;
    case UnitSystem::Centimeters:
        value /= 10.0;  // mm to cm
        unitStr = "cm";
        if (value >= 100.0) {
            value /= 100.0;
            unitStr = "m";
        }
        break;
    case UnitSystem::Meters:
        value /= 1000.0;  // mm to m
        unitStr = "m";
        if (value < 0.01) {
            value *= 100.0;
            unitStr = "cm";
        }
        break;
    case UnitSystem::Inches:
        value /= 25.4;  // mm to inches
        unitStr = "in";
        if (value >= 12.0) {
            value /= 12.0;
            unitStr = "ft";
        }
        break;
    case UnitSystem::Feet:
        value /= 304.8;  // mm to feet
        unitStr = "ft";
        if (value < 1.0) {
            value *= 12.0;
            unitStr = "in";
        }
        break;
    }

    // Format the value
    if (value == std::floor(value) && value < 10000.0)
        std::snprintf(buf, sizeof(buf), "%d %s",
                      static_cast<int>(value), unitStr);
    else
        std::snprintf(buf, sizeof(buf), "%.3g %s", value, unitStr);

    m_label = buf;
}

// ---- onPaint --------------------------------------------------------
//
// Layout (left to right):
//
//     "0"  [gap]  |---bar---|  [gap]  "label"
//
// Origin (0,0) is the anchor point.  The bar is drawn to the right
// of the "0" label.  Y = 0 is the bar center line.

void ScaleBarWidget::onPaint()
{
    Quantity_Color bar = barColor();

    double zeroW = estimateTextWidth("0", kFontHeight);

    // Bar horizontal extents.
    double x0 = zeroW + kTextGap;             // bar left edge
    double x1 = x0 + m_pixelLength;           // bar right edge
    double xMid = (x0 + x1) / 2.0;

    double barY = 0.0;

    // ---- Horizontal bar ----
    drawLine(x0, barY, x1, barY, bar, kLineWidth);

    // ---- End ticks ----
    drawLine(x0, barY - kTickH / 2.0, x0, barY + kTickH / 2.0,
             bar, kLineWidth);
    drawLine(x1, barY - kTickH / 2.0, x1, barY + kTickH / 2.0,
             bar, kLineWidth);

    // ---- Midpoint tick (shorter) ----
    drawLine(xMid, barY - kTickH / 4.0, xMid, barY + kTickH / 4.0,
             bar, kLineWidth);

    // ---- "0" label (left of bar) ----
    // OCCT renders text with Y as the baseline.  Position so the
    // glyph center aligns with the bar.  Baseline ≈ center − 0.35×h.
    double textY = barY - kFontHeight * 0.35;
    drawText(0, textY, "0", bar, kFontHeight);

    // ---- Value label (right of bar) ----
    drawText(x1 + kTextGap, textY, m_label.c_str(), bar, kFontHeight);
}

// ---- niceNumber -----------------------------------------------------

double ScaleBarWidget::niceNumber(double value) const
{
    double exponent = std::floor(std::log10(value));
    double fraction = value / std::pow(10.0, exponent);

    double nice;
    if (fraction < 1.5)
        nice = 1.0;
    else if (fraction < 3.5)
        nice = 2.0;
    else if (fraction < 7.5)
        nice = 5.0;
    else
        nice = 10.0;

    return nice * std::pow(10.0, exponent);
}

// ---- niceNumberBelow ------------------------------------------------

double ScaleBarWidget::niceNumberBelow(double value) const
{
    double exponent = std::floor(std::log10(value));
    double fraction = value / std::pow(10.0, exponent);

    double nice;
    if (fraction > 5.5)
        nice = 5.0;
    else if (fraction > 2.5)
        nice = 2.0;
    else if (fraction > 1.5)
        nice = 1.0;
    else {
        nice = 5.0;
        exponent -= 1.0;
    }

    return nice * std::pow(10.0, exponent);
}

}  // namespace hobbycad
