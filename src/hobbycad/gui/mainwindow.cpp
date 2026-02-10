// =====================================================================
//  src/hobbycad/gui/mainwindow.cpp — Base main window
// =====================================================================

#include "mainwindow.h"
#include "aboutdialog.h"
#include "clipanel.h"
#include "preferencesdialog.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFileDialog>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
#include <QTreeWidget>

namespace hobbycad {

// Standard file dialog filter — BREP first so Qt uses it as default.
// "All Files" lets the user bypass the auto-extension behavior.
static const QString kBrepFilter =
    QStringLiteral("BREP Files (*.brep *.brp);;All Files (*)");

// Ensure a save path has the .brep extension when the BREP filter is
// selected.  When "All Files" is active, the path is left as-is.
static QString ensureBrepExtension(const QString& path,
                                   const QString& selectedFilter)
{
    if (path.isEmpty())
        return path;

    // If the user chose "All Files", don't touch the extension
    if (selectedFilter.contains(QStringLiteral("*.*")) ||
        selectedFilter.startsWith(QStringLiteral("All")))
        return path;

    QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix.isEmpty())
        return path + QStringLiteral(".brep");

    return path;
}

MainWindow::MainWindow(const OpenGLInfo& glInfo, QWidget* parent)
    : QMainWindow(parent)
    , m_glInfo(glInfo)
{
    setObjectName(QStringLiteral("MainWindow"));
    setMinimumSize(1024, 768);
    createMenus();
    createStatusBar();
    createDockPanels();

    // Start with an empty document
    updateTitle();
}

MainWindow::~MainWindow() = default;

Document& MainWindow::document()
{
    return m_document;
}

CliPanel* MainWindow::cliPanel() const
{
    return m_cliPanel;
}

QAction* MainWindow::terminalToggleAction() const
{
    return m_actionToggleTerminal;
}

QAction* MainWindow::resetViewAction() const
{
    return m_actionResetView;
}

QAction* MainWindow::rotateLeftAction() const
{
    return m_actionRotateLeft;
}

QAction* MainWindow::rotateRightAction() const
{
    return m_actionRotateRight;
}

void MainWindow::hideDockTerminal()
{
    if (m_terminalDock) {
        m_terminalDock->setVisible(false);
        m_terminalDock->setEnabled(false);

        // Disconnect the dock from the toggle action so Reduced Mode
        // can reconnect it to the central CLI panel instead
        disconnect(m_actionToggleTerminal, &QAction::toggled,
                   m_terminalDock, &QDockWidget::setVisible);
        disconnect(m_terminalDock, &QDockWidget::visibilityChanged,
                   m_actionToggleTerminal, &QAction::setChecked);
    }
}

void MainWindow::finalizeLayout()
{
    // Restore window geometry and dock/toolbar state from settings,
    // but only if the user hasn't disabled session restore.
    QSettings settings;
    bool restoreSession = settings.value(
        QStringLiteral("preferences/restoreSession"), true).toBool();

    if (restoreSession) {
        if (settings.contains(QStringLiteral("window/geometry"))) {
            restoreGeometry(
                settings.value(QStringLiteral("window/geometry")).toByteArray());
        }
        if (settings.contains(QStringLiteral("window/state"))) {
            restoreState(
                settings.value(QStringLiteral("window/state")).toByteArray());
        }
    }

    // Sync toggle actions to actual dock visibility after restoreState.
    // Use !isHidden() rather than isVisible() because the main window
    // hasn't been show()n yet — isVisible() always returns false during
    // construction, which would hide every dock via the toggled signal.
    if (m_actionToggleFeatureTree && m_featureTreeDock)
        m_actionToggleFeatureTree->setChecked(!m_featureTreeDock->isHidden());
    if (m_actionToggleTerminal && m_terminalDock)
        m_actionToggleTerminal->setChecked(!m_terminalDock->isHidden());

    updateTitle();
}

// ---- Menus ----------------------------------------------------------

