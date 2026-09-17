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
    // ellipse (sketch)
    QT_TRANSLATE_NOOP("QObject",
        "Usage: ellipse [at] <x>,<y> major <a> minor <b>\n"
        "                [rotation <deg>] [angle <start> to <end>]\n"
        "                [construction]\n"
        "\n"
        "Examples:\n"
        "  ellipse at 0,0 major 40 minor 20\n"
        "  ellipse 10,10 major (width/2) minor 15\n"
        "  ellipse at 0,0 major 40 minor 20 rotation 30\n"
        "  ellipse at 0,0 major 40 minor 20 angle 0 to 90"),
    QT_TRANSLATE_NOOP("QObject",
                      "Invalid rotation. Must be a number, parameter, or (expression)."),
    QT_TRANSLATE_NOOP("QObject",
                      "Invalid start angle. Must be a number, parameter, or (expression)."),
    QT_TRANSLATE_NOOP("QObject", "Expected 'to' between the start and end angles"),
    QT_TRANSLATE_NOOP("QObject",
                      "Invalid end angle. Must be a number, parameter, or (expression)."),
    QT_TRANSLATE_NOOP("QObject", "An elliptical arc needs a non-zero sweep."),
    // conic (sketch)
    QT_TRANSLATE_NOOP("QObject",
        "Usage: conic [from] <x>,<y> to <x>,<y> apex <x>,<y> rho <r>\n"
        "             [construction]\n"
        "\n"
        "A conic arc by rho, as in Fusion, Onshape and SolidWorks: the two\n"
        "points are its ends, the apex is where the end tangents meet, and rho\n"
        "says where the curve's shoulder sits between the chord's midpoint (0)\n"
        "and the apex (1). Below 0.5 it is an elliptical arc, at 0.5 a parabola,\n"
        "above 0.5 a hyperbola. It is stored as one rational Bezier segment; rho\n"
        "stays a property of the curve (see `points <id>`, `conic <id> rho`).\n"
        "\n"
        "Examples:\n"
        "  conic 0,0 to 40,0 apex 20,30 rho 0.5          (a parabola)\n"
        "  conic 0,0 to 40,40 apex 40,0 rho 0.41421      (a quarter circle)"),
    QT_TRANSLATE_NOOP("QObject", "Usage: conic <id> rho <r>"),
    QT_TRANSLATE_NOOP("QObject", "Entity %1 is not a conic arc."),
    QT_TRANSLATE_NOOP("QObject", "rho must be a number between 0 and 1, exclusive ('%1')."),
    QT_TRANSLATE_NOOP("QObject", "Entity %1 has no recoverable apex; it is not a conic arc."),
    QT_TRANSLATE_NOOP("QObject", "Updated conic %1: rho %2 (%3)."),
    QT_TRANSLATE_NOOP("QObject", "Invalid start coordinates. Use format: x,y"),
    QT_TRANSLATE_NOOP("QObject", "Expected 'to' between the start and end points"),
    QT_TRANSLATE_NOOP("QObject", "Invalid end coordinates. Use format: x,y"),
    QT_TRANSLATE_NOOP("QObject", "Expected 'apex' keyword"),
    QT_TRANSLATE_NOOP("QObject", "Invalid apex coordinates. Use format: x,y"),
    QT_TRANSLATE_NOOP("QObject", "Expected 'rho' keyword"),
    QT_TRANSLATE_NOOP("QObject", "Invalid rho. Must be a number, parameter, or (expression)."),
    QT_TRANSLATE_NOOP("QObject", "rho must be between 0 and 1, exclusive."),
    QT_TRANSLATE_NOOP("QObject", "The two ends must be apart and the apex off their line."),
    QT_TRANSLATE_NOOP("QObject",
        "Created %6conic arc (%5) from (%1, %2) to (%3, %4), rho %7 [id %8]"),
    QT_TRANSLATE_NOOP("QObject", "entity %1 (conic arc, %2, rho %3):"),
    QT_TRANSLATE_NOOP("QObject", "  start (%1, %2)"),
    QT_TRANSLATE_NOOP("QObject", "  end   (%1, %2)"),
    QT_TRANSLATE_NOOP("QObject",
        "  apex  (%1, %2)   (where the end tangents meet; not a stored point)"),
    QT_TRANSLATE_NOOP("QObject",
        "Change rho: conic %1 rho <r>. Editing a handle makes it a plain bezier."),
    QT_TRANSLATE_NOOP("QObject", "elliptical"),
    QT_TRANSLATE_NOOP("QObject", "parabolic"),
    QT_TRANSLATE_NOOP("QObject", "hyperbolic"),
};

}  // namespace

void installCliTranslator()
{
    setTranslator(&translateThroughQt);
}

}  // namespace hobbycad
