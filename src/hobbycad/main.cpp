// =====================================================================
//  src/hobbycad/main.cpp — HobbyCAD startup dispatcher
// =====================================================================
//
//  Determines the appropriate startup mode:
//
//    1. Subcommands (convert, script)    → Command-Line Mode
//    2. --no-gui flag                    → Interactive CLI Mode
//    3. No display server detected       → Interactive CLI Mode
//    4. OpenGL 3.3+ available            → Full Mode (3D)
//    5. OpenGL below 3.3 or unavailable  → Reduced Mode (2D)
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
    bool help    = false;
    bool version = false;
    bool noGui   = false;
    QString themePath;          // --theme <file.qss>
    QString fileToOpen;         // positional argument (file/project to open)

    // Subcommand: convert
    bool convertCmd = false;
    bool convertHelp = false;
    QString convertInput;
    QString convertOutput;
    QString convertFormat;      // --format (future: step, iges, stl, etc.)

    // Subcommand: script
    bool scriptCmd = false;
    bool scriptHelp = false;
    bool scriptCheck = false;   // --dry-run for syntax validation
    QString scriptPath;
};

static bool isHelpFlag(const QString& arg)
{
    // Unix/macOS style: --help, -h
    if (arg == QLatin1String("--help") ||
        arg == QLatin1String("-h")) {
        return true;
    }

#ifdef Q_OS_WIN
    // Windows style: /h, /?, /help
    if (arg == QLatin1String("/h") ||
        arg == QLatin1String("/?") ||
        arg == QLatin1String("/help")) {
        return true;
    }
#endif

    return false;
}

static bool isVersionFlag(const QString& arg)
{
    // --version only (not -v/-V, which typically means verbose)
    if (arg == QLatin1String("--version")) {
        return true;
    }

#ifdef Q_OS_WIN
    // Windows style: /version
    if (arg == QLatin1String("/version")) {
        return true;
    }
#endif

    return false;
}

static StartupFlags parseFlags(int argc, char* argv[])
{
    StartupFlags flags;

    for (int i = 1; i < argc; ++i) {
        QString arg = QString::fromLocal8Bit(argv[i]);

        // Global flags (before any subcommand)
        if (isHelpFlag(arg) && !flags.convertCmd && !flags.scriptCmd) {
            flags.help = true;
        }
        else if (isVersionFlag(arg)) {
            flags.version = true;
        }
        else if (arg == QLatin1String("--no-gui")) {
            flags.noGui = true;
        }
        else if (arg == QLatin1String("--theme") && i + 1 < argc) {
            flags.themePath = QString::fromLocal8Bit(argv[++i]);
        }
        // Subcommand: convert
        else if (arg == QLatin1String("convert") && !flags.convertCmd && !flags.scriptCmd) {
            flags.convertCmd = true;
            // Parse convert subcommand arguments
            while (++i < argc) {
                QString subArg = QString::fromLocal8Bit(argv[i]);
                if (isHelpFlag(subArg)) {
                    flags.convertHelp = true;
                }
                else if (subArg == QLatin1String("--format") && i + 1 < argc) {
                    flags.convertFormat = QString::fromLocal8Bit(argv[++i]);
                }
                else if (!subArg.startsWith(QLatin1Char('-'))) {
                    // Positional arguments: input and output
                    if (flags.convertInput.isEmpty()) {
                        flags.convertInput = subArg;
                    } else if (flags.convertOutput.isEmpty()) {
                        flags.convertOutput = subArg;
                    }
                }
            }
        }
        // Subcommand: script
        else if (arg == QLatin1String("script") && !flags.convertCmd && !flags.scriptCmd) {
            flags.scriptCmd = true;
            // Parse script subcommand arguments
            while (++i < argc) {
                QString subArg = QString::fromLocal8Bit(argv[i]);
                if (isHelpFlag(subArg)) {
                    flags.scriptHelp = true;
                }
                else if (subArg == QLatin1String("--check") ||
                         subArg == QLatin1String("--dry-run")) {
                    flags.scriptCheck = true;
                }
                else if (!subArg.startsWith(QLatin1Char('-'))) {
                    if (flags.scriptPath.isEmpty()) {
                        flags.scriptPath = subArg;
                    }
                }
            }
        }
        // Positional argument: file to open
        else if (!arg.startsWith(QLatin1Char('-')) && flags.fileToOpen.isEmpty()) {
            flags.fileToOpen = arg;
        }
    }

    return flags;
}

