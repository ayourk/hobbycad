// =====================================================================
//  src/hobbycad/gui/full/aissketchplane.cpp — Sketch plane visualization
// =====================================================================

#include "aissketchplane.h"

#include <Graphic3d_ArrayOfSegments.hxx>
#include <Graphic3d_ArrayOfTriangles.hxx>
#include <Graphic3d_AspectFillArea3d.hxx>
#include <Graphic3d_AspectLine3d.hxx>
#include <Graphic3d_Group.hxx>
#include <gp_Ax1.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>
#include <Prs3d_Presentation.hxx>
#include <SelectMgr_EntityOwner.hxx>
#include <Select3D_SensitiveFace.hxx>
#include <TColgp_Array1OfPnt.hxx>

#include <cmath>

namespace hobbycad {

IMPLEMENT_STANDARD_RTTIEXT(AisSketchPlane, AIS_InteractiveObject)

AisSketchPlane::AisSketchPlane(double size)
    : m_size(size)
    , m_fillColor(0.3, 0.6, 1.0, Quantity_TOC_RGB)   // Light blue
    , m_borderColor(0.1, 0.3, 0.8, Quantity_TOC_RGB) // Darker blue border
{
    // Default XY plane at origin
    m_basePlane = gp_Pln(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1));
}

void AisSketchPlane::setPlane(SketchPlane plane, double offset)
{
    m_offset = offset;
    m_useCustomTransform = false;

    switch (plane) {
    case SketchPlane::XY:
        m_basePlane = gp_Pln(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1));
        break;
    case SketchPlane::XZ:
        m_basePlane = gp_Pln(gp_Pnt(0, 0, 0), gp_Dir(0, 1, 0));
        break;
    case SketchPlane::YZ:
        m_basePlane = gp_Pln(gp_Pnt(0, 0, 0), gp_Dir(1, 0, 0));
        break;
    default:
        m_basePlane = gp_Pln(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1));
        break;
    }

    updatePlaneGeometry();
}

void AisSketchPlane::setCustomPlane(PlaneRotationAxis axis, double angleDeg, double offset)
{
    m_offset = offset;
    m_useCustomTransform = true;

    // Start with XY plane
    m_basePlane = gp_Pln(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1));

    // Set up rotation axis
    gp_Ax1 rotAxis;
    switch (axis) {
    case PlaneRotationAxis::X:
        rotAxis = gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(1, 0, 0));
        break;
    case PlaneRotationAxis::Y:
        rotAxis = gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(0, 1, 0));
        break;
    case PlaneRotationAxis::Z:
        rotAxis = gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1));
        break;
    }

    double angleRad = angleDeg * M_PI / 180.0;
    m_transform.SetRotation(rotAxis, angleRad);

    updatePlaneGeometry();
}

void AisSketchPlane::setFillColor(const Quantity_Color& color)
{
    m_fillColor = color;
    SetToUpdate();
}

void AisSketchPlane::setBorderColor(const Quantity_Color& color)
{
    m_borderColor = color;
    SetToUpdate();
}

void AisSketchPlane::setPlaneTransparency(double alpha)
{
    m_transparency = std::clamp(alpha, 0.0, 1.0);
    SetToUpdate();
}

void AisSketchPlane::setSize(double size)
{
    m_size = size;
    updatePlaneGeometry();
}

void AisSketchPlane::updatePlaneGeometry()
{
    SetToUpdate();
}

void AisSketchPlane::Compute(const Handle(PrsMgr_PresentationManager)& /*thePrsMgr*/,
                              const Handle(Prs3d_Presentation)& thePrs,
                              const Standard_Integer /*theMode*/)
{
    thePrs->Clear();
    buildPlane(thePrs);
}

void AisSketchPlane::ComputeSelection(const Handle(SelectMgr_Selection)& theSel,
                                       const Standard_Integer /*theMode*/)
{
    // Make the plane selectable
    Handle(SelectMgr_EntityOwner) owner = new SelectMgr_EntityOwner(this);

    // Get plane axes
    gp_Dir normal = m_basePlane.Axis().Direction();
    gp_Dir xDir = m_basePlane.XAxis().Direction();
    gp_Dir yDir = m_basePlane.YAxis().Direction();

    if (m_useCustomTransform) {
        normal.Transform(m_transform);
        xDir.Transform(m_transform);
        yDir.Transform(m_transform);
    }

    // Plane center with offset
    gp_Pnt center = m_basePlane.Location();
    if (m_useCustomTransform) {
        center.Transform(m_transform);
    }
    center.Translate(gp_Vec(normal) * m_offset);

    // Calculate corner points
    double hs = m_size / 2.0;
    gp_Pnt p1 = center.Translated(gp_Vec(xDir) * (-hs) + gp_Vec(yDir) * (-hs));
    gp_Pnt p2 = center.Translated(gp_Vec(xDir) * ( hs) + gp_Vec(yDir) * (-hs));
    gp_Pnt p3 = center.Translated(gp_Vec(xDir) * ( hs) + gp_Vec(yDir) * ( hs));
    gp_Pnt p4 = center.Translated(gp_Vec(xDir) * (-hs) + gp_Vec(yDir) * ( hs));

    TColgp_Array1OfPnt points(1, 4);
    points.SetValue(1, p1);
    points.SetValue(2, p2);
    points.SetValue(3, p3);
    points.SetValue(4, p4);

    Handle(Select3D_SensitiveFace) sensitiveFace =
        new Select3D_SensitiveFace(owner, points, Select3D_TOS_BOUNDARY);
    theSel->Add(sensitiveFace);
}

