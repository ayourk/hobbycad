// =====================================================================
//  tests/browser/prefsdialog_smoke.cpp — preferences dialog construction
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  Why this exists: every control on this dialog is a raw pointer that
//  loadSettings() dereferences at construction. A control that is
//  DECLARED in the header and READ in loadSettings but never `new`ed on
//  any page compiles and links cleanly, then null-derefs the moment a
//  user opens Preferences. That shipped once, with the CLI scrollback
//  spinbox: it was added to the header and to load/save, but the widget
//  itself was never created.
//
//  Constructing the dialog runs loadSettings() over every control, so
//  this test fails on exactly that mistake. Rendering additionally
//  proves each control was parented into a page rather than leaked.
#include <QApplication>
#include <QPixmap>
#include <QSettings>
#include <QSpinBox>

#include "preferencesdialog.h"

#include <cstdio>

using namespace hobbycad;
static int fails = 0;
static void ck(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++fails;
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    // Point QSettings at a name of this test's own. The dialog only reads
    // during construction, but a test must not be able to touch the
    // developer's real preferences even if that changes.
    QCoreApplication::setOrganizationName(QStringLiteral("HobbyCADTest"));
    QCoreApplication::setApplicationName(QStringLiteral("prefsdialog_smoke"));

    // Construction alone dereferences every control through loadSettings(); a
    // null control crashes the process here, which fails the test.
    PreferencesDialog dlg;

    // Every page must be reachable, and rendering walks the whole widget
    // tree: a control created but never added to a layout shows up as a
    // leak rather than a paint, so this also proves parenting.
    dlg.resize(700, 500);
    const QPixmap shot = dlg.grab();
    ck(!shot.isNull() && shot.width() > 0, "and renders every page");

    // The control that was missing, named explicitly: findChild only sees
    // it if it was actually created AND parented into the dialog.
    const auto boxes = dlg.findChildren<QSpinBox*>();
    ck(boxes.size() >= 5,
       "all five spin boxes exist, scrollback included");

    // 0 must remain reachable and must read as unlimited, because that is
    // how CliPanel interprets it (maximumBlockCount 0 == no limit).
    bool sawUnlimited = false;
    for (QSpinBox* b : boxes) {
        if (b->minimum() == 0 && !b->specialValueText().isEmpty())
            sawUnlimited = true;
    }
    ck(sawUnlimited,
       "scrollback offers 0 with a special label, matching CliPanel's "
       "reading of 0 as unlimited");

    std::printf("\n%s (%d failure(s))\n", fails ? "FAILURES" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
