// =====================================================================
//  src/hobbycad/gui/objectsbrowserwidget.cpp — objects browser (tab 1)
//  Repository: ayourk/hobbycad
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#include "objectsbrowserwidget.h"

#include "editinplace.h"

#include <QMenu>
#include <QTimer>
#include <QPropertyAnimation>
#include <QLineEdit>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStyle>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace hobbycad {

namespace {

/// Badge pixmap for a node's situation. Standard platform icons, so a
/// warning here looks like a warning everywhere else in the application.
QIcon badgeIcon(QStyle* style, NodeBadge badge)
{
    switch (badge) {
    case NodeBadge::Error:
    case NodeBadge::MissingLink:
        // A reference that cannot be resolved is an error, not a warning:
        // the model it should contain is simply absent.
        return style->standardIcon(QStyle::SP_MessageBoxCritical);
    case NodeBadge::Warning:
        return style->standardIcon(QStyle::SP_MessageBoxWarning);
    case NodeBadge::ExternalLink:
        return style->standardIcon(QStyle::SP_FileLinkIcon);
    case NodeBadge::Modified:
    case NodeBadge::Untracked:
    case NodeBadge::IntentToTrack:
    case NodeBadge::None:
        break;
    }
    return QIcon();
}

}  // namespace

ObjectsBrowserWidget::ObjectsBrowserWidget(QWidget* parent)
    : QWidget(parent)
{
    m_tree = new QTreeWidget(this);
    m_tree->setObjectName(QStringLiteral("ObjectsTree"));
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_tree);

    // Same outlining as the parameters dialog: every editable label gets a
    // border, dark when fine and red when the last rename was refused.
    m_outlineDelegate = new ErrorOutlineDelegate(
        [this](const QModelIndex& index) {
            return m_badItems.contains(m_tree->itemFromIndex(index));
        },
        m_tree);
    m_tree->setItemDelegate(m_outlineDelegate);

    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tree, &QTreeWidget::customContextMenuRequested,
            this, &ObjectsBrowserWidget::showContextMenu);

    connect(m_tree, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem* item, int) {
        if (!item) return;
        emit nodeActivated(
            static_cast<NodeType>(item->data(0, Qt::UserRole + 2).toInt()),
            item->data(0, Qt::UserRole + 1).toInt());
    });

    connect(m_tree, &QTreeWidget::itemChanged, this,
            [this](QTreeWidgetItem* item, int column) {
        // A renamed label: report it so the owner can accept or refuse.
        if (!m_populating && column == 0 && item
            && (item->flags() & Qt::ItemIsEditable)) {
            const QString typed = item->text(0);
            const QString previous = item->data(0, Qt::UserRole + 5).toString();
            if (!previous.isEmpty() && typed != previous) {
                // Optimistic: clear any previous rejection. The owner calls
                // rejectRename() to put it back if this one is refused too.
                m_badItems.remove(item);
                item->setToolTip(0, QString());
                emit nodeRenamed(
                    static_cast<NodeType>(item->data(0, Qt::UserRole + 2).toInt()),
                    item->data(0, Qt::UserRole + 1).toInt(),
                    typed);
            }
        }
        // Rebuilding sets check states programmatically and each write
        // emits itemChanged; without this guard a repopulate would toggle
        // the visibility of everything in the tree.
        if (m_populating || column != 0 || !item) return;
        if (!(item->flags() & Qt::ItemIsUserCheckable)) return;
        emit visibilityChanged(
            static_cast<NodeType>(item->data(0, Qt::UserRole + 2).toInt()),
            item->data(0, Qt::UserRole + 1).toInt(),
            item->checkState(0) == Qt::Checked);
    });
}