void MainWindow::createMenus()
{
    // File menu
    auto* fileMenu = menuBar()->addMenu(tr("&File"));

    m_actionNew = fileMenu->addAction(tr("&New"),
        QKeySequence::New, this, &MainWindow::onFileNew);

    m_actionOpen = fileMenu->addAction(tr("&Open..."),
        QKeySequence::Open, this, &MainWindow::onFileOpen);

    fileMenu->addSeparator();

    m_actionSave = fileMenu->addAction(tr("&Save"),
        QKeySequence::Save, this, &MainWindow::onFileSave);

    m_actionSaveAs = fileMenu->addAction(tr("Save &As..."),
        QKeySequence::SaveAs, this, &MainWindow::onFileSaveAs);

    fileMenu->addSeparator();

    m_actionClose = fileMenu->addAction(tr("&Close"),
        QKeySequence::Close, this, &MainWindow::onFileClose);

    fileMenu->addSeparator();

    m_actionQuit = fileMenu->addAction(tr("&Quit"),
        QKeySequence::Quit, this, &MainWindow::onFileQuit);

    // Help menu
    auto* helpMenu = menuBar()->addMenu(tr("&Help"));

    m_actionAbout = helpMenu->addAction(tr("&About HobbyCAD..."),
        this, &MainWindow::onHelpAbout);

    // View menu (inserted between File and Help)
    auto* viewMenu = new QMenu(tr("&View"), this);
    menuBar()->insertMenu(helpMenu->menuAction(), viewMenu);

    m_actionToggleTerminal = viewMenu->addAction(tr("&Terminal"),
        QKeySequence(Qt::CTRL | Qt::Key_QuoteLeft));
    m_actionToggleTerminal->setCheckable(true);
    m_actionToggleTerminal->setChecked(false);

    m_actionToggleFeatureTree = viewMenu->addAction(tr("&Feature Tree"),
        QKeySequence(Qt::CTRL | Qt::Key_F));
    m_actionToggleFeatureTree->setCheckable(true);
    m_actionToggleFeatureTree->setChecked(true);

    viewMenu->addSeparator();

    m_actionResetView = viewMenu->addAction(tr("&Reset View"),
        QKeySequence(Qt::Key_Home));
    // Connected in FullModeWindow to viewport->resetCamera()

    m_actionRotateLeft = viewMenu->addAction(tr("Rotate &Left 90°"));

    m_actionRotateRight = viewMenu->addAction(tr("Rotate Ri&ght 90°"));

    viewMenu->addSeparator();

    m_actionPreferences = viewMenu->addAction(tr("&Preferences..."),
        QKeySequence::Preferences, this, &MainWindow::onEditPreferences);
}

// ---- Status bar -----------------------------------------------------

void MainWindow::createStatusBar()
{
    statusBar()->setObjectName(QStringLiteral("StatusBar"));

    m_statusLabel = new QLabel(tr("Ready"));
    m_statusLabel->setObjectName(QStringLiteral("StatusLabel"));
    statusBar()->addWidget(m_statusLabel, 1);

    m_glModeLabel = new QLabel();
    m_glModeLabel->setObjectName(QStringLiteral("GlModeLabel"));
    statusBar()->addPermanentWidget(m_glModeLabel);

    if (m_glInfo.meetsMinimum()) {
        m_glModeLabel->setText(
            tr("OpenGL %1.%2 — %3")
                .arg(m_glInfo.majorVersion)
                .arg(m_glInfo.minorVersion)
                .arg(m_glInfo.renderer));
    } else {
        // Warning triangle + reduced mode indicator
        m_glModeLabel->setText(
            QStringLiteral("\u26A0 ") +
            tr("Reduced Mode — OpenGL 3.3 required"));
        m_glModeLabel->setToolTip(
            tr("The 3D viewport is disabled because OpenGL 3.3+ "
               "was not detected. File operations and geometry "
               "operations still work normally."));
    }
}

// ---- Dock panels ----------------------------------------------------

