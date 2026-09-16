// =====================================================================
//  src/hobbycad/main.cpp — HobbyCAD startup dispatcher
// =====================================================================
//
//  Determines the appropriate startup mode:
//
//    1. Subcommands (convert, script)    → Command-Line Mode
//    2. --no-gui flag                    → Interactive CLI Mode
//    3. No display server detected       → Interactive CLI Mode
//    4. OCCT can initialize a GL context  → Full Mode (3D)
//    5. OCCT cannot, or was not asked     → Reduced Mode (2D)
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include <Standard_Failure.hxx>
#include "hobbycad/occt_failure.h"

#include "softcrash.h"
#include <QFileInfo>
#include <hobbycad/sketch/solver.h>
#include <hobbycad/core.h>
#include <hobbycad/crashhandler.h>
#include <hobbycad/opengl_info.h>

#include "cli/climode.h"
#include "cli_translator.h"
#include "gui/full/fullmodewindow.h"
#include "gui/reduced/reducedmodewindow.h"
#include "gui/themevalidator.h"
#include "i18n/translations.h"

#include <QApplication>
#include <QStyleFactory>
#include <cstring>
#include <filesystem>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QLocale>
#include <QSettings>
#include <QMessageBox>
#include <QTimer>

#ifdef Q_OS_WIN
#include <windows.h>
#include <cstdio>
#include <iostream>

// hobbycad.exe is a GUI-subsystem program (WIN32_EXECUTABLE), so a
// double-click opens no console window. A GUI-subsystem process started
// from cmd or PowerShell gets no console of its own either, so here it
// attaches to the parent's: --version, --help and --no-gui then talk to
// the terminal they were typed in. Handles the caller redirected (a
// pipe, a file, the test scripts' capture) are valid already and stay.
// One consequence of the subsystem: an interactive shell does not wait
// for the program, so its prompt can return before the output; "start
// /wait hobbycad --version" waits.
static bool stdHandleValid(DWORD id)
{
    HANDLE h = GetStdHandle(id);
    return h != nullptr && h != INVALID_HANDLE_VALUE;
}

static void attachParentConsole()
{
    const bool hadIn  = stdHandleValid(STD_INPUT_HANDLE);
    const bool hadOut = stdHandleValid(STD_OUTPUT_HANDLE);
    const bool hadErr = stdHandleValid(STD_ERROR_HANDLE);
    // A GUI-subsystem process never inherits its parent's console, only
    // the parent's handles (pipes, files, or console handles from the
    // hobbycad.com launcher). Attach to the console whenever there is
    // one, so console functions work, but keep every inherited handle:
    // only streams that had none are bound to the console.
    if (GetConsoleWindow() == nullptr &&
        !AttachConsole(ATTACH_PARENT_PROCESS)) {
        return;   // started from Explorer or the Start menu: no console
    }
    if (!hadIn)  { std::freopen("CONIN$",  "r", stdin);  }
    if (!hadOut) { std::freopen("CONOUT$", "w", stdout); }
    if (!hadErr) { std::freopen("CONOUT$", "w", stderr); }
    std::ios::sync_with_stdio(true);
}

// The interactive CLI needs somewhere to read and write. With inherited
// handles (a pipe, a file, the launcher's console) it has that already;
// only a shortcut with --no-gui, started with nothing at all, gets a
// fresh console window.
static void ensureConsoleForCli()
{
    if (GetConsoleWindow() != nullptr) {
        return;
    }
    if (stdHandleValid(STD_INPUT_HANDLE) && stdHandleValid(STD_OUTPUT_HANDLE)) {
        return;
    }
    if (!AllocConsole()) {
        return;
    }
    std::freopen("CONIN$",  "r", stdin);
    std::freopen("CONOUT$", "w", stdout);
    std::freopen("CONOUT$", "w", stderr);
    std::ios::sync_with_stdio(true);
}
#endif

