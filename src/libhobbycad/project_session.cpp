// =====================================================================
//  src/libhobbycad/project_session.cpp — edits to an open project
//  Repository: ayourk/hobbycad
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================

#include "hobbycad/project_session.h"

#include "hobbycad/brep/operations.h"
#include "hobbycad/plane_frame.h"
#include "hobbycad/project_undo.h"
#include "hobbycad/sketch/profiles.h"

#include <BRepBuilderAPI_Transform.hxx>
#include <gp.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax3.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>

#include <algorithm>
#include <cmath>
#include <utility>

namespace hobbycad {

namespace {

int indexOfSketch(const std::vector<SketchData>& sketches, int id)
{
    for (std::size_t i = 0; i < sketches.size(); ++i) {
        if (sketches[i].id == id) return static_cast<int>(i);
    }
    return -1;
}

int indexOfFeature(const std::vector<FeatureData>& features, int id)
{
    for (std::size_t i = 0; i < features.size(); ++i) {
        if (features[i].id == id) return static_cast<int>(i);
    }
    return -1;
}

/// Everything a command can touch, so a compound that fails partway can be
/// put back whole.
struct ProjectState {
    std::vector<SketchData> sketches;
    std::vector<BodyData> bodies;
    std::vector<ConstructionPlaneData> planes;
    std::vector<ParameterData> parameters;
    std::vector<FeatureData> features;
    bool modified = false;
};

ProjectState capture(const Project& p)
{
    return {p.sketches(), p.bodies(), p.constructionPlanes(), p.parameters(),
            p.features(), p.isModified()};
}

void restore(Project& p, const ProjectState& s)
{
    p.setSketches(s.sketches);
    p.setBodies(s.bodies);
    p.setConstructionPlanes(s.planes);
    p.setParameters(s.parameters);
    p.setFeatures(s.features);
    p.setModified(s.modified);
}

/// One step of undo or redo. The recipe, the project's lists and a sketch's
/// contents are different targets, and a compound may mix them: the CLI
/// used to snapshot the sketch list while the GUI edited the recipe, so each
/// host applied only the half it knew about.
bool applyStep(const DocumentCommand& cmd, Project& project, bool undo)
{
    switch (cmd.type) {
    case DocumentCommandType::Compound:
        if (undo) {
            for (auto it = cmd.subCommands.rbegin(); it != cmd.subCommands.rend(); ++it) {
                if (!applyStep(*it, project, true)) return false;
            }
        } else {
            for (const DocumentCommand& c : cmd.subCommands) {
                if (!applyStep(c, project, false)) return false;
            }
        }
        return true;

    case DocumentCommandType::ModifyProjectList:
        return undo ? applyProjectUndo(cmd, project) : applyProjectRedo(cmd, project);

    case DocumentCommandType::ModifySketch: {
        const int at = indexOfSketch(project.sketches(), cmd.sketchFeatureId);
        if (at < 0) return false;
        SketchData s = project.sketches()[static_cast<std::size_t>(at)];
        const DocumentCommand::SketchContents& c = undo ? cmd.sketchBefore : cmd.sketchAfter;
        s.entities = c.entities;
        s.constraints = c.constraints;
        s.groups = c.groups;
        project.setSketch(at, s);
        return true;
    }

    default: {
        std::vector<FeatureData> features = project.features();
        if (!(undo ? applyUndo(cmd, features) : applyRedo(cmd, features))) return false;
        project.setFeatures(features);
        return true;
    }
    }
}

// ---- Equality ----------------------------------------------------------
//
// The sketch types have no operator==, and finishing an edit must record
// nothing when nothing changed, or opening a sketch and closing it again
// costs an undo step. The GUI once compared list SIZES, so moving a point
// was not an edit at all. Caches and display state (outline caches, solver
// feedback, tree expansion) are left out on purpose.

template <typename T, typename Eq>
bool sameList(const std::vector<T>& a, const std::vector<T>& b, Eq eq)
{
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (!eq(a[i], b[i])) return false;
    }
    return true;
}

bool sameNumber(double a, double b)
{
    return a == b || (std::isnan(a) && std::isnan(b));
}

bool sameEntity(const sketch::Entity& a, const sketch::Entity& b)
{
    return a.id == b.id && a.type == b.type && a.points == b.points
        && a.radius == b.radius && a.startAngle == b.startAngle
        && a.sweepAngle == b.sweepAngle && a.sides == b.sides
        && a.majorRadius == b.majorRadius && a.minorRadius == b.minorRadius
        && a.ellipseRotation == b.ellipseRotation && a.ellipseStart == b.ellipseStart
        && a.ellipseSweep == b.ellipseSweep && a.text == b.text
        && a.fontFamily == b.fontFamily && a.fontSize == b.fontSize
        && a.fontBold == b.fontBold && a.fontItalic == b.fontItalic
        && a.textRotation == b.textRotation && a.arcFlipped == b.arcFlipped
        && a.splineBezier == b.splineBezier && a.splineClosed == b.splineClosed
        && a.splineRational == b.splineRational && a.weights == b.weights
        && a.pathEntityIds == b.pathEntityIds && a.offsetParentId == b.offsetParentId
        && a.offsetDistance == b.offsetDistance && a.offsetSide == b.offsetSide
        && a.projectionSourceId == b.projectionSourceId
        && a.projectionSourceSketchId == b.projectionSourceSketchId
        && a.isConstruction == b.isConstruction && a.isCenterline == b.isCenterline
        && a.color == b.color && a.constrained == b.constrained
        && a.groupId == b.groupId;
}

bool sameConstraint(const sketch::Constraint& a, const sketch::Constraint& b)
{
    return a.id == b.id && a.type == b.type && a.entityIds == b.entityIds
        && a.pointIndices == b.pointIndices && sameNumber(a.value, b.value)
        && a.expression == b.expression && a.isDriving == b.isDriving
        && a.enabled == b.enabled && a.labelPosition == b.labelPosition
        && a.labelVisible == b.labelVisible && sameNumber(a.labelAngle, b.labelAngle)
        && a.supplementary == b.supplementary;
}

bool sameGroup(const sketch::Group& a, const sketch::Group& b)
{
    return a.id == b.id && a.name == b.name && a.kind == b.kind
        && a.entityIds == b.entityIds && a.constraintIds == b.constraintIds
        && a.childGroupIds == b.childGroupIds && a.parentGroupId == b.parentGroupId
        && a.locked == b.locked && a.hasPivot == b.hasPivot && a.pivot == b.pivot;
}

bool sameBackground(const sketch::BackgroundImage& a, const sketch::BackgroundImage& b)
{
    return a.enabled == b.enabled && a.storage == b.storage && a.filePath == b.filePath
        && a.imageData == b.imageData && a.mimeType == b.mimeType
        && a.position == b.position && a.width == b.width && a.height == b.height
        && a.rotation == b.rotation && a.opacity == b.opacity
        && a.lockAspectRatio == b.lockAspectRatio && a.flipHorizontal == b.flipHorizontal
        && a.flipVertical == b.flipVertical && a.grayscale == b.grayscale
        && a.contrast == b.contrast && a.brightness == b.brightness;
}

bool sameSketch(const SketchData& a, const SketchData& b)
{
    return a.name == b.name && a.plane == b.plane
        && a.constructionPlaneId == b.constructionPlaneId
        && a.planeOffset == b.planeOffset && a.rotationAxis == b.rotationAxis
        && a.rotationAngle == b.rotationAngle && a.gridSpacing == b.gridSpacing
        && a.flipView == b.flipView
        && sameList(a.entities, b.entities, sameEntity)
        && sameList(a.constraints, b.constraints, sameConstraint)
        && sameList(a.groups, b.groups, sameGroup)
        && sameBackground(a.backgroundImage, b.backgroundImage);
}

DocumentCommand oneOrCompound(std::vector<DocumentCommand> steps, const std::string& desc)
{
    if (steps.size() == 1) return std::move(steps.front());
    return DocumentCommand::compound(steps, desc);
}

// ---- Model operations ----------------------------------------------------

/// A solid built on XY, moved onto the sketch's plane. The profile builders
/// work in the XY plane, and the GUI used to hand them only a direction, so
/// a sketch on XZ or YZ extruded from XY.
TopoDS_Shape placeOnSketch(const TopoDS_Shape& shape, const SketchData& sketch,
                           const Project& project)
{
    gp_Trsf trsf;
    trsf.SetDisplacement(gp_Ax3(gp::XOY()), sketchFrame(sketch, project));
    BRepBuilderAPI_Transform xf(shape, trsf, true);
    return xf.IsDone() ? xf.Shape() : TopoDS_Shape();
}

/// Combine a new solid with the last body, or add it as a body of its own.
/// With no body yet, every operation makes one, as the GUI always did.
bool combineIntoBodies(Project& project, const TopoDS_Shape& solid,
                       BodyOperation op, int& bodyId, std::string& error)
{
    std::vector<BodyData> bodies = project.bodies();
    if (op == BodyOperation::NewBody || bodies.empty()) {
        bodyId = project.addBody(solid);
        return true;
    }

    BodyData& target = bodies.back();
    brep::OperationResult r;
    switch (op) {
    case BodyOperation::Join:      r = brep::fuseShapes(target.shape, solid); break;
    case BodyOperation::Cut:       r = brep::cutShape(target.shape, solid); break;
    case BodyOperation::Intersect: r = brep::intersectShapes(target.shape, solid); break;
    case BodyOperation::NewBody:   break;
    }
    if (!r.success) {
        error = r.errorMessage.empty() ? "the boolean operation failed" : r.errorMessage;
        return false;
    }
    target.shape = r.shape;
    bodyId = target.id;
    project.setBodies(bodies);
    return true;
}

std::string uniqueFeatureName(const Project& project, const std::string& stem)
{
    for (int n = 1;; ++n) {
        const std::string name = stem + std::to_string(n);
        bool taken = false;
        for (const FeatureData& f : project.features()) {
            if (f.name == name) { taken = true; break; }
        }
        if (!taken) return name;
    }
}

}  // namespace