// ---- Helper: print help/version without GUI --------------------------

static void printHelp(const char* programPath)
{
    // Extract just the executable name from the path
    QString fullPath = QString::fromLocal8Bit(programPath);
    QString programName = QFileInfo(fullPath).fileName();
    if (programName.isEmpty()) {
        programName = QStringLiteral("hobbycad");
    }

    std::cout << "HobbyCAD - Parametric 3D CAD Application\n"
              << "Version " << hobbycad::version() << "\n"
              << "\n"
              << "Usage: " << programName.toStdString() << " [options] [file]\n"
              << "       " << programName.toStdString() << " <command> [args]\n"
              << "\n"
              << "Options:\n"
#ifdef Q_OS_WIN
              << "  -h, --help, /?, /h       Show this help message and exit\n"
              << "  --version, /version      Show version information and exit\n"
#else
              << "  -h, --help               Show this help message and exit\n"
              << "  --version                Show version information and exit\n"
#endif
              << "  --no-gui                 Start in interactive command-line mode\n"
              << "  --theme <file.qss>       Load custom Qt stylesheet theme\n"
              << "\n"
              << "Commands:\n"
              << "  convert <in> <out>       Convert between file formats\n"
              << "  script <file>            Execute a script file\n"
              << "\n"
              << "  Run '" << programName.toStdString() << " <command> --help' for command-specific options.\n"
              << "\n"
              << "Environment Variables:\n"
              << "  HOBBYCAD_THEME           Path to Qt stylesheet (.qss) file\n"
              << "  HOBBYCAD_REDUCED_MODE=1  Force Reduced Mode (2D canvas only)\n"
              << "  HOBBYCAD_GEOMETRY=WxH    Set initial window size (e.g., 1280x720)\n"
              << "\n"
              << "Startup Modes:\n"
              << "  Full Mode       OpenGL 3.3+ with 3D viewport (default when available)\n"
              << "  Reduced Mode    2D canvas only (when OpenGL unavailable or forced)\n"
              << "  CLI Mode        Interactive terminal (--no-gui or no display server)\n"
              << "\n"
              << "Interactive CLI:\n"
              << "  Start with --no-gui for an interactive command-line interface.\n"
              << "  Type 'help' for available commands including:\n"
              << "    new, open, save, export, import, extrude, sketch, and more.\n"
              << "\n"
              << "File Formats:\n"
              << "  .hcad           Native HobbyCAD project (directory with manifest)\n"
              << "  .brep, .brp     OpenCASCADE BREP geometry\n"
              << "\n"
              << "Examples:\n"
              << "  " << programName.toStdString() << "                       Start GUI (auto-detect mode)\n"
              << "  " << programName.toStdString() << " myproject/            Open project directory\n"
              << "  " << programName.toStdString() << " model.brep            Open BREP file in GUI\n"
              << "  " << programName.toStdString() << " --no-gui              Start interactive CLI\n"
              << "  " << programName.toStdString() << " convert in.brep out.brep\n"
              << "  " << programName.toStdString() << " script myscript.txt\n"
              << "\n"
              << "For more information, visit: https://github.com/ayourk/hobbycad\n";
}

