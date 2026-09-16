// =====================================================================
//  src/hobbycad/cli/cliengine.h — Shared command dispatch engine
// =====================================================================
//
//  Provides command parsing and execution used by both the standalone
//  CLI REPL (CliMode) and the embedded GUI terminal panel (CliPanel).
//
//  All output is returned as a string rather than printed to stdout,
//  so callers can direct it wherever they need (terminal, QTextEdit,
//  log file, etc.).
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_CLIENGINE_H
#define HOBBYCAD_CLIENGINE_H

#include <hobbycad/document_host.h>
#include <hobbycad/document_undo_host.h>
#include <array>
#include <map>
#include <string>

#include <string>
#include <vector>

#include <hobbycad/project.h>
#include <hobbycad/project_session.h>

namespace hobbycad {

class CliHistory;

/// Viewport action type for CLI commands that affect the viewport.
enum class ViewportAction {
    None,
    ZoomPercent,     ///< zoom <percent>
    ZoomHome,        ///< zoom home
    PanTo,           ///< panto <x>,<y>,<z>
    PanHome,         ///< panto home
    RotateAxis,      ///< rotate on <axis> <degrees>
    RotateHome       ///< rotate home
};

/// What the prompt is currently pointing at.
///
/// Aaron, 2026-08-27: *"create and select, I can see them modifying the
/// prompt, much like changing a directory; then the prompt becomes context
/// sensitive to that function."*
///
/// A context is exactly that: a working location. `select` moves into
/// one, `deselect` leaves, and commands that can be scoped (`print`, and
/// later `delete`/`rename`) act on it when there is one.
struct CliContext {
    enum class Kind { None, Sketch, Body, Plane };

    Kind    kind = Kind::None;
    int     id   = -1;      ///< Feature/body/plane id in the document
    std::string name;

    bool isSet() const { return kind != Kind::None; }

    /// The word used in the prompt and in messages.
    std::string kindName() const
    {
        switch (kind) {
        case Kind::Sketch: return "sketch";
        case Kind::Body:   return "body";
        case Kind::Plane:  return "plane";
        case Kind::None:   break;
        }
        return {};
    }
};

/// Result of executing a command.
struct CliResult {
    int     exitCode = 0;    ///< 0 = success, non-zero = error
    std::string output;          ///< Normal output text
    std::string error;           ///< Error output text (if any)
    bool    requestExit = false;  ///< True if exit/quit was entered

    /// Output is for a person to read, so page it when the terminal is
    /// interactive and it does not fit.
    ///
    /// Aaron, 2026-08-27: *"by default, print paginates, and export
    /// doesn't."* The command states the intent; whether to act on it is
    /// the output layer's call, because paginating a PIPE would hang a
    /// script waiting for a keypress that never comes.
    bool    paginate = false;

    // Viewport action fields (for commands that need to affect the viewport)
    ViewportAction viewportAction = ViewportAction::None;
    double vpArg1 = 0.0;     ///< First argument (zoom percent, pan X, rotate degrees)
    double vpArg2 = 0.0;     ///< Second argument (pan Y)
    double vpArg3 = 0.0;     ///< Third argument (pan Z)
    char   vpAxis = 'z';     ///< Rotation axis ('x', 'y', 'z')
};

class CliEngine {
public:
    explicit CliEngine(CliHistory& history);
    ~CliEngine();

    /// Attach the document that `undo` and `redo` act on.
    ///
    /// The CLI has no document of its own; in the GUI it must drive the
    /// SAME history as Edit > Undo, and standalone there may be nothing
    /// open. Null (the default) makes those commands report that there is
    /// no document rather than silently doing nothing.
    void setUndoHost(hobbycad::DocumentUndoHost* host) { m_undoHost = host; }

    /// Attach the owner of the document the commands act on.
    ///
    /// The same engine serves the GUI terminal and `--no-gui`; which one
    /// is running is entirely this pointer. Null means no document is
    /// open, which commands report rather than working around.
    void setDocumentHost(hobbycad::DocumentHost* host) { m_docHost = host; }

