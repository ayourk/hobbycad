// =====================================================================
//  src/libhobbycad/hobbycad/project_undo.h — undo for project objects
//  Repository: ayourk/hobbycad
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//
//  The feature recipe is not the only place a project keeps things.
//  Sketches, bodies and construction planes live in Project's own
//  vectors, and Project::addSketch() puts nothing in the recipe, so a
//  sketch created from the CLI has no FeatureData for the recipe-level
//  commands to act on. Undoing "delete sketch Foo" cannot be expressed
//  as a DeleteFeature.
//
//  These commands fill that gap, and they share the ONE undo stack: a
//  front end pops a DocumentCommand and routes it here or to
//  applyUndo() depending on what it acts on.
//
//  This header is separate from document_undo.h on purpose. It needs
//  project.h, which reaches TopoDS_Shape through BodyData and so pulls
//  in OCCT. Undo bookkeeping should not require the 3D kernel, so only
//  the code that actually builds or applies these commands pays for it.
//
// =====================================================================

#ifndef HOBBYCAD_PROJECT_UNDO_H
#define HOBBYCAD_PROJECT_UNDO_H

#include "core.h"
#include "document_undo.h"
#include "project.h"

#include <vector>

namespace hobbycad {

/// Before/after copies of one of Project's object lists.
///
/// Snapshots are stored whole rather than as a diff, following the
/// precedent ModifySketch set: a correct snapshot beats a clever delta,
/// and one list is a bounded amount of copying. They are immutable once
/// recorded, which is why DocumentCommand shares rather than copies them.
struct HOBBYCAD_EXPORT ProjectListSnapshot {
    std::vector<SketchData> sketchesBefore, sketchesAfter;
    std::vector<BodyData> bodiesBefore, bodiesAfter;
    std::vector<ConstructionPlaneData> planesBefore, planesAfter;
    std::vector<ParameterData> parametersBefore, parametersAfter;
};

/// Build a command recording a change to the sketch list.
HOBBYCAD_EXPORT DocumentCommand makeSketchListCommand(
    const std::vector<SketchData>& before,
    const std::vector<SketchData>& after,
    const std::string& description = {});

HOBBYCAD_EXPORT DocumentCommand makeBodyListCommand(
    const std::vector<BodyData>& before,
    const std::vector<BodyData>& after,
    const std::string& description = {});

HOBBYCAD_EXPORT DocumentCommand makePlaneListCommand(
    const std::vector<ConstructionPlaneData>& before,
    const std::vector<ConstructionPlaneData>& after,
    const std::string& description = {});

HOBBYCAD_EXPORT DocumentCommand makeParameterListCommand(
    const std::vector<ParameterData>& before,
    const std::vector<ParameterData>& after,
    const std::string& description = {});

/// Reverse a ModifyProjectList command against `project`.
///
/// Separate from applyUndo() because it acts on a different thing: the
/// project's own object lists rather than the feature recipe. The two are
/// deliberately not merged: a caller holding only a feature vector must
/// not be able to pass it where a Project is required and have the command
/// silently do nothing.
///
/// @return false, leaving the project untouched, if `cmd` is not a
/// ModifyProjectList (or carries no snapshot).
HOBBYCAD_EXPORT bool applyProjectUndo(const DocumentCommand& cmd,
                                      Project& project);

/// Re-apply a ModifyProjectList command. Same contract.
HOBBYCAD_EXPORT bool applyProjectRedo(const DocumentCommand& cmd,
                                      Project& project);

}  // namespace hobbycad

#endif  // HOBBYCAD_PROJECT_UNDO_H
