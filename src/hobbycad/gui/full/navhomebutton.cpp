// =====================================================================
//  src/hobbycad/gui/full/navhomebutton.cpp — home button control
// =====================================================================
//
//  Draws a small house-shaped icon below and to the left of the
//  ViewCube using AIS_Canvas2D 2D drawing primitives.
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include "navhomebutton.h"

#include <cmath>

namespace {
    // Home icon position — at the vertex of a right angle where:
    //   vertical line up → tangent to green (Y) arrows
    //   horizontal line right → tangent to red (X) arrows
    constexpr double kHomeX = -61.0;
    constexpr double kHomeY = -57.0;

    // Geometry: 1/2 of SVG original.
    constexpr double kHalfW   = 10.0;     // half body width
    constexpr double kBodyH   = 16.0;     // body height
    constexpr double kRoofH   =  7.675;   // roof height
    constexpr double kDoorW   =  7.0;     // door width
    constexpr double kDoorH   = 10.0;     // door height
    constexpr double kHang    =  2.0;     // eave overhang
    constexpr double kRoofLW  =  2.5;     // roof line stroke width
    constexpr double kDoorOLW =  0.75;    // door outline width
}

namespace hobbycad {

NavHomeButton::NavHomeButton()
    : AIS_Canvas2D()  // same corner/offset as ViewCube
{
}

void NavHomeButton::onPaint()
{
    // Center the icon vertically on kHomeY.
    // Total height = kBodyH + kRoofH = 47.35.
    double totalH = kBodyH + kRoofH;
    double botY   = kHomeY - totalH / 2.0;    // body bottom
    double eaveY  = botY + kBodyH;             // eave line (body top)
    double peakY  = eaveY + kRoofH;            // roof peak

    double left   = kHomeX - kHalfW;
    double right  = kHomeX + kHalfW;

    Quantity_Color white(Quantity_NOC_WHITE);
    Quantity_Color red(1.0, 0.2, 0.2, Quantity_TOC_RGB);
    Quantity_Color doorCol(0.855, 0.647, 0.125, Quantity_TOC_RGB); // #DAA520
    Quantity_Color black(0.0, 0.0, 0.0, Quantity_TOC_RGB);

    // ---- Rectangle body (two triangles) ----
    drawFilledTriangle(left, botY, right, botY, right, eaveY, white);
    drawFilledTriangle(left, botY, right, eaveY, left, eaveY, white);

    // ---- Triangle roof ----
    drawFilledTriangle(left, eaveY, right, eaveY, kHomeX, peakY, white);

    // ---- Door (filled rectangle, outline on 3 sides) ----
    double doorL = kHomeX - kDoorW / 2.0;
    double doorR = kHomeX + kDoorW / 2.0;
    double doorB = botY;
    double doorT = botY + kDoorH;

    // Fill
    drawFilledTriangle(doorL, doorB, doorR, doorB, doorR, doorT, doorCol);
    drawFilledTriangle(doorL, doorB, doorR, doorT, doorL, doorT, doorCol);

    // Outline: left, top, right (no bottom).
    // Right side extended 0.5px to cover sub-pixel corner gap.
    drawLine(doorL, doorB, doorL, doorT, black, kDoorOLW);
    drawLine(doorL, doorT, doorR, doorT, black, kDoorOLW);
    drawLine(doorR, doorT, doorR, doorB - 0.5, black, kDoorOLW);

    // ---- Red roof lines: eave overhang, meet at peak ----
    // Left edge unit: from (left,eaveY) → (kHomeX,peakY)
    double ldx = kHomeX - left;
    double ldy = peakY - eaveY;
    double llen = std::sqrt(ldx * ldx + ldy * ldy);
    double lux = ldx / llen;
    double luy = ldy / llen;

    drawLine(left - kHang * lux, eaveY - kHang * luy,
             kHomeX, peakY,
             red, kRoofLW);

    // Right edge unit: from (kHomeX,peakY) → (right,eaveY)
    double rdx = right - kHomeX;
    double rdy = eaveY - peakY;
    double rlen = std::sqrt(rdx * rdx + rdy * rdy);
    double rux = rdx / rlen;
    double ruy = rdy / rlen;

    drawLine(kHomeX, peakY,
             right + kHang * rux, eaveY + kHang * ruy,
             red, kRoofLW);

    // ---- Sensitive click region ----
    double pad = kHalfW + kHang + 4.0;
    std::vector<std::pair<double, double>> poly = {
        { kHomeX - pad, botY - 2.0 },
        { kHomeX + pad, botY - 2.0 },
        { kHomeX + pad, peakY + 4.0 },
        { kHomeX - pad, peakY + 4.0 },
    };

    addSensitivePoly(new NavControlOwner(this, NavCtrl_Home), poly);
}

}  // namespace hobbycad