void AisSketchPlane::buildPlane(const Handle(Prs3d_Presentation)& prs)
{
    // Get plane axes
    gp_Dir normal = m_basePlane.Axis().Direction();
    gp_Dir xDir = m_basePlane.XAxis().Direction();
    gp_Dir yDir = m_basePlane.YAxis().Direction();

    if (m_useCustomTransform) {
        normal.Transform(m_transform);
        xDir.Transform(m_transform);
        yDir.Transform(m_transform);
    }

    // Plane center with offset
    gp_Pnt center = m_basePlane.Location();
    if (m_useCustomTransform) {
        center.Transform(m_transform);
    }
    center.Translate(gp_Vec(normal) * m_offset);

    // Calculate corner points
    double hs = m_size / 2.0;
    gp_Pnt p1 = center.Translated(gp_Vec(xDir) * (-hs) + gp_Vec(yDir) * (-hs));
    gp_Pnt p2 = center.Translated(gp_Vec(xDir) * ( hs) + gp_Vec(yDir) * (-hs));
    gp_Pnt p3 = center.Translated(gp_Vec(xDir) * ( hs) + gp_Vec(yDir) * ( hs));
    gp_Pnt p4 = center.Translated(gp_Vec(xDir) * (-hs) + gp_Vec(yDir) * ( hs));

    // --- Fill (semi-transparent) ---
    Handle(Graphic3d_Group) fillGroup = prs->NewGroup();

    Handle(Graphic3d_AspectFillArea3d) fillAspect = new Graphic3d_AspectFillArea3d();
    fillAspect->SetInteriorStyle(Aspect_IS_SOLID);
    fillAspect->SetInteriorColor(m_fillColor);
    fillAspect->SetEdgeOff();

    // Apply transparency to fill color
    Quantity_Color transpColor(
        m_fillColor.Red() * (1.0 - m_transparency),
        m_fillColor.Green() * (1.0 - m_transparency),
        m_fillColor.Blue() * (1.0 - m_transparency),
        Quantity_TOC_RGB);
    fillAspect->SetInteriorColor(m_fillColor);

    fillGroup->SetPrimitivesAspect(fillAspect);

    // Create quad as two triangles
    Handle(Graphic3d_ArrayOfTriangles) triangles =
        new Graphic3d_ArrayOfTriangles(6, 0, Standard_True);  // with normals

    // Add vertices with normals for both triangles
    triangles->AddVertex(p1, normal);
    triangles->AddVertex(p2, normal);
    triangles->AddVertex(p3, normal);

    triangles->AddVertex(p1, normal);
    triangles->AddVertex(p3, normal);
    triangles->AddVertex(p4, normal);

    fillGroup->AddPrimitiveArray(triangles);

    // --- Border outline ---
    Handle(Graphic3d_Group) borderGroup = prs->NewGroup();

    Handle(Graphic3d_AspectLine3d) borderAspect =
        new Graphic3d_AspectLine3d(m_borderColor, Aspect_TOL_SOLID, 2.0);
    borderGroup->SetPrimitivesAspect(borderAspect);

    Handle(Graphic3d_ArrayOfSegments) outline =
        new Graphic3d_ArrayOfSegments(8);

    outline->AddVertex(p1);
    outline->AddVertex(p2);
    outline->AddVertex(p2);
    outline->AddVertex(p3);
    outline->AddVertex(p3);
    outline->AddVertex(p4);
    outline->AddVertex(p4);
    outline->AddVertex(p1);

    borderGroup->AddPrimitiveArray(outline);

    // --- Center crosshair (shows origin on plane) ---
    Handle(Graphic3d_Group) crossGroup = prs->NewGroup();

    Handle(Graphic3d_AspectLine3d) crossAspect =
        new Graphic3d_AspectLine3d(m_borderColor, Aspect_TOL_DASH, 1.0);
    crossGroup->SetPrimitivesAspect(crossAspect);

    double crossSize = m_size * 0.1;  // 10% of plane size
    Handle(Graphic3d_ArrayOfSegments) cross =
        new Graphic3d_ArrayOfSegments(4);

    // Horizontal line through center
    cross->AddVertex(center.Translated(gp_Vec(xDir) * (-crossSize)));
    cross->AddVertex(center.Translated(gp_Vec(xDir) * ( crossSize)));
    // Vertical line through center
    cross->AddVertex(center.Translated(gp_Vec(yDir) * (-crossSize)));
    cross->AddVertex(center.Translated(gp_Vec(yDir) * ( crossSize)));

    crossGroup->AddPrimitiveArray(cross);
}

}  // namespace hobbycad