#include <hobbycad/project.h>
#include <hobbycad/sketch/parsing.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <optional>

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

    // GUI startup command: --exec "command"
    QString execCommand;
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
        else if (arg == QLatin1String("--exec") && i + 1 < argc) {
            flags.execCommand = QString::fromLocal8Bit(argv[++i]);
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
              << "  --exec \"command\"          Execute a command on GUI startup\n"
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
              << "\n"
              << "Startup Modes:\n"
              << "  Full Mode       3D viewport, when OCCT can initialize one (default)\n"
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
              << "Built with:\n";

    // --version is the command-line equivalent of the About dialog, so it
    // names the SAME dependency set under the same rule: a dependency that
    // is linked in is listed, one that is not is absent entirely. If a row
    // is added to one of these, add it to the other.
    std::cout << "  Qt                        " << QT_VERSION_STR
              << " (runtime " << qVersion() << ")\n";

#ifdef HOBBYCAD_OCCT_VERSION
    std::cout << "  OpenCASCADE               " << HOBBYCAD_OCCT_VERSION << "\n";
#else
    std::cout << "  OpenCASCADE               (unknown)\n";
#endif

    if (hobbycad::sketch::Solver::isAvailable()) {
        std::cout << "  Solver (libslvs)          "
                  << hobbycad::sketch::solverVersionString();
        if (hobbycad::sketch::solverCanRecoverFromFaults()) {
            std::cout << "  (recovers from solver faults)";
        } else if (hobbycad::sketch::solverFatalHandlerAvailable()) {
            std::cout << "  (reports solver faults, cannot recover)";
        } else {
            std::cout << "  (stock: a solver fault ends the process)";
        }
        std::cout << "\n";
    }

#ifdef HOBBYCAD_JSON_VERSION
    std::cout << "  nlohmann/json             " << HOBBYCAD_JSON_VERSION << "\n";
#endif
#ifdef HOBBYCAD_WEBP_VERSION
    std::cout << "  libwebp                   " << HOBBYCAD_WEBP_VERSION << "\n";
#endif
#ifdef HOBBYCAD_STB_VERSION
    std::cout << "  stb_image                 " << HOBBYCAD_STB_VERSION << "\n";
#endif
#ifdef HOBBYCAD_EGL_VERSION
    std::cout << "  EGL                       " << HOBBYCAD_EGL_VERSION << "\n";
#endif
#ifdef HOBBYCAD_CMAKE_VERSION
    std::cout << "  CMake                     " << HOBBYCAD_CMAKE_VERSION << "\n";
#endif

#if defined(__GNUC__) && !defined(__clang__)
    std::cout << "  Compiler                  GCC " << __GNUC__ << "."
              << __GNUC_MINOR__ << "." << __GNUC_PATCHLEVEL__ << "\n";
#elif defined(__clang__)
    std::cout << "  Compiler                  Clang " << __clang_major__ << "."
              << __clang_minor__ << "." << __clang_patchlevel__ << "\n";
#endif
}

// ---- Helper: detect display server -----------------------------------

static bool hasDisplayServer()
{
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    // Windows and macOS have a window server whenever there is a login
    // session; neither sets DISPLAY or WAYLAND_DISPLAY. Qt defines
    // Q_OS_UNIX on macOS too, so this test has to come first: with the
    // X11/Wayland check applied to macOS, a double-click in Finder fell
    // straight into the command-line fallback and no window ever showed.
    return true;
#else
    // X11 or Wayland
    const char* display  = std::getenv("DISPLAY");
    const char* wayland  = std::getenv("WAYLAND_DISPLAY");
    return (display && display[0] != '\0') ||
           (wayland && wayland[0] != '\0');
#endif
}

// ---- main ------------------------------------------------------------

