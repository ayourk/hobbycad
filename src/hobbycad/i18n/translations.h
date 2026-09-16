// SPDX-License-Identifier: GPL-3.0-only
//
// HobbyCAD - ayourk/hobbycad
// src/hobbycad/i18n/translations.h
//
#pragma once

#include <QStringList>

QT_BEGIN_NAMESPACE
class QCoreApplication;
QT_END_NAMESPACE

/// Finding and installing translation catalogs.
///
/// Split out of main.cpp because two places need the same answer: startup,
/// which installs a catalog, and the language menu, which offers the ones that
/// exist. Two separate searches would eventually disagree about which
/// languages are available, and the menu would offer one the program could not
/// load.
namespace hobbycad::translations {

/// Directories searched for catalogs, in order. Beside the binary first, so a
/// build tree can be tested without installing.
QStringList catalogDirs();

/// Locale codes with a catalog present, sorted, e.g. {"de", "ja", "ko"}.
/// Derived from the hobbycad_<locale>.qm files actually on disk: .ts files are
/// the translator's source and are not shipped, so they cannot be what is
/// offered.
QStringList available();

/// Human-readable name for a locale code, in its own language:
/// "Deutsch" for "de". Falls back to the code itself when Qt has no name.
QString languageName(const QString& locale);

/// True when the catalog for this locale was produced by machine translation
/// rather than by a human. Drives the disclaimer in the language menu and the
/// About dialog.
bool isMachineTranslated(const QString& locale);

/// Install the application and Qt catalogs. An empty locale means "follow the
/// system", which is the default and what QLocale() already resolves to.
///
/// Call before anything builds a translated string. QCommandLineParser
/// composes its help text as it is constructed, so installing after that
/// leaves --help in English however complete the catalog is.
void install(QCoreApplication& app, const QString& locale = QString());

/// Change language while the program is running.
///
/// Removes the catalogs install() put in place, installs the ones for
/// `locale`, then walks every widget and calls retranslate() on each that
/// implements hobbycad::Retranslatable.
///
/// The walk is deliberate rather than relying on QEvent::LanguageChange. Qt
/// posts that event to *toplevel* widgets only and it does not recurse, so a
/// changeEvent reimplementation would be needed in each class that owns
/// strings. This reaches every widget from one place, and being explicit also
/// means it happens before this function returns, so a caller can save the
/// setting afterwards knowing the interface already matches it.
void switchTo(QCoreApplication& app, const QString& locale);

/// Locale of the application catalog currently in use, or empty when none is,
/// which means the interface is showing the English of the source.
///
/// Not the same question as "which language did the user pick": the pick can
/// be "system default", and a pick can fail to load. This reports what
/// actually happened, which is what a caller wants when deciding whether to
/// say something about the translation being machine-made.
///
/// Ignores the Qt catalog. Qt's own translations are human-made and shipped by
/// Qt; only this application's are in question.
QString activeLocale();

/// The language "system default" would actually produce, named in its own
/// language: "Deutsch" for a de_DE system that has a German catalog.
///
/// Empty when no catalog matches the system locale, which means the interface
/// stays in the English of the source. Worth showing beside a "system default"
/// choice either way: on an English system the entry does nothing, and on a
/// German one it is the same as picking German, neither of which is apparent
/// from the words "system default" alone.
QString systemLanguageName();

} // namespace hobbycad::translations