void MainWindow::createDockPanels()
{
    // Feature tree (empty placeholder for Phase 0)
    m_featureTreeDock = new QDockWidget(tr("Feature Tree"), this);
    m_featureTreeDock->setObjectName(QStringLiteral("FeatureTreeDock"));
    m_featureTreeDock->setAllowedAreas(
        Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    auto* tree = new QTreeWidget();
    tree->setObjectName(QStringLiteral("FeatureTree"));
    tree->setHeaderLabel(tr("Features"));
    tree->setRootIsDecorated(true);
    m_featureTreeDock->setWidget(tree);

    addDockWidget(Qt::LeftDockWidgetArea, m_featureTreeDock);

    // Connect View > Feature Tree toggle to dock visibility
    connect(m_actionToggleFeatureTree, &QAction::toggled,
            m_featureTreeDock, &QDockWidget::setVisible);
    connect(m_featureTreeDock, &QDockWidget::visibilityChanged,
            m_actionToggleFeatureTree, &QAction::setChecked);

    // Embedded terminal panel
    m_terminalDock = new QDockWidget(tr("Terminal"), this);
    m_terminalDock->setObjectName(QStringLiteral("TerminalDock"));
    m_terminalDock->setAllowedAreas(
        Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);

    m_cliPanel = new CliPanel(this);
    m_terminalDock->setWidget(m_cliPanel);

    addDockWidget(Qt::BottomDockWidgetArea, m_terminalDock);

    // Start hidden — toggled via View > Terminal or Ctrl+`
    m_terminalDock->setVisible(false);

    // Connect the toggle action to the dock visibility
    connect(m_actionToggleTerminal, &QAction::toggled,
            m_terminalDock, &QDockWidget::setVisible);
    connect(m_terminalDock, &QDockWidget::visibilityChanged,
            m_actionToggleTerminal, &QAction::setChecked);

    // Focus the input line when the dock becomes visible
    connect(m_terminalDock, &QDockWidget::visibilityChanged,
            this, [this](bool visible) {
        if (visible && m_cliPanel) {
            m_cliPanel->focusInput();
        }
    });

    // Handle exit request from the terminal panel — close the app
    connect(m_cliPanel, &CliPanel::exitRequested,
            this, [this]() {
        close();
    });
}

// ---- Slots ----------------------------------------------------------

void MainWindow::onFileNew()
{
    if (!maybeSave()) return;

    m_document.createTestSolid();
    updateTitle();
    onDocumentLoaded();
    m_statusLabel->setText(tr("New document created"));
}

void MainWindow::onFileOpen()
{
    if (!maybeSave()) return;

    QString selectedFilter;
    QString path = QFileDialog::getOpenFileName(this,
        tr("Open BREP File"),
        QString(),
        kBrepFilter,
        &selectedFilter);

    if (path.isEmpty()) return;

    // If the file doesn't exist and the BREP filter is active,
    // try appending .brep before giving up
    if (!QFileInfo::exists(path)) {
        QString withExt = ensureBrepExtension(path, selectedFilter);
        if (withExt != path && QFileInfo::exists(withExt))
            path = withExt;
    }

    if (m_document.loadBrep(path)) {
        updateTitle();
        onDocumentLoaded();
        m_statusLabel->setText(tr("Opened: %1").arg(path));
    } else {
        QMessageBox::warning(this,
            tr("Open Failed"),
            tr("Could not open file:\n%1").arg(path));
    }
}

void MainWindow::onFileSave()
{
    if (m_document.isNew()) {
        onFileSaveAs();
        return;
    }

    if (m_document.saveBrep()) {
        updateTitle();
        m_statusLabel->setText(
            tr("Saved: %1").arg(m_document.filePath()));
    } else {
        QMessageBox::warning(this,
            tr("Save Failed"),
            tr("Could not save file:\n%1").arg(m_document.filePath()));
    }
}

void MainWindow::onFileSaveAs()
{
    QString selectedFilter;
    QString path = QFileDialog::getSaveFileName(this,
        tr("Save BREP File"),
        QString(),
        kBrepFilter,
        &selectedFilter);

    if (path.isEmpty()) return;

    path = ensureBrepExtension(path, selectedFilter);

    if (m_document.saveBrep(path)) {
        updateTitle();
        m_statusLabel->setText(tr("Saved: %1").arg(path));
    } else {
        QMessageBox::warning(this,
            tr("Save Failed"),
            tr("Could not save file:\n%1").arg(path));
    }
}

void MainWindow::onFileQuit()
{
    close();  // triggers closeEvent()
}

void MainWindow::onFileClose()
{
    if (!maybeSave()) return;

    m_document.clear();
    m_document.setModified(false);
    updateTitle();
    onDocumentClosed();
    m_statusLabel->setText(tr("Document closed"));
}

// ---- Close event / unsaved changes ---------------------------------

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (maybeSave()) {
        // Save window geometry and dock/toolbar state
        QSettings settings;
        settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
        settings.setValue(QStringLiteral("window/state"), saveState());
        event->accept();
    } else {
        event->ignore();
    }
}

