// =====================================================================
//  tests/project/locale.cpp — which language a Qt-free front end answers in
// =====================================================================
//  SPDX-License-Identifier: GPL-3.0-only
//  A front end without Qt has no QLocale to ask, and every platform
//  answers differently: POSIX sets LC_ALL, LC_MESSAGES or LANG, Windows
//  sets none of them. preferredLocales() hands back the names to try,
//  most specific first, so a caller walks them against whatever catalogs
//  it has. Nothing here may call setlocale(): the numeric locale decides
//  how numbers print, and a command line's output is a script's input.
// =====================================================================
#include <hobbycad/locale.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace hobbycad;

static int failures = 0;
static void check(bool ok, const char* what)
{
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

/// Set or clear one variable for the checks below. MSVC has no setenv;
/// _putenv_s with an empty value is how it removes one.
static void put(const char* name, const char* value)
{
#if defined(_WIN32)
    _putenv_s(name, value ? value : "");
#else
    if (value) {
        setenv(name, value, 1);
    } else {
        unsetenv(name);
    }
#endif
}

static std::string joined(const std::vector<std::string>& names)
{
    std::string out;
    for (const std::string& name : names) {
        if (!out.empty()) out += ",";
        out += name;
    }
    return out;
}

int main()
{
    std::printf("locale\n");

    // ---- spelling a name the way the catalogs do ----------------------------------
    check(normalizeLocale("de") == "de" && normalizeLocale("de_DE") == "de_DE",
          "a plain name is already how a catalog spells it");
    check(normalizeLocale("de-DE") == "de_DE" && normalizeLocale("de_DE.UTF-8") == "de_DE"
              && normalizeLocale("de_DE.UTF-8@euro") == "de_DE",
          "a Windows dash, an encoding and a modifier all give de_DE");
    check(normalizeLocale("DE_de") == "de_DE" && normalizeLocale("PT_br") == "pt_BR",
          "the language lowercases and the territory upper-cases");
    check(normalizeLocale("C").empty() && normalizeLocale("POSIX").empty(),
          "C and POSIX name no catalog: they mean the source language");
    check(normalizeLocale("").empty() && normalizeLocale("../etc/passwd").empty()
              && normalizeLocale("de_DE_extra").empty(),
          "text that is not a locale name gives nothing");
    check(normalizeLocale("de;rm -rf").empty() && normalizeLocale("de/../fr").empty()
              && normalizeLocale("de\tDE").empty(),
          "and nor does one carrying characters a locale name cannot hold");
    check(normalizeLocale("de,fr") == "de", "a language list names the first one");

    // ---- what one name stands for --------------------------------------------------
    check(joined(localeCandidates("pt_BR")) == "pt_BR,pt",
          "a territory name falls back to its language");
    check(joined(localeCandidates("de")) == "de", "a language name stands alone");
    check(joined(localeCandidates("zh_CN")) == "zh_CN,zh"
              && joined(localeCandidates("zh_TW")) == "zh_TW,zh",
          "the two Chinese catalogs stay apart");
    check(localeCandidates("C").empty(), "and C stands for nothing");

    // ---- reading the environment ---------------------------------------------------
    const char* const all[] = {"HOBBYCAD_LANG", "LC_ALL", "LC_MESSAGES", "LANG"};
    for (const char* name : all) put(name, nullptr);

    put("LANG", "fr_FR.UTF-8");
    check(joined(preferredLocales()) == "fr_FR,fr", "LANG is read, encoding and all");

    put("LC_MESSAGES", "it_IT");
    check(joined(preferredLocales()) == "it_IT,it", "LC_MESSAGES outranks LANG");

    put("LC_ALL", "es_ES");
    check(joined(preferredLocales()) == "es_ES,es", "LC_ALL outranks both");

    put("HOBBYCAD_LANG", "ja");
    check(joined(preferredLocales()) == "ja",
          "and HOBBYCAD_LANG outranks the session, for one run or a script");

    put("HOBBYCAD_LANG", "C");
    check(preferredLocales().empty(),
          "a variable set to C means English deliberately, not fall through");

    put("HOBBYCAD_LANG", "");
    check(joined(preferredLocales()) == "es_ES,es",
          "an empty variable is not an answer; the next one is read");

    for (const char* name : all) put(name, nullptr);
    const std::vector<std::string> bare = preferredLocales();
    // On POSIX with nothing set there is nothing to report; on Windows the
    // platform still answers, and either way every name must be usable.
    bool wellFormed = true;
    for (const std::string& name : bare) {
        if (normalizeLocale(name) != name) wellFormed = false;
    }
    check(wellFormed, "with nothing set, whatever the platform says is already normalized");

    std::printf("\n%s (%d failure(s))\n", failures ? "FAILURES" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
