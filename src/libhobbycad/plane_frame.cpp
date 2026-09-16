// =====================================================================
//  src/libhobbycad/plane_frame.cpp — Authoritative plane frames
// =====================================================================
//
//  Implementation of the one right-handed frame source shared by the
//  sketch point<->world mapping and the 3D viewport.  See plane_frame.h.
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include "hobbycad/plane_frame.h"
#include "hobbycad/core.h"
#include "hobbycad/format.h"

#include <gp_Pnt.hxx>
#include <gp_Ax1.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <algorithm>

namespace hobbycad {

namespace {
constexpr double kDeg2Rad = 0.017453292519943295;  // pi / 180

/// gp_Ax3 -> PlaneBasis: location is the origin, XDirection/YDirection are
/// u/v, Direction is the normal. gp_Ax3 is right-handed by construction, so
/// this preserves handedness.
PlaneBasis toBasis(const gp_Ax3& f)
{
    const gp_Pnt L = f.Location();
    const gp_Dir X = f.XDirection();
    const gp_Dir Y = f.YDirection();
    const gp_Dir N = f.Direction();
    return {
        { static_cast<float>(L.X()), static_cast<float>(L.Y()), static_cast<float>(L.Z()) },
        { static_cast<float>(X.X()), static_cast<float>(X.Y()), static_cast<float>(X.Z()) },
        { static_cast<float>(Y.X()), static_cast<float>(Y.Y()), static_cast<float>(Y.Z()) },
        { static_cast<float>(N.X()), static_cast<float>(N.Y()), static_cast<float>(N.Z()) },
    };
}
}  // namespace

gp_Dir axisDir(PlaneRotationAxis a)
{
    switch (a) {
    case PlaneRotationAxis::X: return gp_Dir(1, 0, 0);
    case PlaneRotationAxis::Y: return gp_Dir(0, 1, 0);
    case PlaneRotationAxis::Z: return gp_Dir(0, 0, 1);
    }
    return gp_Dir(0, 0, 1);
}

gp_Ax3 originPlaneFrame(SketchPlane plane)
{
    // gp_Ax3(P, N, Vx): N is the Direction (normal), Vx the XDirection,
    // YDirection = N ^ Vx.  The normals are the derived u x v so every
    // plane is right-handed with first axis right, second axis up:
    //   XY: u=X v=Y n=+Z    YZ: u=Y v=Z n=+X    XZ: u=X v=Z n=-Y
    switch (plane) {
    case SketchPlane::XZ: return gp_Ax3(gp_Pnt(0, 0, 0), gp_Dir(0, -1, 0), gp_Dir(1, 0, 0));
    case SketchPlane::YZ: return gp_Ax3(gp_Pnt(0, 0, 0), gp_Dir(1, 0, 0),  gp_Dir(0, 1, 0));
    case SketchPlane::XY:
    case SketchPlane::Custom:
    default:              return gp_Ax3(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1),  gp_Dir(1, 0, 0));
    }
}

gp_Ax3 constructionPlaneFrame(const ConstructionPlaneData& p, const Project& project,
                              std::vector<int> visited)
{
    // A plane already on the path means a loop (possible only in a
    // hand-edited file: every entry point refuses cycles). The chain stops
    // there and the plane is placed from its base as if absolute.
    const bool looped = std::find(visited.begin(), visited.end(), p.id) != visited.end();
    visited.push_back(p.id);
    gp_Ax3 f = originPlaneFrame(p.basePlane);
    if (!looped && p.type == ConstructionPlaneType::OffsetFromPlane) {
        if (const ConstructionPlaneData* ref = project.constructionPlaneById(p.basePlaneId))
            if (std::find(visited.begin(), visited.end(), ref->id) == visited.end())
                f = constructionPlaneFrame(*ref, project, visited);
    }
    const gp_Ax3 base = f;           // the frame the center may be relative to
    const gp_Pnt pivot = f.Location();
    if (!hobbycad::fuzzyIsNull(p.primaryAngle)) {
        gp_Trsf r; r.SetRotation(gp_Ax1(pivot, axisDir(p.primaryAxis)), p.primaryAngle * kDeg2Rad);
        f.Transform(r);
    }
    if (!hobbycad::fuzzyIsNull(p.secondaryAngle)) {
        gp_Trsf r; r.SetRotation(gp_Ax1(pivot, axisDir(p.secondaryAxis)), p.secondaryAngle * kDeg2Rad);
        f.Transform(r);
    }
    if (!hobbycad::fuzzyIsNull(p.rollAngle)) {
        gp_Trsf r; r.SetRotation(gp_Ax1(f.Location(), f.Direction()), p.rollAngle * kDeg2Rad);
        f.Transform(r);
    }
    if (p.hasCustomOrigin()) {
        if (!p.centerRelative) {
            f.Translate(gp_Vec(p.originX, p.originY, p.originZ));            // global axes
        } else {
            // Offsets along the reference frame's X, Y and normal. The
            // reference is another construction plane, or this plane's own
            // base/reference plane when none is named.
            gp_Ax3 ref = base;
            if (!looped && p.centerRefPlaneId >= 0 && p.centerRefPlaneId != p.id)
                if (const ConstructionPlaneData* rp = project.constructionPlaneById(p.centerRefPlaneId))
                    if (std::find(visited.begin(), visited.end(), rp->id) == visited.end())
                        ref = constructionPlaneFrame(*rp, project, visited);
            f.Translate(gp_Vec(ref.XDirection()) * p.originX + gp_Vec(ref.YDirection()) * p.originY
                        + gp_Vec(ref.Direction()) * p.originZ);
        }
    }
    if (!hobbycad::fuzzyIsNull(p.offset)) f.Translate(gp_Vec(f.Direction()) * p.offset);
    return f;
}

