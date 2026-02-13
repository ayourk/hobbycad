// =====================================================================
//  src/hobbycad/gui/full/fullmodewindow.h — Full Mode window (OpenGL 3.3+)
// =====================================================================

#ifndef HOBBYCAD_FULLMODEWINDOW_H
#define HOBBYCAD_FULLMODEWINDOW_H

#include "gui/mainwindow.h"
#include "gui/parametersdialog.h"
#include "gui/sketchcanvas.h"

#include <QList>

class QLabel;
class QStackedWidget;

namespace hobbycad {

class SketchCanvas;
class SketchToolbar;
class TimelineWidget;
class ViewportToolbar;
class ViewportWidget;

class FullModeWindow : public MainWindow {
    Q_OBJECT

public:
    explicit FullModeWindow(const OpenGLInfo& glInfo,
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
    void onDocumentLoaded() override;
    void onDocumentClosed() override;
    void applyPreferences() override;

private slots:
    void showParametersDialog();
    void onParametersChanged(const QList<Parameter>& params);
    void onCreateSketchClicked();
    void onSketchToolSelected(SketchTool tool);
    void onSketchSelectionChanged(int entityId);
    void onSketchEntityCreated(int entityId);

private:
    void createToolbar();
    void createTimeline();
    void displayShapes();
    void showFeatureProperties(int index);
    void initDefaultParameters();
    void showSketchEntityProperties(int entityId);
    void saveCurrentSketch();
    void discardCurrentSketch();

    // Toolbar stack (normal vs sketch mode)
    QStackedWidget*  m_toolbarStack  = nullptr;
    ViewportToolbar* m_toolbar       = nullptr;
    SketchToolbar*   m_sketchToolbar = nullptr;

    // Viewport stack (3D viewport vs sketch canvas)
    QStackedWidget*  m_viewportStack = nullptr;
    ViewportWidget*  m_viewport      = nullptr;
    SketchCanvas*    m_sketchCanvas  = nullptr;

    TimelineWidget*  m_timeline  = nullptr;
    QLabel*          m_axisLabel = nullptr;

    bool             m_inSketchMode = false;
    QList<Parameter> m_parameters;  ///< Document parameters
};

}  // namespace hobbycad

#endif  // HOBBYCAD_FULLMODEWINDOW_H

