// =====================================================================
//  src/libhobbycad/hobbycad/locale.h — which language to answer in
// =====================================================================
//
//  Capability tier of the front-end support layer. A front end with Qt
//  asks QLocale; one without it has nothing to ask, and every platform
//  answers differently: POSIX sets LC_ALL, LC_MESSAGES or LANG, while
//  Windows sets none of them and keeps the answer in
//  GetUserDefaultLocaleName().
//
//  This returns the locales to try, most specific first, so a front end
//  can walk them against whatever catalogs it has and stop at the first
//  hit. It reads the environment and the platform only: nothing here
//  calls setlocale(), because the numeric locale decides how numbers
//  print and a command line's output is also a script's input.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_LOCALE_H
#define HOBBYCAD_LOCALE_H

#include "core.h"

#include <string>
#include <vector>

namespace hobbycad {

/// The locales to try, most specific first and without duplicates:
/// "de_DE" before "de", "pt_BR" before "pt".
///
/// Sources, in order of precedence: HOBBYCAD_LANG, then LC_ALL,
/// LC_MESSAGES and LANG, then the platform's own answer
/// (GetUserDefaultLocaleName() on Windows). "C" and "POSIX" mean English
/// and end the list. Empty when nothing said anything, which a front end
/// reads as "answer in English".
HOBBYCAD_EXPORT std::vector<std::string> preferredLocales();

/// A locale name as the catalogs spell it: separators become '_', an
/// encoding or modifier suffix goes ("de-DE.UTF-8@euro" -> "de_DE"), the
/// language is lowercased and the territory upper-cased ("DE_de" ->
/// "de_DE"). Returns "" for text that is not a locale name, and for "C"
/// and "POSIX".
HOBBYCAD_EXPORT std::string normalizeLocale(const std::string& name);

/// The candidates one locale name stands for, most specific first:
/// "pt_BR" gives {"pt_BR", "pt"}. Used by preferredLocales(), and
/// separately useful to a front end handed a name of its own.
HOBBYCAD_EXPORT std::vector<std::string> localeCandidates(const std::string& name);

}  // namespace hobbycad

#endif  // HOBBYCAD_LOCALE_H
