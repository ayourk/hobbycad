// =====================================================================
//  src/hobbycad/gui/bindingsdialog.cpp — Keyboard and mouse bindings
// =====================================================================

#include "bindingsdialog.h"
#include "bindingeditrow.h"
#include "commandtext.h"

#include <hobbycad/commands.h>
#include "arrangementstore.h"

#include <hobbycad/layout/arrangement.h>

#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequence>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSizePolicy>
#include <QSplitter>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

namespace hobbycad {

// Gray color for non-selectable binding items (50% of black)
static const QColor kBindingTextColor(128, 128, 128);

// ---- Default action bindings ----------------------------------------

QHash<QString, ActionBinding> BindingsDialog::defaultBindings()
{
    // The defaults are part of the arrangement and the names come from the
    // command registry, so the dialog, the menus, the toolbars and the
    // canvas all agree on what a key does.
    QHash<QString, ActionBinding> defaults;
    for (const auto& entry : ArrangementStore::instance().base().defaultBindings()) {
        const commands::Command* cmd = commands::findCommand(entry.first);
        if (!cmd) continue;
        const commands::BindingContext* context = commands::findBindingContext(cmd->context);
        const QString id = QString::fromStdString(entry.first);
        defaults.insert(id, ActionBinding(
            id, commandtext::label(*cmd),
            context ? commandtext::translate(context->title) : QString(),
            commandtext::resolveDefault(entry.second[0]),
            commandtext::resolveDefault(entry.second[1]),
            commandtext::resolveDefault(entry.second[2])));
    }
    return defaults;
}

bindings::Table BindingsDialog::toTable(const QHash<QString, ActionBinding>& actions)
{
    bindings::Table table;
    // In the arrangement's order, so the first of two colliding commands is
    // the same one every time.
    for (const auto& entry : ArrangementStore::instance().base().defaultBindings()) {
        const auto it = actions.constFind(QString::fromStdString(entry.first));
        if (it == actions.constEnd()) continue;
        const ActionBinding& ab = it.value();
        table.addCommand(entry.first, {ab.default1.toStdString(), ab.default2.toStdString(),
                                       ab.default3.toStdString()});
        table.set(entry.first, 0, ab.binding1.toStdString());
        table.set(entry.first, 1, ab.binding2.toStdString());
        table.set(entry.first, 2, ab.binding3.toStdString());
    }
    return table;
}

bindings::Table BindingsDialog::loadTable()
{
    return toTable(loadBindings());
}

// ---- Load/save bindings ---------------------------------------------

QHash<QString, ActionBinding> BindingsDialog::loadBindings()
{
    QHash<QString, ActionBinding> bindings = defaultBindings();

    // A person's keys are part of their arrangement, beside the menus and
    // toolbars they moved, so they live in the same file (ArrangementStore).
    for (const auto& entry : ArrangementStore::instance().current().defaultBindings()) {
        const auto it = bindings.find(QString::fromStdString(entry.first));
        if (it == bindings.end()) continue;
        it->binding1 = commandtext::resolveDefault(entry.second[0]);
        it->binding2 = commandtext::resolveDefault(entry.second[1]);
        it->binding3 = commandtext::resolveDefault(entry.second[2]);
    }
    return bindings;
}

void BindingsDialog::saveBindings(const QHash<QString, ActionBinding>& bindings)
{
    ArrangementStore& store = ArrangementStore::instance();
    QHash<QString, bindings::Slots> keys;
    for (const auto& entry : store.base().defaultBindings()) {
        const QString id = QString::fromStdString(entry.first);
        const auto it = bindings.constFind(id);
        if (it == bindings.constEnd()) continue;
        keys.insert(id, {it->binding1.toStdString(), it->binding2.toStdString(),
                         it->binding3.toStdString()});
    }
    // One write, one notice: the menus and toolbars rebuild once.
    store.setBindings(keys);
}

// ---- Dialog construction --------------------------------------------

BindingsDialog::BindingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Bindings"));
    setMinimumSize(750, 550);

    m_bindings = loadBindings();
    m_originalBindings = m_bindings;  // Store original for change detection
    createLayout();
    populateActions();
}

