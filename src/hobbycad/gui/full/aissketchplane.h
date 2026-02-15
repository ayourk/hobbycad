// =====================================================================
//  src/hobbycad/gui/full/aissketchplane.h — Sketch plane visualization
// =====================================================================
//
//  Displays a semi-transparent rectangular plane in 3D space to
//  visualize the sketch plane orientation and position. The plane
//  has a visible border outline and supports custom angled orientations.
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_AISSKETCHPLANE_H
#define HOBBYCAD_AISSKETCHPLANE_H

#include <hobbycad/project.h>

#include <AIS_InteractiveObject.hxx>
#include <gp_Pln.hxx>
#include <gp_Trsf.hxx>
#include <Quantity_Color.hxx>

namespace hobbycad {

/// Visual representation of a sketch plane in 3D space.
/// Shows a semi-transparent rectangle with border outline.
class AisSketchPlane : public AIS_InteractiveObject {
public:
    /// Create a plane visualization.
    /// @param size Side length of the square plane (default: 200mm)
    AisSketchPlane(double size = 200.0);

    /// Set the plane from standard sketch plane type with offset.
    void setPlane(SketchPlane plane, double offset = 0.0);

    /// Set a custom angled plane.
    void setCustomPlane(PlaneRotationAxis axis, double angleDeg, double offset = 0.0);

    /// Set the plane fill color.
    void setFillColor(const Quantity_Color& color);

    /// Set the border color.
    void setBorderColor(const Quantity_Color& color);

    /// Set transparency (0.0 = opaque, 1.0 = fully transparent).
    void setPlaneTransparency(double alpha);

    /// Set the plane size.
    void setSize(double size);

    /// Get the current plane size.
    double size() const { return m_size; }

    DEFINE_STANDARD_RTTIEXT(AisSketchPlane, AIS_InteractiveObject)

protected:
    void Compute(const Handle(PrsMgr_PresentationManager)& thePrsMgr,
                 const Handle(Prs3d_Presentation)& thePrs,
                 const Standard_Integer theMode) override;

    void ComputeSelection(const Handle(SelectMgr_Selection)& theSel,
                          const Standard_Integer theMode) override;

private:
    void buildPlane(const Handle(Prs3d_Presentation)& prs);
    void updatePlaneGeometry();

    gp_Pln m_basePlane;          ///< The plane geometry
    gp_Trsf m_transform;         ///< Transform for custom angles
    double m_offset = 0.0;       ///< Offset along normal
    double m_size = 200.0;       ///< Side length of square plane
    Quantity_Color m_fillColor;  ///< Fill color (semi-transparent)
    Quantity_Color m_borderColor;///< Border outline color
    double m_transparency = 0.7; ///< Fill transparency
    bool m_useCustomTransform = false;
};

DEFINE_STANDARD_HANDLE(AisSketchPlane, AIS_InteractiveObject)

}  // namespace hobbycad

#endif  // HOBBYCAD_AISSKETCHPLANE_H