// ========================================================================

ProjectSession::ProjectSession(Project& project)
    : m_project(project)
{
}

// ---- History ---------------------------------------------------------------

void ProjectSession::record(const DocumentCommand& command)
{
    m_history.push(command);
}

bool ProjectSession::undo(std::string* description)
{
    if (!m_history.canUndo()) return false;
    const DocumentCommand cmd = m_history.undo();
    const ProjectState before = capture(m_project);
    if (!applyStep(cmd, m_project, true)) {
        restore(m_project, before);
        m_history.redo();          // put it back; nothing changed
        return false;
    }
    if (description) *description = cmd.description;
    return true;
}

bool ProjectSession::redo(std::string* description)
{
    if (!m_history.canRedo()) return false;
    const DocumentCommand cmd = m_history.redo();
    const ProjectState before = capture(m_project);
    if (!applyStep(cmd, m_project, false)) {
        restore(m_project, before);
        m_history.undo();
        return false;
    }
    if (description) *description = cmd.description;
    return true;
}

// ---- Identity and lookup -----------------------------------------------------

int ProjectSession::nextFeatureId() const
{
    int next = 1;
    for (const FeatureData& f : m_project.features()) next = std::max(next, f.id + 1);
    for (const SketchData& s : m_project.sketches()) next = std::max(next, s.id + 1);
    return next;
}

