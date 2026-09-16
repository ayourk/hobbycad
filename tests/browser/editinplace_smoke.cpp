// =====================================================================
//  tests/browser/editinplace_smoke.cpp — F2 rename-in-place targeting
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#include <QApplication>
#include <QTableWidget>
#include <QTreeWidget>

#include "editinplace.h"

#include <cstdio>

using namespace hobbycad;
static int fails = 0;
static void ck(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++fails;
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    // ---- a tree, as the objects browser uses ------------------------
    {
        QTreeWidget tree;
        tree.setColumnCount(2);

        auto* editable = new QTreeWidgetItem(&tree);
        editable->setText(0, QStringLiteral("Sketch1"));
        editable->setFlags(editable->flags() | Qt::ItemIsEditable);

        auto* readOnly = new QTreeWidgetItem(&tree);
        readOnly->setText(0, QStringLiteral("XY Plane"));
        readOnly->setFlags(readOnly->flags() & ~Qt::ItemIsEditable);

        tree.setCurrentItem(editable);
        ck(editTargetFor(&tree, 0).isValid(), "an editable row offers a target");
        ck(editTargetFor(&tree, 0).column() == 0, "in the label column");

        tree.setCurrentItem(readOnly);
        ck(!editTargetFor(&tree, 0).isValid(),
           "a read-only row offers none, so F2 opens no dead editor");
    }

    // ---- pressing F2 on a different column still edits the label ----
    {
        QTableWidget table(1, 3);
        for (int c = 0; c < 3; ++c) {
            auto* cell = new QTableWidgetItem(QStringLiteral("v"));
            cell->setFlags(cell->flags() | Qt::ItemIsEditable);
            table.setItem(0, c, cell);
        }
        table.setCurrentCell(0, 2);          // cursor on the LAST column
        const QModelIndex target = editTargetFor(&table, 0);
        ck(target.isValid(), "a table row offers a target");
        ck(target.column() == 0,
           "F2 from another column still edits the NAME column");
        ck(target.row() == 0, "on the row the cursor was on");
    }

    // ---- a read-only name cell beside editable ones -----------------
    {
        QTableWidget table(1, 2);
        auto* name = new QTableWidgetItem(QStringLiteral("computed"));
        name->setFlags(name->flags() & ~Qt::ItemIsEditable);
        table.setItem(0, 0, name);
        auto* value = new QTableWidgetItem(QStringLiteral("42"));
        value->setFlags(value->flags() | Qt::ItemIsEditable);
        table.setItem(0, 1, value);

        table.setCurrentCell(0, 1);
        ck(!editTargetFor(&table, 0).isValid(),
           "an editable row with a locked name still refuses to rename");
    }

    // ---- no selection at all -----------------------------------------
    {
        QTreeWidget empty;
        ck(!editTargetFor(&empty, 0).isValid(), "an empty view offers nothing");
        ck(!editTargetFor(nullptr, 0).isValid(), "and a null view is safe");
    }

    std::printf("\n%s (%d failure(s))\n", fails ? "FAILURES" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
