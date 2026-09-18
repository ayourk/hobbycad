// =====================================================================
//  src/hobbycad/gui/customizedialog.h — moving, renaming and hiding what
//  the menus and toolbars show
// =====================================================================
//
//  The one place where HobbyCAD's layout is rearranged, Blender-style:
//  menus and toolbars stay put in normal use, and everything that moves
//  moves here. Each change is a customization in arrangement.json
//  (ArrangementStore), so the default is never edited and "Restore
//  defaults" is a matter of dropping entries.
//
//  Keys are not here: they have their own editor (BindingsDialog), which
//  writes to the same file.
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_CUSTOMIZEDIALOG_H
#define HOBBYCAD_CUSTOMIZEDIALOG_H

#include <hobbycad/layout/customization.h>

#include <QDialog>
#include <QString>

class QLabel;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace hobbycad {

class ArrangementStore;

class CustomizeDialog : public QDialog {
    Q_OBJECT

public:
    explicit CustomizeDialog(QWidget* parent = nullptr);

private slots:
    void onSelectionChanged();
    void onMoveUp();
    void onMoveDown();
    void onMoveTo();
    void onRename();
    void onToggleHidden();
    void onAddSeparator();
    void onRestoreElement();
    void onRestoreArea();
    void onRestoreAll();
    void onExport();
    void onImport();

private:
    /// Fill the tree from the arrangement, keeping the selected element
    /// selected where it still exists.
    void reload();
    void addChildren(QTreeWidgetItem* parent, const QString& containerId, int depth);
    /// The element the person has selected, or "" for a container row.
    QString selectedKey() const;
    QString selectedContainer() const;
    /// Which area the selected element sits in.
    layout::RestoreArea selectedArea() const;
    void showProblems(const QStringList& problems);

    ArrangementStore* m_store = nullptr;
    QTreeWidget* m_tree = nullptr;
    QLabel* m_note = nullptr;
    QPushButton* m_up = nullptr;
    QPushButton* m_down = nullptr;
    QPushButton* m_moveTo = nullptr;
    QPushButton* m_rename = nullptr;
    QPushButton* m_hide = nullptr;
    QPushButton* m_separator = nullptr;
    QPushButton* m_restore = nullptr;
    QPushButton* m_restoreArea = nullptr;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_CUSTOMIZEDIALOG_H
