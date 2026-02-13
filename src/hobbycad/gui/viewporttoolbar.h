// =====================================================================
//  src/hobbycad/gui/viewporttoolbar.h — Toolbar above the viewport
// =====================================================================
//
//  Horizontal toolbar with labeled buttons and associated dropdowns.
//  Each button has an icon above a text label.
//  The dropdown provides related options or variants.
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_VIEWPORTTOOLBAR_H
#define HOBBYCAD_VIEWPORTTOOLBAR_H

#include <QWidget>

class QHBoxLayout;

namespace hobbycad {

class ToolbarButton;

class ViewportToolbar : public QWidget {
    Q_OBJECT

public:
    explicit ViewportToolbar(QWidget* parent = nullptr);

    /// Add a button with icon, text label, and optional tooltip.
    /// Returns the created button for further customization.
    ToolbarButton* addButton(const QIcon& icon,
                             const QString& text,
                             const QString& toolTip = QString());

    /// Add a separator (vertical line).
    void addSeparator();

    /// Add a stretch to push subsequent buttons to the right.
    void addStretch();

    /// Set the icon size for all buttons.
    void setIconSize(int size);

private:
    QHBoxLayout* m_layout = nullptr;
    int m_iconSize = 24;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_VIEWPORTTOOLBAR_H
