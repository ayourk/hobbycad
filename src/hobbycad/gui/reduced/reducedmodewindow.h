// =====================================================================
//  src/hobbycad/gui/reduced/reducedmodewindow.h — Reduced Mode window
// =====================================================================
//
//  In Reduced Mode the 3D viewport is disabled.  The central area
//  uses a QSplitter so the user can see the disabled viewport
//  placeholder and the CLI panel together, or collapse either one.
//
//  View > Terminal (Ctrl+`) toggles the CLI panel visibility within
//  the splitter.  When hidden, the disabled viewport fills the space.
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_REDUCEDMODEWINDOW_H
#define HOBBYCAD_REDUCEDMODEWINDOW_H

#include "gui/mainwindow.h"

class QSplitter;

namespace hobbycad {

class CliPanel;
class ReducedViewport;

class ReducedModeWindow : public MainWindow {
    Q_OBJECT

public:
    explicit ReducedModeWindow(const OpenGLInfo& glInfo,
                               QWidget* parent = nullptr);

private slots:
    void onViewportClicked();
    void onTerminalToggled(bool visible);

private:
    void showDiagnosticDialog();

    QSplitter*       m_splitter        = nullptr;
    ReducedViewport* m_viewport        = nullptr;
    CliPanel*        m_centralCli      = nullptr;
    bool             m_suppressDialog  = false;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_REDUCEDMODEWINDOW_H

