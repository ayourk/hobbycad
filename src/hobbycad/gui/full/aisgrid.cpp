// =====================================================================
//  src/hobbycad/gui/full/aisgrid.cpp — Custom AIS grid overlay
// =====================================================================
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include "aisgrid.h"

#include <Graphic3d_ArrayOfSegments.hxx>
#include <Graphic3d_AspectLine3d.hxx>
#include <Graphic3d_Group.hxx>
#include <Prs3d_LineAspect.hxx>
#include <Prs3d_Presentation.hxx>

#include <cmath>

namespace hobbycad {

IMPLEMENT_STANDARD_RTTIEXT(AisGrid, AIS_InteractiveObject)

AisGrid::AisGrid(Standard_Real extent,
                 Standard_Real minorStep,
                 Standard_Real majorStep)
    : m_extent(extent)
    , m_minorStep(minorStep)
    , m_majorStep(majorStep)
    , m_minorColor(0.35, 0.38, 0.42, Quantity_TOC_RGB)
    , m_majorColor(0.50, 0.53, 0.58, Quantity_TOC_RGB)
{
    // Mark as infinite so FitAll ignores this object's bounding box
    SetInfiniteState(Standard_True);
}

void AisGrid::SetMinorColor(const Quantity_Color& color)
{
    m_minorColor = color;
}

void AisGrid::SetMajorColor(const Quantity_Color& color)
{
    m_majorColor = color;
}

void AisGrid::SetExtent(Standard_Real extent)
{
    m_extent = extent;
}

void AisGrid::SetMinorStep(Standard_Real step)
{
    m_minorStep = step;
}

void AisGrid::SetMajorStep(Standard_Real step)
{
    m_majorStep = step;
}

void AisGrid::Compute(const Handle(PrsMgr_PresentationManager)& /*thePrsMgr*/,
                      const Handle(Prs3d_Presentation)& thePrs,
                      const Standard_Integer /*theMode*/)
{
    buildGrid(thePrs);
}

void AisGrid::ComputeSelection(const Handle(SelectMgr_Selection)& /*theSel*/,
                               const Standard_Integer /*theMode*/)
{
    // Grid is not selectable — no selection primitives
}

void AisGrid::buildGrid(const Handle(Prs3d_Presentation)& prs)
{
    if (m_minorStep <= 0.0 || m_extent <= 0.0) return;

    // Count lines needed
    int numMinorLines = static_cast<int>(std::ceil(m_extent / m_minorStep));

    // Build minor grid lines (both X and Y directions)
    // Each direction: lines from -extent to +extent at each step
    // We need 2 * (2 * numMinorLines + 1) lines (both +/- and center)
    // Each line has 2 vertices

    // Count minor lines (excluding those that fall on major lines)
    int minorCount = 0;
    int majorCount = 0;

    for (int i = -numMinorLines; i <= numMinorLines; ++i) {
        Standard_Real pos = i * m_minorStep;
        bool isMajor = (m_majorStep > 0.0) &&
                       (std::fabs(std::fmod(pos, m_majorStep)) < 0.001 ||
                        std::fabs(std::fmod(pos, m_majorStep) - m_majorStep) < 0.001);
        if (isMajor) {
            majorCount += 2;  // X and Y direction lines
        } else {
            minorCount += 2;
        }
    }

    // Create minor lines group
    if (minorCount > 0) {
        Handle(Graphic3d_Group) minorGroup = prs->NewGroup();
        Handle(Graphic3d_AspectLine3d) minorAspect =
            new Graphic3d_AspectLine3d(m_minorColor, Aspect_TOL_SOLID, 1.0);
        minorGroup->SetPrimitivesAspect(minorAspect);

        Handle(Graphic3d_ArrayOfSegments) minorSegs =
            new Graphic3d_ArrayOfSegments(minorCount * 2);

        for (int i = -numMinorLines; i <= numMinorLines; ++i) {
            Standard_Real pos = i * m_minorStep;
            bool isMajor = (m_majorStep > 0.0) &&
                           (std::fabs(std::fmod(pos, m_majorStep)) < 0.001 ||
                            std::fabs(std::fmod(pos, m_majorStep) - m_majorStep) < 0.001);
            if (isMajor) continue;

            // Line parallel to Y axis at X = pos (on XY plane, Z=0)
            minorSegs->AddVertex(gp_Pnt(pos, -m_extent, 0.0));
            minorSegs->AddVertex(gp_Pnt(pos,  m_extent, 0.0));

            // Line parallel to X axis at Y = pos
            minorSegs->AddVertex(gp_Pnt(-m_extent, pos, 0.0));
            minorSegs->AddVertex(gp_Pnt( m_extent, pos, 0.0));
        }

        minorGroup->AddPrimitiveArray(minorSegs);
    }

    // Create major lines group
    if (majorCount > 0) {
        Handle(Graphic3d_Group) majorGroup = prs->NewGroup();
        Handle(Graphic3d_AspectLine3d) majorAspect =
            new Graphic3d_AspectLine3d(m_majorColor, Aspect_TOL_SOLID, 1.5);
        majorGroup->SetPrimitivesAspect(majorAspect);

        Handle(Graphic3d_ArrayOfSegments) majorSegs =
            new Graphic3d_ArrayOfSegments(majorCount * 2);

        for (int i = -numMinorLines; i <= numMinorLines; ++i) {
            Standard_Real pos = i * m_minorStep;
            bool isMajor = (m_majorStep > 0.0) &&
                           (std::fabs(std::fmod(pos, m_majorStep)) < 0.001 ||
                            std::fabs(std::fmod(pos, m_majorStep) - m_majorStep) < 0.001);
            if (!isMajor) continue;

            // Line parallel to Y axis at X = pos
            majorSegs->AddVertex(gp_Pnt(pos, -m_extent, 0.0));
            majorSegs->AddVertex(gp_Pnt(pos,  m_extent, 0.0));

            // Line parallel to X axis at Y = pos
            majorSegs->AddVertex(gp_Pnt(-m_extent, pos, 0.0));
            majorSegs->AddVertex(gp_Pnt( m_extent, pos, 0.0));
        }

        majorGroup->AddPrimitiveArray(majorSegs);
    }
}

}  // namespace hobbycad
