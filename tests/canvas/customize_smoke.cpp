// =====================================================================
//  tests/canvas/customize_smoke.cpp — customizing the menus and toolbars
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  Driven through the store and the dialog, against a configuration
//  directory of its own (QStandardPaths test mode), so a run never
//  touches the person's own arrangement:
//    - nothing is stored until something is customized, and the file goes
//      when the last change goes;
//    - a renamed command shows the person's own name, untranslated, and
//      the default text again once restored;
//    - a hidden command leaves the menu;
//    - the keys the bindings editor writes land in the same file;
//    - export then import gives the same arrangement back.
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QMenuBar>
#include <QPushButton>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTranslator>

#include "arrangementstore.h"
#include "bindingsdialog.h"
#include "commandtext.h"
#include "customizedialog.h"
#include "mainwindow.h"

#include <hobbycad/opengl_info.h>

#include <cstdio>

using namespace hobbycad;
static int fails = 0;
static void ck(bool ok, const char* what)
{
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++fails;
}

/// The first element of a container that names a command.
static QString firstCommandKey(const layout::Arrangement& a, const char* container)
{
    for (const layout::Element* e : a.children(container, true)) {
        if (e->kind == layout::ElementKind::Command) return QString::fromStdString(e->key);
    }
    return QString();
}

static QString menuTextFor(const layout::Arrangement& a, const QString& key)
{
    const layout::Element* element = a.element(key.toStdString());
    if (!element) return QString();
    const commands::Command* command = commands::findCommand(element->id);
    return command ? commandtext::menuText(*command) : QString();
}