const SketchData* ProjectSession::sketchById(int id) const
{
    const int at = indexOfSketch(m_project.sketches(), id);
    return at < 0 ? nullptr : &m_project.sketches()[static_cast<std::size_t>(at)];
}

const FeatureData* ProjectSession::featureById(int id) const
{
    const int at = indexOfFeature(m_project.features(), id);
    return at < 0 ? nullptr : &m_project.features()[static_cast<std::size_t>(at)];
}

std::string ProjectSession::nextSketchName() const
{
    const auto& sketches = m_project.sketches();
    for (int n = static_cast<int>(sketches.size()) + 1;; ++n) {
        const std::string name = "Sketch" + std::to_string(n);
        bool taken = false;
        for (const SketchData& s : sketches) {
            if (s.name == name) { taken = true; break; }
        }
        if (!taken) return name;
    }
}

void ProjectSession::adoptOrphanSketches()
{
    const bool wasModified = m_project.isModified();
    std::vector<FeatureData> features = m_project.features();
    bool added = false;
    for (const SketchData& s : m_project.sketches()) {
        // An id already held by another kind of feature is left alone:
        // renumbering the sketch would break what refers to it.
        if (indexOfFeature(features, s.id) >= 0) continue;
        FeatureData f;
        f.id = s.id;
        f.type = FeatureType::Sketch;
        f.name = s.name;
        features.push_back(f);
        added = true;
    }
    if (added) {
        m_project.setFeatures(features);
        m_project.setModified(wasModified);
    }
}

