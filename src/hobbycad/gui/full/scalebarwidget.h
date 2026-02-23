// =====================================================================
//  src/hobbycad/gui/full/scalebarwidget.h — 2D scale bar overlay
// =====================================================================
//
//  A screen-fixed scale bar rendered via AIS_Canvas2D in the bottom-
//  left corner of the viewport.  Shows a horizontal bar with ticks
//  and unit labels that update with camera zoom.
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_SCALEBARWIDGET_H
#define HOBBYCAD_SCALEBARWIDGET_H

#include "aiscanvas2d.h"

#include <hobbycad/units.h>

#include <V3d_View.hxx>

#include <string>

namespace hobbycad {

// UnitSystem was a duplicate of LengthUnit — now uses LengthUnit from units.h

class ScaleBarWidget : public AIS_Canvas2D {
    DEFINE_STANDARD_RTTI_INLINE(ScaleBarWidget, AIS_Canvas2D)

public:
    ScaleBarWidget();

    /// Set the V3d_View used to compute world-space scale.
    void setView(const Handle(V3d_View)& view) { m_view = view; }

    /// Set the display unit.
    void setUnitSystem(LengthUnit units);

    /// Get the current display unit.
    LengthUnit unitSystem() const { return m_unitSystem; }

    /// Recompute the scale bar dimensions from current zoom level.
    /// Call this after zoom/pan/resize, then Redisplay the object.
    void updateScale();

protected:
    void onPaint() override;

private:
    /// Build the unit label string from m_worldLength.
    void buildLabel();

    Handle(V3d_View) m_view;
    double      m_worldLength = 100.0;   // world-space length (in base units)
    double      m_pixelLength = 100.0;   // screen-space length (px)
    std::string m_label;                 // e.g. "100 mm"
    LengthUnit  m_unitSystem = LengthUnit::Millimeters;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_SCALEBARWIDGET_H