gp_Ax3 sketchFrame(const SketchData& sketch, const Project& project)
{
    if (sketch.constructionPlaneId >= 0) {
        if (const ConstructionPlaneData* cp = project.constructionPlaneById(sketch.constructionPlaneId))
            return constructionPlaneFrame(*cp, project);
    }
    // Inline: base origin plane, optional rotation about a global axis, then
    // the offset along the resulting normal.
    gp_Ax3 f = originPlaneFrame(sketch.plane);
    if (!hobbycad::fuzzyIsNull(sketch.rotationAngle)) {
        gp_Trsf r; r.SetRotation(gp_Ax1(f.Location(), axisDir(sketch.rotationAxis)),
                                 sketch.rotationAngle * kDeg2Rad);
        f.Transform(r);
    }
    if (!hobbycad::fuzzyIsNull(sketch.planeOffset))
        f.Translate(gp_Vec(f.Direction()) * sketch.planeOffset);
    return f;
}

PlaneBasis planeBasisFor(const SketchData& sketch, const Project& project)
{
    // Canonical plane, no construction plane, no inline rotation: keep the
    // exact closed-form path (also what the CLI and tests pin) so no OCCT
    // rounding creeps into the common case.
    if (sketch.constructionPlaneId < 0 && hobbycad::fuzzyIsNull(sketch.rotationAngle))
        return planeBasisFor(sketch.plane, sketch.planeOffset);
    return toBasis(sketchFrame(sketch, project));
}

const ConstructionPlaneData* findConstructionPlaneByName(
    const std::vector<ConstructionPlaneData>& planes, const std::string& name)
{
    for (const ConstructionPlaneData& cp : planes) {
        if (equalsIgnoreCase(cp.name, name)) return &cp;
    }
    return nullptr;
}

ConstructionPlaneData makeConstructionPlane(const ConstructionPlaneSpec& spec, size_t existingCount)
{
    ConstructionPlaneData plane;
    plane.name = spec.name.empty() ? "Plane " + std::to_string(existingCount + 1) : spec.name;
    plane.basePlane = SketchPlane::XY;
    if (spec.refPlaneId >= 0) {
        plane.type = ConstructionPlaneType::OffsetFromPlane;
        plane.centerRelative = true;
        plane.centerRefPlaneId = spec.refPlaneId;
        plane.basePlaneId = spec.refPlaneId;
        plane.offset = spec.offset;
    } else {
        plane.type = (spec.rotX != 0 || spec.rotY != 0 || spec.rotZ != 0)
                         ? ConstructionPlaneType::Angled
                         : ConstructionPlaneType::OffsetFromOrigin;
        plane.originX = spec.originX; plane.originY = spec.originY; plane.originZ = spec.originZ;
        plane.offset = spec.offset;
    }
    // Three global-axis angles map onto the primary/secondary/roll model as
    // X -> primary, Y -> secondary, Z -> roll (about the plane normal).
    plane.primaryAxis = PlaneRotationAxis::X;   plane.primaryAngle = spec.rotX;
    plane.secondaryAxis = PlaneRotationAxis::Y; plane.secondaryAngle = spec.rotY;
    plane.rollAngle = spec.rotZ;
    return plane;
}

bool resolveSketchPlaneRef(const std::string& ref,
                           const std::vector<ConstructionPlaneData>& planes,
                           SketchPlane& plane, int& constructionPlaneId,
                           std::string* displayName)
{
    struct Origin { const char* name; SketchPlane plane; };
    static const Origin kOrigin[] = {
        {"XY", SketchPlane::XY}, {"XZ", SketchPlane::XZ}, {"YZ", SketchPlane::YZ}};
    for (const Origin& o : kOrigin) {
        if (equalsIgnoreCase(ref, o.name)) {
            plane = o.plane;
            constructionPlaneId = -1;
            if (displayName) *displayName = o.name;
            return true;
        }
    }
    if (const ConstructionPlaneData* cp = findConstructionPlaneByName(planes, ref)) {
        plane = SketchPlane::Custom;
        constructionPlaneId = cp->id;
        if (displayName) *displayName = cp->name;
        return true;
    }
    return false;
}

}  // namespace hobbycad
