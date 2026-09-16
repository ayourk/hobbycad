// =====================================================================
//  src/hobbycad/gui/mainwindow.cpp — Base main window
// =====================================================================

#include "mainwindow.h"
#include "propertyrow.h"
#include <hobbycad/sketch/undo.h>
#include <hobbycad/sketch/parsing.h>
#include <QMouseEvent>
#include "planetransformpanel.h"
#include <QBrush>
#include <hobbycad/sketch/transform.h>
#include <hobbycad/sketch/queries.h>
#include <hobbycad/sketch/properties.h>
#include <hobbycad/geometry/utils.h>
#include "editinplace.h"
#include "objectsbrowserwidget.h"

#include <hobbycad/browser.h>
#include <hobbycad/naming.h>
#include <cstdlib>
#include <QDateTime>
#include <QTextStream>
#include <QDir>
#include <hobbycad/crashhandler.h>
#include "aboutdialog.h"
#include "bindingsdialog.h"
#include "changelogpanel.h"
#include "clipanel.h"
#include "../cli/cliengine.h"
#include "formulafield.h"
#include "modeltoolbar.h"
#include "parametersdialog.h"
#include "preferencesdialog.h"
#include "projectbrowserwidget.h"
#include "sketchplanedialog.h"
#include "constraintexplorer.h"
#include "sketchpropertieswidget.h"
#include <hobbycad/project_undo.h>
#include "backgroundcalibrationdialog.h"
#include "sketchtoolbar.h"
#include "timelinewidget.h"
#include "../i18n/translations.h"

#include "sketchcanvas.h"
#include "themeeditordialog.h"

#include <hobbycad/project.h>
#include <hobbycad/sketch/export.h>
#include <hobbycad/sketch/profiles.h>
#include <hobbycad/sketch/dxf_import.h>
#include "sketchoptionswidget.h"
#include "sketchutils.h"
#include <hobbycad/step_io.h>
#include <hobbycad/stl_io.h>
#include <hobbycad/units.h>

#include <QAction>
#include <QActionGroup>
#include <QKeySequence>
#include <QApplication>
#include <QScreen>
#include <QGuiApplication>
#include <QCloseEvent>
#include <QMoveEvent>
#include <QResizeEvent>
#include <QWindowStateChangeEvent>
#include <QComboBox>
#include <QDockWidget>
#include <QDir>
#include <QFileDialog>
#include <QLineEdit>
#include <QInputDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QFile>
#include <QSettings>
#include <QShortcut>
#include <QStackedWidget>
#include <QScrollArea>
#include <QHeaderView>
#include <QAbstractItemModel>
#include <functional>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialog>
#include <QTextEdit>
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
// Relinking picks a PROJECT, never a BREP or an arbitrary file, so it gets
// its own filter rather than reusing kOpenFilter.
static const char kRelinkFilter[] =
    QT_TR_NOOP("HobbyCAD Projects (*.hcad);;All Files (*)");
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
    // Hand the crash handler a way to save the user's work.
    hobbycad::CrashHandler::setEmergencySave([this]() { saveEmergencyCopy(); });

    setObjectName(QStringLiteral("MainWindow"));
    setMinimumSize(800, 600);
    createMenus();
    createStatusBar();
    createDockPanels();

    // Start with an empty document
    updateTitle();
}

MainWindow::~MainWindow()
{
    // Drop the callback before this window dies, or a crash during
    // shutdown would call into a destroyed object.
    hobbycad::CrashHandler::setEmergencySave(nullptr);
}

// =====================================================================
//  Crash recovery
// =====================================================================

std::string MainWindow::recoveryDir()
{
    const char* home = std::getenv("HOME");
    if (!home) return {};
    return std::string(home) + "/.config/HobbyCAD/recovery";
}

void MainWindow::saveEmergencyCopy()
{
    // Runs from a signal handler.  Guard against re-entry and never let an
    // exception escape into the handler.
    static bool alreadySaving = false;
    if (alreadySaving) return;
    alreadySaving = true;

    try {
        const std::string dir = recoveryDir();
        if (dir.empty()) return;

        QDir().mkpath(QString::fromStdString(dir));

        // The project holds every stored sketch; the one on the canvas is
        // written as it stands.
        const hobbycad::SketchDraft draft = draftFromCanvas();
        const std::string target = dir + "/session";
        std::string err;
        if (m_session.save(target, &err, (m_inSketchMode && m_draftOpen) ? &draft : nullptr)) {
            // Record where the work came from so recovery can explain it.
            QFile info(QString::fromStdString(dir + "/session.info"));
            if (info.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                QTextStream ts(&info);
                ts << "name=" << QString::fromStdString(m_project.name()) << "\n"
                   << "saved=" << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n";
            }
        }
    } catch (...) {
        // Swallow everything: we are already crashing.
    }
}

void MainWindow::checkForCrashRecovery()
{
    const std::string dir = recoveryDir();
    if (dir.empty()) return;

    const QString sessionPath = QString::fromStdString(dir + "/session");
    if (!QDir(sessionPath).exists()) return;

    // saveEmergencyCopy() already records both of these; they were being
    // dumped into the details pane, where a user has to go looking. Someone
    // who has crashed twice needs to know WHICH recovery this is before
    // deciding, and the answer was one click away behind "Show Details".
    QString detail, recoveredName, recoveredWhen;
    QFile info(QString::fromStdString(dir + "/session.info"));
    if (info.open(QIODevice::ReadOnly)) {
        detail = QString::fromUtf8(info.readAll()).trimmed();
        for (const QString& line : detail.split('\n')) {
            if (line.startsWith("name="))  recoveredName = line.mid(5).trimmed();
            if (line.startsWith("saved=")) recoveredWhen = line.mid(6).trimmed();
        }
    }

    QString what = tr("HobbyCAD did not exit cleanly last time.\n\n"
                      "A recovery copy of your project was saved.");
    if (!recoveredName.isEmpty())
        what += tr("\n\nProject: %1").arg(recoveredName);
    if (!recoveredWhen.isEmpty()) {
        const QDateTime when = QDateTime::fromString(recoveredWhen, Qt::ISODate);
        what += tr("\nSaved: %1").arg(when.isValid()
                                      ? when.toString("yyyy-MM-dd HH:mm:ss")
                                      : recoveredWhen);
    }

    // A custom dialog rather than QMessageBox: QMessageBox lays its buttons out
    // by platform role (and adds its own "Show Details" button wherever the
    // style puts it), so the order is not ours to set. Here a plain horizontal
    // row gives left-to-right insertion order, with Show Details first (Aaron).
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Recover Unsaved Work"));
    QVBoxLayout* lay = new QVBoxLayout(&dlg);

    QLabel* msg = new QLabel(what, &dlg);
    msg->setWordWrap(true);
    msg->setTextInteractionFlags(Qt::TextSelectableByMouse);
    lay->addWidget(msg);

    QTextEdit* detailView = nullptr;
    if (!detail.isEmpty()) {
        detailView = new QTextEdit(&dlg);
        detailView->setReadOnly(true);
        detailView->setPlainText(detail);
        detailView->setVisible(false);
        lay->addWidget(detailView);
    }

    QHBoxLayout* row = new QHBoxLayout();
    QPushButton* detailsBtn = nullptr;
    if (detailView) {
        detailsBtn = new QPushButton(tr("Show Details\u2026"), &dlg);
        row->addWidget(detailsBtn);            // first, per request
    }
    row->addStretch();
    QPushButton* openBtn   = new QPushButton(tr("Open Recovery Copy"), &dlg);
    QPushButton* deleteBtn = new QPushButton(tr("Delete"), &dlg);
    QPushButton* ignoreBtn = new QPushButton(tr("Ignore for Now"), &dlg);
    openBtn->setDefault(true);
    row->addWidget(openBtn);
    row->addWidget(deleteBtn);
    row->addWidget(ignoreBtn);
    lay->addLayout(row);

    // Which action the user chose; anything else (closing the dialog) is Ignore.
    enum RecoveryChoice { Choose_Ignore = 0, Choose_Open, Choose_Delete };
    RecoveryChoice choice = Choose_Ignore;
    if (detailsBtn) {
        connect(detailsBtn, &QPushButton::clicked, &dlg, [&]() {
            const bool show = !detailView->isVisible();
            detailView->setVisible(show);
            detailsBtn->setText(show ? tr("Hide Details\u2026") : tr("Show Details\u2026"));
            dlg.adjustSize();
        });
    }
    connect(openBtn,   &QPushButton::clicked, &dlg, [&]() { choice = Choose_Open;   dlg.accept(); });
    connect(deleteBtn, &QPushButton::clicked, &dlg, [&]() { choice = Choose_Delete; dlg.accept(); });
    connect(ignoreBtn, &QPushButton::clicked, &dlg, [&]() { choice = Choose_Ignore; dlg.reject(); });
    dlg.exec();

    if (choice == Choose_Open) {
        std::string err;
        if (m_project.load(sessionPath.toStdString(), &err)) {
            onDocumentLoaded();
            updateTitle();
            m_statusLabel->setText(tr("Recovered project from the last session"));

            // Check what survived. A crash copy is written from a signal
            // handler mid-fault and deliberately validates NOTHING; that
            // is correct there, since whatever exists beats losing it. It
            // also makes this the file in the system most likely to hold a
            // collapsed primitive or a dangling reference, and the only
            // load path that never looked.
            const QStringList problems = sketchProblems();
            if (!problems.isEmpty()) {
                QMessageBox dmg(this);
                dmg.setIcon(QMessageBox::Warning);
                dmg.setWindowTitle(tr("Recovered With Damage"));
                dmg.setText(tr("The recovery copy opened, but %n sketch "
                               "problem(s) were found in it.\n\n"
                               "This is expected after a crash: the emergency "
                               "save records whatever was in memory at the "
                               "moment of the fault.", "", problems.size()));
                dmg.setDetailedText(problems.join('\n'));
                dmg.exec();
            }
        } else {
            QMessageBox::critical(this, tr("Recovery Failed"),
                tr("Could not open the recovery copy:\n%1")
                    .arg(QString::fromStdString(err)));
        }
        return;
    }

    if (choice == Choose_Delete) {
        // Permanently discard the recovery copy. It holds UNSAVED work, so
        // confirm first: deleting it cannot be undone.
        const QMessageBox::StandardButton ok = QMessageBox::question(this,
            tr("Delete Recovery Copy"),
            tr("Permanently delete the recovered unsaved work?\n\n"
               "This cannot be undone."),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (ok == QMessageBox::Yes) {
            QDir(sessionPath).removeRecursively();
            QFile::remove(QString::fromStdString(dir + "/session.info"));
            if (m_statusLabel) m_statusLabel->setText(tr("Deleted the recovery copy"));
        }
        // If canceled, leave the recovery in place; it will prompt again next
        // launch, which is the least surprising outcome for "I did not decide".
        return;
    }

    // Kept, not deleted: move it aside with a timestamp so a later crash
    // cannot silently overwrite work the user has not looked at yet.
    const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd-hhmmss");
    QDir().rename(sessionPath, QString::fromStdString(dir + "/session-") + stamp);
    QFile::rename(QString::fromStdString(dir + "/session.info"),
                  QString::fromStdString(dir + "/session-") + stamp + ".info");
}

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

QAction* MainWindow::lookAtAction() const
{
    return m_actionLookAt;
}

QAction* MainWindow::sliceAction() const
{
    return m_actionSlice;
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

void MainWindow::updatePropertiesTreeHeight()
{
    if (!m_propertiesTree) return;
    int rows = 0;
    std::function<void(QTreeWidgetItem*)> count = [&](QTreeWidgetItem* it) {
        ++rows;
        if (it->isExpanded())
            for (int i = 0; i < it->childCount(); ++i) count(it->child(i));
    };
    for (int i = 0; i < m_propertiesTree->topLevelItemCount(); ++i)
        count(m_propertiesTree->topLevelItem(i));

    const int rowH = qMax(m_propertiesTree->fontMetrics().height() + 8, 26);
    int h = 2 * m_propertiesTree->frameWidth();
    if (m_propertiesTree->header() && !m_propertiesTree->header()->isHidden())
        h += m_propertiesTree->header()->sizeHint().height();
    h += (rows > 0 ? rows : 1) * rowH;
    m_propertiesTree->setFixedHeight(h);
}

hobbycad::ChangelogPanel* MainWindow::changelogPanel() const
{
    return m_changelogPanel;
}

int MainWindow::currentUnits() const
{
    return m_currentUnits;
}

LengthUnit MainWindow::currentLengthUnit() const
{
    return lengthUnitFromIndex(m_currentUnits);
}

QString MainWindow::formatLength(double mm) const
{
    const hobbycad::LengthUnit unit = currentLengthUnit();
    return QString::fromStdString(
        hobbycad::formatDouble(hobbycad::mmToUnit(mm, unit),
                               hobbycad::unitDisplayPrecision(unit)));
}

// "(x, y) unit" and "v unit" for the properties tree.
QString MainWindow::pointText(const Point2D& p, const QString& units) const
{
    return QStringLiteral("(%1, %2) %3").arg(formatLength(p.x)).arg(formatLength(p.y)).arg(units);
}

QString MainWindow::lengthText(double mm, const QString& units) const
{
    return QStringLiteral("%1 %2").arg(formatLength(mm)).arg(units);
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

void MainWindow::applyFirstRunGeometry()
{
    // No saved geometry: the window would open at its 800x600 minimum,
    // which on a large screen leaves the two docks holding most of the width
    // and a small viewport. Open at 85% of the available screen,
    // centered, capped so a very large screen does not get a wall of
    // window; the user's next resize is saved and wins from then on.
    const QScreen* scr = screen() ? screen() : QGuiApplication::primaryScreen();
    if (!scr) {
        return;
    }
    const QRect avail = scr->availableGeometry();
    QSize size(qRound(avail.width() * 0.85), qRound(avail.height() * 0.85));
    size = size.boundedTo(QSize(1920, 1200)).expandedTo(minimumSize());
    resize(size);
    // Centered, but never above or left of the available area: on a screen
    // smaller than the minimum window (a 1024x600 netbook, say) centering
    // would push the title bar off the top edge; anchoring at the top-left
    // keeps the title bar and the menus reachable and lets only the bottom
    // and right edges overflow.
    QPoint pos = avail.center() - QPoint(size.width() / 2, size.height() / 2);
    pos.setX(qMax(pos.x(), avail.left()));
    pos.setY(qMax(pos.y(), avail.top()));
    move(pos);
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
        } else {
            applyFirstRunGeometry();
        }
        if (settings.contains(QStringLiteral("window/state"))) {
            restoreState(
                settings.value(QStringLiteral("window/state")).toByteArray());
        }
        // Restore the maximized state if it was saved. restoreGeometry() ALSO
        // re-applies whatever maximized/fullscreen state the geometry byte array
        // was saved with (Qt stores it there), so a blob captured while
        // maximized reopens maximized even when the flag below is false. Make
        // the saved flag authoritative: maximize when true, and otherwise clear
        // the maximized/fullscreen bits restoreGeometry may have set.
        if (settings.value(QStringLiteral("window/maximized"), false).toBool()) {
            showMaximized();
            m_windowMaximized = true;
        } else {
            setWindowState(windowState()
                           & ~(Qt::WindowMaximized | Qt::WindowFullScreen));
            m_windowMaximized = false;
        }
    }

    // Re-tabify the sketch panels AFTER restoreState(). A saved layout does
    // not know these two docks (their objectNames are newer than most stored
    // states), so Qt appends them as separate stacked docks and silently
    // discards the tabifyDockWidget() done at construction. Doing it again
    // here is idempotent and survives both the restored and fresh cases.
    if (m_propertiesDock && m_constraintsDock) {
        tabifyDockWidget(m_propertiesDock, m_constraintsDock);
        if (m_sketchOptionsDock) tabifyDockWidget(m_constraintsDock, m_sketchOptionsDock);
        m_propertiesDock->raise();
    }
    showProjectSummary();
    // A layout saved while a sketch was open replays these two docks as
    // visible, and nothing hides them again until the next sketch is left.
    // They belong to sketch mode only, and no sketch is open at startup.
    if (!m_inSketchMode) {
        if (m_sketchPropsWidget) m_sketchPropsWidget->setVisible(false);
        if (m_constraintsDock) m_constraintsDock->setVisible(false);
        if (m_sketchOptionsDock) m_sketchOptionsDock->setVisible(false);
    }

    // Sync toggle actions to actual dock visibility after restoreState.
    // Use !isHidden() rather than isVisible() because the main window
    // hasn't been show()n yet; isVisible() always returns false during
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

// File menu
void MainWindow::createFileMenu()
{
    m_menuFile = menuBar()->addMenu(QString());

    m_actionNew = m_menuFile->addAction(QString(), this, &MainWindow::onFileNew);
    m_actionNew->setShortcut(QKeySequence::New);

    m_actionOpen = m_menuFile->addAction(QString(), this, &MainWindow::onFileOpen);
    m_actionOpen->setShortcut(QKeySequence::Open);

    m_menuFile->addSeparator();

    m_actionSave = m_menuFile->addAction(QString(), this, &MainWindow::onFileSave);
    m_actionSave->setShortcut(QKeySequence::Save);

    m_actionSaveAs = m_menuFile->addAction(QString(), this, &MainWindow::onFileSaveAs);
    m_actionSaveAs->setShortcut(QKeySequence::SaveAs);

    m_menuFile->addSeparator();

    m_actionClose = m_menuFile->addAction(QString(), this, &MainWindow::onFileClose);
    m_actionClose->setShortcut(QKeySequence::Close);

    m_menuFile->addSeparator();

    // Import submenu
    m_menuImport = m_menuFile->addMenu(QString());
    m_actionImportStep = m_menuImport->addAction(QString(),
        this, &MainWindow::onFileImportStep);
    m_actionImportDXF = m_menuImport->addAction(QString(),
        this, &MainWindow::onFileImportDXF);
    m_actionImportDXF->setEnabled(false);

    // Export submenu
    m_menuExport = m_menuFile->addMenu(QString());
    m_actionExportStep = m_menuExport->addAction(QString(),
        this, &MainWindow::onFileExportStep);

    m_actionExportStl = m_menuExport->addAction(QString(),
        this, &MainWindow::onFileExportStl);

    m_menuExport->addSeparator();

    m_actionExportDXF = m_menuExport->addAction(QString(),
        this, &MainWindow::onFileExportDXF);
    m_actionExportDXF->setEnabled(false);

    m_actionExportSVG = m_menuExport->addAction(QString(),
        this, &MainWindow::onFileExportSVG);
    m_actionExportSVG->setEnabled(false);

    m_menuFile->addSeparator();

    m_actionQuit = m_menuFile->addAction(QString(), this, &MainWindow::onFileQuit);
    m_actionQuit->setShortcut(QKeySequence::Quit);
}

// Edit menu
void MainWindow::createEditMenu()
{
    m_menuEdit = menuBar()->addMenu(QString());

    m_actionUndo = m_menuEdit->addAction(QString());
    m_actionUndo->setShortcut(QKeySequence::Undo);
    m_actionUndo->setEnabled(false);  // Enabled when undo stack is not empty

    m_actionRedo = m_menuEdit->addAction(QString());
    // Startup fallback only. BindingsDialog::defaultBindings() is where these
    // are decided, and applyBindings() overwrites whatever is set here as
    // soon as settings are read; keep the two in step, or a shortcut will
    // appear to work for a moment at startup and then change.
    m_actionRedo->setShortcuts({QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z),
                                QKeySequence(Qt::CTRL | Qt::Key_Y)});
    m_actionRedo->setEnabled(false);  // Enabled when redo stack is not empty

    // Outer (document) undo. While a sketch is open these are re-pointed at
    // the sketch canvas by initSketchConnections(); outside one they act on
    // the feature recipe. Before this, Edit > Undo did nothing at all unless
    // a sketch happened to be open.
    connect(m_actionUndo, &QAction::triggered, this, [this]() {
        if (!m_inSketchMode) { undoDocument(nullptr); }
    });
    connect(m_actionRedo, &QAction::triggered, this, [this]() {
        if (!m_inSketchMode) { redoDocument(nullptr); }
    });

    m_menuEdit->addSeparator();

    m_actionCut = m_menuEdit->addAction(QString());
    m_actionCut->setShortcut(QKeySequence::Cut);
    m_actionCut->setEnabled(false);  // Enabled when selection exists

    m_actionCopy = m_menuEdit->addAction(QString());
    m_actionCopy->setShortcut(QKeySequence::Copy);
    m_actionCopy->setEnabled(false);  // Enabled when selection exists

    m_actionPaste = m_menuEdit->addAction(QString());
    m_actionPaste->setShortcut(QKeySequence::Paste);
    m_actionPaste->setEnabled(false);  // Enabled when clipboard has compatible data

    m_actionDelete = m_menuEdit->addAction(QString());
    m_actionDelete->setShortcut(QKeySequence::Delete);
    m_actionDelete->setEnabled(false);  // Enabled when selection exists

    m_menuEdit->addSeparator();

    m_actionSelectAll = m_menuEdit->addAction(QString());
    m_actionSelectAll->setShortcut(QKeySequence::SelectAll);
    m_actionSelectAll->setEnabled(false);  // Enabled when document has selectable items
}

// View > Workspace submenu (exclusive group)
void MainWindow::createWorkspaceMenu()
{
    m_menuWorkspace = m_menuView->addMenu(QString());
    auto* workspaceGroup = new QActionGroup(this);
    workspaceGroup->setExclusive(true);

    m_actionWorkspaceDesign = m_menuWorkspace->addAction(QString());
    m_actionWorkspaceDesign->setCheckable(true);
    m_actionWorkspaceDesign->setChecked(true);
    workspaceGroup->addAction(m_actionWorkspaceDesign);
    connect(m_actionWorkspaceDesign, &QAction::triggered, this, [this]() {
        emit workspaceChanged(Workspace::Design);
    });

    m_actionWorkspaceRender = m_menuWorkspace->addAction(QString());
    m_actionWorkspaceRender->setCheckable(true);
    workspaceGroup->addAction(m_actionWorkspaceRender);
    connect(m_actionWorkspaceRender, &QAction::triggered, this, [this]() {
        emit workspaceChanged(Workspace::Render);
    });

    m_actionWorkspaceAnimation = m_menuWorkspace->addAction(QString());
    m_actionWorkspaceAnimation->setCheckable(true);
    workspaceGroup->addAction(m_actionWorkspaceAnimation);
    connect(m_actionWorkspaceAnimation, &QAction::triggered, this, [this]() {
        emit workspaceChanged(Workspace::Animation);
    });

    m_actionWorkspaceSimulation = m_menuWorkspace->addAction(QString());
    m_actionWorkspaceSimulation->setCheckable(true);
    workspaceGroup->addAction(m_actionWorkspaceSimulation);
    connect(m_actionWorkspaceSimulation, &QAction::triggered, this, [this]() {
        emit workspaceChanged(Workspace::Simulation);
    });
}

// View > Theme submenu: Light / Dark (exclusive) + Edit...
void MainWindow::createThemeMenu()
{
    m_menuTheme = m_menuView->addMenu(QString());
    {
        auto* themeGroup = new QActionGroup(this);
        themeGroup->setExclusive(true);
        m_actionThemeLight = m_menuTheme->addAction(QString());
        m_actionThemeLight->setCheckable(true);
        themeGroup->addAction(m_actionThemeLight);
        connect(m_actionThemeLight, &QAction::triggered, this, &MainWindow::onThemeLight);
        m_actionThemeDark = m_menuTheme->addAction(QString());
        m_actionThemeDark->setCheckable(true);
        themeGroup->addAction(m_actionThemeDark);
        connect(m_actionThemeDark, &QAction::triggered, this, &MainWindow::onThemeDark);
        m_menuTheme->addSeparator();
        m_actionThemeEdit = m_menuTheme->addAction(QString(), this, &MainWindow::onThemeEdit);
    }
}

// View > Selection Filter: restrict picking to points or curves (Fusion
// filters).
void MainWindow::createSelectionFilterMenu()
{
    QMenu* mf = m_menuView->addMenu(tr("Selection &Filter"));
    auto* g = new QActionGroup(this);
    g->setExclusive(true);
    auto addf = [&](const QString& t, SketchCanvas::SelectFilter f, bool checked) {
        QAction* a = mf->addAction(t);
        a->setCheckable(true);
        a->setChecked(checked);
        g->addAction(a);
        connect(a, &QAction::triggered, this, [this, f]() {
            if (SketchCanvas* c = activeSketchCanvas()) c->setSelectFilter(f);
        });
    };
    addf(tr("All"),         SketchCanvas::SelectFilter::All, true);
    addf(tr("Points only"), SketchCanvas::SelectFilter::PointsOnly, false);
    addf(tr("Curves only"), SketchCanvas::SelectFilter::CurvesOnly, false);
}

// View menu (inserted between Edit and Help)
void MainWindow::createViewMenu()
{
    m_menuView = new QMenu(this);
    menuBar()->insertMenu(m_menuHelp->menuAction(), m_menuView);

    m_actionToggleTerminal = m_menuView->addAction(QString());
    m_actionToggleTerminal->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_QuoteLeft));
    m_actionToggleTerminal->setCheckable(true);
    m_actionToggleTerminal->setChecked(false);

    m_actionToggleFeatureTree = m_menuView->addAction(QString());
    m_actionToggleFeatureTree->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
    m_actionToggleFeatureTree->setCheckable(true);
    m_actionToggleFeatureTree->setChecked(true);

    m_actionToggleProperties = m_menuView->addAction(QString());
    m_actionToggleProperties->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_P));
    m_actionToggleProperties->setCheckable(true);
    m_actionToggleProperties->setChecked(true);

    m_actionToggleToolbar = m_menuView->addAction(QString());
    m_actionToggleToolbar->setCheckable(true);
    m_actionToggleToolbar->setChecked(true);

    m_actionToggleChangelog = m_menuView->addAction(QString());
    m_actionToggleChangelog->setCheckable(true);
    m_actionToggleChangelog->setChecked(false);

    // Draw-then-constrain: place roughly, then constrain, rather than
    // snapping and typing dimensions during placement.
    m_actionDrawThenConstrain = m_menuView->addAction(QString());
    m_actionDrawThenConstrain->setCheckable(true);
    m_actionDrawThenConstrain->setChecked(false);
    connect(m_actionDrawThenConstrain, &QAction::toggled, this,
            [this](bool on) {
        SketchCanvas* canvas = activeSketchCanvas();
        if (!canvas) return;
        const auto mode = on
            ? SketchCanvas::InteractionMode::DrawThenConstrain
            : SketchCanvas::InteractionMode::PlacementFirst;
        if (!canvas->setInteractionMode(mode)) {
            // Refused mid-entity. Put the tick back rather than leaving the
            // menu claiming a mode the canvas is not in.
            QSignalBlocker block(m_actionDrawThenConstrain);
            m_actionDrawThenConstrain->setChecked(!on);
            statusBar()->showMessage(
                tr("Finish or cancel the current entity before changing "
                   "how clicks are interpreted."), 4000);
        }
    });

    // Fusion's Sketch Palette equivalent of "Show Points".
    m_actionShowUnconstrained = m_menuView->addAction(QString());
    m_actionShowUnconstrained->setCheckable(true);
    m_actionShowUnconstrained->setChecked(true);
    connect(m_actionShowUnconstrained, &QAction::toggled, this,
            [this](bool on) {
        if (SketchCanvas* canvas = activeSketchCanvas()) {
            canvas->setShowUnconstrainedPoints(on);
        }
    });

    // Fusion's Sketch Palette equivalent of "Show Constraints". Hides the
    // geometric glyph chips only; dimensional labels have their own state.
    m_actionShowConstraints = m_menuView->addAction(QString());
    m_actionShowConstraints->setCheckable(true);
    m_actionShowConstraints->setChecked(true);
    connect(m_actionShowConstraints, &QAction::toggled, this,
            [this](bool on) {
        if (SketchCanvas* canvas = activeSketchCanvas()) {
            canvas->setShowConstraints(on);
        }
    });

    // Fusion's Sketch Palette equivalent of "Show Dimensions". The mirror of
    // Show Constraints: hides the dimensional labels, leaves the glyphs.
    m_actionShowDimensions = m_menuView->addAction(QString());
    m_actionShowDimensions->setCheckable(true);
    m_actionShowDimensions->setChecked(true);
    connect(m_actionShowDimensions, &QAction::toggled, this,
            [this](bool on) {
        if (SketchCanvas* canvas = activeSketchCanvas()) {
            canvas->setShowDimensions(on);
        }
    });

    // Fusion's Sketch Palette equivalent of "Show Profile": blue shading of
    // closed profiles. Off by default; the canvas function had no caller.
    m_actionShowProfiles = m_menuView->addAction(QString());
    m_actionShowProfiles->setCheckable(true);
    m_actionShowProfiles->setChecked(false);
    connect(m_actionShowProfiles, &QAction::toggled, this,
            [this](bool on) {
        if (SketchCanvas* canvas = activeSketchCanvas()) canvas->setShowProfiles(on);
    });

    // Frame the sketch in the viewport. Mirrors Fusion's "Fit"; the canvas
    // function existed but nothing invoked it.
    m_actionZoomToFit = m_menuView->addAction(QString());
    connect(m_actionZoomToFit, &QAction::triggered, this, [this]() {
        if (SketchCanvas* canvas = activeSketchCanvas()) canvas->zoomToFit();
    });

    m_menuView->addSeparator();

    createWorkspaceMenu();

    m_menuView->addSeparator();

    m_actionResetView = m_menuView->addAction(QString());
    m_actionResetView->setShortcut(QKeySequence(Qt::Key_Home));
    // Connected in FullModeWindow to viewport->resetCamera()

    // Fusion's "Look At": orient the 3D camera square onto the active sketch
    // plane. Connected in FullModeWindow (the 3D viewport lives there).
    m_actionLookAt = m_menuView->addAction(QString());

    // Fusion's "Slice": section the 3D model at the active sketch plane so its
    // interior is visible while sketching. A clip plane, toggled on and off.
    m_actionSlice = m_menuView->addAction(QString());
    m_actionSlice->setCheckable(true);
    m_actionSlice->setChecked(false);

    m_actionRotateLeft = m_menuView->addAction(QString());

    m_actionRotateRight = m_menuView->addAction(QString());

    m_menuView->addSeparator();

    m_actionShowGrid = m_menuView->addAction(QString());
    m_actionShowGrid->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_G));
    m_actionShowGrid->setCheckable(true);
    m_actionShowGrid->setChecked(true);  // On by default

    m_actionSnapToGrid = m_menuView->addAction(QString());
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

    m_menuView->addSeparator();

    m_actionZUp = m_menuView->addAction(QString());
    m_actionZUp->setCheckable(true);
    m_actionZUp->setChecked(true);  // Z-up is the default
    // Connected in FullModeWindow to handle coordinate system change

    m_actionOrbitSelected = m_menuView->addAction(QString());
    m_actionOrbitSelected->setCheckable(true);
    m_actionOrbitSelected->setChecked(false);  // Off by default
    // Connected in FullModeWindow to viewport

    m_menuView->addSeparator();

    createThemeMenu();

    createSelectionFilterMenu();

    m_menuView->addSeparator();

    // Language submenu. Built last so it sits directly above Preferences,
    // which is where a user looks for it.
    m_menuLanguage = m_menuView->addMenu(QString());
    createLanguageMenu();

    m_actionPreferences = m_menuView->addAction(QString(), this, &MainWindow::onEditPreferences);
    m_actionPreferences->setShortcut(QKeySequence::Preferences);
}

    // Sketch drawing tools also live in a menu, not only the toolbar, so they
    // are discoverable by browsing. Bezier (the pen) and Spline (Catmull-Rom
    // fit points) are the two spline variants; the rest mirror the toolbar.