// ---- Timeline --------------------------------------------------------------

std::vector<TimelineEntry> ProjectSession::timeline() const
{
    // One source. Full mode used to add a timeline item per stored sketch AND
    // per feature record, so a sketch with a record was listed twice.
    std::vector<TimelineEntry> out;
    for (const FeatureData& f : m_project.features()) {
        if (f.type == FeatureType::Origin) continue;
        TimelineEntry e{f.id, f.type, f.name, f.suppressed, f.dependsOn};
        if (f.type == FeatureType::Sketch) {
            const SketchData* s = sketchById(f.id);
            if (!s) continue;              // a record whose sketch is gone
            e.name = s->name;
        }
        out.push_back(std::move(e));
    }
    for (const SketchData& s : m_project.sketches()) {
        const FeatureData* f = featureById(s.id);
        if (f && f->type == FeatureType::Sketch) continue;
        out.push_back({s.id, FeatureType::Sketch, s.name, false, {}});
    }
    return out;
}

// ---- Sketches --------------------------------------------------------------

SketchDraft ProjectSession::beginSketch(SketchPlane plane, double offset,
                                        PlaneRotationAxis axis, double angle,
                                        int constructionPlaneId,
                                        const std::string& name) const
{
    SketchDraft d;
    d.sketch.plane = plane;
    d.sketch.planeOffset = offset;
    d.sketch.rotationAxis = axis;
    d.sketch.rotationAngle = angle;
    d.sketch.constructionPlaneId = constructionPlaneId;
    d.sketch.name = name.empty() ? nextSketchName() : name;
    // Reserved, not held: finishSketch() takes another if it was used since.
    d.sketch.id = nextFeatureId();
    return d;
}

bool ProjectSession::beginEditSketch(int sketchId, SketchDraft& out) const
{
    const SketchData* s = sketchById(sketchId);
    if (!s) return false;
    out.sketch = *s;
    out.editingId = sketchId;
    return true;
}

