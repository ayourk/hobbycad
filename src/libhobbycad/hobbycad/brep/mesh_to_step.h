// =====================================================================
//  src/libhobbycad/hobbycad/brep/mesh_to_step.h — STL -> STEP by lofting
// =====================================================================
//
//  The HobbyMesh reverse-engineering pipeline: slice a triangle mesh into
//  planar cross-sections, build a closed wire per section, and loft them
//  (OCCT ThruSections) into a solid B-rep, then write STEP. Sections are
//  uniform for now; adaptive (variable) spacing is a planned refinement.
//
//  Part of libhobbycad.  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#ifndef HOBBYCAD_BREP_MESH_TO_STEP_H
#define HOBBYCAD_BREP_MESH_TO_STEP_H

#include "../core.h"
#include <array>
#include <string>
#include <vector>
#include <gp_Pnt.hxx>
#include <gp_Dir.hxx>
#include <TopoDS_Shape.hxx>
#include <Poly_Triangulation.hxx>

namespace hobbycad {
namespace brep {

struct MeshLoftResult {
    bool success = false;
    TopoDS_Shape shape;      ///< the lofted solid
    int sections = 0;        ///< number of section wires actually lofted
    std::string error;
};

/// Slice a triangle mesh into `numSections` uniform planar cross-sections along
/// `axis`, build a closed wire per section, and loft them into a solid.
HOBBYCAD_EXPORT MeshLoftResult loftMeshSections(
    const std::vector<std::array<gp_Pnt, 3>>& tris, const gp_Dir& axis,
    int numSections, double weld = 1e-6);

HOBBYCAD_EXPORT MeshLoftResult loftMeshSections(
    const Handle(Poly_Triangulation)& mesh, const gp_Dir& axis,
    int numSections, double weld = 1e-6);

/// Section count a caller with no better number passes to stlToStep():
/// uniform spacing for now; adaptive spacing is planned.
constexpr int kDefaultStlSections = 60;

/// Convenience: read an STL, loft sections, write STEP. Returns success.
HOBBYCAD_EXPORT bool stlToStep(const std::string& inStl, const std::string& outStep,
                               const gp_Dir& axis, int numSections,
                               std::string* error = nullptr);

}  // namespace brep
}  // namespace hobbycad

#endif  // HOBBYCAD_BREP_MESH_TO_STEP_H
