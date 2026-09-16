// =====================================================================
//  src/hobbycad/gui/objectsbrowserwidget.h — objects browser (tab 1)
//  Repository: ayourk/hobbycad
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//
//  Renders a hobbycad::BrowserNode tree into a QTreeWidget.
//
//  All decisions about WHAT nodes exist live in libhobbycad
//  (hobbycad/browser.h); this class only turns them into widgets and
//  reports clicks. That split is what makes the tree testable without a
//  built window, and it is what a second front end would reuse.
//
#ifndef HOBBYCAD_OBJECTSBROWSERWIDGET_H
#define HOBBYCAD_OBJECTSBROWSERWIDGET_H

#include <QPair>
#include <QStringList>
#include <QWidget>

#include <hobbycad/browser.h>

#include "erroroutlinedelegate.h"

class QTreeWidget;

#include <QSet>
class QTreeWidgetItem;

namespace hobbycad {

/// Objects browser: the semantic view of the design (tab 1 of the
/// Project dock). The file browser is a separate widget and a separate
/// tab; see ProjectBrowserWidget.
class ObjectsBrowserWidget : public QWidget {
    Q_OBJECT

public:
    /// What the user was looking at, so a rebuild does not lose it.
    struct ViewState {
        QList<QPair<int, int>> expanded;   ///< (type, id) of expanded nodes
        QStringList expandedFolders;       ///< folder keys, which have no id
        int currentType = -1;
        int currentId = -1;
        int scroll = 0;
    };

    explicit ObjectsBrowserWidget(QWidget* parent = nullptr);

    /// Capture expansion, selection and scroll position.
    ViewState viewState() const;

    /// Reapply what viewState() captured, ignoring anything now absent.
    void restoreViewState(const ViewState& state);

    /// Replace the whole tree from a model.
    void setTree(const BrowserNode& root);

    /// The underlying widget.
    ///
    /// Exposed deliberately during the migration off the inline tree in
    /// MainWindow: existing handlers and the addXToTree() helpers hold a
    /// QTreeWidget* and keep working unchanged. New code should go
    /// through setTree() instead, and this accessor should disappear once
    /// nothing outside needs it.
    QTreeWidget* treeWidget() const { return m_tree; }

    /// Item for a node type plus payload, or nullptr.
    QTreeWidgetItem* itemFor(NodeType type, int id) const;

    /// Accept a rename: clear any red flag and make the new text the
    /// baseline for the next edit.
    void acceptRename(QTreeWidgetItem* item);

    /// Refuse a rename: flag the row red, reopen the
    /// editor with the caret exactly where it was, and shake it.
    void rejectRename(QTreeWidgetItem* item, const QString& reason);

signals:
    /// A node was activated (double-clicked or Enter).
    void nodeActivated(hobbycad::NodeType type, int id);

    /// The user renamed a node in place.
    void nodeRenamed(hobbycad::NodeType type, int id, const QString& name);

    /// A node's visibility checkbox was toggled by the user.
    void visibilityChanged(hobbycad::NodeType type, int id, bool visible);

    /// The user asked to relink an external reference that will not resolve.
    /// Carries the path that failed, so the file dialog can start there.
    void relinkRequested(hobbycad::NodeType type, int id, const QString& currentPath);

    /// A context-menu action was chosen. actionId is the stable id from
    /// NodeTypeInfo::actions(), never the translated label.
    void actionTriggered(hobbycad::NodeType type, int id, const QString& actionId);

private slots:
    void showContextMenu(const QPoint& pos);

private:
    QTreeWidgetItem* build(const BrowserNode& node, QTreeWidgetItem* parent);

    QTreeWidget* m_tree = nullptr;
    ErrorOutlineDelegate* m_outlineDelegate = nullptr;
    bool m_populating = false;   ///< Suppress signals during a rebuild
    QSet<QTreeWidgetItem*> m_badItems;  ///< Rows currently flagged red
};

}  // namespace hobbycad

#endif  // HOBBYCAD_OBJECTSBROWSERWIDGET_H