static void printConvertHelp()
{
    std::cout << "Usage: hobbycad convert [options] <input> <output>\n"
              << "\n"
              << "Convert between CAD file formats.\n"
              << "\n"
              << "Arguments:\n"
              << "  <input>                  Input file path\n"
              << "  <output>                 Output file path\n"
              << "\n"
              << "Options:\n"
              << "  -h, --help               Show this help message\n"
              << "  --format <fmt>           Force output format (auto-detected from extension)\n"
              << "\n"
              << "Supported Formats:\n"
              << "  .hcad                    HobbyCAD project\n"
              << "  .brep, .brp              OpenCASCADE BREP\n"
              << "\n"
              << "Examples:\n"
              << "  hobbycad convert model.brep project/\n"
              << "  hobbycad convert myproject/ export.brep\n";
}

static void printScriptHelp()
{
    std::cout << "Usage: hobbycad script [options] [file]\n"
              << "\n"
              << "Execute a HobbyCAD script file.\n"
              << "\n"
              << "Arguments:\n"
              << "  <file>                   Script file to execute\n"
              << "  -                        Read script from stdin (for piping)\n"
              << "\n"
              << "Options:\n"
              << "  -h, --help               Show this help message\n"
              << "  --dry-run                Check syntax without executing\n"
              << "\n"
              << "Script files contain CLI commands, one per line.\n"
              << "Lines starting with '#' are treated as comments.\n"
              << "\n"
              << "Example script (egg.txt):\n"
              << "  # Create an egg shape from a cube\n"
              << "  new\n"
              << "  box 10 10 10\n"
              << "  fillet 2\n"
              << "  scale 1 1 1.5\n"
              << "  save myegg/\n"
              << "\n"
              << "Run with:\n"
              << "  hobbycad script egg.txt\n"
              << "  hobbycad script --dry-run egg.txt   # Validate without running\n"
              << "  cat egg.txt | hobbycad script -\n";
}

static void printVersion()
{
    std::cout << "HobbyCAD " << hobbycad::version() << "\n"
              << "Copyright (C) 2024-2026 HobbyCAD Contributors\n"
              << "License: GPL-3.0-only\n"
              << "\n"
              << "Built with:\n"
              << "  Qt " << QT_VERSION_STR << "\n"
              << "  OpenCASCADE Technology (OCCT)\n";
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

    // Step 1a: Handle --help and --version immediately (no GUI, no core init)
    if (flags.help) {
        printHelp(argv[0]);
        return 0;
    }
    if (flags.version) {
        printVersion();
        return 0;
    }

    // Step 1b: Handle subcommand help (no core init needed)
    if (flags.convertCmd && flags.convertHelp) {
        printConvertHelp();
        return 0;
    }
    if (flags.scriptCmd && flags.scriptHelp) {
        printScriptHelp();
        return 0;
    }

    // Initialize the core library
    if (!hobbycad::initialize()) {
        std::cerr << "Fatal: failed to initialize HobbyCAD core library."
                  << std::endl;
        return 1;
    }

    // Step 1c: Handle subcommands (CLI-only, no GUI needed)
    if (flags.convertCmd) {
        if (flags.convertInput.isEmpty() || flags.convertOutput.isEmpty()) {
            std::cerr << "Error: convert requires input and output arguments.\n"
                      << "Run 'hobbycad convert --help' for usage.\n";
            hobbycad::shutdown();
            return 1;
        }
        hobbycad::CliMode cli;
        int result = cli.runConvert(flags.convertInput, flags.convertOutput);
        hobbycad::shutdown();
        return result;
    }

    if (flags.scriptCmd) {
        // scriptPath can be empty (for stdin) or "-" or a filename
        hobbycad::CliMode cli;
        int result = cli.runScript(flags.scriptPath, flags.scriptCheck);
        hobbycad::shutdown();
        return result;
    }

    // Step 1d: Interactive CLI mode
    if (flags.noGui) {
        hobbycad::CliMode cli;
        int result = cli.runInteractive();
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

