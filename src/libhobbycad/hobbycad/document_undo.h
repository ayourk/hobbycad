// =====================================================================
//  src/libhobbycad/hobbycad/document_undo.h — Document-level undo/redo
// =====================================================================
//
//  Undo/redo for the FEATURE RECIPE: adding, deleting, reordering,
//  suppressing and editing features. This is the OUTER stack.
//
//  Why this exists: until now the application had exactly one undo stack,
//  sketch::UndoStack, living on the SketchCanvas widget and connected only
//  in initSketchConnections(). Outside sketch editing there was no undo at
//  all: extrude, delete, rename and move were irreversible, and Ctrl+Z in
//  the 3D workspace silently did nothing.
//
//  Two deliberate design points:
//
//    * It lives in libhobbycad, NOT on a widget. The sketch stack is owned
//      by a QWidget and is wiped by enterSketchMode()'s clear(); a document's
//      history must outlive any particular view, and the CLI and a future
//      headless or wxWidgets front end need undo just as much as the GUI.
//
//    * The stack STORES commands; applying them is a separate free function
//      over a feature list. That keeps the container logic testable without
//      a document, a canvas or a GUI, and it means the CLI and the GUI
//      cannot drift into two different notions of what "undo" does.
//
//  Relationship to the inner stack: a whole sketch editing session should
//  collapse into ONE command here (see devdoc-timeline-undo.md). Use
//  beginCompound()/endCompound(), or push a Compound directly.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_DOCUMENT_UNDO_H
#define HOBBYCAD_DOCUMENT_UNDO_H

#include "core.h"
#include "feature.h"
#include "sketch/constraint.h"
#include "sketch/entity.h"
#include "sketch/group.h"

#include <memory>
#include <string>
#include <vector>

namespace hobbycad {

/// Kinds of document-level operation.
///
/// Deliberately small. Rename, parameter edit and suppress toggle are all
/// ModifyFeature; they differ only in which field of FeatureData changed,
/// and giving each its own type would multiply the apply logic for no gain.
enum class DocumentCommandType {
    AddFeature,      ///< A feature was appended or inserted
    DeleteFeature,   ///< A feature was removed
    ModifyFeature,   ///< Any field changed: name, properties, suppressed, state
    ReorderFeature,  ///< A feature moved position in the recipe
    ModifySketch,    ///< A sketch's CONTENTS changed (its entities/constraints)

    /// One of the project's object lists changed as a whole.
    ///
    /// The recipe is not the only place a project keeps things. Sketches,
    /// bodies and construction planes live in Project's own vectors, and
    /// the CLI edits those DIRECTLY: Project::addSketch() does not put
    /// anything in the recipe, so a CLI sketch has no FeatureData for the
    /// commands above to act on. Undoing "delete sketch Foo" therefore
    /// cannot be expressed as a DeleteFeature.
    ///
    /// Snapshots the affected list whole, before and after, following the
    /// precedent set by ModifySketch: a correct snapshot beats a clever
    /// delta, and one list is a bounded amount of copying.
    ModifyProjectList,

    Compound         ///< Several operations that undo as one (e.g. a sketch edit)
};

/// Which of Project's object lists a ModifyProjectList command carries.
enum class ProjectListKind {
    Sketches,
    Bodies,
    Planes,
    Parameters,
};

/// The before/after lists a ModifyProjectList command carries.
///
/// Deliberately opaque here. Defining it would mean including project.h,
/// and through BodyData's TopoDS_Shape that pulls the whole OCCT kernel
/// into every consumer of this header; undo bookkeeping should not
/// require the 3D kernel to compile or link, and a test that needed
/// neither started needing both the moment it did. The definition, and
/// the functions that build and apply these commands, live in
/// project_undo.h.
struct ProjectListSnapshot;

/// One reversible document operation.
///
/// Carries enough to go both directions: `feature` is the state after the
/// operation, `previousFeature` the state before. A DeleteFeature stores the
/// whole record so undo can put it back intact rather than reconstruct it.
struct HOBBYCAD_EXPORT DocumentCommand {
    DocumentCommandType type = DocumentCommandType::ModifyFeature;
    std::string description;          ///< Shown in the UI and by the CLI

    FeatureData feature;              ///< State after the operation
    FeatureData previousFeature;      ///< State before (ModifyFeature only)

    /// Position in the recipe. For Add/Delete this is where it went or came
    /// from: undoing a delete must restore ORDER, not merely existence.
    int index = -1;
    int previousIndex = -1;           ///< ReorderFeature: where it came from