bool MainWindow::maybeSave()
{
    if (!m_document.isModified()) {
        return true;  // nothing to save — proceed
    }

    QMessageBox msgBox(this);
    msgBox.setWindowTitle(tr("Unsaved Changes"));
    msgBox.setText(tr("The document has been modified."));
    msgBox.setInformativeText(tr("Do you want to save your changes?"));
    msgBox.setIcon(QMessageBox::Warning);

    QPushButton* discardBtn = msgBox.addButton(
        tr("Close Without Saving"), QMessageBox::DestructiveRole);
    QPushButton* saveBtn = msgBox.addButton(
        tr("Save and Close"), QMessageBox::AcceptRole);
    QPushButton* cancelBtn = msgBox.addButton(
        tr("Cancel"), QMessageBox::RejectRole);

    msgBox.setDefaultButton(saveBtn);

    msgBox.exec();

    QAbstractButton* clicked = msgBox.clickedButton();

    if (clicked == cancelBtn) {
        return false;  // user cancelled — don't close
    }

    if (clicked == saveBtn) {
        // Try to save — if the doc is new, prompt for a filename
        if (m_document.isNew()) {
            QString selectedFilter;
            QString path = QFileDialog::getSaveFileName(this,
                tr("Save BREP File"),
                QString(),
                kBrepFilter,
                &selectedFilter);

            if (path.isEmpty()) {
                return false;  // user cancelled the save dialog
            }

            path = ensureBrepExtension(path, selectedFilter);

            if (!m_document.saveBrep(path)) {
                QMessageBox::warning(this,
                    tr("Save Failed"),
                    tr("Could not save file:\n%1").arg(path));
                return false;  // save failed — don't close
            }
        } else {
            if (!m_document.saveBrep()) {
                QMessageBox::warning(this,
                    tr("Save Failed"),
                    tr("Could not save file:\n%1")
                        .arg(m_document.filePath()));
                return false;
            }
        }
    }

    // discardBtn or successful save — proceed with close
    return true;
}

void MainWindow::onHelpAbout()
{
    AboutDialog dlg(m_glInfo, this);
    dlg.exec();
}

void MainWindow::onEditPreferences()
{
    PreferencesDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        applyPreferences();
    }
}

void MainWindow::applyPreferences()
{
    // Subclasses override to apply settings to the viewport, etc.
}

// ---- Helpers --------------------------------------------------------

void MainWindow::updateTitle()
{
    QString title = QStringLiteral("HobbyCAD");

    if (!m_document.isNew()) {
        title += QStringLiteral(" — ") + m_document.filePath();
    } else {
        title += QStringLiteral(" — [New Document]");
    }

    if (m_document.isModified()) {
        title += QStringLiteral(" *");
    }

    setWindowTitle(title);
}

}  // namespace hobbycad

