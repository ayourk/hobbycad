// =====================================================================
//  src/hobbycad/gui/erroroutlinedelegate.h — editable-cell outlining
//  Repository: ayourk/hobbycad
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//
//  Aaron, 2026-08-27: "black outline = all things good; red outline =
//  there are problems."
//
//  The outline is drawn on every EDITABLE cell, always: dark when the
//  value is fine, red when it is not. Drawing it only on failure reads
//  as a rendering glitch the first time a user sees it, while a border
//  that is always present and merely changes color is understood
//  without explanation. Cells that cannot be edited get none, because
//  outlining one would promise an edit that is not on offer.
//
#ifndef HOBBYCAD_ERROROUTLINEDELEGATE_H
#define HOBBYCAD_ERROROUTLINEDELEGATE_H

#include <QColor>
#include <QLineEdit>
#include <QModelIndex>
#include <QPainter>
#include <QPen>
#include <QRect>
#include <QStyledItemDelegate>

#include <functional>

namespace hobbycad {

/// The one place the outline's look is decided.
///
/// Three surfaces draw it (the parameters table, the objects browser and
/// the formula field), and they must agree, or "red means a problem"
/// becomes "red means a problem in some panels".
namespace outline {

inline QColor okPen()      { return QColor(60, 60, 60); }
inline QColor errorPen()   { return QColor(200, 40, 40); }
inline int    okWidth()    { return 1; }
inline int    errorWidth() { return 2; }

/// Stroke the outline inside @p rect. Inset so it lands within the cell
/// rather than being clipped in half by a grid line or a frame.
inline void draw(QPainter* painter, const QRect& rect, bool bad)
{
    painter->save();
    QPen pen(bad ? errorPen() : okPen());
    pen.setWidth(bad ? errorWidth() : okWidth());
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(rect.adjusted(1, 1, -2, -2));
    painter->restore();
}

}  // namespace outline

/// Outlines editable cells, and remembers where the caret was when an
/// edit was committed.
class ErrorOutlineDelegate : public QStyledItemDelegate {
public:
    /// Returns true when the cell at this index is flagged as bad.
    ///
    /// Keyed on the index rather than a row/column pair: in a tree, a row
    /// number is only meaningful relative to a parent, so (row, column)
    /// cannot identify a node.
    using Predicate = std::function<bool(const QModelIndex&)>;

    ErrorOutlineDelegate(Predicate isError, QObject* parent)
        : QStyledItemDelegate(parent), m_isError(std::move(isError)) {}

    /// Caret offset at the moment the last edit was committed, or -1.
    ///
    /// Recorded so a REJECTED edit can put the caret back exactly where it
    /// was. Pressing Enter mid-word and being bounced to the end of the
    /// field (or worse, having everything selected so the next keystroke
    /// wipes it) loses the user's place for no reason.
    int lastCursorPosition() const { return m_lastCursor; }

    void setModelData(QWidget* editor,
                      QAbstractItemModel* model,
                      const QModelIndex& index) const override
    {
        // Read the caret BEFORE the base class commits and the editor is
        // torn down; afterwards there is nothing left to ask.
        if (const auto* line = qobject_cast<const QLineEdit*>(editor)) {
            m_lastCursor = line->cursorPosition();
        }
        QStyledItemDelegate::setModelData(editor, model, index);
    }

    void paint(QPainter* painter,
               const QStyleOptionViewItem& option,
               const QModelIndex& index) const override
    {
        QStyledItemDelegate::paint(painter, option, index);
        if (!index.isValid() || !(index.flags() & Qt::ItemIsEditable)) {
            return;
        }

        outline::draw(painter, option.rect, m_isError && m_isError(index));
    }

private:
    Predicate m_isError;
    mutable int m_lastCursor = -1;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_ERROROUTLINEDELEGATE_H
