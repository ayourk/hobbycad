// =====================================================================
//  tests/browser/outline_smoke.cpp — shared error-outline delegate
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#include <QApplication>
#include <QLineEdit>
#include <QPixmap>
#include <QTableWidget>
#include <QTreeWidget>

#include "erroroutlinedelegate.h"

#include <cstdio>

using namespace hobbycad;
static int fails = 0;
static void ck(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++fails;
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    // The predicate is keyed on the index, so it works for a TREE, where a
    // row number is only meaningful relative to a parent. A (row, column)
    // predicate marked the wrong node: the first child of every parent
    // shares row 0.
    {
        QTreeWidget tree;
        auto* parentA = new QTreeWidgetItem(&tree);
        parentA->setText(0, QStringLiteral("Bodies"));
        auto* childA = new QTreeWidgetItem(parentA);
        childA->setText(0, QStringLiteral("Body1"));
        childA->setFlags(childA->flags() | Qt::ItemIsEditable);

        auto* parentB = new QTreeWidgetItem(&tree);
        parentB->setText(0, QStringLiteral("Sketches"));
        auto* childB = new QTreeWidgetItem(parentB);
        childB->setText(0, QStringLiteral("Sketch1"));
        childB->setFlags(childB->flags() | Qt::ItemIsEditable);
        tree.expandAll();

        ck(tree.indexFromItem(childA).row() == tree.indexFromItem(childB).row(),
           "two different nodes really do share a row number");

        QSet<QTreeWidgetItem*> bad;
        bad.insert(childB);
        int flagged = 0;
        ErrorOutlineDelegate delegate(
            [&](const QModelIndex& i) {
                const bool isBad = bad.contains(tree.itemFromIndex(i));
                if (isBad) ++flagged;
                return isBad;
            },
            &tree);
        tree.setItemDelegate(&delegate);

        tree.resize(300, 200);
        const QPixmap shot = tree.grab();
        ck(!shot.isNull(), "the tree renders through the delegate");
        ck(flagged > 0, "and the flagged node was identified during paint");
    }

    // The caret is captured at commit, before the editor is destroyed.
    {
        QTableWidget table(1, 1);
        auto* cell = new QTableWidgetItem(QStringLiteral("width"));
        cell->setFlags(cell->flags() | Qt::ItemIsEditable);
        table.setItem(0, 0, cell);

        ErrorOutlineDelegate delegate([](const QModelIndex&) { return false; },
                                      &table);
        ck(delegate.lastCursorPosition() == -1,
           "no caret is remembered before any edit");

        QLineEdit editor;
        editor.setText(QStringLiteral("widthx"));
        editor.setCursorPosition(3);          // mid-word, as if Enter was hit
        delegate.setModelData(&editor, table.model(), table.model()->index(0, 0));

        ck(delegate.lastCursorPosition() == 3,
           "the caret offset is captured at commit, not the end of the text");
        ck(table.item(0, 0)->text() == QStringLiteral("widthx"),
           "and the value still commits normally");
    }

    std::printf("\n%s (%d failure(s))\n", fails ? "FAILURES" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
