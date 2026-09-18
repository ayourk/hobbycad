// =====================================================================
//  src/libhobbycad/hobbycad/project_session.h — edits to an open project
//  Repository: ayourk/hobbycad
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//
//  Aaron, 2026-09-15: "Reduced mode should work on the same backend as full
//  mode and CLI", and "the backend refactor should allow for more
//  frontends, such as the potential for a web front end."
//
//  One ProjectSession per open project, and every front end edits the
//  project through it: the GUI in Full and Reduced mode, the CLI, a future
//  web front end. Sketches are begun and finished here; features renamed,
//  suppressed, moved and deleted here; bodies extruded and revolved here;
//  and each edit lands in the one undo history the session owns. A front
//  end keeps only what it draws and the sketch draft it has open.
//
//  It used to be otherwise. Full mode kept its sketches in a list of its
//  own that reached the Project only at save, Reduced mode kept none, and
//  the CLI wrote the Project directly and recorded its undo in another
//  form. A sketch finished at the GUI's terminal never reached Full mode's
//  list, so the next Save erased it.
//
// =====================================================================

#ifndef HOBBYCAD_PROJECT_SESSION_H
#define HOBBYCAD_PROJECT_SESSION_H

#include "core.h"
#include "document_undo.h"
#include "project.h"

#include <functional>
#include <string>
#include <vector>

namespace hobbycad {

/// A sketch being drawn or edited.
///
/// Owned by the front end that opened it, so two front ends on one project
/// (the canvas and the GUI's terminal) never share a half-drawn sketch.
struct HOBBYCAD_EXPORT SketchDraft {
    SketchData sketch;     ///< Plane, contents and name as they stand
    int editingId = -1;    ///< Stored sketch being edited; -1 for a new one

    bool isNew() const { return editingId < 0; }
};

/// One row of the modeling history, in order. The Origin is not listed.
struct HOBBYCAD_EXPORT TimelineEntry {
    int featureId = 0;
    FeatureType type = FeatureType::Origin;
    std::string name;
    bool suppressed = false;
    std::vector<int> dependsOn;
};

/// True when the history lets the entry at `from` move to `to` (positions in
/// `entries`): never past a feature that depends on it, nor ahead of one it
/// depends on. A move to where it is is allowed.
HOBBYCAD_EXPORT bool timelineMoveAllowed(const std::vector<TimelineEntry>& entries, int from,
                                         int to);

/// What a timeline row offers.
struct TimelineActions {
    bool editable = false;       ///< edit, rename, suppress, roll back to, delete
    bool unsuppress = false;     ///< the suppress toggle reads Unsuppress
    bool exportable = false;     ///< export as DXF or SVG (a sketch)
};

/// The actions for a row of `type`, suppressed or not. The Origin offers
/// none.
HOBBYCAD_EXPORT TimelineActions timelineActions(FeatureType type, bool suppressed);

/// How a new solid combines with the bodies already there.
enum class BodyOperation {
    NewBody,     ///< A body of its own
    Join,        ///< Fused with the last body
    Cut,         ///< Subtracted from the last body
    Intersect,   ///< Intersected with the last body
};

/// Which way an extrusion leaves the sketch plane.
enum class ExtrudeExtent {
    Normal,      ///< Along the plane normal
    Reverse,     ///< Against it
    Symmetric,   ///< Half the distance each way
};

/// The axis a revolve turns about, in the sketch's own plane.
enum class RevolveAxisKind {
    SketchXAxis,  ///< The sketch's x axis through its origin
    SketchYAxis,  ///< The sketch's y axis through its origin
    SketchLine,   ///< A line of the sketch
};

/// What a model operation did.
struct HOBBYCAD_EXPORT ModelResult {
    bool ok = false;
    int featureId = -1;     ///< The feature recorded
    int bodyId = -1;        ///< The body made or changed
    std::string error;      ///< Why not, when !ok
};

class HOBBYCAD_EXPORT ProjectSession {
public:
    explicit ProjectSession(Project& project);

    Project& project() { return m_project; }
    const Project& project() const { return m_project; }

    // ---- History ----------------------------------------------------

    /// Record an edit the caller already made to the project.
    void record(const DocumentCommand& command);