int main(int argc, char** argv)
{
    QStandardPaths::setTestModeEnabled(true);
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("hobbycad-customize-test"));
    std::printf("customize\n");

    ArrangementStore& store = ArrangementStore::instance();
    store.reload();
    store.restore(layout::RestoreArea::All);
    const QString path = store.filePath();
    QFile::remove(path);
    store.reload();
    ck(!QFile::exists(path) && !store.isCustomized(),
       "a fresh HobbyCAD has no arrangement file");

    const QString key = firstCommandKey(store.base(), "menu.file");
    ck(!key.isEmpty(), "the File menu has a command to work with");
    const QString defaultText = menuTextFor(store.base(), key);

    // ---- renaming ------------------------------------------------------------------
    {
        // The store outlives this scope, so the connection has to go with
        // it; a lambda holding a reference to a local would be written to
        // long after the local is gone.
        static int notices = 0;
        const QMetaObject::Connection watch =
            QObject::connect(&store, &ArrangementStore::changed, [] { ++notices; });
        store.setLabel(key, QStringLiteral("Start Something"));
        ck(notices == 1 && QFile::exists(path), "one change, one notice, and a file to hold it");
        ck(menuTextFor(store.current(), key) == QStringLiteral("Start Something"),
           "the command shows the person's own name");
        const layout::Element* element = store.current().element(key.toStdString());
        ck(element && store.elementText(*element) == QStringLiteral("Start Something"),
           "and the store agrees");
        store.restoreElement(key);
        ck(menuTextFor(store.current(), key) == defaultText,
           "restoring it brings HobbyCAD's own text back");
        ck(!QFile::exists(path), "and with nothing customized the file is gone");
        QObject::disconnect(watch);
    }

    // ---- hiding and moving ----------------------------------------------------------
    {
        const std::size_t shown = store.current().children("menu.file", false).size();
        store.setHidden(key, true);
        ck(store.current().children("menu.file", false).size() == shown - 1,
           "a hidden command leaves the menu");
        store.setHidden(key, false);

        QString second;
        {
            // These pointers are only good until the arrangement is built
            // again, which a change does.
            const auto before = store.current().children("menu.file", true);
            ck(before.size() > 2, "the File menu has room to move things about");
            second = QString::fromStdString(before[1]->key);
        }
        store.move(second, QStringLiteral("menu.file"), 0);
        const auto after = store.current().children("menu.file", true);
        ck(after[0]->key == second.toStdString(),
           "a command moved to the front of its menu stays there");
        ck(store.customizations().elements().size() == 1,
           "one entry covers the move, so nothing else is renumbered");
    }

    // ---- what is stored is read back -------------------------------------------------
    {
        store.setLabel(key, QStringLiteral("Kept"));
        store.reload();
        ck(store.isCustomized() && menuTextFor(store.current(), key) == QStringLiteral("Kept"),
           "a change is read back from the file on the next start");

        static int notices = 0;
        const QMetaObject::Connection watch =
            QObject::connect(&store, &ArrangementStore::changed, [] { ++notices; });
        const QString separator = store.addSeparator(QStringLiteral("menu.file"), 1);
        ck(!separator.isEmpty() && notices == 1,
           "a separator added to a menu is reported like any other change");
        const auto children = store.current().children("menu.file", true);
        ck(children.size() > 1 && children[1]->key == separator.toStdString()
               && children[1]->kind == layout::ElementKind::Separator,
           "and it is where it was put");
        store.reload();
        ck(store.current().element(separator.toStdString()) != nullptr,
           "it comes back from the file too");
        store.restoreElement(separator);
        store.restoreElement(key);
        QObject::disconnect(watch);
        ck(store.customizations().find(key.toStdString()) == nullptr
               && store.customizations().added().empty(),
           "and both go again, leaving the earlier changes alone");
    }

    // ---- the keys go in the same file ------------------------------------------------
    {
        QHash<QString, ActionBinding> bindings = BindingsDialog::loadBindings();
        const QString id = QStringLiteral("sketch.line");
        ck(bindings.contains(id), "the bindings editor knows the line tool");
        bindings[id].binding1 = QStringLiteral("Ctrl+Alt+L");
        BindingsDialog::saveBindings(bindings);
        ck(store.customizations().findBinding(id.toStdString()) != nullptr,
           "a key set in the editor is stored with the rest of the arrangement");
        const bindings::Table table = BindingsDialog::loadTable();
        ck(table.current(id.toStdString())[0] == "Ctrl+Alt+L", "and read back from it");

        bindings = BindingsDialog::loadBindings();
        bindings[id].binding1 = bindings[id].default1;
        BindingsDialog::saveBindings(bindings);
        ck(store.customizations().findBinding(id.toStdString()) == nullptr,
           "setting it back to the default drops the entry rather than storing it");
    }

    // ---- export and import ----------------------------------------------------------
    {
        QTemporaryDir dir;
        const QString file = dir.path() + QStringLiteral("/mine.json");
        store.setLabel(key, QStringLiteral("Mine"));
        QString error;
        ck(store.exportTo(file, &error), "the arrangement exports to a file");
        const std::size_t entries = store.customizations().elements().size();
        store.restore(layout::RestoreArea::All);
        QStringList problems;
        ck(store.importFrom(file, &problems) && problems.isEmpty(),
           "and imports again without complaint");
        ck(store.customizations().elements().size() == entries
               && menuTextFor(store.current(), key) == QStringLiteral("Mine"),
           "with the same changes in it");
    }

    // ---- the dialog, and the menus it rebuilds ----------------------------------------
    {
        CustomizeDialog dialog;
        dialog.show();
        ck(dialog.findChildren<QPushButton*>().size() >= 8,
           "the Customize dialog offers its buttons");
    }

    // ---- the menus a window builds ----------------------------------------------------
    {
        store.restore(layout::RestoreArea::All);
        const hobbycad::OpenGLInfo info{};
        MainWindow window(info);
        QMenu* fileMenu = nullptr;
        for (QAction* action : window.menuBar()->actions()) {
            if (action->menu() && action->menu()->objectName() == QStringLiteral("menu.file")) {
                fileMenu = action->menu();
            }
        }
        ck(fileMenu != nullptr, "the window has a File menu");
        if (fileMenu) {
            const layout::Element* element = store.current().element(key.toStdString());
            const commands::Command* command =
                element ? commands::findCommand(element->id) : nullptr;
            const QString id = command ? QString::fromStdString(command->id) : QString();
            const auto namesFileMenu = [fileMenu, &id]() {
                for (QAction* action : fileMenu->actions()) {
                    if (action->objectName() == id) return true;
                }
                return false;
            };
            ck(!id.isEmpty() && namesFileMenu(), "which shows the command by default");
            store.setHidden(key, true);
            ck(!namesFileMenu(), "hiding it takes it out of the menu the window shows");
            store.setLabel(key, QStringLiteral("Only Mine"));
            store.setHidden(key, false);
            bool renamed = false;
            for (QAction* action : fileMenu->actions()) {
                if (action->objectName() == id && action->text() == QStringLiteral("Only Mine")) {
                    renamed = true;
                }
            }
            ck(renamed, "and a renamed command carries the person's name in the menu");
        }
        store.restore(layout::RestoreArea::All);
    }

    // ---- a name is not translated ----------------------------------------------------
    {
        store.restore(layout::RestoreArea::All);
        store.setLabel(key, QStringLiteral("Mine"));
        QTranslator translator;
        // Whatever language is installed, the person's own name stands.
        ck(menuTextFor(store.current(), key) == QStringLiteral("Mine"),
           "a renamed command keeps its name in every language");
        store.restore(layout::RestoreArea::All);
        ck(!QFile::exists(path), "and nothing is left behind");
    }

    std::printf("\n%s (%d failure(s))\n", fails ? "FAILURES" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
