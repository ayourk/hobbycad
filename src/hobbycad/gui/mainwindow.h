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

#include <QMainWindow>

class QAction;
class QLabel;
class QDockWidget;

namespace hobbycad {

class CliPanel;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(const OpenGLInfo& glInfo,
                        QWidget* parent = nullptr);
    ~MainWindow() override;

    /// Access the current document.
    Document& document();

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

    /// Intercept window close to prompt for unsaved changes.
    void closeEvent(QCloseEvent* event) override;

    /// Access the View > Terminal toggle action.
    QAction* terminalToggleAction() const;

    /// Access the View > Reset View action.
    QAction* resetViewAction() const;

    /// Access the View > Rotate Left/Right actions.
    QAction* rotateLeftAction() const;
    QAction* rotateRightAction() const;

    /// Hide the dock-based terminal (used by Reduced Mode which
    /// has its own central CLI panel instead).
    void hideDockTerminal();

    OpenGLInfo m_glInfo;
    Document   m_document;

private slots:
    void onFileNew();
    void onFileOpen();
    void onFileSave();
    void onFileSaveAs();
    void onFileClose();
    void onFileQuit();
    void onEditPreferences();
    void onHelpAbout();

private:
    void createMenus();
    void createStatusBar();
    void createDockPanels();
    void updateTitle();

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
    QAction* m_actionQuit   = nullptr;
    QAction* m_actionAbout  = nullptr;
    QAction* m_actionPreferences = nullptr;
    QAction* m_actionToggleTerminal = nullptr;
    QAction* m_actionToggleFeatureTree = nullptr;
    QAction* m_actionResetView = nullptr;
    QAction* m_actionRotateLeft = nullptr;
    QAction* m_actionRotateRight = nullptr;

    // Status bar
    QLabel* m_statusLabel   = nullptr;
    QLabel* m_glModeLabel   = nullptr;

    // Dock panels
    QDockWidget* m_featureTreeDock = nullptr;
    QDockWidget* m_terminalDock    = nullptr;
    CliPanel*    m_cliPanel        = nullptr;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_MAINWINDOW_H

