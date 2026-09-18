// =====================================================================
//  tests/project/settings_schema.cpp — preference keys and defaults
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  Each preference's default was written at every place it was read (the
//  preferences page, the viewport, the CLI panel, the canvas), so a reader
//  that disagreed showed one thing and did another. The defaults now live
//  in hobbycad/settings_schema.h; these checks keep that table sound.
// =====================================================================
#include <hobbycad/settings_schema.h>

#include <cstdio>
#include <set>
#include <string>

using namespace hobbycad::settings;

static int failures = 0;
static void check(bool ok, const char* what)
{
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

int main()
{
    std::printf("settings schema\n");

    std::set<std::string> seen;
    bool unique = true, sane = true;
    for (const Setting& s : allSettings()) {
        unique = seen.insert(s.key).second && unique;
        switch (s.kind) {
        case Kind::Bool:
            sane = sane && (s.defaultValue == 0 || s.defaultValue == 1);
            break;
        case Kind::Int:
            sane = sane && s.minimum <= s.defaultValue && s.defaultValue <= s.maximum;
            break;
        case Kind::Choice:
            sane = sane && s.defaultValue >= 0
                   && s.defaultValue < static_cast<int>(s.choices.size());
            break;
        }
    }
    check(unique, "every key is listed once");
    check(sane, "every default is valid for its setting");

    bool listed = true;
    for (const char* key : {keys::MousePreset, keys::DefaultAxis, keys::PageStepDeg,
                            keys::SpinInterval, keys::SnapStepDeg, keys::SnapInterval,
                            keys::ShowGrid, keys::RestoreSession, keys::CliScrollback,
                            keys::ZUpOrientation, keys::OrbitSelected, keys::ShowCursorHints,
                            keys::SketchShowPoints, keys::SketchShowConstraints,
                            keys::SketchShowDimensions, keys::SketchShowProfiles,
                            keys::SketchShowGrid, keys::SketchSnapToGrid}) {
        listed = listed && findSetting(key) != nullptr;
    }
    check(listed, "every named key has an entry");

    check(defaultBool(keys::ShowCursorHints) && !defaultBool(keys::OrbitSelected)
              && defaultInt(keys::CliScrollback) == 10000
              && defaultChoice(keys::MousePreset) == "hobbycad",
          "the defaults are the ones the preferences page always used");
    check(clampInt(keys::DefaultAxis, 7) == 2 && clampInt(keys::PageStepDeg, 0) == 1
              && clampInt(keys::CliScrollback, 0) == 0,
          "a stored number is brought into range; 0 scrollback stays unlimited");
    check(validChoice(keys::MousePreset, "blender") == "blender"
              && validChoice(keys::MousePreset, "maya") == "hobbycad",
          "an unknown preset falls back to the default");
    check(!findSetting("preferences/nothing") && !defaultBool("preferences/nothing")
              && clampInt("preferences/nothing", 5) == 5,
          "an unknown key has no setting and changes nothing");

    std::printf("\n%s (%d failure(s))\n", failures ? "FAILURES" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
