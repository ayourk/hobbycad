// =====================================================================
//  src/hobbycad/gui/customizedialog.cpp — the Customize dialog
// =====================================================================
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include "customizedialog.h"

#include "arrangementstore.h"
#include "commandtext.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace hobbycad {

namespace {

/// The tree carries the element's key and the container it is shown in.
constexpr int kKeyRole = Qt::UserRole + 1;
constexpr int kContainerRole = Qt::UserRole + 2;

}  // namespace

CustomizeDialog::CustomizeDialog(QWidget* parent)
    : QDialog(parent), m_store(&ArrangementStore::instance())
{
    setWindowTitle(tr("Customize Menus and Toolbars"));
    resize(640, 520);

    auto* layout = new QVBoxLayout(this);
    m_note = new QLabel(tr("Menus and toolbars are rearranged here, and nowhere else. Every "
                           "change is yours alone: HobbyCAD's own layout stays put behind it, "
                           "and Restore defaults brings it back."));
    m_note->setWordWrap(true);
    layout->addWidget(m_note);

    auto* middle = new QHBoxLayout;
    layout->addLayout(middle, 1);

    m_tree = new QTreeWidget;
    m_tree->setHeaderLabels({tr("Shown"), tr("Command")});
    m_tree->setColumnWidth(0, 340);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(m_tree, &QTreeWidget::itemSelectionChanged,
            this, &CustomizeDialog::onSelectionChanged);
    middle->addWidget(m_tree, 1);

    auto* buttons = new QVBoxLayout;
    middle->addLayout(buttons);
    const auto addButton = [this, buttons](const QString& text, void (CustomizeDialog::*slot)()) {
        auto* button = new QPushButton(text);
        connect(button, &QPushButton::clicked, this, slot);
        buttons->addWidget(button);
        return button;
    };
    m_up = addButton(tr("Move Up"), &CustomizeDialog::onMoveUp);
    m_down = addButton(tr("Move Down"), &CustomizeDialog::onMoveDown);
    m_moveTo = addButton(tr("Move To..."), &CustomizeDialog::onMoveTo);
    m_rename = addButton(tr("Rename..."), &CustomizeDialog::onRename);
    m_hide = addButton(tr("Hide"), &CustomizeDialog::onToggleHidden);
    m_separator = addButton(tr("Add Separator"), &CustomizeDialog::onAddSeparator);
    buttons->addSpacing(12);
    m_restore = addButton(tr("Restore This"), &CustomizeDialog::onRestoreElement);
    m_restoreArea = addButton(tr("Restore Menus"), &CustomizeDialog::onRestoreArea);
    addButton(tr("Restore All..."), &CustomizeDialog::onRestoreAll);
    buttons->addSpacing(12);
    addButton(tr("Export..."), &CustomizeDialog::onExport);
    addButton(tr("Import..."), &CustomizeDialog::onImport);
    buttons->addStretch(1);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::accept);
    layout->addWidget(box);

    reload();
    showProblems(m_store->problems());
}

void CustomizeDialog::addChildren(QTreeWidgetItem* parent, const QString& containerId, int depth)
{
    if (depth > layout::kMaxDepth) return;
    const layout::Arrangement& arrangement = m_store->current();
    for (const layout::Element* element : arrangement.children(containerId.toStdString(), true)) {
        auto* item = new QTreeWidgetItem(parent);
        const QString key = QString::fromStdString(element->key);
        item->setData(0, kKeyRole, key);
        item->setData(0, kContainerRole, containerId);
        QString shown;
        switch (element->kind) {
        case layout::ElementKind::Separator:
            shown = tr("-- separator --");
            break;
        case layout::ElementKind::Placeholder:
            shown = tr("(filled in by HobbyCAD)");
            break;
        default:
            shown = m_store->elementText(*element);
            if (shown.isEmpty()) shown = QString::fromStdString(element->id);
            break;
        }
        if (element->hidden) shown = tr("%1  (hidden)").arg(shown);
        if (!element->userLabel.empty()) shown = tr("%1  (renamed)").arg(shown);
        item->setText(0, shown);
        item->setText(1, QString::fromStdString(element->id));
        if (element->kind == layout::ElementKind::Container) {
            addChildren(item, QString::fromStdString(element->id), depth + 1);
        }
        // A tool group's variants hang under the group.
        if (const layout::Container* variants = arrangement.variantsOf(element->id)) {
            addChildren(item, QString::fromStdString(variants->id), depth + 1);
        }
    }
}

