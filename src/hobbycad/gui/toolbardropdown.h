// =====================================================================
//  src/hobbycad/gui/toolbardropdown.h — Toolbar dropdown popup
// =====================================================================
//
//  A popup widget that displays toolbar-style buttons (icon above text)
//  in a grid layout. Used as the dropdown for ToolbarButton.
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_TOOLBARDROPDOWN_H
#define HOBBYCAD_TOOLBARDROPDOWN_H

#include <QFrame>
#include <QVector>

class QToolButton;
class QGridLayout;

namespace hobbycad {

class ToolbarDropdown : public QFrame {
    Q_OBJECT

public:
    explicit ToolbarDropdown(QWidget* parent = nullptr);

    /// Add a button with icon and text. Returns the button for further customization.
    QToolButton* addButton(const QIcon& icon, const QString& text,
                           const QString& toolTip = QString());

    /// Add a separator (horizontal line spanning full width).
    void addSeparator();

    /// Set number of columns in the grid (default 1 for vertical list).
    void setColumns(int cols);

    /// Set the icon size for buttons.
    void setIconSize(int size);

    /// Show the popup below the given widget.
    void showBelow(QWidget* anchor);

signals:
    /// Emitted when any button is clicked (popup auto-hides).
    void buttonClicked(int index);

protected:
    void focusOutEvent(QFocusEvent* event) override;
    bool event(QEvent* event) override;

private:
    QGridLayout* m_layout = nullptr;
    QVector<QToolButton*> m_buttons;
    int m_columns = 1;
    int m_currentRow = 0;
    int m_currentCol = 0;
    int m_iconSize = 24;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_TOOLBARDROPDOWN_H
