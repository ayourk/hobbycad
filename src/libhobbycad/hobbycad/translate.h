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

/// Marks a literal for extraction into the translation catalogs without
/// translating it where it stands; the front end translates it at display
/// time with the same context. The library does not depend on Qt, so it
/// cannot use QT_TRANSLATE_NOOP; the lupdate target is told to read this
/// name as that one (src/hobbycad/CMakeLists.txt, -tr-function-alias).
/// A macro, not a function, because the extraction tool matches names.
#define HOBBYCAD_TRANSLATE_NOOP(context, text) text

/// The same with a disambiguation, for text that needs one; it expands to a
/// braced {text, disambiguation} pair. Read by lupdate as QT_TRANSLATE_NOOP3.
#define HOBBYCAD_TRANSLATE_NOOP3(context, text, disambiguation) {text, disambiguation}

namespace hobbycad {

/// Signature a front end installs. Returns the translated text, or the
/// source when it has no translation for it. `disambiguation` is null for
/// text extracted without one.
using TranslatorFn =
    std::string (*)(const char* context, const char* source, const char* disambiguation);

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
inline std::string translate(const char* context, const char* source,
                             const char* disambiguation = nullptr)
{
    if (TranslatorFn hook = translatorHook()) {
        std::string out = hook(context, source, disambiguation);
        if (!out.empty()) return out;
    }
    return source ? std::string(source) : std::string();
}

}  // namespace hobbycad

#endif  // HOBBYCAD_TRANSLATE_H
