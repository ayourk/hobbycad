// =====================================================================
//  src/hobbycad/gui/reduced/reducedmodewindow.cpp — Reduced Mode window
// =====================================================================

#include "reducedmodewindow.h"
#include "reducedviewport.h"
#include "diagnosticdialog.h"
#include "gui/clipanel.h"

#include <QAction>
#include <QApplication>
#include <QSplitter>

namespace hobbycad {

ReducedModeWindow::ReducedModeWindow(const OpenGLInfo& glInfo,
                                     QWidget* parent)
    : MainWindow(glInfo, parent)
{
    setObjectName(QStringLiteral("ReducedModeWindow"));

    // Central area: vertical splitter
    //   top:    disabled viewport placeholder
    //   bottom: CLI panel (shown by default, togglable)
    m_splitter = new QSplitter(Qt::Vertical, this);

    m_viewport = new ReducedViewport(this);
    m_splitter->addWidget(m_viewport);

    m_centralCli = new CliPanel(this);
    m_splitter->addWidget(m_centralCli);

    // Exit command in the central CLI panel closes the app
    connect(m_centralCli, &CliPanel::exitRequested,
            this, &QWidget::close);

    // Give most space to the CLI panel
    m_splitter->setStretchFactor(0, 1);   // viewport: small
    m_splitter->setStretchFactor(1, 3);   // CLI: large

    setCentralWidget(m_splitter);
    finalizeLayout();

    connect(m_viewport, &ReducedViewport::viewportClicked,
            this, &ReducedModeWindow::onViewportClicked);

    // Hook into the View > Terminal toggle from MainWindow.
    // In Reduced Mode, toggling the terminal shows/hides the
    // central CLI panel (not the dock — we hide the dock entirely
    // since the central panel serves that role).
    if (auto* action = findChild<QAction*>(
            QString(), Qt::FindDirectChildrenOnly)) {
        // We need the specific toggle action — use the one we stored
    }

    // Connect to the toggle action created in MainWindow::createMenus()
    // The action is m_actionToggleTerminal, accessible via the menu.
    // We override the dock behavior: in Reduced Mode, toggle controls
    // the central CLI panel instead.
    connect(terminalToggleAction(), &QAction::toggled,
            this, &ReducedModeWindow::onTerminalToggled);

    // Start with terminal visible and action checked
    terminalToggleAction()->setChecked(true);

    // Hide the dock-based terminal — not needed in Reduced Mode
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

}  // namespace hobbycad

