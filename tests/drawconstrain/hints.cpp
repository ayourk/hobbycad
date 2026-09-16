// =====================================================================
//  tests/drawconstrain/hints.cpp — draw-then-constrain hint derivation
//  Repository: ayourk/hobbycad
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//
//  Draw-then-constrain has no typed dimension fields, so its hints must
//  not offer one. They are DERIVED from the placement-first hints rather
//  than copied, and this is the derivation.
//
//  The strings below are real hints taken from the tool handlers. If one
//  of those is reworded so the clause no longer matches, the mode starts
//  telling people to type a length it will not accept, which is what
//  this notices.
//
// =====================================================================

#include "tools/drawconstrainhandlers.h"

#include <QString>
#include <cstdio>

static int failures = 0;

static void check(const QString& in, const QString& want)
{
    const QString got = hobbycad::hintWithoutTypedDimension(in);
    if (got != want) {
        std::printf("  FAIL  %s\n        got  %s\n        want %s\n",
                    in.toUtf8().constData(),
                    got.toUtf8().constData(),
                    want.toUtf8().constData());
        ++failures;
    }
}

int main()
{
    // The clause goes.
    check(QStringLiteral("Circle: click to set the radius, or type one"),
          QStringLiteral("Circle: click to set the radius"));
    check(QStringLiteral("Line: click the end point, or type a length"),
          QStringLiteral("Line: click the end point"));
    check(QStringLiteral("Slot: click the other end, or type an overall length"),
          QStringLiteral("Slot: click the other end"));
    check(QStringLiteral("Rectangle: click the opposite corner, or type a width"),
          QStringLiteral("Rectangle: click the opposite corner"));

    // A trailing parenthetical describes something this mode still does,
    // so it survives even though the typed dimension between them goes.
    check(QStringLiteral("Arc: click the end point, or type a sweep angle"
                         "  (Shift = long way round)"),
          QStringLiteral("Arc: click the end point  (Shift = long way round)"));
    check(QStringLiteral("Polygon: click to set the radius, or type one"
                         "  (scroll = side count)"),
          QStringLiteral("Polygon: click to set the radius  (scroll = side count)"));

    // A hint with no clause is left exactly as it is.
    check(QStringLiteral("Line: click the start point"),
          QStringLiteral("Line: click the start point"));
    check(QStringLiteral("Slot: click one end  (scroll = width)"),
          QStringLiteral("Slot: click one end  (scroll = width)"));

    if (failures == 0) {
        std::printf("  hint derivation: ok\n");
        return 0;
    }
    std::printf("  hint derivation: %d failure(s)\n", failures);
    return 1;
}
