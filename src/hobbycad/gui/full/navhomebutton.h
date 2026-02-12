// =====================================================================
//  src/hobbycad/gui/full/navhomebutton.h — home button control
// =====================================================================
//
//  A clickable home icon below the ViewCube.  Rendered screen-fixed
//  via AIS_Canvas2D (Graphic3d_TMF_2d).
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_NAVHOMEBUTTON_H
#define HOBBYCAD_NAVHOMEBUTTON_H

#include "aiscanvas2d.h"
#include "navcontrols.h"

namespace hobbycad {

/// A clickable home-view icon positioned below the ViewCube.
class NavHomeButton : public AIS_Canvas2D {
    DEFINE_STANDARD_RTTI_INLINE(NavHomeButton, AIS_Canvas2D)

public:
    NavHomeButton();

protected:
    void onPaint() override;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_NAVHOMEBUTTON_H