void BindingsDialog::createLayout()
{
    auto* mainLayout = new QVBoxLayout(this);

    // Splitter: action tree on left, binding editors on right
    auto* splitter = new QSplitter(Qt::Horizontal);

    // Left side: action tree
    m_actionTree = new QTreeWidget;
    m_actionTree->setHeaderLabel(tr("Actions / Bindings"));
    m_actionTree->setRootIsDecorated(true);
    m_actionTree->setAlternatingRowColors(true);
    m_actionTree->header()->setStretchLastSection(true);
    m_actionTree->setMinimumWidth(250);

    connect(m_actionTree, &QTreeWidget::itemSelectionChanged,
            this, &BindingsDialog::onSelectionChanged);

    splitter->addWidget(m_actionTree);

    // Right side: binding editors
    auto* editorWidget = new QWidget;
    auto* editorLayout = new QVBoxLayout(editorWidget);

    // Label showing which action is selected
    m_actionLabel = new QLabel(tr("Select an action to edit bindings"));
    m_actionLabel->setWordWrap(true);
    m_actionLabel->setStyleSheet(
        QStringLiteral("QLabel { font-weight: bold; }"));
    editorLayout->addWidget(m_actionLabel);

    // Three binding editors
    auto* bindGroup1 = new QGroupBox(tr("Binding 1"));
    auto* bindLayout1 = new QVBoxLayout(bindGroup1);
    bindLayout1->setContentsMargins(8, 8, 8, 8);
    m_bindingRow1 = new BindingEditRow;
    bindLayout1->addWidget(m_bindingRow1);
    editorLayout->addWidget(bindGroup1);

    auto* bindGroup2 = new QGroupBox(tr("Binding 2"));
    auto* bindLayout2 = new QVBoxLayout(bindGroup2);
    bindLayout2->setContentsMargins(8, 8, 8, 8);
    m_bindingRow2 = new BindingEditRow;
    bindLayout2->addWidget(m_bindingRow2);
    editorLayout->addWidget(bindGroup2);

    auto* bindGroup3 = new QGroupBox(tr("Binding 3"));
    auto* bindLayout3 = new QVBoxLayout(bindGroup3);
    bindLayout3->setContentsMargins(8, 8, 8, 8);
    m_bindingRow3 = new BindingEditRow;
    bindLayout3->addWidget(m_bindingRow3);
    editorLayout->addWidget(bindGroup3);

    connect(m_bindingRow1, &BindingEditRow::bindingChanged,
            this, &BindingsDialog::onBinding1Changed);
    connect(m_bindingRow2, &BindingEditRow::bindingChanged,
            this, &BindingsDialog::onBinding2Changed);
    connect(m_bindingRow3, &BindingEditRow::bindingChanged,
            this, &BindingsDialog::onBinding3Changed);

    // Conflict warning label
    m_conflictLabel = new QLabel;
    m_conflictLabel->setStyleSheet(
        QStringLiteral("QLabel { color: #cc4444; font-weight: bold; }"));
    m_conflictLabel->setWordWrap(true);
    m_conflictLabel->hide();
    editorLayout->addWidget(m_conflictLabel);

    // Restore defaults button
    auto* restoreLayout = new QHBoxLayout;
    m_restoreBtn = new QPushButton(tr("Restore Defaults"));
    connect(m_restoreBtn, &QPushButton::clicked,
            this, &BindingsDialog::onRestoreDefaults);
    restoreLayout->addWidget(m_restoreBtn);
    restoreLayout->addStretch();
    editorLayout->addLayout(restoreLayout);

    editorLayout->addStretch();

    splitter->addWidget(editorWidget);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);

    mainLayout->addWidget(splitter, 1);

    // Button box
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel |
        QDialogButtonBox::Apply);
    connect(buttons, &QDialogButtonBox::accepted,
            this, &BindingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected,
            this, &QDialog::reject);
    m_applyBtn = buttons->button(QDialogButtonBox::Apply);
    connect(m_applyBtn, &QPushButton::clicked, this, &BindingsDialog::apply);
    mainLayout->addWidget(buttons);

    // Initially disable editors (nothing selected)
    m_bindingRow1->setEnabled(false);
    m_bindingRow2->setEnabled(false);
    m_bindingRow3->setEnabled(false);
    m_restoreBtn->setEnabled(false);
    m_applyBtn->setEnabled(false);  // No changes yet
}

