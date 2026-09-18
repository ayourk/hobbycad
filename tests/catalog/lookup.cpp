// =====================================================================
//  tests/catalog/lookup.cpp — the compiled-in translation table
// =====================================================================
//  SPDX-License-Identifier: GPL-3.0-only
//  The Qt-free command line carries its catalogs as generated C++
//  (scripts/ts2cpp.py) behind the library's translator seam. This links
//  a table generated from fixture catalogs and checks what the seam
//  answers: the right string, English where the catalog is silent, and
//  the two "Copy" commands kept apart by their disambiguation, which is
//  the whole reason the key has three parts.
// =====================================================================
#include <hobbycad/translate.h>

#include <cstdio>
#include <string>

// Defined by the generated unit compiled alongside this file.
namespace hobbycad {
namespace cli_catalogs {
bool install(const char* locale);
const char* language(std::size_t index);
}  // namespace cli_catalogs
}  // namespace hobbycad

static int failures = 0;
static void check(bool ok, const char* what)
{
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

/// What the seam answers, which is what every caller in the library and
/// the command layer sees.
static std::string say(const char* source, const char* disambiguation = nullptr)
{
    return hobbycad::translate("QObject", source, disambiguation);
}

int main()
{
    std::printf("catalog lookup\n");

    // ---- English is language 0 and needs no table ----------------------------------
    check(hobbycad::cli_catalogs::language(0) != nullptr
              && std::string(hobbycad::cli_catalogs::language(0)) == "en",
          "language 0 is English");
    check(hobbycad::cli_catalogs::install("en") && say("Design rejected") == "Design rejected",
          "installing English answers with the source string");
    check(hobbycad::cli_catalogs::language(99) == nullptr, "and there is no language 99");

    check(!hobbycad::cli_catalogs::install("kl"), "a language the table lacks is refused");
    check(!hobbycad::cli_catalogs::install(""), "so is an empty name");
    check(say("Design rejected") == "Design rejected",
          "and a refused install leaves the language alone");

    // ---- a real language -----------------------------------------------------------
    check(hobbycad::cli_catalogs::install("de"), "German installs");
    check(say("Design rejected") == "Design abgelehnt", "and a message comes back translated");

    // The two commands share one English word and one context, and are
    // told apart only by the command id in the disambiguation.
    check(say("Copy", "edit.copy") == "Kopieren (Bearbeiten)", "Copy the edit command");
    check(say("Copy", "sketch.transform.copy") == "Kopieren (Skizze)",
          "Copy the sketch transform, a different string entirely");
    check(say("Copy", "edit.copy") != say("Copy", "sketch.transform.copy"),
          "so the disambiguation is what keeps them apart");
    check(say("Copy") == "Copy",
          "and asking without one matches neither: English, not a wrong guess");

    // ---- where the catalog is silent -----------------------------------------------
    check(say("Not in any catalog") == "Not in any catalog",
          "a message no catalog carries answers in English");
    check(say("Design rejected", "no.such.id") == "Design rejected",
          "so does a known message under an unknown disambiguation");

    // ---- both ends of the table, where a bisect goes wrong -------------------------
    check(say("Aardvark") == "Erdferkel", "the first entry in sort order");
    check(say("Zulu") == "Zulu-Zeit", "and the last one");

    // ---- a language that carries only some of the messages -------------------------
    check(hobbycad::cli_catalogs::install("fr"), "French installs");
    check(say("Copy", "edit.copy") == "Copier", "what French has, French answers");
    check(say("Design rejected") == "Design rejected",
          "and where its entry is null the source stands");

    // ---- text that has to survive being written as C++ -----------------------------
    check(hobbycad::cli_catalogs::install("nl"), "Dutch installs");
    check(say("Quoted") == "Hij zei \"ga\" en 5\xC2\xB0 en \\ terug",
          "quotes, a degree sign and a backslash all come back as they went in");

    std::printf("\n%s (%d failure(s))\n", failures ? "FAILURES" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
