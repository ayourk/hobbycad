// =====================================================================
//  src/hobbycad/main.cpp — HobbyCAD startup dispatcher
// =====================================================================
//
//  Determines the appropriate startup mode:
//
//    1. CLI flags (--no-gui, --convert, --script) → Command-Line Mode
//    2. No display server detected                → Command-Line Mode
//    3. OpenGL 3.3+ available                     → Full Mode
//    4. OpenGL below 3.3 or unavailable           → Reduced Mode
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include <hobbycad/core.h>
#include <hobbycad/opengl_info.h>

#include "cli/climode.h"
#include "gui/full/fullmodewindow.h"
#include "gui/reduced/reducedmodewindow.h"
#include "gui/themevalidator.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QLocale>
#include <QMessageBox>
#include <QTranslator>

#include <cstdio>
#include <cstdlib>
#include <iostream>

// ---- Helper: check for CLI-only flags --------------------------------

struct StartupFlags {
    bool noGui   = false;
    bool convert = false;
    bool script  = false;
    QString convertInput;
    QString convertOutput;
    QString scriptPath;
    QString themePath;     // --theme <file.qss>
};

static StartupFlags parseFlags(int argc, char* argv[])
{
    StartupFlags flags;

    for (int i = 1; i < argc; ++i) {
        QString arg = QString::fromLocal8Bit(argv[i]);

        if (arg == QLatin1String("--no-gui")) {
            flags.noGui = true;
        }
        else if (arg == QLatin1String("--convert") && i + 2 < argc) {
            flags.convert      = true;
            flags.convertInput  = QString::fromLocal8Bit(argv[++i]);
            flags.convertOutput = QString::fromLocal8Bit(argv[++i]);
        }
        else if (arg == QLatin1String("--script") && i + 1 < argc) {
            flags.script     = true;
            flags.scriptPath = QString::fromLocal8Bit(argv[++i]);
        }
        else if (arg == QLatin1String("--theme") && i + 1 < argc) {
            flags.themePath = QString::fromLocal8Bit(argv[++i]);
        }
    }

    return flags;
}

// ---- Helper: detect display server -----------------------------------

static bool hasDisplayServer()
{
#if defined(Q_OS_LINUX) || defined(Q_OS_UNIX)
    // Check for X11 or Wayland
    const char* display  = std::getenv("DISPLAY");
    const char* wayland  = std::getenv("WAYLAND_DISPLAY");
    return (display && display[0] != '\0') ||
           (wayland && wayland[0] != '\0');
#else
    // Windows and macOS always have a display
    return true;
#endif
}

// ---- main ------------------------------------------------------------