void BindingsDialog::populateActions()
{
    m_actionTree->clear();

    // Listed in the arrangement's order, grouped by context.
    QStringList actionOrder;
    for (const auto& entry : ArrangementStore::instance().base().defaultBindings()) {
        actionOrder.append(QString::fromStdString(entry.first));
    }

    // Group actions by category
    QHash<QString, QTreeWidgetItem*> categoryItems;

    for (const QString& actionId : actionOrder) {
        if (!m_bindings.contains(actionId)) continue;
        const ActionBinding& ab = m_bindings.value(actionId);

        // Create category item if needed
        if (!categoryItems.contains(ab.category)) {
            auto* catItem = new QTreeWidgetItem(m_actionTree);
            catItem->setText(0, ab.category);
            catItem->setFlags(catItem->flags() & ~Qt::ItemIsSelectable);
            catItem->setExpanded(true);
            categoryItems.insert(ab.category, catItem);
        }

        // Create action item under its category (selectable)
        auto* actionItem = new QTreeWidgetItem(categoryItems[ab.category]);
        actionItem->setText(0, ab.displayName);
        actionItem->setData(0, Qt::UserRole, ab.actionId);

        // Check if any bindings are set
        bool hasAnyBinding = !ab.binding1.isEmpty() ||
                             !ab.binding2.isEmpty() ||
                             !ab.binding3.isEmpty();

        // Create binding child items under the action
        if (!ab.binding1.isEmpty()) {
            auto* bind1Item = new QTreeWidgetItem(actionItem);
            bind1Item->setText(0, ab.binding1);
            bind1Item->setData(0, Qt::UserRole, ab.actionId);
            bind1Item->setFlags(bind1Item->flags() & ~Qt::ItemIsSelectable);
            bind1Item->setForeground(0, kBindingTextColor);
        } else if (!hasAnyBinding) {
            // Show (none) only for binding 1 if all bindings are empty
            auto* bind1Item = new QTreeWidgetItem(actionItem);
            bind1Item->setText(0, tr("(none)"));
            bind1Item->setData(0, Qt::UserRole, ab.actionId);
            bind1Item->setFlags(bind1Item->flags() & ~Qt::ItemIsSelectable);
            bind1Item->setForeground(0, kBindingTextColor);
        }

        if (!ab.binding2.isEmpty()) {
            auto* bind2Item = new QTreeWidgetItem(actionItem);
            bind2Item->setText(0, ab.binding2);
            bind2Item->setData(0, Qt::UserRole, ab.actionId);
            bind2Item->setFlags(bind2Item->flags() & ~Qt::ItemIsSelectable);
            bind2Item->setForeground(0, kBindingTextColor);
        }

        if (!ab.binding3.isEmpty()) {
            auto* bind3Item = new QTreeWidgetItem(actionItem);
            bind3Item->setText(0, ab.binding3);
            bind3Item->setData(0, Qt::UserRole, ab.actionId);
            bind3Item->setFlags(bind3Item->flags() & ~Qt::ItemIsSelectable);
            bind3Item->setForeground(0, kBindingTextColor);
        }

        // Collapse items with no bindings, expand those with bindings
        actionItem->setExpanded(hasAnyBinding);
    }

    // Resize column to fit content
    m_actionTree->resizeColumnToContents(0);
}

void BindingsDialog::onSelectionChanged()
{
    QList<QTreeWidgetItem*> items = m_actionTree->selectedItems();

    if (items.isEmpty()) {
        m_selectedAction.clear();
        m_actionLabel->setText(tr("Select an action to edit bindings"));
        m_bindingRow1->setEnabled(false);
        m_bindingRow2->setEnabled(false);
        m_bindingRow3->setEnabled(false);
        m_restoreBtn->setEnabled(false);
        m_conflictLabel->hide();
        return;
    }

    QTreeWidgetItem* item = items.first();
    QString actionId = item->data(0, Qt::UserRole).toString();

    if (actionId.isEmpty()) {
        // Category item selected (no actionId)
        m_selectedAction.clear();
        m_actionLabel->setText(tr("Select an action to edit bindings"));
        m_bindingRow1->setEnabled(false);
        m_bindingRow2->setEnabled(false);
        m_bindingRow3->setEnabled(false);
        m_restoreBtn->setEnabled(false);
        m_conflictLabel->hide();
        return;
    }

    m_selectedAction = actionId;

    const ActionBinding& ab = m_bindings.value(actionId);
    m_actionLabel->setText(ab.displayName);

    updateBindingEditors();

    m_bindingRow1->setEnabled(true);
    m_bindingRow2->setEnabled(true);
    m_bindingRow3->setEnabled(true);
    m_restoreBtn->setEnabled(true);
}

