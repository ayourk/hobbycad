// =====================================================================
//  tests/browser/toolbar_smoke.cpp — toolbars built from the arrangement
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  The sketch and model toolbars used to list their tools, captions and
//  dropdown indices by hand, once per handler; they are now laid out from
//  the default arrangement (hobbycad/layout/arrangement.h). These checks
//  click through them and hold what a user sees and what each click
//  reports to what the hand-written toolbars did: Create opens its list
//  first, the other groups run their default tool, a variant is picked
//  from a row's submenu, a transform is not a tool, the latches report
//  their state, and the tooltips name the bound key.
#include <QApplication>
#include <QMenu>
#include <QToolButton>

#include "modeltoolbar.h"
#include "sketchtoolbar.h"
#include "toolbarbutton.h"
#include "toolbardropdown.h"

#include <cstdio>

using namespace hobbycad;
static int fails = 0;
static void ck(bool ok, const char* what)
{
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++fails;
}

/// The toolbar's buttons in order.
static QList<ToolbarButton*> buttons(QWidget* bar)
{
    return bar->findChildren<ToolbarButton*>(QString(), Qt::FindDirectChildrenOnly);
}

/// The clickable part of a toolbar button.
static QToolButton* face(ToolbarButton* b)
{
    return b->findChildren<QToolButton*>(QString(), Qt::FindDirectChildrenOnly).value(0);
}

/// The dropdown row showing `text`.
static QToolButton* row(ToolbarButton* b, const QString& text)
{
    for (QToolButton* t : b->dropdown()->findChildren<QToolButton*>()) {
        if (t->text() == text) return t;
    }
    return nullptr;
}

/// Trigger a variant in the submenu of the `menu`-th row that has one.
/// Modes restart at 0 per tool, so Line's and Arc's "Tangent" share an id;
/// the row is what tells them apart.
static bool pickVariant(ToolbarButton* b, int menu, CreationMode mode,
                        const QString& expectText)
{
    const QList<QMenu*> menus = b->dropdown()->findChildren<QMenu*>();
    if (menu < 0 || menu >= menus.size()) return false;
    for (QAction* a : menus[menu]->actions()) {
        if (a->data().toInt() == static_cast<int>(mode) && a->text() == expectText) {
            a->trigger();
            return true;
        }
    }
    return false;
}