    /// Reverse or reapply the newest command. All or nothing: a command
    /// that does not fit the project leaves it untouched and stays on its
    /// stack, so the history and the model never disagree.
    bool undo(std::string* description = nullptr);
    bool redo(std::string* description = nullptr);

    const DocumentUndoStack& history() const { return m_history; }

    /// Forget the history, as when another project is opened.
    void clearHistory() { m_history.clear(); }

    // ---- Identity and lookup ---------------------------------------

    /// A feature id no feature or sketch uses. Sketches and features share
    /// one id space: a sketch's id is its feature record's id.
    int nextFeatureId() const;
    const SketchData* sketchById(int id) const;
    const FeatureData* featureById(int id) const;

    /// "SketchN" for the lowest N, from the sketch count up, that no sketch
    /// is called.
    std::string nextSketchName() const;

    /// Give each stored sketch without a feature record one, at the end of
    /// the recipe. Projects written before sketches carried records have
    /// such sketches. Not an edit: nothing is recorded, and the modified
    /// flag is left as it was.
    void adoptOrphanSketches();

    // ---- Timeline ---------------------------------------------------

    /// The history in order: the recipe, each sketch feature named from its
    /// sketch, then any sketch still without a record.
    std::vector<TimelineEntry> timeline() const;

    // ---- Sketches ---------------------------------------------------

    /// A draft for a new sketch. Nothing is stored until finishSketch().
    SketchDraft beginSketch(SketchPlane plane, double offset = 0.0,
                            PlaneRotationAxis axis = PlaneRotationAxis::X,
                            double angle = 0.0, int constructionPlaneId = -1,
                            const std::string& name = {}) const;

    /// A draft of a stored sketch, for editing. False when there is none.
    bool beginEditSketch(int sketchId, SketchDraft& out) const;

    /// Store the draft and record it. A new sketch gets an id, a name when
    /// it has none, and a feature record; an edited one replaces the stored
    /// sketch, and nothing is recorded when nothing changed.
    /// @return the sketch id, or -1 when the edited sketch no longer exists.
    int finishSketch(const SketchDraft& draft, const std::string& description = {});

    /// The stored sketches with `draft` applied, without storing it: what a
    /// Save in the middle of a sketch writes.
    std::vector<SketchData> sketchesWith(const SketchDraft& draft) const;

    /// Save the project, writing `openDraft` as it stands when one is given.
    /// Saving does not store the draft in the project.
    bool save(const std::string& path, std::string* errorMsg = nullptr,
              const SketchDraft* openDraft = nullptr);

    // ---- Features ---------------------------------------------------

    /// Append a feature, assigning an id when it has none free, and record it.
    int addFeature(FeatureData feature, const std::string& description = {});

    /// Change one feature. Nothing is recorded when `edit` changes nothing.
    bool modifyFeature(int id, const std::function<void(FeatureData&)>& edit,
                       const std::string& description = {});

    /// Rename a feature, and its sketch when it is one.
    bool renameFeature(int id, const std::string& name,
                       const std::string& description = {});
    bool setFeatureSuppressed(int id, bool suppressed,
                              const std::string& description = {});

    /// Delete a feature, and its sketch when it is one.
    bool deleteFeature(int id, const std::string& description = {});

    /// Move a feature to a position in timeline(). Refused when a feature
    /// depends on it in between, or it depends on one (timelineMoveAllowed).
    bool moveFeature(int id, int toTimelineIndex,
                     const std::string& description = {});

    // ---- Bodies -----------------------------------------------------

    bool renameBody(int bodyId, const std::string& name,
                    const std::string& description = {});

    /// Extrude a sketch's first closed profile, placed on the sketch's own
    /// plane, and record the feature.
    ModelResult extrudeSketch(int sketchId, double distance, ExtrudeExtent extent,
                              BodyOperation operation,
                              const std::string& description = {});

    /// Revolve a sketch's first closed profile. `axisLineId` names the line
    /// when `axis` is SketchLine.
    ModelResult revolveSketch(int sketchId, double angleDegrees,
                              RevolveAxisKind axis, int axisLineId,
                              BodyOperation operation,
                              const std::string& description = {});

private:
    Project& m_project;
    DocumentUndoStack m_history;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_PROJECT_SESSION_H