int main(int argc, char* argv[])
{
    // Step 1: Parse CLI flags
    StartupFlags flags = parseFlags(argc, argv);

    // Initialize the core library
    if (!hobbycad::initialize()) {
        std::cerr << "Fatal: failed to initialize HobbyCAD core library."
                  << std::endl;
        return 1;
    }

    // Step 1b: CLI-only modes — no GUI needed
    if (flags.noGui || flags.convert || flags.script) {
        hobbycad::CliMode cli;

        int result = 0;
        if (flags.convert) {
            result = cli.runConvert(flags.convertInput, flags.convertOutput);
        } else if (flags.script) {
            result = cli.runScript(flags.scriptPath);
        } else {
            result = cli.runInteractive();
        }

        hobbycad::shutdown();
        return result;
    }

    // Step 2: Check for a display server
    if (!hasDisplayServer()) {
        std::cerr << "No display server detected (neither X11 nor Wayland)."
                  << std::endl
                  << "Cannot start graphical interface."
                  << std::endl
                  << "Falling back to command-line mode."
                  << std::endl
                  << "Type 'help' for available commands, or 'exit' to quit."
                  << std::endl;

        hobbycad::CliMode cli;
        int result = cli.runInteractive();
        hobbycad::shutdown();
        return result;
    }

    // Step 3: Initialize Qt
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("HobbyCAD"));
    app.setApplicationVersion(QString::fromLatin1(hobbycad::version()));
    app.setOrganizationName(QStringLiteral("HobbyCAD"));

    // Step 3a: Load translations
    //   Priority: user preference > system locale > English (built-in)
    QTranslator translator;
    QString locale = QLocale::system().name();  // e.g., "de_DE"

    // Try embedded resource, then external file
    if (translator.load(
            QStringLiteral(":/translations/hobbycad_") + locale)) {
        app.installTranslator(&translator);
    } else if (translator.load(
                   QStringLiteral("translations/hobbycad_") + locale)) {
        app.installTranslator(&translator);
    }
    // If neither loads, English tr() strings are used as-is.

    // Step 3b: Load theme stylesheet
    //   Priority: --theme flag > HOBBYCAD_THEME env > user config >
    //             built-in default
    QString themeSource;
    auto loadTheme = [&](const QString& path) -> bool {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QString qss = QString::fromUtf8(f.readAll());

            // Validate: reject themes where bg == fg
            hobbycad::ThemeValidationResult vr =
                hobbycad::validateTheme(qss);
            if (!vr.valid) {
                QString detail = vr.warnings.join(
                    QStringLiteral("\n\n"));
                QMessageBox::warning(nullptr,
                    QObject::tr("Theme Rejected"),
                    QObject::tr("The theme \"%1\" was not applied "
                        "because it contains rules where the "
                        "background color equals the text color, "
                        "which would make text invisible.\n\n%2")
                        .arg(path, detail));
                return false;
            }

            app.setStyleSheet(qss);
            themeSource = path;
            return true;
        }
        return false;
    };

    bool themeLoaded = false;

    // 1. Command-line flag
    if (!flags.themePath.isEmpty()) {
        themeLoaded = loadTheme(flags.themePath);
        if (!themeLoaded) {
            std::cerr << "Warning: could not load theme: "
                      << flags.themePath.toStdString() << std::endl;
        }
    }

    // 2. Environment variable
    if (!themeLoaded) {
        const char* envTheme = std::getenv("HOBBYCAD_THEME");
        if (envTheme && envTheme[0] != '\0') {
            themeLoaded = loadTheme(QString::fromLocal8Bit(envTheme));
        }
    }

    // 3. User config file
    if (!themeLoaded) {
        QString userTheme = QDir::homePath() +
            QStringLiteral("/.config/HobbyCAD/theme.qss");
        themeLoaded = loadTheme(userTheme);
    }

    // 4. Built-in default (embedded via .qrc)
    if (!themeLoaded) {
        themeLoaded = loadTheme(
            QStringLiteral(":/themes/default.qss"));
    }

    // Step 4: Probe OpenGL capabilities
    hobbycad::OpenGLInfo glInfo = hobbycad::probeOpenGL();

    // Step 5: Check for forced Reduced Mode via environment variable
    //   HOBBYCAD_REDUCED_MODE=1  — force Reduced Mode even if OpenGL
    //                              is available (useful for testing)
    bool forceReduced = false;
    const char* envReduced = std::getenv("HOBBYCAD_REDUCED_MODE");
    if (envReduced && envReduced[0] == '1') {
        forceReduced = true;
    }

    // Step 5b: Check for forced window geometry via environment variable
    //   HOBBYCAD_GEOMETRY=WxH  — force window to specific dimensions
    //                            (e.g. HOBBYCAD_GEOMETRY=800x600)
    int forceWidth = 0, forceHeight = 0;
    const char* envGeometry = std::getenv("HOBBYCAD_GEOMETRY");
    if (envGeometry) {
        if (std::sscanf(envGeometry, "%dx%d", &forceWidth, &forceHeight) != 2) {
            forceWidth = forceHeight = 0;
        }
    }

    int result = 0;

    if (glInfo.meetsMinimum() && !forceReduced) {
        // Step 6a: Full Mode — OpenGL 3.3+ available
        hobbycad::FullModeWindow window(glInfo);
        if (forceWidth > 0 && forceHeight > 0)
            window.resize(forceWidth, forceHeight);
        window.show();
        result = app.exec();
    } else {
        // Step 6b: Reduced Mode — OpenGL insufficient or forced
        hobbycad::ReducedModeWindow window(glInfo);
        if (forceWidth > 0 && forceHeight > 0)
            window.resize(forceWidth, forceHeight);
        window.show();
        result = app.exec();
    }

    hobbycad::shutdown();
    return result;
}

