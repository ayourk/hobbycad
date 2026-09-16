// =====================================================================
//  src/hobbycad/gui/reduced/reducedmodewindow.cpp — Reduced Mode window
// =====================================================================

#include "reducedmodewindow.h"
#include "../propertyrow.h"
#include "gui/sketchutils.h"
#include "reducedviewport.h"
#include "diagnosticdialog.h"
#include "gui/changelogpanel.h"
#include "gui/clipanel.h"
#include "cli/cliengine.h"
#include "gui/modeltoolbar.h"
#include "gui/toolbarbutton.h"
#include "gui/toolbardropdown.h"
#include "gui/timelinewidget.h"
#include "gui/formulafield.h"
#include "gui/sketchtoolbar.h"
#include "gui/sketchcanvas.h"
#include "gui/sketchplanedialog.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtMath>
#include <cmath>

namespace hobbycad {

ReducedModeWindow::ReducedModeWindow(const OpenGLInfo& glInfo,
                                     QWidget* parent)
    : MainWindow(glInfo, parent)
{
    setObjectName(QStringLiteral("ReducedModeWindow"));

    // Create central widget container with toolbar + viewport + timeline
    auto* container = new QWidget(this);
    m_mainLayout = new QVBoxLayout(container);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);

    // Toolbar stack (normal toolbar vs sketch toolbar)
    m_toolbarStack = new QStackedWidget(container);

    m_toolbar = new ModelToolbar(m_toolbarStack);
    m_toolbarStack->addWidget(m_toolbar);

    m_sketchToolbar = new SketchToolbar(m_toolbarStack);
    m_toolbarStack->addWidget(m_sketchToolbar);

    m_mainLayout->addWidget(m_toolbarStack);

    // Connect View > Toolbar toggle
    if (toolbarToggleAction()) {
        connect(toolbarToggleAction(), &QAction::toggled,
                m_toolbarStack, &QWidget::setVisible);
    }

    // Viewport stack (normal splitter vs sketch canvas)
    m_viewportStack = new QStackedWidget(container);

    // Normal mode: vertical splitter with viewport + CLI
    m_splitter = new QSplitter(Qt::Vertical, m_viewportStack);

    m_viewport = new ReducedViewport(m_splitter);
    m_splitter->addWidget(m_viewport);

    m_centralCli = new CliPanel(m_splitter);
    m_splitter->addWidget(m_centralCli);

    // The terminal edits the project this window shows, into the same history
    // as the Edit menu. It was never given a host, so Reduced mode's own
    // terminal could not touch the model at all; it is how this mode reaches
    // the 3D model it cannot draw (Aaron, 2026-09-15: "In reduced mode, The
    // GUI CLI window should be able to manipulate the 3D Stuff that reduced
    // mode can't.").
    if (m_centralCli->engine()) {
        m_centralCli->engine()->setUndoHost(this);
        m_centralCli->engine()->setDocumentHost(this);
    }
    m_centralCli->setGuiMode(true);

    // Exit command in the central CLI panel closes the app
    connect(m_centralCli, &CliPanel::exitRequested,
            this, &QWidget::close);

    // Give most space to the CLI panel
    m_splitter->setStretchFactor(0, 1);   // viewport: small
    m_splitter->setStretchFactor(1, 3);   // CLI: large

    m_viewportStack->addWidget(m_splitter);

    // Sketch mode: 2D canvas
    m_sketchCanvas = new SketchCanvas(m_viewportStack);

    installSketchCanvasResolvers();   // projection resolver and source lister (MainWindow)

    m_sketchCanvas->setUnitSuffix(unitSuffix());
    m_viewportStack->addWidget(m_sketchCanvas);

    m_mainLayout->addWidget(m_viewportStack, 1);  // stretch factor 1

    // Connect sketch canvas constraint selection (ReducedMode-specific)
    // Wire Edit > Cut/Copy/Paste to the sketch canvas.
    connectClipboardActions();

    connect(m_sketchCanvas, &SketchCanvas::constraintSelectionChanged,
            this, &ReducedModeWindow::onConstraintSelectionChanged);

    // Timeline below the viewport stack
    m_timeline = new TimelineWidget(container);
    m_mainLayout->addWidget(m_timeline);
    connectTimeline();   // the project's history, as in Full mode

    setCentralWidget(container);
    finalizeLayout();

    // Initialize shared sketch signal/slot connections from base class
    initSketchConnections();

    // Initialize default parameters
    initDefaultParameters();

    connect(m_viewport, &ReducedViewport::viewportClicked,
            this, &ReducedModeWindow::onViewportClicked);

    // Hook into the View > Terminal toggle from MainWindow.
    // In Reduced Mode, toggling the terminal shows/hides the
    // central CLI panel (not the dock; we hide the dock entirely
    // since the central panel serves that role).
    if (auto* action = findChild<QAction*>(
            QString(), Qt::FindDirectChildrenOnly)) {
        // We need the specific toggle action; use the one we stored
    }

    // Connect to the toggle action created in MainWindow::createMenus()
    // The action is m_actionToggleTerminal, accessible via the menu.
    // We override the dock behavior: in Reduced Mode, toggle controls
    // the central CLI panel instead.
    connect(terminalToggleAction(), &QAction::toggled,
            this, &ReducedModeWindow::onTerminalToggled);

    // Start with terminal visible and action checked
    terminalToggleAction()->setChecked(true);

    // Hide the dock-based terminal, not needed in Reduced Mode
    // since we have the central one
    hideDockTerminal();

    // Show the diagnostic dialog on first launch
    showDiagnosticDialog();

    // Focus the CLI panel
    m_centralCli->focusInput();
}

void ReducedModeWindow::onTerminalToggled(bool visible)
{
    m_centralCli->setVisible(visible);
    if (visible) {
        m_centralCli->focusInput();
    }
}

void ReducedModeWindow::applyPreferences()
{
    // Call base class to apply standard bindings
    MainWindow::applyPreferences();

    // Reload sketch canvas key bindings
    if (m_sketchCanvas) {
        m_sketchCanvas->reloadBindings();
    }
}

void ReducedModeWindow::onViewportClicked()
{
    showDiagnosticDialog();
}

void ReducedModeWindow::showDiagnosticDialog()
{
    if (m_suppressDialog) {
        QApplication::beep();
        return;
    }

    DiagnosticDialog dlg(m_glInfo, this);
    int result = dlg.exec();

    if (result == 2) {
        QApplication::quit();
        return;
    }

    if (dlg.dontShowAgain()) {
        m_suppressDialog = true;
        m_viewport->setSuppressDialog(true);
    }
}

void ReducedModeWindow::exitSketchMode()
{
    MainWindow::exitSketchMode();

    // Switch back to normal viewport (splitter with disabled viewport + CLI)
    m_viewportStack->setCurrentWidget(m_splitter);

    // Focus CLI
    m_centralCli->focusInput();
}

void ReducedModeWindow::onConstraintSelectionChanged(int constraintId)
{
    if (constraintId < 0) {
        // Constraint deselected: back to the sketch's own page. This used to
        // call enterSketchMode() again, which added a second timeline item.
        if (m_inSketchMode) {
            showSketchProperties();
        }
        return;
    }
    showSketchConstraintProperties(constraintId);
}

}  // namespace hobbycad