void ObjectsBrowserWidget::rejectRename(QTreeWidgetItem* item,
                                       const QString& reason)
{
    if (!item) {
        return;
    }

    const QString previous = item->data(0, Qt::UserRole + 5).toString();
    m_badItems.insert(item);
    item->setToolTip(0, reason);

    // Repaint so the outline turns red before the editor reopens over it.
    m_tree->viewport()->update();

    reopenRejectedEdit(m_tree, m_tree->indexFromItem(item, 0),
                       m_outlineDelegate);

    Q_UNUSED(previous);
}

void ObjectsBrowserWidget::acceptRename(QTreeWidgetItem* item)
{
    if (!item) {
        return;
    }
    m_badItems.remove(item);
    item->setToolTip(0, QString());
    // The accepted text becomes the baseline the next edit is compared to.
    item->setData(0, Qt::UserRole + 5, item->text(0));
    m_tree->viewport()->update();
}

void ObjectsBrowserWidget::showContextMenu(const QPoint& pos)
{
    QTreeWidgetItem* item = m_tree->itemAt(pos);
    if (!item) {
        return;
    }

    // Rebuild just enough of the node for the type to decide its menu. The
    // menu is NOT assembled here: what a node offers is a property of its
    // type, and duplicating that knowledge in the widget is how the two
    // drift apart.
    BrowserNode node;
    node.type = static_cast<NodeType>(item->data(0, Qt::UserRole + 2).toInt());
    node.id = item->data(0, Qt::UserRole + 1).toInt();
    node.flags = item->data(0, Qt::UserRole + 3).toUInt();
    node.refPath = item->data(0, Qt::UserRole + 4).toString().toStdString();
    node.visible = (item->flags() & Qt::ItemIsUserCheckable)
                       ? item->checkState(0) == Qt::Checked
                       : true;

    const std::vector<NodeAction> actions = nodeTypeInfo(node.type).actions(node);
    if (actions.empty()) {
        return;   // system folders offer nothing, and show no empty menu
    }

    QMenu menu(this);
    bool pendingSeparator = false;

    for (const NodeAction& action : actions) {
        if (action.separatorBefore && !menu.isEmpty()) {
            pendingSeparator = true;
        }
        if (pendingSeparator) {
            menu.addSeparator();
            pendingSeparator = false;
        }

        QAction* entry = menu.addAction(QString::fromStdString(action.label));
        const QString actionId = QString::fromStdString(action.id);

        connect(entry, &QAction::triggered, this, [this, node, actionId, item]() {
            // Two actions are handled by the widget because they are pure
            // view operations; everything else is the window's business.
            if (actionId == QLatin1String("rename")) {
                m_tree->editItem(item, 0);
                return;
            }
            if (actionId == QLatin1String("toggle_visibility")) {
                item->setCheckState(0,
                    item->checkState(0) == Qt::Checked ? Qt::Unchecked : Qt::Checked);
                return;
            }
            if (actionId == QLatin1String("relink")) {
                emit relinkRequested(node.type, node.id,
                                     QString::fromStdString(node.refPath));
                return;
            }
            emit actionTriggered(node.type, node.id, actionId);
        });
    }

    menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

QTreeWidgetItem* ObjectsBrowserWidget::build(const BrowserNode& node,
                                             QTreeWidgetItem* parent)
{
    auto* item = parent ? new QTreeWidgetItem(parent)
                        : new QTreeWidgetItem(m_tree);

    QString label = QString::fromStdString(node.name);
    if (node.type == NodeType::Setting && !node.detail.empty()) {
        // Keep the legacy "Units: mm" spelling; the inline editor parses it.
        label += QStringLiteral(": ") + QString::fromStdString(node.detail);
    }
    item->setText(0, label);

    // The tag comes from the type registry in the library, so the widget
    // no longer carries its own switch that had to be kept in step.
    const QString tag = QString::fromLatin1(nodeTypeInfo(node.type).legacyTag());
    if (!tag.isEmpty()) {
        item->setData(0, Qt::UserRole, tag);
    }
    item->setData(0, Qt::UserRole + 1, node.id);
    item->setData(0, Qt::UserRole + 2, static_cast<int>(node.type));
    item->setData(0, Qt::UserRole + 3, node.flags);
    item->setData(0, Qt::UserRole + 4, QString::fromStdString(node.refPath));
    // The last accepted label, so a rejected rename can be put back.
    item->setData(0, Qt::UserRole + 5, label);

    if (node.has(NodeRenameable)) {
        item->setFlags(item->flags() | Qt::ItemIsEditable);
    }
    if (node.has(NodeVisible)) {
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, node.visible ? Qt::Checked : Qt::Unchecked);
    }

    const QIcon badge = badgeIcon(style(), node.badge);
    if (!badge.isNull()) {
        item->setIcon(0, badge);
    }
    // Tooltip. For a reference the PATH is the useful text: when a link
    // cannot be resolved that path is the only thing that lets the user find
    // the file again, so it must be visible without opening a dialog.
    QString tip;
    if (!node.refPath.empty()) {
        tip = QString::fromStdString(node.refPath);
        if (node.refState == ReferenceState::Missing) {
            tip = tr("Cannot find:") + QLatin1Char(' ') + tip;
        }
    } else if (!node.detail.empty() && node.type != NodeType::Setting) {
        tip = QString::fromStdString(node.detail);
    }
    if (!tip.isEmpty()) {
        item->setToolTip(0, tip);
    }

    for (const BrowserNode& child : node.children) {
        build(child, item);
    }

    // Origin is collapsed by default as in Fusion; content folders open.
    item->setExpanded(node.type != NodeType::OriginFolder);
    return item;
}

