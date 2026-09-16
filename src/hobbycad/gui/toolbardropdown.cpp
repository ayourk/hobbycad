// =====================================================================
//  src/hobbycad/gui/toolbardropdown.cpp — Toolbar dropdown popup
// =====================================================================

#include "toolbardropdown.h"

#include <QApplication>
#include <QEvent>
#include <QFocusEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMenu>
#include <QToolButton>
#include <QVBoxLayout>

namespace hobbycad {

ToolbarDropdown::ToolbarDropdown(QWidget* parent)
    : QFrame(parent, Qt::Popup)
{
    setFrameStyle(QFrame::StyledPanel | QFrame::Raised);
    setFocusPolicy(Qt::StrongFocus);

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(4, 4, 4, 4);
    m_layout->setSpacing(2);
}

int ToolbarDropdown::addButton(const QIcon& icon, const QString& text,
                                const QString& toolTip)
{
    int index = m_items.size();

    // Create a horizontal row widget
    auto* rowWidget = new QWidget(this);
    auto* rowLayout = new QHBoxLayout(rowWidget);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(0);

    // Main button (icon + text)
    auto* mainBtn = new QToolButton(rowWidget);
    mainBtn->setIcon(icon);
    mainBtn->setText(text);
    mainBtn->setToolTip(toolTip.isEmpty() ? text : toolTip);
    mainBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    mainBtn->setIconSize(QSize(m_iconSize, m_iconSize));
    mainBtn->setAutoRaise(true);
    mainBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    rowLayout->addWidget(mainBtn);

    // Arrow button for submenu (initially hidden, shown when variants added)
    auto* arrowBtn = new QToolButton(rowWidget);
    arrowBtn->setArrowType(Qt::RightArrow);
    arrowBtn->setFixedWidth(16);
    arrowBtn->setAutoRaise(true);
    arrowBtn->setVisible(false);
    rowLayout->addWidget(arrowBtn);

    m_layout->addWidget(rowWidget);

    // Store item info
    DropdownItem item;
    item.widget = rowWidget;
    item.mainButton = mainBtn;
    item.arrowButton = arrowBtn;
    item.submenu = nullptr;
    item.index = index;
    item.originalText = text;      // Remember original text for reset
    item.lastVariantId = -1;       // No variant selected yet
    item.lastVariantName.clear();
    m_items.append(item);

    // Connect main button click
    connect(mainBtn, &QToolButton::clicked, this, [this, index]() {
        onItemClicked(index);
    });

    return index;
}

void ToolbarDropdown::addVariant(const QString& variantName, int variantId)
{
    if (m_items.isEmpty()) return;

    int itemIndex = m_items.size() - 1;
    DropdownItem& item = m_items[itemIndex];

    // Create submenu if not exists
    if (!item.submenu) {
        item.submenu = new QMenu(this);
        item.arrowButton->setVisible(true);

        // Show submenu when arrow clicked - capture index, not reference
        connect(item.arrowButton, &QToolButton::clicked, this, [this, itemIndex]() {
            if (itemIndex < m_items.size()) {
                const DropdownItem& it = m_items[itemIndex];
                QPoint pos = it.arrowButton->mapToGlobal(
                    QPoint(it.arrowButton->width(), 0));
                it.submenu->exec(pos);
            }
        });
    }

    // Add variant action
    QAction* action = item.submenu->addAction(variantName);
    action->setData(variantId);
    connect(action, &QAction::triggered, this, [this, itemIndex, variantId, action]() {
        // Read the name from the action rather than capturing a copy of the
        // string. A captured copy is frozen in whatever language was current
        // when the variant was registered, and it feeds the remembered
        // selection, the button caption and the emitted signal, so after a
        // live language switch all three would silently revert to the old
        // language the next time a variant was picked.
        const QString variantName = action->text();
        if (itemIndex < m_items.size()) {
            DropdownItem& it = m_items[itemIndex];
            it.lastVariantId = variantId;
            it.lastVariantName = variantName;
            it.mainButton->setText(variantCategory(variantName));
        }
        onVariantTriggered(itemIndex, variantId);
        emit variantSelected(itemIndex, variantId, variantName);
    });
}

QString ToolbarDropdown::variantCategory(const QString& variantName)
{
    // The button shows the category rather than the full variant, so
    // "Arc Slot (Radius)" becomes "Arc Slot".
    //
    // Both bracket forms are checked because CJK translations use the
    // fullwidth pair: the Chinese for that string is "弧形槽（半径）", and
    // matching only U+0028 would leave the qualifier on the button for every
    // CJK language while working perfectly in every Latin one.
    static const QChar kOpeners[] = {QLatin1Char('('), QChar(0xFF08)};
    int cut = -1;
    for (QChar opener : kOpeners) {
        const int at = variantName.indexOf(opener);
        if (at > 0 && (cut < 0 || at < cut)) {
            cut = at;
        }
    }
    return cut > 0 ? variantName.left(cut).trimmed() : variantName;
}

void ToolbarDropdown::setVariantText(int index, int variantId,
                                     const QString& text)
{
    if (index < 0 || index >= m_items.size()) {
        return;
    }
    DropdownItem& item = m_items[index];
    if (!item.submenu) {
        return;
    }
    const auto actions = item.submenu->actions();
    for (QAction* action : actions) {
        if (action->data().toInt() != variantId) {
            continue;
        }
        action->setText(text);
        // If this is the variant currently showing on the button, the caption
        // and the remembered name are derived from it and must follow.
        if (item.lastVariantId == variantId) {
            item.lastVariantName = text;
            if (item.mainButton) {
                item.mainButton->setText(variantCategory(text));
            }
        }
        return;
    }
}

void ToolbarDropdown::setButtonText(int index, const QString& text,
                                    const QString& toolTip)
{
    if (index < 0 || index >= m_items.size()) {
        return;
    }
    DropdownItem& item = m_items[index];
    item.originalText = text;
    if (item.mainButton) {
        item.mainButton->setText(text);
        item.mainButton->setToolTip(toolTip.isEmpty() ? text : toolTip);
    }
}

void ToolbarDropdown::addSeparator()
{
    auto* line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    m_layout->addWidget(line);
}

void ToolbarDropdown::setIconSize(int size)
{
    m_iconSize = size;
    for (auto& item : m_items) {
        if (item.mainButton) {
            item.mainButton->setIconSize(QSize(size, size));
        }
    }
}

void ToolbarDropdown::showBelow(QWidget* anchor)
{
    if (!anchor) return;

    // Calculate minimum width based on content
    adjustSize();

    // Position below the anchor widget, left-aligned
    QPoint pos = anchor->mapToGlobal(QPoint(0, anchor->height()));
    move(pos);
    show();
    setFocus();
}

void ToolbarDropdown::resetAllItems()
{
    // Reset all items to their original text and clear variant selections
    for (auto& item : m_items) {
        item.lastVariantId = -1;
        item.lastVariantName.clear();
        if (item.mainButton && !item.originalText.isEmpty()) {
            item.mainButton->setText(item.originalText);
        }
    }
}

void ToolbarDropdown::onItemClicked(int index)
{
    hide();

    // If this item has a previously selected variant, use that
    if (index >= 0 && index < m_items.size()) {
        const DropdownItem& item = m_items[index];
        if (item.lastVariantId >= 0) {
            // Re-emit the last variant selection
            emit variantClicked(index, item.lastVariantId);
            emit variantSelected(index, item.lastVariantId, item.lastVariantName);
            return;
        }
    }

    // Otherwise emit regular button click (default behavior)
    emit buttonClicked(index);
}

void ToolbarDropdown::onVariantTriggered(int index, int variantId)
{
    hide();
    emit variantClicked(index, variantId);
}

void ToolbarDropdown::focusOutEvent(QFocusEvent* event)
{
    Q_UNUSED(event);
    // Don't hide if a submenu is open
    for (const auto& item : m_items) {
        if (item.submenu && item.submenu->isVisible()) {
            return;
        }
    }
    hide();
}

bool ToolbarDropdown::event(QEvent* event)
{
    // Hide on escape key
    if (event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            hide();
            return true;
        }
    }
    return QFrame::event(event);
}

}  // namespace hobbycad
