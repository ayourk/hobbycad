// =====================================================================
//  src/hobbycad/gui/full/navorbitring.h — single-axis orbit ring
// =====================================================================
//
//  A Fusion 360-style orbit arc for one axis.  Three instances (X, Y,
//  Z) form the complete orbit ring around the ViewCube.
//
//  Each instance draws a colored arc section split into two sub-arcs
//  with a gap, and two inward-pointing arrow triangles at the gap.
//  Only the triangles are clickable; the arc lines pass clicks
//  through to the ViewCube.
//
//  Each instance is a separate AIS object so hover-highlighting
//  works per-axis automatically.
//
//  Rendered flat and screen-fixed via AIS_Canvas2D (Graphic3d_TMF_2d),
//  so it does NOT rotate with the camera.
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_NAVORBITRING_H
#define HOBBYCAD_NAVORBITRING_H

#include "aiscanvas2d.h"
#include "navcontrols.h"

namespace hobbycad {

/// One axis section of the orbit ring.
///
/// @param startDeg   Arc start angle (degrees, CCW from east).
/// @param sweepDeg   Total arc sweep in degrees.
/// @param cwCtrl     NavControlId for the CW-direction arrow.
/// @param ccwCtrl    NavControlId for the CCW-direction arrow.
/// @param color      Arc and arrow fill color.
/// @param radius     Orbit ring radius in pixel units.
class NavOrbitRing : public AIS_Canvas2D {
    DEFINE_STANDARD_RTTI_INLINE(NavOrbitRing, AIS_Canvas2D)

public:
    NavOrbitRing(double startDeg, double sweepDeg,
                 NavControlId cwCtrl, NavControlId ccwCtrl,
                 const Quantity_Color& color,
                 double radius = 55.0);

    /// Change the orbit ring radius.
    void SetRadius(double theRadius) { m_radius = theRadius; }

    /// Get the current radius.
    double Radius() const { return m_radius; }

protected:
    void onPaint() override;

private:
    /// Draw an arrow triangle at a point on the ring.
    /// @param angleDeg     Position on the ring.
    /// @param tangentSign  +1 = points CCW, −1 = points CW.
    /// @param ctrl         NavControlId for the sensitive region.
    void paintArrow(double angleDeg, double tangentSign,
                    NavControlId ctrl);

    double         m_startDeg;
    double         m_sweepDeg;
    NavControlId   m_cwCtrl;
    NavControlId   m_ccwCtrl;
    Quantity_Color m_color;
    double         m_radius;

    static constexpr double kArrowLen  = 10.0;  ///< tip-to-base
    static constexpr double kArrowHalf =  5.0;  ///< half-width
    static constexpr double kBackoff   =  3.0;  ///< gap between arrows
    static constexpr double kMidGapDeg = 20.0;  ///< gap between sub-arcs
    static constexpr double kLineWidth =  2.5;  ///< arc line thickness
};

}  // namespace hobbycad

#endif  // HOBBYCAD_NAVORBITRING_H