int main(int argc, char* argv[])
{
#ifdef Q_OS_WIN
    attachParentConsole();
#endif
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

    // Install crash handler early; it catches SIGABRT/SIGSEGV from
    // third-party libraries (e.g., libslvs assertion failures) and
    // exits gracefully with a diagnostic message instead of a raw crash.
    hobbycad::CrashHandler::install();

    // Crash log location, per platform convention, with its directory
    // created here. The handler opens the file with a bare fopen() from a
    // signal context and cannot create directories; on Linux the directory
    // happened to exist because QSettings writes its file there, on macOS
    // and Windows it never did, and every crash log was silently lost.
    {
        std::string logDir;
        const char* home = std::getenv("HOME");
#if defined(_WIN32)
        if (const char* appdata = std::getenv("APPDATA")) {
            logDir = std::string(appdata) + "\\HobbyCAD";
        }
#elif defined(__APPLE__)
        if (home) {
            logDir = std::string(home) + "/Library/Logs/HobbyCAD";
        }
#else
        if (home) {
            logDir = std::string(home) + "/.config/HobbyCAD";
        }
#endif
        if (!logDir.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(logDir, ec);
            const std::string logPath = logDir +
#ifdef _WIN32
                "\\crash.log";
#else
                "/crash.log";
#endif
            hobbycad::CrashHandler::setCrashLogPath(logPath.c_str());
        }
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
        hobbycad::installCliTranslator();
        hobbycad::CliMode cli;
        int result = cli.runConvert(flags.convertInput.toStdString(),
                                    flags.convertOutput.toStdString());
        hobbycad::shutdown();
        return result;
    }

    if (flags.scriptCmd) {
        // scriptPath can be empty (for stdin) or "-" or a filename
        hobbycad::installCliTranslator();
        hobbycad::CliMode cli;
        int result = cli.runScript(flags.scriptPath.toStdString(), flags.scriptCheck);
        hobbycad::shutdown();
        return result;
    }

    // Step 1d: Interactive CLI mode
    if (flags.noGui) {
#ifdef Q_OS_WIN
        ensureConsoleForCli();
#endif
        hobbycad::installCliTranslator();
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

#ifdef Q_OS_WIN
        ensureConsoleForCli();
#endif
        hobbycad::installCliTranslator();
        hobbycad::CliMode cli;
        int result = cli.runInteractive();
        hobbycad::shutdown();
        return result;
    }

    // Step 3: Initialize Qt
    // A QApplication subclass, so an exception escaping an event handler
    // becomes a soft crash (logged, degraded if possible, saved and
    // closed cleanly if not) rather than std::terminate.
    // Did the user pick a widget style on the command line? QApplication
    // consumes -style, so look before constructing it.
    bool styleChosen = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-style") == 0 ||
            std::strncmp(argv[i], "-style=", 7) == 0) {
            styleChosen = true;
        }
    }

    hobbycad::SoftCrashApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("HobbyCAD"));
    app.setApplicationVersion(QString::fromLatin1(hobbycad::version()));
    app.setOrganizationName(QStringLiteral("HobbyCAD"));

    // Widget style. The stylesheet themes are written against Qt's Fusion
    // style, which is also what Linux runs by default, so that is where
    // they have always been seen. macOS and Windows apply their native
    // styles underneath the same stylesheet and the mix shows: on macOS
    // the Objects/Files tab bar drew over the Project dock's title, the
    // dock buttons kept the light native glyphs and the tabs became a blue
    // segmented control. Fusion on every platform, unless the user chose
    // a style with -style or QT_STYLE_OVERRIDE.
    if (!styleChosen && !qEnvironmentVariableIsSet("QT_STYLE_OVERRIDE")) {
        if (QStyle* fusion = QStyleFactory::create(QStringLiteral("Fusion"))) {
            app.setStyle(fusion);
        }
    }

    // Set application icon for taskbar/dock/window decorations
    // This enables the icon to display like Chrome/Firefox on Linux
    app.setWindowIcon(QIcon::fromTheme(
        QStringLiteral("hobbycad"),
        QIcon(QStringLiteral(":/icons/hobbycad.svg"))));

    // Step 3a: Load translations
    //   Priority: user preference > system locale > English (built-in)
    //
    // An empty setting means "follow the system", which is the default and
    // what QLocale() already resolves to, so it is passed through as-is
    // rather than being turned into a concrete locale here. Doing that would
    // freeze the answer at whatever the system said the first time the
    // program ran.
    //
    // This has to happen before anything composes a translated string.
    // QCommandLineParser builds its help text as it is constructed, so
    // installing after that point leaves --help in English however complete
    // the catalog is.
    {
        QSettings settings;
        settings.beginGroup(QStringLiteral("preferences"));
        const QString locale =
            settings.value(QStringLiteral("language")).toString();
        settings.endGroup();
        hobbycad::translations::install(app, locale);
    }

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

    // The sketch canvas paints with QPainter, which a QSS stylesheet cannot
    // reach, so tell it whether the widget theme is dark and it picks its own
    // dark palette (SketchCanvas::isDarkContext reads this qApp property).
    // Heuristic: the loaded theme names a dark theme; HOBBYCAD_DARK=1/0
    // overrides explicitly.
    {
        bool dark = themeSource.contains(QStringLiteral("dark"), Qt::CaseInsensitive);
        const char* envDark = std::getenv("HOBBYCAD_DARK");
        if (envDark && envDark[0] != '\0') {
            const char c = envDark[0];
            dark = (c == '1' || c == 't' || c == 'T' || c == 'y' || c == 'Y');
        }
        app.setProperty("hobbycad_dark_theme", dark);

        // Remember whether the launch chose the theme explicitly, so the saved
        // in-app Light/Dark choice does not override an intentional override.
        const bool fromFlag = !flags.themePath.isEmpty()
            || (std::getenv("HOBBYCAD_THEME") && std::getenv("HOBBYCAD_THEME")[0])
            || (envDark && envDark[0]);
        app.setProperty("hobbycad_theme_from_flag", fromFlag);
    }

    // Step 4: Probe OpenGL capabilities
    hobbycad::OpenGLInfo glInfo = hobbycad::probeOpenGL();
    // OCCT is the component that has to succeed, so it decides. The GL
    // probe above still runs, but only to gather version/renderer/vendor
    // for the About dialog and the status bar; it no longer gates
    // anything.
    hobbycad::probeOcctViewer(glInfo);

    // Step 5: Check for forced Reduced Mode via environment variable
    //   HOBBYCAD_REDUCED_MODE=1:   force Reduced Mode even if OpenGL
    //                              is available (useful for testing)
    bool forceReduced = false;
    const char* envReduced = std::getenv("HOBBYCAD_REDUCED_MODE");
    if (envReduced && envReduced[0] == '1') {
        forceReduced = true;
    }

    // Step 5b: Check for forced window geometry via environment variable
    //   HOBBYCAD_GEOMETRY=WxH:   force window to specific dimensions
    //                            (e.g. HOBBYCAD_GEOMETRY=800x600)
    //
    // Deliberately NOT listed in --help or the man page. This exists for the
    // screenshot harness, which needs a fixed window size; ordinary users
    // size the window through their window manager. Please do not
    // re-advertise it.
    //
    // Note: Qt honors the X11 "-geometry WxH" form, so users have a way to
    // size the window without this variable. Two caveats, both verified:
    //   * Our own parser does not know "-geometry", so the WxH value falls
    //     through to the positional file argument and prints
    //     "Error: no such file or directory: 1280x900" while still resizing.
    //   * "--geometry=WxH" is not honored at all.
    // Either route is clamped by setMinimumSize(800, 600) in MainWindow, so
    // a request smaller than that yields 800x600.
    int forceWidth = 0, forceHeight = 0;
    const char* envGeometry = std::getenv("HOBBYCAD_GEOMETRY");
    if (envGeometry) {
        if (std::sscanf(envGeometry, "%dx%d", &forceWidth, &forceHeight) != 2) {
            forceWidth = forceHeight = 0;
        }
    }

    // Helper: parse an --exec command and resolve the sketch plane.
    // Returns the SketchPlane to enter, or std::nullopt if invalid.
    using hobbycad::SketchPlane;

    auto resolveExecSketchPlane = [](const QString& cmd) -> std::optional<SketchPlane> {
        auto tokens = hobbycad::sketch::tokenizeLine(cmd.toStdString());
        // Expect: create sketch [XY|XZ|YZ | on [plane] <name>]
        auto toLower = [](std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), ::tolower);
            return s;
        };
        auto toUpper = [](std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), ::toupper);
            return s;
        };

        if (tokens.size() < 2
            || toLower(tokens[0]) != "create"
            || toLower(tokens[1]) != "sketch") {
            return std::nullopt;
        }

        if (tokens.size() == 2)
            return SketchPlane::XY;  // bare "create sketch"

        // Check for built-in plane name at tokens[2]
        std::string arg = toUpper(tokens[2]);
        if (arg == "XY") return SketchPlane::XY;
        if (arg == "XZ") return SketchPlane::XZ;
        if (arg == "YZ") return SketchPlane::YZ;

        // Check for "on [plane] <name>"
        if (toLower(tokens[2]) == "on" && tokens.size() >= 4) {
            size_t idx = 3;
            if (toLower(tokens[idx]) == "plane" && tokens.size() >= 5)
                idx = 4;
            std::string pArg = toUpper(tokens[idx]);
            if (pArg == "XY") return SketchPlane::XY;
            if (pArg == "XZ") return SketchPlane::XZ;
            if (pArg == "YZ") return SketchPlane::YZ;
            // TODO: named construction planes
            return SketchPlane::Custom;
        }

        return SketchPlane::XY;  // name-only, default plane
    };

    // Helper: open the positional file argument once the window exists.
    //
    // This used to be missing entirely: the argument was parsed into
    // flags.fileToOpen and then never read by anything, so `hobbycad
    // myproject/` opened an empty document while --help advertised it.
    // Scheduled the same way as --exec so the window is shown first and any
    // failure dialog has a parent.
    auto scheduleOpen = [&](auto* window) {
        if (flags.fileToOpen.isEmpty()) {
            return;
        }
        const QString path = flags.fileToOpen;
        if (!QFileInfo::exists(path)) {
            std::cerr << "Error: no such file or directory: "
                      << path.toStdString() << std::endl;
            return;
        }
        QTimer::singleShot(0, window, [window, path]() {
            window->openPath(path);
        });
    };

    // Helper: schedule --exec command to run after the event loop starts
    auto scheduleExec = [&](auto* window) {
        if (!flags.execCommand.isEmpty()) {
            auto plane = resolveExecSketchPlane(flags.execCommand);
            if (plane.has_value()) {
                QTimer::singleShot(0, window, [window, p = *plane]() {
                    // Let the full startup (viewport init and all) finish, THEN
                    // switch to the 2D sketch, through the one sketch-begin path.
                    window->beginStartupSketch(p);
                });
            } else {
                std::cerr << "Warning: unrecognized --exec command: "
                          << flags.execCommand.toStdString() << std::endl;
            }
        }
    };

    int result = 0;

    // The startup probe says OCCT *can* bring up a context. Actually
    // building the viewport can still fail: a different context, a driver
    // that dies under load, a display that goes away between the probe and
    // the window. Without this guard that throw left main() and terminated
    // the process, which is the worst outcome available: the 2D workspace
    // would have run perfectly.
    bool ranFullMode = false;

    if (glInfo.canRunViewport() && !forceReduced) {
        try {
            hobbycad::FullModeWindow window(glInfo);
            if (forceWidth > 0 && forceHeight > 0)
                window.resize(forceWidth, forceHeight);
            window.show();
            window.checkForCrashRecovery();
            scheduleOpen(&window);
            scheduleExec(&window);

            // Past construction and show: the viewport exists. Falling back
            // after the event loop starts would mean running a second one
            // over a half-torn-down window, so from here a failure is not
            // ours to swallow.
            ranFullMode = true;

            // From here the event loop owns the failure path: drop the
            // viewport and keep sketching if we can, save and exit cleanly
            // if we cannot.
            app.setDegradeHandler([&window]() { return window.dropViewport(); });
            app.setEmergencySave([&window]() { window.saveEmergencyCopy(); });

            result = app.exec();
        } catch (const Standard_Failure& e) {
            // OCCT's own type. 7.x does not derive it from std::exception,
            // so it must be caught by name to work across versions.
            if (ranFullMode) throw;
            const char* what = hobbycad::occtFailureMessage(e);
            glInfo.occtViewerWorks = false;
            glInfo.errorMessage =
                std::string("The 3D viewport failed to start: ")
                + (what ? what : "unknown OCCT error");
        } catch (const std::exception& e) {
            if (ranFullMode) throw;
            glInfo.occtViewerWorks = false;
            glInfo.errorMessage =
                std::string("The 3D viewport failed to start: ") + e.what();
        } catch (...) {
            if (ranFullMode) throw;
            glInfo.occtViewerWorks = false;
            glInfo.errorMessage =
                "The 3D viewport failed to start with an unknown error";
        }

        if (!ranFullMode) {
            std::fprintf(stderr, "%s\nFalling back to Reduced Mode.\n",
                         glInfo.errorMessage.c_str());
        }
    }

    if (!ranFullMode) {
        // Step 6b: Reduced Mode (OCCT declined, the viewport failed to
        // start, or Reduced Mode was forced).
        hobbycad::ReducedModeWindow window(glInfo);
        if (forceWidth > 0 && forceHeight > 0)
            window.resize(forceWidth, forceHeight);
        window.show();
        window.checkForCrashRecovery();
        scheduleOpen(&window);
        scheduleExec(&window);
        result = app.exec();
    }

    hobbycad::shutdown();
    return result;
}