    /// The project, or null when nothing is open.
    hobbycad::Project* project();
    const hobbycad::Project* project() const;
    /// The session model edits go through, or null when nothing is open.
    hobbycad::ProjectSession* session();

    /// Execute a single command line.  Returns result with output.
    CliResult execute(const std::string& line);

    /// Get the list of known command names (for tab completion).
    std::vector<std::string> commandNames() const;

    /// Get completion hints for a command's arguments.
    /// Returns possible completions or a hint message (prefixed with "?")
    /// for the current argument position.
    /// @param tokens The tokens entered so far (first token is the command)
    /// @param prefix The partial text of the current argument being typed
    /// @return List of completions, or single "?hint message" for help
    std::vector<std::string> completeArguments(const std::vector<std::string>& tokens,
                                   const std::string& prefix) const;
    std::vector<std::string> completeCreateArguments(const std::vector<std::string>& tokens, const std::string& prefix,
                                        int argIndex) const;
    std::vector<std::string> completeSketchArguments(const std::vector<std::string>& tokens, const std::string& prefix,
                                        int argIndex) const;

    /// Build a prompt string showing the current directory.
    std::string buildPrompt() const;

    /// Returns true if currently in sketch editing mode.
    bool inSketchMode() const;

    /// Returns the name of the current sketch (empty if not in sketch mode).
    std::string currentSketchName() const;

private:
    CliResult cmdHelp() const;
    CliResult cmdVersion() const;
    CliResult cmdNew(const std::vector<std::string>& args);
    /// Why "new" or "open" may not replace the project now, or empty when it
    /// may. `discard` drops unsaved changes and a sketch open in the window.
    std::string replaceProjectBlocker(bool discard) const;
    /// Reset what named things in the old project, and tell the host.
    void adoptReplacedProject();
    CliResult cmdOpen(const std::vector<std::string>& args);
    CliResult cmdSave(const std::vector<std::string>& args);
    CliResult cmdConvert(const std::vector<std::string>& args);
    CliResult cmdScript(const std::vector<std::string>& args);
    CliResult cmdCd(const std::vector<std::string>& args);
    CliResult cmdPwd() const;
    CliResult cmdInfo() const;
    CliResult cmdSketches() const;
    CliResult cmdBodies() const;
    CliResult cmdPlanes() const;
    CliResult cmdParameters(const std::vector<std::string>& args);

    /// Measure a reference parameter's value from the current sketch geometry.
    /// @param source measurement descriptor, currently "distance <ptA> <ptB>"
    ///        where a point ref is <entityId>[.<pointIndex>].
    /// @return true and sets @p value on success; false if the descriptor is
    ///         unknown or a referenced point does not exist.
    bool measureReferenceParam(const std::string& source, double& value) const;

    /// Re-measure every reference parameter from the current (solved) geometry
    /// and re-evaluate the parameter set so dependents update. Call after a
    /// solve. No-op when the document has no reference parameters.
    void recomputeReferenceParameters();
    CliResult cmdCoords(const std::vector<std::string>& args);
    CliResult cmdDeselect();
    /// Show the current context's contents, for reading.
    CliResult cmdPrint(const std::vector<std::string>& args) const;

    /// One line per entity, with its id and type. Shared by "print" on a
    /// saved sketch and on the one being edited, so the two cannot drift.
    std::vector<std::string> describeEntities(const hobbycad::SketchData& sketch) const;

    /// Emit a replayable script. Always full precision.
    CliResult cmdExport(const std::vector<std::string>& args) const;

    /// Emit the commands that recreate one sketch.
    std::vector<std::string> printSketch(const hobbycad::SketchData& sketch,
                            int precision) const;
    CliResult cmdHistory(const std::vector<std::string>& args);
    CliResult cmdUndo(const std::vector<std::string>& args);
    CliResult cmdRedo(const std::vector<std::string>& args);
    CliResult cmdSelect(const std::vector<std::string>& args);
    CliResult cmdCreate(const std::vector<std::string>& args);
    CliResult cmdCreatePlane(const std::vector<std::string>& args);
    CliResult cmdDelete(const std::vector<std::string>& args);
    CliResult cmdRename(const std::vector<std::string>& args);
    CliResult cmdExtrude(const std::vector<std::string>& args);
    CliResult cmdRevolve(const std::vector<std::string>& args);