// Rows of the Create list that have variants, in order.
enum { LineMenu = 0, RectangleMenu, CircleMenu, ArcMenu };

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    std::printf("toolbars built from the arrangement\n");

    // ---- sketch toolbar -----------------------------------------------------
    {
        SketchToolbar bar;
        const QList<ToolbarButton*> b = buttons(&bar);
        ck(b.size() == 7, "sketch toolbar: four groups, 3D, Flip and Finish");
        if (b.size() != 7) return 1;
        ToolbarButton* create = b[0];
        ToolbarButton* constrain = b[1];
        ToolbarButton* modify = b[2];
        ToolbarButton* pattern = b[3];

        ck(face(create)->text() == QStringLiteral("Create")
               && face(constrain)->text() == QStringLiteral("Dimension")
               && face(modify)->text() == QStringLiteral("Trim")
               && face(pattern)->text() == QStringLiteral("Rect\nPattern")
               && face(b[4])->text() == QStringLiteral("3D")
               && face(b[5])->text() == QStringLiteral("Flip")
               && face(b[6])->text() == QStringLiteral("Finish Sketch"),
           "captions match the hand-written toolbar");
        ck(create->toolTip() == QStringLiteral("Create geometry")
               && face(create)->toolTip().isEmpty(),
           "a group shows its own tooltip before a pick");

        SketchTool gotTool = SketchTool::Select;
        CreationMode gotMode = CreationMode::Default;
        int selections = 0;
        QObject::connect(&bar, &SketchToolbar::toolSelected,
                         [&](SketchTool t, CreationMode m) {
            gotTool = t;
            gotMode = m;
            ++selections;
        });

        face(create)->click();
        ck(selections == 0, "Create opens its list first and selects nothing");

        face(constrain)->click();
        ck(selections == 1 && gotTool == SketchTool::Dimension && face(constrain)->isChecked(),
           "Constrain runs Dimension at once and is checked");

        QToolButton* line = row(create, QStringLiteral("Line"));
        ck(line != nullptr, "the Create list has a Line row");
        if (line) line->click();
        ck(gotTool == SketchTool::Line && face(create)->text() == QStringLiteral("Line")
               && face(create)->isChecked() && !face(constrain)->isChecked(),
           "picking Line captions and checks Create only");

        ck(pickVariant(create, CircleMenu, CreationMode::CircleTwoPoint,
                       QStringLiteral("2-Point (Diameter)")),
           "the Circle row offers its own variants");
        ck(gotTool == SketchTool::Circle && gotMode == CreationMode::CircleTwoPoint
               && face(create)->text() == QStringLiteral("2-Point (Diameter)"),
           "a variant selects its tool and mode and captions the group");

        face(create)->click();
        ck(gotTool == SketchTool::Select, "clicking the same tool again goes back to Select");

        int transform = -1;
        QObject::connect(&bar, &SketchToolbar::transformRequested,
                         [&](int t) { transform = t; });
        const int before = selections;
        if (QToolButton* rotate = row(modify, QStringLiteral("Rotate"))) rotate->click();
        ck(transform == 2 && selections == before
               && face(modify)->text() == QStringLiteral("Trim"),
           "Rotate asks for a transform and leaves Modify as it was");

        // Arc's tangent mode can be refused; the caption goes back.
        if (QToolButton* arc = row(create, QStringLiteral("Arc"))) arc->click();
        ck(pickVariant(create, ArcMenu, CreationMode::ArcTangent, QStringLiteral("Tangent")),
           "the Arc row offers Tangent");
        bar.revertCreationMode(SketchTool::Arc);
        ck(face(create)->text() == QStringLiteral("Arc")
               && bar.activeTool() == SketchTool::Arc,
           "a refused mode puts back the previous pick");

        bool latched = false;
        QObject::connect(&bar, &SketchToolbar::sketch3DModeToggled,
                         [&](bool on) { latched = on; });
        face(b[4])->click();
        ck(latched && face(b[4])->isChecked(), "the 3D latch reports its state");
        bar.set3DChecked(false);
        ck(!face(b[4])->isChecked(), "and can be cleared without reporting");

        bool finished = false;
        QObject::connect(&bar, &SketchToolbar::finishSketchRequested,
                         [&]() { finished = true; });
        face(b[6])->click();
        ck(finished, "Finish Sketch asks to finish");

        bar.setActiveTool(SketchTool::Trim);
        ck(face(modify)->isChecked() && !face(create)->isChecked(),
           "setting a tool checks the group that holds it");

        bar.resetCreateButton();
        ck(face(create)->text() == QStringLiteral("Create")
               && face(modify)->text() == QStringLiteral("Trim"),
           "a reset puts every caption back");
        const int beforeReset = selections;
        face(create)->click();
        ck(selections == beforeReset, "and Create opens its list first again");

        bindings::Table keys;
        keys.addCommand("sketch.line", {"L", "", ""});
        bar.setBindings(keys);
        const QToolButton* lineRow = row(create, QStringLiteral("Line"));
        ck(lineRow && lineRow->toolTip() == QStringLiteral("Draw line (L)"),
           "a tooltip names the bound key");
        const QToolButton* textRow = row(constrain, QStringLiteral("Text"));
        ck(textRow && textRow->toolTip() == QStringLiteral("Add text"),
           "and names none when none is bound");
    }

    // ---- model toolbar ------------------------------------------------------
    {
        ModelToolbar bar;
        const QList<ToolbarButton*> b = buttons(&bar);
        ck(b.size() == 8, "model toolbar: eight groups");
        if (b.size() != 8) return 1;
        ck(face(b[0])->text() == QStringLiteral("Sketch")
               && face(b[4])->text() == QStringLiteral("Simple\nHole")
               && face(b[7])->text() == QStringLiteral("Params"),
           "captions match the hand-written toolbar");
        ck(face(b[0])->isEnabled() && face(b[1])->isEnabled() && !face(b[2])->isEnabled()
               && !face(b[3])->isEnabled() && !face(b[4])->isEnabled()
               && !face(b[5])->isEnabled() && !face(b[6])->isEnabled()
               && face(b[7])->isEnabled(),
           "groups with no working tool are disabled");

        int sketches = 0;
        int params = 0;
        ModelTool got = ModelTool::None;
        QObject::connect(&bar, &ModelToolbar::createSketchClicked, [&]() { ++sketches; });
        QObject::connect(&bar, &ModelToolbar::parametersClicked, [&]() { ++params; });
        QObject::connect(&bar, &ModelToolbar::toolSelected, [&](ModelTool t) { got = t; });

        face(b[0])->click();
        ck(sketches == 1 && got == ModelTool::Sketch && face(b[0])->isChecked(),
           "Sketch runs at once");

        face(b[7])->click();
        ck(params == 1 && !face(b[7])->isChecked() && face(b[0])->isChecked(),
           "Params opens the dialog and never stays checked");

        if (QToolButton* onFace = row(b[0], QStringLiteral("Sketch on Face"))) onFace->click();
        ck(got == ModelTool::SketchOnFace
               && face(b[0])->text() == QStringLiteral("Sketch on\nFace"),
           "a picked tool captions the group with its toolbar text");

        bar.resetAllButtons();
        ck(face(b[0])->text() == QStringLiteral("Sketch") && !face(b[0])->isChecked()
               && bar.activeTool() == ModelTool::None,
           "a reset clears captions, checks and the active tool");
    }

    std::printf("\n%s (%d failure(s))\n", fails ? "FAILURES" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
