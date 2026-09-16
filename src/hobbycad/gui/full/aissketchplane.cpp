// =====================================================================
//  src/hobbycad/gui/full/aissketchplane.cpp — Sketch plane visualization
// =====================================================================

#include "aissketchplane.h"
#include <hobbycad/units.h>

#include <Graphic3d_ArrayOfSegments.hxx>
#include <Graphic3d_ArrayOfTriangles.hxx>
#include <Graphic3d_AspectFillArea3d.hxx>
#include <Graphic3d_AspectLine3d.hxx>
#include <Graphic3d_Group.hxx>
#include <gp_Ax1.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <hobbycad/plane_frame.h>
#include <Prs3d_Presentation.hxx>
#include <SelectMgr_EntityOwner.hxx>
#include <Select3D_SensitiveFace.hxx>
#include <NCollection_Array1.hxx>

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

    // One authoritative right-handed frame shared with the sketch mapping
    // (hobbycad/plane_frame.h): sets the normal AND the in-plane X/Y axes,
    // so the visualized plane and the 2D sketch axes agree. XZ is normal=-Y.
    m_basePlane = gp_Pln(hobbycad::originPlaneFrame(plane));

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

    double angleRad = degreesToRadians(angleDeg);
    m_transform.SetRotation(rotAxis, angleRad);

    updatePlaneGeometry();
}

void AisSketchPlane::setFrame(const gp_Ax3& frame)
{
    m_basePlane = gp_Pln(frame);
    m_transform = gp_Trsf();
    m_useCustomTransform = false;
    m_offset = 0.0;
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
                              const int /*theMode*/)
{
    thePrs->Clear();
    buildPlane(thePrs);
}

// The plane's axes, offset center and four corners after the optional custom
// transform: what both the presentation and the selection are built from.
AisSketchPlane::Frame AisSketchPlane::frame() const
{
    Frame f;
    f.normal = m_basePlane.Axis().Direction();
    f.xDir = m_basePlane.XAxis().Direction();
    f.yDir = m_basePlane.YAxis().Direction();
    if (m_useCustomTransform) {
        f.normal.Transform(m_transform);
        f.xDir.Transform(m_transform);
        f.yDir.Transform(m_transform);
    }
    f.center = m_basePlane.Location();
    if (m_useCustomTransform) {
        f.center.Transform(m_transform);
    }
    f.center.Translate(gp_Vec(f.normal) * m_offset);
    const double hs = m_size / 2.0;
    f.corner[0] = f.center.Translated(gp_Vec(f.xDir) * (-hs) + gp_Vec(f.yDir) * (-hs));
    f.corner[1] = f.center.Translated(gp_Vec(f.xDir) * ( hs) + gp_Vec(f.yDir) * (-hs));
    f.corner[2] = f.center.Translated(gp_Vec(f.xDir) * ( hs) + gp_Vec(f.yDir) * ( hs));
    f.corner[3] = f.center.Translated(gp_Vec(f.xDir) * (-hs) + gp_Vec(f.yDir) * ( hs));
    return f;
}

void AisSketchPlane::ComputeSelection(const Handle(SelectMgr_Selection)& theSel,
                                       const int /*theMode*/)
{
    // Make the plane selectable
    Handle(SelectMgr_EntityOwner) owner = new SelectMgr_EntityOwner(this);

    const Frame f = frame();
    const gp_Dir normal = f.normal;
    const gp_Dir xDir = f.xDir;
    const gp_Dir yDir = f.yDir;
    const gp_Pnt center = f.center;
    const gp_Pnt p1 = f.corner[0], p2 = f.corner[1], p3 = f.corner[2], p4 = f.corner[3];

    NCollection_Array1<gp_Pnt> points(1, 4);
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
    const Frame f = frame();
    const gp_Dir normal = f.normal;
    const gp_Dir xDir = f.xDir;
    const gp_Dir yDir = f.yDir;
    const gp_Pnt center = f.center;
    const gp_Pnt p1 = f.corner[0], p2 = f.corner[1], p3 = f.corner[2], p4 = f.corner[3];

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
        new Graphic3d_ArrayOfTriangles(6, 0, true);  // with normals

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
