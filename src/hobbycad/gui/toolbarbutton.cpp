// =====================================================================
//  src/hobbycad/gui/toolbarbutton.cpp — Toolbar button with dropdown
// =====================================================================

#include "toolbarbutton.h"
#include "toolbardropdown.h"

#include <QHBoxLayout>
#include <QToolButton>

namespace hobbycad {

ToolbarButton::ToolbarButton(const QIcon& icon,
                             const QString& text,
                             const QString& toolTip,
                             QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(2, 2, 0, 2);
    layout->setSpacing(0);

    // Main button with icon above text
    m_mainButton = new QToolButton(this);
    m_mainButton->setIcon(icon);
    m_mainButton->setText(text);
    m_mainButton->setToolTip(toolTip.isEmpty() ? text : toolTip);
    m_mainButton->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    m_mainButton->setIconSize(QSize(m_iconSize, m_iconSize));
    m_mainButton->setAutoRaise(true);
    layout->addWidget(m_mainButton);

    // Dropdown arrow button (narrow, full height)
    m_dropButton = new QToolButton(this);
    m_dropButton->setArrowType(Qt::DownArrow);
    m_dropButton->setFixedWidth(14);
    m_dropButton->setAutoRaise(true);
    layout->addWidget(m_dropButton);

    // Create dropdown popup
    m_dropdown = new ToolbarDropdown(this);
    m_dropdown->setIconSize(m_iconSize);

    // Forward signals
    connect(m_mainButton, &QToolButton::clicked,
            this, &ToolbarButton::clicked);
    connect(m_mainButton, &QToolButton::toggled,
            this, &ToolbarButton::toggled);

    // Show dropdown when arrow clicked
    connect(m_dropButton, &QToolButton::clicked,
            this, &ToolbarButton::showDropdown);

    // Forward dropdown button clicks
    connect(m_dropdown, &ToolbarDropdown::buttonClicked,
            this, &ToolbarButton::dropdownClicked);
}

ToolbarDropdown* ToolbarButton::dropdown() const
{
    return m_dropdown;
}

void ToolbarButton::setIconSize(int size)
{
    m_iconSize = size;
    m_mainButton->setIconSize(QSize(size, size));
    m_dropdown->setIconSize(size);
}

void ToolbarButton::setEnabled(bool enabled)
{
    m_mainButton->setEnabled(enabled);
    // Keep dropdown enabled so users can still see available options
    // even when the main button action is disabled
}

bool ToolbarButton::isEnabled() const
{
    return m_mainButton->isEnabled();
}

void ToolbarButton::setCheckable(bool checkable)
{
    m_mainButton->setCheckable(checkable);
}

bool ToolbarButton::isCheckable() const
{
    return m_mainButton->isCheckable();
}

void ToolbarButton::setChecked(bool checked)
{
    m_mainButton->setChecked(checked);
}

bool ToolbarButton::isChecked() const
{
    return m_mainButton->isChecked();
}

void ToolbarButton::showDropdown()
{
    m_dropdown->showBelow(this);
}

}  // namespace hobbycad
