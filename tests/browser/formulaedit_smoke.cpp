// =====================================================================
//  tests/browser/formulaedit_smoke.cpp — dimension/formula field
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#include <QApplication>
#include <QKeyEvent>
#include <QPixmap>

#include "formulaedit.h"

#include <cstdio>

using namespace hobbycad;
static int fails = 0;
static void ck(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++fails;
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    FormulaEdit edit;
    edit.resize(200, 28);

    // ONE counter and ONE connection for the whole test. A lambda made
    // inside a block and capturing that block's counter by reference
    // outlives it: the connection is never disconnected, so a later Enter
    // fires the stale lambda and writes into a dead stack frame.
    int committed = 0;
    QObject::connect(&edit, &QLineEdit::returnPressed, [&committed]() {
        ++committed;
    });

    edit.setText(QStringLiteral("10 + 5"));
    ck(edit.isValid(), "a well-formed expression is valid");
    ck(!edit.grab().isNull(), "and the field paints, outline included");

    edit.setText(QStringLiteral("10 +"));
    ck(!edit.isValid(), "a malformed one is not");
    ck(!edit.grab().isNull(), "and still paints, now with the red outline");

    // Enter on an invalid expression must be swallowed: committing would
    // hand the caller a stale value while the field shows something else.
    {
        committed = 0;
        committed = 0;
        edit.setText(QStringLiteral("10 +"));
        edit.setCursorPosition(2);          // mid-expression

        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QApplication::sendEvent(&edit, &enter);

        ck(committed == 0, "Enter on an invalid expression is refused");
        ck(edit.text() == QStringLiteral("10 +"),
           "the text the user typed is left alone");
        ck(edit.cursorPosition() == 2, "and the caret does not move");
        ck(!edit.hasSelectedText(), "with nothing selected to be overwritten");
    }

    // Enter on a valid one still commits.
    {
        committed = 0;
        edit.setText(QStringLiteral("42"));
        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QApplication::sendEvent(&edit, &enter);
        ck(committed == 1, "Enter on a valid expression still commits");
    }

    // An empty field is not an error the user has made yet.
    {
        committed = 0;
        edit.setText(QString());
        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QApplication::sendEvent(&edit, &enter);
        ck(committed == 1, "Enter on an empty field is not blocked");
    }

    std::printf("\n%s (%d failure(s))\n", fails ? "FAILURES" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
