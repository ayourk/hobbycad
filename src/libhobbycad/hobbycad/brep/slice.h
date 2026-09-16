// =====================================================================
//  src/libhobbycad/hobbycad/brep/slice.h — mesh cross-section slicing
// =====================================================================
//
//  Slice a triangle mesh with a plane and chain the triangle-plane
//  intersection segments into closed boundary loops, expressed in the
//  plane's 2D (u,v) frame, ready for brep::fitBSpline2D. Step 1 of the
//  HobbyMesh STL -> STEP path: slice -> fit -> loft -> STEP.
//
//  Part of libhobbycad.  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#ifndef HOBBYCAD_BREP_SLICE_H
#define HOBBYCAD_BREP_SLICE_H

#include "../core.h"
#include "../types.h"

#include <array>
#include <vector>

#include <gp_Pnt.hxx>
#include <gp_Pln.hxx>
#include <Poly_Triangulation.hxx>

namespace hobbycad {
namespace brep {

/// One boundary loop of a cross-section, in the cutting plane's 2D (u,v) frame.
struct SliceLoop {
    std::vector<Point2D> points;   ///< ordered around the loop
    bool closed = true;            ///< whether the chain closed back on itself
};

/// A cross-section as solid regions: one outer boundary plus the holes cut in it.
/// A section may hold several (multiple bodies, or a solid sitting inside another
/// body's cavity), so classifySection() returns a vector of these.
struct SliceContour {
    SliceLoop outer;                 ///< a solid boundary (CCW by convention)
    std::vector<SliceLoop> holes;    ///< holes cut into this outer (CW)
};

/// Classify the raw loops of one section into outer boundaries and holes by
/// even-odd nesting: a loop contained by an even number of others bounds solid
/// (outer); an odd number means it is a hole in the smallest outer that contains
/// it. Windings are normalized (outer CCW, holes CW). Loops with < 3 points are
/// dropped. This is what turns "largest loop wins" into holes-and-bodies aware.
HOBBYCAD_EXPORT std::vector<SliceContour> classifySection(
    const std::vector<SliceLoop>& loops);

/// Slice a set of triangles with a plane; return the closed boundary loops in
/// the plane's 2D (u,v) coordinates. `weld` merges endpoints within this
/// distance when chaining. Triangles not crossing the plane are ignored.
HOBBYCAD_EXPORT std::vector<SliceLoop> sliceTriangles(
    const std::vector<std::array<gp_Pnt, 3>>& tris, const gp_Pln& plane,
    double weld = 1e-6);

/// Same, for an OCCT triangulation (e.g. from readStl()).
HOBBYCAD_EXPORT std::vector<SliceLoop> sliceMesh(
    const Handle(Poly_Triangulation)& mesh, const gp_Pln& plane,
    double weld = 1e-6);

}  // namespace brep
}  // namespace hobbycad

#endif  // HOBBYCAD_BREP_SLICE_H
