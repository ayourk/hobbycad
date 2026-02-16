// =====================================================================
//  src/hobbycad/gui/toolbardropdown.h — Toolbar dropdown popup
// =====================================================================
//
//  A popup widget that displays toolbar-style buttons (icon above text)
//  in a vertical list layout. Used as the dropdown for ToolbarButton.
//
//  Items can optionally have submenus for alternate modes (e.g.,
//  Rectangle can have Corner, Center, 3-Point variants).
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_TOOLBARDROPDOWN_H
#define HOBBYCAD_TOOLBARDROPDOWN_H

#include <QFrame>
#include <QVector>

class QToolButton;
class QVBoxLayout;
class QMenu;

namespace hobbycad {

/// A single item in the dropdown, with optional submenu for variants
struct DropdownItem {
    QWidget* widget = nullptr;      // The row widget
    QToolButton* mainButton = nullptr;  // Main clickable button
    QToolButton* arrowButton = nullptr; // Arrow for submenu (if any)
    QMenu* submenu = nullptr;       // Submenu with variants (if any)
    int index = -1;                 // Item index
    QString originalText;           // Original button text (e.g., "Slot")
    int lastVariantId = -1;         // Last selected variant ID (-1 = default)
    QString lastVariantName;        // Last selected variant name
};

class ToolbarDropdown : public QFrame {
    Q_OBJECT

public:
    explicit ToolbarDropdown(QWidget* parent = nullptr);

    /// Add a button with icon and text. Returns the index for reference.
    /// If no submenu variants are needed, this is a simple clickable item.
    int addButton(const QIcon& icon, const QString& text,
                  const QString& toolTip = QString());

    /// Add a submenu variant to the last added button.
    /// @param variantName  Display name (e.g., "Center + Radius")
    /// @param variantId    ID to emit when this variant is selected
    void addVariant(const QString& variantName, int variantId);

    /// Add a separator (horizontal line).
    void addSeparator();

    /// Set the icon size for buttons.
    void setIconSize(int size);

    /// Show the popup below the given widget.
    void showBelow(QWidget* anchor);

    /// Reset all items to their original text (clears variant selections)
    void resetAllItems();

signals:
    /// Emitted when main button clicked (uses default/first variant).
    /// @param index  The item index
    void buttonClicked(int index);

    /// Emitted when a specific variant is selected from submenu.
    /// @param index      The item index
    /// @param variantId  The variant ID passed to addVariant()
    void variantClicked(int index, int variantId);

    /// Emitted when a specific variant is selected, includes the variant name.
    /// @param index        The item index
    /// @param variantId    The variant ID passed to addVariant()
    /// @param variantName  The display name of the variant
    void variantSelected(int index, int variantId, const QString& variantName);

protected:
    void focusOutEvent(QFocusEvent* event) override;
    bool event(QEvent* event) override;

private:
    void onItemClicked(int index);
    void onVariantTriggered(int index, int variantId);

    QVBoxLayout* m_layout = nullptr;
    QVector<DropdownItem> m_items;
    int m_iconSize = 16;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_TOOLBARDROPDOWN_H
