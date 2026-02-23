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

#include <hobbycad/document.h>
#include <hobbycad/opengl_info.h>
#include <hobbycad/project.h>
#include <hobbycad/units.h>

#include <QMainWindow>

class QAction;
class QLabel;
class QDockWidget;
class QTreeWidget;
class QTreeWidgetItem;

namespace hobbycad {
namespace sketch {
struct Entity;
struct Constraint;
}  // namespace sketch

class CliPanel;
class ProjectBrowserWidget;
class SketchActionBar;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(const OpenGLInfo& glInfo,
                        QWidget* parent = nullptr);
    ~MainWindow() override;

    /// Access the current document (legacy BREP-only mode).
    Document& document();

    /// Access the current project.
    Project& project();

    /// Access the embedded CLI panel (may be nullptr if not created).
    CliPanel* cliPanel() const;

protected:
    /// Called by subclasses after setting the central widget.
    void finalizeLayout();

    /// Override to update the viewport when a new document is loaded.
    virtual void onDocumentLoaded() {}

    /// Override to clear the viewport when a document is closed.
    virtual void onDocumentClosed() {}

    /// Override to apply changed preferences to the viewport.
    virtual void applyPreferences();

    /// Override to provide the active sketch canvas (for sketch export).
    virtual class SketchCanvas* activeSketchCanvas() const { return nullptr; }

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

    /// Track resize to save normal geometry when not maximized.
    void resizeEvent(QResizeEvent* event) override;

    /// Track move to save normal geometry when not maximized.
    void moveEvent(QMoveEvent* event) override;

    /// Access the View > Terminal toggle action.
    QAction* terminalToggleAction() const;

    /// Access the View > Reset View action.
    QAction* resetViewAction() const;

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

    /// Access the sketch action bar (Save/Cancel buttons)
    SketchActionBar* sketchActionBar() const;

    /// Show or hide the sketch action bar
    void setSketchActionBarVisible(bool visible);

    /// Add a sketch to the feature tree
    void addSketchToTree(const QString& name, int index);

    /// Select a sketch in the feature tree
    void selectSketchInTree(int index);

    /// Clear all sketches from the feature tree
    void clearSketchesInTree();

    /// Add a body to the feature tree
    void addBodyToTree(const QString& name, int index);

    /// Clear all bodies from the feature tree
    void clearBodiesInTree();

    /// Add a construction plane to the feature tree
    void addConstructionPlaneToTree(const QString& name, int id);

    /// Select a construction plane in the feature tree
    void selectConstructionPlaneInTree(int id);

    /// Clear all construction planes from the feature tree
    void clearConstructionPlanesInTree();

    /// Set document units from project
    void setUnitsFromString(const QString& units);

    /// Get the current unit system index (0=mm, 1=cm, 2=m, 3=in, 4=ft).
    int currentUnits() const;

    /// Get the current unit as a LengthUnit enum.
    LengthUnit currentLengthUnit() const;

    /// Get the current unit suffix string (e.g., "mm", "in").
    QString unitSuffix() const;

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

    /// Emitted when a sketch is selected in the feature tree.
    void sketchSelectedInTree(int sketchIndex);

protected:
    /// Hide the dock-based terminal (used by Reduced Mode which
    /// has its own central CLI panel instead).
    void hideDockTerminal();

    OpenGLInfo m_glInfo;
    Document   m_document;
    Project    m_project;

    // Objects tree (protected for subclass access)
    QTreeWidget* m_objectsTree = nullptr;

private slots:
    void onFileNew();
    void onFileOpen();
    void onFileSave();
    void onFileSaveAs();
    void onFileClose();
    void onFileQuit();
    void onFileImportStep();
    void onFileExportStep();
    void onFileExportStl();
    void onFileExportDXF();
    void onFileExportSVG();
    void onEditPreferences();
    void onHelpAbout();

private:
    void createMenus();
    void createStatusBar();
    void createDockPanels();
    void updateTitle();
    void applyBindings();

    /// If the document has unsaved changes, show a dialog offering
    /// "Close Without Saving", "Save and Close", or "Cancel".
    /// Returns true if the caller should proceed (close/quit).
    /// Returns false if the user cancelled.
    bool maybeSave();

    // Menus
    QAction* m_actionNew    = nullptr;
    QAction* m_actionOpen   = nullptr;
    QAction* m_actionSave   = nullptr;
    QAction* m_actionSaveAs = nullptr;
    QAction* m_actionClose  = nullptr;
    QAction* m_actionImportStep = nullptr;
    QAction* m_actionExportStep = nullptr;
    QAction* m_actionExportStl = nullptr;
    QAction* m_actionExportDXF = nullptr;
    QAction* m_actionExportSVG = nullptr;
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
    QAction* m_actionToggleTerminal = nullptr;
    QAction* m_actionToggleFeatureTree = nullptr;
    QAction* m_actionToggleProperties = nullptr;
    QAction* m_actionToggleToolbar = nullptr;
    QAction* m_actionResetView = nullptr;
    QAction* m_actionRotateLeft = nullptr;
    QAction* m_actionRotateRight = nullptr;
    QAction* m_actionShowGrid = nullptr;
    QAction* m_actionSnapToGrid = nullptr;
    QAction* m_actionZUp = nullptr;
    QAction* m_actionOrbitSelected = nullptr;

    // Construct menu
    QAction* m_actionNewConstructionPlane = nullptr;

    // Status bar
    QLabel* m_statusLabel   = nullptr;
    QLabel* m_glModeLabel   = nullptr;

    // Dock panels
    QDockWidget* m_featureTreeDock = nullptr;
    QDockWidget* m_propertiesDock  = nullptr;
    QDockWidget* m_terminalDock    = nullptr;
    CliPanel*    m_cliPanel        = nullptr;
    QTreeWidget* m_propertiesTree  = nullptr;
    SketchActionBar* m_sketchActionBar = nullptr;
    ProjectBrowserWidget* m_projectBrowser = nullptr;  // In Project panel's Files tab

    // Feature tree container items
    QTreeWidgetItem* m_sketchesTreeItem = nullptr;
    QTreeWidgetItem* m_bodiesTreeItem = nullptr;
    QTreeWidgetItem* m_constructionTreeItem = nullptr;

    // Current unit system (0=mm, 1=cm, 2=m, 3=in, 4=ft)
    int m_currentUnits = 0;

    // Normal (non-maximized) geometry for save/restore
    QByteArray m_normalGeometry;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_MAINWINDOW_H