void BindingsDialog::updateBindingEditors()
{
    if (m_selectedAction.isEmpty()) return;

    const ActionBinding& ab = m_bindings.value(m_selectedAction);

    m_bindingRow1->blockSignals(true);
    m_bindingRow1->setBinding(ab.binding1);
    m_bindingRow1->blockSignals(false);

    m_bindingRow2->blockSignals(true);
    m_bindingRow2->setBinding(ab.binding2);
    m_bindingRow2->blockSignals(false);

    m_bindingRow3->blockSignals(true);
    m_bindingRow3->setBinding(ab.binding3);
    m_bindingRow3->blockSignals(false);

    m_conflictLabel->hide();
}

void BindingsDialog::updateTreeForAction(const QString& actionId)
{
    if (actionId.isEmpty()) return;

    // Find the action item in the tree and rebuild its children
    QTreeWidgetItemIterator it(m_actionTree);
    while (*it) {
        if ((*it)->data(0, Qt::UserRole).toString() == actionId) {
            QTreeWidgetItem* actionItem = *it;

            // Remove existing binding children
            while (actionItem->childCount() > 0) {
                delete actionItem->takeChild(0);
            }

            // Add binding children
            const ActionBinding& ab = m_bindings.value(actionId);

            bool hasAnyBinding = !ab.binding1.isEmpty() ||
                                 !ab.binding2.isEmpty() ||
                                 !ab.binding3.isEmpty();

            if (!ab.binding1.isEmpty()) {
                auto* bind1Item = new QTreeWidgetItem(actionItem);
                bind1Item->setText(0, ab.binding1);
                bind1Item->setData(0, Qt::UserRole, actionId);
                bind1Item->setFlags(bind1Item->flags() & ~Qt::ItemIsSelectable);
                bind1Item->setForeground(0, kBindingTextColor);
            } else if (!hasAnyBinding) {
                // Show (none) only for binding 1 if all bindings are empty
                auto* bind1Item = new QTreeWidgetItem(actionItem);
                bind1Item->setText(0, tr("(none)"));
                bind1Item->setData(0, Qt::UserRole, actionId);
                bind1Item->setFlags(bind1Item->flags() & ~Qt::ItemIsSelectable);
                bind1Item->setForeground(0, kBindingTextColor);
            }

            if (!ab.binding2.isEmpty()) {
                auto* bind2Item = new QTreeWidgetItem(actionItem);
                bind2Item->setText(0, ab.binding2);
                bind2Item->setData(0, Qt::UserRole, actionId);
                bind2Item->setFlags(bind2Item->flags() & ~Qt::ItemIsSelectable);
                bind2Item->setForeground(0, kBindingTextColor);
            }

            if (!ab.binding3.isEmpty()) {
                auto* bind3Item = new QTreeWidgetItem(actionItem);
                bind3Item->setText(0, ab.binding3);
                bind3Item->setData(0, Qt::UserRole, actionId);
                bind3Item->setFlags(bind3Item->flags() & ~Qt::ItemIsSelectable);
                bind3Item->setForeground(0, kBindingTextColor);
            }

            // Keep expanded if it has bindings
            actionItem->setExpanded(hasAnyBinding);

            break;
        }
        ++it;
    }
}

QString BindingsDialog::getActionContext(const QString& actionId)
{
    if (const commands::Command* cmd = commandtext::find(actionId)) {
        return QString::fromLatin1(cmd->context);
    }
    // An id the registry does not know: its first component.
    const int dotIndex = actionId.indexOf(QLatin1Char('.'));
    return dotIndex > 0 ? actionId.left(dotIndex) : QString();
}

QString BindingsDialog::checkConflict(const QString& actionId,
                                       const QString& binding) const
{
    // A global or window-wide key collides with the same key anywhere; two
    // keys heard by different views (the sketch canvas, the 3D view) do
    // not (bindings::contextsOverlap).
    return QString::fromStdString(
        toTable(m_bindings).findConflict(actionId.toStdString(), binding.toStdString()));
}

