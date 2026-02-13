// =====================================================================
//  src/hobbycad/gui/viewporttoolbar.cpp — Toolbar above the viewport
// =====================================================================

#include "viewporttoolbar.h"
#include "toolbarbutton.h"

#include <QFrame>
#include <QHBoxLayout>

namespace hobbycad {

ViewportToolbar::ViewportToolbar(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("ViewportToolbar"));

    setAutoFillBackground(true);

    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(4, 2, 4, 2);
    m_layout->setSpacing(4);

    // Add stretch at the end by default to left-align buttons
    m_layout->addStretch();
}

ToolbarButton* ViewportToolbar::addButton(const QIcon& icon,
                                          const QString& text,
                                          const QString& toolTip)
{
    auto* btn = new ToolbarButton(icon, text, toolTip, this);
    btn->setIconSize(m_iconSize);

    // Insert before the final stretch
    int idx = m_layout->count() - 1;
    if (idx < 0) idx = 0;
    m_layout->insertWidget(idx, btn);

    return btn;
}

void ViewportToolbar::addSeparator()
{
    auto* sep = new QFrame(this);
    sep->setFrameShape(QFrame::VLine);
    sep->setFrameShadow(QFrame::Sunken);
    sep->setFixedWidth(2);

    // Insert before the final stretch
    int idx = m_layout->count() - 1;
    if (idx < 0) idx = 0;
    m_layout->insertWidget(idx, sep);
}

void ViewportToolbar::addStretch()
{
    // Insert before the final stretch
    int idx = m_layout->count() - 1;
    if (idx < 0) idx = 0;
    m_layout->insertStretch(idx);
}

void ViewportToolbar::setIconSize(int size)
{
    m_iconSize = size;

    // Update all existing buttons
    for (int i = 0; i < m_layout->count(); ++i) {
        auto* item = m_layout->itemAt(i);
        if (auto* btn = qobject_cast<ToolbarButton*>(item->widget())) {
            btn->setIconSize(size);
        }
    }
}

}  // namespace hobbycad
