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
//  The toolbar and timeline are still available for feature editing,
//  even though the 3D preview is not functional.
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_REDUCEDMODEWINDOW_H
#define HOBBYCAD_REDUCEDMODEWINDOW_H

#include "gui/mainwindow.h"
#include "gui/parametersdialog.h"
#include "gui/sketchcanvas.h"

#include <QList>
#include <QStackedWidget>

class QSplitter;
class QTreeWidgetItem;
class QVBoxLayout;

namespace hobbycad {

class CliPanel;
class ReducedViewport;

class ReducedModeWindow : public MainWindow {
    Q_OBJECT

public:
    explicit ReducedModeWindow(const OpenGLInfo& glInfo,
                               QWidget* parent = nullptr);

public slots:
    void enterSketchMode(SketchPlane plane = SketchPlane::XY) override;
    void exitSketchMode() override;

protected:
    void applyPreferences() override;

private slots:
    void onViewportClicked();
    void onTerminalToggled(bool visible);
    void onConstraintSelectionChanged(int constraintId);

private:
    // Overrides from MainWindow
    void onCreateSketchClicked() override;
    void saveCurrentSketch() override;
    void discardCurrentSketch() override;

    void showDiagnosticDialog();
    void createTimeline();

    // Main container layout
    QVBoxLayout*     m_mainLayout      = nullptr;

    QSplitter*       m_splitter        = nullptr;
    ReducedViewport* m_viewport        = nullptr;
    CliPanel*        m_centralCli      = nullptr;
    bool             m_suppressDialog  = false;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_REDUCEDMODEWINDOW_H