bool BindingsDialog::confirmConflict(const QString& conflictingActionId,
                                      const QString& binding)
{
    const ActionBinding& conflicting = m_bindings.value(conflictingActionId);

    // Determine why there's a conflict for a better message
    QString myContext = getActionContext(m_selectedAction);
    QString otherContext = getActionContext(conflictingActionId);

    const commands::BindingContext* mine =
        commands::findBindingContext(myContext.toStdString());
    const commands::BindingContext* theirs =
        commands::findBindingContext(otherContext.toStdString());
    const auto scopeOf = [](const commands::BindingContext* c) {
        return c ? c->scope : commands::BindingScope::Application;
    };

    QString contextInfo;
    if (myContext == otherContext) {
        contextInfo = tr("Both actions are in the %1 context.")
                      .arg(conflicting.category);
    } else if (scopeOf(mine) == commands::BindingScope::Global ||
               scopeOf(theirs) == commands::BindingScope::Global) {
        contextInfo = tr("Global bindings are active in all contexts.");
    } else if (scopeOf(theirs) == commands::BindingScope::Application) {
        contextInfo = tr("The %1 context is always active.")
                      .arg(conflicting.category);
    } else if (scopeOf(mine) == commands::BindingScope::Application) {
        contextInfo = tr("The %1 context is always active.")
                      .arg(mine ? commandtext::translate(mine->title) : myContext);
    } else {
        contextInfo = tr("Both actions are used in the same view.");
    }

    QMessageBox msgBox(this);
    msgBox.setWindowTitle(tr("Binding Conflict"));
    msgBox.setIcon(QMessageBox::Warning);
    msgBox.setText(tr("The binding \"%1\" is already assigned to \"%2\".")
                   .arg(binding, conflicting.displayName));
    msgBox.setInformativeText(
        tr("%1\n\nDo you want to remove it from \"%2\" and assign it here?")
        .arg(contextInfo, conflicting.displayName));
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    msgBox.setDefaultButton(QMessageBox::No);

    if (msgBox.exec() == QMessageBox::Yes) {
        // Remove the conflicting binding
        ActionBinding& ab = m_bindings[conflictingActionId];
        if (ab.binding1 == binding) ab.binding1.clear();
        if (ab.binding2 == binding) ab.binding2.clear();
        if (ab.binding3 == binding) ab.binding3.clear();

        // Refresh tree display
        populateActions();
        return true;
    }

    return false;
}

void BindingsDialog::handleBindingChange(int slot, const QString& binding)
{
    if (m_selectedAction.isEmpty()) return;

    QString conflict = checkConflict(m_selectedAction, binding);

    if (!conflict.isEmpty()) {
        if (!confirmConflict(conflict, binding)) {
            updateBindingEditors();
            return;
        }
    }

    // Update the binding in our data
    ActionBinding& ab = m_bindings[m_selectedAction];
    switch (slot) {
        case 1: ab.binding1 = binding; break;
        case 2: ab.binding2 = binding; break;
        case 3: ab.binding3 = binding; break;
    }

    // Update the tree and apply button
    updateTreeForAction(m_selectedAction);
    updateApplyButton();
}

void BindingsDialog::onBinding1Changed(const QString& binding)
{
    handleBindingChange(1, binding);
}

void BindingsDialog::onBinding2Changed(const QString& binding)
{
    handleBindingChange(2, binding);
}

void BindingsDialog::onBinding3Changed(const QString& binding)
{
    handleBindingChange(3, binding);
}

void BindingsDialog::onRestoreDefaults()
{
    if (m_selectedAction.isEmpty()) return;

    QHash<QString, ActionBinding> defaults = defaultBindings();
    if (!defaults.contains(m_selectedAction)) return;

    const ActionBinding& def = defaults.value(m_selectedAction);
    ActionBinding& ab = m_bindings[m_selectedAction];

    // Restore all binding slots
    ab.binding1 = def.default1;
    ab.binding2 = def.default2;
    ab.binding3 = def.default3;

    updateBindingEditors();
    updateTreeForAction(m_selectedAction);
    updateApplyButton();
}

void BindingsDialog::apply()
{
    saveBindings(m_bindings);
    m_originalBindings = m_bindings;  // Update baseline after saving
    updateApplyButton();
    emit bindingsChanged();
}

void BindingsDialog::accept()
{
    apply();
    QDialog::accept();
}

bool BindingsDialog::hasChanges() const
{
    for (auto it = m_bindings.constBegin(); it != m_bindings.constEnd(); ++it) {
        const QString& key = it.key();
        const ActionBinding& current = it.value();

        if (!m_originalBindings.contains(key)) return true;

        const ActionBinding& original = m_originalBindings.value(key);
        if (current.binding1 != original.binding1 ||
            current.binding2 != original.binding2 ||
            current.binding3 != original.binding3) {
            return true;
        }
    }
    return false;
}

void BindingsDialog::updateApplyButton()
{
    m_applyBtn->setEnabled(hasChanges());
}

}  // namespace hobbycad