void MainWindow::createSketchMenu()
{
    QMenu* mSketch = new QMenu(this);
    mSketch->setTitle(tr("S&ketch"));
    menuBar()->insertMenu(m_menuHelp->menuAction(), mSketch);
    auto addTool = [&](const QString& text, SketchTool t, CreationMode m) {
        QAction* a = mSketch->addAction(text);
        connect(a, &QAction::triggered, this, [this, t, m]() {
            onSketchToolSelected(t);                 // activate on canvas + hint
            if (m_sketchCanvas) m_sketchCanvas->setCreationMode(m);
            if (m_sketchToolbar) m_sketchToolbar->setActiveTool(t, m);  // sync (no emit)
        });
    };
    addTool(tr("&Line"),        SketchTool::Line,      CreationMode::Default);
    addTool(tr("&Rectangle"),   SketchTool::Rectangle, CreationMode::Default);
    addTool(tr("&Circle"),      SketchTool::Circle,    CreationMode::Default);
    addTool(tr("&Arc"),         SketchTool::Arc,       CreationMode::Default);
    addTool(tr("&Point"),       SketchTool::Point,     CreationMode::Default);
    mSketch->addSeparator();
    addTool(tr("&Cubic Bezier"),        SketchTool::Spline, CreationMode::SplineControlPoints);
    addTool(tr("&Catmull-Rom Spline"),  SketchTool::Spline, CreationMode::SplineFitPoints);
    addTool(tr("&Rational Bezier"),     SketchTool::Spline, CreationMode::SplineRational);
    mSketch->addSeparator();
    QAction* combA = mSketch->addAction(tr("Curvature Comb"));
    combA->setCheckable(true);
    connect(combA, &QAction::toggled, this, [this](bool on) {
        if (m_sketchCanvas) m_sketchCanvas->setCurvatureCombVisible(on);
    });

    // Finish Sketch: a reliable, always-findable way to leave the sketch
    // (the action bar's button lives in a properties surface that is not
    // always the visible one). Enabled only while a sketch is open.
    mSketch->addSeparator();
    m_finishSketchAction = mSketch->addAction(tr("Finish Sketch"));
    m_finishSketchAction->setEnabled(false);
    connect(m_finishSketchAction, &QAction::triggered,
            this, &MainWindow::finishSketchInteractive);
}

    // ===== Constraints menu: apply a specific constraint to the selection =====
    // Fusion-style per-type constraints (the select-first flow): select points
    // or entities, then pick the constraint. Each applies to the current
    // selection via the canvas; insufficient selections report what they need.
void MainWindow::createConstraintsMenu()
{
    QMenu* mCon = new QMenu(this);
    mCon->setTitle(tr("C&onstraints"));
    menuBar()->insertMenu(m_menuHelp->menuAction(), mCon);
    auto add = [&](const QString& text, sketch::ConstraintType t) {
        QAction* a = mCon->addAction(text);
        connect(a, &QAction::triggered, this, [this, t]() {
            onConstraintMenu(static_cast<int>(t));
        });
    };
    add(tr("Coincident"),    sketch::ConstraintType::Coincident);
    add(tr("Horizontal"),    sketch::ConstraintType::Horizontal);
    add(tr("Vertical"),      sketch::ConstraintType::Vertical);
    add(tr("Parallel"),      sketch::ConstraintType::Parallel);
    add(tr("Perpendicular"), sketch::ConstraintType::Perpendicular);
    add(tr("Tangent"),       sketch::ConstraintType::Tangent);
    add(tr("Curvature (G2)"), sketch::ConstraintType::Curvature);
    add(tr("Equal"),         sketch::ConstraintType::Equal);
    add(tr("Midpoint"),      sketch::ConstraintType::Midpoint);
    add(tr("Concentric"),    sketch::ConstraintType::Concentric);
    add(tr("Collinear"),     sketch::ConstraintType::Collinear);
    add(tr("Point on Spline"), sketch::ConstraintType::PointOnSpline);
    add(tr("Tangent Angle"),   sketch::ConstraintType::TangentAngle);
    {   // Angle dimension between two selected lines (dimensional, not the
        // geometric-constraint path; refuses parallel lines).
        QAction* angleA = mCon->addAction(tr("Angle (2 lines)"));
        connect(angleA, &QAction::triggered, this, [this]() {
            if (SketchCanvas* c = activeSketchCanvas()) c->dimensionSelectedLinesAngle();
        });
    }
    add(tr("Symmetric"),     sketch::ConstraintType::Symmetric);
    mCon->addSeparator();
    // Fix applies to the selection directly (no tool-first mode).
    QAction* fixA = mCon->addAction(tr("Fix / Unfix"));
    connect(fixA, &QAction::triggered, this, [this]() {
        if (SketchCanvas* c = activeSketchCanvas()) c->applyFixConstraint();
    });
    mCon->addSeparator();
    QAction* autoA = mCon->addAction(tr("Auto Constrain"));
    connect(autoA, &QAction::triggered, this, [this]() {
        if (SketchCanvas* c = activeSketchCanvas()) c->autoConstrainSketch();
    });
}

void MainWindow::createMenus()
{
    createFileMenu();
    createEditMenu();

    // Construct menu
    m_menuConstruct = menuBar()->addMenu(QString());

    m_actionNewConstructionPlane = m_menuConstruct->addAction(QString());
    // Connected in FullModeWindow to open dialog

    // Help menu
    m_menuHelp = menuBar()->addMenu(QString());

    m_actionAbout = m_menuHelp->addAction(QString(),
        this, &MainWindow::onHelpAbout);

    createViewMenu();
    createSketchMenu();
    createConstraintsMenu();

    // Reflect the current theme in the Light/Dark checks, and restore a saved
    // choice, unless the launch set the theme explicitly (--theme / env),
    // which wins.
    {
        const bool fromFlag = qApp && qApp->property("hobbycad_theme_from_flag").toBool();
        const QVariant saved = QSettings().value(QStringLiteral("theme/dark"));
        bool dark = qApp && qApp->property("hobbycad_dark_theme").toBool();
        if (!fromFlag && saved.isValid()) {
            dark = saved.toBool();
            applyThemeChoice(dark);
        }
        if (m_actionThemeLight) m_actionThemeLight->setChecked(!dark);
        if (m_actionThemeDark)  m_actionThemeDark->setChecked(dark);
    }

    // Every string above is set here, and only here. Setting text at
    // construction as well would mean adding each new one in two places, and
    // the copy that gets missed is invisible until somebody switches
    // language.
    retranslate();
}

// ---- Language menu --------------------------------------------------

void MainWindow::createLanguageMenu()
{
    m_languageGroup = new QActionGroup(this);
    m_languageGroup->setExclusive(true);

    const QSettings settings;
    const QString chosen =
        settings.value(QStringLiteral("preferences/language")).toString();

    // "System default" first, then one entry per catalog that exists. The
    // catalogs are discovered rather than listed here so the menu cannot
    // offer a language the program would then fail to load.
    m_actionLanguageSystem = m_menuLanguage->addAction(QString());
    m_actionLanguageSystem->setCheckable(true);
    m_actionLanguageSystem->setChecked(chosen.isEmpty());
    m_actionLanguageSystem->setData(QString());
    m_languageGroup->addAction(m_actionLanguageSystem);

    const QStringList locales = translations::available();
    if (!locales.isEmpty()) {
        m_menuLanguage->addSeparator();
    }
    for (const QString& locale : locales) {
        QAction* action = m_menuLanguage->addAction(QString());
        action->setCheckable(true);
        action->setChecked(locale == chosen);
        // The locale rides on the action rather than being parsed back out of
        // its text, which changes with the language. Named "languageCode" and
        // not "locale" deliberately: QWidget already has a "locale" property,
        // of type QLocale, and setProperty() on that name would write the
        // widget's own property and read back as empty.
        action->setData(locale);
        m_languageGroup->addAction(action);
        m_languageActions.append(action);
    }

    connect(m_languageGroup, &QActionGroup::triggered,
            this, &MainWindow::onLanguageSelected);
}

void MainWindow::onLanguageSelected(QAction* action)
{
    const QString locale = action->data().toString();

    QSettings settings;
    settings.setValue(QStringLiteral("preferences/language"), locale);

    // switchTo() walks every widget and calls retranslate() before it
    // returns, so by the time the setting is written the interface already
    // matches it.
    translations::switchTo(*qApp, locale);
}

// ---- Retranslation --------------------------------------------------

void MainWindow::retranslate()
{
    m_menuFile->setTitle(tr("&File"));
    m_actionNew->setText(tr("&New"));
    m_actionOpen->setText(tr("&Open..."));
    m_actionSave->setText(tr("&Save"));
    m_actionSaveAs->setText(tr("Save &As..."));
    m_actionClose->setText(tr("&Close"));
    m_menuImport->setTitle(tr("&Import"));
    m_actionImportStep->setText(tr("STEP File..."));
    m_actionImportStep->setToolTip(tr("Import geometry from STEP file"));
    m_actionImportDXF->setText(tr("DXF File (Sketch)..."));
    m_actionImportDXF->setToolTip(tr("Import DXF geometry into the active sketch"));
    m_menuExport->setTitle(tr("&Export"));
    m_actionExportStep->setText(tr("STEP File..."));
    m_actionExportStep->setToolTip(tr("Export geometry to STEP file"));
    m_actionExportStl->setText(tr("STL File..."));
    m_actionExportStl->setToolTip(tr("Export geometry to STL file for 3D printing"));
    m_actionExportDXF->setText(tr("DXF File (Sketch)..."));
    m_actionExportDXF->setToolTip(tr("Export sketch to DXF file"));
    m_actionExportSVG->setText(tr("SVG File (Sketch)..."));
    m_actionExportSVG->setToolTip(tr("Export sketch to SVG file"));
    m_actionQuit->setText(tr("&Quit"));
    m_menuEdit->setTitle(tr("&Edit"));
    m_actionUndo->setText(tr("&Undo"));
    m_actionRedo->setText(tr("&Redo"));
    m_actionCut->setText(tr("Cu&t"));
    m_actionCopy->setText(tr("&Copy"));
    m_actionPaste->setText(tr("&Paste"));
    m_actionDelete->setText(tr("&Delete"));
    m_actionSelectAll->setText(tr("Select &All"));
    m_menuConstruct->setTitle(tr("&Construct"));
    m_actionNewConstructionPlane->setText(tr("New Construction &Plane..."));
    m_actionNewConstructionPlane->setToolTip(tr("Create a new construction plane"));
    m_menuHelp->setTitle(tr("&Help"));
    m_actionAbout->setText(tr("&About HobbyCAD..."));
    m_menuView->setTitle(tr("&View"));
    m_actionToggleTerminal->setText(tr("&Terminal"));
    m_actionToggleFeatureTree->setText(tr("P&roject"));
    m_actionToggleProperties->setText(tr("&Properties"));
    m_actionToggleToolbar->setText(tr("Tool&bar"));
    m_actionToggleChangelog->setText(tr("Change &History"));
    m_actionDrawThenConstrain->setText(tr("&Draw, then Constrain"));
    m_actionShowUnconstrained->setText(tr("Show &Unconstrained Points"));
    m_actionShowUnconstrained->setToolTip(
        tr("Mark endpoints that are not constrained to other geometry"));
    m_actionShowConstraints->setText(tr("Show &Constraints"));
    m_actionShowConstraints->setToolTip(
        tr("Show the constraint symbols on the canvas; dimensions are "
           "unaffected"));
    m_actionShowDimensions->setText(tr("Show &Dimensions"));
    m_actionShowDimensions->setToolTip(
        tr("Show the dimensional labels on the canvas; constraint symbols "
           "are unaffected"));
    m_actionShowProfiles->setText(tr("Show &Profiles"));
    m_actionShowProfiles->setToolTip(tr("Shade closed sketch profiles in blue"));
    m_actionZoomToFit->setText(tr("&Fit Sketch to View"));
    m_actionZoomToFit->setToolTip(tr("Zoom and pan so the whole sketch is visible"));
    m_actionDrawThenConstrain->setToolTip(
        tr("Place geometry roughly and fix it with constraints afterwards, "
           "instead of snapping and typing dimensions as you place it"));
    m_menuWorkspace->setTitle(tr("&Workspace"));
    m_menuLanguage->setTitle(tr("L&anguage"));
    if (m_menuTheme) m_menuTheme->setTitle(tr("&Theme"));
    if (m_actionThemeLight) m_actionThemeLight->setText(tr("&Light"));
    if (m_actionThemeDark)  m_actionThemeDark->setText(tr("&Dark"));
    if (m_actionThemeEdit)  m_actionThemeEdit->setText(tr("&Edit..."));
    m_actionWorkspaceDesign->setText(tr("&Design"));
    m_actionWorkspaceRender->setText(tr("&Render"));
    m_actionWorkspaceAnimation->setText(tr("&Animation"));
    m_actionWorkspaceSimulation->setText(tr("&Simulation"));
    m_actionResetView->setText(tr("Reset &View"));
    m_actionLookAt->setText(tr("Look &At Sketch Plane"));
    m_actionLookAt->setToolTip(tr("Orient the camera square onto the active sketch plane"));
    m_actionSlice->setText(tr("&Slice at Sketch Plane"));
    m_actionSlice->setToolTip(tr("Section the model at the active sketch plane to see inside"));
    m_actionRotateLeft->setText(tr("Rotate &Left 90°"));
    m_actionRotateRight->setText(tr("Rotate Ri&ght 90°"));
    m_actionShowGrid->setText(tr("Show Gri&d"));
    m_actionSnapToGrid->setText(tr("&Snap to Grid"));
    m_actionZUp->setText(tr("&Z-Up Orientation"));
    m_actionOrbitSelected->setText(tr("&Orbit Selected Object"));
    m_actionPreferences->setText(tr("Pre&ferences..."));

    // Rebuilt from current state rather than restored, because what "system
    // default" resolves to depends on which catalogs are present, and the
    // names are themselves translated.
    const QString systemName = translations::systemLanguageName();
    m_actionLanguageSystem->setText(
        systemName.isEmpty()
            ? tr("System Default (English)")
            : tr("System Default (%1)").arg(systemName));

    for (QAction* action : m_languageActions) {
        const QString locale = action->data().toString();
        const QString name = translations::languageName(locale);
        // Machine-translated catalogs are marked in the menu itself. A user
        // who picks one should know before they see the result, not after.
        action->setText(translations::isMachineTranslated(locale)
                            ? tr("%1 (machine translation)").arg(name)
                            : name);
    }

    updateTitle();
}

// ---- Status bar -----------------------------------------------------

