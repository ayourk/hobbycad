// =====================================================================
//  src/libhobbycad/hobbycad/settings_schema.h — preference keys and defaults
// =====================================================================
//
//  Capability tier of the front-end support layer. Every preference a
//  front end stores: its key, its type, its default and its range. The
//  defaults used to be written wherever a value was read, and a reader
//  that disagreed with the preferences page showed one thing and did
//  another. Storage stays with the front end (QSettings in the Qt one).
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_SETTINGS_SCHEMA_H
#define HOBBYCAD_SETTINGS_SCHEMA_H

#include "core.h"

#include <string>
#include <vector>

namespace hobbycad {
namespace settings {

/// Stored keys.
namespace keys {
constexpr const char* MousePreset = "preferences/mousePreset";
constexpr const char* DefaultAxis = "preferences/defaultAxis";
constexpr const char* PageStepDeg = "preferences/pgUpStepDeg";
constexpr const char* SpinInterval = "preferences/spinInterval";
constexpr const char* SnapStepDeg = "preferences/snapStepDeg";
constexpr const char* SnapInterval = "preferences/snapInterval";
constexpr const char* ShowGrid = "preferences/showGrid";
constexpr const char* RestoreSession = "preferences/restoreSession";
constexpr const char* CliScrollback = "preferences/cliScrollback";
constexpr const char* ZUpOrientation = "preferences/zUpOrientation";
constexpr const char* OrbitSelected = "preferences/orbitSelected";
constexpr const char* ShowCursorHints = "preferences/showCursorHints";
constexpr const char* SketchShowPoints = "sketch/view/points";
constexpr const char* SketchShowConstraints = "sketch/view/constraints";
constexpr const char* SketchShowDimensions = "sketch/view/dimensions";
constexpr const char* SketchShowProfiles = "sketch/view/profiles";
constexpr const char* SketchShowGrid = "sketch/view/grid";
constexpr const char* SketchSnapToGrid = "sketch/view/snapGrid";
}  // namespace keys

/// What a setting holds.
enum class Kind {
    Bool,
    Int,
    Choice,   ///< one of a fixed set of stored words
};

/// One setting.
struct Setting {
    const char* key = "";
    Kind kind = Kind::Bool;
    /// Bool: 0 or 1. Int: the value. Choice: the index into `choices`.
    int defaultValue = 0;
    int minimum = 0;   ///< Int only
    int maximum = 0;   ///< Int only
    std::vector<const char*> choices;
};

/// Every setting, in a stable order.
HOBBYCAD_EXPORT const std::vector<Setting>& allSettings();

/// The setting with this key, or nullptr.
HOBBYCAD_EXPORT const Setting* findSetting(const std::string& key);

/// A Bool setting's default (false for an unknown key).
HOBBYCAD_EXPORT bool defaultBool(const std::string& key);

/// An Int setting's default (0 for an unknown key).
HOBBYCAD_EXPORT int defaultInt(const std::string& key);

/// A Choice setting's default word ("" for an unknown key).
HOBBYCAD_EXPORT std::string defaultChoice(const std::string& key);

/// A stored Int brought into its setting's range.
HOBBYCAD_EXPORT int clampInt(const std::string& key, int value);

/// A stored Choice, or the default when it is not one of the choices.
HOBBYCAD_EXPORT std::string validChoice(const std::string& key, const std::string& value);

}  // namespace settings
}  // namespace hobbycad

#endif  // HOBBYCAD_SETTINGS_SCHEMA_H
