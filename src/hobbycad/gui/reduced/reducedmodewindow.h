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
class QVBoxLayout;

namespace hobbycad {

class CliPanel;
class ReducedViewport;
class SketchCanvas;
class SketchToolbar;
class TimelineWidget;
class ViewportToolbar;

class ReducedModeWindow : public MainWindow {
    Q_OBJECT

public:
    explicit ReducedModeWindow(const OpenGLInfo& glInfo,
                               QWidget* parent = nullptr);

    /// Get document parameters (for formula fields)
    QMap<QString, double> parameterValues() const;

    /// Check if currently in sketch mode
    bool isSketchMode() const { return m_inSketchMode; }

public slots:
    /// Enter sketch editing mode
    void enterSketchMode(SketchPlane plane = SketchPlane::XY);

    /// Exit sketch editing mode
    void exitSketchMode();

protected:
    void applyPreferences() override;

private slots:
    void onViewportClicked();
    void onTerminalToggled(bool visible);
    void showParametersDialog();
    void onParametersChanged(const QList<Parameter>& params);
    void showFeatureProperties(int index);
    void onCreateSketchClicked();
    void onSketchToolSelected(SketchTool tool);
    void onSketchSelectionChanged(int entityId);
    void onSketchEntityCreated(int entityId);

private:
    void showDiagnosticDialog();
    void createToolbar();
    void createTimeline();
    void initDefaultParameters();
    void showSketchEntityProperties(int entityId);
    void saveCurrentSketch();
    void discardCurrentSketch();

    // Main container layout
    QVBoxLayout*     m_mainLayout      = nullptr;

    // Toolbar stack (normal vs sketch mode)
    QStackedWidget*  m_toolbarStack    = nullptr;
    ViewportToolbar* m_toolbar         = nullptr;
    SketchToolbar*   m_sketchToolbar   = nullptr;

    // Viewport stack (reduced viewport vs sketch canvas)
    QStackedWidget*  m_viewportStack   = nullptr;
    QSplitter*       m_splitter        = nullptr;
    ReducedViewport* m_viewport        = nullptr;
    CliPanel*        m_centralCli      = nullptr;
    SketchCanvas*    m_sketchCanvas    = nullptr;

    TimelineWidget*  m_timeline        = nullptr;
    bool             m_suppressDialog  = false;
    bool             m_inSketchMode    = false;
    double           m_pendingSketchOffset = 0.0;  ///< Offset for sketch being created

    QList<Parameter> m_parameters;  ///< Document parameters
};

}  // namespace hobbycad

#endif  // HOBBYCAD_REDUCEDMODEWINDOW_H