void CustomizeDialog::reload()
{
    const QString keep = selectedKey();
    m_tree->clear();
    const layout::Arrangement& arrangement = m_store->current();
    for (const layout::Container& container : arrangement.containers()) {
        // The top of each tree: the menu bar and each toolbar.
        if (container.kind != layout::ContainerKind::MenuBar
            && container.kind != layout::ContainerKind::Toolbar) {
            continue;
        }
        auto* root = new QTreeWidgetItem(m_tree);
        QString title = QString::fromStdString(container.id);
        if (const commands::Command* command = commands::findCommand(container.title)) {
            title = commandtext::menuText(*command);
        }
        root->setText(0, title);
        root->setData(0, kContainerRole, QString::fromStdString(container.id));
        root->setExpanded(true);
        addChildren(root, QString::fromStdString(container.id), 1);
    }
    if (!keep.isEmpty()) {
        const auto found = m_tree->findItems(QString(), Qt::MatchContains | Qt::MatchRecursive, 0);
        for (QTreeWidgetItem* item : found) {
            if (item->data(0, kKeyRole).toString() != keep) continue;
            m_tree->setCurrentItem(item);
            break;
        }
    }
    onSelectionChanged();
}

QString CustomizeDialog::selectedKey() const
{
    QTreeWidgetItem* item = m_tree->currentItem();
    return item ? item->data(0, kKeyRole).toString() : QString();
}

QString CustomizeDialog::selectedContainer() const
{
    QTreeWidgetItem* item = m_tree->currentItem();
    return item ? item->data(0, kContainerRole).toString() : QString();
}

layout::RestoreArea CustomizeDialog::selectedArea() const
{
    const layout::Container* container =
        m_store->current().container(selectedContainer().toStdString());
    const layout::ContainerKind kind =
        container ? container->kind : layout::ContainerKind::Menu;
    const bool menus = kind == layout::ContainerKind::Menu
                    || kind == layout::ContainerKind::MenuBar;
    return menus ? layout::RestoreArea::Menus : layout::RestoreArea::Toolbars;
}

void CustomizeDialog::onSelectionChanged()
{
    const QString key = selectedKey();
    const layout::Element* element =
        key.isEmpty() ? nullptr : m_store->current().element(key.toStdString());
    const bool has = element != nullptr;
    const bool named = has && element->kind != layout::ElementKind::Separator
                    && element->kind != layout::ElementKind::Placeholder;
    m_up->setEnabled(has);
    m_down->setEnabled(has);
    m_moveTo->setEnabled(has);
    m_rename->setEnabled(named);
    m_hide->setEnabled(has);
    m_hide->setText(has && element->hidden ? tr("Show") : tr("Hide"));
    m_separator->setEnabled(!selectedContainer().isEmpty());
    m_restore->setEnabled(has && (m_store->customizations().find(key.toStdString()) != nullptr
                                  || key.startsWith(QLatin1String(layout::kAddedPrefix))));
    m_restoreArea->setText(selectedArea() == layout::RestoreArea::Menus ? tr("Restore Menus")
                                                                        : tr("Restore Toolbars"));
    m_restoreArea->setEnabled(!selectedContainer().isEmpty());
}

void CustomizeDialog::onMoveUp()
{
    const QString key = selectedKey();
    const QString container = selectedContainer();
    if (key.isEmpty()) return;
    const auto children = m_store->current().children(container.toStdString(), true);
    for (std::size_t i = 0; i < children.size(); ++i) {
        if (children[i]->key != key.toStdString()) continue;
        if (i == 0) return;
        m_store->move(key, container, static_cast<int>(i) - 1);
        reload();
        return;
    }
}

void CustomizeDialog::onMoveDown()
{
    const QString key = selectedKey();
    const QString container = selectedContainer();
    if (key.isEmpty()) return;
    const auto children = m_store->current().children(container.toStdString(), true);
    for (std::size_t i = 0; i < children.size(); ++i) {
        if (children[i]->key != key.toStdString()) continue;
        if (i + 1 >= children.size()) return;
        // Past the next one: the element after it in the list that stays.
        m_store->move(key, container, static_cast<int>(i) + 1);
        reload();
        return;
    }
}

