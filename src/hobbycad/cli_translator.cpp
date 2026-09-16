// =====================================================================
//  src/hobbycad/cli_translator.cpp — Qt translation for the CLI
// =====================================================================
//
//  The command layer lives in libhobbycad and is Qt-free, so it asks
//  hobbycad::translate() for its message text. This file is the Qt half
//  of that arrangement and is compiled only when the application is
//  built: it installs a translator that forwards to Qt, so a CLI run
//  inside the GUI, or a GUI build of the standalone CLI, still speaks
//  the user's language. A library-only build simply never installs one
//  and answers in English.
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include "cli_translator.h"

#include <hobbycad/translate.h>

#include <QCoreApplication>
#include <QString>

namespace hobbycad {

namespace {

std::string translateThroughQt(const char* context, const char* source)
{
    return QCoreApplication::translate(context, source).toStdString();
}

// ---------------------------------------------------------------------
//  Extraction table
// ---------------------------------------------------------------------
//
//  lupdate cannot see through hobbycad::translate(), so every string the
//  command layer passes to it is listed here as well. QT_TRANSLATE_NOOP
//  marks a string for extraction without translating it at that point,
//  which is exactly what is needed: the call site does the translating,
//  this table does the extracting.
//
//  The context stays "QObject" because that is where these messages
//  already sit in the fifteen catalogs, with translations attached. A
//  different context here would orphan every one of them.
//
//  A message added to the command layer must be added here too, or it
//  ships untranslated. The catalogs are the check: after adding one,
//  `cmake --build build --target hobbycad_lupdate` must report it.
//
[[maybe_unused]] const char* const kCliMessages[] = {
    QT_TRANSLATE_NOOP("QObject", "Usage: undo [count]"),
    QT_TRANSLATE_NOOP("QObject", "No document is open, so there is nothing to undo."),
    QT_TRANSLATE_NOOP("QObject", "Nothing to undo."),
    QT_TRANSLATE_NOOP("QObject", "Undone: %1"),
    QT_TRANSLATE_NOOP("QObject", "\n(only %1 of %2 steps were available)"),
    QT_TRANSLATE_NOOP("QObject", "\nNext undo: %1"),
    QT_TRANSLATE_NOOP("QObject", "Usage: redo [count]"),
    QT_TRANSLATE_NOOP("QObject", "No document is open, so there is nothing to redo."),
    QT_TRANSLATE_NOOP("QObject", "Nothing to redo."),
    QT_TRANSLATE_NOOP("QObject", "Redone: %1"),
    QT_TRANSLATE_NOOP("QObject", "\nNext redo: %1"),
};

}  // namespace

void installCliTranslator()
{
    setTranslator(&translateThroughQt);
}

}  // namespace hobbycad
