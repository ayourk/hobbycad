// =====================================================================
//  src/hobbycad/gui/full/aisgrid.h — Custom AIS grid overlay
// =====================================================================
//
//  A world-space grid drawn as an AIS_InteractiveObject. Unlike the
//  V3d_Viewer built-in grid, this can be marked as infinite to exclude
//  it from FitAll bounding box calculations.
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_AISGRID_H
#define HOBBYCAD_AISGRID_H

#include <AIS_InteractiveObject.hxx>
#include <Quantity_Color.hxx>

namespace hobbycad {

/// A rectangular grid on the XY plane (Z=0) rendered as line segments.
/// Can be marked infinite to exclude from FitAll calculations.
class AisGrid : public AIS_InteractiveObject {
public:
    /// Create a grid with given extent and spacing.
    /// @param extent     Half-size of the grid (grid spans -extent to +extent)
    /// @param minorStep  Spacing between minor grid lines
    /// @param majorStep  Spacing between major (emphasized) grid lines
    AisGrid(Standard_Real extent = 100.0,
            Standard_Real minorStep = 10.0,
            Standard_Real majorStep = 100.0);

    /// Set the minor grid line color.
    void SetMinorColor(const Quantity_Color& color);

    /// Set the major grid line color.
    void SetMajorColor(const Quantity_Color& color);

    /// Set the grid extent (half-size).
    void SetExtent(Standard_Real extent);

    /// Set the minor line spacing.
    void SetMinorStep(Standard_Real step);

    /// Set the major line spacing.
    void SetMajorStep(Standard_Real step);

    DEFINE_STANDARD_RTTIEXT(AisGrid, AIS_InteractiveObject)

protected:
    void Compute(const Handle(PrsMgr_PresentationManager)& thePrsMgr,
                 const Handle(Prs3d_Presentation)& thePrs,
                 const Standard_Integer theMode) override;

    void ComputeSelection(const Handle(SelectMgr_Selection)& theSel,
                          const Standard_Integer theMode) override;

private:
    void buildGrid(const Handle(Prs3d_Presentation)& prs);

    Standard_Real   m_extent;
    Standard_Real   m_minorStep;
    Standard_Real   m_majorStep;
    Quantity_Color  m_minorColor;
    Quantity_Color  m_majorColor;
};

DEFINE_STANDARD_HANDLE(AisGrid, AIS_InteractiveObject)

}  // namespace hobbycad

#endif  // HOBBYCAD_AISGRID_H
