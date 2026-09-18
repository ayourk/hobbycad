// =====================================================================
//  src/hobbycad/gui/mainwindow.h — Base main window
// =====================================================================
//
//  Provides the application skeleton shared by Full Mode and Reduced
//  Mode: menu bar, status bar, and dock panel placeholders.
//
//  Subclasses (FullModeWindow, ReducedModeWindow) set the central
//  widget to the appropriate viewport.
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_MAINWINDOW_H
#define HOBBYCAD_MAINWINDOW_H

#include <functional>
#include <hobbycad/bindings.h>
#include <hobbycad/sketch/parsing.h>
#include <hobbycad/document_host.h>
#include <hobbycad/document_undo_host.h>
#include <hobbycad/document_undo.h>
#include <hobbycad/document.h>
#include <hobbycad/opengl_info.h>
#include <hobbycad/project.h>
#include <hobbycad/project_session.h>
#include <hobbycad/units.h>

#include <QMainWindow>

#include <string>
#include "parametersdialog.h"
#include "sketchcanvas.h"
#include "../i18n/retranslatable.h"

class QAction;
class QLabel;
class QDockWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QStackedWidget;

class QTabWidget;   // Qt, global scope

#include <QSet>

#include <hobbycad/browser.h>

namespace hobbycad {
namespace sketch {
struct Entity;
struct Constraint;
}  // namespace sketch

class ChangelogPanel;
class CliPanel;
class ProjectBrowserWidget;
class ObjectsBrowserWidget;
class ErrorOutlineDelegate;
class PlaneTransformPanel;
class ConstraintExplorer;
class SketchPropertiesWidget;
class SketchOptionsWidget;
class BackgroundCalibrationDialog;
class ModelToolbar;
class SketchToolbar;
class TimelineWidget;

class MainWindow : public QMainWindow,
                   public hobbycad::DocumentUndoHost,
                   public hobbycad::DocumentHost,
                   public Retranslatable {
    Q_OBJECT

public:
    explicit MainWindow(const OpenGLInfo& glInfo,
                        QWidget* parent = nullptr);
    ~MainWindow() override;

    /// Re-apply every menu title, action text and tooltip this window owns.
    /// Called by hobbycad::translations::switchTo() after a new catalog is
    /// installed, and by createMenus() so the strings live in one place.
    void retranslate() override;

    /// Write the in-memory project to the crash-recovery directory.
    /// Registered with CrashHandler, so it runs from a signal handler on
    /// SIGABRT/SIGSEGV.  Best-effort by nature: it touches Qt containers
    /// and the filesystem, neither of which is async-signal-safe, but a
    /// possible save beats a certain loss.
    void saveEmergencyCopy();

    // ---- DocumentUndoHost -------------------------------------------
    //
    // Implemented here rather than on the document so that undo can also
    // refresh the views. The CLI panel is handed this same object, so
    // `undo` at the prompt and Edit > Undo share one history.
    bool undoDocument(std::string* description) override;
    bool redoDocument(std::string* description) override;
    std::string nextUndoDescription() const override;
    std::string nextRedoDescription() const override;
    int undoDepth() const override;
    int redoDepth() const override;

    /// Record a reversible document operation.
    void pushDocumentCommand(const hobbycad::DocumentCommand& cmd) override;

    /// Add a feature to the recipe AND record it as undoable, in one step.
    ///
    /// These two must not be done separately: a feature added without a
    /// command is invisible to undo, and a command pushed without the
    /// feature refers to something that is not there, which applyUndo()
    /// will (correctly) refuse. Keeping them together makes the pairing
    /// impossible to forget.
    void recordFeatureAdded(const hobbycad::FeatureData& feature,
                            const QString& description = {});

    /// Remove a feature from the recipe and record the removal.
    /// Returns false if no feature with that id is in the recipe.
    bool recordFeatureDeleted(int featureId, const QString& description = {});

    /// Change one feature in the recipe and record the change.
    ///
    /// `edit` receives the feature to modify; whatever it changes becomes the
    /// "after" state. Taking a callback rather than a finished FeatureData
    /// means the caller cannot accidentally record a before/after pair that
    /// never matched the recipe.
    ///
    /// Returns false if no feature with that id is present, or if `edit`
    /// left it unchanged: an edit that changes nothing should not add an
    /// undo step.
    bool recordFeatureModified(int featureId,
                               const std::function<void(hobbycad::FeatureData&)>& edit,
                               const QString& description = {});

    /// The recipe, for views that rebuild themselves from it.
    const std::vector<hobbycad::FeatureData>& recipe() const {
        return m_project.features();
    }

protected:
    /// Give the sketch canvas the host's projection resolver and source lister.
    void installSketchCanvasResolvers();
    void addSketchHeaderRows(QTreeWidget* propsTree, const QString& sketchName, SketchPlane plane);
    /// Called after undo/redo alters the feature recipe, so views can catch
    /// up. Override to refresh anything MainWindow does not own.
    virtual void onDocumentRecipeChanged();

    /// Replace the OpenGL status text. Used when the viewport is
    /// abandoned mid-session, so the indicator stops claiming a working
    /// 3D context.
    void setGlModeText(const QString& text, const QString& tooltip = {});

    // ---- DocumentHost: the CLI acts on the project this window shows ----
    hobbycad::Project* hostProject() override { return &m_project; }
    const hobbycad::Project* hostProject() const override { return &m_project; }
    void hostDocumentChanged() override;
    bool hostHasViewport() const override { return false; }
    hobbycad::ProjectSession* hostSession() override { return &m_session; }
    void hostProjectReplaced() override;
    bool hostHasOpenWork() const override { return m_inSketchMode; }

    /// Rebuild the objects tree from the model, preserving expansion and
    /// selection. Replaces the per-item addXToTree() helpers.
    void rebuildObjectsTree();

    /// Inputs the objects tree is built from. Overridden by the full window
    /// so the tree shows live geometry rather than the last saved state.
    virtual hobbycad::BrowserInput browserInput() const;

    /// Show or hide a body in the viewport, by body ID.
    /// Base class has no viewport.
    virtual bool setBodyVisible(int /*bodyId*/, bool /*visible*/) { return false; }

    /// Refuse a property value: flag the row red, reopen the editor with
    /// the caret where it was, and shake it.
    void rejectPropertyEdit(QTreeWidgetItem* item, const QString& reason);

    /// Clear a previous refusal on a property row.
    void acceptPropertyEdit(QTreeWidgetItem* item);

    /// Apply a rename that has already passed the shared checks.
    /// @return false to refuse it; the browser then shows the rejection.
    virtual bool handleNodeRenamed(hobbycad::NodeType type, int id, const QString& name);

    /// Carry out a context-menu action from the objects browser.
    /// @return true if handled; false falls through to a "not implemented"
    /// message, so an unhandled action is never silently ignored.
    virtual bool handleObjectAction(hobbycad::NodeType /*type*/,
                                    int /*id*/,
                                    const QString& /*actionId*/) { return false; }

    /// Enable/label Edit > Undo and Redo from the OUTER stack. A no-op while
    /// a sketch is open, where the inner stack owns those actions.
    void updateUndoActions();

    // ---- The session's views -----------------------------------------
    //
    // Sketches, the timeline and their edits belong to the ProjectSession,
    // which the CLI and a future web front end use too. Both GUI modes share
    // the code below; a mode differs only in what it draws.

    /// Rebuild the timeline from the session: the project's history, plus the
    /// sketch being drawn when there is one. Keeps the rollback marker and the
    /// selection by feature id.
    void refreshTimeline();

    /// Wire the timeline's signals to the shared handlers and fill it.
    /// Call once the derived window has created m_timeline.
    void connectTimeline();

    /// Redraw what this mode draws of the model: Full mode's bodies and sketch
    /// wireframes. Reduced mode draws none.
    virtual void refreshModelViews() {}

    /// Highlight a sketch's plane; Full mode draws one in the viewport.
    virtual void showSketchPlaneHighlight(SketchPlane /*plane*/, double /*offset*/,
                                          PlaneRotationAxis /*axis*/, double /*angle*/) {}

    /// The canvas contents as the draft of the sketch being drawn or edited.
    hobbycad::SketchDraft draftFromCanvas() const;

    /// Sketch-level properties of the sketch on the canvas.
    void showSketchProperties();

    /// Rebuild the GUI parameter list from the project, or the defaults.
    void loadParametersFromProject();

    /// Make the document's bodies match the project's. The project holds them;
    /// the document keeps a copy for the BREP, STEP and STL paths.
    void syncDocumentFromProject();

    /// The sketch selected in the timeline for a 3D operation, or -1 after
    /// telling the user why there is none.
    int selectedTimelineSketchId(const QString& operationName);

    /// False, with a status message, for an out-of-range index or the Origin.
    bool validateFeatureAction(int index, const QString& actionVerb) const;

public:

    /// Open a project directory, .hcad, or BREP file by path.
    ///
    /// Shared by the File > Open dialog and the command-line argument, so
    /// the two cannot drift apart. `selectedFilter` is the dialog's filter
    /// and may be empty; it only affects the BREP extension fallback.
    /// Returns false and shows a message box on failure.
    bool openPath(const QString& path, const QString& selectedFilter = QString());

    /// If a previous session left a recovery copy behind, offer to open it.
    /// Call once after the window is shown.
    void checkForCrashRecovery();

    /// Directory used for crash-recovery copies.
    static std::string recoveryDir();

    /// Access the current document (legacy BREP-only mode).
    Document& document();

    /// Access the current project.
    Project& project();

    /// Access the embedded CLI panel (may be nullptr if not created).
    CliPanel* cliPanel() const;

    /// Check if currently in sketch mode
    bool isSketchMode() const { return m_inSketchMode; }

    /// Get document parameters (for formula fields)
    QMap<QString, double> parameterValues() const;

public slots:
    /// Enter sketch editing mode (override for mode-specific setup).
    virtual void enterSketchMode(SketchPlane plane = SketchPlane::XY);
    /// The single entry point for beginning a sketch. Both the interactive
    /// path (onCreateSketchClicked, after it resolves the plane) and the
    /// startup --exec command funnel through here so there is one place that
    /// begins a sketch. The base applies the offset and enters; FullMode adds
    /// the rotation members it owns.
    virtual void createSketchOnPlane(SketchPlane plane, double offset = 0.0,
                                     PlaneRotationAxis axis = PlaneRotationAxis::X,
                                     double angle = 0.0);

    /// Begin a sketch requested at startup (--exec). The base begins it now;
    /// FullMode defers until the 3D viewport has finished initializing, so a
    /// run-script launch completes normal startup before switching to 2D.
    virtual void beginStartupSketch(SketchPlane plane) { createSketchOnPlane(plane); }

    /// Load the persisted sketch view toggles (grid, snap, points,
    /// constraints, dimensions, profiles) onto the active canvas and the
    /// View-menu check marks. Called on entering a sketch.
    /// Wire Edit > Cut/Copy/Paste to the sketch canvas and keep their enabled
    /// state in step with the selection and the clipboard. Call once the
    /// derived window has created m_sketchCanvas.
    void connectClipboardActions();
    void loadSketchViewPreferences();
    /// Persist the active canvas's sketch view toggles.
    void saveSketchViewPreferences();

    /// Exit sketch editing mode (override for mode-specific teardown).
    virtual void exitSketchMode();
    void finishSketchInteractive();  ///< Finish Sketch: prompt Save/Discard, then leave (toolbar + menu)

protected:
    /// Called by subclasses after setting the central widget.
    void finalizeLayout();
    void applyFirstRunGeometry();

    /// A document was loaded: rebuild the timeline, tree, parameters and model
    /// views from it. Full mode extends this with its construction planes.
    virtual void onDocumentLoaded();

    /// The document was closed: empty the views.
    virtual void onDocumentClosed();

    /// Override to apply changed preferences to the viewport.
    virtual void applyPreferences();

    /// Override to provide the active sketch canvas (for sketch export).
    virtual class SketchCanvas* activeSketchCanvas() const { return nullptr; }

    /// Initialize shared sketch signal/slot connections.
    /// Call from subclass constructor after creating widgets.
    void initSketchConnections();

    // Sketch mode shared methods
    void initDefaultParameters();
    void showParametersDialog();
    void onParametersChanged(const QList<Parameter>& params);
    /// Rebuild the GUI parameter list (m_parameters) from the project, so it
    /// stays in step after a parameter undo/redo.
    void syncParametersFromProject();
    /// Re-measure every reference parameter from the just-solved sketch, feed
    /// the values through the ParameterEngine so dependents follow, and adopt
    /// them into m_parameters and the project. Driven off the post-solve
    /// sketchConstraintStateChanged signal; a no-op when no reference parameter
    /// exists. The measurement itself is the library helper
    /// sketch::measureReferenceSource, shared with the CLI.
    void recomputeReferenceParameters();
    void onSketchToolSelected(SketchTool tool);
    void onSketchSelectionChanged(int entityId);
    void onSketchEntityCreated(int entityId);
    void showSketchEntityProperties(int entityId);
    /// Geometry rows for showSketchEntityProperties.
    void addGeometryRows(QTreeWidgetItem* geomHeader, const SketchEntity* entity,
                         int entityId, const QString& units);
    void showSketchConstraintProperties(int constraintId);
    void showFeatureProperties(int index);
    void installDropdownEditor(QTreeWidget* propsTree);
    void onSketchPropertyItemChanged(QTreeWidgetItem* item, int column);
    /// Group section rows that act on click: a member row drills into the
    /// group and selects the member; a constraint row selects the constraint.
    void onSketchPropertyItemClicked(QTreeWidgetItem* item, int column);
    /// Prepend the Group section when the selection is exactly one whole group.
    void addGroupSection(QTreeWidget* propsTree, int groupId);

    /// Override to handle sketch deselection (entity deselected, no constraint selected).
    virtual void onSketchDeselected();

    /// 3D-mode Properties panel when nothing is selected: the project at a
    /// glance instead of a blank tree. Shown at startup, after leaving a
    /// sketch, and when the timeline selection is cleared.
    void showProjectSummary();
    /// Properties of an origin plane (XY, XZ, YZ) picked in the Project tree.
    /// Full mode overrides to also highlight the plane in the viewport.
    virtual void showOriginPlaneProperties(SketchPlane plane);
    /// How many stored sketches sit on this origin plane, unoffset.
    virtual int sketchCountOnPlane(SketchPlane plane) const;
    /// Remove the plane highlight from the viewport (Full mode draws one when
    /// a plane or a sketch feature is selected). Called when the selection
    /// moves to something that is not a plane.
    virtual void hidePlaneHighlight() {}
    /// Called when the Project tree selection leaves a plane: the highlight
    /// goes, the plane editor closes, and a plane page gives way to the summary.
    void onPlaneDeselected();
    bool eventFilter(QObject* watched, QEvent* event) override;
    bool m_planePageShown = false;   ///< The properties tree currently shows a plane page

    /// Override to populate sketch feature properties with real data.
    virtual void populateSketchFeatureProperties(QTreeWidgetItem* parent,
                                                  int timelineIndex,
                                                  const QString& units);

    /// Override to create sketch in timeline and handle save logic.
    virtual void onCreateSketchClicked();

    /// Override to save the current sketch.
    /// Problems across the project's sketches, one line each, naming the
    /// sketch. `sketchCount` receives how many sketches contributed.
    ///
    /// An unfinished sketch on disk is expected, not a fault: saving is a
    /// checkpoint and is never blocked. This is how the program says so.
    QStringList sketchProblems(int* sketchCount = nullptr) const;

    /// Is the sketch fit to be called finished? Shows the reason and
    /// returns false when not.
    ///
    /// Only Finish consults this. Saving is a checkpoint of the current
    /// state and is never blocked: refusing to write a work in progress
    /// is how a user loses one.
    bool sketchIsFinishable();

    virtual void saveCurrentSketch();

    /// Override to discard the current sketch.
    virtual void discardCurrentSketch();

    // Static parse helpers for property editing
    /// The project's parameters as the expression evaluator wants them.
    std::map<std::string, double> parameterMap() const;
    /// Read a properties cell the way the Parameters dialog reads an
    /// expression (sketch::resolveMeasurement): parameters, bare formulas, an
    /// optional unit, the cell's display unit as the default.
    bool parseCell(const QString& text, sketch::MeasureKind kind, double& out) const;
    bool parseCellPoint(const QString& text, double& x, double& y) const;

    /// Override to provide sketch entities for export when no active canvas.
    /// Used when a completed sketch is selected in the 3D view.
    /// Returns true if entities are available for export.
    virtual bool getSelectedSketchForExport(
        QVector<sketch::Entity>& outEntities,
        QVector<sketch::Constraint>& outConstraints) const;

    /// Enable or disable sketch export actions (DXF/SVG).
    void setSketchExportEnabled(bool enabled);

    /// Intercept window close to prompt for unsaved changes.
    void closeEvent(QCloseEvent* event) override;

    /// Track window state changes.
    void changeEvent(QEvent* event) override;

    /// True when the window frame nearly fills the screen work area (a pure
    /// size test, live and WM-independent).
    bool frameNearlyFillsScreen() const;

    /// True when the window is maximized: the WM-reported state confirmed by
    /// frameNearlyFillsScreen(). Preferred over isMaximized(), which goes
    /// stale on some X11 WMs (fluxbox) that omit _NET_WM_STATE on unmaximize.
    bool isEffectivelyMaximized() const;

    /// Track resize to save normal geometry when not maximized.
    void resizeEvent(QResizeEvent* event) override;

    /// Track move to save normal geometry when not maximized.
    void moveEvent(QMoveEvent* event) override;

    /// Access the View > Terminal toggle action.
    QAction* terminalToggleAction() const;

    /// Access the View > Reset View action.
    QAction* resetViewAction() const;
    QAction* lookAtAction() const;
    QAction* sliceAction() const;

    /// Access the View > Rotate Left/Right actions.
    QAction* rotateLeftAction() const;
    QAction* rotateRightAction() const;

    /// Access the View > Show Grid action.
    QAction* showGridAction() const;

    /// Access the View > Snap to Grid action.
    QAction* snapToGridAction() const;

    /// Access the View > Z-Up Orientation action.
    QAction* zUpAction() const;

    /// Access the View > Orbit Selected Object action.
    QAction* orbitSelectedAction() const;

    /// Access the View > Toolbar toggle action.
    QAction* toolbarToggleAction() const;

    /// Access the Construct > New Construction Plane action.
    QAction* newConstructionPlaneAction() const;

    /// Access the Edit > Undo action.
    QAction* undoAction() const;

    /// Access the Edit > Redo action.
    QAction* redoAction() const;

    /// Access the Edit > Delete action.
    QAction* deleteAction() const;

    /// Access the Edit > Select All action.
    QAction* selectAllAction() const;

    /// Access the properties tree widget for displaying selected item properties.
    QTreeWidget* propertiesTree() const;
    void updatePropertiesTreeHeight();

    /// The construction-plane transform editor under the properties tree.
    /// Hidden unless a construction plane is selected; Full mode drives it.
    PlaneTransformPanel* planeTransformPanel() const { return m_planeTransformPanel; }
    void hidePlaneTransformPanel();

    /// Access the changelog (undo history) panel
    ChangelogPanel* changelogPanel() const;

    /// Add a sketch to the feature tree

    /// Select a sketch in the feature tree
    /// Select a sketch in the objects tree by FEATURE ID.
    ///
    /// It used to take a position in the sketch list. The tree is now keyed
    /// by feature id (the sketch's stable identity), so a position would
    /// select the wrong sketch after any deletion.
    void selectSketchInTree(int featureId);

    /// Clear all sketches from the feature tree

    /// Add a body to the feature tree

    /// Clear all bodies from the feature tree

    /// Add a construction plane to the feature tree

    /// Select a construction plane in the feature tree
    void selectConstructionPlaneInTree(int id);

    /// Clear all construction planes from the feature tree

    /// Set document units from project
    void setUnitsFromString(const QString& units);

    /// Get the current unit system index (0=mm, 1=cm, 2=m, 3=in, 4=ft).
    int currentUnits() const;

    /// Get the current unit as a LengthUnit enum.
    LengthUnit currentLengthUnit() const;

    /// Get the current unit suffix string (e.g., "mm", "in").
    QString unitSuffix() const;

    /// Format a stored length (always mm) in the CURRENT display unit,
    /// at that unit's precision, without a suffix.
    ///
    /// The properties panel used to print the raw millimeter number and
    /// append the current unit's suffix, so with inches selected a 25.4 mm
    /// point read "25.40 in", wrong by a factor of 25.4, and at a fixed
    /// two places regardless of unit.
    QString formatLength(double mm) const;
    QString pointText(const Point2D& p, const QString& units) const;
    QString lengthText(double mm, const QString& units) const;

    /// Workspace types for the toolbar.
    enum class Workspace {
        Design,
        Render,
        Animation,
        Simulation
    };

signals:
    /// Emitted when the user changes the workspace.
    void workspaceChanged(Workspace workspace);

    /// Emitted when the user changes the display units.
    /// @param units  0=mm, 1=cm, 2=m, 3=in, 4=ft
    void unitsChanged(int units);

    /// Emitted when a construction plane is selected in the feature tree.
    void constructionPlaneSelected(int planeId);

    /// Emitted when a sketch is selected in the feature tree, with its id.
    void sketchSelectedInTree(int sketchId);

protected:
    /// Hide the dock-based terminal (used by Reduced Mode which
    /// has its own central CLI panel instead).
    void hideDockTerminal();

    OpenGLInfo m_glInfo;
    Document   m_document;
    Project    m_project;
    /// Every edit to m_project goes through this, the GUI's and the CLI
    /// panel's alike, into its one history. Declared after m_project, which
    /// it refers to.
    hobbycad::ProjectSession m_session{m_project};

    // Objects tree (protected for subclass access)
    QTreeWidget* m_objectsTree = nullptr;

    // Sketch mode infrastructure (shared by Full and Reduced modes)
    QStackedWidget*  m_toolbarStack      = nullptr;
    ModelToolbar*    m_toolbar           = nullptr;
    SketchToolbar*   m_sketchToolbar     = nullptr;
    QStackedWidget*  m_viewportStack     = nullptr;
    SketchCanvas*    m_sketchCanvas      = nullptr;
    bool             m_recomputingReferences = false;  ///< re-entrancy guard for recomputeReferenceParameters()
    QAction*         m_actionDrawThenConstrain = nullptr;
    QAction*         m_actionShowUnconstrained = nullptr;
    QAction*         m_actionShowConstraints = nullptr;
    QAction*         m_actionShowDimensions = nullptr;
    QAction*         m_actionShowProfiles = nullptr;
    QAction*         m_actionZoomToFit = nullptr;
    TimelineWidget*  m_timeline          = nullptr;
    bool             m_inSketchMode      = false;
    QList<Parameter> m_parameters;
    /// The sketch on the canvas, while one is open (m_draftOpen). New sketches
    /// are not in the project until Finish.
    hobbycad::SketchDraft m_draft;
    bool             m_draftOpen = false;

private slots:
    void onFileNew();
    void onFileOpen();

    // Timeline actions, shared by both modes.
    void onEditFeature(int index);
    void onRenameFeature(int index);
    void onDeleteFeature(int index);
    void onSuppressFeature(int index, bool suppress);
    void onFeatureMoved(int fromIndex, int toIndex);
    void onRollbackChanged(int index);
    void onExportSketchDXF(int index);
    void onExportSketchSVG(int index);

    /// Route a context-menu action from the objects browser.
    void onObjectAction(hobbycad::NodeType type, int id, const QString& actionId);

    /// Pick a replacement file for an external reference that will not
    /// resolve. Uses the same file browser as File > Open.
    void onRelinkReference(hobbycad::NodeType type, int id,
                           const QString& currentPath);
    void onFileSave();
    void onFileSaveAs();
    void onFileClose();
    void onFileQuit();
    void onFileImportStep();
    void onFileImportDXF();
    void onFileExportStep();
    void onFileExportStl();
    void onFileExportDXF();
    void onFileExportSVG();
    void onEditPreferences();
    void onThemeLight();
    void onThemeDark();
    void onThemeEdit();
    void applyThemeChoice(bool dark);
    void onConstraintMenu(int constraintType);  // apply to selection, or arm tool-first
    void onHelpAbout();

private slots:
    /// A language was picked from the View > Language menu.
    void onLanguageSelected(QAction* action);

private:
    void createMenus();
    /// The action for a registry command, made on first use.
    QAction* commandAction(const char* id);
    /// The menu for an arrangement container, made on first use.
    QMenu* commandMenu(const std::string& id);
    /// Take the menus and toolbars down and build them again, after the
    /// person changed the arrangement in the Customize dialog.
    void rebuildArrangedUi();
    /// Place every action in the menus the arrangement names.
    void populateMenus();
    /// Name every command action and menu in the current language, with
    /// the keys bound to them.
    void retranslateCommands();
    void createFileMenu();
    void createEditMenu();
    void createViewMenu();
    void createWorkspaceMenu();
    void createThemeMenu();
    void createSelectionFilterMenu();
    void createSketchMenu();
    void createConstraintsMenu();
    void createLanguageMenu();
    /// Open the Customize dialog, the one place the layout is rearranged.
    void onCustomize();
    void createStatusBar();
    /// Reflect the sketch's constraint state in the status bar.
    void updateSketchStateLabel(hobbycad::sketch::SketchState state, int dof);
    void createDockPanels();
    void createProjectDock();
    void createPropertiesDock();
    void createSketchSideDocks();
    void createChangelogDock();
    void createTerminalDock();
    void updateTitle();
    void applyBindings();

    /// If the document has unsaved changes, show a dialog offering
    /// "Close Without Saving", "Save and Close", or "Cancel".
    /// Returns true if the caller should proceed (close/quit).
    /// Returns false if the user canceled.
    bool maybeSave();

    /// Save the project: to `chosenPath` (a Save As target, resolved by
    /// projectDirForSavePath) or, when empty, where it already lives. A sketch
    /// open on the canvas is written as it stands; failure is reported here.
    bool saveProject(const QString& chosenPath = QString());

    /// Leave any open sketch and drop the undo history before New, Open or
    /// Close puts another document in the window.
    void resetForDocumentChange();

    // Menus. Held as members because their titles change with the
    // language, and a local QAction* is enough right up until the text
    // has to be set a second time.
    /// Command id (or menu container id) -> its action (or menu).
    QHash<QString, QAction*> m_commandActions;
    QHash<QString, QMenu*> m_commandMenus;
    /// Registry radio group name -> the Qt group enforcing it.
    QHash<QString, QActionGroup*> m_radioGroups;
    /// The bindings last applied.
    bindings::Table m_keyTable;

    QMenu* m_menuFile      = nullptr;
    QMenu* m_menuImport    = nullptr;
    QMenu* m_menuExport    = nullptr;
    QMenu* m_menuEdit      = nullptr;
    QMenu* m_menuConstruct = nullptr;
    QMenu* m_menuHelp      = nullptr;
    QMenu* m_menuView      = nullptr;
    QMenu* m_menuWorkspace = nullptr;
    QMenu* m_menuLanguage  = nullptr;
    QMenu* m_menuTheme     = nullptr;

    QAction* m_actionNew    = nullptr;
    QAction* m_actionOpen   = nullptr;
    QAction* m_actionSave   = nullptr;
    QAction* m_actionSaveAs = nullptr;
    QAction* m_actionClose  = nullptr;
    QAction* m_actionImportStep = nullptr;
    QAction* m_actionImportDXF = nullptr;
    QAction* m_actionExportStep = nullptr;
    QAction* m_actionExportStl = nullptr;
    QAction* m_actionExportDXF = nullptr;
    QAction* m_finishSketchAction = nullptr;  ///< Sketch menu > Finish Sketch (enabled in a sketch)
    QAction* m_actionExportSVG = nullptr;
    QAction* m_actionThemeLight = nullptr;
    QAction* m_actionThemeDark  = nullptr;
    QAction* m_actionThemeEdit  = nullptr;
    QAction* m_actionQuit   = nullptr;
    QAction* m_actionUndo   = nullptr;
    QAction* m_actionRedo   = nullptr;
    QAction* m_actionCut    = nullptr;
    QAction* m_actionCopy   = nullptr;
    QAction* m_actionPaste  = nullptr;
    QAction* m_actionDelete = nullptr;
    QAction* m_actionSelectAll = nullptr;
    QAction* m_actionAbout  = nullptr;
    QAction* m_actionPreferences = nullptr;
    QAction* m_actionCustomize = nullptr;
    QAction* m_actionToggleTerminal = nullptr;
    QAction* m_actionToggleFeatureTree = nullptr;
    QAction* m_actionToggleProperties = nullptr;
    QAction* m_actionToggleToolbar = nullptr;
    QAction* m_actionToggleChangelog = nullptr;
    QAction* m_actionResetView = nullptr;
    QAction* m_actionLookAt = nullptr;
    QAction* m_actionSlice = nullptr;
    QAction* m_actionRotateLeft = nullptr;
    QAction* m_actionRotateRight = nullptr;
    QAction* m_actionShowGrid = nullptr;
    QAction* m_actionSnapToGrid = nullptr;
    QAction* m_actionZUp = nullptr;
    QAction* m_actionOrbitSelected = nullptr;

    QAction* m_actionWorkspaceDesign     = nullptr;
    QAction* m_actionWorkspaceRender     = nullptr;
    QAction* m_actionWorkspaceAnimation  = nullptr;
    QAction* m_actionWorkspaceSimulation = nullptr;

    // Language menu. The per-locale actions are kept in a list because
    // retranslate() has to rename all of them, and the "system default"
    // entry is separate because its text is composed rather than looked up.
    QActionGroup* m_languageGroup = nullptr;
    QAction* m_actionLanguageSystem = nullptr;
    QList<QAction*> m_languageActions;

    // Construct menu
    QAction* m_actionNewConstructionPlane = nullptr;

    // Status bar
    QLabel* m_statusLabel   = nullptr;
    QLabel* m_glModeLabel   = nullptr;
    QLabel* m_sketchStateLabel = nullptr;  ///< Constraint state, sketch mode only

    // Dock panels
    QDockWidget* m_featureTreeDock = nullptr;
    QDockWidget* m_propertiesDock  = nullptr;
    QDockWidget* m_terminalDock    = nullptr;
    QDockWidget* m_changelogDock   = nullptr;
    QDockWidget* m_constraintsDock = nullptr;
    CliPanel*    m_cliPanel        = nullptr;
    QTreeWidget* m_propertiesTree  = nullptr;
    bool         m_propsTreeHeightPending = false;
    PlaneTransformPanel* m_planeTransformPanel = nullptr;
    ChangelogPanel*  m_changelogPanel  = nullptr;

    /// Sketch-mode properties panel (background image, grid, entity props).
    /// Docked next to the model-mode Properties tree but only shown while a
    /// sketch is open (see enterSketchMode()/exitSketchMode()).
    SketchPropertiesWidget* m_sketchPropsWidget = nullptr;
    class QDockWidget* m_sketchOptionsDock = nullptr;
    SketchOptionsWidget* m_sketchOptionsWidget = nullptr;
    ConstraintExplorer* m_constraintExplorer = nullptr;
    /// Connections to the canvas are made lazily: the canvas is created by the
    /// ReducedModeWindow/FullModeWindow subclasses, i.e. AFTER this base class
    /// has built its docks, so it does not exist yet at dock-construction time.
    bool m_sketchPanelsBound = false;
    void bindSketchPanels();
    void onRemoveBackgroundImage();

    /// Open the background calibration dialog for the current sketch.
    ///
    /// NON-modal on purpose, and this is the whole reason the dialog sat
    /// unused: calibration works by clicking two points ON THE CANVAS while
    /// the dialog is open. exec() takes application modality, which blocks
    /// exactly those clicks, so the feature cannot work as a modal dialog.
    void onCalibrateBackground();

    /// Push the project directory into the sketch properties panel.
    ///
    /// The panel needs it to decide whether a background image lives inside
    /// the project (reference it by relative path) or outside it (embed the
    /// bytes), and to enable Export Image.
    void syncSketchProjectDir();

    /// Created on first use and kept, so a calibration that was canceled
    /// does not lose the points already picked. Parented to the window, so
    /// Qt owns it.
    BackgroundCalibrationDialog* m_bgCalibrationDialog = nullptr;
    ProjectBrowserWidget* m_projectBrowser = nullptr;  // In Project panel's Files tab
    QTabWidget* m_projectTabs = nullptr;   ///< Objects / Files tabs in the Project dock

    // Feature tree container items
    bool m_suppressObjectsTreeSignal = false;  ///< Reserved: block itemChanged during bulk edits
    ObjectsBrowserWidget* m_objectsBrowser = nullptr;
    ErrorOutlineDelegate* m_propsOutlineDelegate = nullptr;
    QSet<QTreeWidgetItem*> m_badPropertyItems;  ///< Property rows flagged red

    // Current unit system (0=mm, 1=cm, 2=m, 3=in, 4=ft)
    int m_currentUnits = 0;

    // Normal (non-maximized) geometry for save/restore
    QByteArray m_normalGeometry;
    // WM-reported maximized state (from WindowStateChange). Precise on
    // well-behaved window managers; cross-checked with the frame size to
    // cover X11 WMs (fluxbox) that leave it stale after an unmaximize.
    bool m_windowMaximized = false;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_MAINWINDOW_H