void ObjectsBrowserWidget::setTree(const BrowserNode& root)
{
    m_populating = true;
    const QSignalBlocker block(m_tree);
    m_tree->clear();
    for (const BrowserNode& child : root.children) {
        build(child, nullptr);
    }
    m_populating = false;
}

ObjectsBrowserWidget::ViewState ObjectsBrowserWidget::viewState() const
{
    ViewState state;
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it) {
        QTreeWidgetItem* item = *it;
        const int type = item->data(0, Qt::UserRole + 2).toInt();
        const int id = item->data(0, Qt::UserRole + 1).toInt();

        if (item->isExpanded()) {
            // Folders share a type and carry id -1, so they are keyed by
            // name instead; otherwise expanding Bodies would also expand
            // Sketches on the next rebuild.
            if (item->childCount() > 0 && id < 0) {
                state.expandedFolders << item->text(0);
            } else {
                state.expanded.append(qMakePair(type, id));
            }
        }
        if (item == m_tree->currentItem()) {
            state.currentType = type;
            state.currentId = id;
        }
    }
    if (m_tree->verticalScrollBar()) {
        state.scroll = m_tree->verticalScrollBar()->value();
    }
    return state;
}

void ObjectsBrowserWidget::restoreViewState(const ViewState& state)
{
    const QSignalBlocker block(m_tree);
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it) {
        QTreeWidgetItem* item = *it;
        const int type = item->data(0, Qt::UserRole + 2).toInt();
        const int id = item->data(0, Qt::UserRole + 1).toInt();

        if (item->childCount() > 0 && id < 0) {
            item->setExpanded(state.expandedFolders.contains(item->text(0)));
        } else if (item->childCount() > 0) {
            item->setExpanded(state.expanded.contains(qMakePair(type, id)));
        }

        if (type == state.currentType && id == state.currentId
            && state.currentType >= 0) {
            m_tree->setCurrentItem(item);
        }
    }
    if (m_tree->verticalScrollBar()) {
        m_tree->verticalScrollBar()->setValue(state.scroll);
    }
}

QTreeWidgetItem* ObjectsBrowserWidget::itemFor(NodeType type, int id) const
{
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it) {
        if (static_cast<NodeType>((*it)->data(0, Qt::UserRole + 2).toInt()) == type
            && (*it)->data(0, Qt::UserRole + 1).toInt() == id) {
            return *it;
        }
    }
    return nullptr;
}

}  // namespace hobbycad
