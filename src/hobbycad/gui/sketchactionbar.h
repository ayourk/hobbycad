// =====================================================================
//  src/hobbycad/gui/sketchactionbar.h — Sketch action bar
// =====================================================================
//
//  A widget with Finish Sketch button, and Save/Discard buttons that
//  appear when Finish is clicked. Displayed at the bottom of the
//  properties panel during sketch mode.
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_SKETCHACTIONBAR_H
#define HOBBYCAD_SKETCHACTIONBAR_H

#include <QWidget>

class QPushButton;

namespace hobbycad {

class SketchActionBar : public QWidget {
    Q_OBJECT

public:
    explicit SketchActionBar(QWidget* parent = nullptr);

    /// Reset to initial state (Finish button visible, Save/Discard hidden)
    void reset();

    /// Enable/disable the Save button
    void setSaveEnabled(bool enabled);

    /// Set whether the sketch has unsaved changes
    void setModified(bool modified);
    bool isModified() const { return m_modified; }

signals:
    /// Emitted when the user clicks Save
    void saveClicked();

    /// Emitted when the user clicks Discard
    void discardClicked();

private slots:
    void onFinishClicked();

private:
    QPushButton* m_finishButton = nullptr;
    QWidget* m_saveDiscardWidget = nullptr;
    QPushButton* m_saveButton = nullptr;
    QPushButton* m_discardButton = nullptr;
    bool m_modified = false;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_SKETCHACTIONBAR_H
