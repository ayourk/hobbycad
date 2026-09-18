// =====================================================================
//  src/libhobbycad/locale.cpp — which language to answer in
// =====================================================================
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include <hobbycad/locale.h>

#include <algorithm>
#include <cstdlib>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

namespace hobbycad {

namespace {

char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

char upper(char c)
{
    return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
}

/// True for the characters a locale name is made of, before normalizing.
bool nameChar(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
        || c == '_' || c == '-';
}

void addUnique(std::vector<std::string>& out, const std::string& value)
{
    if (value.empty()) return;
    if (std::find(out.begin(), out.end(), value) != out.end()) return;
    out.push_back(value);
}

/// An environment variable, or "" when unset or empty.
std::string fromEnvironment(const char* name)
{
    const char* value = std::getenv(name);
    return (value && *value) ? std::string(value) : std::string();
}

}  // namespace

std::string normalizeLocale(const std::string& name)
{
    // Everything from an encoding or modifier on is not part of the name:
    // "de_DE.UTF-8@euro" is the "de_DE" catalog.
    std::string head;
    for (char c : name) {
        if (c == '.' || c == '@') break;
        head.push_back(c);
    }
    // A language list ("de,fr") names the first one.
    const std::size_t comma = head.find(',');
    if (comma != std::string::npos) head.erase(comma);

    std::string language;
    std::string territory;
    bool inTerritory = false;
    for (char c : head) {
        if (c == '_' || c == '-') {
            if (inTerritory) return std::string();   // "de_DE_extra" is not a name
            inTerritory = true;
            continue;
        }
        if (!nameChar(c)) return std::string();
        (inTerritory ? territory : language).push_back(c);
    }
    if (language.empty()) return std::string();

    std::string out;
    for (char c : language) out.push_back(lower(c));
    // "C" and "POSIX" mean the source language, which needs no catalog.
    if (out == "c" || out == "posix") return std::string();
    if (!territory.empty()) {
        out.push_back('_');
        for (char c : territory) out.push_back(upper(c));
    }
    return out;
}

std::vector<std::string> localeCandidates(const std::string& name)
{
    std::vector<std::string> out;
    const std::string full = normalizeLocale(name);
    if (full.empty()) return out;
    addUnique(out, full);
    const std::size_t bar = full.find('_');
    if (bar != std::string::npos) addUnique(out, full.substr(0, bar));
    return out;
}

std::vector<std::string> preferredLocales()
{
    std::vector<std::string> out;

    // HOBBYCAD_LANG first: a person naming a language for one run, or a
    // script pinning one, outranks the session's own setting.
    const char* const variables[] = {"HOBBYCAD_LANG", "LC_ALL", "LC_MESSAGES", "LANG"};
    for (const char* variable : variables) {
        const std::string value = fromEnvironment(variable);
        if (value.empty()) continue;
        // The first one that is set is the answer, whatever it says: "C"
        // means English deliberately, and must not fall through to the
        // platform's own idea.
        for (const std::string& candidate : localeCandidates(value)) {
            addUnique(out, candidate);
        }
        return out;
    }

#if defined(_WIN32)
    // Windows sets none of those, and its own answer is a name like
    // "de-DE" rather than a number.
    if (out.empty()) {
        wchar_t wide[LOCALE_NAME_MAX_LENGTH] = {};
        if (GetUserDefaultLocaleName(wide, LOCALE_NAME_MAX_LENGTH) > 0) {
            std::string narrow;
            for (const wchar_t* p = wide; *p; ++p) {
                // A locale name is ASCII; anything else is not one.
                if (*p < 0x20 || *p > 0x7E) {
                    narrow.clear();
                    break;
                }
                narrow.push_back(static_cast<char>(*p));
            }
            for (const std::string& candidate : localeCandidates(narrow)) {
                addUnique(out, candidate);
            }
        }
    }
#endif

    return out;
}

}  // namespace hobbycad