int ProjectSession::finishSketch(const SketchDraft& draft, const std::string& description)
{
    SketchData s = draft.sketch;

    if (draft.isNew()) {
        if (s.id <= 0 || sketchById(s.id) || featureById(s.id)) s.id = nextFeatureId();
        if (s.name.empty()) s.name = nextSketchName();
        const std::string desc = description.empty()
            ? "Create sketch '" + s.name + "'" : description;

        const std::vector<SketchData> sketchesBefore = m_project.sketches();
        m_project.addSketch(s);

        FeatureData f;
        f.id = s.id;
        f.type = FeatureType::Sketch;
        f.name = s.name;
        std::vector<FeatureData> features = m_project.features();
        const int index = static_cast<int>(features.size());
        features.push_back(f);
        m_project.setFeatures(features);

        record(DocumentCommand::compound(
            {makeSketchListCommand(sketchesBefore, m_project.sketches(), desc),
             DocumentCommand::addFeature(f, index, desc)},
            desc));
        return s.id;
    }

    const int at = indexOfSketch(m_project.sketches(), draft.editingId);
    if (at < 0) return -1;
    const std::vector<SketchData> sketchesBefore = m_project.sketches();
    const SketchData& stored = sketchesBefore[static_cast<std::size_t>(at)];
    s.id = stored.id;
    s.designId = stored.designId;
    if (sameSketch(stored, s)) return s.id;

    const std::string desc = description.empty() ? "Edit sketch '" + s.name + "'" : description;
    m_project.setSketch(at, s);
    std::vector<DocumentCommand> steps{
        makeSketchListCommand(sketchesBefore, m_project.sketches(), desc)};

    // A sketch renamed while open renames its record too.
    std::vector<FeatureData> features = m_project.features();
    const int fAt = indexOfFeature(features, s.id);
    if (fAt >= 0 && features[static_cast<std::size_t>(fAt)].type == FeatureType::Sketch
        && features[static_cast<std::size_t>(fAt)].name != s.name) {
        const FeatureData before = features[static_cast<std::size_t>(fAt)];
        FeatureData after = before;
        after.name = s.name;
        features[static_cast<std::size_t>(fAt)] = after;
        m_project.setFeatures(features);
        steps.push_back(DocumentCommand::modifyFeature(before, after, desc));
    }
    record(oneOrCompound(std::move(steps), desc));
    return s.id;
}

std::vector<SketchData> ProjectSession::sketchesWith(const SketchDraft& draft) const
{
    std::vector<SketchData> out = m_project.sketches();
    SketchData s = draft.sketch;
    if (draft.isNew()) {
        if (s.id <= 0 || sketchById(s.id) || featureById(s.id)) s.id = nextFeatureId();
        if (s.name.empty()) s.name = nextSketchName();
        if (s.designId <= 0) s.designId = m_project.activeDesignId();
        out.push_back(s);
        return out;
    }
    for (SketchData& e : out) {
        if (e.id == draft.editingId) {
            s.id = e.id;
            s.designId = e.designId;
            e = s;
            break;
        }
    }
    return out;
}

bool ProjectSession::save(const std::string& path, std::string* errorMsg,
                          const SketchDraft* openDraft)
{
    if (!openDraft) return m_project.save(path, errorMsg);

    // Save is a checkpoint, never gated, so the sketch on the canvas goes
    // into the file as it stands. It is written, not stored: Discard must
    // still be able to leave the project as it was before the sketch.
    const bool wasModified = m_project.isModified();
    const std::vector<SketchData> stored = m_project.sketches();
    m_project.setSketches(sketchesWith(*openDraft));
    const bool ok = m_project.save(path, errorMsg);
    m_project.setSketches(stored);
    m_project.setModified(ok ? false : wasModified);
    return ok;
}

// ---- Features --------------------------------------------------------------

int ProjectSession::addFeature(FeatureData feature, const std::string& description)
{
    if (feature.id <= 0 || featureById(feature.id) || sketchById(feature.id)) {
        feature.id = nextFeatureId();
    }
    std::vector<FeatureData> features = m_project.features();
    const int index = static_cast<int>(features.size());
    features.push_back(feature);
    m_project.setFeatures(features);
    record(DocumentCommand::addFeature(feature, index, description));
    return feature.id;
}

bool ProjectSession::modifyFeature(int id, const std::function<void(FeatureData&)>& edit,
                                   const std::string& description)
{
    std::vector<FeatureData> features = m_project.features();
    const int at = indexOfFeature(features, id);
    if (at < 0 || !edit) return false;

    const FeatureData before = features[static_cast<std::size_t>(at)];
    FeatureData after = before;
    edit(after);
    after.id = before.id;
    if (after.type == before.type && after.name == before.name
        && after.suppressed == before.suppressed && after.state == before.state
        && after.stateMessage == before.stateMessage
        && after.dependsOn == before.dependsOn && after.properties == before.properties) {
        return false;
    }
    features[static_cast<std::size_t>(at)] = after;
    m_project.setFeatures(features);
    record(DocumentCommand::modifyFeature(before, after, description));
    return true;
}

