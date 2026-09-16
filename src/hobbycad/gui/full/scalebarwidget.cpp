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

#include <hobbycad/units.h>   // kScaleBar*Px (shared with the 2D bar)

namespace {
    // Layout constants (in pixel units).
    constexpr double kFontHeight = 18.0;
    constexpr double kTickH      = 12.0;   // end tick total height
    constexpr double kLineWidth  =  2.0;
    constexpr double kTextGap    =  6.0;   // gap between text and tick

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

void ScaleBarWidget::setUnitSystem(LengthUnit units)
{
    m_unitSystem = units;
}

// ---- updateScale ----------------------------------------------------

void ScaleBarWidget::updateScale()
{
    if (m_view.IsNull()) return;

    double worldPerPixel = m_view->Convert(1);  // mm per pixel
    if (worldPerPixel <= 0.0) return;

    double rawWorld = worldPerPixel * hobbycad::kScaleBarTargetPx;

    // Snap to a "nice" round number.
    m_worldLength = niceNumber(rawWorld);
    m_pixelLength = m_worldLength / worldPerPixel;

    // If bar would be too wide, step down.
    while (m_pixelLength > hobbycad::kScaleBarMaxPx && m_worldLength > 0.001) {
        m_worldLength = niceNumberBelow(m_worldLength);
        m_pixelLength = m_worldLength / worldPerPixel;
    }

    // Floor at minimum visible size.
    if (m_pixelLength < hobbycad::kScaleBarMinPx) m_pixelLength = hobbycad::kScaleBarMinPx;

    buildLabel();
}

// ---- buildLabel -----------------------------------------------------

void ScaleBarWidget::buildLabel()
{
    // Shared with the 2D sketch scale bar (units.h) so both read identically.
    m_label = hobbycad::formatScaleBarLabel(m_worldLength, m_unitSystem);
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

}  // namespace hobbycad
