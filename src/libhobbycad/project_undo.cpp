// =====================================================================
//  src/libhobbycad/project_undo.cpp — undo for project objects
//  Repository: ayourk/hobbycad
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================

#include "hobbycad/project_undo.h"

#include <memory>

namespace hobbycad {

namespace {

/// Build a ModifyProjectList command around a filled-in snapshot.
DocumentCommand wrap(ProjectListKind kind,
                     std::shared_ptr<ProjectListSnapshot> snap,
                     const std::string& desc,
                     const char* fallback)
{
    DocumentCommand c;
    c.type = DocumentCommandType::ModifyProjectList;
    c.listKind = kind;
    c.projectSnapshot = std::move(snap);
    c.description = desc.empty() ? fallback : desc;
    return c;
}

/// Put one snapshot back into the project.
bool restoreList(const DocumentCommand& cmd, Project& project, bool undo)
{
    if (cmd.type != DocumentCommandType::ModifyProjectList) return false;

    // A command of the right type but with no payload cannot be applied.
    // Refusing keeps the caller's "put it back on the stack" path correct
    // rather than reporting an undo that moved nothing.
    if (!cmd.projectSnapshot) return false;

    const ProjectListSnapshot& s = *cmd.projectSnapshot;
    switch (cmd.listKind) {
    case ProjectListKind::Sketches:
        project.setSketches(undo ? s.sketchesBefore : s.sketchesAfter);
        return true;
    case ProjectListKind::Bodies:
        project.setBodies(undo ? s.bodiesBefore : s.bodiesAfter);
        return true;
    case ProjectListKind::Planes:
        project.setConstructionPlanes(undo ? s.planesBefore : s.planesAfter);
        return true;
    case ProjectListKind::Parameters:
        project.setParameters(undo ? s.parametersBefore : s.parametersAfter);
        return true;
    }
    return false;
}

bool applyEither(const DocumentCommand& cmd, Project& project, bool undo)
{
    if (cmd.type == DocumentCommandType::Compound) {
        // Same all-or-nothing rule the feature applier uses. Checking every
        // step BEFORE touching the project is how it is achieved here: a
        // snapshot restore cannot be rolled back halfway.
        if (cmd.subCommands.empty()) return false;
        for (const auto& c : cmd.subCommands) {
            if (c.type != DocumentCommandType::ModifyProjectList ||
                !c.projectSnapshot) {
                return false;
            }
        }
        if (undo) {
            for (auto it = cmd.subCommands.rbegin();
                 it != cmd.subCommands.rend(); ++it) {
                restoreList(*it, project, true);
            }
        } else {
            for (const auto& c : cmd.subCommands) {
                restoreList(c, project, false);
            }
        }
        return true;
    }
    return restoreList(cmd, project, undo);
}

}  // namespace

DocumentCommand makeSketchListCommand(const std::vector<SketchData>& before,
                                      const std::vector<SketchData>& after,
                                      const std::string& description)
{
    auto snap = std::make_shared<ProjectListSnapshot>();
    snap->sketchesBefore = before;
    snap->sketchesAfter = after;
    return wrap(ProjectListKind::Sketches, std::move(snap), description,
                "Change sketches");
}

DocumentCommand makeBodyListCommand(const std::vector<BodyData>& before,
                                    const std::vector<BodyData>& after,
                                    const std::string& description)
{
    auto snap = std::make_shared<ProjectListSnapshot>();
    snap->bodiesBefore = before;
    snap->bodiesAfter = after;
    return wrap(ProjectListKind::Bodies, std::move(snap), description,
                "Change bodies");
}

DocumentCommand makePlaneListCommand(
    const std::vector<ConstructionPlaneData>& before,
    const std::vector<ConstructionPlaneData>& after,
    const std::string& description)
{
    auto snap = std::make_shared<ProjectListSnapshot>();
    snap->planesBefore = before;
    snap->planesAfter = after;
    return wrap(ProjectListKind::Planes, std::move(snap), description,
                "Change construction planes");
}

DocumentCommand makeParameterListCommand(
    const std::vector<ParameterData>& before,
    const std::vector<ParameterData>& after,
    const std::string& description)
{
    auto snap = std::make_shared<ProjectListSnapshot>();
    snap->parametersBefore = before;
    snap->parametersAfter = after;
    return wrap(ProjectListKind::Parameters, std::move(snap), description,
                "Change parameters");
}

bool applyProjectUndo(const DocumentCommand& cmd, Project& project)
{
    return applyEither(cmd, project, true);
}

bool applyProjectRedo(const DocumentCommand& cmd, Project& project)
{
    return applyEither(cmd, project, false);
}

}  // namespace hobbycad