bool ProjectSession::renameFeature(int id, const std::string& name,
                                   const std::string& description)
{
    std::vector<FeatureData> features = m_project.features();
    const int fAt = indexOfFeature(features, id);
    const int sAt = indexOfSketch(m_project.sketches(), id);
    const bool isSketch = sAt >= 0
        && (fAt < 0 || features[static_cast<std::size_t>(fAt)].type == FeatureType::Sketch);
    if (fAt < 0 && !isSketch) return false;

    const std::string desc = description.empty() ? "Rename to '" + name + "'" : description;
    std::vector<DocumentCommand> steps;
    if (isSketch && m_project.sketches()[static_cast<std::size_t>(sAt)].name != name) {
        const std::vector<SketchData> before = m_project.sketches();
        SketchData s = before[static_cast<std::size_t>(sAt)];
        s.name = name;
        m_project.setSketch(sAt, s);
        steps.push_back(makeSketchListCommand(before, m_project.sketches(), desc));
    }
    if (fAt >= 0 && features[static_cast<std::size_t>(fAt)].name != name) {
        const FeatureData before = features[static_cast<std::size_t>(fAt)];
        FeatureData after = before;
        after.name = name;
        features[static_cast<std::size_t>(fAt)] = after;
        m_project.setFeatures(features);
        steps.push_back(DocumentCommand::modifyFeature(before, after, desc));
    }
    if (!steps.empty()) record(oneOrCompound(std::move(steps), desc));
    return true;
}

bool ProjectSession::setFeatureSuppressed(int id, bool suppressed,
                                          const std::string& description)
{
    return modifyFeature(id, [suppressed](FeatureData& f) { f.suppressed = suppressed; },
                         description);
}

bool ProjectSession::deleteFeature(int id, const std::string& description)
{
    std::vector<FeatureData> features = m_project.features();
    const int fAt = indexOfFeature(features, id);
    const int sAt = indexOfSketch(m_project.sketches(), id);
    const bool isSketch = sAt >= 0
        && (fAt < 0 || features[static_cast<std::size_t>(fAt)].type == FeatureType::Sketch);
    if (fAt < 0 && !isSketch) return false;

    const std::string name = isSketch
        ? m_project.sketches()[static_cast<std::size_t>(sAt)].name
        : features[static_cast<std::size_t>(fAt)].name;
    const std::string desc = description.empty() ? "Delete '" + name + "'" : description;

    std::vector<DocumentCommand> steps;
    if (isSketch) {
        const std::vector<SketchData> before = m_project.sketches();
        m_project.removeSketch(sAt);
        steps.push_back(makeSketchListCommand(before, m_project.sketches(), desc));
    }
    if (fAt >= 0) {
        const FeatureData removed = features[static_cast<std::size_t>(fAt)];
        features.erase(features.begin() + fAt);
        m_project.setFeatures(features);
        steps.push_back(DocumentCommand::deleteFeature(removed, fAt, desc));
    }
    record(oneOrCompound(std::move(steps), desc));
    return true;
}

