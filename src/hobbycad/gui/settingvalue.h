// SPDX-License-Identifier: GPL-3.0-only
// HobbyCAD - settingvalue.h
// Reading a preference with the default and range the library gives it
// (hobbycad/settings_schema.h), so no reader carries a default of its own.

#pragma once

#include <hobbycad/settings_schema.h>

#include <QSettings>
#include <QString>

namespace hobbycad {

/// A Bool setting, or its default when it was never stored.
inline bool settingBool(const char* key)
{
    return QSettings().value(QLatin1String(key), settings::defaultBool(key)).toBool();
}

/// An Int setting within its range, or its default when it was never stored
/// or is not a number.
inline int settingInt(const char* key)
{
    bool ok = false;
    const int value = QSettings().value(QLatin1String(key)).toInt(&ok);
    return ok ? settings::clampInt(key, value) : settings::defaultInt(key);
}

/// A Choice setting, or its default when the stored word is not a choice.
inline QString settingChoice(const char* key)
{
    const QString stored = QSettings().value(QLatin1String(key)).toString();
    return QString::fromStdString(settings::validChoice(key, stored.toStdString()));
}

}  // namespace hobbycad
