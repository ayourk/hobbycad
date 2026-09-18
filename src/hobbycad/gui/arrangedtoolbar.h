// =====================================================================
//  src/hobbycad/gui/arrangedtoolbar.h — a toolbar built from the arrangement
// =====================================================================
//
//  Lays out one toolbar container of the arrangement
//  (hobbycad/layout/arrangement.h): a ToolbarButton per tool group, with
//  the group's commands as dropdown rows and a tool's variants in the row's
//  submenu; a plain button per command placed on the toolbar itself; and a
//  divider per separator. Which buttons exist and in what order is the
//  arrangement's business, so this class knows no tool by name.
//
//  A group button starts on the command its group activates, or on nothing
//  when the group opens its list first. Picking a row puts that command on
//  the button until the groups are reset. What a chosen command then DOES
//  is the subclass's business (commandChosen()).
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_ARRANGEDTOOLBAR_H
#define HOBBYCAD_ARRANGEDTOOLBAR_H

#include <hobbycad/bindings.h>
#include <hobbycad/commands.h>

#include <QString>
#include <QWidget>

#include <string>
#include <vector>

#include "../i18n/retranslatable.h"

class QHBoxLayout;

namespace hobbycad {

class ToolbarButton;

class ArrangedToolbar : public QWidget, public Retranslatable {
    Q_OBJECT

public:
    /// Build the toolbar container `containerId` of the default arrangement.
    ArrangedToolbar(const char* containerId, QWidget* parent);

    /// Re-apply every caption, tooltip, row and variant.
    void retranslate() override;

    /// Build the toolbar again from the arrangement, after the person
    /// changed it in the Customize dialog. Picks are forgotten.
    void rebuild();

    /// Key bindings to name in tooltips.
    void setBindings(const bindings::Table& keys);

protected:
    /// How a chosen command reached the subclass.
    enum class Via {
        GroupButton,   ///< the group button, running what it shows
        Row,           ///< a dropdown row
        Variant,       ///< a variant in a row's submenu
        Button,        ///< a command placed on the toolbar itself
    };

    /// A command was chosen. `groupId` is the group it came from ("" for a
    /// command on the toolbar itself); `checked` is a toggle's new state.
    virtual void commandChosen(const commands::Command& command, const std::string& groupId,
                               Via via, bool checked) = 0;

    /// Check the button of one group and uncheck the others ("" for none).
    void markActiveGroup(const std::string& groupId);

    /// The group showing `command` (a tool or one of its variants), or "".
    std::string groupOf(const commands::Command& command) const;

    /// The command a group's button currently runs, or nullptr when the
    /// group opens its list first and nothing was picked. For a tool whose
    /// variant was picked, the variant.
    const commands::Command* groupCommand(const std::string& groupId) const;

    /// Put every group back on its first command, forgetting picks. Which
    /// button is checked is left alone.
    void resetGroups();

    /// Put a group back on what it showed before its last pick.
    void revertGroup(const std::string& groupId);

    /// Check or uncheck the button of a toggle placed on the toolbar
    /// without reporting it.
    void setButtonChecked(const std::string& commandId, bool on);

private:
    struct Group {
        std::string id;
        const commands::Command* title = nullptr;
        ToolbarButton* button = nullptr;
        std::vector<const commands::Command*> rows;
        /// What the button runs before a pick; nullptr = open the list.
        const commands::Command* fallback = nullptr;
        /// The picked row's command, or its variant; nullptr = none.
        const commands::Command* picked = nullptr;
        const commands::Command* previous = nullptr;
    };
    struct Single {
        const commands::Command* command = nullptr;
        ToolbarButton* button = nullptr;
    };

    void build(const char* containerId);
    void addGroup(const std::string& groupId);
    void addDivider();
    void onGroupClicked(Group& group);
    void onRowClicked(Group& group, int row);
    void onVariantClicked(Group& group, int row, int variantId);
    void pick(Group& group, const commands::Command* command);
    void refreshGroup(Group& group);
    Group* findGroup(const std::string& groupId);
    const Group* findGroup(const std::string& groupId) const;

    std::string m_containerId;
    QHBoxLayout* m_layout = nullptr;
    std::vector<Group> m_groups;
    std::vector<Single> m_singles;
    bindings::Table m_keys;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_ARRANGEDTOOLBAR_H