bool ProjectSession::moveFeature(int id, int toTimelineIndex, const std::string& description)
{
    std::vector<TimelineEntry> entries = timeline();
    int from = -1;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].featureId == id) { from = static_cast<int>(i); break; }
    }
    const int count = static_cast<int>(entries.size());
    if (from < 0 || toTimelineIndex < 0 || toTimelineIndex >= count || from == toTimelineIndex) {
        return false;
    }

    std::vector<FeatureData> features = m_project.features();
    const int fromR = indexOfFeature(features, id);
    if (fromR < 0) return false;          // a sketch with no record has no place to move

    std::vector<int> order;
    for (const TimelineEntry& e : entries) order.push_back(e.featureId);
    order.erase(order.begin() + from);
    order.insert(order.begin() + toTimelineIndex, id);

    const FeatureData moved = features[static_cast<std::size_t>(fromR)];
    features.erase(features.begin() + fromR);

    // Land in front of the next entry in the new order that has a record,
    // or at the end when none follows.
    int toR = static_cast<int>(features.size());
    for (std::size_t k = static_cast<std::size_t>(toTimelineIndex) + 1; k < order.size(); ++k) {
        const int at = indexOfFeature(features, order[k]);
        if (at >= 0) { toR = at; break; }
    }
    features.insert(features.begin() + toR, moved);
    if (toR == fromR) return false;
    m_project.setFeatures(features);
    record(DocumentCommand::reorderFeature(fromR, toR,
        description.empty() ? "Move '" + moved.name + "'" : description));
    return true;
}

// ---- Bodies ----------------------------------------------------------------

bool ProjectSession::renameBody(int bodyId, const std::string& name,
                                const std::string& description)
{
    std::vector<BodyData> bodies = m_project.bodies();
    for (BodyData& b : bodies) {
        if (b.id != bodyId) continue;
        if (b.name == name) return true;
        const std::vector<BodyData> before = m_project.bodies();
        b.name = name;
        m_project.setBodies(bodies);
        record(makeBodyListCommand(before, m_project.bodies(),
            description.empty() ? "Rename to '" + name + "'" : description));
        return true;
    }
    return false;
}

ModelResult ProjectSession::extrudeSketch(int sketchId, double distance,
                                          ExtrudeExtent extent, BodyOperation operation,
                                          const std::string& description)
{
    ModelResult res;
    const SketchData* sk = sketchById(sketchId);
    if (!sk) { res.error = "there is no sketch with id " + std::to_string(sketchId); return res; }
    if (!(distance > 0.0)) { res.error = "the distance must be greater than zero"; return res; }

    sketch::ProfileDetectionOptions options;
    options.excludeConstruction = true;
    const std::vector<sketch::Profile> profiles = sketch::detectProfiles(sk->entities, options);
    if (profiles.empty()) { res.error = "the sketch has no closed profile"; return res; }

    const gp_Dir up(0, 0, extent == ExtrudeExtent::Reverse ? -1 : 1);
    const brep::OperationResult built = extent == ExtrudeExtent::Symmetric
        ? brep::extrudeProfileSymmetric(profiles[0], sk->entities, up, distance, true)
        : brep::extrudeProfile(profiles[0], sk->entities, up, distance);
    if (!built.success) { res.error = built.errorMessage; return res; }

    const TopoDS_Shape placed = placeOnSketch(built.shape, *sk, m_project);
    if (placed.IsNull()) { res.error = "the solid could not be placed on the sketch plane"; return res; }

    const std::string sketchName = sk->name;
    const std::vector<BodyData> bodiesBefore = m_project.bodies();
    int bodyId = -1;
    if (!combineIntoBodies(m_project, placed, operation, bodyId, res.error)) return res;

    FeatureData f;
    f.id = nextFeatureId();
    f.type = FeatureType::Extrude;
    f.name = uniqueFeatureName(m_project, "Extrude");
    f.dependsOn = {sketchId};
    f.properties["distance"] = distance;
    f.properties["direction"] = static_cast<int>(extent);
    f.properties["operation"] = static_cast<int>(operation);
    f.properties["sketch_feature_id"] = sketchId;
    f.properties["body_id"] = bodyId;

    std::vector<FeatureData> features = m_project.features();
    const int index = static_cast<int>(features.size());
    features.push_back(f);
    m_project.setFeatures(features);

    const std::string desc = description.empty() ? "Extrude " + sketchName : description;
    record(DocumentCommand::compound(
        {makeBodyListCommand(bodiesBefore, m_project.bodies(), desc),
         DocumentCommand::addFeature(f, index, desc)},
        desc));

    res.ok = true;
    res.featureId = f.id;
    res.bodyId = bodyId;
    return res;
}

