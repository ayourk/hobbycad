// =====================================================================
//  src/hobbycad/gui/reduced/reducedviewport.h — Disabled viewport placeholder
// =====================================================================
//
//  Replaces the OpenGL viewport in Reduced Mode.  Shows an
//  informational message.  Clicking it re-shows the diagnostic
//  dialog or plays a system "ding" if the user has checked
//  "don't show again."
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_REDUCEDVIEWPORT_H
#define HOBBYCAD_REDUCEDVIEWPORT_H

#include <QFrame>

namespace hobbycad {

class ReducedViewport : public QFrame {
    Q_OBJECT

public:
    explicit ReducedViewport(QWidget* parent = nullptr);

    /// If true, clicks play a "ding" instead of showing the dialog.
    void setSuppressDialog(bool suppress);

signals:
    /// Emitted when the user clicks and the dialog is not suppressed.
    void viewportClicked();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    bool m_suppressDialog = false;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_REDUCEDVIEWPORT_H

