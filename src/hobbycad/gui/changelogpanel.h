// =====================================================================
//  src/hobbycad/gui/changelogpanel.h — Undo history panel
// =====================================================================
//
//  Displays the undo/redo history stack as a list.  The most recent
//  operation appears at the top (bold + highlighted).  Undone entries
//  appear grayed out below.  Clicking an entry navigates the undo
//  stack to that point via undoMultiple() / redoMultiple().
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_CHANGELOGPANEL_H
#define HOBBYCAD_CHANGELOGPANEL_H

#include <QWidget>

class QListWidget;
class QLabel;

namespace hobbycad {

class SketchCanvas;

class ChangelogPanel : public QWidget {
    Q_OBJECT

public:
    explicit ChangelogPanel(QWidget* parent = nullptr);

    /// Connect to a sketch canvas to track its undo stack
    void setSketchCanvas(SketchCanvas* canvas);

    /// Disconnect from the current sketch canvas
    void clearSketchCanvas();

public slots:
    /// Refresh the list from the connected canvas's undo stack
    void refresh();

private slots:
    void onItemClicked(int row);

private:
    void buildList();

    QListWidget* m_listWidget  = nullptr;
    QLabel*      m_emptyLabel  = nullptr;
    SketchCanvas* m_canvas     = nullptr;

    int m_undoCount = 0;   ///< Number of undo entries (top of list)
};

}  // namespace hobbycad

#endif  // HOBBYCAD_CHANGELOGPANEL_H
