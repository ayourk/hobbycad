// =====================================================================
//  src/hobbycad/gui/arrangedtoolbar.cpp — a toolbar built from the arrangement
// =====================================================================

#include "arrangedtoolbar.h"
#include "arrangementstore.h"

#include "commandtext.h"
#include "toolbarbutton.h"
#include "toolbardropdown.h"

#include <hobbycad/layout/arrangement.h>

#include <QFrame>
#include <QHBoxLayout>

namespace hobbycad {

using commands::Command;
using layout::ContainerKind;
using layout::ElementKind;

ArrangedToolbar::ArrangedToolbar(const char* containerId, QWidget* parent)
    : QWidget(parent), m_containerId(containerId ? containerId : "")
{
    setAutoFillBackground(true);
    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(4, 2, 4, 2);
    m_layout->setSpacing(4);
    build(containerId);
    // Left-align the buttons.
    m_layout->addStretch();
}

void ArrangedToolbar::rebuild()
{
    // The buttons belong to the layout; both go, and the groups with them.
    while (QLayoutItem* item = m_layout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    m_groups.clear();
    m_singles.clear();
    build(m_containerId.c_str());
    m_layout->addStretch();
    setBindings(m_keys);
    retranslate();
}

void ArrangedToolbar::build(const char* containerId)
{
    const layout::Arrangement& arr = ArrangementStore::instance().current();
    // Groups are looked up by index from the button signals, so the vector
    // must not reallocate once the first one is connected.
    std::size_t groupCount = 0;
    for (const layout::Element* e : arr.children(containerId)) {
        if (e->kind == ElementKind::Container) ++groupCount;
    }
    m_groups.reserve(groupCount);

    for (const layout::Element* e : arr.children(containerId)) {
        switch (e->kind) {
        case ElementKind::Container:
            addGroup(e->id);
            break;
        case ElementKind::Separator:
            addDivider();
            break;
        case ElementKind::Command: {
            const Command* cmd = commands::findCommand(e->id);
            if (!cmd) break;
            auto* btn = new ToolbarButton(commandtext::icon(*cmd, style()), QString(),
                                          QString(), this);
            btn->setCheckable(cmd->kind == commands::Kind::Toggle);
            btn->setChecked(cmd->checkedByDefault);
            connect(btn, &ToolbarButton::clicked, this, [this, cmd, btn]() {
                commandChosen(*cmd, std::string(), Via::Button, btn->isChecked());
            });
            m_layout->addWidget(btn);
            m_singles.push_back({cmd, btn});
            break;
        }
        case ElementKind::Placeholder:
            break;
        }
    }
    retranslate();
}

void ArrangedToolbar::addDivider()
{
    auto* sep = new QFrame(this);
    sep->setFrameShape(QFrame::VLine);
    sep->setFrameShadow(QFrame::Sunken);
    sep->setFixedWidth(2);
    m_layout->addWidget(sep);
}

void ArrangedToolbar::addGroup(const std::string& groupId)
{
    const layout::Arrangement& arr = ArrangementStore::instance().current();
    const layout::Container* container = arr.container(groupId);
    if (!container || container->kind != ContainerKind::ToolGroup) return;

    Group g;
    g.id = groupId;
    g.title = commands::findCommand(container->title);
    g.fallback = commands::findCommand(container->activates);
    g.button = new ToolbarButton(g.title ? commandtext::icon(*g.title, style()) : QIcon(),
                                 QString(), QString(), this);
    g.button->setCheckable(true);

    ToolbarDropdown* drop = g.button->dropdown();
    drop->setIconSize(16);
    bool anyUsable = false;
    for (const layout::Element* e : arr.children(groupId)) {
        if (e->kind == ElementKind::Separator) {
            drop->addSeparator();
            continue;
        }
        if (e->kind != ElementKind::Command) continue;
        const Command* cmd = commands::findCommand(e->id);
        if (!cmd) continue;
        drop->addButton(commandtext::icon(*cmd, style()), QString(), QString());
        g.rows.push_back(cmd);
        anyUsable = anyUsable || !(cmd->needs & commands::NotImplemented);
        if (const layout::Container* v = arr.variantsOf(cmd->id)) {
            for (const std::string& id : arr.commandIds(v->id)) {
                const Command* variant = commands::findCommand(id);
                if (variant && variant->hasMode) {
                    drop->addVariant(QString(), static_cast<int>(variant->mode));
                }
            }
        }
    }
    // A group of tools none of which exist yet is shown but cannot be used.
    g.button->setEnabled(anyUsable);

    const std::size_t index = m_groups.size();
    connect(g.button, &ToolbarButton::clicked, this, [this, index]() {
        onGroupClicked(m_groups[index]);
    });
    connect(drop, &ToolbarDropdown::buttonClicked, this, [this, index](int row) {
        onRowClicked(m_groups[index], row);
    });
    connect(drop, &ToolbarDropdown::variantClicked, this,
            [this, index](int row, int variantId) {
        onVariantClicked(m_groups[index], row, variantId);
    });
    m_layout->addWidget(g.button);
    m_groups.push_back(std::move(g));
}

void ArrangedToolbar::onGroupClicked(Group& group)
{
    const Command* cmd = group.picked ? group.picked : group.fallback;
    if (!cmd) {
        // The group opens its list first: nothing to run until one is picked.
        group.button->showDropdown();
        return;
    }
    commandChosen(*cmd, group.id, Via::GroupButton, false);
}

void ArrangedToolbar::onRowClicked(Group& group, int row)
{
    if (row < 0 || row >= static_cast<int>(group.rows.size())) return;
    const Command* cmd = group.rows[static_cast<std::size_t>(row)];
    // An action (a transform of the selection) runs once and does not take
    // the button over.
    if (cmd->kind == commands::Kind::Tool) pick(group, cmd);
    commandChosen(*cmd, group.id, Via::Row, false);
}

void ArrangedToolbar::onVariantClicked(Group& group, int row, int variantId)
{
    if (row < 0 || row >= static_cast<int>(group.rows.size())) return;
    const Command* tool = group.rows[static_cast<std::size_t>(row)];
    const Command* variant =
        commands::commandForMode(tool->sketchTool, static_cast<CreationMode>(variantId));
    if (!variant) return;
    pick(group, variant);
    commandChosen(*variant, group.id, Via::Variant, false);
}

void ArrangedToolbar::pick(Group& group, const Command* command)
{
    group.previous = group.picked;
    group.picked = command;
    refreshGroup(group);
}

void ArrangedToolbar::refreshGroup(Group& group)
{
    const Command* shown = group.picked ? group.picked : group.title;
    if (!shown) return;
    group.button->setText(commandtext::toolbarText(*shown));
    // A variant has no tooltip of its own; its tool's says what it does.
    const Command* tipFrom = shown;
    if (shown->variant) {
        if (const Command* tool = commands::commandForTool(shown->sketchTool)) tipFrom = tool;
    }
    group.button->setToolTip(commandtext::tooltip(*tipFrom, &m_keys));
}

void ArrangedToolbar::retranslate()
{
    const layout::Arrangement& arr = ArrangementStore::instance().current();
    for (Group& g : m_groups) {
        refreshGroup(g);
        ToolbarDropdown* drop = g.button->dropdown();
        for (std::size_t row = 0; row < g.rows.size(); ++row) {
            const Command* cmd = g.rows[row];
            drop->setButtonText(static_cast<int>(row), commandtext::label(*cmd),
                                commandtext::tooltip(*cmd, &m_keys));
            const layout::Container* v = arr.variantsOf(cmd->id);
            if (!v) continue;
            for (const std::string& id : arr.commandIds(v->id)) {
                const Command* variant = commands::findCommand(id);
                if (!variant || !variant->hasMode) continue;
                drop->setVariantText(static_cast<int>(row), static_cast<int>(variant->mode),
                                     commandtext::label(*variant));
            }
        }
    }
    for (const Single& s : m_singles) {
        s.button->setText(commandtext::toolbarText(*s.command));
        s.button->setToolTip(commandtext::tooltip(*s.command, &m_keys));
    }
}

void ArrangedToolbar::setBindings(const bindings::Table& keys)
{
    m_keys = keys;
    retranslate();
}

void ArrangedToolbar::markActiveGroup(const std::string& groupId)
{
    for (Group& g : m_groups) g.button->setChecked(g.id == groupId);
}

std::string ArrangedToolbar::groupOf(const Command& command) const
{
    const layout::Container* g = ArrangementStore::instance().current().groupOf(command.id);
    return g && findGroup(g->id) ? g->id : std::string();
}

const Command* ArrangedToolbar::groupCommand(const std::string& groupId) const
{
    const Group* g = findGroup(groupId);
    if (!g) return nullptr;
    return g->picked ? g->picked : g->fallback;
}

void ArrangedToolbar::resetGroups()
{
    for (Group& g : m_groups) {
        g.picked = nullptr;
        g.previous = nullptr;
        if (g.title) g.button->setIcon(commandtext::icon(*g.title, style()));
        g.button->dropdown()->resetAllItems();
        refreshGroup(g);
    }
}

void ArrangedToolbar::revertGroup(const std::string& groupId)
{
    Group* g = findGroup(groupId);
    if (!g) return;
    g->picked = g->previous;
    refreshGroup(*g);
    if (!g->picked) g->button->setChecked(false);
}

void ArrangedToolbar::setButtonChecked(const std::string& commandId, bool on)
{
    for (const Single& s : m_singles) {
        if (commandId == s.command->id && s.button->isChecked() != on) {
            s.button->setChecked(on);   // setChecked does not fire clicked
        }
    }
}

ArrangedToolbar::Group* ArrangedToolbar::findGroup(const std::string& groupId)
{
    for (Group& g : m_groups) {
        if (g.id == groupId) return &g;
    }
    return nullptr;
}

const ArrangedToolbar::Group* ArrangedToolbar::findGroup(const std::string& groupId) const
{
    for (const Group& g : m_groups) {
        if (g.id == groupId) return &g;
    }
    return nullptr;
}

}  // namespace hobbycad
