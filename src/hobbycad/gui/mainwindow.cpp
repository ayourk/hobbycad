// =====================================================================
//  src/hobbycad/gui/mainwindow.cpp — Base main window
// =====================================================================

#include "mainwindow.h"
#include "aboutdialog.h"
#include "bindingsdialog.h"
#include "changelogpanel.h"
#include "clipanel.h"
#include "formulafield.h"
#include "modeltoolbar.h"
#include "parametersdialog.h"
#include "preferencesdialog.h"
#include "projectbrowserwidget.h"
#include "sketchactionbar.h"
#include "sketchplanedialog.h"
#include "sketchtoolbar.h"
#include "timelinewidget.h"

#include "sketchcanvas.h"

#include <hobbycad/project.h>
#include <hobbycad/sketch/export.h>
#include <hobbycad/step_io.h>
#include <hobbycad/stl_io.h>
#include <hobbycad/units.h>

#include <QAction>
#include <QActionGroup>
#include <QKeySequence>
#include <QApplication>
#include <QCloseEvent>
#include <QMoveEvent>
#include <QResizeEvent>
#include <QWindowStateChangeEvent>
#include <QComboBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtMath>

#include <cmath>
#include <limits>

namespace hobbycad {

// Standard file dialog filters
// Project format (.hcad) is the native format.
// BREP format is supported for import/export of raw geometry.
static const char kProjectFilter[] =
    QT_TR_NOOP("HobbyCAD Projects (*.hcad)");
static const char kBrepFilter[] =
    QT_TR_NOOP("BREP Files (*.brep *.brp)");
static const char kAllFilesFilter[] =
    QT_TR_NOOP("All Files (*)");
static const char kOpenFilter[] =
    QT_TR_NOOP("HobbyCAD Projects (*.hcad);;BREP Files (*.brep *.brp);;All Files (*)");
static const char kSaveFilter[] =
    QT_TR_NOOP("HobbyCAD Projects (*.hcad);;BREP Files (*.brep *.brp)");

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

Project& MainWindow::project()
{
    return m_project;
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

QAction* MainWindow::showGridAction() const
{
    return m_actionShowGrid;
}

QAction* MainWindow::snapToGridAction() const
{
    return m_actionSnapToGrid;
}

QAction* MainWindow::zUpAction() const
{
    return m_actionZUp;
}

QAction* MainWindow::orbitSelectedAction() const
{
    return m_actionOrbitSelected;
}

QAction* MainWindow::toolbarToggleAction() const
{
    return m_actionToggleToolbar;
}

QAction* MainWindow::newConstructionPlaneAction() const
{
    return m_actionNewConstructionPlane;
}

QAction* MainWindow::undoAction() const
{
    return m_actionUndo;
}

QAction* MainWindow::redoAction() const
{
    return m_actionRedo;
}

QAction* MainWindow::deleteAction() const
{
    return m_actionDelete;
}

QAction* MainWindow::selectAllAction() const
{
    return m_actionSelectAll;
}

QTreeWidget* MainWindow::propertiesTree() const
{
    return m_propertiesTree;
}

SketchActionBar* MainWindow::sketchActionBar() const
{
    return m_sketchActionBar;
}

hobbycad::ChangelogPanel* MainWindow::changelogPanel() const
{
    return m_changelogPanel;
}

void MainWindow::setSketchActionBarVisible(bool visible)
{
    if (m_sketchActionBar) {
        m_sketchActionBar->setVisible(visible);
    }
}

int MainWindow::currentUnits() const
{
    return m_currentUnits;
}

LengthUnit MainWindow::currentLengthUnit() const
{
    return lengthUnitFromIndex(m_currentUnits);
}

QString MainWindow::unitSuffix() const
{
    return QString::fromLatin1(hobbycad::unitSuffix(currentLengthUnit()));
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
            QByteArray geom = settings.value(
                QStringLiteral("window/geometry")).toByteArray();
            restoreGeometry(geom);
            // Store as normal geometry in case we're not maximized
            m_normalGeometry = geom;
        }
        if (settings.contains(QStringLiteral("window/state"))) {
            restoreState(
                settings.value(QStringLiteral("window/state")).toByteArray());
        }
        // Restore maximized state if it was saved
        if (settings.value(QStringLiteral("window/maximized"), false).toBool()) {
            showMaximized();
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
    if (m_actionToggleChangelog && m_changelogDock)
        m_actionToggleChangelog->setChecked(!m_changelogDock->isHidden());

    // Apply keyboard bindings from settings
    applyBindings();

    updateTitle();
}

// ---- Menus ----------------------------------------------------------

void MainWindow::createMenus()
{
    // File menu
    auto* fileMenu = menuBar()->addMenu(tr("&File"));

    m_actionNew = fileMenu->addAction(tr("&New"), this, &MainWindow::onFileNew);
    m_actionNew->setShortcut(QKeySequence::New);

    m_actionOpen = fileMenu->addAction(tr("&Open..."), this, &MainWindow::onFileOpen);
    m_actionOpen->setShortcut(QKeySequence::Open);

    fileMenu->addSeparator();

    m_actionSave = fileMenu->addAction(tr("&Save"), this, &MainWindow::onFileSave);
    m_actionSave->setShortcut(QKeySequence::Save);

    m_actionSaveAs = fileMenu->addAction(tr("Save &As..."), this, &MainWindow::onFileSaveAs);
    m_actionSaveAs->setShortcut(QKeySequence::SaveAs);

    fileMenu->addSeparator();

    m_actionClose = fileMenu->addAction(tr("&Close"), this, &MainWindow::onFileClose);
    m_actionClose->setShortcut(QKeySequence::Close);

    fileMenu->addSeparator();

    // Import submenu
    auto* importMenu = fileMenu->addMenu(tr("&Import"));
    m_actionImportStep = importMenu->addAction(tr("STEP File..."),
        this, &MainWindow::onFileImportStep);
    m_actionImportStep->setToolTip(tr("Import geometry from STEP file"));

    // Export submenu
    auto* exportMenu = fileMenu->addMenu(tr("&Export"));
    m_actionExportStep = exportMenu->addAction(tr("STEP File..."),
        this, &MainWindow::onFileExportStep);
    m_actionExportStep->setToolTip(tr("Export geometry to STEP file"));

    m_actionExportStl = exportMenu->addAction(tr("STL File..."),
        this, &MainWindow::onFileExportStl);
    m_actionExportStl->setToolTip(tr("Export geometry to STL file for 3D printing"));

    exportMenu->addSeparator();

    m_actionExportDXF = exportMenu->addAction(tr("DXF File (Sketch)..."),
        this, &MainWindow::onFileExportDXF);
    m_actionExportDXF->setToolTip(tr("Export sketch to DXF file"));
    m_actionExportDXF->setEnabled(false);

    m_actionExportSVG = exportMenu->addAction(tr("SVG File (Sketch)..."),
        this, &MainWindow::onFileExportSVG);
    m_actionExportSVG->setToolTip(tr("Export sketch to SVG file"));
    m_actionExportSVG->setEnabled(false);

    fileMenu->addSeparator();

    m_actionQuit = fileMenu->addAction(tr("&Quit"), this, &MainWindow::onFileQuit);
    m_actionQuit->setShortcut(QKeySequence::Quit);

    // Edit menu
    auto* editMenu = menuBar()->addMenu(tr("&Edit"));

    m_actionUndo = editMenu->addAction(tr("&Undo"));
    m_actionUndo->setShortcut(QKeySequence::Undo);
    m_actionUndo->setEnabled(false);  // Enabled when undo stack is not empty

    m_actionRedo = editMenu->addAction(tr("&Redo"));
    m_actionRedo->setShortcuts({QKeySequence::Redo,
                                QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z),
                                QKeySequence(Qt::CTRL | Qt::Key_Y)});
    m_actionRedo->setEnabled(false);  // Enabled when redo stack is not empty

    editMenu->addSeparator();

    m_actionCut = editMenu->addAction(tr("Cu&t"));
    m_actionCut->setShortcut(QKeySequence::Cut);
    m_actionCut->setEnabled(false);  // Enabled when selection exists

    m_actionCopy = editMenu->addAction(tr("&Copy"));
    m_actionCopy->setShortcut(QKeySequence::Copy);
    m_actionCopy->setEnabled(false);  // Enabled when selection exists

    m_actionPaste = editMenu->addAction(tr("&Paste"));
    m_actionPaste->setShortcut(QKeySequence::Paste);
    m_actionPaste->setEnabled(false);  // Enabled when clipboard has compatible data

    m_actionDelete = editMenu->addAction(tr("&Delete"));
    m_actionDelete->setShortcut(QKeySequence::Delete);
    m_actionDelete->setEnabled(false);  // Enabled when selection exists

    editMenu->addSeparator();

    m_actionSelectAll = editMenu->addAction(tr("Select &All"));
    m_actionSelectAll->setShortcut(QKeySequence::SelectAll);
    m_actionSelectAll->setEnabled(false);  // Enabled when document has selectable items

    // Construct menu
    auto* constructMenu = menuBar()->addMenu(tr("&Construct"));

    m_actionNewConstructionPlane = constructMenu->addAction(tr("New Construction &Plane..."));
    m_actionNewConstructionPlane->setToolTip(tr("Create a new construction plane"));
    // Connected in FullModeWindow to open dialog

    // Help menu
    auto* helpMenu = menuBar()->addMenu(tr("&Help"));

    m_actionAbout = helpMenu->addAction(tr("&About HobbyCAD..."),
        this, &MainWindow::onHelpAbout);

    // View menu (inserted between Edit and Help)
    auto* viewMenu = new QMenu(tr("&View"), this);
    menuBar()->insertMenu(helpMenu->menuAction(), viewMenu);

    m_actionToggleTerminal = viewMenu->addAction(tr("&Terminal"));
    m_actionToggleTerminal->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_QuoteLeft));
    m_actionToggleTerminal->setCheckable(true);
    m_actionToggleTerminal->setChecked(false);

    m_actionToggleFeatureTree = viewMenu->addAction(tr("P&roject"));
    m_actionToggleFeatureTree->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
    m_actionToggleFeatureTree->setCheckable(true);
    m_actionToggleFeatureTree->setChecked(true);

    m_actionToggleProperties = viewMenu->addAction(tr("&Properties"));
    m_actionToggleProperties->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_P));
    m_actionToggleProperties->setCheckable(true);
    m_actionToggleProperties->setChecked(true);

    m_actionToggleToolbar = viewMenu->addAction(tr("Tool&bar"));
    m_actionToggleToolbar->setCheckable(true);
    m_actionToggleToolbar->setChecked(true);

    m_actionToggleChangelog = viewMenu->addAction(tr("Change &History"));
    m_actionToggleChangelog->setCheckable(true);
    m_actionToggleChangelog->setChecked(false);

    viewMenu->addSeparator();

    // Workspace submenu
    auto* workspaceMenu = viewMenu->addMenu(tr("&Workspace"));
    auto* workspaceGroup = new QActionGroup(this);
    workspaceGroup->setExclusive(true);

    auto* designAction = workspaceMenu->addAction(tr("&Design"));
    designAction->setCheckable(true);
    designAction->setChecked(true);
    workspaceGroup->addAction(designAction);
    connect(designAction, &QAction::triggered, this, [this]() {
        emit workspaceChanged(Workspace::Design);
    });

    auto* renderAction = workspaceMenu->addAction(tr("&Render"));
    renderAction->setCheckable(true);
    workspaceGroup->addAction(renderAction);
    connect(renderAction, &QAction::triggered, this, [this]() {
        emit workspaceChanged(Workspace::Render);
    });

    auto* animationAction = workspaceMenu->addAction(tr("&Animation"));
    animationAction->setCheckable(true);
    workspaceGroup->addAction(animationAction);
    connect(animationAction, &QAction::triggered, this, [this]() {
        emit workspaceChanged(Workspace::Animation);
    });

    auto* simulationAction = workspaceMenu->addAction(tr("&Simulation"));
    simulationAction->setCheckable(true);
    workspaceGroup->addAction(simulationAction);
    connect(simulationAction, &QAction::triggered, this, [this]() {
        emit workspaceChanged(Workspace::Simulation);
    });

    viewMenu->addSeparator();

    m_actionResetView = viewMenu->addAction(tr("&Reset View"));
    m_actionResetView->setShortcut(QKeySequence(Qt::Key_Home));
    // Connected in FullModeWindow to viewport->resetCamera()

    m_actionRotateLeft = viewMenu->addAction(tr("Rotate &Left 90°"));

    m_actionRotateRight = viewMenu->addAction(tr("Rotate Ri&ght 90°"));

    viewMenu->addSeparator();

    m_actionShowGrid = viewMenu->addAction(tr("Show &Grid"));
    m_actionShowGrid->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_G));
    m_actionShowGrid->setCheckable(true);
    m_actionShowGrid->setChecked(true);  // On by default

    m_actionSnapToGrid = viewMenu->addAction(tr("&Snap to Grid"));
    m_actionSnapToGrid->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_G));
    m_actionSnapToGrid->setCheckable(true);
    m_actionSnapToGrid->setChecked(false);  // Off by default

    // Snap to grid is only available when grid is visible
    connect(m_actionShowGrid, &QAction::toggled, this, [this](bool visible) {
        m_actionSnapToGrid->setEnabled(visible);
        if (!visible) {
            m_actionSnapToGrid->setChecked(false);
        }
    });

    viewMenu->addSeparator();

    m_actionZUp = viewMenu->addAction(tr("&Z-Up Orientation"));
    m_actionZUp->setCheckable(true);
    m_actionZUp->setChecked(true);  // Z-up is the default
    // Connected in FullModeWindow to handle coordinate system change

    m_actionOrbitSelected = viewMenu->addAction(tr("&Orbit Selected Object"));
    m_actionOrbitSelected->setCheckable(true);
    m_actionOrbitSelected->setChecked(false);  // Off by default
    // Connected in FullModeWindow to viewport

    viewMenu->addSeparator();

    m_actionPreferences = viewMenu->addAction(tr("&Preferences..."), this, &MainWindow::onEditPreferences);
    m_actionPreferences->setShortcut(QKeySequence::Preferences);
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
                .arg(QString::fromStdString(m_glInfo.renderer)));
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
    // Project panel with File and Objects tabs
    m_featureTreeDock = new QDockWidget(tr("Project"), this);
    m_featureTreeDock->setObjectName(QStringLiteral("ProjectDock"));
    m_featureTreeDock->setAllowedAreas(
        Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    auto* projectTabs = new QTabWidget();
    projectTabs->setObjectName(QStringLiteral("ProjectTabs"));

    // Files tab - shows project file structure with the ProjectBrowserWidget
    m_projectBrowser = new ProjectBrowserWidget();
    projectTabs->addTab(m_projectBrowser, tr("Files"));

    // Objects tab - shows model objects/features (default)
    m_objectsTree = new QTreeWidget();
    m_objectsTree->setObjectName(QStringLiteral("ObjectsTree"));
    m_objectsTree->setHeaderHidden(true);
    m_objectsTree->setRootIsDecorated(true);

    // Document Settings section
    auto* docSettings = new QTreeWidgetItem(m_objectsTree);
    docSettings->setText(0, tr("Document Settings"));
    docSettings->setExpanded(true);

    auto* unitsItem = new QTreeWidgetItem(docSettings);
    unitsItem->setText(0, tr("Units: mm"));
    unitsItem->setData(0, Qt::UserRole, QStringLiteral("units"));

    // Double-click on units shows a combobox dropdown
    connect(m_objectsTree, &QTreeWidget::itemDoubleClicked,
            this, [this](QTreeWidgetItem* item, int column) {
        Q_UNUSED(column);
        if (item->data(0, Qt::UserRole).toString() != QStringLiteral("units"))
            return;

        auto* combo = new QComboBox(m_objectsTree);
        combo->addItems({tr("mm"), tr("cm"), tr("m"), tr("in"), tr("ft")});

        // Select current unit
        QString currentText = item->text(0);
        int colonPos = currentText.indexOf(':');
        if (colonPos >= 0) {
            QString currentUnit = currentText.mid(colonPos + 2).trimmed();
            int idx = combo->findText(currentUnit);
            if (idx >= 0) combo->setCurrentIndex(idx);
        }

        m_objectsTree->setItemWidget(item, 0, combo);
        combo->showPopup();

        // When user selects an item, update and remove the widget
        connect(combo, &QComboBox::activated, this,
                [this, item, combo](int index) {
            item->setText(0, tr("Units: %1").arg(combo->currentText()));
            m_currentUnits = index;
            // Defer widget removal to avoid deleting during signal
            QTimer::singleShot(0, this, [this, item, index]() {
                m_objectsTree->setItemWidget(item, 0, nullptr);
                emit unitsChanged(index);
            });
        });
    });

    // Origin section
    auto* origin = new QTreeWidgetItem(m_objectsTree);
    origin->setText(0, tr("Origin"));
    origin->setExpanded(false);

    auto* originXY = new QTreeWidgetItem(origin);
    originXY->setText(0, tr("XY Plane"));
    originXY->setData(0, Qt::UserRole, QStringLiteral("origin_plane"));
    originXY->setData(0, Qt::UserRole + 1, static_cast<int>(SketchPlane::XY));
    auto* originXZ = new QTreeWidgetItem(origin);
    originXZ->setText(0, tr("XZ Plane"));
    originXZ->setData(0, Qt::UserRole, QStringLiteral("origin_plane"));
    originXZ->setData(0, Qt::UserRole + 1, static_cast<int>(SketchPlane::XZ));
    auto* originYZ = new QTreeWidgetItem(origin);
    originYZ->setText(0, tr("YZ Plane"));
    originYZ->setData(0, Qt::UserRole, QStringLiteral("origin_plane"));
    originYZ->setData(0, Qt::UserRole + 1, static_cast<int>(SketchPlane::YZ));
    auto* originX = new QTreeWidgetItem(origin);
    originX->setText(0, tr("X Axis"));
    auto* originY = new QTreeWidgetItem(origin);
    originY->setText(0, tr("Y Axis"));
    auto* originZ = new QTreeWidgetItem(origin);
    originZ->setText(0, tr("Z Axis"));
    auto* originPt = new QTreeWidgetItem(origin);
    originPt->setText(0, tr("Origin Point"));

    // Bodies section
    m_bodiesTreeItem = new QTreeWidgetItem(m_objectsTree);
    m_bodiesTreeItem->setText(0, tr("Bodies"));
    m_bodiesTreeItem->setData(0, Qt::UserRole, QStringLiteral("container.bodies"));
    m_bodiesTreeItem->setExpanded(true);

    // Sketches section
    m_sketchesTreeItem = new QTreeWidgetItem(m_objectsTree);
    m_sketchesTreeItem->setText(0, tr("Sketches"));
    m_sketchesTreeItem->setData(0, Qt::UserRole, QStringLiteral("container.sketches"));
    m_sketchesTreeItem->setExpanded(true);

    // Construction section
    m_constructionTreeItem = new QTreeWidgetItem(m_objectsTree);
    m_constructionTreeItem->setText(0, tr("Construction"));
    m_constructionTreeItem->setData(0, Qt::UserRole, QStringLiteral("container.construction"));
    m_constructionTreeItem->setExpanded(false);

    // Bodies, Sketches, and Construction containers are empty by default
    // Items are added as the user creates features

    projectTabs->addTab(m_objectsTree, tr("Objects"));

    // Select Objects tab by default
    projectTabs->setCurrentIndex(1);

    // F2 to edit selected item in objects tree
    auto* objectsF2 = new QShortcut(QKeySequence(Qt::Key_F2), m_objectsTree);
    connect(objectsF2, &QShortcut::activated, this, [this]() {
        QTreeWidgetItem* item = m_objectsTree->currentItem();
        if (item && (item->flags() & Qt::ItemIsEditable)) {
            m_objectsTree->editItem(item, 0);
        }
    });

    // Handle item selection in objects tree
    connect(m_objectsTree, &QTreeWidget::currentItemChanged,
            this, [this](QTreeWidgetItem* current, QTreeWidgetItem*) {
        if (!current) return;

        QString itemType = current->data(0, Qt::UserRole).toString();

        if (itemType == QStringLiteral("construction_plane")) {
            int planeId = current->data(0, Qt::UserRole + 1).toInt();
            emit constructionPlaneSelected(planeId);
        } else if (itemType == QStringLiteral("sketch")) {
            int sketchIdx = current->data(0, Qt::UserRole + 1).toInt();
            emit sketchSelectedInTree(sketchIdx);
        }
    });

    m_featureTreeDock->setWidget(projectTabs);

    addDockWidget(Qt::LeftDockWidgetArea, m_featureTreeDock);

    // Connect View > Project toggle to dock visibility
    connect(m_actionToggleFeatureTree, &QAction::toggled,
            m_featureTreeDock, &QDockWidget::setVisible);
    connect(m_featureTreeDock, &QDockWidget::visibilityChanged,
            m_actionToggleFeatureTree, &QAction::setChecked);

    // Properties panel (shows properties of selected timeline/tree item)
    m_propertiesDock = new QDockWidget(tr("Properties"), this);
    m_propertiesDock->setObjectName(QStringLiteral("PropertiesDock"));
    m_propertiesDock->setAllowedAreas(
        Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_propertiesDock->setMinimumWidth(150);  // Ensure title "Properties" isn't clipped

    // Container widget with tree and action bar
    auto* propsContainer = new QWidget();
    auto* propsLayout = new QVBoxLayout(propsContainer);
    propsLayout->setContentsMargins(0, 0, 0, 0);
    propsLayout->setSpacing(0);

    m_propertiesTree = new QTreeWidget();
    m_propertiesTree->setObjectName(QStringLiteral("PropertiesTree"));
    m_propertiesTree->setColumnCount(2);
    m_propertiesTree->setHeaderLabels({tr("Property"), tr("Value")});
    m_propertiesTree->setRootIsDecorated(true);
    propsLayout->addWidget(m_propertiesTree, 1);  // stretch factor 1

    // Sketch action bar (Save/Cancel) - hidden by default
    m_sketchActionBar = new SketchActionBar(propsContainer);
    m_sketchActionBar->setVisible(false);
    propsLayout->addWidget(m_sketchActionBar);

    m_propertiesDock->setWidget(propsContainer);

    // F2 to edit selected item in properties tree (column 1 = value)
    auto* propsF2 = new QShortcut(QKeySequence(Qt::Key_F2), m_propertiesTree);
    connect(propsF2, &QShortcut::activated, this, [this]() {
        QTreeWidgetItem* item = m_propertiesTree->currentItem();
        if (item && (item->flags() & Qt::ItemIsEditable)) {
            m_propertiesTree->editItem(item, 1);  // Edit value column
        }
    });

    addDockWidget(Qt::RightDockWidgetArea, m_propertiesDock);

    // Connect View > Properties toggle to dock visibility
    connect(m_actionToggleProperties, &QAction::toggled,
            m_propertiesDock, &QDockWidget::setVisible);
    connect(m_propertiesDock, &QDockWidget::visibilityChanged,
            m_actionToggleProperties, &QAction::setChecked);

    // Changelog / undo history panel
    m_changelogDock = new QDockWidget(tr("History"), this);
    m_changelogDock->setObjectName(QStringLiteral("ChangelogDock"));
    m_changelogDock->setAllowedAreas(
        Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    m_changelogPanel = new hobbycad::ChangelogPanel(m_changelogDock);
    m_changelogDock->setWidget(m_changelogPanel);

    addDockWidget(Qt::RightDockWidgetArea, m_changelogDock);

    // Start hidden — toggled via View > Change History
    m_changelogDock->setVisible(false);

    // Connect View > Change History toggle to dock visibility
    connect(m_actionToggleChangelog, &QAction::toggled,
            m_changelogDock, &QDockWidget::setVisible);
    connect(m_changelogDock, &QDockWidget::visibilityChanged,
            m_actionToggleChangelog, &QAction::setChecked);

    // Embedded terminal panel
    m_terminalDock = new QDockWidget(tr("Terminal"), this);
    m_terminalDock->setObjectName(QStringLiteral("TerminalDock"));
    m_terminalDock->setAllowedAreas(
        Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);

    m_cliPanel = new CliPanel(this);
    m_cliPanel->setGuiMode(true);  // We're in GUI mode, show warnings for missing viewport
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

    // Connect project browser signals (m_projectBrowser is created above in the Project tabs)
    connect(m_projectBrowser, &ProjectBrowserWidget::openCadFileRequested,
            this, [this](const QString& relativePath) {
        // Handle opening CAD files (sketches, etc.)
        Q_UNUSED(relativePath);
        // TODO: Implement opening sketches from the file browser
    });

    connect(m_projectBrowser, &ProjectBrowserWidget::foreignFilesChanged,
            this, [this]() {
        // Mark project as modified when foreign files change
        m_project.setModified(true);
        updateTitle();
    });
}

// ---- Slots ----------------------------------------------------------

void MainWindow::onFileNew()
{
    if (!maybeSave()) return;

    m_document.clear();
    m_document.setModified(false);  // New document starts unmodified
    updateTitle();
    onDocumentLoaded();
    m_statusLabel->setText(tr("New document created"));
}

void MainWindow::onFileOpen()
{
    if (!maybeSave()) return;

    QString selectedFilter;
    QString path = QFileDialog::getOpenFileName(this,
        tr("Open File"),
        QString(),
        tr(kOpenFilter),
        &selectedFilter);

    if (path.isEmpty()) return;

    // Determine file type by extension or by checking if it's a directory
    QFileInfo info(path);
    bool isProject = info.isDir() || path.endsWith(QStringLiteral(".hcad"), Qt::CaseInsensitive);

    if (isProject) {
        // Open as HobbyCAD project
        std::string errorMsg;
        if (m_project.load(path.toStdString(), &errorMsg)) {
            // Sync document shapes from project
            m_document.clear();
            for (const auto& shape : m_project.shapes()) {
                m_document.addShape(shape);
            }
            m_document.setModified(false);

            // Update project browser
            if (m_projectBrowser) {
                m_projectBrowser->setProject(&m_project);
            }

            updateTitle();
            onDocumentLoaded();
            m_statusLabel->setText(tr("Opened project: %1").arg(QString::fromStdString(m_project.name())));
        } else {
            QMessageBox::warning(this,
                tr("Open Failed"),
                tr("Could not open project:\n%1\n\n%2").arg(path, QString::fromStdString(errorMsg)));
        }
    } else {
        // Open as standalone BREP file (raw geometry import)
        // If the file doesn't exist and the BREP filter is active,
        // try appending .brep before giving up
        if (!QFileInfo::exists(path)) {
            QString withExt = ensureBrepExtension(path, selectedFilter);
            if (withExt != path && QFileInfo::exists(withExt))
                path = withExt;
        }

        if (m_document.loadBrep(path.toStdString())) {
            // Clear project state since we're loading raw geometry only
            m_project.close();

            updateTitle();
            onDocumentLoaded();
            m_statusLabel->setText(tr("Opened: %1").arg(path));
        } else {
            QMessageBox::warning(this,
                tr("Open Failed"),
                tr("Could not open file:\n%1").arg(path));
        }
    }
}

void MainWindow::onFileSave()
{
    // Check if we have an existing project or document
    if (!m_project.isNew()) {
        // Save to existing project
        std::string errorMsg;
        if (m_project.save({}, &errorMsg)) {
            m_document.setModified(false);
            updateTitle();
            m_statusLabel->setText(tr("Saved project: %1").arg(QString::fromStdString(m_project.name())));
        } else {
            QMessageBox::warning(this,
                tr("Save Failed"),
                tr("Could not save project:\n%1").arg(QString::fromStdString(errorMsg)));
        }
    } else if (!m_document.isNew()) {
        // Save to existing BREP file
        if (m_document.saveBrep()) {
            updateTitle();
            m_statusLabel->setText(tr("Saved: %1").arg(QString::fromStdString(m_document.filePath())));
        } else {
            QMessageBox::warning(this,
                tr("Save Failed"),
                tr("Could not save file:\n%1").arg(QString::fromStdString(m_document.filePath())));
        }
    } else {
        // No existing file - prompt for location
        onFileSaveAs();
    }
}

void MainWindow::onFileSaveAs()
{
    QString selectedFilter = tr(kProjectFilter);  // Default to project format
    QString path = QFileDialog::getSaveFileName(this,
        tr("Save As"),
        QString(),
        tr(kSaveFilter),
        &selectedFilter);

    if (path.isEmpty()) return;

    bool saveAsProject = selectedFilter.contains(QStringLiteral(".hcad"));

    if (saveAsProject) {
        // Project structure per project_definition.txt Section 5.2:
        //   my_widget/              <- directory (NO .hcad extension)
        //     my_widget.hcad        <- manifest file
        //
        // User might enter:
        //   - "my_widget" (directory name)
        //   - "my_widget.hcad" (we strip the extension for directory)
        //   - "/path/to/my_widget.hcad" (manifest path)

        // Strip .hcad extension if user added it (it's for the manifest, not directory)
        if (path.endsWith(QStringLiteral(".hcad"), Qt::CaseInsensitive)) {
            path.chop(5);
        }

        // Sync shapes to project
        m_project.setShapes(m_document.shapes());

        // Set project name from directory name
        QFileInfo info(path);
        m_project.setName(info.fileName().toStdString());

        std::string errorMsg;
        if (m_project.save(path.toStdString(), &errorMsg)) {
            m_document.setModified(false);

            // Update project browser with new path
            if (m_projectBrowser) {
                m_projectBrowser->setProject(&m_project);
            }

            updateTitle();
            m_statusLabel->setText(tr("Saved project: %1").arg(QString::fromStdString(m_project.name())));
        } else {
            QMessageBox::warning(this,
                tr("Save Failed"),
                tr("Could not save project:\n%1").arg(QString::fromStdString(errorMsg)));
        }
    } else {
        // Save as BREP
        path = ensureBrepExtension(path, selectedFilter);

        if (m_document.saveBrep(path.toStdString())) {
            updateTitle();
            m_statusLabel->setText(tr("Saved: %1").arg(path));
        } else {
            QMessageBox::warning(this,
                tr("Save Failed"),
                tr("Could not save file:\n%1").arg(path));
        }
    }
}

void MainWindow::onFileQuit()
{
    close();  // triggers closeEvent()
}

void MainWindow::onFileImportStep()
{
    QString filePath = QFileDialog::getOpenFileName(
        this,
        tr("Import STEP File"),
        QString(),
        tr("STEP Files (*.step *.stp *.STEP *.STP);;All Files (*)"));

    if (filePath.isEmpty()) {
        return;
    }

    // Read the STEP file
    step_io::ReadResult result = step_io::readStep(filePath.toStdString());

    if (!result.success) {
        QMessageBox::critical(this, tr("Import Failed"),
            tr("Failed to import STEP file:\n%1").arg(QString::fromStdString(result.errorMessage)));
        return;
    }

    // Add shapes to the document
    for (const TopoDS_Shape& shape : result.shapes) {
        m_document.addShape(shape);
    }

    m_document.setModified(true);
    onDocumentLoaded();

    statusBar()->showMessage(
        tr("Imported %1 shape(s) from STEP file").arg(result.shapeCount), 5000);
}

void MainWindow::onFileExportStep()
{
    const auto& shapes = m_document.shapes();

    if (shapes.empty()) {
        QMessageBox::information(this, tr("Export STEP"),
            tr("No geometry to export.\n"
               "Create some geometry first using extrude or other operations."));
        return;
    }

    QString filePath = QFileDialog::getSaveFileName(
        this,
        tr("Export STEP File"),
        QString(),
        tr("STEP Files (*.step);;All Files (*)"));

    if (filePath.isEmpty()) {
        return;
    }

    // Ensure .step extension
    if (!filePath.toLower().endsWith(QLatin1String(".step")) &&
        !filePath.toLower().endsWith(QLatin1String(".stp"))) {
        filePath += QStringLiteral(".step");
    }

    // Write the STEP file
    step_io::WriteResult result = step_io::writeStep(filePath.toStdString(), shapes);

    if (!result.success) {
        QMessageBox::critical(this, tr("Export Failed"),
            tr("Failed to export STEP file:\n%1").arg(QString::fromStdString(result.errorMessage)));
        return;
    }

    statusBar()->showMessage(
        tr("Exported %1 shape(s) to STEP file").arg(result.shapeCount), 5000);
}

void MainWindow::onFileExportStl()
{
    const auto& shapes = m_document.shapes();

    if (shapes.empty()) {
        QMessageBox::information(this, tr("Export STL"),
            tr("No geometry to export.\n"
               "Create some geometry first using extrude or other operations."));
        return;
    }

    QString filePath = QFileDialog::getSaveFileName(
        this,
        tr("Export STL File"),
        QString(),
        tr("STL Files (*.stl);;All Files (*)"));

    if (filePath.isEmpty()) {
        return;
    }

    // Ensure .stl extension
    if (!filePath.toLower().endsWith(QLatin1String(".stl"))) {
        filePath += QStringLiteral(".stl");
    }

    // Write the STL file with default quality
    stl_io::WriteResult result = stl_io::writeStl(filePath.toStdString(), shapes);

    if (!result.success) {
        QMessageBox::critical(this, tr("Export Failed"),
            tr("Failed to export STL file:\n%1").arg(QString::fromStdString(result.errorMessage)));
        return;
    }

    statusBar()->showMessage(tr("Exported geometry to STL file"), 5000);
}

void MainWindow::setSketchExportEnabled(bool enabled)
{
    if (m_actionExportDXF) m_actionExportDXF->setEnabled(enabled);
    if (m_actionExportSVG) m_actionExportSVG->setEnabled(enabled);
}

bool MainWindow::getSelectedSketchForExport(
    QVector<sketch::Entity>& /*outEntities*/,
    QVector<sketch::Constraint>& /*outConstraints*/) const
{
    return false;  // Base class has no selected sketch support
}

void MainWindow::onFileExportDXF()
{
    QVector<sketch::Entity> entities;
    QVector<sketch::Constraint> constraints;

    // Try active sketch canvas first (sketch edit mode)
    SketchCanvas* canvas = activeSketchCanvas();
    if (canvas) {
        const auto& guiEntities = canvas->entities();
        if (guiEntities.isEmpty()) {
            QMessageBox::information(this, tr("Export DXF"),
                tr("The sketch is empty. Add some geometry first."));
            return;
        }
        entities.reserve(guiEntities.size());
        for (const auto& e : guiEntities) {
            entities.append(static_cast<const sketch::Entity&>(e));
        }
    } else if (!getSelectedSketchForExport(entities, constraints)) {
        // No active canvas and no selected sketch
        QMessageBox::information(this, tr("Export DXF"),
            tr("No sketch available for export.\n"
               "Enter sketch mode or select a completed sketch in the timeline."));
        return;
    }

    if (entities.isEmpty()) {
        QMessageBox::information(this, tr("Export DXF"),
            tr("The sketch is empty. Add some geometry first."));
        return;
    }

    QString filePath = QFileDialog::getSaveFileName(
        this,
        tr("Export DXF File"),
        QString(),
        tr("DXF Files (*.dxf);;All Files (*)"));

    if (filePath.isEmpty()) return;

    if (!filePath.toLower().endsWith(QLatin1String(".dxf"))) {
        filePath += QStringLiteral(".dxf");
    }

    sketch::DXFExportOptions options;
    std::vector<sketch::Entity> stdEntities(entities.begin(), entities.end());
    bool success = sketch::exportSketchToDXF(stdEntities, filePath.toStdString(), options);

    if (!success) {
        QMessageBox::critical(this, tr("Export Failed"),
            tr("Failed to export DXF file."));
        return;
    }

    statusBar()->showMessage(
        tr("Exported %1 entities to DXF file").arg(entities.size()), 5000);
}

void MainWindow::onFileExportSVG()
{
    QVector<sketch::Entity> entities;
    QVector<sketch::Constraint> constraints;

    // Try active sketch canvas first (sketch edit mode)
    SketchCanvas* canvas = activeSketchCanvas();
    if (canvas) {
        const auto& guiEntities = canvas->entities();
        if (guiEntities.isEmpty()) {
            QMessageBox::information(this, tr("Export SVG"),
                tr("The sketch is empty. Add some geometry first."));
            return;
        }
        entities.reserve(guiEntities.size());
        for (const auto& e : guiEntities) {
            entities.append(static_cast<const sketch::Entity&>(e));
        }
        // Convert constraints from canvas
        for (const auto& c : canvas->constraints()) {
            sketch::Constraint lc;
            lc.id = c.id;
            lc.type = static_cast<sketch::ConstraintType>(c.type);
            lc.entityIds = c.entityIds;
            lc.pointIndices = c.pointIndices;
            lc.value = c.value;
            lc.isDriving = c.isDriving;
            lc.labelPosition = c.labelPosition;
            lc.labelVisible = c.labelVisible;
            lc.enabled = c.enabled;
            constraints.append(lc);
        }
    } else if (!getSelectedSketchForExport(entities, constraints)) {
        // No active canvas and no selected sketch
        QMessageBox::information(this, tr("Export SVG"),
            tr("No sketch available for export.\n"
               "Enter sketch mode or select a completed sketch in the timeline."));
        return;
    }

    if (entities.isEmpty()) {
        QMessageBox::information(this, tr("Export SVG"),
            tr("The sketch is empty. Add some geometry first."));
        return;
    }

    QString filePath = QFileDialog::getSaveFileName(
        this,
        tr("Export SVG File"),
        QString(),
        tr("SVG Files (*.svg);;All Files (*)"));

    if (filePath.isEmpty()) return;

    if (!filePath.toLower().endsWith(QLatin1String(".svg"))) {
        filePath += QStringLiteral(".svg");
    }

    sketch::SVGExportOptions options;
    std::vector<sketch::Entity> stdEntities(entities.begin(), entities.end());
    std::vector<sketch::Constraint> stdConstraints(constraints.begin(), constraints.end());
    bool success = sketch::exportSketchToSVG(stdEntities, stdConstraints, filePath.toStdString(), options);

    if (!success) {
        QMessageBox::critical(this, tr("Export Failed"),
            tr("Failed to export SVG file."));
        return;
    }

    statusBar()->showMessage(
        tr("Exported %1 entities to SVG file").arg(entities.size()), 5000);
}

void MainWindow::onFileClose()
{
    if (!maybeSave()) return;

    m_document.clear();
    m_document.setModified(false);
    m_project.close();
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

        // If maximized, save the stored normal geometry instead of current
        if (isMaximized() && !m_normalGeometry.isEmpty()) {
            settings.setValue(QStringLiteral("window/geometry"), m_normalGeometry);
        } else {
            settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
        }
        settings.setValue(QStringLiteral("window/state"), saveState());
        settings.setValue(QStringLiteral("window/maximized"), isMaximized());
        event->accept();
    } else {
        event->ignore();
    }
}

void MainWindow::changeEvent(QEvent* event)
{
    QMainWindow::changeEvent(event);
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    // Save geometry when not maximized (normal window state)
    if (!isMaximized() && !isMinimized() && !isFullScreen()) {
        m_normalGeometry = saveGeometry();
    }
}

void MainWindow::moveEvent(QMoveEvent* event)
{
    QMainWindow::moveEvent(event);
    // Save geometry when not maximized (normal window state)
    if (!isMaximized() && !isMinimized() && !isFullScreen()) {
        m_normalGeometry = saveGeometry();
    }
}

bool MainWindow::maybeSave()
{
    if (!m_document.isModified() && !m_project.isModified()) {
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
        // Try to save
        if (!m_project.isNew()) {
            // Save to existing project
            std::string errorMsg;
            if (!m_project.save({}, &errorMsg)) {
                QMessageBox::warning(this,
                    tr("Save Failed"),
                    tr("Could not save project:\n%1").arg(QString::fromStdString(errorMsg)));
                return false;
            }
        } else if (!m_document.isNew()) {
            // Save to existing BREP file
            if (!m_document.saveBrep()) {
                QMessageBox::warning(this,
                    tr("Save Failed"),
                    tr("Could not save file:\n%1").arg(QString::fromStdString(m_document.filePath())));
                return false;
            }
        } else {
            // New document — prompt for location
            QString selectedFilter = tr(kProjectFilter);
            QString path = QFileDialog::getSaveFileName(this,
                tr("Save As"),
                QString(),
                tr(kSaveFilter),
                &selectedFilter);

            if (path.isEmpty()) {
                return false;  // user cancelled the save dialog
            }

            bool saveAsProject = selectedFilter.contains(QStringLiteral(".hcad"));

            if (saveAsProject) {
                if (!path.endsWith(QStringLiteral(".hcad"), Qt::CaseInsensitive)) {
                    path += QStringLiteral(".hcad");
                }
                m_project.setShapes(m_document.shapes());
                QFileInfo info(path);
                m_project.setName(info.baseName().replace(QStringLiteral(".hcad"), QString()).toStdString());

                std::string errorMsg;
                if (!m_project.save(path.toStdString(), &errorMsg)) {
                    QMessageBox::warning(this,
                        tr("Save Failed"),
                        tr("Could not save project:\n%1").arg(QString::fromStdString(errorMsg)));
                    return false;
                }
            } else {
                path = ensureBrepExtension(path, selectedFilter);
                if (!m_document.saveBrep(path.toStdString())) {
                    QMessageBox::warning(this,
                        tr("Save Failed"),
                        tr("Could not save file:\n%1").arg(path));
                    return false;
                }
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
    connect(&dlg, &PreferencesDialog::bindingsChanged,
            this, &MainWindow::applyBindings);
    if (dlg.exec() == QDialog::Accepted) {
        applyPreferences();
    }
}

void MainWindow::applyPreferences()
{
    // Subclasses override to apply settings to the viewport, etc.
    applyBindings();
}

// ---- Helpers --------------------------------------------------------

void MainWindow::applyBindings()
{
    // Load bindings from settings and apply to actions
    auto bindings = BindingsDialog::loadBindings();

    // Map action IDs to QAction pointers
    QHash<QString, QAction*> actionMap;
    actionMap.insert(QStringLiteral("file.new"), m_actionNew);
    actionMap.insert(QStringLiteral("file.open"), m_actionOpen);
    actionMap.insert(QStringLiteral("file.save"), m_actionSave);
    actionMap.insert(QStringLiteral("file.saveAs"), m_actionSaveAs);
    actionMap.insert(QStringLiteral("file.close"), m_actionClose);
    actionMap.insert(QStringLiteral("file.quit"), m_actionQuit);
    actionMap.insert(QStringLiteral("edit.cut"), m_actionCut);
    actionMap.insert(QStringLiteral("edit.copy"), m_actionCopy);
    actionMap.insert(QStringLiteral("edit.paste"), m_actionPaste);
    actionMap.insert(QStringLiteral("edit.delete"), m_actionDelete);
    actionMap.insert(QStringLiteral("edit.selectAll"), m_actionSelectAll);
    actionMap.insert(QStringLiteral("view.terminal"), m_actionToggleTerminal);
    actionMap.insert(QStringLiteral("view.project"), m_actionToggleFeatureTree);
    actionMap.insert(QStringLiteral("view.properties"), m_actionToggleProperties);
    actionMap.insert(QStringLiteral("view.resetView"), m_actionResetView);
    actionMap.insert(QStringLiteral("view.rotateLeft"), m_actionRotateLeft);
    actionMap.insert(QStringLiteral("view.rotateRight"), m_actionRotateRight);
    actionMap.insert(QStringLiteral("view.preferences"), m_actionPreferences);
    actionMap.insert(QStringLiteral("view.toolbar"), m_actionToggleToolbar);
    actionMap.insert(QStringLiteral("edit.undo"), m_actionUndo);
    actionMap.insert(QStringLiteral("edit.redo"), m_actionRedo);
    actionMap.insert(QStringLiteral("construct.plane"), m_actionNewConstructionPlane);

    // Apply keyboard bindings to each action
    for (auto it = bindings.constBegin(); it != bindings.constEnd(); ++it) {
        QAction* action = actionMap.value(it.key());
        if (!action) continue;

        const ActionBinding& ab = it.value();

        // Collect all keyboard bindings (skip mouse bindings)
        QList<QKeySequence> shortcuts;

        auto addIfKeyboard = [&shortcuts](const QString& binding) {
            if (binding.isEmpty()) return;
            // Skip mouse bindings
            if (binding.contains(QStringLiteral("Button"), Qt::CaseInsensitive) ||
                binding.contains(QStringLiteral("Wheel"), Qt::CaseInsensitive) ||
                binding.contains(QStringLiteral("Drag"), Qt::CaseInsensitive) ||
                binding.contains(QStringLiteral("Click"), Qt::CaseInsensitive)) {
                return;
            }
            // A comma in a binding string means multiple separate shortcuts
            // (e.g. "Ctrl+Shift+Z,Ctrl+Y"), not a multi-key chord.
            // Split and add each as an independent shortcut.
            const QStringList parts = binding.split(QLatin1Char(','),
                                                     Qt::SkipEmptyParts);
            for (const QString& part : parts) {
                QKeySequence seq(part.trimmed());
                if (!seq.isEmpty()) {
                    shortcuts.append(seq);
                }
            }
        };

        addIfKeyboard(ab.binding1);
        addIfKeyboard(ab.binding2);
        addIfKeyboard(ab.binding3);

        action->setShortcuts(shortcuts);
    }
}

void MainWindow::updateTitle()
{
    QString title = QStringLiteral("HobbyCAD");

    if (!m_project.isNew()) {
        // Show project name
        title += QStringLiteral(" — ") + QString::fromStdString(m_project.name());
    } else if (!m_document.isNew()) {
        // Show legacy BREP file path
        title += QStringLiteral(" — ") + QString::fromStdString(m_document.filePath());
    } else {
        title += QStringLiteral(" — [New Document]");
    }

    if (m_project.isModified() || m_document.isModified()) {
        title += QStringLiteral(" *");
    }

    setWindowTitle(title);
}

void MainWindow::addSketchToTree(const QString& name, int index)
{
    if (!m_sketchesTreeItem)
        return;

    auto* item = new QTreeWidgetItem(m_sketchesTreeItem);
    item->setText(0, name);
    item->setFlags(item->flags() | Qt::ItemIsEditable);
    item->setData(0, Qt::UserRole, QStringLiteral("sketch"));
    item->setData(0, Qt::UserRole + 1, index);  // Store sketch index

    m_sketchesTreeItem->setExpanded(true);
}

void MainWindow::selectSketchInTree(int index)
{
    if (!m_sketchesTreeItem)
        return;

    for (int i = 0; i < m_sketchesTreeItem->childCount(); ++i) {
        QTreeWidgetItem* item = m_sketchesTreeItem->child(i);
        if (item->data(0, Qt::UserRole + 1).toInt() == index) {
            item->treeWidget()->setCurrentItem(item);
            return;
        }
    }
}

void MainWindow::clearSketchesInTree()
{
    if (!m_sketchesTreeItem)
        return;

    while (m_sketchesTreeItem->childCount() > 0) {
        delete m_sketchesTreeItem->takeChild(0);
    }
}

void MainWindow::addBodyToTree(const QString& name, int index)
{
    if (!m_bodiesTreeItem)
        return;

    auto* item = new QTreeWidgetItem(m_bodiesTreeItem);
    item->setText(0, name);
    item->setFlags(item->flags() | Qt::ItemIsEditable);
    item->setData(0, Qt::UserRole, QStringLiteral("body"));
    item->setData(0, Qt::UserRole + 1, index);

    m_bodiesTreeItem->setExpanded(true);
}

void MainWindow::clearBodiesInTree()
{
    if (!m_bodiesTreeItem)
        return;

    while (m_bodiesTreeItem->childCount() > 0) {
        delete m_bodiesTreeItem->takeChild(0);
    }
}

void MainWindow::setUnitsFromString(const QString& units)
{
    m_currentUnits = lengthUnitToIndex(parseUnitSuffix(units.toStdString()));
}

void MainWindow::addConstructionPlaneToTree(const QString& name, int id)
{
    if (!m_constructionTreeItem)
        return;

    auto* item = new QTreeWidgetItem(m_constructionTreeItem);
    item->setText(0, name);
    item->setFlags(item->flags() | Qt::ItemIsEditable);
    item->setData(0, Qt::UserRole, QStringLiteral("construction_plane"));
    item->setData(0, Qt::UserRole + 1, id);  // Store plane ID

    m_constructionTreeItem->setExpanded(true);
}

void MainWindow::selectConstructionPlaneInTree(int id)
{
    if (!m_constructionTreeItem)
        return;

    for (int i = 0; i < m_constructionTreeItem->childCount(); ++i) {
        QTreeWidgetItem* item = m_constructionTreeItem->child(i);
        if (item->data(0, Qt::UserRole + 1).toInt() == id) {
            item->treeWidget()->setCurrentItem(item);
            return;
        }
    }
}

void MainWindow::clearConstructionPlanesInTree()
{
    if (!m_constructionTreeItem)
        return;

    while (m_constructionTreeItem->childCount() > 0) {
        delete m_constructionTreeItem->takeChild(0);
    }
}


// ---- Shared sketch mode methods ------------------------------------

// Parse a scalar value from text like "10.00 mm" or "90.0°"
double MainWindow::parseScalar(const QString& text)
{
    QString cleaned;
    for (const QChar& ch : text) {
        if (ch.isDigit() || ch == QLatin1Char('.') || ch == QLatin1Char('-'))
            cleaned += ch;
    }
    bool ok = false;
    double val = cleaned.toDouble(&ok);
    return ok ? val : std::numeric_limits<double>::quiet_NaN();
}

// Parse a point value from text like "(10.00, 20.00) mm"
bool MainWindow::parsePoint(const QString& text, double& x, double& y)
{
    int lp = text.indexOf(QLatin1Char('('));
    int rp = text.indexOf(QLatin1Char(')'));
    if (lp < 0 || rp < 0 || rp <= lp) return false;
    QString inner = text.mid(lp + 1, rp - lp - 1);
    QStringList parts = inner.split(QLatin1Char(','));
    if (parts.size() != 2) return false;
    bool okX = false, okY = false;
    x = parts[0].trimmed().toDouble(&okX);
    y = parts[1].trimmed().toDouble(&okY);
    return okX && okY;
}

QMap<QString, double> MainWindow::parameterValues() const
{
    QMap<QString, double> values;
    for (const auto& p : m_parameters) {
        values[QString::fromStdString(p.name)] = p.value;
    }
    return values;
}

void MainWindow::initDefaultParameters()
{
    // Initialize with some example user parameters
    m_parameters.clear();

    Parameter width;
    width.name = "width";
    width.expression = "50";
    width.value = 50.0;
    width.unit = unitSuffix().toStdString();
    width.comment = tr("Base width dimension").toStdString();
    width.isUserParam = true;
    m_parameters.append(width);

    Parameter height;
    height.name = "height";
    height.expression = "30";
    height.value = 30.0;
    height.unit = unitSuffix().toStdString();
    height.comment = tr("Base height dimension").toStdString();
    height.isUserParam = true;
    m_parameters.append(height);

    Parameter depth;
    depth.name = "depth";
    depth.expression = "20";
    depth.value = 20.0;
    depth.unit = unitSuffix().toStdString();
    depth.comment = tr("Base depth dimension").toStdString();
    depth.isUserParam = true;
    m_parameters.append(depth);

    Parameter radius;
    radius.name = "radius";
    radius.expression = "5";
    radius.value = 5.0;
    radius.unit = unitSuffix().toStdString();
    radius.comment = tr("Default fillet radius").toStdString();
    radius.isUserParam = true;
    m_parameters.append(radius);

    Parameter angle;
    angle.name = "angle";
    angle.expression = "45";
    angle.value = 45.0;
    angle.unit = "deg";
    angle.comment = tr("Default angle").toStdString();
    angle.isUserParam = true;
    m_parameters.append(angle);

    // Example object parameters (from features - read-only)
    Parameter extrude1Dist;
    extrude1Dist.name = "Extrude1_Distance";
    extrude1Dist.expression = "10";
    extrude1Dist.value = 10.0;
    extrude1Dist.unit = unitSuffix().toStdString();
    extrude1Dist.comment = tr("Extrude1 distance").toStdString();
    extrude1Dist.isUserParam = false;
    m_parameters.append(extrude1Dist);

    Parameter fillet1Rad;
    fillet1Rad.name = "Fillet1_Radius";
    fillet1Rad.expression = "radius";
    fillet1Rad.value = 5.0;
    fillet1Rad.unit = unitSuffix().toStdString();
    fillet1Rad.comment = tr("Fillet1 radius (uses 'radius' param)").toStdString();
    fillet1Rad.isUserParam = false;
    m_parameters.append(fillet1Rad);
}

void MainWindow::showParametersDialog()
{
    ParametersDialog dlg(this);
    dlg.setDefaultUnit(unitSuffix());
    dlg.setParameters(m_parameters);

    connect(&dlg, &ParametersDialog::parametersChanged,
            this, &MainWindow::onParametersChanged);

    dlg.exec();
}

void MainWindow::onParametersChanged(const QList<Parameter>& params)
{
    m_parameters = params;

    // TODO: Re-evaluate all features that use these parameters
    // and regenerate the model

    // For now, just update the status bar
    statusBar()->showMessage(tr("Parameters updated"), 3000);
}

void MainWindow::onSketchToolSelected(SketchTool tool)
{
    m_sketchCanvas->setActiveTool(tool);

    // Update status bar with tool hint
    QString hint;
    switch (tool) {
    case SketchTool::Select:
        hint = tr("Click to select entities, drag to move");
        break;
    case SketchTool::Line:
        hint = tr("Click to start line, click again to end");
        break;
    case SketchTool::Rectangle:
        hint = tr("Click and drag to draw rectangle");
        break;
    case SketchTool::Circle:
        hint = tr("Click center, drag to set radius");
        break;
    case SketchTool::Arc:
        hint = tr("Click center, drag to set radius and arc");
        break;
    case SketchTool::Point:
        hint = tr("Click to place construction point");
        break;
    case SketchTool::Dimension:
        hint = tr("Click two points or an entity to add dimension");
        break;
    case SketchTool::Constraint:
        hint = tr("Select entities to add constraints");
        break;
    default:
        hint = tr("Select a tool to start drawing");
    }
    statusBar()->showMessage(hint);
}

void MainWindow::onSketchEntityCreated(int entityId)
{
    // Update entity count in properties
    if (QTreeWidget* propsTree = propertiesTree()) {
        for (int i = 0; i < propsTree->topLevelItemCount(); ++i) {
            auto* item = propsTree->topLevelItem(i);
            if (item && item->text(0) == tr("Entities")) {
                item->setText(1, QString::number(m_sketchCanvas->entities().size()));
                break;
            }
        }
    }

    // Select the new entity
    showSketchEntityProperties(entityId);
}

void MainWindow::onSketchSelectionChanged(int entityId)
{
    if (entityId < 0) {
        // Entity deselected — check if a constraint is selected instead
        int cid = m_sketchCanvas->selectedConstraintId();
        if (cid >= 0) {
            showSketchConstraintProperties(cid);
            return;
        }
        // No constraint either — let subclass handle deselection
        onSketchDeselected();
        return;
    }

    // Show entity properties (sketch remains selected)
    m_sketchCanvas->setSketchSelected(true);
    showSketchEntityProperties(entityId);
}

void MainWindow::onSketchDeselected()
{
    // Default: re-enter sketch mode to show sketch-level properties
    if (m_inSketchMode && m_sketchCanvas) {
        m_sketchCanvas->setSketchSelected(true);
        if (sketchActionBar())
            sketchActionBar()->reset();
    }
}

void MainWindow::onCreateSketchClicked()
{
    // Base class does nothing — subclasses override
}

void MainWindow::saveCurrentSketch()
{
    // Base class does nothing — subclasses override
}

void MainWindow::discardCurrentSketch()
{
    // Base class does nothing — subclasses override
}

void MainWindow::enterSketchMode(SketchPlane plane)
{
    if (m_inSketchMode) return;

    m_inSketchMode = true;

    // Enable sketch export actions
    setSketchExportEnabled(true);

    // Notify CLI panel that viewport commands won't work in sketch mode
    if (cliPanel()) {
        cliPanel()->setSketchModeActive(true);
    }

    // Switch to sketch toolbar and set to Select mode
    m_toolbarStack->setCurrentWidget(m_sketchToolbar);
    m_sketchToolbar->setActiveTool(SketchTool::Select);

    // Show the action bar in properties panel and reset its state
    if (sketchActionBar()) {
        sketchActionBar()->reset();
    }
    setSketchActionBarVisible(true);

    // Configure sketch canvas
    m_sketchCanvas->setSketchPlane(plane);
    m_sketchCanvas->clear();
    m_sketchCanvas->resetView();
    m_sketchCanvas->setActiveTool(SketchTool::Select);
    m_sketchCanvas->setSketchSelected(true);
    m_viewportStack->setCurrentWidget(m_sketchCanvas);

    // Update status bar and focus canvas
    statusBar()->showMessage(tr("Sketch mode - Draw entities or press Escape to finish"));
    m_sketchCanvas->setFocus();
}

void MainWindow::exitSketchMode()
{
    if (!m_inSketchMode) return;

    m_inSketchMode = false;

    // Disable sketch export actions
    setSketchExportEnabled(false);

    // Notify CLI panel that viewport commands are available again
    if (cliPanel()) {
        cliPanel()->setSketchModeActive(false);
    }

    // Switch back to normal toolbar
    m_toolbarStack->setCurrentWidget(m_toolbar);

    // Hide the Save/Cancel action bar
    setSketchActionBarVisible(false);

    // Clear properties
    if (QTreeWidget* propsTree = propertiesTree()) {
        propsTree->clear();
    }

    // Deselect timeline item
    m_timeline->setSelectedIndex(-1);

    // Update status bar
    statusBar()->showMessage(tr("Sketch finished"), 3000);
}

void MainWindow::initSketchConnections()
{
    // Sketch toolbar → tool selection
    connect(m_sketchToolbar, &SketchToolbar::toolChanged,
            this, &MainWindow::onSketchToolSelected);

    // Sketch canvas signals
    connect(m_sketchCanvas, &SketchCanvas::selectionChanged,
            this, &MainWindow::onSketchSelectionChanged);
    connect(m_sketchCanvas, &SketchCanvas::entityCreated,
            this, &MainWindow::onSketchEntityCreated);
    connect(m_sketchCanvas, &SketchCanvas::mousePositionChanged,
            this, [this](const QPointF& pos) {
        statusBar()->showMessage(
            tr("X: %1  Y: %2").arg(pos.x(), 0, 'f', 2).arg(pos.y(), 0, 'f', 2));
    });

    // Undo/redo wiring
    if (undoAction()) {
        connect(undoAction(), &QAction::triggered,
                m_sketchCanvas, &SketchCanvas::undo);
        connect(m_sketchCanvas, &SketchCanvas::undoAvailabilityChanged,
                undoAction(), &QAction::setEnabled);
    }
    if (redoAction()) {
        connect(redoAction(), &QAction::triggered,
                m_sketchCanvas, &SketchCanvas::redo);
        connect(m_sketchCanvas, &SketchCanvas::redoAvailabilityChanged,
                redoAction(), &QAction::setEnabled);
    }

    // Changelog panel
    if (changelogPanel()) {
        changelogPanel()->setSketchCanvas(m_sketchCanvas);
    }

    // Sketch action bar (Save/Discard buttons)
    if (sketchActionBar()) {
        connect(sketchActionBar(), &SketchActionBar::saveClicked,
                this, [this]() {
            saveCurrentSketch();
            exitSketchMode();
        });
        connect(sketchActionBar(), &SketchActionBar::discardClicked,
                this, [this]() {
            discardCurrentSketch();
            exitSketchMode();
        });
    }

    // Properties tree item editing
    if (QTreeWidget* propsTree = propertiesTree()) {
        connect(propsTree, &QTreeWidget::itemChanged,
                this, &MainWindow::onSketchPropertyItemChanged);
    }

    // Toolbar → model toolbar signals
    connect(m_toolbar, &ModelToolbar::createSketchClicked,
            this, [this]() { onCreateSketchClicked(); });
    connect(m_toolbar, &ModelToolbar::parametersClicked,
            this, &MainWindow::showParametersDialog);
}

void MainWindow::showSketchEntityProperties(int entityId)
{
    const SketchEntity* entity = nullptr;
    for (const auto& e : m_sketchCanvas->entities()) {
        if (e.id == entityId) {
            entity = &e;
            break;
        }
    }

    if (!entity) return;

    QTreeWidget* propsTree = propertiesTree();
    if (!propsTree) return;

    propsTree->clear();
    QString units = unitSuffix();

    // Entity type
    auto* typeItem = new QTreeWidgetItem(propsTree);
    typeItem->setText(0, tr("Type"));
    QString typeName;
    switch (entity->type) {
    case SketchEntityType::Point:     typeName = tr("Point"); break;
    case SketchEntityType::Line:      typeName = tr("Line"); break;
    case SketchEntityType::Rectangle: typeName = tr("Rectangle"); break;
    case SketchEntityType::Circle:    typeName = tr("Circle"); break;
    case SketchEntityType::Arc:       typeName = tr("Arc"); break;
    case SketchEntityType::Spline:    typeName = tr("Spline"); break;
    case SketchEntityType::Polygon:   typeName = tr("Polygon"); break;
    case SketchEntityType::Slot:      typeName = tr("Slot"); break;
    case SketchEntityType::Ellipse:   typeName = tr("Ellipse"); break;
    case SketchEntityType::Text:      typeName = tr("Text"); break;
    case SketchEntityType::Dimension: typeName = tr("Dimension"); break;
    }
    typeItem->setText(1, typeName);

    // Entity ID
    auto* idItem = new QTreeWidgetItem(propsTree);
    idItem->setText(0, tr("ID"));
    idItem->setText(1, QString::number(entity->id));

    // Geometry header
    auto* geomHeader = new QTreeWidgetItem(propsTree);
    geomHeader->setText(0, tr("Geometry"));

    // Entity-specific properties
    switch (entity->type) {
    case SketchEntityType::Point:
        if (!entity->points.empty()) {
            auto* posItem = new QTreeWidgetItem(geomHeader);
            posItem->setText(0, tr("Position"));
            posItem->setText(1, QStringLiteral("(%1, %2) %3")
                             .arg(entity->points[0].x, 0, 'f', 2)
                             .arg(entity->points[0].y, 0, 'f', 2)
                             .arg(units));
            posItem->setFlags(posItem->flags() | Qt::ItemIsEditable);
            posItem->setData(0, Qt::UserRole, entityId);
            posItem->setData(0, Qt::UserRole + 1, QStringLiteral("point0"));
        }
        break;

    case SketchEntityType::Line:
        if (entity->points.size() >= 2) {
            auto* p1Item = new QTreeWidgetItem(geomHeader);
            p1Item->setText(0, tr("Start"));
            p1Item->setText(1, QStringLiteral("(%1, %2) %3")
                             .arg(entity->points[0].x, 0, 'f', 2)
                             .arg(entity->points[0].y, 0, 'f', 2)
                             .arg(units));
            p1Item->setFlags(p1Item->flags() | Qt::ItemIsEditable);
            p1Item->setData(0, Qt::UserRole, entityId);
            p1Item->setData(0, Qt::UserRole + 1, QStringLiteral("point0"));

            auto* p2Item = new QTreeWidgetItem(geomHeader);
            p2Item->setText(0, tr("End"));
            p2Item->setText(1, QStringLiteral("(%1, %2) %3")
                             .arg(entity->points[1].x, 0, 'f', 2)
                             .arg(entity->points[1].y, 0, 'f', 2)
                             .arg(units));
            p2Item->setFlags(p2Item->flags() | Qt::ItemIsEditable);
            p2Item->setData(0, Qt::UserRole, entityId);
            p2Item->setData(0, Qt::UserRole + 1, QStringLiteral("point1"));

            auto* lenItem = new QTreeWidgetItem(geomHeader);
            lenItem->setText(0, tr("Length"));
            double len = QLineF(entity->points[0], entity->points[1]).length();
            lenItem->setText(1, QStringLiteral("%1 %2").arg(len, 0, 'f', 2).arg(units));
            lenItem->setFlags(lenItem->flags() | Qt::ItemIsEditable);
            lenItem->setData(0, Qt::UserRole, entityId);
            lenItem->setData(0, Qt::UserRole + 1, QStringLiteral("length"));
        }
        break;

    case SketchEntityType::Rectangle:
        if (entity->points.size() >= 2) {
            auto* p1Item = new QTreeWidgetItem(geomHeader);
            p1Item->setText(0, tr("Corner 1"));
            p1Item->setText(1, QStringLiteral("(%1, %2) %3")
                             .arg(entity->points[0].x, 0, 'f', 2)
                             .arg(entity->points[0].y, 0, 'f', 2)
                             .arg(units));

            auto* p2Item = new QTreeWidgetItem(geomHeader);
            p2Item->setText(0, tr("Corner 2"));
            p2Item->setText(1, QStringLiteral("(%1, %2) %3")
                             .arg(entity->points[1].x, 0, 'f', 2)
                             .arg(entity->points[1].y, 0, 'f', 2)
                             .arg(units));

            auto* widthItem = new QTreeWidgetItem(geomHeader);
            widthItem->setText(0, tr("Width"));
            double w = qAbs(entity->points[1].x - entity->points[0].x);
            widthItem->setText(1, QStringLiteral("%1 %2").arg(w, 0, 'f', 2).arg(units));
            widthItem->setFlags(widthItem->flags() | Qt::ItemIsEditable);
            widthItem->setData(0, Qt::UserRole, entityId);
            widthItem->setData(0, Qt::UserRole + 1, QStringLiteral("width"));

            auto* heightItem = new QTreeWidgetItem(geomHeader);
            heightItem->setText(0, tr("Height"));
            double h = qAbs(entity->points[1].y - entity->points[0].y);
            heightItem->setText(1, QStringLiteral("%1 %2").arg(h, 0, 'f', 2).arg(units));
            heightItem->setFlags(heightItem->flags() | Qt::ItemIsEditable);
            heightItem->setData(0, Qt::UserRole, entityId);
            heightItem->setData(0, Qt::UserRole + 1, QStringLiteral("height"));
        }
        break;

    case SketchEntityType::Circle:
        if (!entity->points.empty()) {
            auto* centerItem = new QTreeWidgetItem(geomHeader);
            centerItem->setText(0, tr("Center"));
            centerItem->setText(1, QStringLiteral("(%1, %2) %3")
                                .arg(entity->points[0].x, 0, 'f', 2)
                                .arg(entity->points[0].y, 0, 'f', 2)
                                .arg(units));
            centerItem->setFlags(centerItem->flags() | Qt::ItemIsEditable);
            centerItem->setData(0, Qt::UserRole, entityId);
            centerItem->setData(0, Qt::UserRole + 1, QStringLiteral("point0"));

            auto* radiusItem = new QTreeWidgetItem(geomHeader);
            radiusItem->setText(0, tr("Radius"));
            radiusItem->setText(1, QStringLiteral("%1 %2").arg(entity->radius, 0, 'f', 2).arg(units));
            radiusItem->setFlags(radiusItem->flags() | Qt::ItemIsEditable);
            radiusItem->setData(0, Qt::UserRole, entityId);
            radiusItem->setData(0, Qt::UserRole + 1, QStringLiteral("radius"));

            auto* diamItem = new QTreeWidgetItem(geomHeader);
            diamItem->setText(0, tr("Diameter"));
            diamItem->setText(1, QStringLiteral("%1 %2").arg(entity->radius * 2, 0, 'f', 2).arg(units));
            diamItem->setFlags(diamItem->flags() | Qt::ItemIsEditable);
            diamItem->setData(0, Qt::UserRole, entityId);
            diamItem->setData(0, Qt::UserRole + 1, QStringLiteral("diameter"));
        }
        break;

    case SketchEntityType::Arc:
        if (!entity->points.empty()) {
            auto* centerItem = new QTreeWidgetItem(geomHeader);
            centerItem->setText(0, tr("Center"));
            centerItem->setText(1, QStringLiteral("(%1, %2) %3")
                                .arg(entity->points[0].x, 0, 'f', 2)
                                .arg(entity->points[0].y, 0, 'f', 2)
                                .arg(units));

            auto* radiusItem = new QTreeWidgetItem(geomHeader);
            radiusItem->setText(0, tr("Radius"));
            radiusItem->setText(1, QStringLiteral("%1 %2").arg(entity->radius, 0, 'f', 2).arg(units));
            radiusItem->setFlags(radiusItem->flags() | Qt::ItemIsEditable);
            radiusItem->setData(0, Qt::UserRole, entityId);
            radiusItem->setData(0, Qt::UserRole + 1, QStringLiteral("radius"));

            auto* startItem = new QTreeWidgetItem(geomHeader);
            startItem->setText(0, tr("Start Angle"));
            startItem->setText(1, QStringLiteral("%1°").arg(entity->startAngle, 0, 'f', 1));
            startItem->setFlags(startItem->flags() | Qt::ItemIsEditable);
            startItem->setData(0, Qt::UserRole, entityId);
            startItem->setData(0, Qt::UserRole + 1, QStringLiteral("startAngle"));

            auto* sweepItem = new QTreeWidgetItem(geomHeader);
            sweepItem->setText(0, tr("Sweep Angle"));
            sweepItem->setText(1, QStringLiteral("%1°").arg(entity->sweepAngle, 0, 'f', 1));
            sweepItem->setFlags(sweepItem->flags() | Qt::ItemIsEditable);
            sweepItem->setData(0, Qt::UserRole, entityId);
            sweepItem->setData(0, Qt::UserRole + 1, QStringLiteral("sweepAngle"));
        }
        break;

    case SketchEntityType::Polygon:
        if (!entity->points.empty()) {
            auto* centerItem = new QTreeWidgetItem(geomHeader);
            centerItem->setText(0, tr("Center"));
            centerItem->setText(1, QStringLiteral("(%1, %2) %3")
                                .arg(entity->points[0].x, 0, 'f', 2)
                                .arg(entity->points[0].y, 0, 'f', 2)
                                .arg(units));
            centerItem->setFlags(centerItem->flags() | Qt::ItemIsEditable);
            centerItem->setData(0, Qt::UserRole, entityId);
            centerItem->setData(0, Qt::UserRole + 1, QStringLiteral("point0"));

            auto* sidesItem = new QTreeWidgetItem(geomHeader);
            sidesItem->setText(0, tr("Sides"));
            sidesItem->setText(1, QString::number(entity->sides));
            sidesItem->setFlags(sidesItem->flags() | Qt::ItemIsEditable);
            sidesItem->setData(0, Qt::UserRole, entityId);
            sidesItem->setData(0, Qt::UserRole + 1, QStringLiteral("sides"));

            auto* radiusItem = new QTreeWidgetItem(geomHeader);
            radiusItem->setText(0, tr("Radius"));
            radiusItem->setText(1, QStringLiteral("%1 %2").arg(entity->radius, 0, 'f', 2).arg(units));
            radiusItem->setFlags(radiusItem->flags() | Qt::ItemIsEditable);
            radiusItem->setData(0, Qt::UserRole, entityId);
            radiusItem->setData(0, Qt::UserRole + 1, QStringLiteral("radius"));
        }
        break;

    case SketchEntityType::Slot:
        if (entity->points.size() >= 2) {
            auto* p1Item = new QTreeWidgetItem(geomHeader);
            p1Item->setText(0, tr("Center 1"));
            p1Item->setText(1, QStringLiteral("(%1, %2) %3")
                             .arg(entity->points[0].x, 0, 'f', 2)
                             .arg(entity->points[0].y, 0, 'f', 2)
                             .arg(units));
            p1Item->setFlags(p1Item->flags() | Qt::ItemIsEditable);
            p1Item->setData(0, Qt::UserRole, entityId);
            p1Item->setData(0, Qt::UserRole + 1, QStringLiteral("point0"));

            auto* p2Item = new QTreeWidgetItem(geomHeader);
            p2Item->setText(0, tr("Center 2"));
            p2Item->setText(1, QStringLiteral("(%1, %2) %3")
                             .arg(entity->points[1].x, 0, 'f', 2)
                             .arg(entity->points[1].y, 0, 'f', 2)
                             .arg(units));
            p2Item->setFlags(p2Item->flags() | Qt::ItemIsEditable);
            p2Item->setData(0, Qt::UserRole, entityId);
            p2Item->setData(0, Qt::UserRole + 1, QStringLiteral("point1"));

            auto* lenItem = new QTreeWidgetItem(geomHeader);
            lenItem->setText(0, tr("Length"));
            double len = QLineF(entity->points[0], entity->points[1]).length();
            lenItem->setText(1, QStringLiteral("%1 %2").arg(len, 0, 'f', 2).arg(units));

            auto* widthItem = new QTreeWidgetItem(geomHeader);
            widthItem->setText(0, tr("Width"));
            widthItem->setText(1, QStringLiteral("%1 %2").arg(entity->radius * 2, 0, 'f', 2).arg(units));
            widthItem->setFlags(widthItem->flags() | Qt::ItemIsEditable);
            widthItem->setData(0, Qt::UserRole, entityId);
            widthItem->setData(0, Qt::UserRole + 1, QStringLiteral("radius"));
        }
        break;

    case SketchEntityType::Ellipse:
        if (!entity->points.empty()) {
            auto* centerItem = new QTreeWidgetItem(geomHeader);
            centerItem->setText(0, tr("Center"));
            centerItem->setText(1, QStringLiteral("(%1, %2) %3")
                                .arg(entity->points[0].x, 0, 'f', 2)
                                .arg(entity->points[0].y, 0, 'f', 2)
                                .arg(units));
            centerItem->setFlags(centerItem->flags() | Qt::ItemIsEditable);
            centerItem->setData(0, Qt::UserRole, entityId);
            centerItem->setData(0, Qt::UserRole + 1, QStringLiteral("point0"));

            auto* majorItem = new QTreeWidgetItem(geomHeader);
            majorItem->setText(0, tr("Major Radius"));
            majorItem->setText(1, QStringLiteral("%1 %2").arg(entity->majorRadius, 0, 'f', 2).arg(units));
            majorItem->setFlags(majorItem->flags() | Qt::ItemIsEditable);
            majorItem->setData(0, Qt::UserRole, entityId);
            majorItem->setData(0, Qt::UserRole + 1, QStringLiteral("majorRadius"));

            auto* minorItem = new QTreeWidgetItem(geomHeader);
            minorItem->setText(0, tr("Minor Radius"));
            minorItem->setText(1, QStringLiteral("%1 %2").arg(entity->minorRadius, 0, 'f', 2).arg(units));
            minorItem->setFlags(minorItem->flags() | Qt::ItemIsEditable);
            minorItem->setData(0, Qt::UserRole, entityId);
            minorItem->setData(0, Qt::UserRole + 1, QStringLiteral("minorRadius"));
        }
        break;

    case SketchEntityType::Spline:
        if (!entity->points.empty()) {
            auto* pointsItem = new QTreeWidgetItem(geomHeader);
            pointsItem->setText(0, tr("Control Points"));
            pointsItem->setText(1, QString::number(entity->points.size()));

            // Show each control point
            for (int i = 0; i < entity->points.size(); ++i) {
                auto* ptItem = new QTreeWidgetItem(geomHeader);
                ptItem->setText(0, tr("Point %1").arg(i + 1));
                ptItem->setText(1, QStringLiteral("(%1, %2) %3")
                                 .arg(entity->points[i].x, 0, 'f', 2)
                                 .arg(entity->points[i].y, 0, 'f', 2)
                                 .arg(units));
                ptItem->setFlags(ptItem->flags() | Qt::ItemIsEditable);
                ptItem->setData(0, Qt::UserRole, entityId);
                ptItem->setData(0, Qt::UserRole + 1, QStringLiteral("point%1").arg(i));
            }
        }
        break;

    case SketchEntityType::Text:
        if (!entity->points.empty()) {
            auto* posItem = new QTreeWidgetItem(geomHeader);
            posItem->setText(0, tr("Position"));
            posItem->setText(1, QStringLiteral("(%1, %2) %3")
                             .arg(entity->points[0].x, 0, 'f', 2)
                             .arg(entity->points[0].y, 0, 'f', 2)
                             .arg(units));
            posItem->setFlags(posItem->flags() | Qt::ItemIsEditable);
            posItem->setData(0, Qt::UserRole, entityId);
            posItem->setData(0, Qt::UserRole + 1, QStringLiteral("point0"));

            auto* textItem = new QTreeWidgetItem(geomHeader);
            textItem->setText(0, tr("Text"));
            textItem->setText(1, QString::fromStdString(entity->text));
            textItem->setFlags(textItem->flags() | Qt::ItemIsEditable);
            textItem->setData(0, Qt::UserRole, entityId);
            textItem->setData(0, Qt::UserRole + 1, QStringLiteral("text"));

            auto* sizeItem = new QTreeWidgetItem(geomHeader);
            sizeItem->setText(0, tr("Font Size"));
            sizeItem->setText(1, QStringLiteral("%1 %2").arg(entity->fontSize, 0, 'f', 1).arg(units));
            sizeItem->setFlags(sizeItem->flags() | Qt::ItemIsEditable);
            sizeItem->setData(0, Qt::UserRole, entityId);
            sizeItem->setData(0, Qt::UserRole + 1, QStringLiteral("fontSize"));

            auto* rotItem = new QTreeWidgetItem(geomHeader);
            rotItem->setText(0, tr("Rotation"));
            rotItem->setText(1, QStringLiteral("%1%2").arg(entity->textRotation, 0, 'f', 1).arg(QChar(0x00B0)));
            rotItem->setFlags(rotItem->flags() | Qt::ItemIsEditable);
            rotItem->setData(0, Qt::UserRole, entityId);
            rotItem->setData(0, Qt::UserRole + 1, QStringLiteral("textRotation"));
        }
        break;

    default:
        break;
    }

    // Constraints
    auto* constraintItem = new QTreeWidgetItem(propsTree);
    constraintItem->setText(0, tr("Constrained"));
    constraintItem->setText(1, entity->constrained ? tr("Yes") : tr("No"));

    propsTree->expandAll();
}

void MainWindow::showSketchConstraintProperties(int constraintId)
{
    const SketchConstraint* constraint = m_sketchCanvas->constraintById(constraintId);
    if (!constraint) return;

    QTreeWidget* propsTree = propertiesTree();
    if (!propsTree) return;

    propsTree->blockSignals(true);
    propsTree->clear();

    // Constraint type name
    auto* typeItem = new QTreeWidgetItem(propsTree);
    typeItem->setText(0, tr("Type"));
    QString typeName;
    bool isSweep = false;
    switch (constraint->type) {
    case ConstraintType::Distance:     typeName = tr("Distance"); break;
    case ConstraintType::Radius:       typeName = tr("Radius"); break;
    case ConstraintType::Diameter:     typeName = tr("Diameter"); break;
    case ConstraintType::Angle:
        for (const auto& g : m_sketchCanvas->groups()) {
            if (m_sketchCanvas->isSweepAngleGroup(g.id)
                    && g.containsConstraint(constraintId)) {
                isSweep = true;
                break;
            }
        }
        typeName = isSweep ? tr("Sweep Angle") : tr("Angle");
        break;
    case ConstraintType::FixedAngle:   typeName = tr("Fixed Angle"); break;
    case ConstraintType::Horizontal:   typeName = tr("Horizontal"); break;
    case ConstraintType::Vertical:     typeName = tr("Vertical"); break;
    case ConstraintType::Parallel:     typeName = tr("Parallel"); break;
    case ConstraintType::Perpendicular:typeName = tr("Perpendicular"); break;
    case ConstraintType::Coincident:   typeName = tr("Coincident"); break;
    case ConstraintType::Tangent:      typeName = tr("Tangent"); break;
    case ConstraintType::Equal:        typeName = tr("Equal"); break;
    case ConstraintType::Midpoint:     typeName = tr("Midpoint"); break;
    case ConstraintType::Symmetric:    typeName = tr("Symmetric"); break;
    case ConstraintType::Concentric:   typeName = tr("Concentric"); break;
    case ConstraintType::Collinear:    typeName = tr("Collinear"); break;
    case ConstraintType::PointOnLine:  typeName = tr("Point On Line"); break;
    case ConstraintType::PointOnCircle:typeName = tr("Point On Circle"); break;
    case ConstraintType::FixedPoint:   typeName = tr("Fixed Point"); break;
    }
    typeItem->setText(1, typeName);

    // Constraint ID
    auto* idItem = new QTreeWidgetItem(propsTree);
    idItem->setText(0, tr("ID"));
    idItem->setText(1, QString::number(constraint->id));

    // Value (for dimensional constraints)
    bool hasDimensionalValue = (constraint->type == ConstraintType::Distance
                                || constraint->type == ConstraintType::Radius
                                || constraint->type == ConstraintType::Diameter
                                || constraint->type == ConstraintType::Angle
                                || constraint->type == ConstraintType::FixedAngle);
    if (hasDimensionalValue) {
        auto* valueItem = new QTreeWidgetItem(propsTree);
        valueItem->setText(0, tr("Value"));
        bool isAngle = (constraint->type == ConstraintType::Angle
                        || constraint->type == ConstraintType::FixedAngle);
        QString suffix = isAngle ? QStringLiteral("\u00B0") : QStringLiteral(" ") + unitSuffix();
        valueItem->setText(1, QStringLiteral("%1%2")
                           .arg(constraint->value, 0, 'f', 2)
                           .arg(suffix));
        if (constraint->isDriving) {
            valueItem->setFlags(valueItem->flags() | Qt::ItemIsEditable);
            valueItem->setData(0, Qt::UserRole, constraintId);
            valueItem->setData(0, Qt::UserRole + 1, QStringLiteral("constraintValue"));
        }
    }

    // Driving / Reference
    auto* drivingItem = new QTreeWidgetItem(propsTree);
    drivingItem->setText(0, tr("Mode"));
    drivingItem->setText(1, constraint->isDriving ? tr("Driving") : tr("Reference"));

    // Enabled
    auto* enabledItem = new QTreeWidgetItem(propsTree);
    enabledItem->setText(0, tr("Enabled"));
    enabledItem->setText(1, constraint->enabled ? tr("Yes") : tr("No"));

    // Parent entities
    auto* entitiesHeader = new QTreeWidgetItem(propsTree);
    entitiesHeader->setText(0, tr("Entities"));

    auto entityTypeName = [](SketchEntityType t) -> QString {
        switch (t) {
        case SketchEntityType::Point:     return QStringLiteral("Point");
        case SketchEntityType::Line:      return QStringLiteral("Line");
        case SketchEntityType::Rectangle: return QStringLiteral("Rectangle");
        case SketchEntityType::Circle:    return QStringLiteral("Circle");
        case SketchEntityType::Arc:       return QStringLiteral("Arc");
        case SketchEntityType::Spline:    return QStringLiteral("Spline");
        case SketchEntityType::Polygon:   return QStringLiteral("Polygon");
        case SketchEntityType::Slot:      return QStringLiteral("Slot");
        case SketchEntityType::Ellipse:   return QStringLiteral("Ellipse");
        case SketchEntityType::Text:      return QStringLiteral("Text");
        case SketchEntityType::Dimension: return QStringLiteral("Dimension");
        default:                          return QStringLiteral("Unknown");
        }
    };

    if (isSweep) {
        for (const auto& g : m_sketchCanvas->groups()) {
            if (m_sketchCanvas->isSweepAngleGroup(g.id)
                    && g.containsConstraint(constraintId)) {
                for (int eid : g.entityIds) {
                    const SketchEntity* e = m_sketchCanvas->entityById(eid);
                    if (e && e->type == SketchEntityType::Arc) {
                        auto* eItem = new QTreeWidgetItem(entitiesHeader);
                        eItem->setText(0, entityTypeName(e->type));
                        eItem->setText(1, QStringLiteral("ID %1").arg(e->id));
                    }
                }
                break;
            }
        }
    } else {
        for (int eid : constraint->entityIds) {
            const SketchEntity* e = m_sketchCanvas->entityById(eid);
            if (e) {
                auto* eItem = new QTreeWidgetItem(entitiesHeader);
                eItem->setText(0, entityTypeName(e->type));
                eItem->setText(1, QStringLiteral("ID %1").arg(e->id));
            }
        }
    }

    propsTree->expandAll();
    propsTree->blockSignals(false);
}

void MainWindow::showFeatureProperties(int index)
{
    QTreeWidget* propsTree = propertiesTree();
    if (!propsTree)
        return;

    propsTree->clear();

    if (index < 0 || index >= m_timeline->itemCount())
        return;

    TimelineFeature feature = m_timeline->featureAt(index);
    QString featureName = m_timeline->nameAt(index);
    QString units = unitSuffix();

    // Helper to set property (column 0) and value (column 1), read-only
    auto setProperty = [](QTreeWidgetItem* item, const QString& prop, const QString& value) {
        item->setText(0, prop);
        item->setText(1, value);
        item->setToolTip(0, prop);
        item->setToolTip(1, value);
    };

    // Helper to set property and editable value (no units)
    auto setEditableProperty = [](QTreeWidgetItem* item, const QString& prop, const QString& value) {
        item->setText(0, prop);
        item->setText(1, value);
        item->setToolTip(0, prop);
        item->setToolTip(1, value);
        item->setFlags(item->flags() | Qt::ItemIsEditable);
    };

    // Helper for section headers (spans both columns)
    auto setHeader = [](QTreeWidgetItem* item, const QString& text) {
        item->setText(0, text);
        item->setToolTip(0, text);
    };

    // Helper to create dropdown property
    auto setDropdownProperty = [](QTreeWidgetItem* item, const QString& prop,
                                  const QStringList& options, int currentIndex) {
        item->setText(0, prop);
        item->setText(1, options.value(currentIndex));
        item->setToolTip(0, prop);
        item->setToolTip(1, options.value(currentIndex));
        item->setData(1, Qt::UserRole, QStringLiteral("dropdown"));
        item->setData(1, Qt::UserRole + 1, options);
        item->setData(1, Qt::UserRole + 2, currentIndex);
    };

    // Get parameters from the document for formula support
    QMap<QString, double> params = parameterValues();

    // Helper to create formula-enabled property with fx button
    auto setFormulaProperty = [propsTree, &params](QTreeWidgetItem* item, const QString& prop,
                                                    const QString& expr, const QString& unitSuffix) {
        item->setText(0, prop);
        item->setToolTip(0, prop);

        auto* formulaField = new FormulaField(propsTree);
        formulaField->setPropertyName(prop);
        formulaField->setUnitSuffix(unitSuffix);
        formulaField->setParameters(params);
        formulaField->setExpression(expr);

        propsTree->setItemWidget(item, 1, formulaField);
    };

    // Feature name (editable)
    auto* nameItem = new QTreeWidgetItem(propsTree);
    setEditableProperty(nameItem, tr("Name"), featureName);

    // Feature type (read-only)
    auto* typeItem = new QTreeWidgetItem(propsTree);
    QString typeName;
    switch (feature) {
    case TimelineFeature::Origin:    typeName = tr("Origin"); break;
    case TimelineFeature::Sketch:    typeName = tr("Sketch"); break;
    case TimelineFeature::Extrude:   typeName = tr("Extrude"); break;
    case TimelineFeature::Revolve:   typeName = tr("Revolve"); break;
    case TimelineFeature::Fillet:    typeName = tr("Fillet"); break;
    case TimelineFeature::Chamfer:   typeName = tr("Chamfer"); break;
    case TimelineFeature::Hole:      typeName = tr("Hole"); break;
    case TimelineFeature::Mirror:    typeName = tr("Mirror"); break;
    case TimelineFeature::Pattern:   typeName = tr("Pattern"); break;
    case TimelineFeature::Box:       typeName = tr("Box"); break;
    case TimelineFeature::Cylinder:  typeName = tr("Cylinder"); break;
    case TimelineFeature::Sphere:    typeName = tr("Sphere"); break;
    case TimelineFeature::Move:      typeName = tr("Move"); break;
    case TimelineFeature::Join:      typeName = tr("Join"); break;
    case TimelineFeature::Cut:       typeName = tr("Cut"); break;
    case TimelineFeature::Intersect: typeName = tr("Intersect"); break;
    }
    setProperty(typeItem, tr("Type"), typeName);

    // Feature-specific properties
    auto* propsHeader = new QTreeWidgetItem(propsTree);
    setHeader(propsHeader, tr("Properties"));
    propsHeader->setExpanded(true);

    switch (feature) {
    case TimelineFeature::Origin:
        {
            auto* item = new QTreeWidgetItem(propsHeader);
            setProperty(item, tr("Position"), tr("(0, 0, 0)"));
        }
        break;

    case TimelineFeature::Sketch:
        // Delegate to virtual method for mode-specific data
        populateSketchFeatureProperties(propsHeader, index, units);
        break;

    case TimelineFeature::Extrude:
        {
            auto* distItem = new QTreeWidgetItem(propsHeader);
            setFormulaProperty(distItem, tr("Distance"), QStringLiteral("10"), units);
            auto* dirItem = new QTreeWidgetItem(propsHeader);
            setDropdownProperty(dirItem, tr("Direction"),
                {tr("One Side"), tr("Two Sides"), tr("Symmetric")}, 0);
            auto* opItem = new QTreeWidgetItem(propsHeader);
            setDropdownProperty(opItem, tr("Operation"),
                {tr("Join"), tr("Cut"), tr("Intersect"), tr("New Body")}, 0);
        }
        break;

    case TimelineFeature::Revolve:
        {
            auto* angleItem = new QTreeWidgetItem(propsHeader);
            setFormulaProperty(angleItem, tr("Angle"), QStringLiteral("360"), QStringLiteral("°"));
            auto* axisItem = new QTreeWidgetItem(propsHeader);
            setDropdownProperty(axisItem, tr("Axis"),
                {tr("X Axis"), tr("Y Axis"), tr("Z Axis")}, 0);
        }
        break;

    case TimelineFeature::Fillet:
        {
            auto* radiusItem = new QTreeWidgetItem(propsHeader);
            setFormulaProperty(radiusItem, tr("Radius"), QStringLiteral("radius"), units);
            auto* edgesItem = new QTreeWidgetItem(propsHeader);
            setProperty(edgesItem, tr("Edges"), tr("4"));
        }
        break;

    case TimelineFeature::Chamfer:
        {
            auto* distItem = new QTreeWidgetItem(propsHeader);
            setFormulaProperty(distItem, tr("Distance"), QStringLiteral("1"), units);
            auto* edgesItem = new QTreeWidgetItem(propsHeader);
            setProperty(edgesItem, tr("Edges"), tr("2"));
        }
        break;

    case TimelineFeature::Hole:
        {
            auto* diamItem = new QTreeWidgetItem(propsHeader);
            setFormulaProperty(diamItem, tr("Diameter"), QStringLiteral("5"), units);
            auto* depthItem = new QTreeWidgetItem(propsHeader);
            setDropdownProperty(depthItem, tr("Depth"),
                {tr("Through All"), tr("To Depth"), tr("To Face")}, 0);
            auto* typeHole = new QTreeWidgetItem(propsHeader);
            setDropdownProperty(typeHole, tr("Hole Type"),
                {tr("Simple"), tr("Counterbore"), tr("Countersink"), tr("Threaded")}, 0);
        }
        break;

    case TimelineFeature::Mirror:
        {
            auto* planeItem = new QTreeWidgetItem(propsHeader);
            setDropdownProperty(planeItem, tr("Mirror Plane"),
                {tr("XY"), tr("XZ"), tr("YZ")}, 2);
            auto* bodiesItem = new QTreeWidgetItem(propsHeader);
            setProperty(bodiesItem, tr("Bodies"), tr("1"));
        }
        break;

    case TimelineFeature::Pattern:
        {
            auto* typeP = new QTreeWidgetItem(propsHeader);
            setDropdownProperty(typeP, tr("Pattern Type"),
                {tr("Rectangular"), tr("Circular")}, 0);
            auto* countItem = new QTreeWidgetItem(propsHeader);
            setEditableProperty(countItem, tr("Count"), tr("3 x 2"));
            auto* spacingItem = new QTreeWidgetItem(propsHeader);
            setEditableProperty(spacingItem, tr("Spacing"), QStringLiteral("15 x 10 %1").arg(units));
        }
        break;

    case TimelineFeature::Box:
        {
            auto* dimItem = new QTreeWidgetItem(propsHeader);
            setEditableProperty(dimItem, tr("Dimensions"), QStringLiteral("50 x 30 x 20 %1").arg(units));
        }
        break;

    case TimelineFeature::Cylinder:
        {
            auto* diamItem = new QTreeWidgetItem(propsHeader);
            setFormulaProperty(diamItem, tr("Diameter"), QStringLiteral("width / 2"), units);
            auto* heightItem = new QTreeWidgetItem(propsHeader);
            setFormulaProperty(heightItem, tr("Height"), QStringLiteral("height + depth"), units);
        }
        break;

    case TimelineFeature::Sphere:
        {
            auto* diamItem = new QTreeWidgetItem(propsHeader);
            setFormulaProperty(diamItem, tr("Diameter"), QStringLiteral("25"), units);
        }
        break;

    case TimelineFeature::Move:
        {
            auto* transItem = new QTreeWidgetItem(propsHeader);
            setEditableProperty(transItem, tr("Translation"), QStringLiteral("(10, 5, 0) %1").arg(units));
            auto* rotItem = new QTreeWidgetItem(propsHeader);
            setFormulaProperty(rotItem, tr("Rotation"), QStringLiteral("0"), QStringLiteral("°"));
        }
        break;

    case TimelineFeature::Join:
    case TimelineFeature::Cut:
    case TimelineFeature::Intersect:
        {
            auto* bodiesItem = new QTreeWidgetItem(propsHeader);
            setProperty(bodiesItem, tr("Target Bodies"), tr("2"));
            auto* toolItem = new QTreeWidgetItem(propsHeader);
            setProperty(toolItem, tr("Tool Bodies"), tr("1"));
        }
        break;
    }

    propsTree->expandAll();

    // Connect double-click to show dropdown for dropdown properties
    disconnect(propsTree, &QTreeWidget::itemDoubleClicked, nullptr, nullptr);
    connect(propsTree, &QTreeWidget::itemDoubleClicked,
            this, [this, propsTree](QTreeWidgetItem* item, int column) {
        if (column != 1)
            return;

        if (item->data(1, Qt::UserRole).toString() != QStringLiteral("dropdown"))
            return;

        QStringList options = item->data(1, Qt::UserRole + 1).toStringList();
        int currentIndex = item->data(1, Qt::UserRole + 2).toInt();

        auto* combo = new QComboBox(propsTree);
        combo->addItems(options);
        combo->setCurrentIndex(currentIndex);

        propsTree->setItemWidget(item, 1, combo);
        combo->showPopup();

        connect(combo, &QComboBox::activated, this,
                [this, item, combo, propsTree](int index) {
            item->setText(1, combo->currentText());
            item->setToolTip(1, combo->currentText());
            item->setData(1, Qt::UserRole + 2, index);
            QTimer::singleShot(0, this, [propsTree, item]() {
                propsTree->setItemWidget(item, 1, nullptr);
            });
        });
    });
}

void MainWindow::populateSketchFeatureProperties(QTreeWidgetItem* parent,
                                                   int /*timelineIndex*/,
                                                   const QString& /*units*/)
{
    // Default: placeholder values (overridden by FullModeWindow with real data)
    auto* planeItem = new QTreeWidgetItem(parent);
    planeItem->setText(0, tr("Plane"));
    planeItem->setText(1, tr("XY"));

    auto* entitiesItem = new QTreeWidgetItem(parent);
    entitiesItem->setText(0, tr("Entities"));
    entitiesItem->setText(1, tr("5"));

    auto* constraintsItem = new QTreeWidgetItem(parent);
    constraintsItem->setText(0, tr("Constraints"));
    constraintsItem->setText(1, tr("8"));
}

void MainWindow::onSketchPropertyItemChanged(QTreeWidgetItem* item, int column)
{
    if (!item || !m_sketchCanvas || column != 1) return;

    QString propertyName = item->data(0, Qt::UserRole + 1).toString();
    if (propertyName.isEmpty()) return;

    // Handle constraint value edits
    if (propertyName == QStringLiteral("constraintValue")) {
        int constraintId = item->data(0, Qt::UserRole).toInt();
        if (constraintId <= 0) return;

        QString text = item->text(1);
        double newValue = parseScalar(text);
        if (std::isnan(newValue)) return;

        m_sketchCanvas->setConstraintValue(constraintId, newValue);

        QTimer::singleShot(0, this, [this, constraintId]() {
            if (QTreeWidget* pt = propertiesTree()) pt->blockSignals(true);
            showSketchConstraintProperties(constraintId);
            if (QTreeWidget* pt = propertiesTree()) pt->blockSignals(false);
        });
        return;
    }

    int entityId = item->data(0, Qt::UserRole).toInt();
    if (entityId <= 0) return;

    SketchEntity* entity = m_sketchCanvas->entityById(entityId);
    if (!entity) return;

    // Capture entity state before modification for undo
    const SketchEntity oldEntity = *entity;

    QString text = item->text(1);
    bool changed = false;

    if (propertyName.startsWith(QStringLiteral("point"))) {
        int idx = propertyName.mid(5).toInt();
        if (idx >= 0 && idx < entity->points.size()) {
            double x, y;
            if (parsePoint(text, x, y)) {
                entity->points[idx] = {x, y};
                changed = true;
            }
        }
    }
    else if (propertyName == QStringLiteral("radius")) {
        double val = parseScalar(text);
        if (!std::isnan(val) && val > 0) {
            entity->radius = val;
            if (entity->type == SketchEntityType::Arc
                    && entity->points.size() >= 3) {
                QPointF center(entity->points[0]);
                double startRad = qDegreesToRadians(entity->startAngle);
                double endRad = qDegreesToRadians(
                    entity->startAngle + entity->sweepAngle);
                entity->points[1] = {
                    center.x() + val * qCos(startRad),
                    center.y() + val * qSin(startRad)};
                entity->points[2] = {
                    center.x() + val * qCos(endRad),
                    center.y() + val * qSin(endRad)};
            } else if (entity->type == SketchEntityType::Circle) {
                QPointF center(entity->points[0]);
                for (int i = 1; i < entity->points.size(); ++i) {
                    QPointF dir = QPointF(entity->points[i]) - center;
                    double len = std::sqrt(dir.x() * dir.x()
                                         + dir.y() * dir.y());
                    if (len > 1e-6)
                        entity->points[i] = center + dir * (val / len);
                }
            }
            changed = true;
        }
    }
    else if (propertyName == QStringLiteral("diameter")) {
        double val = parseScalar(text);
        if (!std::isnan(val) && val > 0) {
            double r = val / 2.0;
            entity->radius = r;
            if (entity->type == SketchEntityType::Circle) {
                QPointF center(entity->points[0]);
                for (int i = 1; i < entity->points.size(); ++i) {
                    QPointF dir = QPointF(entity->points[i]) - center;
                    double len = std::sqrt(dir.x() * dir.x()
                                         + dir.y() * dir.y());
                    if (len > 1e-6)
                        entity->points[i] = center + dir * (r / len);
                }
            }
            changed = true;
        }
    }
    else if (propertyName == QStringLiteral("startAngle")) {
        double val = parseScalar(text);
        if (!std::isnan(val)) {
            entity->startAngle = val;
            if (entity->points.size() >= 3) {
                QPointF center(entity->points[0]);
                double r = entity->radius;
                double startRad = qDegreesToRadians(val);
                double endRad = qDegreesToRadians(
                    val + entity->sweepAngle);
                entity->points[1] = {
                    center.x() + r * qCos(startRad),
                    center.y() + r * qSin(startRad)};
                entity->points[2] = {
                    center.x() + r * qCos(endRad),
                    center.y() + r * qSin(endRad)};
            }
            changed = true;
        }
    }
    else if (propertyName == QStringLiteral("sweepAngle")) {
        double val = parseScalar(text);
        if (!std::isnan(val)) {
            entity->sweepAngle = val;
            if (entity->points.size() >= 3) {
                QPointF center(entity->points[0]);
                double r = entity->radius;
                double endRad = qDegreesToRadians(
                    entity->startAngle + val);
                entity->points[2] = {
                    center.x() + r * qCos(endRad),
                    center.y() + r * qSin(endRad)};
            }
            changed = true;
        }
    }
    else if (propertyName == QStringLiteral("length")) {
        double val = parseScalar(text);
        if (!std::isnan(val) && val > 0
                && entity->points.size() >= 2) {
            QPointF p0(entity->points[0]);
            QPointF p1(entity->points[1]);
            QPointF dir = p1 - p0;
            double curLen = std::sqrt(dir.x() * dir.x()
                                    + dir.y() * dir.y());
            if (curLen > 1e-6) {
                QPointF unit = dir / curLen;
                entity->points[1] = p0 + unit * val;
                changed = true;
            }
        }
    }
    else if (propertyName == QStringLiteral("width")) {
        double val = parseScalar(text);
        if (!std::isnan(val) && val > 0
                && entity->points.size() >= 2) {
            double sign = (entity->points[1].x >= entity->points[0].x)
                ? 1.0 : -1.0;
            entity->points[1].x = entity->points[0].x + sign * val;
            changed = true;
        }
    }
    else if (propertyName == QStringLiteral("height")) {
        double val = parseScalar(text);
        if (!std::isnan(val) && val > 0
                && entity->points.size() >= 2) {
            double sign = (entity->points[1].y >= entity->points[0].y)
                ? 1.0 : -1.0;
            entity->points[1].y = entity->points[0].y + sign * val;
            changed = true;
        }
    }
    else if (propertyName == QStringLiteral("sides")) {
        double val = parseScalar(text);
        int sides = static_cast<int>(val);
        if (sides >= 3 && sides <= 100) {
            entity->sides = sides;
            changed = true;
        }
    }
    else if (propertyName == QStringLiteral("majorRadius")) {
        double val = parseScalar(text);
        if (!std::isnan(val) && val > 0) {
            entity->majorRadius = val;
            changed = true;
        }
    }
    else if (propertyName == QStringLiteral("minorRadius")) {
        double val = parseScalar(text);
        if (!std::isnan(val) && val > 0) {
            entity->minorRadius = val;
            changed = true;
        }
    }
    else if (propertyName == QStringLiteral("text")) {
        entity->text = text.toStdString();
        changed = true;
    }
    else if (propertyName == QStringLiteral("fontSize")) {
        double val = parseScalar(text);
        if (!std::isnan(val) && val > 0) {
            entity->fontSize = val;
            changed = true;
        }
    }
    else if (propertyName == QStringLiteral("textRotation")) {
        double val = parseScalar(text);
        if (!std::isnan(val)) {
            entity->textRotation = val;
            changed = true;
        }
    }

    if (changed) {
        SketchCanvas::recomputeTextRotationHandle(*entity);

        m_sketchCanvas->pushUndoCommand(sketch::UndoCommand::modifyEntity(
            oldEntity, *entity,
            "Edit " + propertyName.toStdString()));

        m_sketchCanvas->notifyEntityChanged(entityId);
        QTimer::singleShot(0, this, [this, entityId]() {
            if (QTreeWidget* pt = propertiesTree()) pt->blockSignals(true);
            showSketchEntityProperties(entityId);
            if (QTreeWidget* pt = propertiesTree()) pt->blockSignals(false);
        });
    }
}

}  // namespace hobbycad