ModelResult ProjectSession::revolveSketch(int sketchId, double angleDegrees,
                                          RevolveAxisKind axis, int axisLineId,
                                          BodyOperation operation,
                                          const std::string& description)
{
    ModelResult res;
    const SketchData* sk = sketchById(sketchId);
    if (!sk) { res.error = "there is no sketch with id " + std::to_string(sketchId); return res; }
    if (!(angleDegrees > 0.0) || angleDegrees > 360.0) {
        res.error = "the angle must be greater than 0 and at most 360 degrees";
        return res;
    }

    gp_Ax1 turn;
    switch (axis) {
    case RevolveAxisKind::SketchXAxis: turn = gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(1, 0, 0)); break;
    case RevolveAxisKind::SketchYAxis: turn = gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(0, 1, 0)); break;
    case RevolveAxisKind::SketchLine: {
        const sketch::Entity* line = nullptr;
        for (const sketch::Entity& e : sk->entities) {
            if (e.id == axisLineId && e.type == sketch::EntityType::Line) { line = &e; break; }
        }
        if (!line || line->points.size() < 2) {
            res.error = "the axis must be a line of the sketch";
            return res;
        }
        const double dx = line->points[1].x - line->points[0].x;
        const double dy = line->points[1].y - line->points[0].y;
        if (std::hypot(dx, dy) <= 0.0) { res.error = "the axis line has no length"; return res; }
        turn = gp_Ax1(gp_Pnt(line->points[0].x, line->points[0].y, 0), gp_Dir(dx, dy, 0));
        break;
    }
    }

    sketch::ProfileDetectionOptions options;
    options.excludeConstruction = true;
    const std::vector<sketch::Profile> profiles = sketch::detectProfiles(sk->entities, options);
    if (profiles.empty()) { res.error = "the sketch has no closed profile"; return res; }

    const brep::OperationResult built =
        brep::revolveProfile(profiles[0], sk->entities, turn, angleDegrees);
    if (!built.success) { res.error = built.errorMessage; return res; }

    const TopoDS_Shape placed = placeOnSketch(built.shape, *sk, m_project);
    if (placed.IsNull()) { res.error = "the solid could not be placed on the sketch plane"; return res; }

    const std::string sketchName = sk->name;
    const std::vector<BodyData> bodiesBefore = m_project.bodies();
    int bodyId = -1;
    if (!combineIntoBodies(m_project, placed, operation, bodyId, res.error)) return res;

    // The axis is stored in the sketch's own plane, as a point and a
    // direction: the recipe is JSON and must not depend on an OCCT type.
    FeatureData f;
    f.id = nextFeatureId();
    f.type = FeatureType::Revolve;
    f.name = uniqueFeatureName(m_project, "Revolve");
    f.dependsOn = {sketchId};
    f.properties["angle"] = angleDegrees;
    f.properties["operation"] = static_cast<int>(operation);
    f.properties["sketch_feature_id"] = sketchId;
    f.properties["body_id"] = bodyId;
    f.properties["axis_kind"] = static_cast<int>(axis);
    f.properties["axis_line_id"] = axisLineId;
    f.properties["axis_x"] = turn.Location().X();
    f.properties["axis_y"] = turn.Location().Y();
    f.properties["axis_z"] = turn.Location().Z();
    f.properties["axis_dx"] = turn.Direction().X();
    f.properties["axis_dy"] = turn.Direction().Y();
    f.properties["axis_dz"] = turn.Direction().Z();

    std::vector<FeatureData> features = m_project.features();
    const int index = static_cast<int>(features.size());
    features.push_back(f);
    m_project.setFeatures(features);

    const std::string desc = description.empty() ? "Revolve " + sketchName : description;
    record(DocumentCommand::compound(
        {makeBodyListCommand(bodiesBefore, m_project.bodies(), desc),
         DocumentCommand::addFeature(f, index, desc)},
        desc));

    res.ok = true;
    res.featureId = f.id;
    res.bodyId = bodyId;
    return res;
}

}  // namespace hobbycad
