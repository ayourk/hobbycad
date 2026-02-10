// =====================================================================
//  src/hobbycad/gui/themevalidator.h — QSS theme validation
// =====================================================================
//
//  Validates Qt stylesheets before applying them.  Currently checks
//  that no selector has background-color equal to color (foreground),
//  which would render text invisible.
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_THEMEVALIDATOR_H
#define HOBBYCAD_THEMEVALIDATOR_H

#include <QString>
#include <QStringList>

namespace hobbycad {

struct ThemeValidationResult {
    bool     valid = true;
    QStringList warnings;  // human-readable warning messages
};

/// Validate a QSS stylesheet string.
/// Returns a result with valid=false and warnings if any rules
/// have background-color equal to color (foreground).
ThemeValidationResult validateTheme(const QString& qss);

}  // namespace hobbycad

#endif  // HOBBYCAD_THEMEVALIDATOR_H

