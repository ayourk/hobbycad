// =====================================================================
//  src/hobbycad/gui/full/aiscanvas2d.cpp — 2D drawing canvas for OCCT
// =====================================================================
//
//  Translates 2D drawing commands into OCCT Graphic3d primitives
//  rendered in Graphic3d_TMF_2d screen-space.
//
//  TMF_2d anchors geometry at a pixel position (from viewport center)
//  and does NOT apply camera rotation — making it truly screen-fixed.
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include "aiscanvas2d.h"

#include <Graphic3d_ArrayOfPolylines.hxx>
#include <Graphic3d_ArrayOfTriangles.hxx>
#include <Graphic3d_AspectFillArea3d.hxx>
#include <Graphic3d_AspectLine3d.hxx>
#include <Graphic3d_AspectText3d.hxx>
#include <Graphic3d_Group.hxx>
#include <Graphic3d_Text.hxx>
#include <Graphic3d_TransformPers.hxx>
#include <Prs3d_Presentation.hxx>
#include <PrsMgr_PresentationManager.hxx>
#include <Select3D_SensitiveFace.hxx>
#include <SelectMgr_Selection.hxx>
#include <TColgp_Array1OfPnt.hxx>

#include <cmath>

namespace {
    constexpr double kPi      = 3.14159265358979323846;
    constexpr double kDeg2Rad = kPi / 180.0;
    constexpr double kTau     = 2.0 * kPi;
}

