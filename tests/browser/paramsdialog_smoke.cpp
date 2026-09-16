// =====================================================================
//  tests/browser/paramsdialog_smoke.cpp — parameters dialog rendering
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  Narrow on purpose: the dialog's edit handling is private, so this
//  covers what IS reachable: that the error-outline delegate's paint
//  path runs over a populated table without faulting, and that the
//  dialog round-trips the parameters it was given.
#include <QApplication>
#include <QPixmap>

#include "parametersdialog.h"

#include <cstdio>

using namespace hobbycad;
static int fails = 0;
static void ck(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++fails;
}

static Parameter mk(const char* name, const char* expr) {
    Parameter p;
    p.name = name;
    p.expression = expr;
    p.unit = "mm";
    return p;
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    ParametersDialog dlg;
    QList<Parameter> params;
    params << mk("width", "50")
           << mk("height", "width * 2")
           << mk("depth", "width + height");
    dlg.setParameters(params);

    ck(dlg.parameters().size() == 3, "the dialog holds what it was given");

    // Rendering drives ErrorOutlineDelegate::paint() for every cell. A bad
    // predicate or a null dereference there would fault here rather than
    // in front of a user.
    dlg.resize(800, 400);
    const QPixmap shot = dlg.grab();
    ck(!shot.isNull(), "the dialog renders, so the outline delegate paints");
    ck(shot.width() > 0 && shot.height() > 0, "with a real surface");

    // Names must survive the round trip unchanged: the rename path
    // rewrites expressions, and a stray rewrite on load would show up here.
    const auto out = dlg.parameters();
    ck(out.size() == 3 && out[1].name == "height",
       "names round-trip unchanged");
    ck(out.size() == 3 && out[1].expression == "width * 2",
       "and so do expressions, with no rewrite on load");

    std::printf("\n%s (%d failure(s))\n", fails ? "FAILURES" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