    /// Record a change to one of the project's object lists, so it can be
    /// undone.
    ///
    /// Each takes the list as it was BEFORE and reads the current one as
    /// the after, so the call has to come after the mutation, and there
    /// is no way to record a change that was not actually made.
    ///
    /// A null undo host makes these no-ops rather than errors: a front end
    /// may legitimately have no history. That is also why "delete" says out
    /// loud whether the operation can be undone.
    void recordBodyListChange(const std::vector<hobbycad::BodyData>& before,
                              const std::string& description);
    void recordPlaneListChange(
        const std::vector<hobbycad::ConstructionPlaneData>& before,
        const std::string& description);
    void recordParameterListChange(
        const std::vector<hobbycad::ParameterData>& before,
        const std::string& description);

    /// Resolve a name-or-id token to a POSITION in the project's vector.
    ///
    /// @param kindValue  A hobbycad::ObjectKind, passed as int so the header can stay
    ///        private to the .cpp; it is an implementation detail of two
    ///        commands, not part of the engine's interface.
    /// @param error  Set to a message explaining any failure.
    /// @return the index, or -1.
    int resolveObjectIndex(int kindValue, const std::string& token,
                           std::string* error) const;
    CliResult cmdFinish();
    CliResult cmdDiscard();

    // Sketch mode geometry commands
    CliResult cmdSketchPoint(std::vector<std::string> args);
    CliResult cmdSketchLine(std::vector<std::string> args);
    CliResult cmdSketchCircle(std::vector<std::string> args);
    CliResult cmdSketchRectangle(std::vector<std::string> args);
    /// "<name> [from] <x1>,<y1> to <x2>,<y2>": the body line and rectangle share.
    CliResult sketchTwoPoints(std::vector<std::string> args, sketch::EntityType type, const std::string& usage,
                              const std::string& missingTo, const std::string& badFirst,
                              const std::string& badSecond, const std::string& createdFormat);
    CliResult cmdSketchArc(std::vector<std::string> args);
    CliResult cmdSketchPolygon(std::vector<std::string> args);
    CliResult cmdSketchEllipse(std::vector<std::string> args);
    CliResult cmdSketchSlot(std::vector<std::string> args);

    /// The "slot arc ..." form: an arc slot from a center, a CENTERLINE
    /// radius, an angle range and a width.
    CliResult sketchArcSlot(const std::vector<std::string>& args, bool construction);

    /// The "slot along <id> ..." form: sweep an existing line or arc.
    CliResult sketchSlotAlong(const std::vector<std::string>& args, bool construction);

    /// Usage text shared by both slot forms.
    std::string slotUsage() const;
    CliResult cmdSketchSpline(std::vector<std::string> args);
    CliResult cmdSketchBezier(std::vector<std::string> args);
    CliResult cmdSketchText(std::vector<std::string> args);
    CliResult cmdConstrain(std::vector<std::string> args);
    CliResult cmdConstrainEdit(const std::vector<std::string>& args);
    CliResult cmdConstraints() const;
    CliResult cmdSolve(std::vector<std::string> args);
    int rederiveDependentEntities(std::vector<hobbycad::SketchEntityData>& scratch);
    CliResult cmdGroup(std::vector<std::string> args);
    CliResult cmdTransform(std::vector<std::string> args);
    CliResult cmdGroups() const;
    CliResult cmdSweep(std::vector<std::string> args);
    CliResult cmdProject(std::vector<std::string> args);
    CliResult cmdPoints(std::vector<std::string> args);
    std::string sweepUsage() const;

    /// Lowest unused group id in the sketch being edited.
    int nextPendingGroupId() const;

    /// Build the construction centerline a slot follows, add it, and point
    /// the slot at it. Returns the centerline's id, or -1.
    int addSlotCenterline(hobbycad::SketchEntityData& slot, double pathRadius,
                          double startAngle, double sweep);

