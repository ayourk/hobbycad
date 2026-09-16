// =====================================================================
//  src/libhobbycad/hobbycad/plane_frame.h — Authoritative plane frames
// =====================================================================
//
//  One source for the right-handed coordinate frame of every sketch
//  plane: the three origin planes, construction planes (with their full
//  rotation/roll/offset chain), and the inline-parameter planes a sketch
//  can carry.  Both the sketch point<->world mapping (planeBasisFor) and
//  the 3D viewport visualization derive from these, so what is previewed
//  is what is committed and stored, and the two can never diverge.
//
//  Convention (see planeBasisFor in project.cpp): each plane is
//  right-handed with the in-plane axes kept "natural" (first axis to
//  the right, second axis up) and the normal DERIVED as u x v:
//      XY -> n=+Z    YZ -> n=+X    XZ -> n=-Y
//  Handedness is never a stored degree of freedom; drawing from the far
//  side of a plane ("tails") is a view flip, not a frame change.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_PLANE_FRAME_H
#define HOBBYCAD_PLANE_FRAME_H

#include "core.h"
#include "project.h"

#include <gp_Ax3.hxx>
#include <gp_Dir.hxx>

#include <vector>

namespace hobbycad {

/// Direction of a plane-rotation axis (X/Y/Z -> unit gp_Dir).
HOBBYCAD_EXPORT gp_Dir axisDir(PlaneRotationAxis a);

/// Right-handed frame for a canonical origin plane. The normal is the
/// derived u x v (XY->+Z, YZ->+X, XZ->-Y), so a sketch drawn on it reads
/// with its first axis right and second axis up when viewed from +normal.
HOBBYCAD_EXPORT gp_Ax3 originPlaneFrame(SketchPlane plane);

/// Authoritative frame for a construction plane: base plane (or the
/// referenced plane, chained), primary rotation, secondary rotation, roll
/// about the plane's own normal, the center translation, then the offset
/// along the normal. `visited` guards against reference cycles in a
/// hand-edited file.
HOBBYCAD_EXPORT gp_Ax3 constructionPlaneFrame(const ConstructionPlaneData& p,
                                              const Project& project,
                                              std::vector<int> visited = {});

/// Frame a sketch actually sits on: its construction plane when it names
/// one, otherwise its inline base plane + rotation + offset.
HOBBYCAD_EXPORT gp_Ax3 sketchFrame(const SketchData& sketch, const Project& project);

}  // namespace hobbycad

#endif  // HOBBYCAD_PLANE_FRAME_H
