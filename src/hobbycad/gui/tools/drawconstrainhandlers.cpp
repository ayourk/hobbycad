// =====================================================================
//  src/hobbycad/gui/tools/drawconstrainhandlers.cpp — draw-then-constrain
//  Repository: ayourk/hobbycad
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================

#include "drawconstrainhandlers.h"

namespace hobbycad {

QString hintWithoutTypedDimension(QString hint)
{
    // "Circle: click to set the radius, or type one"
    //   -> "Circle: click to set the radius"
    // "Arc: click the end point, or type a sweep angle  (Shift = ...)"
    //   -> "Arc: click the end point  (Shift = ...)"
    const int clause = hint.indexOf(QStringLiteral(", or type"));
    if (clause < 0) return hint;

    // A trailing parenthetical describes something this mode still does
    // (Shift for the long way round, scroll for the side count), so it is
    // kept even though the typed dimension between them goes.
    const int tail = hint.indexOf(QStringLiteral("  ("), clause);
    QString out = hint.left(clause);
    if (tail >= 0) out += hint.mid(tail);
    return out;
}

}  // namespace hobbycad
