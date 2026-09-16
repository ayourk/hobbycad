// src/libhobbycad/brep/mesh_to_step.cpp — STL -> STEP by slicing + lofting
// SPDX-License-Identifier: GPL-3.0-only
#include <hobbycad/brep/mesh_to_step.h>
#include <hobbycad/geometry/types.h>
#include <hobbycad/brep/slice.h>
#include <hobbycad/stl_io.h>
#include <hobbycad/step_io.h>

#include <algorithm>
#include <limits>

#include <gp_Vec.hxx>
#include <gp_Pln.hxx>
// 7.9.x has no NCollection_HArray1 template; TColgp_HArray1OfPnt is its
// distinct class, so the array type is version-guarded (see operations.cpp).
#include <Standard_Version.hxx>
#if OCC_VERSION_MAJOR >= 8
#include <NCollection_HArray1.hxx>
#else
#include <TColgp_HArray1OfPnt.hxx>
#endif
#include <GeomAPI_Interpolate.hxx>
#include <Geom_Curve.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <Standard_Failure.hxx>
#include "hobbycad/occt_failure.h"

namespace hobbycad {
namespace brep {

namespace {
// Resample a closed 2D loop to n points at even arc length, with a start aligned
// to the loop's rightmost vertex, so sections from different heights become
// COMPATIBLE wires (same pole count, aligned start) for ThruSections lofting.
std::vector<Point2D> resampleLoop(const std::vector<Point2D>& pts, int n) {
    const int m = static_cast<int>(pts.size());
    if (m < 3 || n < 3) return pts;
    std::vector<double> cum(m + 1, 0.0);
    for (int i = 0; i < m; ++i) {
        const Point2D& a = pts[i];
        const Point2D& b = pts[(i + 1) % m];
        cum[i + 1] = cum[i] + std::hypot(b.x - a.x, b.y - a.y);
    }
    const double L = cum[m];
    if (!(L > 0.0)) return pts;
    int rstart = 0;
    for (int i = 1; i < m; ++i) if (pts[i].x > pts[rstart].x) rstart = i;
    const double s0 = cum[rstart];
    auto at = [&](double s) -> Point2D {
        s = std::fmod(s, L); if (s < 0) s += L;
        int i = 0; while (i < m && cum[i + 1] < s) ++i; if (i >= m) i = m - 1;
        const double seg = cum[i + 1] - cum[i];
        const double t = seg > hobbycad::geometry::kExactEps ? (s - cum[i]) / seg : 0.0;
        const Point2D& a = pts[i];
        const Point2D& b = pts[(i + 1) % m];
        return { a.x + t * (b.x - a.x), a.y + t * (b.y - a.y) };
    };
    std::vector<Point2D> out;
    out.reserve(n);
    for (int k = 0; k < n; ++k) out.push_back(at(s0 + L * k / n));
    return out;
}
}  // namespace

MeshLoftResult loftMeshSections(const std::vector<std::array<gp_Pnt, 3>>& tris,
                                const gp_Dir& axis, int numSections, double weld) {
    MeshLoftResult r;
    if (tris.empty()) { r.error = "no triangles"; return r; }
    if (numSections < 2) { r.error = "need at least 2 sections"; return r; }

    const gp_Pnt O(0, 0, 0);
    const gp_Vec A(axis);
    double lo = std::numeric_limits<double>::max();
    double hi = -std::numeric_limits<double>::max();
    for (const auto& t : tris)
        for (int i = 0; i < 3; ++i) {
            const double d = gp_Vec(O, t[i]).Dot(A);
            lo = std::min(lo, d);
            hi = std::max(hi, d);
        }
    if (!(hi > lo)) { r.error = "mesh is flat along the slice axis"; return r; }

    try {
        BRepOffsetAPI_ThruSections loft(/*solid=*/true,
                                        /*ruled=*/false, /*pres3d=*/1e-6);
        int made = 0;
        for (int k = 0; k < numSections; ++k) {
            const double t = lo + (hi - lo) * (k + 0.5) / numSections;  // section centers
            const gp_Pnt o = O.Translated(A * t);
            const gp_Pln plane(o, axis);
            auto loops = sliceTriangles(tris, plane, weld);
            if (loops.empty()) continue;
            // pick the loop with the most points (outer boundary, first cut)
            const SliceLoop* L = &loops[0];
            for (const auto& c : loops) if (c.points.size() > L->points.size()) L = &c;
            if (L->points.size() < 3) continue;
            const std::vector<Point2D> rs = resampleLoop(L->points, 48);
            const gp_Vec xd(plane.Position().XDirection());
            const gp_Vec yd(plane.Position().YDirection());
#if OCC_VERSION_MAJOR >= 8
            Handle(NCollection_HArray1<gp_Pnt>) pts =
                new NCollection_HArray1<gp_Pnt>(1, static_cast<int>(rs.size()));
#else
            Handle(TColgp_HArray1OfPnt) pts =
                new TColgp_HArray1OfPnt(1, static_cast<int>(rs.size()));
#endif
            for (int i = 0; i < static_cast<int>(rs.size()); ++i) {
                const Point2D& p = rs[static_cast<std::size_t>(i)];
                pts->SetValue(i + 1, o.Translated(xd * p.x + yd * p.y));
            }
            GeomAPI_Interpolate interp(pts, /*periodic=*/true, 1e-6);
            interp.Perform();
            if (!interp.IsDone()) continue;
            BRepBuilderAPI_MakeEdge me(interp.Curve());
            if (!me.IsDone()) continue;
            BRepBuilderAPI_MakeWire mw(me.Edge());
            if (!mw.IsDone()) continue;
            loft.AddWire(mw.Wire());
            ++made;
        }
        if (made < 2) { r.error = "fewer than 2 usable sections"; return r; }
        loft.Build();
        if (!loft.IsDone()) { r.error = "ThruSections loft failed"; return r; }
        r.shape = loft.Shape();
        r.sections = made;
        r.success = true;
    } catch (const Standard_Failure& e) {
        r.error = std::string("OCCT: ") + occtFailureMessage(e);
    }
    return r;
}

MeshLoftResult loftMeshSections(const Handle(Poly_Triangulation)& mesh,
                                const gp_Dir& axis, int numSections, double weld) {
    if (mesh.IsNull()) { MeshLoftResult r; r.error = "null mesh"; return r; }
    std::vector<std::array<gp_Pnt, 3>> tris;
    const int nt = mesh->NbTriangles();
    tris.reserve(static_cast<std::size_t>(nt));
    for (int i = 1; i <= nt; ++i) {
        int n1, n2, n3;
        mesh->Triangle(i).Get(n1, n2, n3);
        tris.push_back({ mesh->Node(n1), mesh->Node(n2), mesh->Node(n3) });
    }
    return loftMeshSections(tris, axis, numSections, weld);
}

bool stlToStep(const std::string& inStl, const std::string& outStep,
               const gp_Dir& axis, int numSections, std::string* error) {
    Handle(Poly_Triangulation) mesh = stl_io::readStlAsMesh(inStl);
    if (mesh.IsNull()) { if (error) *error = "could not read STL: " + inStl; return false; }
    MeshLoftResult m = loftMeshSections(mesh, axis, numSections);
    if (!m.success) { if (error) *error = m.error; return false; }
    step_io::WriteResult w = step_io::writeStep(outStep, m.shape);
    if (!w.success && error) *error = w.errorMessage;
    return w.success;
}

}  // namespace brep
}  // namespace hobbycad