void CustomizeDialog::onMoveTo()
{
    const QString key = selectedKey();
    if (key.isEmpty()) return;
    // The menus and toolbars an element may be moved into.
    QStringList names;
    QStringList ids;
    for (const layout::Container& container : m_store->current().containers()) {
        if (container.kind == layout::ContainerKind::Variants) continue;
        QString title = QString::fromStdString(container.id);
        if (const commands::Command* command = commands::findCommand(container.title)) {
            title = commandtext::menuText(*command) + QStringLiteral("  (") + title
                  + QStringLiteral(")");
        }
        names << title;
        ids << QString::fromStdString(container.id);
    }
    bool ok = false;
    const QString picked = QInputDialog::getItem(this, tr("Move To"), tr("Show it in:"), names,
                                                 ids.indexOf(selectedContainer()), false, &ok);
    if (!ok) return;
    const int at = names.indexOf(picked);
    if (at < 0) return;
    const int index = static_cast<int>(m_store->current().children(ids[at].toStdString(),
                                                                  true).size());
    m_store->move(key, ids[at], index);
    reload();
}

void CustomizeDialog::onRename()
{
    const QString key = selectedKey();
    const layout::Element* element =
        key.isEmpty() ? nullptr : m_store->current().element(key.toStdString());
    if (!element) return;
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, tr("Rename"),
        tr("Name it as you like; leave it empty for HobbyCAD's own name.\n"
           "Your name is shown exactly as typed, in every language."),
        QLineEdit::Normal, QString::fromStdString(element->userLabel), &ok);
    if (!ok) return;
    if (!name.isEmpty() && !layout::isUserLabel(name.toStdString())) {
        QMessageBox::warning(this, tr("Rename"),
                             tr("That name cannot be used: it is too long or holds characters "
                                "a name cannot have."));
        return;
    }
    m_store->setLabel(key, name);
    reload();
}

void CustomizeDialog::onToggleHidden()
{
    const QString key = selectedKey();
    const layout::Element* element =
        key.isEmpty() ? nullptr : m_store->current().element(key.toStdString());
    if (!element) return;
    m_store->setHidden(key, !element->hidden);
    reload();
}

void CustomizeDialog::onAddSeparator()
{
    const QString container = selectedContainer();
    if (container.isEmpty()) return;
    int index = static_cast<int>(m_store->current().children(container.toStdString(),
                                                             true).size());
    const QString key = selectedKey();
    if (!key.isEmpty()) {
        const auto children = m_store->current().children(container.toStdString(), true);
        for (std::size_t i = 0; i < children.size(); ++i) {
            if (children[i]->key == key.toStdString()) index = static_cast<int>(i) + 1;
        }
    }
    m_store->addSeparator(container, index);
    reload();
}

void CustomizeDialog::onRestoreElement()
{
    const QString key = selectedKey();
    if (key.isEmpty()) return;
    m_store->restoreElement(key);
    reload();
}

void CustomizeDialog::onRestoreArea()
{
    const layout::RestoreArea area = selectedArea();
    const QString what = area == layout::RestoreArea::Menus ? tr("menus") : tr("toolbars");
    if (QMessageBox::question(this, tr("Restore Defaults"),
                              tr("Give the %1 HobbyCAD's own layout back? Your changes to them "
                                 "are dropped.").arg(what))
        != QMessageBox::Yes) {
        return;
    }
    m_store->restore(area);
    reload();
}

void CustomizeDialog::onRestoreAll()
{
    if (QMessageBox::question(this, tr("Restore Defaults"),
                              tr("Give HobbyCAD's own layout back everywhere, keys included? "
                                 "Every change you have made to it is dropped."))
        != QMessageBox::Yes) {
        return;
    }
    m_store->restore(layout::RestoreArea::All);
    reload();
}

void CustomizeDialog::onExport()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("Export Arrangement"),
                                                      QStringLiteral("arrangement.json"),
                                                      tr("Arrangement files (*.json)"));
    if (path.isEmpty()) return;
    QString error;
    if (!m_store->exportTo(path, &error)) {
        QMessageBox::warning(this, tr("Export Arrangement"),
                             tr("It could not be written: %1").arg(error));
    }
}

void CustomizeDialog::onImport()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Import Arrangement"), QString(),
                                                      tr("Arrangement files (*.json)"));
    if (path.isEmpty()) return;
    QStringList problems;
    if (!m_store->importFrom(path, &problems)) {
        QMessageBox::warning(this, tr("Import Arrangement"),
                             problems.isEmpty()
                                 ? tr("That file is not an arrangement.")
                                 : tr("That file is not an arrangement:\n%1")
                                       .arg(problems.join(QLatin1Char('\n'))));
        return;
    }
    reload();
    showProblems(problems);
}

void CustomizeDialog::showProblems(const QStringList& problems)
{
    if (problems.isEmpty()) return;
    QMessageBox::information(this, tr("Arrangement"),
                             tr("Some of the arrangement was skipped:\n%1")
                                 .arg(problems.join(QLatin1Char('\n'))));
}

}  // namespace hobbycad
