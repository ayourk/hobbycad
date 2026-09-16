// =====================================================================
//  src/hobbycad/gui/editinplace.h — F2 rename-in-place
//  Repository: ayourk/hobbycad
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//
//  F2 starting an in-place edit is the platform convention users expect
//  from a file manager or a spreadsheet, and HobbyCAD had it in two
//  places already (the objects tree and the properties tree) as two
//  copies of the same six lines differing only in which column they
//  edit. This is that logic, once.
//
#ifndef HOBBYCAD_EDITINPLACE_H
#define HOBBYCAD_EDITINPLACE_H

#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QKeySequence>
#include <QAbstractAnimation>
#include <QLineEdit>
#include <QPoint>
#include <QPropertyAnimation>
#include <QShortcut>
#include <QTimer>

#include "erroroutlinedelegate.h"

namespace hobbycad {

/// The cell F2 should open an editor on, or an invalid index if none.
///
/// Split out from the shortcut so the decision can be tested without
/// synthesizing key events, which need a shown and focused window.
inline QModelIndex editTargetFor(const QAbstractItemView* view, int column)
{
    if (!view || !view->model()) {
        return {};
    }
    const QModelIndex current = view->currentIndex();
    if (!current.isValid()) {
        return {};
    }

    // Move to the label column: F2 pressed on any cell of a row should
    // edit that row's name, not whatever happened to be under the cursor.
    const QModelIndex target = current.sibling(current.row(), column);
    if (!target.isValid()) {
        return {};
    }
    if (!(view->model()->flags(target) & Qt::ItemIsEditable)) {
        return {};   // read-only: do nothing rather than open a dead editor
    }
    return target;
}

/// Make F2 begin an in-place edit of @p column on the current row.
///
/// Works for any item view, tree or table. The edit is refused when the
/// target cell is not editable, so read-only rows (origin geometry in the
/// objects tree, a computed value in a parameter table) simply do
/// nothing rather than opening an editor that discards what is typed.
///
/// @p column is the one that holds the label the user thinks they are
/// renaming, which is not always the column they have selected: pressing
/// F2 anywhere on a row should edit that row's name.
inline void bindEditInPlace(QAbstractItemView* view, int column)
{
    if (!view) {
        return;
    }

    auto* shortcut = new QShortcut(QKeySequence(Qt::Key_F2), view);
    QObject::connect(shortcut, &QShortcut::activated, view, [view, column]() {
        const QModelIndex target = editTargetFor(view, column);
        if (!target.isValid()) {
            return;
        }
        view->setCurrentIndex(target);
        view->edit(target);
    });
}

/// Nudge a widget left and right once, as a rejection cue.
inline void shakeWidget(QWidget* widget)
{
    if (!widget) {
        return;
    }
    const QPoint home = widget->pos();
    auto* shake = new QPropertyAnimation(widget, "pos", widget);
    shake->setDuration(220);
    shake->setKeyValueAt(0.00, home);
    shake->setKeyValueAt(0.20, home + QPoint(-5, 0));
    shake->setKeyValueAt(0.40, home + QPoint( 5, 0));
    shake->setKeyValueAt(0.60, home + QPoint(-3, 0));
    shake->setKeyValueAt(0.80, home + QPoint( 3, 0));
    shake->setKeyValueAt(1.00, home);
    shake->start(QAbstractAnimation::DeleteWhenStopped);
}

/// Put the user back where they were after a refused edit.
///
/// Reopens the editor on @p index, restores the caret to where it was when
/// they committed, selects nothing, and shakes the field.
///
/// The three properties together are what make a rejection read as a
/// non-event rather than as lost work:
///   - the text they typed stays, so a nearly-correct value is not thrown
///     away with no hint of which rule it broke;
///   - nothing is selected, so the next keystroke does not erase it;
///   - the caret is where it was, so pressing Enter mid-word does not
///     bounce the cursor to the end.
///
/// The caret offset comes from the delegate, which captures it in
/// setModelData(), the last moment the editor still exists.
inline void reopenRejectedEdit(QAbstractItemView* view,
                               const QModelIndex& index,
                               const ErrorOutlineDelegate* delegate)
{
    if (!view || !index.isValid()) {
        return;
    }
    const int caret = delegate ? delegate->lastCursorPosition() : -1;

    view->setCurrentIndex(index);
    view->edit(index);

    // The editor is created during edit(), so it only exists to adjust once
    // the event loop has returned. Queued rather than immediate.
    QTimer::singleShot(0, view, [view, caret]() {
        auto* editor = view->findChild<QLineEdit*>();
        if (!editor) {
            return;
        }
        editor->deselect();
        if (caret >= 0 && caret <= editor->text().length()) {
            editor->setCursorPosition(caret);
        }
        shakeWidget(editor);
    });
}

}  // namespace hobbycad

#endif  // HOBBYCAD_EDITINPLACE_H