    /// ModifySketch payload.
    ///
    /// A sketch's "parameters" ARE its geometry, and that geometry lives in
    /// SketchData rather than in FeatureData::properties, so a rename-style
    /// ModifyFeature cannot express "the user changed the sketch". Snapshots
    /// are stored whole rather than as a diff: sketches hold tens of
    /// entities, and a correct snapshot is worth more than a clever delta.
    struct SketchContents {
        std::vector<sketch::Entity> entities;
        std::vector<sketch::Constraint> constraints;
        std::vector<sketch::Group> groups;
    };
    int sketchFeatureId = -1;      ///< Which sketch (ModifySketch only)
    SketchContents sketchBefore;
    SketchContents sketchAfter;

    /// ModifyProjectList payload, or null. An explicit `listKind` is what
    /// keeps "the sketch list is now empty" distinguishable from "this
    /// command is not about sketches".
    ///
    /// Shared rather than copied: a recorded snapshot never changes, and
    /// commands are copied by value into and out of the stack. shared_ptr
    /// to an incomplete type is fine: the deleter is captured where the
    /// type IS complete, in project_undo.cpp.
    ProjectListKind listKind = ProjectListKind::Sketches;
    std::shared_ptr<const ProjectListSnapshot> projectSnapshot;

    std::vector<DocumentCommand> subCommands;  ///< Compound only

    bool isCompound() const { return type == DocumentCommandType::Compound; }

    /// How many atomic operations this represents (recursive).
    int operationCount() const;

    static DocumentCommand addFeature(const FeatureData& f, int index,
                                      const std::string& desc = {});
    static DocumentCommand deleteFeature(const FeatureData& f, int index,
                                         const std::string& desc = {});
    static DocumentCommand modifyFeature(const FeatureData& before,
                                         const FeatureData& after,
                                         const std::string& desc = {});
    static DocumentCommand reorderFeature(int fromIndex, int toIndex,
                                          const std::string& desc = {});
    static DocumentCommand modifySketch(int sketchFeatureId,
                                        const SketchContents& before,
                                        const SketchContents& after,
                                        const std::string& desc = {});
    static DocumentCommand compound(const std::vector<DocumentCommand>& cmds,
                                    const std::string& desc = {});

};

/// Reverse `cmd` against `features`. Returns false and leaves the list
/// UNTOUCHED if the command does not fit it (stale index, missing id).
///
/// Failing rather than guessing is deliberate: silently applying a stale
/// undo to the wrong feature is far worse than refusing to apply it.
HOBBYCAD_EXPORT bool applyUndo(const DocumentCommand& cmd,
                               std::vector<FeatureData>& features);

/// Re-apply `cmd` against `features`. Same contract as applyUndo().
HOBBYCAD_EXPORT bool applyRedo(const DocumentCommand& cmd,
                               std::vector<FeatureData>& features);

/// True if `cmd` acts on the project's object lists rather than the recipe.
HOBBYCAD_EXPORT bool isProjectListCommand(const DocumentCommand& cmd);

/// Multi-level undo/redo for the feature recipe.
///
/// Mirrors sketch::UndoStack's shape on purpose, so the two behave alike
/// where they overlap.
class HOBBYCAD_EXPORT DocumentUndoStack {
public:
    explicit DocumentUndoStack(int maxSize = 100);

    /// Record a command. Clears the redo stack, as any new edit must.
    void push(const DocumentCommand& command);

    bool canUndo() const { return !m_undoStack.empty(); }
    bool canRedo() const { return !m_redoStack.empty(); }
    int  undoLevels() const { return static_cast<int>(m_undoStack.size()); }
    int  redoLevels() const { return static_cast<int>(m_redoStack.size()); }

    /// Move the top command to the redo stack and return it. The caller
    /// applies it; an empty description means the stack was empty.
    DocumentCommand undo();
    DocumentCommand redo();

    std::string undoDescription() const;
    std::string redoDescription() const;
    std::vector<std::string> undoDescriptions() const;
    std::vector<std::string> redoDescriptions() const;

    void clear();
    void clearRedo();

    int  maxSize() const { return m_maxSize; }
    void setMaxSize(int maxSize);

    bool isModified() const { return m_modified; }
    void setUnmodified() { m_modified = false; }

    /// Group everything pushed until endCompound() into one undo step.
    /// This is how a sketch editing session becomes a single outer command.
    void beginCompound(const std::string& description = {});
    void endCompound();
    bool isRecordingCompound() const { return m_recordingCompound; }

private:
    void trimToMaxSize();

    std::vector<DocumentCommand> m_undoStack;
    std::vector<DocumentCommand> m_redoStack;
    int  m_maxSize = 100;
    bool m_modified = false;

    bool m_recordingCompound = false;
    std::string m_compoundDescription;
    std::vector<DocumentCommand> m_compoundBuffer;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_DOCUMENT_UNDO_H
