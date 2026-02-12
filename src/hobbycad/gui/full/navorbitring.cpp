// =====================================================================
//  src/hobbycad/gui/full/navorbitring.cpp — single-axis orbit ring
// =====================================================================
//
//  Draws one colored arc section of the orbit ring.  The section is
//  split into two sub-arcs with a gap, and each sub-arc has an
//  inward-pointing arrow triangle pulled back from the gap edge
//  to create visible space between opposing arrow tips.
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include "navorbitring.h"

#include <cmath>

namespace {
    constexpr double kPi      = 3.14159265358979323846;
    constexpr double kDeg2Rad = kPi / 180.0;
}

namespace hobbycad {

// ---- Constructor ----------------------------------------------------

NavOrbitRing::NavOrbitRing(double startDeg, double sweepDeg,
                           NavControlId cwCtrl, NavControlId ccwCtrl,
                           const Quantity_Color& color,
                           double radius)
    : m_startDeg(startDeg)
    , m_sweepDeg(sweepDeg)
    , m_cwCtrl(cwCtrl)
    , m_ccwCtrl(ccwCtrl)
    , m_color(color)
    , m_radius(radius)
{
}

// ---- onPaint --------------------------------------------------------

void NavOrbitRing::onPaint()
{
    double halfSweep = m_sweepDeg / 2.0;
    double halfGap   = kMidGapDeg / 2.0;

    // Sub-arc 1 (CW side): startDeg → mid − halfGap
    double arc1Start = m_startDeg;
    double arc1End   = m_startDeg + halfSweep - halfGap;

    // Sub-arc 2 (CCW side): mid + halfGap → endDeg
    double arc2Start = m_startDeg + halfSweep + halfGap;
    double arc2End   = m_startDeg + m_sweepDeg;

    // Draw the two sub-arcs
    drawArc(0, 0, m_radius, arc1Start, arc1End - arc1Start,
            m_color, kLineWidth, 24);
    drawArc(0, 0, m_radius, arc2Start, arc2End - arc2Start,
            m_color, kLineWidth, 24);

    // Arrow at arc1 inner end — points CCW (toward gap center)
    paintArrow(arc1End, +1.0, m_cwCtrl);

    // Arrow at arc2 inner end — points CW (toward gap center)
    paintArrow(arc2Start, -1.0, m_ccwCtrl);
}

// ---- paintArrow -----------------------------------------------------

void NavOrbitRing::paintArrow(double angleDeg, double tangentSign,
                               NavControlId ctrl)
{
    double rad = angleDeg * kDeg2Rad;

    // Point on the ring
    double px = m_radius * std::cos(rad);
    double py = m_radius * std::sin(rad);

    // Tangent direction (CCW positive)
    double tx = -std::sin(rad);
    double ty =  std::cos(rad);

    // Radial outward direction
    double nx = std::cos(rad);
    double ny = std::sin(rad);

    // Pull the base back from the gap edge to create visible space
    // between opposing arrow tips.
    double bx = px - kBackoff * tangentSign * tx;
    double by = py - kBackoff * tangentSign * ty;

    // Triangle: tip extends forward along tangent, base straddles arc.
    double tipX = bx + kArrowLen * tangentSign * tx;
    double tipY = by + kArrowLen * tangentSign * ty;

    double b1X = bx + kArrowHalf * nx;
    double b1Y = by + kArrowHalf * ny;
    double b2X = bx - kArrowHalf * nx;
    double b2Y = by - kArrowHalf * ny;

    drawFilledTriangle(tipX, tipY, b1X, b1Y, b2X, b2Y, m_color);

    // Sensitive region — padded rectangle around the arrow center.
    double cx = (tipX + b1X + b2X) / 3.0;
    double cy = (tipY + b1Y + b2Y) / 3.0;
    double pad = kArrowLen * 0.8;

    std::vector<std::pair<double, double>> poly = {
        { cx + pad * tangentSign * tx + pad * nx,
          cy + pad * tangentSign * ty + pad * ny },
        { cx + pad * tangentSign * tx - pad * nx,
          cy + pad * tangentSign * ty - pad * ny },
        { cx - pad * tangentSign * tx - pad * nx,
          cy - pad * tangentSign * ty - pad * ny },
        { cx - pad * tangentSign * tx + pad * nx,
          cy - pad * tangentSign * ty + pad * ny },
    };

    addSensitivePoly(new NavControlOwner(this, ctrl), poly);
}

}  // namespace hobbycad
