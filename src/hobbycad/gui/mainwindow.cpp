// =====================================================================
//  src/hobbycad/gui/mainwindow.cpp — Base main window
// =====================================================================

#include "mainwindow.h"
#include "aboutdialog.h"
#include "bindingsdialog.h"
#include "clipanel.h"
#include "preferencesdialog.h"
#include "sketchactionbar.h"

#include <QAction>
#include <QActionGroup>
#include <QKeySequence>
#include <QApplication>
#include <QCloseEvent>
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
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

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

QTreeWidget* MainWindow::propertiesTree() const
{
    return m_propertiesTree;
}

SketchActionBar* MainWindow::sketchActionBar() const
{
    return m_sketchActionBar;
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

QString MainWindow::unitSuffix() const
{
    static const char* suffixes[] = {"mm", "cm", "m", "in", "ft"};
    if (m_currentUnits >= 0 && m_currentUnits < 5)
        return QString::fromLatin1(suffixes[m_currentUnits]);
    return QStringLiteral("mm");
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

    // Apply keyboard bindings from settings
    applyBindings();

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

    // Edit menu
    auto* editMenu = menuBar()->addMenu(tr("&Edit"));

    m_actionCut = editMenu->addAction(tr("Cu&t"),
        QKeySequence::Cut);
    m_actionCut->setEnabled(false);  // Enabled when selection exists

    m_actionCopy = editMenu->addAction(tr("&Copy"),
        QKeySequence::Copy);
    m_actionCopy->setEnabled(false);  // Enabled when selection exists

    m_actionPaste = editMenu->addAction(tr("&Paste"),
        QKeySequence::Paste);
    m_actionPaste->setEnabled(false);  // Enabled when clipboard has compatible data

    m_actionDelete = editMenu->addAction(tr("&Delete"),
        QKeySequence::Delete);
    m_actionDelete->setEnabled(false);  // Enabled when selection exists

    editMenu->addSeparator();

    m_actionSelectAll = editMenu->addAction(tr("Select &All"),
        QKeySequence::SelectAll);
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

    m_actionToggleTerminal = viewMenu->addAction(tr("&Terminal"),
        QKeySequence(Qt::CTRL | Qt::Key_QuoteLeft));
    m_actionToggleTerminal->setCheckable(true);
    m_actionToggleTerminal->setChecked(false);

    m_actionToggleFeatureTree = viewMenu->addAction(tr("P&roject"),
        QKeySequence(Qt::CTRL | Qt::Key_R));
    m_actionToggleFeatureTree->setCheckable(true);
    m_actionToggleFeatureTree->setChecked(true);

    m_actionToggleProperties = viewMenu->addAction(tr("&Properties"),
        QKeySequence(Qt::CTRL | Qt::Key_P));
    m_actionToggleProperties->setCheckable(true);
    m_actionToggleProperties->setChecked(true);

    m_actionToggleToolbar = viewMenu->addAction(tr("Tool&bar"));
    m_actionToggleToolbar->setCheckable(true);
    m_actionToggleToolbar->setChecked(true);

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

    m_actionResetView = viewMenu->addAction(tr("&Reset View"),
        QKeySequence(Qt::Key_Home));
    // Connected in FullModeWindow to viewport->resetCamera()

    m_actionRotateLeft = viewMenu->addAction(tr("Rotate &Left 90°"));

    m_actionRotateRight = viewMenu->addAction(tr("Rotate Ri&ght 90°"));

    viewMenu->addSeparator();

    m_actionShowGrid = viewMenu->addAction(tr("Show &Grid"),
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_G));
    m_actionShowGrid->setCheckable(true);
    m_actionShowGrid->setChecked(true);  // On by default

    m_actionSnapToGrid = viewMenu->addAction(tr("&Snap to Grid"),
        QKeySequence(Qt::CTRL | Qt::Key_G));
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
    // Project panel with File and Objects tabs
    m_featureTreeDock = new QDockWidget(tr("Project"), this);
    m_featureTreeDock->setObjectName(QStringLiteral("ProjectDock"));
    m_featureTreeDock->setAllowedAreas(
        Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    auto* projectTabs = new QTabWidget();
    projectTabs->setObjectName(QStringLiteral("ProjectTabs"));

    // File tab - shows project file structure
    auto* fileTree = new QTreeWidget();
    fileTree->setObjectName(QStringLiteral("FileTree"));
    fileTree->setHeaderHidden(true);
    fileTree->setRootIsDecorated(true);
    projectTabs->addTab(fileTree, tr("File"));

    // Objects tab - shows model objects/features (default)
    auto* objectsTree = new QTreeWidget();
    objectsTree->setObjectName(QStringLiteral("ObjectsTree"));
    objectsTree->setHeaderHidden(true);
    objectsTree->setRootIsDecorated(true);

    // Document Settings section
    auto* docSettings = new QTreeWidgetItem(objectsTree);
    docSettings->setText(0, tr("Document Settings"));
    docSettings->setExpanded(true);

    auto* unitsItem = new QTreeWidgetItem(docSettings);
    unitsItem->setText(0, tr("Units: mm"));
    unitsItem->setData(0, Qt::UserRole, QStringLiteral("units"));

    // Double-click on units shows a combobox dropdown
    connect(objectsTree, &QTreeWidget::itemDoubleClicked,
            this, [this, objectsTree](QTreeWidgetItem* item, int column) {
        Q_UNUSED(column);
        if (item->data(0, Qt::UserRole).toString() != QStringLiteral("units"))
            return;

        auto* combo = new QComboBox(objectsTree);
        combo->addItems({tr("mm"), tr("cm"), tr("m"), tr("in"), tr("ft")});

        // Select current unit
        QString currentText = item->text(0);
        int colonPos = currentText.indexOf(':');
        if (colonPos >= 0) {
            QString currentUnit = currentText.mid(colonPos + 2).trimmed();
            int idx = combo->findText(currentUnit);
            if (idx >= 0) combo->setCurrentIndex(idx);
        }

        objectsTree->setItemWidget(item, 0, combo);
        combo->showPopup();

        // When user selects an item, update and remove the widget
        connect(combo, &QComboBox::activated, this,
                [this, item, combo, objectsTree](int index) {
            item->setText(0, tr("Units: %1").arg(combo->currentText()));
            m_currentUnits = index;
            // Defer widget removal to avoid deleting during signal
            QTimer::singleShot(0, this, [objectsTree, item, index, this]() {
                objectsTree->setItemWidget(item, 0, nullptr);
                emit unitsChanged(index);
            });
        });
    });

    // Origin section
    auto* origin = new QTreeWidgetItem(objectsTree);
    origin->setText(0, tr("Origin"));
    origin->setExpanded(false);

    auto* originXY = new QTreeWidgetItem(origin);
    originXY->setText(0, tr("XY Plane"));
    auto* originXZ = new QTreeWidgetItem(origin);
    originXZ->setText(0, tr("XZ Plane"));
    auto* originYZ = new QTreeWidgetItem(origin);
    originYZ->setText(0, tr("YZ Plane"));
    auto* originX = new QTreeWidgetItem(origin);
    originX->setText(0, tr("X Axis"));
    auto* originY = new QTreeWidgetItem(origin);
    originY->setText(0, tr("Y Axis"));
    auto* originZ = new QTreeWidgetItem(origin);
    originZ->setText(0, tr("Z Axis"));
    auto* originPt = new QTreeWidgetItem(origin);
    originPt->setText(0, tr("Origin Point"));

    // Bodies section
    m_bodiesTreeItem = new QTreeWidgetItem(objectsTree);
    m_bodiesTreeItem->setText(0, tr("Bodies"));
    m_bodiesTreeItem->setData(0, Qt::UserRole, QStringLiteral("container.bodies"));
    m_bodiesTreeItem->setExpanded(true);

    // Sketches section
    m_sketchesTreeItem = new QTreeWidgetItem(objectsTree);
    m_sketchesTreeItem->setText(0, tr("Sketches"));
    m_sketchesTreeItem->setData(0, Qt::UserRole, QStringLiteral("container.sketches"));
    m_sketchesTreeItem->setExpanded(true);

    // Construction section
    m_constructionTreeItem = new QTreeWidgetItem(objectsTree);
    m_constructionTreeItem->setText(0, tr("Construction"));
    m_constructionTreeItem->setData(0, Qt::UserRole, QStringLiteral("container.construction"));
    m_constructionTreeItem->setExpanded(false);

    // Bodies, Sketches, and Construction containers are empty by default
    // Items are added as the user creates features

    projectTabs->addTab(objectsTree, tr("Objects"));

    // Select Objects tab by default
    projectTabs->setCurrentIndex(1);

    // F2 to edit selected item in objects tree
    auto* objectsF2 = new QShortcut(QKeySequence(Qt::Key_F2), objectsTree);
    connect(objectsF2, &QShortcut::activated, this, [objectsTree]() {
        QTreeWidgetItem* item = objectsTree->currentItem();
        if (item && (item->flags() & Qt::ItemIsEditable)) {
            objectsTree->editItem(item, 0);
        }
    });

    // Handle item selection in objects tree
    connect(objectsTree, &QTreeWidget::currentItemChanged,
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
        QString errorMsg;
        if (m_project.load(path, &errorMsg)) {
            // Sync document shapes from project
            m_document.clear();
            for (const auto& shape : m_project.shapes()) {
                m_document.addShape(shape);
            }
            m_document.setModified(false);

            updateTitle();
            onDocumentLoaded();
            m_statusLabel->setText(tr("Opened project: %1").arg(m_project.name()));
        } else {
            QMessageBox::warning(this,
                tr("Open Failed"),
                tr("Could not open project:\n%1\n\n%2").arg(path, errorMsg));
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

        if (m_document.loadBrep(path)) {
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
        QString errorMsg;
        if (m_project.save(QString(), &errorMsg)) {
            m_document.setModified(false);
            updateTitle();
            m_statusLabel->setText(tr("Saved project: %1").arg(m_project.name()));
        } else {
            QMessageBox::warning(this,
                tr("Save Failed"),
                tr("Could not save project:\n%1").arg(errorMsg));
        }
    } else if (!m_document.isNew()) {
        // Save to existing BREP file
        if (m_document.saveBrep()) {
            updateTitle();
            m_statusLabel->setText(tr("Saved: %1").arg(m_document.filePath()));
        } else {
            QMessageBox::warning(this,
                tr("Save Failed"),
                tr("Could not save file:\n%1").arg(m_document.filePath()));
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
        m_project.setName(info.fileName());

        QString errorMsg;
        if (m_project.save(path, &errorMsg)) {
            m_document.setModified(false);
            updateTitle();
            m_statusLabel->setText(tr("Saved project: %1").arg(m_project.name()));
        } else {
            QMessageBox::warning(this,
                tr("Save Failed"),
                tr("Could not save project:\n%1").arg(errorMsg));
        }
    } else {
        // Save as BREP
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
        settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
        settings.setValue(QStringLiteral("window/state"), saveState());
        event->accept();
    } else {
        event->ignore();
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
            QString errorMsg;
            if (!m_project.save(QString(), &errorMsg)) {
                QMessageBox::warning(this,
                    tr("Save Failed"),
                    tr("Could not save project:\n%1").arg(errorMsg));
                return false;
            }
        } else if (!m_document.isNew()) {
            // Save to existing BREP file
            if (!m_document.saveBrep()) {
                QMessageBox::warning(this,
                    tr("Save Failed"),
                    tr("Could not save file:\n%1").arg(m_document.filePath()));
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
                m_project.setName(info.baseName().replace(QStringLiteral(".hcad"), QString()));

                QString errorMsg;
                if (!m_project.save(path, &errorMsg)) {
                    QMessageBox::warning(this,
                        tr("Save Failed"),
                        tr("Could not save project:\n%1").arg(errorMsg));
                    return false;
                }
            } else {
                path = ensureBrepExtension(path, selectedFilter);
                if (!m_document.saveBrep(path)) {
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
            QKeySequence seq(binding);
            if (!seq.isEmpty()) {
                shortcuts.append(seq);
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
        title += QStringLiteral(" — ") + m_project.name();
    } else if (!m_document.isNew()) {
        // Show legacy BREP file path
        title += QStringLiteral(" — ") + m_document.filePath();
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
    QString u = units.toLower();
    if (u == QLatin1String("mm"))
        m_currentUnits = 0;
    else if (u == QLatin1String("cm"))
        m_currentUnits = 1;
    else if (u == QLatin1String("m"))
        m_currentUnits = 2;
    else if (u == QLatin1String("in"))
        m_currentUnits = 3;
    else if (u == QLatin1String("ft"))
        m_currentUnits = 4;
    else
        m_currentUnits = 0;  // default to mm
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

}  // namespace hobbycad

