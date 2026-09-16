// =====================================================================
//  tests/browser/clipager_smoke.cpp — GUI terminal paging on resize
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  Aaron's case: 50 lines of output, a viewport that shows ~25, shrunk to
//  ~20, then grown past the whole output.
#include <QApplication>
#include <QKeyEvent>
#include <QPlainTextEdit>
#include <cstdio>

#include "clipanel.h"

using namespace hobbycad;
static int fails = 0;
static void ck(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++fails;
}

/// Height in pixels that shows roughly `lines` text lines.
static int heightFor(const CliPanel& p, int lines) {
    return p.fontMetrics().lineSpacing() * (lines + 1) + 8;
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    CliPanel panel;
    panel.show();

    QString big;
    for (int i = 1; i <= 50; ++i) {
        big += QStringLiteral("line %1\n").arg(i);
    }
    big.chop(1);

    const int before = panel.toPlainText().count(QLatin1Char('\n'));

    panel.resize(600, heightFor(panel, 25));
    panel.appendPaginatedForTest(big);
    const int after25 = panel.toPlainText().count(QLatin1Char('\n'));

    ck(after25 - before < 50,
       "a 50-line result is NOT all shown in a 25-line viewport");
    ck(panel.toPlainText().contains(QStringLiteral("more line")),
       "and the held-back lines are announced");

    // Shrinking reveals nothing new: the lines already written stay put and
    // the widget scrolls, which is what a text view is for.
    const int shownAt25 = after25;
    panel.resize(600, heightFor(panel, 20));
    ck(panel.toPlainText().count(QLatin1Char('\n')) == shownAt25,
       "shrinking does not reveal more, and does not lose what was shown");

    // EXACTLY the output height. The "-- N more --" marker takes a line of
    // its own, so a naive "does the remainder fit" test leaves one line
    // held back and spends that line announcing it. Showing all 50 uses no
    // more room than 49-plus-a-marker, so the pager must finish here.
    panel.resize(600, heightFor(panel, 50));
    {
        const QString t = panel.toPlainText();
        ck(t.contains(QStringLiteral("line 50")),
           "at exactly the output height, the last line is shown");
        ck(!t.contains(QStringLiteral("more line")),
           "and the pager does not linger to announce a single line");
        ck(t.contains(QStringLiteral("(END)")),
           "a pager that finished because the panel GREW says so");
    }

    // Growing further changes nothing: paging already finished.
    panel.resize(600, heightFor(panel, 80));
    const QString finalText = panel.toPlainText();
    ck(finalText.contains(QStringLiteral("line 50")),
       "growing past the output reveals the last line");
    ck(!finalText.contains(QStringLiteral("more line")),
       "and the more-marker is gone: paging finished");

    // (END) is acknowledgement, not content: a keypress clears the WORD and
    // leaves the LINE, so a blank row separates the output from the prompt.
    {
        CliPanel p2;
        p2.show();
        p2.resize(600, heightFor(p2, 10));
        QString twenty;
        for (int i = 1; i <= 20; ++i) twenty += QStringLiteral("row %1\n").arg(i);
        twenty.chop(1);
        p2.appendPaginatedForTest(twenty);
        ck(p2.toPlainText().contains(QStringLiteral("more line")),
           "20 rows in a 10-row panel pages");

        p2.resize(600, heightFor(p2, 40));       // grows past the remainder
        ck(p2.toPlainText().contains(QStringLiteral("(END)")),
           "growing finishes the pager and marks the end");

        QKeyEvent key(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier,
                      QStringLiteral("a"));
        QApplication::sendEvent(&p2, &key);

        const QString after = p2.toPlainText();
        ck(!after.contains(QStringLiteral("(END)")),
           "a keypress clears the marker");
        ck(after.contains(QStringLiteral("row 20\n\n")),
           "leaving a BLANK line after the last output row, not closing the gap");
    }

    std::printf("\n%s (%d failure(s))\n", fails ? "FAILURES" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
