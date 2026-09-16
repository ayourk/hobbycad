// =====================================================================
//  src/libhobbycad/hobbycad/translate.h — optional message translation
// =====================================================================
//
//  The command layer is Qt-free, so it cannot call QObject::tr(). It
//  asks here instead, and a front end that has Qt installs a translator
//  that forwards to QCoreApplication::translate().
//
//  Without Qt the library answers in English, which is the deal: the
//  CLI still runs, it just stops translating (Aaron, 2026-09-15: "Langs,
//  if it makes it easier, CLI without Qt can be english only").
//
//  The context string stays "QObject" for messages that already live in
//  the catalogs under that context, so their existing translations in
//  all fifteen .ts files keep matching. lupdate cannot see through this
//  call, so every source string reaching it must also appear in the Qt
//  side's QT_TRANSLATE_NOOP table, which is what keeps it extractable.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_TRANSLATE_H
#define HOBBYCAD_TRANSLATE_H

#include <string>

namespace hobbycad {

/// Signature a front end installs. Returns the translated text, or the
/// source when it has no translation for it.
using TranslatorFn = std::string (*)(const char* context, const char* source);

/// The installed translator, or nullptr when nothing installed one.
/// A reference so both accessors below share one object without needing
/// a .cpp file.
inline TranslatorFn& translatorHook()
{
    static TranslatorFn hook = nullptr;
    return hook;
}

/// Install a translator. Passing nullptr goes back to English.
inline void setTranslator(TranslatorFn fn) { translatorHook() = fn; }

/// Translate a message, or return it unchanged when no translator is
/// installed. Never returns the empty string for a non-empty source: a
/// front end that answers with nothing is treated as having no
/// translation, because a blank command result is worse than an
/// untranslated one.
inline std::string translate(const char* context, const char* source)
{
    if (TranslatorFn hook = translatorHook()) {
        std::string out = hook(context, source);
        if (!out.empty()) return out;
    }
    return source ? std::string(source) : std::string();
}

}  // namespace hobbycad

#endif  // HOBBYCAD_TRANSLATE_H