namespace hobbycad {

// ---- Constructor ----------------------------------------------------

AIS_Canvas2D::AIS_Canvas2D(Aspect_TypeOfTriedronPosition theCorner,
                            int theOffsetX, int theOffsetY)
    : m_corner(theCorner)
    , m_offsetX(theOffsetX)
    , m_offsetY(theOffsetY)
{
    // TMF_2d uses the same corner + offset API as TMF_TriedronPers,
    // but does NOT apply camera rotation — geometry stays screen-fixed.
    SetTransformPersistence(
        new Graphic3d_TransformPers(Graphic3d_TMF_2d,
                                    theCorner,
                                    Graphic3d_Vec2i(theOffsetX,
                                                    theOffsetY)));
}

// ---- Coordinate conversion ------------------------------------------

gp_Pnt AIS_Canvas2D::to3D(double x, double y) const
{
    // TMF_2d: local coordinates are pixel offsets from anchor.
    // X = right, Y = up, Z = depth ordering.
    return gp_Pnt(x, y, 0.0);
}

// ---- Drawing primitives ---------------------------------------------

void AIS_Canvas2D::drawArc(double cx, double cy, double radius,
                            double startDeg, double sweepDeg,
                            const Quantity_Color& color, double lineWidth,
                            int segments)
{
    m_arcs.push_back({cx, cy, radius,
                      startDeg * kDeg2Rad, sweepDeg * kDeg2Rad,
                      color, lineWidth, segments});
}

void AIS_Canvas2D::drawLine(double x1, double y1,
                             double x2, double y2,
                             const Quantity_Color& color, double lineWidth)
{
    m_lines.push_back({x1, y1, x2, y2, color, lineWidth});
}

void AIS_Canvas2D::drawFilledTriangle(double x1, double y1,
                                       double x2, double y2,
                                       double x3, double y3,
                                       const Quantity_Color& color)
{
    m_tris.push_back({x1, y1, x2, y2, x3, y3, color});
}

void AIS_Canvas2D::drawFilledCircle(double cx, double cy, double radius,
                                     const Quantity_Color& color,
                                     int segments)
{
    m_circles.push_back({cx, cy, radius, color, segments});
}

void AIS_Canvas2D::addSensitivePoly(
    const Handle(SelectMgr_EntityOwner)& owner,
    const std::vector<std::pair<double, double>>& poly2d)
{
    SensitiveCmd cmd;
    cmd.owner = owner;
    cmd.pts3d.reserve(poly2d.size());
    for (const auto& [x, y] : poly2d)
        cmd.pts3d.push_back(to3D(x, y));
    m_sensitives.push_back(std::move(cmd));
}

void AIS_Canvas2D::drawText(double x, double y,
                             const char* text,
                             const Quantity_Color& color,
                             double height,
                             const char* font)
{
    m_texts.push_back({x, y, std::string(text), color, height,
                       std::string(font)});
}

double AIS_Canvas2D::estimateTextWidth(const char* text, double height)
{
    // Approximate: each character is ~0.6 × height wide.
    int len = 0;
    for (const char* p = text; *p; ++p) ++len;
    return len * height * 0.6;
}

// ---- clearPrimitives ------------------------------------------------

void AIS_Canvas2D::clearPrimitives()
{
    m_arcs.clear();
    m_lines.clear();
    m_tris.clear();
    m_circles.clear();
    m_texts.clear();
    m_sensitives.clear();
}

// ---- Compute --------------------------------------------------------

void AIS_Canvas2D::Compute(
    const Handle(PrsMgr_PresentationManager)& /*thePM*/,
    const Handle(Prs3d_Presentation)& thePrs,
    const Standard_Integer /*theMode*/)
{
    clearPrimitives();
    onPaint();
    renderVisuals(thePrs);
}

// ---- ComputeSelection -----------------------------------------------

void AIS_Canvas2D::ComputeSelection(
    const Handle(SelectMgr_Selection)& theSel,
    const Standard_Integer /*theMode*/)
{
    clearPrimitives();
    onPaint();
    renderSensitives(theSel);
}

// ---- renderVisuals --------------------------------------------------

void AIS_Canvas2D::renderVisuals(const Handle(Prs3d_Presentation)& thePrs)
{
    // Arcs (polylines)
    for (const auto& a : m_arcs) {
        Handle(Graphic3d_Group) grp = thePrs->NewGroup();
        Handle(Graphic3d_AspectLine3d) asp =
            new Graphic3d_AspectLine3d(
                a.color, Aspect_TOL_SOLID,
                static_cast<Standard_Real>(a.lineWidth));
        grp->SetPrimitivesAspect(asp);

        int n = a.segments;
        Handle(Graphic3d_ArrayOfPolylines) poly =
            new Graphic3d_ArrayOfPolylines(n + 1);
        for (int i = 0; i <= n; ++i) {
            double t = a.startRad
                     + a.sweepRad * static_cast<double>(i) / n;
            double px = a.cx + a.radius * std::cos(t);
            double py = a.cy + a.radius * std::sin(t);
            poly->AddVertex(to3D(px, py));
        }
        grp->AddPrimitiveArray(poly);
    }

    // Lines
    for (const auto& l : m_lines) {
        Handle(Graphic3d_Group) grp = thePrs->NewGroup();
        Handle(Graphic3d_AspectLine3d) asp =
            new Graphic3d_AspectLine3d(
                l.color, Aspect_TOL_SOLID,
                static_cast<Standard_Real>(l.lineWidth));
        grp->SetPrimitivesAspect(asp);

        Handle(Graphic3d_ArrayOfPolylines) poly =
            new Graphic3d_ArrayOfPolylines(2);
        poly->AddVertex(to3D(l.x1, l.y1));
        poly->AddVertex(to3D(l.x2, l.y2));
        grp->AddPrimitiveArray(poly);
    }

    // Filled triangles
    for (const auto& t : m_tris) {
        Handle(Graphic3d_Group) grp = thePrs->NewGroup();
        Handle(Graphic3d_AspectFillArea3d) asp =
            new Graphic3d_AspectFillArea3d();
        asp->SetInteriorStyle(Aspect_IS_SOLID);
        asp->SetInteriorColor(t.color);
        asp->SetEdgeOff();
        grp->SetPrimitivesAspect(asp);

        Handle(Graphic3d_ArrayOfTriangles) tri =
            new Graphic3d_ArrayOfTriangles(3);
        tri->AddVertex(to3D(t.x1, t.y1));
        tri->AddVertex(to3D(t.x2, t.y2));
        tri->AddVertex(to3D(t.x3, t.y3));
        grp->AddPrimitiveArray(tri);
    }

    // Filled circles (triangle fan)
    for (const auto& c : m_circles) {
        Handle(Graphic3d_Group) grp = thePrs->NewGroup();
        Handle(Graphic3d_AspectFillArea3d) asp =
            new Graphic3d_AspectFillArea3d();
        asp->SetInteriorStyle(Aspect_IS_SOLID);
        asp->SetInteriorColor(c.color);
        asp->SetEdgeOff();
        grp->SetPrimitivesAspect(asp);

        int n = c.segments;
        Handle(Graphic3d_ArrayOfTriangles) fan =
            new Graphic3d_ArrayOfTriangles(n * 3);
        gp_Pnt center = to3D(c.cx, c.cy);
        for (int i = 0; i < n; ++i) {
            double a0 = kTau * static_cast<double>(i) / n;
            double a1 = kTau * static_cast<double>(i + 1) / n;
            fan->AddVertex(center);
            fan->AddVertex(to3D(c.cx + c.radius * std::cos(a0),
                                c.cy + c.radius * std::sin(a0)));
            fan->AddVertex(to3D(c.cx + c.radius * std::cos(a1),
                                c.cy + c.radius * std::sin(a1)));
        }
        grp->AddPrimitiveArray(fan);
    }

    // Text labels
    for (const auto& t : m_texts) {
        Handle(Graphic3d_Group) grp = thePrs->NewGroup();
        Handle(Graphic3d_AspectText3d) asp =
            new Graphic3d_AspectText3d();
        asp->SetColor(t.color);
        asp->SetFont(t.font.c_str());
        grp->SetPrimitivesAspect(asp);

        Handle(Graphic3d_Text) txt =
            new Graphic3d_Text(static_cast<Standard_ShortReal>(t.height));
        txt->SetText(t.text.c_str());
        txt->SetPosition(to3D(t.x, t.y));
        grp->AddText(txt);
    }
}

// ---- renderSensitives -----------------------------------------------

void AIS_Canvas2D::renderSensitives(
    const Handle(SelectMgr_Selection)& theSel)
{
    for (const auto& s : m_sensitives) {
        if (s.pts3d.empty()) continue;

        TColgp_Array1OfPnt pts(1, static_cast<int>(s.pts3d.size()));
        for (int i = 0; i < static_cast<int>(s.pts3d.size()); ++i)
            pts.SetValue(1 + i, s.pts3d[static_cast<size_t>(i)]);

        Handle(Select3D_SensitiveFace) face =
            new Select3D_SensitiveFace(
                s.owner, pts, Select3D_TOS_INTERIOR);
        theSel->Add(face);
    }
}

}  // namespace hobbycad
