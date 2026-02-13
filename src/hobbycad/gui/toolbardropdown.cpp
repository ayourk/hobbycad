// =====================================================================
//  src/hobbycad/gui/toolbardropdown.cpp — Toolbar dropdown popup
// =====================================================================

#include "toolbardropdown.h"

#include <QApplication>
#include <QEvent>
#include <QFocusEvent>
#include <QFrame>
#include <QGridLayout>
#include <QToolButton>

namespace hobbycad {

ToolbarDropdown::ToolbarDropdown(QWidget* parent)
    : QFrame(parent, Qt::Popup)
{
    setFrameStyle(QFrame::StyledPanel | QFrame::Raised);
    setFocusPolicy(Qt::StrongFocus);

    m_layout = new QGridLayout(this);
    m_layout->setContentsMargins(4, 4, 4, 4);
    m_layout->setSpacing(2);
}

QToolButton* ToolbarDropdown::addButton(const QIcon& icon, const QString& text,
                                         const QString& toolTip)
{
    auto* btn = new QToolButton(this);
    btn->setIcon(icon);
    btn->setText(text);
    btn->setToolTip(toolTip.isEmpty() ? text : toolTip);
    btn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    btn->setIconSize(QSize(m_iconSize, m_iconSize));
    btn->setAutoRaise(true);
    btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    int index = m_buttons.size();
    m_buttons.append(btn);

    m_layout->addWidget(btn, m_currentRow, m_currentCol);

    // Move to next position
    m_currentCol++;
    if (m_currentCol >= m_columns) {
        m_currentCol = 0;
        m_currentRow++;
    }

    // Connect click to hide popup and emit signal
    connect(btn, &QToolButton::clicked, this, [this, index]() {
        hide();
        emit buttonClicked(index);
    });

    return btn;
}

void ToolbarDropdown::addSeparator()
{
    // Move to next row if not at start of row
    if (m_currentCol != 0) {
        m_currentCol = 0;
        m_currentRow++;
    }

    auto* line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    m_layout->addWidget(line, m_currentRow, 0, 1, m_columns);

    m_currentRow++;
}

void ToolbarDropdown::setColumns(int cols)
{
    m_columns = qMax(1, cols);
}

void ToolbarDropdown::setIconSize(int size)
{
    m_iconSize = size;
    for (auto* btn : m_buttons) {
        btn->setIconSize(QSize(size, size));
    }
}

void ToolbarDropdown::showBelow(QWidget* anchor)
{
    if (!anchor) return;

    // Match the width of the anchor button
    setFixedWidth(anchor->width());

    // Position below the anchor widget, left-aligned
    QPoint pos = anchor->mapToGlobal(QPoint(0, anchor->height()));
    move(pos);
    show();
    setFocus();
}

void ToolbarDropdown::focusOutEvent(QFocusEvent* event)
{
    Q_UNUSED(event);
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