    /// Put a slot and its centerline in a group named "Slot <id>".
    int groupSlotWithPath(int slotId, int pathId);

    /// Give a constraint an id and add it to the sketch being edited.
    int addPendingConstraint(hobbycad::ConstraintData constraint);

    /// Lowest unused constraint id in the sketch being edited.
    int nextPendingConstraintId() const;

    /// The entity with that id in the sketch being edited, or null.
    const hobbycad::SketchEntityData* pendingEntity(int id) const;

    /// The sketch that read-only commands should report on: the one being
    /// edited, or failing that the selected one. Null when neither.
    const hobbycad::SketchData* currentSketchForReading() const;

    /// Usage text for "constrain", built from the constraint enum.
    std::string constrainUsage() const;

    /// Add an entity to the pending sketch, giving it an id.
    ///
    /// Entities carry ids so that constraints can refer to them and so that
    /// a person can name one to select or delete it. The geometry commands
    /// used to push entities straight onto the vector, leaving every one of
    /// them at the default id 0: fine while nothing read the id, and
    /// wrong the moment anything did.
    ///
    /// Ids are 1-based and allocated per sketch, matching SketchCanvas.
    ///
    /// @return the id given to the entity.
    int addPendingEntity(hobbycad::SketchEntityData entity);

    /// Strip a trailing "construction" keyword from a geometry command's
    /// arguments, if present.
    ///
    /// Uniform across every geometry command on purpose: a modifier that
    /// works on some shapes and not others is harder to remember than one
    /// that always works.
    ///
    /// @param args  Modified in place: the keyword is removed.
    /// @return true if the keyword was there.
    static bool takeConstructionFlag(std::vector<std::string>& args);

    // Viewport commands (parsed here, executed via signals)
    CliResult cmdZoom(const std::vector<std::string>& args);
    CliResult cmdPanTo(const std::vector<std::string>& args);
    CliResult cmdRotate(const std::vector<std::string>& args);

    CliHistory& m_history;

    // Sketch mode state
    hobbycad::DocumentUndoHost* m_undoHost = nullptr;
    hobbycad::DocumentHost*     m_docHost  = nullptr;

    /// The sketch being built right now, before "finish" commits it.
    ///
    /// The geometry commands used to parse their arguments, report success
    /// and throw the result away; "finish" then said "Saved sketch" while
    /// saving nothing. Everything entered in sketch mode accumulates here
    /// and is handed to the document on finish.
    hobbycad::SketchData m_pendingSketch;

    /// Entity selected inside a sketch, or -1.
    ///
    /// Separate from m_context, which names a DOCUMENT object. An entity
    /// lives inside a sketch and disappears with it, so it cannot be
    /// represented the same way.
    int m_selectedEntityId = -1;

    /// Where the prompt is pointing. Distinct from m_inSketchMode, which
    /// means a sketch is being EDITED; you can select a sketch to look
    /// at it without opening it for editing.
    CliContext m_context;
    bool    m_inSketchMode = false;
    std::string m_currentSketchName;
    std::string m_currentSketchPlaneName;          // Display name (e.g. "XY", "MyPlane")
    SketchPlane m_currentSketchPlane = SketchPlane::XY;
    int     m_currentConstructionPlaneId = -1;   // with SketchPlane::Custom: the plane's id
    int     m_sketchCounter = 0;  // For auto-naming sketches

    /// Parameter names offered by tab completion.
    ///
    /// Read from the document when one is open; this list is only the
    /// fallback for an empty session, so completion still suggests
    /// something sensible before a project exists. It used to be the ONLY
    /// source, which meant completion advertised parameters the document
    /// did not have and hid the ones it did.
    std::vector<std::string> parameterNames() const;

    /// The document's parameters as name -> value, for evaluating
    /// expressions typed into geometry commands.
    std::map<std::string, double> parameterValues() const;
    std::map<std::string, std::array<double, 3>> namedPointValues() const;

};

}  // namespace hobbycad

#endif  // HOBBYCAD_CLIENGINE_H

