// =====================================================================
//  src/libhobbycad/settings_schema.cpp — preference keys and defaults
// =====================================================================
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include <hobbycad/settings_schema.h>

#include <algorithm>

namespace hobbycad {
namespace settings {

namespace {

Setting boolean(const char* key, bool value)
{
    Setting s;
    s.key = key;
    s.kind = Kind::Bool;
    s.defaultValue = value ? 1 : 0;
    return s;
}

Setting integer(const char* key, int value, int minimum, int maximum)
{
    Setting s;
    s.key = key;
    s.kind = Kind::Int;
    s.defaultValue = value;
    s.minimum = minimum;
    s.maximum = maximum;
    return s;
}

Setting choice(const char* key, std::vector<const char*> words, int value)
{
    Setting s;
    s.key = key;
    s.kind = Kind::Choice;
    s.choices = std::move(words);
    s.defaultValue = value;
    return s;
}

}  // namespace

const std::vector<Setting>& allSettings()
{
    static const std::vector<Setting> all = {
        // Navigation
        choice(keys::MousePreset, {"hobbycad", "fusion360", "freecad", "blender"}, 0),
        integer(keys::DefaultAxis, 0, 0, 2),   // X, Y, Z
        integer(keys::PageStepDeg, 10, 1, 45),
        integer(keys::SpinInterval, 10, 1, 1000),
        integer(keys::SnapStepDeg, 10, 1, 15),
        integer(keys::SnapInterval, 10, 1, 100),
        // General
        boolean(keys::ShowGrid, true),
        boolean(keys::RestoreSession, true),
        // 0 is unlimited: a deliberate choice, not the default.
        integer(keys::CliScrollback, 10000, 0, 1000000),
        boolean(keys::ZUpOrientation, true),
        boolean(keys::OrbitSelected, false),
        boolean(keys::ShowCursorHints, true),
        // What the sketch canvas shows, remembered between sketches
        boolean(keys::SketchShowPoints, true),
        boolean(keys::SketchShowConstraints, true),
        boolean(keys::SketchShowDimensions, true),
        boolean(keys::SketchShowProfiles, false),
        boolean(keys::SketchShowGrid, true),
        boolean(keys::SketchSnapToGrid, false),
    };
    return all;
}

const Setting* findSetting(const std::string& key)
{
    for (const Setting& s : allSettings()) {
        if (key == s.key) return &s;
    }
    return nullptr;
}

bool defaultBool(const std::string& key)
{
    const Setting* s = findSetting(key);
    return s && s->kind == Kind::Bool && s->defaultValue != 0;
}

int defaultInt(const std::string& key)
{
    const Setting* s = findSetting(key);
    return (s && s->kind == Kind::Int) ? s->defaultValue : 0;
}

std::string defaultChoice(const std::string& key)
{
    const Setting* s = findSetting(key);
    if (!s || s->kind != Kind::Choice || s->choices.empty()) return std::string();
    const auto i = static_cast<std::size_t>(s->defaultValue);
    return i < s->choices.size() ? s->choices[i] : s->choices.front();
}

int clampInt(const std::string& key, int value)
{
    const Setting* s = findSetting(key);
    if (!s || s->kind != Kind::Int) return value;
    return std::clamp(value, s->minimum, s->maximum);
}

std::string validChoice(const std::string& key, const std::string& value)
{
    const Setting* s = findSetting(key);
    if (!s || s->kind != Kind::Choice) return value;
    for (const char* word : s->choices) {
        if (value == word) return value;
    }
    return defaultChoice(key);
}

}  // namespace settings
}  // namespace hobbycad
