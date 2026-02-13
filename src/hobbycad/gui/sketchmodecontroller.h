// =====================================================================
//  src/hobbycad/gui/sketchmodecontroller.h — Sketch mode controller
// =====================================================================
//
//  Manages sketch mode state and interactions, used by both FullModeWindow
//  and ReducedModeWindow to avoid code duplication.
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_SKETCHMODECONTROLLER_H
#define HOBBYCAD_SKETCHMODECONTROLLER_H

#include "sketchcanvas.h"

#include <QObject>

class QStatusBar;
class QTreeWidget;

namespace hobbycad {

class SketchActionBar;
class SketchCanvas;
class SketchToolbar;
class TimelineWidget;

/// Controller for sketch mode, managing the interaction between
/// sketch canvas, toolbar, properties panel, and timeline.
class SketchModeController : public QObject {
    Q_OBJECT

public:
    explicit SketchModeController(QObject* parent = nullptr);

    /// Set the sketch canvas to control
    void setSketchCanvas(SketchCanvas* canvas);

    /// Set the sketch toolbar
    void setSketchToolbar(SketchToolbar* toolbar);

    /// Set the timeline widget
    void setTimeline(TimelineWidget* timeline);

    /// Set the properties tree widget
    void setPropertiesTree(QTreeWidget* tree);

    /// Set the sketch action bar (Save/Cancel)
    void setSketchActionBar(SketchActionBar* actionBar);

    /// Set the status bar for messages
    void setStatusBar(QStatusBar* statusBar);

    /// Set the unit suffix (e.g., "mm")
    void setUnitSuffix(const QString& units);

    /// Check if sketch mode is active
    bool isActive() const { return m_active; }

    /// Get the current sketch plane
    SketchPlane sketchPlane() const;

public slots:
    /// Enter sketch mode on the specified plane
    void enter(SketchPlane plane = SketchPlane::XY);

    /// Exit sketch mode
    void exit();

    /// Save the current sketch and exit
    void save();

    /// Discard changes and exit
    void discard();

signals:
    /// Emitted when sketch mode is entered
    void entered(SketchPlane plane);

    /// Emitted when sketch mode is exited
    void exited();

    /// Emitted when the user should switch UI to sketch mode
    void showSketchUI();

    /// Emitted when the user should switch UI back to normal mode
    void showNormalUI();

private slots:
    void onToolSelected(SketchTool tool);
    void onSelectionChanged(int entityId);
    void onEntityCreated(int entityId);
    void onFinishRequested();
    void onSaveClicked();
    void onCancelClicked();

private:
    void updatePropertiesForSketch(const QString& sketchName, SketchPlane plane);
    void showEntityProperties(int entityId);
    void updateEntityCount();

    SketchCanvas*    m_canvas     = nullptr;
    SketchToolbar*   m_toolbar    = nullptr;
    TimelineWidget*  m_timeline   = nullptr;
    QTreeWidget*     m_propsTree  = nullptr;
    SketchActionBar* m_actionBar  = nullptr;
    QStatusBar*      m_statusBar  = nullptr;

    QString m_unitSuffix;
    bool    m_active = false;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_SKETCHMODECONTROLLER_H