void MainWindow::updateSketchStateLabel(sketch::SketchState state, int dof)
{
    if (!m_sketchStateLabel) {
        return;
    }

    QString text;
    QString color;
    QString tip;

    switch (state) {
    case sketch::SketchState::Empty:
        // Nothing drawn yet: saying "fully constrained" would be technically
        // true and completely unhelpful.
        text = tr("Empty sketch");
        tip  = tr("No geometry yet.");
        break;
    case sketch::SketchState::UnderConstrained:
        // NOT tr("%n degree(s) ..."): with no translation catalog loaded,
        // Qt substitutes %n and leaves the literal "(s)" on screen. Only a
        // translated build would ever read correctly.
        text = (dof == 1) ? tr("1 degree of freedom")
                          : tr("%1 degrees of freedom").arg(dof);
        tip  = tr("The sketch can still move. Add constraints or dimensions "
                  "to pin it down.");
        break;
    case sketch::SketchState::FullyConstrained:
        text  = tr("Fully constrained");
        color = QStringLiteral("#009650");
        tip   = tr("Every degree of freedom is pinned; the sketch has exactly "
                   "one solution.");
        break;
    case sketch::SketchState::OverConstrained:
        text  = tr("Over-constrained");
        color = QStringLiteral("#b06000");
        tip   = tr("The sketch still solves, but some constraints are "
                   "redundant; they repeat what others already say.");
        break;
    case sketch::SketchState::Inconsistent:
        text  = tr("Conflicting constraints");
        color = QStringLiteral("#c00000");
        tip   = tr("No solution exists: two or more constraints contradict "
                   "each other. The conflicting ones are drawn in red.");
        break;
    case sketch::SketchState::Failed:
        text  = tr("Solver failed");
        color = QStringLiteral("#c00000");
        tip   = tr("The solver could not produce an answer. The sketch may "
                   "still be satisfiable; this is not the same as a "
                   "conflict.");
        break;
    case sketch::SketchState::Unknown:
        // No solver, or nothing solved yet. Say nothing rather than guess.
        m_sketchStateLabel->setVisible(false);
        return;
    }

    m_sketchStateLabel->setText(text);
    m_sketchStateLabel->setToolTip(tip);
    m_sketchStateLabel->setStyleSheet(
        color.isEmpty() ? QString()
                        : QStringLiteral("color: %1; font-weight: bold;").arg(color));
    m_sketchStateLabel->setVisible(true);
}

void MainWindow::createStatusBar()
{
    statusBar()->setObjectName(QStringLiteral("StatusBar"));

    m_statusLabel = new QLabel(tr("Ready"));
    m_statusLabel->setObjectName(QStringLiteral("StatusLabel"));
    statusBar()->addWidget(m_statusLabel, 1);

    // Constraint state. Permanent (right-hand) side so a transient tool hint
    // on the left cannot overwrite it. Hidden outside sketch mode, where a
    // sketch's constraint state means nothing.
    m_sketchStateLabel = new QLabel();
    m_sketchStateLabel->setObjectName(QStringLiteral("SketchStateLabel"));
    m_sketchStateLabel->setVisible(false);
    statusBar()->addPermanentWidget(m_sketchStateLabel);

    m_glModeLabel = new QLabel();
    m_glModeLabel->setObjectName(QStringLiteral("GlModeLabel"));
    statusBar()->addPermanentWidget(m_glModeLabel);

    // Must ask the SAME question main() asked when it chose the mode.
    // This used to call meetsMinimum(), the GL-version proxy, while main()
    // decided with canRunViewport(), which prefers OCCT's own answer. The
    // two disagree exactly when the rework mattered: OCCT can bring up a
    // viewer on a compatibility context reporting less than 3.3 (label
    // claimed "Reduced Mode" while the 3D viewport was running), and a
    // driver can report 4.6 while OCCT still fails to initialize (label
    // proudly announced OpenGL 4.6 with no viewport on screen).
    if (m_glInfo.canRunViewport()) {
        m_glModeLabel->setText(
            tr("OpenGL %1.%2: %3")
                .arg(m_glInfo.majorVersion)
                .arg(m_glInfo.minorVersion)
                .arg(QString::fromStdString(m_glInfo.renderer)));
        m_glModeLabel->setToolTip(QString());
    } else {
        m_glModeLabel->setText(
            QStringLiteral("\u26A0 ") + tr("Reduced Mode"));

        // Say what actually refused. Citing "OpenGL 3.3 required" when OCCT
        // was the one that declined sends the user off to check a driver
        // version that was never the problem.
        QString why;
        if (m_glInfo.occtViewerProbed) {
            why = tr("The 3D viewport is disabled because the OCCT viewer "
                     "could not start.");
            if (!m_glInfo.errorMessage.empty()) {
                why += QLatin1Char('\n');
                why += QString::fromStdString(m_glInfo.errorMessage);
            }
        } else {
            why = tr("The 3D viewport is disabled because OpenGL 3.3+ "
                     "was not detected.");
        }
        why += QLatin1Char('\n');
        why += tr("File operations and geometry operations still work "
                  "normally.");
        m_glModeLabel->setToolTip(why);
    }
}

// ---- Dock panels ----------------------------------------------------

