// SPDX-License-Identifier: GPL-3.0-only
//
// HobbyCAD - ayourk/hobbycad
// src/hobbycad/i18n/translations.cpp
//
#include "translations.h"

#include "retranslatable.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QLibraryInfo>
#include <QLocale>
#include <QRegularExpression>
#include <QSet>
#include <QTranslator>
#include <QWidget>

namespace hobbycad::translations {
namespace {

/// Set by install() to the locale whose application catalog actually loaded,
/// and cleared when none did. File-scope rather than derived on demand because
/// the only other way to answer the question is to inspect the installed
/// QTranslators, and the Qt base catalog is indistinguishable from ours there.
QString g_activeLocale;

/// Catalogs produced by machine translation. Kept here rather than in the .ts
/// files because a .qm carries no metadata a program can read back, and the
/// disclaimer has to be shown by the running application, not by the
/// translator's tool. Remove a code from this list when a human has reviewed
/// that catalog.
const char* const kMachineTranslated[] = {
    "cs", "de", "es", "fr", "it", "ja", "ko",
    "nl", "pl", "pt", "ru", "tr", "zh"};

} // namespace

QStringList catalogDirs()
{
    // Order is deliberate: the first hit wins, so an external catalog
    // overrides the one compiled in. That lets a translator test a .qm without
    // rebuilding, while the embedded copy guarantees the program is translated
    // even when nothing was installed.
    QStringList dirs;
    dirs << QCoreApplication::applicationDirPath()
                + QStringLiteral("/translations");
#ifdef HOBBYCAD_TRANSLATIONS_DIR
    dirs << QStringLiteral(HOBBYCAD_TRANSLATIONS_DIR);
#endif
    dirs << QStringLiteral(":/i18n");
    return dirs;
}

QStringList available()
{
    // hobbycad_de.qm -> "de", hobbycad_pt_BR.qm -> "pt_BR".
    static const QRegularExpression pattern(
        QStringLiteral("^hobbycad_([A-Za-z]{2,3}(?:_[A-Za-z0-9]+)*)\\.qm$"));

    QStringList locales;
    for (const QString& path : catalogDirs()) {
        const QDir dir(path);
        if (!dir.exists()) {
            continue;
        }
        const QStringList entries =
            dir.entryList({QStringLiteral("hobbycad_*.qm")}, QDir::Files);
        for (const QString& name : entries) {
            const auto m = pattern.match(name);
            if (!m.hasMatch()) {
                continue;
            }
            // The first directory that has a catalog for a locale wins,
            // matching the load order, so a build-tree copy shadows an
            // installed one instead of appearing twice.
            if (locales.contains(m.captured(1))) {
                continue;
            }
            // Skip a catalog with nothing in it. hobbycad_en.qm is the
            // extraction template: English is the source language, so it adds
            // nothing over the source strings. lrelease still emits a valid
            // file, and offered as a language it would appear in the menu as
            // "American English" and, when chosen, silently show the same
            // text: an entry that looks like a choice and is not one.
            QTranslator probe;
            if (!probe.load(dir.filePath(name)) || probe.isEmpty()) {
                continue;
            }
            locales << m.captured(1);
        }
    }
    locales.sort();
    return locales;
}

QString languageName(const QString& locale)
{
    const QString name = QLocale(locale).nativeLanguageName();
    return name.isEmpty() ? locale : name;
}

bool isMachineTranslated(const QString& locale)
{
    // Match on the language part, so "de_DE" and "de" answer the same.
    const QString language = locale.section(QLatin1Char('_'), 0, 0);
    for (const char* const code : kMachineTranslated) {
        if (language.compare(QLatin1String(code), Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

void install(QCoreApplication& app, const QString& locale)
{
    const QLocale target = locale.isEmpty() ? QLocale() : QLocale(locale);

    // Cleared first: a failed load must not leave the previous language's
    // answer in place, which would matter after switchTo() to a locale with no
    // catalog.
    g_activeLocale.clear();

    // Parented to the application: QCoreApplication does not take ownership of
    // a translator, and one that goes out of scope takes its strings with it.
    auto* appCatalog = new QTranslator(&app);
    for (const QString& dir : catalogDirs()) {
        if (appCatalog->load(target, QStringLiteral("hobbycad"),
                             QStringLiteral("_"), dir)) {
            QCoreApplication::installTranslator(appCatalog);
            // isEmpty() guards the extraction template: hobbycad_en.qm loads
            // successfully and adds nothing, so loading is not by itself
            // evidence that anything is translated.
            if (!appCatalog->isEmpty()) {
                g_activeLocale = target.name();
            }
            break;
        }
    }

    // Qt's own, for the standard dialogs and the Qt options in --help-all.
    auto* qtCatalog = new QTranslator(&app);
    if (qtCatalog->load(target, QStringLiteral("qtbase"), QStringLiteral("_"),
                        QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
        QCoreApplication::installTranslator(qtCatalog);
    }
}

QString activeLocale()
{
    return g_activeLocale;
}

QString systemLanguageName()
{
    const QLocale system;
    // uiLanguages is the ordered list the loader itself walks, e.g.
    // ("de-DE", "de-Latn-DE", "de"), so matching against it gives the same
    // answer QTranslator::load would reach rather than a guess from name().
    const QStringList codes = available();
    for (const QString& tag : system.uiLanguages()) {
        const QString candidate =
            QString(tag).replace(QLatin1Char('-'), QLatin1Char('_'));
        for (const QString& code : codes) {
            if (candidate.compare(code, Qt::CaseInsensitive) == 0
                || candidate.startsWith(code + QLatin1Char('_'),
                                        Qt::CaseInsensitive)
                || code.startsWith(candidate + QLatin1Char('_'),
                                   Qt::CaseInsensitive)) {
                return languageName(code);
            }
        }
    }
    return QString();
}

void switchTo(QCoreApplication& app, const QString& locale)
{
    // Removing a translator posts LanguageChange too, so anything that does
    // implement changeEvent still hears about it; the sweep below is for the
    // classes that only implement Retranslatable.
    const auto installed = app.findChildren<QTranslator*>();
    for (QTranslator* old : installed) {
        QCoreApplication::removeTranslator(old);
        delete old;
    }

    install(app, locale);

    // Every widget, not just toplevel ones, and including hidden ones so a tab
    // that is not currently visible is correct when it is shown.
    const auto widgets = QApplication::allWidgets();
    for (QWidget* w : widgets) {
        if (auto* r = dynamic_cast<Retranslatable*>(w)) {
            r->retranslate();
        }
    }
}

} // namespace hobbycad::translations
