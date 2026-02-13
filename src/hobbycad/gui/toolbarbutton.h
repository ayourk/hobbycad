// =====================================================================
//  src/hobbycad/gui/toolbarbutton.h — Toolbar button with dropdown
// =====================================================================
//
//  A button widget with icon above text label, and a small dropdown
//  arrow on the right side.
//  The main button area triggers the primary action; clicking the
//  dropdown arrow opens a popup with related actions in the same style.
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_TOOLBARBUTTON_H
#define HOBBYCAD_TOOLBARBUTTON_H

#include <QWidget>

class QToolButton;

namespace hobbycad {

class ToolbarDropdown;

class ToolbarButton : public QWidget {
    Q_OBJECT

public:
    explicit ToolbarButton(const QIcon& icon,
                           const QString& text,
                           const QString& toolTip = QString(),
                           QWidget* parent = nullptr);

    /// Access the dropdown popup for adding buttons.
    ToolbarDropdown* dropdown() const;

    /// Set the icon size.
    void setIconSize(int size);

    /// Enable/disable the button.
    void setEnabled(bool enabled);
    bool isEnabled() const;

    /// Set checkable state.
    void setCheckable(bool checkable);
    bool isCheckable() const;

    /// Set checked state (only meaningful if checkable).
    void setChecked(bool checked);
    bool isChecked() const;

signals:
    /// Emitted when the main button is clicked.
    void clicked();

    /// Emitted when the checked state changes (if checkable).
    void toggled(bool checked);

    /// Emitted when a dropdown button is clicked.
    void dropdownClicked(int index);

private:
    void showDropdown();

    QToolButton* m_mainButton = nullptr;
    QToolButton* m_dropButton = nullptr;
    ToolbarDropdown* m_dropdown = nullptr;
    int m_iconSize = 24;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_TOOLBARBUTTON_H