// Project dock: the Objects and Files tabs, their handlers, and the View >
// Project toggle.
void MainWindow::createProjectDock()
{
    // Project panel with File and Objects tabs
    m_featureTreeDock = new QDockWidget(tr("Project"), this);
    m_featureTreeDock->setObjectName(QStringLiteral("ProjectDock"));
    m_featureTreeDock->setAllowedAreas(
        Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    auto* projectTabs = new QTabWidget();
    projectTabs->setObjectName(QStringLiteral("ProjectTabs"));

    // The file browser is built here but added AFTER the objects tree, so
    // the tab order runs model-first, files-second: the semantic view of the
    // design is what a user wants on opening a project, and the filesystem is
    // the secondary, more technical surface.
    m_projectBrowser = new ProjectBrowserWidget();

    // Objects tab - the semantic view of the design.
    //
    // The tree's CONTENT is decided by hobbycad::buildBrowserTree() in the
    // library, not here: what nodes exist is a question about the document,
    // and a second front end needs the same answer. This block only renders
    // it and keeps m_objectsTree pointing at the underlying widget so the
    // existing handlers and addXToTree() helpers keep working unchanged.
    m_objectsBrowser = new ObjectsBrowserWidget();
    m_objectsTree = m_objectsBrowser->treeWidget();

    // Folders are created on demand and disappear with their last leaf,
    // so nothing holds a pointer to one any more.
    m_objectsBrowser->setTree(hobbycad::buildBrowserTree(browserInput()));

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


    // Renames. Validated here rather than in the widget: whether a name is
    // acceptable depends on what else the document contains, which the
    // browser deliberately does not know.
    connect(m_objectsBrowser, &ObjectsBrowserWidget::nodeRenamed, this,
            [this](hobbycad::NodeType type, int id, const QString& name) {
        QTreeWidgetItem* item = m_objectsBrowser->itemFor(type, id);
        if (!item) {
            return;
        }

        // Same rule the CLI applies, from the library, so a name accepted
        // in one place cannot be refused in the other.
        const QString trimmed = name.trimmed();
        std::string why;
        if (!hobbycad::isValidObjectName(trimmed.toStdString(), &why)) {
            m_objectsBrowser->rejectRename(item, QString::fromStdString(why));
            return;
        }

        // Duplicates are refused among nodes of the SAME kind. Two sketches
        // called "Profile" are ambiguous everywhere they are referred to; a
        // sketch and a body sharing a name are not.
        for (QTreeWidgetItemIterator it(m_objectsTree); *it; ++it) {
            QTreeWidgetItem* other = *it;
            if (other == item) {
                continue;
            }
            const auto otherType = static_cast<hobbycad::NodeType>(
                other->data(0, Qt::UserRole + 2).toInt());
            if (otherType == type && other->text(0) == trimmed) {
                m_objectsBrowser->rejectRename(
                    item, tr("Another %1 is already called '%2'.")
                              .arg(QString::fromLatin1(
                                       hobbycad::nodeTypeName(type)).toLower(),
                                   trimmed));
                return;
            }
        }

        if (!handleNodeRenamed(type, id, trimmed)) {
            m_objectsBrowser->rejectRename(
                item, tr("This item cannot be renamed."));
            return;
        }
        m_objectsBrowser->acceptRename(item);
    });

    // Context-menu actions. The menu itself is decided by the node's TYPE
    // (hobbycad::NodeTypeInfo::actions), so this only routes stable ids;
    // it never has to agree with the widget about what a menu contains.
    connect(m_objectsBrowser, &ObjectsBrowserWidget::actionTriggered, this,
            [this](hobbycad::NodeType type, int id, const QString& actionId) {
        onObjectAction(type, id, actionId);
    });

    // Relink an external project reference whose path no longer resolves.
    connect(m_objectsBrowser, &ObjectsBrowserWidget::relinkRequested, this,
            [this](hobbycad::NodeType type, int id, const QString& currentPath) {
        onRelinkReference(type, id, currentPath);
    });

    // Body visibility. Guarded by m_suppressObjectsTreeSignal because
    // rebuilding the folder sets check states programmatically, and each of
    // those emits itemChanged; without the guard, repopulating the tree
    // would toggle every body in the viewport.
    connect(m_objectsTree, &QTreeWidget::itemChanged, this,
            [this](QTreeWidgetItem* item, int column) {
        if (column != 0 || m_suppressObjectsTreeSignal || !item) {
            return;
        }
        if (item->data(0, Qt::UserRole).toString() != QStringLiteral("body")) {
            return;
        }
        setBodyVisible(item->data(0, Qt::UserRole + 1).toInt(),
                       item->checkState(0) == Qt::Checked);
    });

    // The WIDGET, not its inner tree: adding m_objectsTree directly would
    // reparent it out of ObjectsBrowserWidget's layout and leave the widget
    // holding a dangling child.
    projectTabs->addTab(m_objectsBrowser, tr("Objects"));
    projectTabs->addTab(m_projectBrowser, tr("Files"));

    // Select Objects tab by default
    // Objects is the default for a new user, but the last tab used is
    // restored: the file browser is genuinely useful during development, and
    // reordering the tabs should not cost someone who lives in it a click on
    // every launch.
    m_projectTabs = projectTabs;
    const QSettings tabSettings;
    projectTabs->setCurrentIndex(
        tabSettings.value(QStringLiteral("window/projectTab"), 0).toInt());

    // F2 to edit selected item in objects tree
    // F2 renames the object under the cursor. Column 0 is the label.
    bindEditInPlace(m_objectsTree, 0);

    // Handle item selection in objects tree
    connect(m_objectsTree, &QTreeWidget::currentItemChanged,
            this, [this](QTreeWidgetItem* current, QTreeWidgetItem*) {
        if (!current) { onPlaneDeselected(); return; }

        QString itemType = current->data(0, Qt::UserRole).toString();

        if (itemType == QStringLiteral("construction_plane")) {
            int planeId = current->data(0, Qt::UserRole + 1).toInt();
            m_planePageShown = true;
            emit constructionPlaneSelected(planeId);
        } else if (itemType == QStringLiteral("sketch")) {
            // The node carries the sketch's id, its feature id. Full mode used
            // to read it as a position in its own list, which picked the wrong
            // sketch as soon as one had been deleted.
            const int sketchId = current->data(0, Qt::UserRole + 1).toInt();
            emit sketchSelectedInTree(sketchId);
            if (m_timeline) {
                const int t = m_timeline->indexOfFeatureId(sketchId);
                if (t >= 0) {
                    m_timeline->setSelectedIndex(t);
                    showFeatureProperties(t);
                }
            }
        } else if (itemType == QStringLiteral("origin_plane")) {
            showOriginPlaneProperties(static_cast<SketchPlane>(current->data(0, Qt::UserRole + 1).toInt()));
        } else {
            // Document Settings, Parameters, an axis, a body: not a plane.
            onPlaneDeselected();
        }
    });
    // A click on empty tree space clears the selection, so "clicking away"
    // from a plane really deselects it instead of leaving it current.
    m_objectsTree->viewport()->installEventFilter(this);

    m_featureTreeDock->setWidget(projectTabs);

    addDockWidget(Qt::LeftDockWidgetArea, m_featureTreeDock);

    // Connect View > Project toggle to dock visibility
    connect(m_actionToggleFeatureTree, &QAction::toggled,
            m_featureTreeDock, &QDockWidget::setVisible);
    connect(m_featureTreeDock, &QDockWidget::visibilityChanged,
            m_actionToggleFeatureTree, &QAction::setChecked);

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

// Properties dock: the properties tree, plane transform panel and (during a
// sketch) the sketch properties widget in one vertical scroll area, plus the
// View > Properties toggle for the whole right-side panel.
void MainWindow::createPropertiesDock()
{
    // Properties panel (shows properties of selected timeline/tree item)
    m_propertiesDock = new QDockWidget(tr("Properties"), this);
    m_propertiesDock->setObjectName(QStringLiteral("PropertiesDock"));
    m_propertiesDock->setAllowedAreas(
        Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_propertiesDock->setMinimumWidth(150);  // Ensure title "Properties" isn't clipped

    // Container: properties tree, plane transform, and (during a sketch) the
    // sketch properties widget, all in one panel, wrapped in a vertical scroll area so
    // tall content scrolls down rather than sideways.
    auto* propsContainer = new QWidget();
    propsContainer->setObjectName(QStringLiteral("PropertiesContainer"));
    // The tree is sized to its content, so the container shows below it.
    // Paint the container in the palette's Base role, the color the tree
    // rows use, so the panel reads as one surface instead of tree rows on
    // a lighter (or darker) window color. Holds for any theme and palette.
    propsContainer->setAutoFillBackground(true);
    propsContainer->setBackgroundRole(QPalette::Base);
    auto* propsLayout = new QVBoxLayout(propsContainer);
    propsLayout->setContentsMargins(0, 0, 0, 0);
    propsLayout->setSpacing(0);

    m_propertiesTree = new QTreeWidget();
    m_propertiesTree->setObjectName(QStringLiteral("PropertiesTree"));
    m_propertiesTree->setColumnCount(2);
    m_propertiesTree->setHeaderLabels({tr("Property"), tr("Value")});
    m_propertiesTree->setRootIsDecorated(true);
    // Taller rows so the bordered editable value cells (Geometry: Start / End /
    // Length) are not cramped.
    m_propertiesTree->setStyleSheet(
        QStringLiteral("QTreeWidget::item { min-height: 26px; }"));
    // The outer scroll area owns scrolling; the tree grows to its content so it
    // does not add a second, nested scrollbar.
    m_propertiesTree->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_propertiesTree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    propsLayout->addWidget(m_propertiesTree);

    m_planeTransformPanel = new PlaneTransformPanel(propsContainer);
    m_planeTransformPanel->setVisible(false);
    propsLayout->addWidget(m_planeTransformPanel);

    // Sketch properties (background, selected entity, bezier, transform) now live
    // in the Properties panel, shown only while a sketch is open. One surface, no
    // separate "Sketch" dock.
    m_sketchPropsWidget = new SketchPropertiesWidget(propsContainer);
    m_sketchPropsWidget->setVisible(false);
    propsLayout->addWidget(m_sketchPropsWidget);
    propsLayout->addStretch(1);

    // Keep the tree sized to its content so the single outer scroll area is the
    // only scroller; recompute on population and expand/collapse (debounced).
    auto scheduleTreeHeight = [this]() {
        if (m_propsTreeHeightPending) return;
        m_propsTreeHeightPending = true;
        QTimer::singleShot(0, this, [this]() {
            m_propsTreeHeightPending = false;
            updatePropertiesTreeHeight();
        });
    };
    connect(m_propertiesTree->model(), &QAbstractItemModel::rowsInserted,
            this, [scheduleTreeHeight](const QModelIndex&, int, int) { scheduleTreeHeight(); });
    connect(m_propertiesTree->model(), &QAbstractItemModel::rowsRemoved,
            this, [scheduleTreeHeight](const QModelIndex&, int, int) { scheduleTreeHeight(); });
    connect(m_propertiesTree->model(), &QAbstractItemModel::modelReset,
            this, [scheduleTreeHeight]() { scheduleTreeHeight(); });
    connect(m_propertiesTree, &QTreeWidget::itemExpanded,
            this, [scheduleTreeHeight](QTreeWidgetItem*) { scheduleTreeHeight(); });
    connect(m_propertiesTree, &QTreeWidget::itemCollapsed,
            this, [scheduleTreeHeight](QTreeWidgetItem*) { scheduleTreeHeight(); });

    auto* propsScroll = new QScrollArea();
    propsScroll->setObjectName(QStringLiteral("PropertiesScroll"));
    propsScroll->setWidgetResizable(true);
    propsScroll->setFrameShape(QFrame::NoFrame);
    propsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    propsScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    propsScroll->setWidget(propsContainer);
    propsScroll->viewport()->setAutoFillBackground(true);
    propsScroll->viewport()->setBackgroundRole(QPalette::Base);
    m_propertiesDock->setWidget(propsScroll);

    // F2 to edit selected item in properties tree (column 1 = value)
    // Column 1 here: in the properties panel the editable thing is the
    // VALUE, not the property name.
    bindEditInPlace(m_propertiesTree, 1);

    // Same outlining as the parameters dialog and the objects browser: the
    // editable VALUE column is always bordered, red when the last edit was
    // refused.
    m_propsOutlineDelegate = new ErrorOutlineDelegate(
        [this](const QModelIndex& index) {
            return m_badPropertyItems.contains(
                m_propertiesTree->itemFromIndex(index));
        },
        m_propertiesTree);
    m_propertiesTree->setItemDelegate(m_propsOutlineDelegate);

    addDockWidget(Qt::RightDockWidgetArea, m_propertiesDock);

    // Connect View > Properties toggle to dock visibility
    // The toggle governs the whole right-side panel, not just the Properties
    // tab: Constraints/Options ride with it (they are sketch-only, so only while
    // a sketch is open). Raise Properties when the panel is shown.
    connect(m_actionToggleProperties, &QAction::toggled, this, [this](bool on) {
        if (m_propertiesDock) m_propertiesDock->setVisible(on);
        if (m_inSketchMode) {
            if (m_constraintsDock) m_constraintsDock->setVisible(on);
            if (m_sketchOptionsDock) m_sketchOptionsDock->setVisible(on);
        }
        if (on && m_propertiesDock) m_propertiesDock->raise();
    });
    // Properties now shares a tab stack with Constraints/Options; switching to
    // one of those makes Properties a background tab and fires
    // visibilityChanged(false). Sync the check state WITHOUT re-emitting
    // toggled; otherwise that re-enters setVisible(false) and removes the
    // Properties tab entirely.
    connect(m_propertiesDock, &QDockWidget::visibilityChanged, this,
            [this](bool visible) {
        // Reflect open-vs-closed, not current-vs-background-tab. Switching to
        // the Constraints/Options tab fires visibilityChanged(false) on
        // Properties even though it is still open behind the tab, so keep the
        // check ON while a tabified sibling is visible. Block signals so this
        // never re-enters setVisible() (which would remove the tab).
        bool open = visible;
        if (!visible)
            for (QDockWidget* d : tabifiedDockWidgets(m_propertiesDock))
                if (d->isVisible()) { open = true; break; }
        QSignalBlocker block(m_actionToggleProperties);
        m_actionToggleProperties->setChecked(open);
    });
}

// Constraints and Options docks: sketch-only tabs beside Properties.
void MainWindow::createSketchSideDocks()
{
    // Constraint explorer: a 2nd tab beside the sketch properties panel.
    // This is the app's first tabbed dock pair; tabifyDockWidget is used
    // nowhere else.
    m_constraintsDock = new QDockWidget(tr("Constraints"), this);
    m_constraintsDock->setObjectName(QStringLiteral("ConstraintsDock"));
    m_constraintsDock->setAllowedAreas(
        Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_constraintExplorer = new ConstraintExplorer(m_constraintsDock);
    m_constraintsDock->setWidget(m_constraintExplorer);
    addDockWidget(Qt::RightDockWidgetArea, m_constraintsDock);
    tabifyDockWidget(m_propertiesDock, m_constraintsDock);
    m_constraintsDock->setVisible(false);

    // Sketch Options ("palette"): display + show toggles for the active sketch.
    m_sketchOptionsDock = new QDockWidget(tr("Options"), this);
    m_sketchOptionsDock->setObjectName(QStringLiteral("SketchOptionsDock"));
    m_sketchOptionsDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_sketchOptionsWidget = new SketchOptionsWidget(m_sketchOptionsDock);
    m_sketchOptionsDock->setWidget(m_sketchOptionsWidget);
    addDockWidget(Qt::RightDockWidgetArea, m_sketchOptionsDock);
    tabifyDockWidget(m_constraintsDock, m_sketchOptionsDock);
    m_sketchOptionsDock->setVisible(false);

    // Properties is the first/active tab of the right-side stack.
    m_propertiesDock->raise();
}

// History (undo changelog) dock, hidden until View > Change History.
void MainWindow::createChangelogDock()
{
    // Changelog / undo history panel
    m_changelogDock = new QDockWidget(tr("History"), this);
    m_changelogDock->setObjectName(QStringLiteral("ChangelogDock"));
    m_changelogDock->setAllowedAreas(
        Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    m_changelogPanel = new hobbycad::ChangelogPanel(m_changelogDock);
    m_changelogDock->setWidget(m_changelogPanel);

    addDockWidget(Qt::RightDockWidgetArea, m_changelogDock);

    // Start hidden (toggled via View > Change History)
    m_changelogDock->setVisible(false);

    // Connect View > Change History toggle to dock visibility
    connect(m_actionToggleChangelog, &QAction::toggled,
            m_changelogDock, &QDockWidget::setVisible);
    connect(m_changelogDock, &QDockWidget::visibilityChanged,
            m_actionToggleChangelog, &QAction::setChecked);
}

// Embedded terminal dock with the CLI panel, hidden until View > Terminal.
void MainWindow::createTerminalDock()
{
    // Embedded terminal panel
    m_terminalDock = new QDockWidget(tr("Terminal"), this);
    m_terminalDock->setObjectName(QStringLiteral("TerminalDock"));
    m_terminalDock->setAllowedAreas(
        Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);

    m_cliPanel = new CliPanel(this);
    // The prompt drives the SAME undo history as the Edit menu.
    if (m_cliPanel->engine()) {
        m_cliPanel->engine()->setUndoHost(this);
        // Same document the windows are showing, not a copy.
        m_cliPanel->engine()->setDocumentHost(this);
    }
    m_cliPanel->setGuiMode(true);  // We're in GUI mode, show warnings for missing viewport
    m_terminalDock->setWidget(m_cliPanel);

    addDockWidget(Qt::BottomDockWidgetArea, m_terminalDock);

    // Start hidden (toggled via View > Terminal or Ctrl+`)
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

    // Handle exit request from the terminal panel: close the app
    connect(m_cliPanel, &CliPanel::exitRequested,
            this, [this]() {
        close();
    });
}

// Cross-sketch projection support: the canvas asks the host (which owns the
// whole Project) to resolve a source entity and to list projectable ones.
void MainWindow::installSketchCanvasResolvers()
{
    // Resolve a projection's source that lives in another sketch: the host owns
    // the whole Project, so it can look the source up by (sketchId, entityId)
    // and hand back the entity plus its plane/offset. updateProjectedEntities
    // uses this to re-derive cross-sketch projections at solve.
    m_sketchCanvas->setProjectionResolver(
        [this](int sketchId, int entityId, SketchEntity& out,
               SketchPlane& plane, double& offset) -> bool {
            for (const auto& sk : m_project.sketches()) {
                if (sk.id != sketchId) continue;
                for (const auto& e : sk.entities) {
                    if (e.id != entityId) continue;
                    out = hobbycad::toGuiEntity(e);
                    plane = sk.plane;
                    offset = sk.planeOffset;
                    return true;
                }
            }
            return false;
        });

    // Projectable geometry in the document's other sketches, for the Project
    // tool's picker (Line/Point/Spline/Polygon/Circle, see makeProjectionChild;
    // Circle projects to an Ellipse, Arc is still deferred).
    m_sketchCanvas->setProjectionSourceLister(
        [this]() -> std::vector<SketchCanvas::ProjectionSource> {
            std::vector<SketchCanvas::ProjectionSource> out;
            for (const auto& sk : m_project.sketches()) {
                for (const auto& e : sk.entities) {
                    QString typeName;
                    switch (e.type) {
                    case sketch::EntityType::Line:    typeName = tr("line"); break;
                    case sketch::EntityType::Point:   typeName = tr("point"); break;
                    case sketch::EntityType::Spline:  typeName = tr("spline"); break;
                    case sketch::EntityType::Polygon: typeName = tr("polygon"); break;
                    case sketch::EntityType::Circle:  typeName = tr("circle"); break;
                    case sketch::EntityType::Arc:     typeName = tr("arc"); break;
                    case sketch::EntityType::Ellipse: typeName = tr("ellipse"); break;
                    default: continue;
                    }
                    SketchCanvas::ProjectionSource src;
                    src.sketchId = sk.id;
                    src.entityId = e.id;
                    src.sketchName = QString::fromStdString(sk.name);
                    src.label = QStringLiteral("%1 / %2 %3")
                        .arg(src.sketchName, typeName, QString::number(e.id));
                    out.push_back(src);
                }
            }
            return out;
        });
}

// The Name and Plane rows that open a sketch's properties in both modes.
void MainWindow::addSketchHeaderRows(QTreeWidget* propsTree, const QString& sketchName, SketchPlane plane)
{
    auto* nameItem = new QTreeWidgetItem(propsTree);
    nameItem->setText(0, tr("Name"));
    nameItem->setText(1, sketchName);
    nameItem->setFlags(nameItem->flags() | Qt::ItemIsEditable);

    auto* planeItem = new QTreeWidgetItem(propsTree);
    planeItem->setText(0, tr("Plane"));
    QStringList planes = {tr("XY"), tr("XZ"), tr("YZ")};
    int planeIdx = static_cast<int>(plane);
    planeItem->setText(1, planes.value(planeIdx));
    planeItem->setData(1, Qt::UserRole, QStringLiteral("dropdown"));
    planeItem->setData(1, Qt::UserRole + 1, planes);
    planeItem->setData(1, Qt::UserRole + 2, planeIdx);
}

void MainWindow::createDockPanels()
{
    createProjectDock();
    createPropertiesDock();
    createSketchSideDocks();
    createChangelogDock();
    createTerminalDock();
}

// ---- Slots ----------------------------------------------------------

void MainWindow::onFileNew()
{
    if (!maybeSave()) return;

    resetForDocumentChange();
    m_document.clear();
    m_document.setModified(false);  // New document starts unmodified
    // Close the project as well: New used to keep its path, so the old
    // sketches came straight back and the next Save overwrote that project.
    m_project.close();
    updateTitle();
    onDocumentLoaded();
    m_statusLabel->setText(tr("New document created"));
}

void MainWindow::onObjectAction(hobbycad::NodeType type,
                                int id,
                                const QString& actionId)
{
    // "Create Sketch" is the one action the base window can complete: the
    // existing path reads the objects tree's current item, so selecting the
    // node the user right-clicked is enough to reuse it unchanged.
    if (actionId == QLatin1String("sketch") && m_objectsBrowser) {
        if (QTreeWidgetItem* item = m_objectsBrowser->itemFor(type, id)) {
            m_objectsTree->setCurrentItem(item);
            onCreateSketchClicked();
            return;
        }
    }

    // A Canvas node IS the sketch's background image, and browser.cpp
    // relabels its "edit" action to "Calibrate...". Without this it fell
    // through to the not-implemented message below, which is why the
    // calibration dialog had no reachable entry point from the tree.
    if (type == hobbycad::NodeType::SketchCanvas &&
        actionId == QLatin1String("edit")) {
        // Calibration applies to the background of the sketch that is
        // CURRENTLY open on the canvas, and nothing here ties the node's id
        // to that sketch. Rather than risk calibrating a different sketch's
        // image, require the sketch to be open and say so plainly.
        if (!m_inSketchMode) {
            statusBar()->showMessage(
                tr("Open the sketch before calibrating its canvas"), 4000);
            return;
        }
        onCalibrateBackground();
        return;
    }

    if (handleObjectAction(type, id, actionId)) {
        return;
    }

    // Say so rather than doing nothing: a menu entry that silently no-ops
    // is indistinguishable from a broken one.
    QString what;
    if (actionId == QLatin1String("delete"))           what = tr("Delete");
    else if (actionId == QLatin1String("export"))      what = tr("Export");
    else if (actionId == QLatin1String("embed"))       what = tr("Embed a copy");
    else if (actionId == QLatin1String("externalize")) what = tr("Link to a project file");
    else if (actionId == QLatin1String("edit"))        what = tr("Editing this");
    else                                               what = actionId;

    statusBar()->showMessage(tr("%1 is not implemented yet").arg(what), 4000);
}

void MainWindow::onRelinkReference(hobbycad::NodeType type,
                                   int id,
                                   const QString& currentPath)
{
    Q_UNUSED(type);

    // A plain file browser, as File > Open uses; Aaron asked for "a file
    // browser much like fileOpen" rather than a bespoke resolver dialog.
    // Starting it in the directory the reference USED to live in is the
    // whole point: a link most often breaks because the project moved with
    // its neighbors, so the replacement is usually a sibling.
    QString startDir;
    if (!currentPath.isEmpty()) {
        const QFileInfo info(currentPath);
        QDir dir = info.dir();
        // Walk up to the first directory that still exists; the immediate
        // parent is frequently gone too.
        while (!dir.exists() && dir.cdUp()) {
        }
        if (dir.exists()) {
            startDir = dir.absolutePath();
        }
    }

    const QString picked = QFileDialog::getOpenFileName(
        this,
        currentPath.isEmpty()
            ? tr("Link to Project")
            : tr("Relink '%1'").arg(QFileInfo(currentPath).fileName()),
        startDir,
        tr(kRelinkFilter));

    if (picked.isEmpty()) {
        return;    // canceled; the reference stays broken and stays badged
    }

    // Storing the new path and re-resolving belongs to the reference model,
    // which does not exist yet (see devdoc section 13). Reaching here with
    // a real path is the part the UI owes; report it rather than silently
    // doing nothing, so this cannot be mistaken for a working relink.
    statusBar()->showMessage(
        tr("Relinking is not implemented yet (selected: %1)").arg(picked), 5000);
}

void MainWindow::onFileOpen()
{
    if (!maybeSave()) return;

    QString selectedFilter;
    const QString path = QFileDialog::getOpenFileName(this,
        tr("Open File"),
        QString(),
        tr(kOpenFilter),
        &selectedFilter);

    if (path.isEmpty()) return;
    openPath(path, selectedFilter);
}

bool MainWindow::openPath(const QString& pathIn, const QString& selectedFilter)
{
    // Everything below used to live inside onFileOpen(), reachable only by
    // going through the file dialog. That left the documented command-line
    // argument (`hobbycad myproject/`) with nowhere to go: main.cpp parsed
    // it into flags.fileToOpen and no code ever read it. One entry point now
    // serves both, so the two can never diverge.
    QString path = pathIn;
    if (path.isEmpty()) return false;

    // Determine file type by extension or by checking if it's a directory
    QFileInfo info(path);
    bool isProject = info.isDir() || path.endsWith(QStringLiteral(".hcad"), Qt::CaseInsensitive);

    if (isProject) {
        // Open as HobbyCAD project
        std::string errorMsg;
        if (m_project.load(path.toStdString(), &errorMsg)) {
            resetForDocumentChange();
            // Sync document shapes from project
            m_document.clear();
            // addBody, not addShape: the project's bodies already have
            // ids, names and designs, and the document must keep them.
            for (const auto& body : m_project.bodies()) {
                m_document.addBody(body);
            }
            m_document.setModified(false);

            // Update project browser
            if (m_projectBrowser) {
                m_projectBrowser->setProject(&m_project);
            }

            updateTitle();
            onDocumentLoaded();

            // Saving an unfinished sketch is allowed and expected, so this
            // is a note rather than a dialog: a project that is mid-work
            // must not interrogate the user every time it is opened. The
            // detail is a Finish away, which is where it matters.
            int unfinished = 0;
            sketchProblems(&unfinished);
            if (unfinished > 0) {
                m_statusLabel->setText(
                    tr("Opened project: %1 (%n sketch(es) not finished)",
                       "", unfinished)
                        .arg(QString::fromStdString(m_project.name())));
            } else {
                m_statusLabel->setText(tr("Opened project: %1")
                    .arg(QString::fromStdString(m_project.name())));
            }
            return true;
        }
        QMessageBox::warning(this,
            tr("Open Failed"),
            tr("Could not open project:\n%1\n\n%2").arg(path, QString::fromStdString(errorMsg)));
        return false;
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
            resetForDocumentChange();
            // Clear project state since we're loading raw geometry only
            m_project.close();
            m_project.setBodies(m_document.bodies());   // the project holds the bodies
            m_project.setModified(false);

            updateTitle();
            onDocumentLoaded();
            m_statusLabel->setText(tr("Opened: %1").arg(path));
            return true;
        }
        QMessageBox::warning(this,
            tr("Open Failed"),
            tr("Could not open file:\n%1").arg(path));
        return false;
    }
}

// ---- Recipe maintenance ---------------------------------------------

void MainWindow::recordFeatureAdded(const hobbycad::FeatureData& feature,
                                    const QString& description)
{
    m_session.addFeature(feature, description.toStdString());
    updateUndoActions();
}

bool MainWindow::recordFeatureModified(
    int featureId,
    const std::function<void(hobbycad::FeatureData&)>& edit,
    const QString& description)
{
    const bool changed = m_session.modifyFeature(featureId, edit, description.toStdString());
    updateUndoActions();
    return changed;
}

bool MainWindow::recordFeatureDeleted(int featureId, const QString& description)
{
    const bool removed = m_session.deleteFeature(featureId, description.toStdString());
    updateUndoActions();
    return removed;
}

// ---- DocumentUndoHost -----------------------------------------------

void MainWindow::pushDocumentCommand(const hobbycad::DocumentCommand& cmd)
{
    m_session.record(cmd);
    updateUndoActions();
}

bool MainWindow::undoDocument(std::string* description)
{
    // The session applies the command, whatever it acts on (the recipe, one
    // of the project's lists, a sketch's contents), all or nothing. The views
    // then follow the project.
    if (!m_session.undo(description)) {
        return false;
    }
    syncParametersFromProject();
    onDocumentRecipeChanged();
    updateUndoActions();
    return true;
}

bool MainWindow::redoDocument(std::string* description)
{
    if (!m_session.redo(description)) {
        return false;
    }
    syncParametersFromProject();
    onDocumentRecipeChanged();
    updateUndoActions();
    return true;
}

std::string MainWindow::nextUndoDescription() const { return m_session.history().undoDescription(); }
std::string MainWindow::nextRedoDescription() const { return m_session.history().redoDescription(); }
int MainWindow::undoDepth() const { return m_session.history().undoLevels(); }
int MainWindow::redoDepth() const { return m_session.history().redoLevels(); }

void MainWindow::onDocumentRecipeChanged()
{
    // The project changed underneath the views: undo, a CLI command, a
    // timeline action, a model operation. Everything shown is rebuilt from it.
    if (m_inSketchMode && m_draftOpen && !m_draft.isNew()
        && !m_session.sketchById(m_draft.editingId)) {
        // The sketch being edited is gone (undone, or deleted at the prompt).
        m_draftOpen = false;
        exitSketchMode();
    }
    syncDocumentFromProject();
    if (m_projectBrowser) {
        m_projectBrowser->setProject(&m_project);
    }
    refreshTimeline();
    rebuildObjectsTree();
    refreshModelViews();
    updateTitle();
}

void MainWindow::updateUndoActions()
{
    // While a sketch is open the Edit menu belongs to the INNER stack:
    // undo inside a sketch must never pop a feature off the outer one.
    // initSketchConnections() drives the actions in that case.
    if (m_inSketchMode) {
        return;
    }
    if (m_actionUndo) {
        m_actionUndo->setEnabled(m_session.history().canUndo());
        const std::string d = m_session.history().undoDescription();
        m_actionUndo->setText(d.empty() ? tr("Undo")
                                        : tr("Undo %1").arg(QString::fromStdString(d)));
    }
    if (m_actionRedo) {
        m_actionRedo->setEnabled(m_session.history().canRedo());
        const std::string d = m_session.history().redoDescription();
        m_actionRedo->setText(d.empty() ? tr("Redo")
                                        : tr("Redo %1").arg(QString::fromStdString(d)));
    }
}

bool MainWindow::saveProject(const QString& chosenPath)
{
    // The one project save behind Save, Save As and the unsaved-changes
    // prompt. Save used to skip the bodies (only Save As copied them in), and
    // the prompt appended ".hcad" to a new name, which the library took for a
    // manifest in the folder being browsed: the project landed loose in it.
    std::string target;
    if (!chosenPath.isEmpty()) {
        target = hobbycad::projectDirForSavePath(chosenPath.toStdString());
        m_project.setName(QFileInfo(QString::fromStdString(target)).fileName().toStdString());
    }
    // The project holds the bodies and sketches; a sketch open on the canvas
    // is written as it stands, since Save is an ungated checkpoint.
    const hobbycad::SketchDraft draft = draftFromCanvas();
    std::string errorMsg;
    if (!m_session.save(target, &errorMsg, (m_inSketchMode && m_draftOpen) ? &draft : nullptr)) {
        QMessageBox::warning(this,
            tr("Save Failed"),
            tr("Could not save project:\n%1").arg(QString::fromStdString(errorMsg)));
        return false;
    }
    m_document.setModified(false);
    if (m_projectBrowser) {
        m_projectBrowser->setProject(&m_project);
    }
    updateTitle();
    m_statusLabel->setText(tr("Saved project: %1").arg(QString::fromStdString(m_project.name())));
    return true;
}

void MainWindow::resetForDocumentChange()
{
    // maybeSave() has run, so an open sketch was either saved with the project
    // or deliberately dropped; it must not stay on the next document's canvas.
    if (m_inSketchMode) {
        discardCurrentSketch();
        exitSketchMode();
    }
    // The history describes the old document. Undoing it into the new one
    // would apply its commands to features and sketches that are not there.
    m_session.clearHistory();
    updateUndoActions();
}

void MainWindow::onFileSave()
{
    // Check if we have an existing project or document
    if (!m_project.isNew()) {
        saveProject();   // where it already lives
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
        // Project structure per project_definition.txt Section 5.2: a
        // directory holding a manifest of the same name. The user may type
        // the directory or "name.hcad", or pick an existing manifest; the
        // library resolves all three the same way.
        saveProject(path);
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

    // Into the project, which holds the bodies; the document follows it.
    for (const TopoDS_Shape& shape : result.shapes) {
        m_project.addBody(shape);
    }

    m_document.setModified(true);
    onDocumentRecipeChanged();

    statusBar()->showMessage(
        tr("Imported %1 shape(s) from STEP file").arg(result.shapeCount), 5000);
}

void MainWindow::onFileExportStep()
{
    const auto& bodies = m_document.bodies();

    // The exporters take bare shapes; unpack at the boundary.
    std::vector<TopoDS_Shape> shapes;
    shapes.reserve(bodies.size());
    for (const auto& b : bodies) {
        shapes.push_back(b.shape);
    }

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
    const auto& bodies = m_document.bodies();

    // The exporters take bare shapes; unpack at the boundary.
    std::vector<TopoDS_Shape> shapes;
    shapes.reserve(bodies.size());
    for (const auto& b : bodies) {
        shapes.push_back(b.shape);
    }

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
    if (m_actionImportDXF) m_actionImportDXF->setEnabled(enabled);
    if (m_actionExportDXF) m_actionExportDXF->setEnabled(enabled);
    if (m_actionExportSVG) m_actionExportSVG->setEnabled(enabled);
}

bool MainWindow::getSelectedSketchForExport(
    QVector<sketch::Entity>& outEntities,
    QVector<sketch::Constraint>& outConstraints) const
{
    // In a sketch the canvas is the source; otherwise the sketch selected in
    // the timeline.
    if (m_inSketchMode || !m_timeline) return false;
    const int index = m_timeline->selectedIndex();
    if (index < 0 || m_timeline->featureAt(index) != TimelineFeature::Sketch) return false;
    const SketchData* sk = m_session.sketchById(m_timeline->featureIdAt(index));
    if (!sk || sk->entities.empty()) return false;
    outEntities = QVector<sketch::Entity>(sk->entities.begin(), sk->entities.end());
    outConstraints = QVector<sketch::Constraint>(sk->constraints.begin(), sk->constraints.end());
    return true;
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
    // Tag the file with the sketch plane's normal (DXF OCS) so a round-trip
    // preserves the plane instead of flattening to XY.
    if (canvas) options.extrusion = hobbycad::planeBasisFor(canvas->sketchPlane()).normal;
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

void MainWindow::onFileImportDXF()
{
    SketchCanvas* canvas = activeSketchCanvas();
    if (!canvas) {
        QMessageBox::information(this, tr("Import DXF"),
            tr("Enter a sketch first. DXF geometry is imported into the active sketch."));
        return;
    }

    const QString filePath = QFileDialog::getOpenFileName(
        this, tr("Import DXF File"), QString(),
        tr("DXF Files (*.dxf);;All Files (*)"));
    if (filePath.isEmpty()) return;

    // startId is irrelevant: addImportedEntities reassigns fresh sketch ids.
    sketch::DXFImportOptions options;
    const sketch::DXFImportResult result =
        sketch::importDXFFile(filePath.toStdString(), 1, options);

    if (!result.success) {
        QMessageBox::critical(this, tr("Import Failed"),
            tr("Could not import the DXF file:\n%1")
                .arg(QString::fromStdString(result.errorMessage)));
        return;
    }
    if (result.entities.empty()) {
        QMessageBox::information(this, tr("Import DXF"),
            tr("No supported geometry was found in the DXF file."));
        return;
    }

    const int added = canvas->addImportedEntities(toGuiEntities(result.entities));

    // Surface the non-fatal notes (units assumed, OCS flattening, unsupported
    // constructs) so the under-specified corners are visible, not silent.
    if (!result.warnings.empty()) {
        QStringList notes;
        for (const auto& w : result.warnings)
            notes << QStringLiteral("\u2022 ") + QString::fromStdString(w);
        QMessageBox::warning(this, tr("DXF Imported with Notes"),
            tr("Imported %1 entities. Notes:\n\n%2").arg(added).arg(notes.join(QLatin1Char('\n'))));
    } else {
        statusBar()->showMessage(tr("Imported %1 entities from DXF").arg(added), 5000);
    }
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
            constraints.append(toLibraryConstraint(c));
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

    resetForDocumentChange();
    m_document.clear();
    m_document.setModified(false);
    m_project.close();
    updateTitle();
    onDocumentClosed();
    m_statusLabel->setText(tr("Document closed"));
}

// ---- Close event / unsaved changes ---------------------------------

bool MainWindow::frameNearlyFillsScreen() const
{
    // Pure size test: a maximized window fills the screen's work area, a
    // normal one is significantly smaller. Live and WM-independent, so it
    // stays correct even when the WM's state signal is stale or missing.
    if (isMinimized() || isFullScreen())
        return false;
    const QScreen* scr = screen();
    if (!scr)
        return false;
    const QRect avail = scr->availableGeometry();
    const QSize frame = frameGeometry().size();
    return frame.width()  >= avail.width()  * 0.9 &&
           frame.height() >= avail.height() * 0.9;
}

bool MainWindow::isEffectivelyMaximized() const
{
    // The WM-reported state (m_windowMaximized) is precise where it is
    // reported, and rejects a large-but-not-maximized window. The size test
    // overrides the known-stale case: X11 WMs such as fluxbox omit
    // _NET_WM_STATE on an unmaximize from the title bar, leaving the reported
    // state stuck true even though the window is no longer maximized.
    if (isMinimized() || isFullScreen())
        return false;
    if (!screen())
        return m_windowMaximized;   // no size info: trust the WM flag
    return m_windowMaximized && frameNearlyFillsScreen();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (maybeSave()) {
        // Save window geometry and dock/toolbar state
        QSettings settings;

        // Whether the window is maximized, decided from the actual size
        // (isEffectivelyMaximized) rather than isMaximized(): some X11 WMs
        // (fluxbox) omit _NET_WM_STATE on unmaximize, leaving Qt's flag stale.
        const bool maximized = isEffectivelyMaximized();

        // Save the pre-maximize (normal) geometry, not the maximized frame, so
        // a maximized window reopens maximized but keeps its normal size.
        if (maximized && !m_normalGeometry.isEmpty()) {
            settings.setValue(QStringLiteral("window/geometry"), m_normalGeometry);
        } else {
            settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
        }
        settings.setValue(QStringLiteral("window/state"), saveState());
        settings.setValue(QStringLiteral("window/maximized"), maximized);
        saveSketchViewPreferences();
        if (m_projectTabs) {
            settings.setValue(QStringLiteral("window/projectTab"),
                              m_projectTabs->currentIndex());
        }
        event->accept();
    } else {
        event->ignore();
    }
}

void MainWindow::changeEvent(QEvent* event)
{
    QMainWindow::changeEvent(event);
    // Record the WM-reported maximized state as it transitions. This is the
    // authoritative signal on window managers that report it; it is
    // cross-checked against the frame size (see isEffectivelyMaximized) for
    // the ones that do not.
    if (event->type() == QEvent::WindowStateChange) {
        m_windowMaximized = windowState().testFlag(Qt::WindowMaximized);
    }
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    // Save geometry when not maximized (normal window state)
    if (!frameNearlyFillsScreen() && !isMinimized() && !isFullScreen()) {
        m_normalGeometry = saveGeometry();
    }
}

void MainWindow::moveEvent(QMoveEvent* event)
{
    QMainWindow::moveEvent(event);
    // Save geometry when not maximized (normal window state)
    if (!frameNearlyFillsScreen() && !isMinimized() && !isFullScreen()) {
        m_normalGeometry = saveGeometry();
    }
}

bool MainWindow::maybeSave()
{
    if (!m_document.isModified() && !m_project.isModified()) {
        return true;  // nothing to save; proceed
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
        return false;  // user canceled; don't close
    }

    if (clicked == saveBtn) {
        // Try to save
        if (!m_project.isNew()) {
            if (!saveProject()) return false;
        } else if (!m_document.isNew()) {
            // Save to existing BREP file
            if (!m_document.saveBrep()) {
                QMessageBox::warning(this,
                    tr("Save Failed"),
                    tr("Could not save file:\n%1").arg(QString::fromStdString(m_document.filePath())));
                return false;
            }
        } else {
            // New document: prompt for location
            QString selectedFilter = tr(kProjectFilter);
            QString path = QFileDialog::getSaveFileName(this,
                tr("Save As"),
                QString(),
                tr(kSaveFilter),
                &selectedFilter);

            if (path.isEmpty()) {
                return false;  // user canceled the save dialog
            }

            bool saveAsProject = selectedFilter.contains(QStringLiteral(".hcad"));

            if (saveAsProject) {
                if (!saveProject(path)) return false;
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

    // discardBtn or successful save: proceed with close
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

void MainWindow::applyThemeChoice(bool dark)
{
    // Widget stylesheet (QSS reaches the Qt widgets; the sketch canvas is
    // QPainter and picks its own palette from the flag below).
    QFile f(dark ? QStringLiteral(":/themes/darkmode.qss")
                 : QStringLiteral(":/themes/default.qss"));
    if (f.open(QIODevice::ReadOnly | QIODevice::Text))
        qApp->setStyleSheet(QString::fromUtf8(f.readAll()));

    // The canvas reads this in Auto mode; set it so future canvases follow too.
    qApp->setProperty("hobbycad_dark_theme", dark);

    // Every open sketch canvas, in this window or any other.
    for (QWidget* w : qApp->allWidgets())
        if (auto* c = qobject_cast<SketchCanvas*>(w))
            c->setThemeMode(dark ? SketchCanvas::ThemeMode::Dark
                                 : SketchCanvas::ThemeMode::Light);

    QSettings().setValue(QStringLiteral("theme/dark"), dark);
    if (m_actionThemeLight) m_actionThemeLight->setChecked(!dark);
    if (m_actionThemeDark)  m_actionThemeDark->setChecked(dark);
}

void MainWindow::onThemeLight() { applyThemeChoice(false); }
void MainWindow::onThemeDark()  { applyThemeChoice(true); }

void MainWindow::onConstraintMenu(int constraintType)
{
    SketchCanvas* c = activeSketchCanvas();
    if (!c) return;
    const auto t = static_cast<sketch::ConstraintType>(constraintType);
    const int selCount = static_cast<int>(c->selectedPoints().size()) + c->selectionCount();
    if (selCount > 0) {
        c->applyTypedConstraint(t);          // select-first: apply to the selection
    } else {
        c->setConstraintToolType(constraintType);  // tool-first: arm the type, click to apply
    }
}

void MainWindow::onThemeEdit()
{
    ThemeEditorDialog dlg(this);
    connect(&dlg, &ThemeEditorDialog::themeApplied, this, [this]() {
        for (QWidget* w : qApp->allWidgets())
            if (auto* c = qobject_cast<SketchCanvas*>(w)) c->applyTheme();
    });
    dlg.exec();
}

void MainWindow::applyPreferences()
{
    // Subclasses override to apply settings to the viewport, etc.
    applyBindings();

    // Cursor-trailing sketch hints: a global preference, pushed to every open
    // canvas in any window (mirrors applyThemeChoice). Default on. (Aaron)
    const bool showHints = QSettings()
        .value(QStringLiteral("preferences/showCursorHints"), true).toBool();
    for (QWidget* w : qApp->allWidgets())
        if (auto* c = qobject_cast<SketchCanvas*>(w))
            c->setShowCursorHints(showHints);
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

    // The window title and the sketch panel's project directory are both
    // functions of project identity, so they change at exactly the same
    // moments. Riding along here means the panel cannot be left holding a
    // stale directory by a save/open path that forgot to tell it, which
    // is what happened: SketchPropertiesWidget::setProjectDirectory() had
    // no callers at all, so m_projectDir was permanently empty. That
    // silently disabled the Export Image button and forced every
    // background image to be embedded, since the "is this file inside the
    // project" test can never pass against an empty directory.
    syncSketchProjectDir();
}

void MainWindow::syncSketchProjectDir()
{
    if (!m_sketchPropsWidget) return;
    m_sketchPropsWidget->setProjectDirectory(
        QString::fromStdString(m_project.projectPath()));
}

hobbycad::BrowserInput MainWindow::browserInput() const
{
    // The base window has no live geometry of its own, so the saved project
    // is the best it can do. FullModeWindow overrides this.
    return hobbycad::browserInputFor(m_project);
}

void MainWindow::hostDocumentChanged()
{
    // A CLI command just changed the model. The views do not know that, so
    // rebuild what reads from it; a sketch finished at the prompt shows up in
    // the timeline, the tree and the viewport of either mode.
    syncParametersFromProject();
    onDocumentRecipeChanged();
    updateUndoActions();
}

void MainWindow::hostProjectReplaced()
{
    // "new" or "open" at the prompt swapped the project: the same reset as
    // File > New and File > Open, without their dialogs.
    resetForDocumentChange();
    m_document.clear();
    m_document.setModified(false);
    onDocumentLoaded();
    if (m_projectBrowser) {
        m_projectBrowser->setProject(&m_project);
    }
    updateTitle();
    updateUndoActions();
}

void MainWindow::setGlModeText(const QString& text, const QString& tooltip)
{
    if (!m_glModeLabel) {
        return;
    }
    m_glModeLabel->setText(text);
    m_glModeLabel->setToolTip(tooltip);
}

void MainWindow::rebuildObjectsTree()
{
    if (!m_objectsBrowser) {
        return;
    }

    // Preserve what the user was looking at. A wholesale rebuild otherwise
    // collapses every folder and drops the selection on any change, which
    // is worse than the eight duplicated clear-and-refill loops this
    // replaced.
    const ObjectsBrowserWidget::ViewState view = m_objectsBrowser->viewState();
    m_objectsBrowser->setTree(hobbycad::buildBrowserTree(browserInput()));
    m_objectsBrowser->restoreViewState(view);
}

void MainWindow::setUnitsFromString(const QString& units)
{
    m_currentUnits = lengthUnitToIndex(parseUnitSuffix(units.toStdString()));
}

void MainWindow::selectSketchInTree(int featureId)
{
    if (!m_objectsBrowser) {
        return;
    }
    if (QTreeWidgetItem* item =
            m_objectsBrowser->itemFor(hobbycad::NodeType::Sketch, featureId)) {
        m_objectsTree->setCurrentItem(item);
    }
}

void MainWindow::selectConstructionPlaneInTree(int id)
{
    if (!m_objectsBrowser) {
        return;
    }
    // itemFor() searches the whole tree by type and id, so this no longer
    // needs a pointer to the Construction folder.
    if (QTreeWidgetItem* item =
            m_objectsBrowser->itemFor(hobbycad::NodeType::ConstructionPlane, id)) {
        m_objectsTree->setCurrentItem(item);
    }
}

// ---- Shared sketch mode methods ------------------------------------

std::map<std::string, double> MainWindow::parameterMap() const
{
    std::map<std::string, double> out;
    const QMap<QString, double> values = parameterValues();
    for (auto it = values.begin(); it != values.end(); ++it) out[it.key().toStdString()] = it.value();
    return out;
}

// A properties cell reads like an expression in the Parameters dialog:
// parameters and formulas evaluate, a unit may trail ("2 in", "45 deg", the
// degree sign the cells show), and a bare length is in the unit the panel
// displays. The rules are the library's (sketch::resolveMeasurement).
bool MainWindow::parseCell(const QString& text, sketch::MeasureKind kind, double& out) const
{
    return sketch::resolveMeasurement(text.toStdString(), kind, currentLengthUnit(), parameterMap(), out);
}

bool MainWindow::parseCellPoint(const QString& text, double& x, double& y) const
{
    Point2D p;
    if (!sketch::resolveMeasuredPoint(text.toStdString(), currentLengthUnit(), parameterMap(), p)) return false;
    x = p.x; y = p.y;
    return true;
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
    // Evaluate through the single authority (ParameterEngine): it resolves in
    // dependency order, so a change propagates to whatever uses it, and it
    // refuses circular definitions. A circular set is rejected (the previous
    // parameters are kept and the user is told) rather than persisted.
    hobbycad::ParameterEngine engine;
    {
        std::vector<hobbycad::Parameter> in;
        in.reserve(static_cast<size_t>(params.size()));
        for (const Parameter& p : params) {
            hobbycad::Parameter pp;
            pp.name = p.name; pp.expression = p.expression; pp.value = p.value;
            pp.unit = p.unit; pp.comment = p.comment; pp.isUserParam = p.isUserParam;
            pp.isReference = p.isReference; pp.referenceSource = p.referenceSource;
            in.push_back(pp);
        }
        engine.setParameters(in);
    }
    engine.evaluate();
    if (engine.hasCircularDependencies()) {
        const auto chain = engine.circularDependencyChain();
        QStringList parts;
        for (const auto& c : chain) parts << QString::fromStdString(c);
        statusBar()->showMessage(
            tr("Circular parameter definition (%1): not applied")
                .arg(parts.join(QStringLiteral(" -> "))), 6000);
        return;   // keep the previous parameters
    }

    // Adopt the evaluated set, in the dialog's order, so dependents carry their
    // propagated values.
    m_parameters.clear();
    for (const Parameter& p : params) {
        Parameter np = p;
        if (const hobbycad::Parameter* ep = engine.parameter(p.name)) {
            np.expression = ep->expression;
            np.value = ep->value;
            np.isValid = ep->isValid;
            np.isReference = ep->isReference;
            np.referenceSource = ep->referenceSource;
        }
        m_parameters.append(np);
    }

    // Persist the evaluated parameters to the project's own list, and record
    // ONE undo step per Apply/OK (the dialog only emits on those).
    const std::vector<hobbycad::ParameterData> before = m_project.parameters();
    std::vector<hobbycad::ParameterData> after;
    after.reserve(static_cast<size_t>(m_parameters.size()));
    for (const Parameter& p : m_parameters) {
        hobbycad::ParameterData d;
        d.name = p.name;
        d.expression = p.expression;
        d.value = p.value;
        d.unit = p.unit;
        d.comment = p.comment;
        d.isUserParam = p.isUserParam;
        d.isReference = p.isReference;
        d.referenceSource = p.referenceSource;
        // Keep a parameter's design when its name is unchanged; a brand-new
        // one joins the active design, as Project::addParameter() does.
        d.designId = m_project.activeDesignId();
        for (const auto& b : before) {
            if (b.name == p.name) { d.designId = b.designId; break; }
        }
        after.push_back(d);
    }

    auto sameParams = [](const std::vector<hobbycad::ParameterData>& a,
                         const std::vector<hobbycad::ParameterData>& b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) {
            if (a[i].name != b[i].name || a[i].expression != b[i].expression
                || a[i].value != b[i].value || a[i].unit != b[i].unit
                || a[i].comment != b[i].comment
                || a[i].isReference != b[i].isReference
                || a[i].referenceSource != b[i].referenceSource) return false;
        }
        return true;
    };

    if (!sameParams(before, after)) {
        m_project.setParameters(after);
        pushDocumentCommand(hobbycad::makeParameterListCommand(
            before, after, "Edit parameters"));
        // Let the document react. NOTE: regenerating dependent FEATURE
        // geometry from a parameter change is still a TODO in
        // onDocumentRecipeChanged(); this persists and undoes the parameters
        // themselves, which is what the dialog now writes to the project.
        onDocumentRecipeChanged();
    }

    // Feed the current parameter values to the active sketch so expression-
    // backed dimensions re-evaluate and the geometry follows on the re-solve.
    if (SketchCanvas* canvas = activeSketchCanvas()) {
        std::map<std::string, double> values;
        for (const Parameter& pp : m_parameters) values[pp.name] = pp.value;
        canvas->setParameterValues(values);
        canvas->refreshConstraintState();
    }

    statusBar()->showMessage(tr("Parameters updated"), 3000);
}

void MainWindow::syncParametersFromProject()
{
    m_parameters.clear();
    for (const auto& d : m_project.parameters()) {
        Parameter p;
        p.name = d.name;
        p.expression = d.expression;
        p.value = d.value;
        p.unit = d.unit;
        p.comment = d.comment;
        p.isUserParam = d.isUserParam;
        p.isReference = d.isReference;
        p.referenceSource = d.referenceSource;
        p.isValid = true;
        m_parameters.append(p);
    }
}

void MainWindow::recomputeReferenceParameters()
{
    if (m_recomputingReferences) return;            // signal is post-solve; never recurse
    if (!m_sketchCanvas) return;

    bool anyRef = false;
    for (const Parameter& p : m_parameters) if (p.isReference) { anyRef = true; break; }
    if (!anyRef) return;

    m_recomputingReferences = true;

    // Resolve (entityId, pointIndex) against the just-solved canvas geometry.
    // SketchEntity is a sketch::Entity, so its points are the solved positions.
    auto resolve = [this](int id, int idx, hobbycad::Point2D& out) -> bool {
        for (const SketchEntity& e : m_sketchCanvas->entities()) {
            if (e.id != id) continue;
            if (idx < 0 || idx >= static_cast<int>(e.points.size())) return false;
            out = e.points[static_cast<size_t>(idx)];
            return true;
        }
        return false;
    };

    // Same authority path the dialog uses: build the engine from the current
    // parameters, push the freshly measured reference values, evaluate so
    // dependents propagate.
    hobbycad::ParameterEngine engine;
    {
        std::vector<hobbycad::Parameter> in;
        in.reserve(static_cast<size_t>(m_parameters.size()));
        for (const Parameter& p : m_parameters) in.push_back(p);
        engine.setParameters(in);
    }
    for (const Parameter& p : m_parameters) {
        if (!p.isReference) continue;
        double v = 0.0;
        if (hobbycad::sketch::measureReferenceSource(p.referenceSource, resolve, v))
            engine.setReferenceValue(p.name, v);
    }
    engine.evaluate();

    // Adopt the evaluated values, noting whether anything actually moved so we
    // do not dirty the document on a solve that changed no measured length.
    bool changed = false;
    for (Parameter& p : m_parameters) {
        if (const hobbycad::Parameter* ep = engine.parameter(p.name)) {
            if (p.value != ep->value || p.isValid != ep->isValid) changed = true;
            p.value = ep->value;
            p.isValid = ep->isValid;
        }
    }

    if (changed) {
        // Persist the derived values (they save with the document and are
        // re-measured on the next solve). This is not a user edit, so it takes
        // no undo step; it rides with the geometry change that produced it.
        std::vector<hobbycad::ParameterData> after = m_project.parameters();
        for (auto& d : after)
            for (const Parameter& p : m_parameters)
                if (p.name == d.name) { d.value = p.value; break; }
        m_project.setParameters(after);

        // Stage the refreshed values on the sketch so an expression dimension
        // that USES a reference parameter re-evaluates on the next solve.
        std::map<std::string, double> values;
        for (const Parameter& pp : m_parameters) values[pp.name] = pp.value;
        m_sketchCanvas->setParameterValues(values);
    }

    m_recomputingReferences = false;
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

    // Show the new entity's properties. Nothing is selected on the canvas;
    // the drawing tool stays armed for the next entity.
    showSketchEntityProperties(entityId);
}

void MainWindow::onSketchSelectionChanged(int entityId)
{
    if (entityId < 0) {
        // Entity deselected: check if a constraint is selected instead
        int cid = m_sketchCanvas->selectedConstraintId();
        if (cid >= 0) {
            showSketchConstraintProperties(cid);
            return;
        }
        // No constraint either: let subclass handle deselection
        onSketchDeselected();
        return;
    }

    // Show entity properties (sketch remains selected)
    m_sketchCanvas->setSketchSelected(true);
    showSketchEntityProperties(entityId);
}

void MainWindow::onSketchDeselected()
{
    // Nothing selected on the canvas: the sketch itself is, and its page shows.
    if (m_inSketchMode && m_sketchCanvas) {
        m_sketchCanvas->setSketchSelected(true);
        showSketchProperties();
    }
}

void MainWindow::onCreateSketchClicked()
{
    // A plane picked in the objects tree is used as it is.
    if (m_objectsTree) {
        if (QTreeWidgetItem* current = m_objectsTree->currentItem()) {
            const QString itemType = current->data(0, Qt::UserRole).toString();
            if (itemType == QStringLiteral("origin_plane")) {
                createSketchOnPlane(static_cast<SketchPlane>(current->data(0, Qt::UserRole + 1).toInt()));
                return;
            }
            if (itemType == QStringLiteral("construction_plane")) {
                const int planeId = current->data(0, Qt::UserRole + 1).toInt();
                if (const ConstructionPlaneData* cp = m_project.constructionPlaneById(planeId)) {
                    createSketchOnPlane(SketchPlane::Custom, cp->offset,
                                        cp->primaryAxis, cp->primaryAngle);
                    return;
                }
            }
        }
    }

    // Otherwise ask.
    SketchPlaneDialog dialog(this);
    dialog.setAvailableConstructionPlanes(m_project.constructionPlanes());
    if (dialog.exec() != QDialog::Accepted) {
        return;  // User canceled
    }

    SketchPlane plane = dialog.selectedPlane();
    double offset = dialog.offset();
    PlaneRotationAxis axis = dialog.rotationAxis();
    double angle = dialog.rotationAngle();
    if (const ConstructionPlaneData* cp = m_project.constructionPlaneById(dialog.constructionPlaneId())) {
        plane = SketchPlane::Custom;
        offset = dialog.offset() + cp->offset;
        axis = cp->primaryAxis;
        angle = cp->primaryAngle;
    }
    createSketchOnPlane(plane, offset, axis, angle);
}

QStringList MainWindow::sketchProblems(int* sketchCount) const
{
    QStringList out;
    int n = 0;
    for (const auto& sk : m_project.sketches()) {
        // ConstraintData is an alias for sketch::Constraint, so the stored
        // list is already the solver's form; pass it straight through.
        const sketch::ValidationResult v =
            sketch::validateSketch(sk.entities, sk.constraints, sk.groups);
        if (v.valid) continue;
        ++n;
        const QString name = sk.name.empty()
            ? tr("Sketch %1").arg(sk.id) : QString::fromStdString(sk.name);
        for (const std::string& e : v.errors)
            out << tr("%1: %2").arg(name, QString::fromStdString(e));
    }
    if (sketchCount) *sketchCount = n;
    return out;
}

bool MainWindow::sketchIsFinishable()
{
    if (!m_sketchCanvas) return true;

    // SketchEntity/SketchConstraint DERIVE from the library types, adding
    // canvas-only fields, so the vectors are not interchangeable. Slice to
    // the base: the validator has no use for label placement.
    std::vector<sketch::Entity> ents;
    ents.reserve(m_sketchCanvas->entities().size());
    for (const SketchEntity& e : m_sketchCanvas->entities())
        ents.push_back(static_cast<const sketch::Entity&>(e));
    std::vector<sketch::Constraint> cons;
    cons.reserve(m_sketchCanvas->constraints().size());
    for (const SketchConstraint& c : m_sketchCanvas->constraints())
        cons.push_back(static_cast<const sketch::Constraint&>(c));
    std::vector<sketch::Group> grps;
    grps.reserve(m_sketchCanvas->groups().size());
    for (const SketchGroup& g : m_sketchCanvas->groups())
        grps.push_back(static_cast<const sketch::Group&>(g));

    const sketch::ValidationResult v = sketch::validateSketch(ents, cons, grps);
    if (v.valid) return true;

    // What lands here is corruption, not work in progress: duplicate ids,
    // dangling constraint references, primitives collapsed to points.
    // Under-constrained and open-profile sketches are NOT errors and never
    // reach this, so it cannot nag during ordinary work.
    QStringList problems;
    for (const std::string& e : v.errors)
        problems << QString::fromStdString(e);

    if (!sketch::debugModeEnabled()) {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle(tr("Sketch Cannot Be Finished"));
        // Says what to do next, and does not imply the work is lost: Save
        // still works, so leaving it half-fixed costs nothing.
        box.setText(tr("This sketch has %n problem(s), so it is not finished "
                       "yet.\n\nFix them, or discard the sketch. Saving "
                       "still works in the meantime.", "", problems.size()));
        box.setDetailedText(problems.join('\n'));
        box.exec();
        return false;
    }

    // The debug escape hatch, so a damaged sketch can be handed to someone
    // who needs to see it. Allowed, but never silently.
    QMessageBox::warning(this, tr("Finishing A Damaged Sketch"),
        tr("HOBBYCAD_DEBUG is set, so this sketch is being kept despite "
           "%n problem(s).\n\n%1", "", problems.size())
            .arg(problems.join('\n')));
    return true;
}

void MainWindow::saveCurrentSketch()
{
    if (!m_draftOpen) return;
    const hobbycad::SketchDraft draft = draftFromCanvas();
    const QString name = QString::fromStdString(draft.sketch.name);
    const int entityCount = static_cast<int>(draft.sketch.entities.size());

    // One undoable step: a new sketch with its feature record, or the edit.
    const int id = m_session.finishSketch(draft,
        (draft.isNew() ? tr("Create %1") : tr("Edit %1")).arg(name).toStdString());
    m_draftOpen = false;
    m_draft = hobbycad::SketchDraft{};

    onDocumentRecipeChanged();
    if (id >= 0) {
        selectSketchInTree(id);
        if (m_timeline) m_timeline->setSelectedIndex(m_timeline->indexOfFeatureId(id));
    }
    statusBar()->showMessage(
        tr("Sketch '%1' saved with %2 entities").arg(name).arg(entityCount), 3000);
}

void MainWindow::discardCurrentSketch()
{
    if (!m_draftOpen) return;
    const bool wasNew = m_draft.isNew();
    const QString name = QString::fromStdString(m_draft.sketch.name);
    const int entityCount = m_sketchCanvas ? static_cast<int>(m_sketchCanvas->entities().size()) : 0;

    // Nothing was stored, so there is nothing to take back out of the project.
    m_draftOpen = false;
    m_draft = hobbycad::SketchDraft{};
    refreshTimeline();
    rebuildObjectsTree();

    if (!wasNew) {
        statusBar()->showMessage(tr("Changes to '%1' discarded").arg(name), 3000);
    } else if (entityCount == 0) {
        statusBar()->showMessage(tr("Empty sketch discarded"), 3000);
    } else {
        statusBar()->showMessage(tr("Sketch discarded (%1 entities)").arg(entityCount), 3000);
    }
}

void MainWindow::onCalibrateBackground()
{
    if (!m_sketchCanvas) return;

    const sketch::BackgroundImage bg = m_sketchCanvas->backgroundImage();
    if (!bg.enabled) {
        QMessageBox::information(this, tr("Calibrate Background"),
            tr("Load a background image before calibrating it."));
        return;
    }

    if (!m_bgCalibrationDialog) {
        m_bgCalibrationDialog = new BackgroundCalibrationDialog(this);

        // However the dialog ends (Apply, Cancel, Escape, or the window's
        // close button), the canvas has to come out of calibration mode.
        // The dialog clears the modes in closeEvent(), but Escape and the
        // button box reach done() WITHOUT generating a close event, so on
        // those paths the canvas would be left silently swallowing clicks
        // with no visible way back.
        connect(m_bgCalibrationDialog, &QDialog::finished,
                this, [this](int result) {
            if (!m_sketchCanvas) return;
            m_sketchCanvas->setBackgroundCalibrationMode(false);
            m_sketchCanvas->setCalibrationEntitySelectionMode(false);

            if (result != QDialog::Accepted ||
                !m_bgCalibrationDialog->wasCalibrated()) {
                return;
            }

            const sketch::BackgroundImage out =
                m_bgCalibrationDialog->calibratedBackground();
            m_sketchCanvas->setBackgroundImage(out);
            if (m_sketchPropsWidget) {
                m_sketchPropsWidget->setBackgroundImage(out);
            }
            statusBar()->showMessage(tr("Background image calibrated"), 3000);
        });
    }

    // Rebind every time: the canvas is recreated per sketch session, and the
    // dialog outlives it.
    m_bgCalibrationDialog->setSketchCanvas(m_sketchCanvas);
    m_bgCalibrationDialog->setBackgroundImage(bg);

    // show(), not exec(). See the declaration in mainwindow.h: the user
    // has to reach the canvas while this is open.
    m_bgCalibrationDialog->show();
    m_bgCalibrationDialog->raise();
    m_bgCalibrationDialog->activateWindow();
}

void MainWindow::onRemoveBackgroundImage()
{
    if (!m_sketchCanvas) return;
    sketch::BackgroundImage bg;   // default-constructed == disabled, no path
    m_sketchCanvas->setBackgroundImage(bg);
    if (m_sketchPropsWidget) m_sketchPropsWidget->setBackgroundImage(bg);
    statusBar()->showMessage(tr("Background image removed"), 3000);
}

void MainWindow::bindSketchPanels()
{
    if (m_sketchPanelsBound || !m_sketchCanvas || !m_sketchPropsWidget) return;
    m_sketchPanelsBound = true;

    m_sketchPropsWidget->setSketchCanvas(m_sketchCanvas);
    if (m_sketchOptionsWidget) m_sketchOptionsWidget->setSketchCanvas(m_sketchCanvas);
    if (m_sketchOptionsWidget) m_sketchOptionsWidget->setSliceAction(sliceAction());
    syncSketchProjectDir();

    // Two-way binding is safe: every change slot in SketchPropertiesWidget is
    // guarded by m_updatingUi, so pushing state into the widget cannot bounce
    // back out as a change signal.
    connect(m_sketchCanvas, &SketchCanvas::backgroundImageChanged,
            m_sketchPropsWidget, &SketchPropertiesWidget::setBackgroundImage);
    connect(m_sketchPropsWidget, &SketchPropertiesWidget::backgroundImageChanged,
            m_sketchCanvas, &SketchCanvas::setBackgroundImage);
    connect(m_sketchPropsWidget, &SketchPropertiesWidget::backgroundEditModeRequested,
            m_sketchCanvas, &SketchCanvas::setBackgroundEditMode);
    // Was: flip the canvas into calibration mode and nothing else. The canvas
    // then emitted calibrationPointPicked() to no receiver at all, so Calibrate
    // led into a mode with no UI and no exit. It opens the dialog now.
    connect(m_sketchPropsWidget, &SketchPropertiesWidget::calibrateBackgroundRequested,
            this, &MainWindow::onCalibrateBackground);

    connect(m_sketchPropsWidget, &SketchPropertiesWidget::removeBackgroundImageRequested,
            this, &MainWindow::onRemoveBackgroundImage);

    m_sketchPropsWidget->setBackgroundImage(m_sketchCanvas->backgroundImage());

    if (m_constraintExplorer) m_constraintExplorer->setSketchCanvas(m_sketchCanvas);

    // Per-stage tool hints in the status bar. An empty hint means the tool
    // has no handler yet, so leave the existing sketch-mode message alone
    // rather than blanking it.
    connect(m_sketchCanvas, &SketchCanvas::sketchConstraintStateChanged,
            this, &MainWindow::updateSketchStateLabel);

    // A solve may have changed the geometry a reference parameter measures;
    // re-measure and propagate to dependents.
    connect(m_sketchCanvas, &SketchCanvas::sketchConstraintStateChanged,
            this, [this](sketch::SketchState, int) { recomputeReferenceParameters(); });

    connect(m_sketchCanvas, &SketchCanvas::toolHintChanged,
            this, [this](const QString& hint) {
                if (!hint.isEmpty()) statusBar()->showMessage(hint);
            });
    connect(m_sketchCanvas, &SketchCanvas::statusMessage,
            this, [this](const QString& msg, int ms) {
                if (!msg.isEmpty()) statusBar()->showMessage(msg, ms);
            });
}

void MainWindow::connectClipboardActions()
{
    if (!m_sketchCanvas) return;

    if (m_actionCopy) {
        connect(m_actionCopy, &QAction::triggered, this, [this]() {
            if (!m_sketchCanvas) return;
            m_sketchCanvas->copySelection();
            if (m_actionPaste) m_actionPaste->setEnabled(m_sketchCanvas->hasClipboard());
        });
    }
    if (m_actionCut) {
        connect(m_actionCut, &QAction::triggered, this, [this]() {
            if (!m_sketchCanvas) return;
            m_sketchCanvas->cutSelection();
            if (m_actionPaste) m_actionPaste->setEnabled(m_sketchCanvas->hasClipboard());
        });
    }
    if (m_actionPaste) {
        connect(m_actionPaste, &QAction::triggered, this, [this]() {
            if (m_sketchCanvas) m_sketchCanvas->pasteClipboard();
        });
    }

    // Cut/Copy follow the selection; Paste follows the clipboard.
    connect(m_sketchCanvas, &SketchCanvas::selectionChanged, this, [this](int) {
        const bool hasSel = m_sketchCanvas && !m_sketchCanvas->selectedEntityIds().isEmpty();
        if (m_actionCopy) m_actionCopy->setEnabled(hasSel);
        if (m_actionCut)  m_actionCut->setEnabled(hasSel);
        if (m_actionPaste) m_actionPaste->setEnabled(m_sketchCanvas && m_sketchCanvas->hasClipboard());
    });
}

void MainWindow::loadSketchViewPreferences()
{
    if (!m_sketchCanvas) return;
    const QSettings s;
    auto b = [&](const char* key, bool def) {
        return s.value(QStringLiteral("sketch/view/") + QLatin1String(key), def).toBool();
    };
    const bool points = b("points", true);
    const bool cons   = b("constraints", true);
    const bool dims   = b("dimensions", true);
    const bool prof   = b("profiles", false);
    const bool grid   = b("grid", true);
    const bool snap   = b("snapGrid", false);

    m_sketchCanvas->setShowUnconstrainedPoints(points);
    m_sketchCanvas->setShowConstraints(cons);
    m_sketchCanvas->setShowDimensions(dims);
    m_sketchCanvas->setShowProfiles(prof);
    m_sketchCanvas->setGridVisible(grid);
    m_sketchCanvas->setSnapToGrid(snap);

    // Reflect the saved state in the menu without re-pushing to the canvas.
    auto check = [](QAction* a, bool v) { if (a) { QSignalBlocker blk(a); a->setChecked(v); } };
    check(m_actionShowUnconstrained, points);
    check(m_actionShowConstraints, cons);
    check(m_actionShowDimensions, dims);
    check(m_actionShowProfiles, prof);
    check(m_actionShowGrid, grid);
    check(m_actionSnapToGrid, snap);
}

void MainWindow::saveSketchViewPreferences()
{
    if (!m_sketchCanvas) return;
    QSettings s;
    s.setValue(QStringLiteral("sketch/view/points"), m_sketchCanvas->showUnconstrainedPoints());
    s.setValue(QStringLiteral("sketch/view/constraints"), m_sketchCanvas->showConstraints());
    s.setValue(QStringLiteral("sketch/view/dimensions"), m_sketchCanvas->showDimensions());
    s.setValue(QStringLiteral("sketch/view/profiles"), m_sketchCanvas->showProfiles());
    s.setValue(QStringLiteral("sketch/view/grid"), m_sketchCanvas->isGridVisible());
    s.setValue(QStringLiteral("sketch/view/snapGrid"), m_sketchCanvas->snapToGrid());
}

void MainWindow::createSketchOnPlane(SketchPlane plane, double offset,
                                     PlaneRotationAxis axis, double angle)
{
    // The one place a new sketch begins: the session drafts it (plane, name,
    // a reserved id) and the canvas edits the draft until Finish.
    if (m_inSketchMode) return;
    m_draft = m_session.beginSketch(plane, offset, axis, angle);
    m_draftOpen = true;
    enterSketchMode(plane);
}

void MainWindow::enterSketchMode(SketchPlane plane)
{
    hidePlaneTransformPanel();
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

    // Finish is now on the sketch toolbar (next to Flip) and in the Sketch menu.
    if (m_finishSketchAction) m_finishSketchAction->setEnabled(true);

    // Sketch panels only make sense while a sketch is open.
    bindSketchPanels();

    // Restore the persisted sketch view toggles onto the canvas + menu.
    loadSketchViewPreferences();

    // Give the canvas the current parameter values so a sketch that uses
    // expression-backed dimensions re-evaluates them on its solves.
    if (m_sketchCanvas) {
        std::map<std::string, double> values;
        for (const Parameter& pp : m_parameters) values[pp.name] = pp.value;
        m_sketchCanvas->setParameterValues(values);
    }
    if (m_sketchCanvas) {
        // Publish the state now so the status bar is populated on entry,
        // not only after the first edit.
        m_sketchCanvas->refreshConstraintState();
    }
    if (m_sketchPropsWidget) m_sketchPropsWidget->setVisible(true);
    if (m_propertiesDock) m_propertiesDock->setVisible(true);
    if (m_constraintsDock) m_constraintsDock->setVisible(true);
    if (m_sketchOptionsDock) m_sketchOptionsDock->setVisible(true);
    if (m_sketchOptionsWidget) m_sketchOptionsWidget->syncFromCanvas();
    if (m_propertiesDock) m_propertiesDock->raise();  // Properties is the active tab

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

    // The draft this canvas edits. A sketch entered some other way than
    // createSketchOnPlane() or an edit gets a fresh one.
    if (!m_draftOpen) {
        m_draft = m_session.beginSketch(plane);
        m_draftOpen = true;
    }
    if (m_draft.isNew()) {
        m_draft.sketch.gridSpacing = m_sketchCanvas->gridSpacing();
    } else {
        // A stored sketch brings its own grid and background image; the GUI
        // never wrote either back before, so both were lost on every save.
        m_sketchCanvas->setGridSpacing(m_draft.sketch.gridSpacing);
        m_sketchCanvas->setBackgroundImage(m_draft.sketch.backgroundImage);
    }
    refreshTimeline();
    rebuildObjectsTree();
    showSketchProperties();
}

void MainWindow::finishSketchInteractive()
{
    // The single Finish path shared by the sketch toolbar button and the Sketch
    // menu (the redundant properties-panel action bar is no longer shown).
    // Finish is where validity is checked; refusing here keeps the user in the
    // sketch with the tools to fix it.
    if (!m_inSketchMode) return;
    if (!sketchIsFinishable()) return;   // refused: stay, tools in hand
    const auto r = QMessageBox::question(
        this, tr("Finish Sketch"), tr("Save this sketch?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);
    if (r == QMessageBox::Save)         { saveCurrentSketch();    exitSketchMode(); }
    else if (r == QMessageBox::Discard) { discardCurrentSketch(); exitSketchMode(); }
}

void MainWindow::exitSketchMode()
{
    if (!m_inSketchMode) return;

    if (m_sketchPropsWidget) m_sketchPropsWidget->setVisible(false);
    if (m_constraintsDock) m_constraintsDock->setVisible(false);
    if (m_sketchOptionsDock) m_sketchOptionsDock->setVisible(false);

    // A non-modal dialog does not follow the sketch out. Calibrating a
    // background belonging to a sketch that is no longer open would apply
    // its result to whatever canvas came next.
    if (m_bgCalibrationDialog && m_bgCalibrationDialog->isVisible()) {
        m_bgCalibrationDialog->reject();
    }

    m_inSketchMode = false;

    // Disable sketch export actions
    setSketchExportEnabled(false);

    // Notify CLI panel that viewport commands are available again
    if (cliPanel()) {
        cliPanel()->setSketchModeActive(false);
    }

    // Switch back to normal toolbar
    m_toolbarStack->setCurrentWidget(m_toolbar);

    if (m_sketchStateLabel) {
        m_sketchStateLabel->setVisible(false);
    }
    if (m_finishSketchAction) m_finishSketchAction->setEnabled(false);

    // Deselect timeline item
    m_timeline->setSelectedIndex(-1);

    // Nothing is selected now: show the project summary, not a blank tree
    showProjectSummary();

    // A draft left open (a sketch left without Finish or Discard) must not
    // keep its pending timeline item.
    if (m_draftOpen) {
        m_draftOpen = false;
        m_draft = hobbycad::SketchDraft{};
        refreshTimeline();
    }
    updateUndoActions();

    // Update status bar
    statusBar()->showMessage(tr("Sketch finished"), 3000);
}

void MainWindow::initSketchConnections()
{
    // Sketch toolbar → tool selection
    connect(m_sketchToolbar, &SketchToolbar::toolChanged,
            this, &MainWindow::onSketchToolSelected);

    // Sketch toolbar → transform (Move/Rotate/Scale/Mirror/Copy) opens the
    // Transform section of the properties panel for the current selection.
    connect(m_sketchToolbar, &SketchToolbar::transformRequested,
            this, [this](int t) {
        if (SketchCanvas* c = activeSketchCanvas())
            c->transformSelectedEntities(static_cast<sketch::TransformType>(t));
    });

    // Sketch toolbar → 2D/3D mode toggle (the checkmark). Flips the canvas
    // mode; the panel relabels/reveals its coordinate fields in response.
    connect(m_sketchToolbar, &SketchToolbar::finishSketchRequested,
            this, &MainWindow::finishSketchInteractive);
    connect(m_sketchToolbar, &SketchToolbar::sketch3DModeToggled,
            this, [this](bool on) {
        if (SketchCanvas* c = activeSketchCanvas())
            c->setSketchMode(on);
    });

    // Sketch toolbar → heads/tails view flip (the Flip latch). Draws from the
    // far side of the plane; a view flip only, coordinates are untouched.
    connect(m_sketchToolbar, &SketchToolbar::sketchFlipToggled,
            this, [this](bool on) {
        if (SketchCanvas* c = activeSketchCanvas())
            c->setFlipView(on);
    });
    // Keep the Flip latch in step with the canvas (e.g. when a flipped sketch
    // is opened, or the flag is set programmatically for a face sketch).
    if (m_sketchCanvas)
        connect(m_sketchCanvas, &SketchCanvas::flipViewChanged,
                m_sketchToolbar, &SketchToolbar::setFlipChecked);

    // Sketch canvas signals
    connect(m_sketchCanvas, &SketchCanvas::selectionChanged,
            this, &MainWindow::onSketchSelectionChanged);
    connect(m_sketchCanvas, &SketchCanvas::entityCreated,
            this, &MainWindow::onSketchEntityCreated);
    connect(m_sketchCanvas, &SketchCanvas::mousePositionChanged,
            this, [this](const QPointF& pos) {
        statusBar()->showMessage(
            tr("X: %1  Y: %2").arg(formatLength(pos.x())).arg(formatLength(pos.y())));
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

    // Properties tree item editing
    if (QTreeWidget* propsTree = propertiesTree()) {
        connect(propsTree, &QTreeWidget::itemChanged,
                this, &MainWindow::onSketchPropertyItemChanged);
        // itemClicked, not currentItemChanged: repopulating the tree must
        // never drill into a group by itself.
        connect(propsTree, &QTreeWidget::itemClicked,
                this, &MainWindow::onSketchPropertyItemClicked);
    }

    // Toolbar → model toolbar signals
    connect(m_toolbar, &ModelToolbar::createSketchClicked,
            this, [this]() { onCreateSketchClicked(); });
    connect(m_toolbar, &ModelToolbar::parametersClicked,
            this, &MainWindow::showParametersDialog);
}

// Geometry rows of the properties tree for a line.
void MainWindow::addLineGeometryRows(QTreeWidgetItem* geomHeader, const SketchEntity* entity,
                                     int entityId, const QString& units)
{
    if (entity->points.size() >= 2) {
                addEditablePropertyRow(geomHeader, tr("Start"), pointText(entity->points[0], units), entityId, QStringLiteral("point0"));

                addEditablePropertyRow(geomHeader, tr("End"), pointText(entity->points[1], units), entityId, QStringLiteral("point1"));

        auto* lenItem = new QTreeWidgetItem(geomHeader);
        lenItem->setText(0, tr("Length"));
        double len = QLineF(entity->points[0], entity->points[1]).length();
        lenItem->setText(1, lengthText(len, units));
        lenItem->setFlags(lenItem->flags() | Qt::ItemIsEditable);
        lenItem->setData(0, Qt::UserRole, entityId);
        lenItem->setData(0, Qt::UserRole + 1, QStringLiteral("length"));
    }
}

// Geometry rows of the properties tree for a rectangle.
void MainWindow::addRectangleGeometryRows(QTreeWidgetItem* geomHeader, const SketchEntity* entity,
                                     int entityId, const QString& units)
{
    if (entity->points.size() >= 4) {
        // Four stored corners (a rotated rectangle or a parallelogram): list
        // them. Width and height describe only an axis-aligned rectangle.
        for (int i = 0; i < 4; ++i)
            addPropertyRow(geomHeader, tr("Corner %1").arg(i + 1), pointText(entity->points[i], units));
        return;
    }
    if (entity->points.size() >= 2) {
                addPropertyRow(geomHeader, tr("Corner 1"), pointText(entity->points[0], units));

                addPropertyRow(geomHeader, tr("Corner 2"), pointText(entity->points[1], units));

        auto* widthItem = new QTreeWidgetItem(geomHeader);
        widthItem->setText(0, tr("Width"));
        double w = qAbs(entity->points[1].x - entity->points[0].x);
        widthItem->setText(1, lengthText(w, units));
        widthItem->setFlags(widthItem->flags() | Qt::ItemIsEditable);
        widthItem->setData(0, Qt::UserRole, entityId);
        widthItem->setData(0, Qt::UserRole + 1, QStringLiteral("width"));

        auto* heightItem = new QTreeWidgetItem(geomHeader);
        heightItem->setText(0, tr("Height"));
        double h = qAbs(entity->points[1].y - entity->points[0].y);
        heightItem->setText(1, lengthText(h, units));
        heightItem->setFlags(heightItem->flags() | Qt::ItemIsEditable);
        heightItem->setData(0, Qt::UserRole, entityId);
        heightItem->setData(0, Qt::UserRole + 1, QStringLiteral("height"));
    }
}

// Geometry rows of the properties tree for a circle.
void MainWindow::addCircleGeometryRows(QTreeWidgetItem* geomHeader, const SketchEntity* entity,
                                     int entityId, const QString& units)
{
    if (!entity->points.empty()) {
                addEditablePropertyRow(geomHeader, tr("Center"), pointText(entity->points[0], units), entityId, QStringLiteral("point0"));

                addEditablePropertyRow(geomHeader, tr("Radius"), lengthText(entity->radius, units), entityId, QStringLiteral("radius"));

                addEditablePropertyRow(geomHeader, tr("Diameter"), lengthText(entity->radius * 2, units), entityId, QStringLiteral("diameter"));
    }
}

// Geometry rows of the properties tree for a arc.
void MainWindow::addArcGeometryRows(QTreeWidgetItem* geomHeader, const SketchEntity* entity,
                                     int entityId, const QString& units)
{
    if (!entity->points.empty()) {
                addPropertyRow(geomHeader, tr("Center"), pointText(entity->points[0], units));

                addEditablePropertyRow(geomHeader, tr("Radius"), lengthText(entity->radius, units), entityId, QStringLiteral("radius"));

                addEditablePropertyRow(geomHeader, tr("Start Angle"), QStringLiteral("%1°").arg(entity->startAngle, 0, 'f', 1), entityId, QStringLiteral("startAngle"));

                addEditablePropertyRow(geomHeader, tr("Sweep Angle"), QStringLiteral("%1°").arg(entity->sweepAngle, 0, 'f', 1), entityId, QStringLiteral("sweepAngle"));
    }
}

// Geometry rows of the properties tree for a polygon.
void MainWindow::addPolygonGeometryRows(QTreeWidgetItem* geomHeader, const SketchEntity* entity,
                                     int entityId, const QString& units)
{
    if (!entity->points.empty()) {
                addEditablePropertyRow(geomHeader, tr("Center"), pointText(entity->points[0], units), entityId, QStringLiteral("point0"));

                addEditablePropertyRow(geomHeader, tr("Sides"), QString::number(entity->sides), entityId, QStringLiteral("sides"));

                addEditablePropertyRow(geomHeader, tr("Radius"), lengthText(entity->radius, units), entityId, QStringLiteral("radius"));
    }
}

// Geometry rows of the properties tree for a slot.
void MainWindow::addSlotGeometryRows(QTreeWidgetItem* geomHeader, const SketchEntity* entity,
                                     int entityId, const QString& units)
{
    if (entity->points.size() >= 2) {
                addEditablePropertyRow(geomHeader, tr("Center 1"), pointText(entity->points[0], units), entityId, QStringLiteral("point0"));

                addEditablePropertyRow(geomHeader, tr("Center 2"), pointText(entity->points[1], units), entityId, QStringLiteral("point1"));

        auto* lenItem = new QTreeWidgetItem(geomHeader);
        lenItem->setText(0, tr("Length"));
        double len = QLineF(entity->points[0], entity->points[1]).length();
        lenItem->setText(1, lengthText(len, units));

                addEditablePropertyRow(geomHeader, tr("Width"), lengthText(entity->radius * 2, units), entityId, QStringLiteral("radius"));
    }
}

// Geometry rows of the properties tree for a ellipse.
void MainWindow::addEllipseGeometryRows(QTreeWidgetItem* geomHeader, const SketchEntity* entity,
                                     int entityId, const QString& units)
{
    if (!entity->points.empty()) {
                addEditablePropertyRow(geomHeader, tr("Center"), pointText(entity->points[0], units), entityId, QStringLiteral("point0"));

                addEditablePropertyRow(geomHeader, tr("Major Radius"), lengthText(entity->majorRadius, units), entityId, QStringLiteral("majorRadius"));

                addEditablePropertyRow(geomHeader, tr("Minor Radius"), lengthText(entity->minorRadius, units), entityId, QStringLiteral("minorRadius"));
    }
}

// Geometry rows of the properties tree for a spline.
void MainWindow::addSplineGeometryRows(QTreeWidgetItem* geomHeader, const SketchEntity* entity,
                                     int entityId, const QString& units)
{
    if (!entity->points.empty()) {
                addPropertyRow(geomHeader, tr("Control Points"), QString::number(entity->points.size()));

        // Show each control point
        for (int i = 0; i < entity->points.size(); ++i) {
                        addEditablePropertyRow(geomHeader, tr("Point %1").arg(i + 1), pointText(entity->points[i], units), entityId, QStringLiteral("point%1").arg(i));
        }
    }
}

// Geometry rows of the properties tree for a text.
void MainWindow::addTextGeometryRows(QTreeWidgetItem* geomHeader, const SketchEntity* entity,
                                     int entityId, const QString& units)
{
    if (!entity->points.empty()) {
                addEditablePropertyRow(geomHeader, tr("Position"), pointText(entity->points[0], units), entityId, QStringLiteral("point0"));

                addEditablePropertyRow(geomHeader, tr("Text"), QString::fromStdString(entity->text), entityId, QStringLiteral("text"));

                addEditablePropertyRow(geomHeader, tr("Font Size"), QStringLiteral("%1 %2").arg(entity->fontSize, 0, 'f', 1).arg(units), entityId, QStringLiteral("fontSize"));

                addEditablePropertyRow(geomHeader, tr("Rotation"), QStringLiteral("%1%2").arg(entity->textRotation, 0, 'f', 1).arg(QChar(0x00B0)), entityId, QStringLiteral("textRotation"));
    }
}

void MainWindow::showSketchEntityProperties(int entityId)
{
    hidePlaneTransformPanel();
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

    if (int gid = m_sketchCanvas->selectedWholeGroupId(); gid >= 0)
        addGroupSection(propsTree, gid);

    // Entity type
    auto* typeItem = new QTreeWidgetItem(propsTree);
    typeItem->setText(0, tr("Type"));
    QString typeName;
    switch (entity->type) {
    case SketchEntityType::Point:     typeName = tr("Point"); break;
    case SketchEntityType::Line:      typeName = tr("Line"); break;
    case SketchEntityType::Rectangle: typeName = tr("Rectangle"); break;
    case SketchEntityType::Parallelogram: typeName = tr("Parallelogram"); break;
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
        addPropertyRow(propsTree, tr("ID"), QString::number(entity->id));

    // Geometry header
    auto* geomHeader = new QTreeWidgetItem(propsTree);
    geomHeader->setText(0, tr("Geometry"));

    // Entity-specific properties
    switch (entity->type) {
    case SketchEntityType::Point:
        if (!entity->points.empty()) {
                        addEditablePropertyRow(geomHeader, tr("Position"), pointText(entity->points[0], units), entityId, QStringLiteral("point0"));
        }
        break;

    case SketchEntityType::Line:
        addLineGeometryRows(geomHeader, entity, entityId, units);
        break;

    case SketchEntityType::Rectangle:
    case SketchEntityType::Parallelogram:
        addRectangleGeometryRows(geomHeader, entity, entityId, units);
        break;

    case SketchEntityType::Circle:
        addCircleGeometryRows(geomHeader, entity, entityId, units);
        break;

    case SketchEntityType::Arc:
        addArcGeometryRows(geomHeader, entity, entityId, units);
        break;

    case SketchEntityType::Polygon:
        addPolygonGeometryRows(geomHeader, entity, entityId, units);
        break;

    case SketchEntityType::Slot:
        addSlotGeometryRows(geomHeader, entity, entityId, units);
        break;

    case SketchEntityType::Ellipse:
        addEllipseGeometryRows(geomHeader, entity, entityId, units);
        break;

    case SketchEntityType::Spline:
        addSplineGeometryRows(geomHeader, entity, entityId, units);
        break;

    case SketchEntityType::Text:
        addTextGeometryRows(geomHeader, entity, entityId, units);
        break;

    default:
        break;
    }

    // Constraints
        addPropertyRow(propsTree, tr("Constrained"), entity->constrained ? tr("Yes") : tr("No"));

    propsTree->expandAll();
}

void MainWindow::addGroupSection(QTreeWidget* propsTree, int groupId)
{
    const SketchGroup* g = m_sketchCanvas->groupById(groupId);
    if (!g) return;
    const QSignalBlocker block(propsTree);   // building rows must not read as edits
    const QString units = unitSuffix();

    auto* head = new QTreeWidgetItem(propsTree);
    head->setText(0, tr("Group"));
    head->setText(1, QString::fromStdString(g->name));
    head->setExpanded(true);

    auto* nameItem = new QTreeWidgetItem(head);
    nameItem->setText(0, tr("Name"));
    nameItem->setText(1, QString::fromStdString(g->name));
    nameItem->setFlags(nameItem->flags() | Qt::ItemIsEditable);
    nameItem->setData(0, Qt::UserRole + 1, QStringLiteral("groupName"));
    nameItem->setData(0, Qt::UserRole + 2, groupId);

    // Members: the count, then one indented row per member. Clicking a row
    // enters the group and selects that member (selection, not state).
    auto* members = new QTreeWidgetItem(head);
    members->setText(0, tr("Members"));
    members->setText(1, QString::number(g->entityIds.size()));
    members->setExpanded(true);
    for (int eid : g->entityIds) {
        const SketchEntity* e = m_sketchCanvas->findEntity(eid);
        if (!e) continue;
        QString kind;
        switch (e->type) {
        case SketchEntityType::Point:     kind = tr("point"); break;
        case SketchEntityType::Line:      kind = tr("line"); break;
        case SketchEntityType::Rectangle: kind = tr("rectangle"); break;
        case SketchEntityType::Parallelogram: kind = tr("parallelogram"); break;
        case SketchEntityType::Circle:    kind = tr("circle"); break;
        case SketchEntityType::Arc:       kind = tr("arc"); break;
        case SketchEntityType::Spline:    kind = tr("spline"); break;
        case SketchEntityType::Polygon:   kind = tr("polygon"); break;
        case SketchEntityType::Slot:      kind = tr("slot"); break;
        case SketchEntityType::Ellipse:   kind = tr("ellipse"); break;
        case SketchEntityType::Text:      kind = tr("text"); break;
        case SketchEntityType::Dimension: kind = tr("dimension"); break;
        }
        auto* row = new QTreeWidgetItem(members);
        row->setText(0, QStringLiteral("    %1 %2").arg(kind).arg(eid));
        if (!e->points.empty())
            row->setText(1, QStringLiteral("(%1, %2)").arg(e->points[0].x, 0, 'f', 2).arg(e->points[0].y, 0, 'f', 2));
        row->setData(0, Qt::UserRole + 1, QStringLiteral("groupMember"));
        row->setData(0, Qt::UserRole + 2, groupId);
        row->setData(0, Qt::UserRole + 3, eid);
        row->setToolTip(0, tr("Click to enter the group and select this member"));
    }

    auto* cons = new QTreeWidgetItem(head);
    cons->setText(0, tr("Constraints"));
    cons->setText(1, QString::number(g->constraintIds.size()));
    for (int cid : g->constraintIds) {
        const SketchConstraint* c = m_sketchCanvas->constraintById(cid);
        if (!c) continue;
        auto* row = new QTreeWidgetItem(cons);
        row->setText(0, QStringLiteral("    %1 %2").arg(QString::fromLatin1(sketch::constraintTypeName(c->type))).arg(cid));
        row->setData(0, Qt::UserRole + 1, QStringLiteral("groupConstraint"));
        row->setData(0, Qt::UserRole + 2, groupId);
        row->setData(0, Qt::UserRole + 3, cid);
        row->setToolTip(0, tr("Click to select this constraint"));
    }

    auto* locked = new QTreeWidgetItem(head);
    locked->setText(0, tr("Locked"));
    locked->setFlags((locked->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
    locked->setCheckState(1, g->locked ? Qt::Checked : Qt::Unchecked);
    locked->setData(0, Qt::UserRole + 1, QStringLiteral("groupLocked"));
    locked->setData(0, Qt::UserRole + 2, groupId);

    std::vector<sketch::Entity> ents(m_sketchCanvas->entities().begin(), m_sketchCanvas->entities().end());
    const Point2D gc = sketch::memberCenter(ents, g->entityIds);
    auto* center = new QTreeWidgetItem(head);
    center->setText(0, tr("Geometric center"));
    center->setText(1, QStringLiteral("%1, %2%3").arg(gc.x, 0, 'f', 3).arg(gc.y, 0, 'f', 3).arg(units));

    auto* pivot = new QTreeWidgetItem(head);
    pivot->setText(0, tr("Pivot"));
    pivot->setText(1, g->hasPivot ? QStringLiteral("%1, %2%3").arg(g->pivot.x, 0, 'f', 3).arg(g->pivot.y, 0, 'f', 3).arg(units)
                                  : tr("center"));
    pivot->setFlags(pivot->flags() | Qt::ItemIsEditable);
    pivot->setData(0, Qt::UserRole + 1, QStringLiteral("groupPivot"));
    pivot->setData(0, Qt::UserRole + 2, groupId);
    pivot->setToolTip(1, tr("Type \"x, y\" to store a pivot on the group, or \"center\" to follow the geometric center"));

    auto* reset = new QTreeWidgetItem(head);
    reset->setText(0, tr("Reset pivot"));
    reset->setFlags(reset->flags() & ~Qt::ItemIsEditable);
    auto* btn = new QPushButton(tr("Center"), propsTree);
    btn->setEnabled(g->hasPivot);
    btn->setToolTip(tr("Drop the stored pivot; the group rotates about its geometric center again (one undo step)"));
    connect(btn, &QPushButton::clicked, this, [this, groupId]() {
        m_sketchCanvas->setGroupPivot(groupId, std::nullopt);
        const SketchGroup* gg = m_sketchCanvas->groupById(groupId);
        if (gg && !gg->entityIds.empty()) {
            const int firstId = gg->entityIds.front();
            QTimer::singleShot(0, this, [this, firstId]() {
                if (QTreeWidget* pt = propertiesTree()) pt->blockSignals(true);
                showSketchEntityProperties(firstId);
                if (QTreeWidget* pt = propertiesTree()) pt->blockSignals(false);
            });
        }
    });
    propsTree->setItemWidget(reset, 1, btn);
}

void MainWindow::onSketchPropertyItemClicked(QTreeWidgetItem* item, int)
{
    if (!item) return;
    const QString prop = item->data(0, Qt::UserRole + 1).toString();
    if (prop == QStringLiteral("gotoTimeline")) {
        const int j = item->data(0, Qt::UserRole + 3).toInt();
        if (m_timeline && j >= 0 && j < m_timeline->itemCount()) {
            m_timeline->setSelectedIndex(j);
            QTimer::singleShot(0, this, [this, j]() { showFeatureProperties(j); });
        }
        return;
    }
    if (!m_sketchCanvas) return;
    const int gid = item->data(0, Qt::UserRole + 2).toInt();
    const int id = item->data(0, Qt::UserRole + 3).toInt();
    if (prop == QStringLiteral("groupMember")) {
        m_sketchCanvas->enterGroup(gid);        // drilling in is selection, not state
        m_sketchCanvas->selectEntity(id);
    } else if (prop == QStringLiteral("groupConstraint")) {
        m_sketchCanvas->enterGroup(gid);
        m_sketchCanvas->setSelectedConstraint(id);
    }
}

void MainWindow::hidePlaneTransformPanel()
{
    if (m_planeTransformPanel && m_planeTransformPanel->isVisible()) {
        m_planeTransformPanel->setVisible(false);
        emit m_planeTransformPanel->resetRequested();   // drop any preview
    }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (m_objectsTree && watched == m_objectsTree->viewport() && event->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::LeftButton && !m_objectsTree->itemAt(me->pos())) {
            m_objectsTree->clearSelection();
            m_objectsTree->setCurrentItem(nullptr);
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::onPlaneDeselected()
{
    hidePlaneHighlight();
    hidePlaneTransformPanel();
    if (m_planePageShown) showProjectSummary();
}

void MainWindow::showProjectSummary()
{
    hidePlaneTransformPanel();
    m_planePageShown = false;
    QTreeWidget* propsTree = propertiesTree();
    if (!propsTree || m_inSketchMode) return;
    const QSignalBlocker block(propsTree);
    propsTree->clear();

    auto row = [propsTree](const QString& k, const QString& v, QTreeWidgetItem* parent = nullptr) {
        auto* it = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(propsTree);
        it->setText(0, k); it->setText(1, v); it->setToolTip(0, k); it->setToolTip(1, v);
        return it;
    };

    const QString name = QString::fromStdString(m_project.name());
    const QString path = QString::fromStdString(m_project.projectPath());
    row(tr("Project"), name.isEmpty() ? tr("Untitled") : name);
    row(tr("File"), path.isEmpty() ? tr("Not saved yet") : path);
    row(tr("Units"), unitSuffix().trimmed());

    int sketches = 0, features = 0;
    if (m_timeline) {
        for (int i = 0; i < m_timeline->itemCount(); ++i) {
            switch (m_timeline->featureAt(i)) {
            case TimelineFeature::Origin: break;
            case TimelineFeature::Sketch: ++sketches; break;
            default: ++features; break;
            }
        }
    }
    auto* contents = row(tr("Contents"), QString());
    contents->setExpanded(true);
    row(tr("Sketches"), QString::number(sketches), contents);
    row(tr("Features"), QString::number(features), contents);
    row(tr("Bodies"), QString::number(m_project.bodies().size()), contents);
    row(tr("Planes"), QString::number(m_project.constructionPlanes().size()), contents);
    row(tr("Parameters"), QString::number(m_parameters.size()), contents);

    auto* hint = new QTreeWidgetItem(propsTree);
    hint->setText(0, tr("Pick a plane, sketch, or feature to see its properties."));
    hint->setToolTip(0, tr("Select an item in the Project tree or the timeline."));
    hint->setFirstColumnSpanned(true);
    hint->setForeground(0, QBrush(QColor(0x66, 0x66, 0x66)));
    hint->setFlags(hint->flags() & ~Qt::ItemIsSelectable);
    propsTree->resizeColumnToContents(0);
}

void MainWindow::showOriginPlaneProperties(SketchPlane plane)
{
    hidePlaneTransformPanel();
    m_planePageShown = true;
    QTreeWidget* propsTree = propertiesTree();
    if (!propsTree || m_inSketchMode) return;
    const QSignalBlocker block(propsTree);
    propsTree->clear();

    QString name, normal, axes;
    switch (plane) {
    case SketchPlane::XY: name = tr("XY plane"); normal = tr("+Z"); axes = tr("Sketch X = global X, sketch Y = global Y"); break;
    case SketchPlane::XZ: name = tr("XZ plane"); normal = tr("+Y"); axes = tr("Sketch X = global X, sketch Y = global Z"); break;
    case SketchPlane::YZ: name = tr("YZ plane"); normal = tr("+X"); axes = tr("Sketch X = global Y, sketch Y = global Z"); break;
    case SketchPlane::Custom: name = tr("Custom plane"); normal = tr("(derived)"); axes = QString(); break;
    }
    auto row = [propsTree](const QString& k, const QString& v) {
        auto* it = new QTreeWidgetItem(propsTree);
        it->setText(0, k); it->setText(1, v); it->setToolTip(0, k); it->setToolTip(1, v);
        return it;
    };
    row(tr("Name"), name);
    row(tr("Type"), tr("Origin plane"));
    row(tr("Normal"), normal);
    row(tr("Origin"), tr("(0, 0, 0)"));
    if (!axes.isEmpty()) row(tr("Axes"), axes);
    row(tr("Offset"), tr("0 (fixed)"));
    const int n = sketchCountOnPlane(plane);
    if (n >= 0) row(tr("Sketches here"), QString::number(n))->setToolTip(0, tr("Completed sketches on this plane"));

    // Origin planes are fixed: nothing here is editable. Offset, angled and
    // rolled planes are construction planes, which have their own page.
    auto* actionItem = new QTreeWidgetItem(propsTree);
    actionItem->setText(0, tr("Actions"));
    auto* btn = new QPushButton(tr("New sketch"), propsTree);
    btn->setToolTip(tr("Starts a sketch on this plane, the same as the Sketch button while it is selected"));
    connect(btn, &QPushButton::clicked, this, [this]() { onCreateSketchClicked(); });
    propsTree->setItemWidget(actionItem, 1, btn);
    auto* hint = new QTreeWidgetItem(propsTree);
    hint->setText(0, tr("Offset or angled: Construct > New Construction Plane."));
    hint->setFirstColumnSpanned(true);
    hint->setForeground(0, QBrush(QColor(0x66, 0x66, 0x66)));
    hint->setFlags(hint->flags() & ~Qt::ItemIsSelectable);
    propsTree->resizeColumnToContents(0);
}

void MainWindow::showSketchConstraintProperties(int constraintId)
{
    hidePlaneTransformPanel();
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
    case ConstraintType::Curvature:    typeName = tr("Curvature (G2)"); break;
    case ConstraintType::PointOnSpline:typeName = tr("Point on Spline"); break;
    case ConstraintType::CurvatureDimension: typeName = tr("Radius of Curvature"); break;
    case ConstraintType::TangentAngle: typeName = tr("Tangent Angle"); break;
    }
    typeItem->setText(1, typeName);

    // Constraint ID
        addPropertyRow(propsTree, tr("ID"), QString::number(constraint->id));

    // Value (for dimensional constraints)
    // The library's classification, so a newer dimensional type (radius of
    // curvature, tangent angle) is not left without a value row.
    bool hasDimensionalValue = sketch::isDimensionalConstraint(constraint->type);
    if (hasDimensionalValue) {
        auto* valueItem = new QTreeWidgetItem(propsTree);
        valueItem->setText(0, tr("Value"));
        bool isAngle = sketch::isAngularConstraint(constraint->type);
        QString suffix = isAngle ? QStringLiteral("\u00B0") : QStringLiteral(" ") + unitSuffix();
        valueItem->setText(1, QStringLiteral("%1%2")
                           .arg(formatLength(constraint->value))
                           .arg(suffix));
        if (constraint->isDriving) {
            valueItem->setFlags(valueItem->flags() | Qt::ItemIsEditable);
            valueItem->setData(0, Qt::UserRole, constraintId);
            valueItem->setData(0, Qt::UserRole + 1, QStringLiteral("constraintValue"));
        }
    }

    // Driving / Reference
        addPropertyRow(propsTree, tr("Mode"), constraint->isDriving ? tr("Driving") : tr("Reference"));

    // Enabled
        addPropertyRow(propsTree, tr("Enabled"), constraint->enabled ? tr("Yes") : tr("No"));

    // Parent entities
    auto* entitiesHeader = new QTreeWidgetItem(propsTree);
    entitiesHeader->setText(0, tr("Entities"));

    auto entityTypeName = [](SketchEntityType t) -> QString {
        return QString::fromUtf8(sketch::entityTypeName(t));   // one table, in the library
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
                                addPropertyRow(entitiesHeader, entityTypeName(e->type), QStringLiteral("ID %1").arg(e->id));
            }
        }
    }

    propsTree->expandAll();
    propsTree->blockSignals(false);
}

// Double-click on a dropdown property row shows its combo box in place.
void MainWindow::installDropdownEditor(QTreeWidget* propsTree)
{
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

void MainWindow::showFeatureProperties(int index)
{
    hidePlaneTransformPanel();
    m_planePageShown = false;
    hidePlaneHighlight();   // a sketch feature re-shows its own plane below
    QTreeWidget* propsTree = propertiesTree();
    if (!propsTree)
        return;

    propsTree->clear();

    if (index < 0 || index >= m_timeline->itemCount()) {
        showProjectSummary();
        return;
    }

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

    // Source sketch: features record the sketch they were built from as a
    // timeline dependency. Show it as a row that jumps to that sketch.
    if (feature != TimelineFeature::Sketch && feature != TimelineFeature::Origin) {
        for (int depId : m_timeline->dependenciesAt(index)) {
            for (int j = 0; j < m_timeline->itemCount(); ++j) {
                if (m_timeline->featureIdAt(j) != depId || m_timeline->featureAt(j) != TimelineFeature::Sketch) continue;
                auto* srcItem = new QTreeWidgetItem(propsTree);
                setProperty(srcItem, tr("Sketch"), m_timeline->nameAt(j));
                srcItem->setToolTip(1, tr("Click to select this sketch"));
                srcItem->setForeground(1, QBrush(QColor(0, 90, 180)));
                srcItem->setData(0, Qt::UserRole + 1, QStringLiteral("gotoTimeline"));
                srcItem->setData(0, Qt::UserRole + 3, j);
            }
        }
    }

    // Feature-specific properties: what the recipe records about the feature,
    // and nothing invented. These rows used to show made-up values (a 10 mm
    // extrude, a 50 x 30 x 20 box, "width / 2") whatever the feature held.
    // Types that record no inputs yet show none.
    auto* propsHeader = new QTreeWidgetItem(propsTree);
    setHeader(propsHeader, tr("Properties"));
    propsHeader->setExpanded(true);

    const hobbycad::FeatureData* recorded = m_session.featureById(m_timeline->featureIdAt(index));
    auto recordedNumber = [recorded](const char* key, double& out) {
        if (!recorded || !recorded->properties.contains(QLatin1String(key))) return false;
        out = recorded->properties.value(QLatin1String(key)).toDouble();
        return true;
    };
    auto addRow = [&setProperty, propsHeader](const QString& prop, const QString& value) {
        setProperty(new QTreeWidgetItem(propsHeader), prop, value);
    };
    const QStringList operations = {tr("New Body"), tr("Join"), tr("Cut"), tr("Intersect")};
    double v = 0.0;

    switch (feature) {
    case TimelineFeature::Origin:
        addRow(tr("Position"), tr("(0, 0, 0)"));
        break;

    case TimelineFeature::Sketch:
        populateSketchFeatureProperties(propsHeader, index, units);
        break;

    case TimelineFeature::Extrude:
        if (recordedNumber("distance", v)) addRow(tr("Distance"), lengthText(v, units));
        if (recordedNumber("direction", v)) {
            const QStringList directions = {tr("One Side"), tr("One Side (Reverse)"),
                                            tr("Two Sides (Symmetric)")};
            addRow(tr("Direction"), directions.value(static_cast<int>(v)));
        }
        if (recordedNumber("operation", v)) addRow(tr("Operation"), operations.value(static_cast<int>(v)));
        break;

    case TimelineFeature::Revolve:
        if (recordedNumber("angle", v)) addRow(tr("Angle"), QStringLiteral("%1°").arg(v, 0, 'g', 6));
        if (recordedNumber("axis_kind", v)) {
            const QStringList axes = {tr("X Axis"), tr("Y Axis"), tr("Sketch Line")};
            addRow(tr("Axis"), axes.value(static_cast<int>(v)));
        }
        if (recordedNumber("operation", v)) addRow(tr("Operation"), operations.value(static_cast<int>(v)));
        break;

    default:
        break;
    }

    if (propsHeader->childCount() == 0) {
        delete propsHeader;
    }

    propsTree->expandAll();

    installDropdownEditor(propsTree);
}

void MainWindow::populateSketchFeatureProperties(QTreeWidgetItem* parent,
                                                   int timelineIndex,
                                                   const QString& units)
{
    const SketchData* sk = (m_timeline && timelineIndex >= 0 && timelineIndex < m_timeline->itemCount())
        ? m_session.sketchById(m_timeline->featureIdAt(timelineIndex)) : nullptr;

    int planeIdx = 0;
    double offset = 0.0;
    int entityCount = 0, constraintCount = 0, groupCount = 0;
    if (sk) {
        switch (sk->plane) {
        case SketchPlane::XY: planeIdx = 0; break;
        case SketchPlane::XZ: planeIdx = 1; break;
        case SketchPlane::YZ: planeIdx = 2; break;
        case SketchPlane::Custom: planeIdx = 3; break;
        }
        offset = sk->planeOffset;
        entityCount = static_cast<int>(sk->entities.size());
        constraintCount = static_cast<int>(sk->constraints.size());
        groupCount = static_cast<int>(sk->groups.size());
        showSketchPlaneHighlight(sk->plane, offset, sk->rotationAxis, sk->rotationAngle);
    }

    auto* planeItem = new QTreeWidgetItem(parent);
    const QStringList planeOptions = {tr("XY"), tr("XZ"), tr("YZ"), tr("Custom")};
    planeItem->setText(0, tr("Plane"));
    planeItem->setText(1, planeOptions.value(planeIdx));
    planeItem->setToolTip(0, tr("Plane"));
    planeItem->setToolTip(1, planeOptions.value(planeIdx));
    planeItem->setData(1, Qt::UserRole, QStringLiteral("dropdown"));
    planeItem->setData(1, Qt::UserRole + 1, planeOptions);
    planeItem->setData(1, Qt::UserRole + 2, planeIdx);

    if (!qFuzzyIsNull(offset)) {
        addPropertyRow(parent, tr("Offset"), QStringLiteral("%1 %2").arg(offset, 0, 'g', 6).arg(units));
    }
    addPropertyRow(parent, tr("Entities"), QString::number(entityCount));
    addPropertyRow(parent, tr("Constraints"), QString::number(constraintCount));
    if (groupCount > 0) {
        addPropertyRow(parent, tr("Groups"), QString::number(groupCount));
    }

    // Edit sketch: the same path as the timeline's Edit action.
    if (sk && !m_inSketchMode) {
        if (QTreeWidget* propsTree = propertiesTree()) {
            auto* editItem = new QTreeWidgetItem(parent);
            editItem->setText(0, tr("Actions"));
            auto* btn = new QPushButton(tr("Edit sketch"), propsTree);
            btn->setToolTip(tr("Open this sketch in the sketcher"));
            connect(btn, &QPushButton::clicked, this, [this, timelineIndex]() { onEditFeature(timelineIndex); });
            propsTree->setItemWidget(editItem, 1, btn);
        }
    }
}

void MainWindow::rejectPropertyEdit(QTreeWidgetItem* item, const QString& reason)
{
    if (!item || !m_propertiesTree) {
        return;
    }
    m_badPropertyItems.insert(item);
    item->setToolTip(1, reason);
    m_propertiesTree->viewport()->update();

    if (m_statusLabel) {
        m_statusLabel->setText(reason);
    }

    reopenRejectedEdit(m_propertiesTree,
                       m_propertiesTree->indexFromItem(item, 1),
                       m_propsOutlineDelegate);
}

void MainWindow::acceptPropertyEdit(QTreeWidgetItem* item)
{
    if (!item || !m_propertiesTree) {
        return;
    }
    if (m_badPropertyItems.remove(item)) {
        item->setToolTip(1, QString());
        m_propertiesTree->viewport()->update();
    }
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
        const auto* c = m_sketchCanvas->constraintById(constraintId);
        if (!c) return;
        const sketch::MeasureKind kind = sketch::isAngularConstraint(c->type)
            ? sketch::MeasureKind::Angle : sketch::MeasureKind::Length;
        double newValue = 0.0;
        if (!parseCell(text, kind, newValue)) {
            // Say so rather than leaving the cell showing a value the model
            // does not have.
            rejectPropertyEdit(item, tr("'%1' is not a number or an expression.").arg(text));
            return;
        }
        if (!sketch::isValidConstraintValue(c->type, newValue)) {
            rejectPropertyEdit(item, tr("A size must be greater than zero."));
            return;
        }
        acceptPropertyEdit(item);

        m_sketchCanvas->setConstraintValue(constraintId, newValue);

        QTimer::singleShot(0, this, [this, constraintId]() {
            if (QTreeWidget* pt = propertiesTree()) pt->blockSignals(true);
            showSketchConstraintProperties(constraintId);
            if (QTreeWidget* pt = propertiesTree()) pt->blockSignals(false);
        });
        return;
    }

    if (propertyName.startsWith(QStringLiteral("group"))) {
        const int gid = item->data(0, Qt::UserRole + 2).toInt();
        const SketchGroup* g = m_sketchCanvas->groupById(gid);
        if (!g) return;
        const QString text = item->text(1).trimmed();
        if (propertyName == QStringLiteral("groupName")) {
            if (text.isEmpty()) { rejectPropertyEdit(item, tr("A group needs a name.")); return; }
            acceptPropertyEdit(item);
            m_sketchCanvas->renameGroup(gid, text);
        } else if (propertyName == QStringLiteral("groupLocked")) {
            m_sketchCanvas->setGroupLocked(gid, item->checkState(1) == Qt::Checked);
        } else if (propertyName == QStringLiteral("groupPivot")) {
            double x = 0, y = 0;
            if (text.compare(QStringLiteral("center"), Qt::CaseInsensitive) == 0) {
                acceptPropertyEdit(item);
                m_sketchCanvas->setGroupPivot(gid, std::nullopt);
            } else if (parseCellPoint(text, x, y)) {
                acceptPropertyEdit(item);
                m_sketchCanvas->setGroupPivot(gid, QPointF(x, y));
            } else {
                rejectPropertyEdit(item, tr("'%1' is not a point; use \"x, y\" or \"center\".").arg(text));
                return;
            }
        }
        const int firstId = g->entityIds.empty() ? -1 : g->entityIds.front();
        QTimer::singleShot(0, this, [this, firstId]() {
            if (firstId < 0) return;
            if (QTreeWidget* pt = propertiesTree()) pt->blockSignals(true);
            showSketchEntityProperties(firstId);
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

    // The rules per property (positivity, ranges, what follows a change)
    // are the library's (sketch/properties.h); this reads the cell and
    // reports a refusal the way this panel does.
    sketch::PropertyEdit edit;
    if (propertyName.startsWith(QStringLiteral("point"))) {
        const int idx = propertyName.mid(5).toInt();
        double x = 0, y = 0;
        if (!parseCellPoint(text, x, y)) {
            rejectPropertyEdit(item, tr("'%1' is not a point; use \"x, y\" (a unit may follow).").arg(text));
            return;
        }
        edit = sketch::setEntityPoint(*entity, idx, Point2D(x, y));
    } else if (propertyName == QStringLiteral("text")) {
        edit = sketch::setEntityText(*entity, text.toStdString());
    } else {
        const bool angle = propertyName == QStringLiteral("startAngle")
                        || propertyName == QStringLiteral("sweepAngle")
                        || propertyName == QStringLiteral("textRotation");
        const sketch::MeasureKind kind = angle ? sketch::MeasureKind::Angle
                                       : propertyName == QStringLiteral("sides") ? sketch::MeasureKind::Count
                                                                                 : sketch::MeasureKind::Length;
        double value = 0.0;
        if (!parseCell(text, kind, value)) {
            rejectPropertyEdit(item, tr("'%1' is not a number or an expression.").arg(text));
            return;
        }
        edit = sketch::setEntityNumber(*entity, propertyName.toStdString(), value);
    }
    if (edit.problem != sketch::PropertyProblem::None) {
        // Say so for the fields whose refusal used to be silent-and-visible
        // (the cell kept a value the model did not have).
        if (propertyName == QStringLiteral("radius"))
            rejectPropertyEdit(item, tr("A radius must be a positive number."));
        else if (propertyName == QStringLiteral("diameter"))
            rejectPropertyEdit(item, tr("A diameter must be a positive number."));
        else if (propertyName == QStringLiteral("startAngle"))
            rejectPropertyEdit(item, tr("'%1' is not a number.").arg(text));
        return;
    }
    if (propertyName == QStringLiteral("radius") || propertyName == QStringLiteral("diameter")
        || propertyName == QStringLiteral("startAngle"))
        acceptPropertyEdit(item);
    const bool changed = edit.changed;
    const int editedPointIndex = edit.editedPointIndex;

    if (changed) {
        m_sketchCanvas->pushUndoCommand(sketch::UndoCommand::modifyEntity(
            oldEntity, *entity,
            "Edit " + propertyName.toStdString()));

        // Use FixedPoint constraint during solve if editing a specific point,
        // so the edited point stays exactly where placed and other geometry adjusts
        if (editedPointIndex >= 0) {
            m_sketchCanvas->notifyEntityPointChanged(entityId, editedPointIndex);
        } else {
            m_sketchCanvas->notifyEntityChanged(entityId);
        }
        QTimer::singleShot(0, this, [this, entityId]() {
            if (QTreeWidget* pt = propertiesTree()) pt->blockSignals(true);
            showSketchEntityProperties(entityId);
            if (QTreeWidget* pt = propertiesTree()) pt->blockSignals(false);
        });
    }
}

// ---- The session's views ----------------------------------------------
//
// Everything below used to live in FullModeWindow, over a sketch list of its
// own, while Reduced mode had a demo timeline and stored nothing. It works on
// the ProjectSession now, so both modes show and edit the same project the
// CLI does.

namespace {

TimelineFeature timelineFeatureFor(hobbycad::FeatureType type)
{
    switch (type) {
    case hobbycad::FeatureType::Origin:    return TimelineFeature::Origin;
    case hobbycad::FeatureType::Sketch:    return TimelineFeature::Sketch;
    case hobbycad::FeatureType::Extrude:   return TimelineFeature::Extrude;
    case hobbycad::FeatureType::Revolve:   return TimelineFeature::Revolve;
    case hobbycad::FeatureType::Fillet:    return TimelineFeature::Fillet;
    case hobbycad::FeatureType::Chamfer:   return TimelineFeature::Chamfer;
    case hobbycad::FeatureType::Hole:      return TimelineFeature::Hole;
    case hobbycad::FeatureType::Mirror:    return TimelineFeature::Mirror;
    case hobbycad::FeatureType::Pattern:   return TimelineFeature::Pattern;
    case hobbycad::FeatureType::Box:       return TimelineFeature::Box;
    case hobbycad::FeatureType::Cylinder:  return TimelineFeature::Cylinder;
    case hobbycad::FeatureType::Sphere:    return TimelineFeature::Sphere;
    case hobbycad::FeatureType::Move:      return TimelineFeature::Move;
    case hobbycad::FeatureType::Join:      return TimelineFeature::Join;
    case hobbycad::FeatureType::Cut:       return TimelineFeature::Cut;
    case hobbycad::FeatureType::Intersect: return TimelineFeature::Intersect;
    }
    return TimelineFeature::Sketch;
}

}  // namespace

void MainWindow::connectTimeline()
{
    if (!m_timeline) return;

    connect(m_timeline, &TimelineWidget::itemClicked,
            this, [this](int index) { showFeatureProperties(index); });
    connect(m_timeline, &TimelineWidget::editFeatureRequested, this, &MainWindow::onEditFeature);
    connect(m_timeline, &TimelineWidget::itemDoubleClicked, this, &MainWindow::onEditFeature);
    connect(m_timeline, &TimelineWidget::renameFeatureRequested, this, &MainWindow::onRenameFeature);
    connect(m_timeline, &TimelineWidget::deleteFeatureRequested, this, &MainWindow::onDeleteFeature);
    connect(m_timeline, &TimelineWidget::suppressFeatureRequested, this, &MainWindow::onSuppressFeature);
    connect(m_timeline, &TimelineWidget::exportDXFRequested, this, &MainWindow::onExportSketchDXF);
    connect(m_timeline, &TimelineWidget::exportSVGRequested, this, &MainWindow::onExportSketchSVG);
    connect(m_timeline, &TimelineWidget::itemMoved, this, &MainWindow::onFeatureMoved);
    connect(m_timeline, &TimelineWidget::rollbackChanged, this, &MainWindow::onRollbackChanged);

    refreshTimeline();
}

void MainWindow::refreshTimeline()
{
    if (!m_timeline) return;

    // Keep the marker and the selection by feature id: positions move when
    // the history changes.
    const int count = m_timeline->itemCount();
    const int rollback = m_timeline->rollbackPosition();
    const int rollbackId = (rollback >= 0 && rollback < count) ? m_timeline->featureIdAt(rollback) : -1;
    const int selected = m_timeline->selectedIndex();
    const bool hadSelection = selected >= 0 && selected < count;
    const int selectedId = hadSelection ? m_timeline->featureIdAt(selected) : -1;

    const QSignalBlocker block(m_timeline);
    m_timeline->clear();
    m_timeline->addItem(TimelineFeature::Origin, tr("Origin"));
    m_timeline->setFeatureId(0, 0);   // the Origin is feature 0

    for (const hobbycad::TimelineEntry& e : m_session.timeline()) {
        m_timeline->addItem(timelineFeatureFor(e.type), QString::fromStdString(e.name));
        const int at = m_timeline->itemCount() - 1;
        m_timeline->setFeatureId(at, e.featureId);
        m_timeline->setDependencies(at, QVector<int>(e.dependsOn.begin(), e.dependsOn.end()));
        if (e.suppressed) m_timeline->setFeatureSuppressed(at, true);
    }
    if (rollbackId >= 0) {
        const int at = m_timeline->indexOfFeatureId(rollbackId);
        if (at >= 0 && at < m_timeline->itemCount() - 1) m_timeline->setRollbackPosition(at);
    }

    // The sketch being drawn is not in the project yet, but it belongs on the
    // timeline where it will land.
    int draftAt = -1;
    if (m_inSketchMode && m_draftOpen) {
        draftAt = m_draft.isNew()
            ? m_timeline->addItemAtRollback(TimelineFeature::Sketch,
                                            QString::fromStdString(m_draft.sketch.name))
            : m_timeline->indexOfFeatureId(m_draft.editingId);
    }

    if (draftAt >= 0) {
        m_timeline->setSelectedIndex(draftAt);
    } else if (hadSelection) {
        m_timeline->setSelectedIndex(m_timeline->indexOfFeatureId(selectedId));
    }
}

hobbycad::SketchDraft MainWindow::draftFromCanvas() const
{
    hobbycad::SketchDraft d = m_draft;
    if (!m_sketchCanvas) return d;
    d.sketch.plane = m_sketchCanvas->sketchPlane();
    // SketchEntity and SketchConstraint derive from the library types with
    // canvas-only fields added; the draft keeps the library part.
    d.sketch.entities.assign(m_sketchCanvas->entities().begin(), m_sketchCanvas->entities().end());
    d.sketch.constraints.assign(m_sketchCanvas->constraints().begin(), m_sketchCanvas->constraints().end());
    d.sketch.groups.assign(m_sketchCanvas->groups().begin(), m_sketchCanvas->groups().end());
    d.sketch.flipView = m_sketchCanvas->isFlipped();
    d.sketch.gridSpacing = m_sketchCanvas->gridSpacing();
    d.sketch.backgroundImage = m_sketchCanvas->backgroundImage();
    return d;
}

void MainWindow::showSketchProperties()
{
    QTreeWidget* propsTree = propertiesTree();
    if (!propsTree || !m_sketchCanvas) return;

    propsTree->clear();
    addSketchHeaderRows(propsTree, QString::fromStdString(m_draft.sketch.name),
                        m_sketchCanvas->sketchPlane());
    addPropertyRow(propsTree, tr("Offset"), tr("%1 mm").arg(m_draft.sketch.planeOffset, 0, 'g', 6));
    addPropertyRow(propsTree, tr("Entities"), QString::number(m_sketchCanvas->entities().size()));
    propsTree->expandAll();
}

void MainWindow::loadParametersFromProject()
{
    m_parameters.clear();
    for (const auto& paramData : m_project.parameters()) {
        Parameter param;
        param.name = paramData.name;
        param.expression = paramData.expression;
        param.value = paramData.value;
        param.unit = paramData.unit;
        param.comment = paramData.comment;
        m_parameters.append(param);
    }
    if (m_parameters.isEmpty()) {
        initDefaultParameters();
    }
}

void MainWindow::syncDocumentFromProject()
{
    const bool modified = m_document.isModified();
    m_document.setBodies(m_project.bodies());
    m_document.setModified(modified);
}

bool MainWindow::validateFeatureAction(int index, const QString& actionVerb) const
{
    if (!m_timeline || index < 0 || index >= m_timeline->itemCount()) return false;
    if (m_timeline->featureAt(index) == TimelineFeature::Origin) {
        statusBar()->showMessage(tr("Origin cannot be %1").arg(actionVerb), 3000);
        return false;
    }
    return true;
}

int MainWindow::selectedTimelineSketchId(const QString& operationName)
{
    const int index = m_timeline ? m_timeline->selectedIndex() : -1;
    if (index < 0) {
        QMessageBox::information(this, operationName,
            tr("Please select a sketch in the timeline first."));
        return -1;
    }
    if (m_timeline->featureAt(index) != TimelineFeature::Sketch) {
        QMessageBox::information(this, operationName,
            tr("Please select a sketch to %1.").arg(operationName.toLower()));
        return -1;
    }
    const int id = m_timeline->featureIdAt(index);
    const SketchData* sk = m_session.sketchById(id);
    if (!sk) {
        QMessageBox::warning(this, operationName, tr("Could not find sketch data."));
        return -1;
    }
    sketch::ProfileDetectionOptions options;
    options.excludeConstruction = true;
    if (sketch::detectProfiles(sk->entities, options).empty()) {
        QMessageBox::warning(this, operationName,
            tr("No closed profiles found in the sketch.\n"
               "Make sure the sketch contains a closed loop."));
        return -1;
    }
    return id;
}

int MainWindow::sketchCountOnPlane(SketchPlane plane) const
{
    int n = 0;
    for (const SketchData& s : m_project.sketches()) {
        if (s.plane == plane && qFuzzyIsNull(s.planeOffset)) ++n;
    }
    return n;
}

void MainWindow::onDocumentLoaded()
{
    // Sketches written before they carried feature records get one, so each
    // appears on the timeline once and can be moved and suppressed.
    m_session.adoptOrphanSketches();
    if (!m_project.isNew()) {
        setUnitsFromString(QString::fromStdString(m_project.units()));
        if (m_sketchCanvas) m_sketchCanvas->setUnitSuffix(unitSuffix());
    }
    loadParametersFromProject();
    syncDocumentFromProject();
    refreshTimeline();
    rebuildObjectsTree();
    refreshModelViews();
}

void MainWindow::onDocumentClosed()
{
    m_draftOpen = false;
    m_draft = hobbycad::SketchDraft{};
    initDefaultParameters();
    refreshTimeline();
    rebuildObjectsTree();
    refreshModelViews();
}

bool MainWindow::handleNodeRenamed(hobbycad::NodeType type, int id, const QString& name)
{
    using hobbycad::NodeType;

    if (type == NodeType::Sketch) {
        if (!m_session.sketchById(id)) return false;
        m_session.renameFeature(id, name.toStdString(), tr("Rename to %1").arg(name).toStdString());
        refreshTimeline();
        updateUndoActions();
        return true;
    }

    if (type == NodeType::Body) {
        if (!m_session.renameBody(id, name.toStdString(), tr("Rename to %1").arg(name).toStdString())) {
            return false;
        }
        syncDocumentFromProject();
        updateUndoActions();
        return true;
    }

    if (type == NodeType::ConstructionPlane) {
        const auto& planes = m_project.constructionPlanes();
        for (size_t i = 0; i < planes.size(); ++i) {
            if (planes[i].id == id) {
                ConstructionPlaneData updated = planes[i];
                updated.name = name.toStdString();
                m_project.setConstructionPlane(static_cast<int>(i), updated);
                return true;
            }
        }
        return false;
    }

    return false;   // origin geometry and folders are not renameable
}

// ---- Timeline actions ---------------------------------------------------

void MainWindow::onEditFeature(int index)
{
    if (!validateFeatureAction(index, tr("edited"))) return;
    if (m_timeline->featureAt(index) != TimelineFeature::Sketch) {
        statusBar()->showMessage(
            tr("Editing %1 features not yet implemented").arg(m_timeline->nameAt(index)), 3000);
        return;
    }
    if (m_inSketchMode) return;

    hobbycad::SketchDraft draft;
    if (!m_session.beginEditSketch(m_timeline->featureIdAt(index), draft)) return;
    m_draft = draft;
    m_draftOpen = true;

    // ORDER MATTERS: enterSketchMode() clears the canvas, so the contents go
    // in after it. Loading them first once opened every edit on an empty canvas.
    enterSketchMode(draft.sketch.plane);
    m_sketchCanvas->setSketchContents(
        QVector<SketchEntity>(draft.sketch.entities.begin(), draft.sketch.entities.end()),
        QVector<SketchConstraint>(draft.sketch.constraints.begin(), draft.sketch.constraints.end()),
        QVector<SketchGroup>(draft.sketch.groups.begin(), draft.sketch.groups.end()));
    m_sketchCanvas->setFlipView(draft.sketch.flipView);
    showSketchProperties();
}

void MainWindow::onRenameFeature(int index)
{
    if (!validateFeatureAction(index, tr("renamed"))) return;

    const QString currentName = m_timeline->nameAt(index);
    bool ok = false;
    const QString newName = QInputDialog::getText(this, tr("Rename Feature"), tr("New name:"),
                                                  QLineEdit::Normal, currentName, &ok);
    if (!ok || newName.isEmpty() || newName == currentName) return;

    // By feature id, so a sketch's record and the sketch rename together.
    m_session.renameFeature(m_timeline->featureIdAt(index), newName.toStdString(),
                            tr("Rename to %1").arg(newName).toStdString());
    onDocumentRecipeChanged();
    updateUndoActions();
    statusBar()->showMessage(tr("Renamed to '%1'").arg(newName), 3000);
}

void MainWindow::onDeleteFeature(int index)
{
    if (!validateFeatureAction(index, tr("deleted"))) return;

    const QString featureName = m_timeline->nameAt(index);
    if (m_timeline->featureAt(index) != TimelineFeature::Sketch) {
        // Deleting a 3D feature would have to take its body with it.
        statusBar()->showMessage(tr("%1 is not implemented yet").arg(tr("Delete")), 4000);
        return;
    }

    const QMessageBox::StandardButton reply = QMessageBox::question(this,
        tr("Delete Feature"),
        tr("Are you sure you want to delete '%1'?").arg(featureName),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (reply != QMessageBox::Yes) return;

    if (m_session.deleteFeature(m_timeline->featureIdAt(index),
                                tr("Delete %1").arg(featureName).toStdString())) {
        onDocumentRecipeChanged();
        updateUndoActions();
        statusBar()->showMessage(tr("Deleted '%1'").arg(featureName), 3000);
    }
}

void MainWindow::onSuppressFeature(int index, bool suppress)
{
    if (!validateFeatureAction(index, tr("suppressed"))) return;

    const QString featureName = m_timeline->nameAt(index);
    // Suppression is part of the recipe: it changes what a recompute would
    // evaluate, so it goes in the history like any other edit.
    m_session.setFeatureSuppressed(m_timeline->featureIdAt(index), suppress,
        (suppress ? tr("Suppress %1") : tr("Unsuppress %1")).arg(featureName).toStdString());
    onDocumentRecipeChanged();
    updateUndoActions();
    statusBar()->showMessage(
        (suppress ? tr("Suppressed '%1'") : tr("Unsuppressed '%1'")).arg(featureName), 3000);
}

void MainWindow::onFeatureMoved(int fromIndex, int toIndex)
{
    // The widget has moved the item already. The session decides whether the
    // history agrees, and the timeline is rebuilt from it either way, so a
    // move it refuses snaps back.
    if (!m_timeline || fromIndex < 0 || toIndex < 0 || toIndex >= m_timeline->itemCount()) return;
    const int id = m_timeline->featureIdAt(toIndex);
    const QString name = m_timeline->nameAt(toIndex);
    if (m_session.moveFeature(id, toIndex - 1,                  // the Origin is item 0
                              tr("Moved '%1'").arg(name).toStdString())) {
        statusBar()->showMessage(tr("Moved '%1'").arg(name), 3000);
    }
    onDocumentRecipeChanged();
    updateUndoActions();
}

void MainWindow::onRollbackChanged(int index)
{
    refreshModelViews();
    if (index < 0) {
        statusBar()->showMessage(tr("Rollback cleared - all features active"), 3000);
    } else if (m_timeline) {
        statusBar()->showMessage(tr("Rolled back to '%1'").arg(m_timeline->nameAt(index)), 3000);
    }
}

void MainWindow::onExportSketchDXF(int index)
{
    if (!m_timeline || index < 0 || index >= m_timeline->itemCount()
        || m_timeline->featureAt(index) != TimelineFeature::Sketch) return;
    const SketchData* stored = m_session.sketchById(m_timeline->featureIdAt(index));
    if (!stored) return;
    const SketchData sk = *stored;   // the dialog below runs an event loop
    const QString name = QString::fromStdString(sk.name);

    if (sk.entities.empty()) {
        QMessageBox::information(this, tr("Export DXF"), tr("The sketch '%1' is empty.").arg(name));
        return;
    }
    QString filePath = QFileDialog::getSaveFileName(this, tr("Export DXF File"),
        name + QStringLiteral(".dxf"), tr("DXF Files (*.dxf);;All Files (*)"));
    if (filePath.isEmpty()) return;
    if (!filePath.toLower().endsWith(QLatin1String(".dxf"))) filePath += QStringLiteral(".dxf");

    sketch::DXFExportOptions options;
    options.extrusion = hobbycad::planeBasisFor(sk.plane, sk.planeOffset).normal;  // DXF OCS: preserve the plane
    if (!sketch::exportSketchToDXF(sk.entities, filePath.toStdString(), options)) {
        QMessageBox::critical(this, tr("Export Failed"), tr("Failed to export DXF file."));
        return;
    }
    statusBar()->showMessage(tr("Exported '%1' (%2 entities) to DXF file")
                                 .arg(name).arg(sk.entities.size()), 5000);
}

void MainWindow::onExportSketchSVG(int index)
{
    if (!m_timeline || index < 0 || index >= m_timeline->itemCount()
        || m_timeline->featureAt(index) != TimelineFeature::Sketch) return;
    const SketchData* stored = m_session.sketchById(m_timeline->featureIdAt(index));
    if (!stored) return;
    const SketchData sk = *stored;
    const QString name = QString::fromStdString(sk.name);

    if (sk.entities.empty()) {
        QMessageBox::information(this, tr("Export SVG"), tr("The sketch '%1' is empty.").arg(name));
        return;
    }
    QString filePath = QFileDialog::getSaveFileName(this, tr("Export SVG File"),
        name + QStringLiteral(".svg"), tr("SVG Files (*.svg);;All Files (*)"));
    if (filePath.isEmpty()) return;
    if (!filePath.toLower().endsWith(QLatin1String(".svg"))) filePath += QStringLiteral(".svg");

    // The stored sketch keeps its constraints, so dimensions export too; the
    // old copy of the sketch list did not carry them.
    sketch::SVGExportOptions options;
    if (!sketch::exportSketchToSVG(sk.entities, sk.constraints, filePath.toStdString(), options)) {
        QMessageBox::critical(this, tr("Export Failed"), tr("Failed to export SVG file."));
        return;
    }
    statusBar()->showMessage(tr("Exported '%1' (%2 entities) to SVG file")
                                 .arg(name).arg(sk.entities.size()), 5000);
}


}  // namespace hobbycad

