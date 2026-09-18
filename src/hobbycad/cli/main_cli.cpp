// =====================================================================
//  src/hobbycad/cli/main_cli.cpp — entry point for the standalone CLI
// =====================================================================
//
//  The command line as its own front end: libhobbycad plus the command
//  layer, no Qt and no GUI. The application has its own main() that can
//  reach the same commands, and adds a Qt translator on the way; this
//  one answers in English, which is the deal for a Qt-free build.
//
//  Kept deliberately small. Anything a front end and the application
//  would both want belongs in CliMode or the library, not here.
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include "climode.h"

#include <hobbycad/core.h>
#if HOBBYCAD_CLI_CATALOGS
#  include <hobbycad/locale.h>
#endif

#include <iostream>
#include <string>
#include <vector>

namespace {

void printVersion()
{
    std::cout << "HobbyCAD " << hobbycad::version() << "\n"
              << "Copyright (C) 2024-2026 HobbyCAD Contributors\n"
              << "License: GPL-3.0-only\n";
}

void printUsage(const std::string& program)
{
    std::cout
        << "Usage: " << program << " [command] [options]\n"
        << "\n"
        << "The HobbyCAD command line. It drives the same model the\n"
        << "application does, without needing a display or Qt.\n"
        << "\n"
        << "Commands:\n"
        << "  convert <input> <output>   Convert a file and exit\n"
        << "  script [file]              Run a script and exit; \"-\" or no\n"
        << "                             file reads standard input\n"
        << "  (none)                     Start the interactive prompt\n"
        << "\n"
        << "Options:\n"
        << "  --check, --dry-run         With \"script\": check the syntax\n"
        << "                             without running the commands\n"
#if HOBBYCAD_CLI_CATALOGS
        << "  --lang <name>              Answer in this language (de, pt_BR, ...)\n"
#endif
        << "  --version                  Print the version and exit\n"
        << "  --help                     Print this text and exit\n";
}

}  // namespace

#if HOBBYCAD_CLI_CATALOGS
namespace hobbycad {
namespace cli_catalogs {
/// Defined by the generated cli_catalogs.cpp: installs the table for a
/// locale, or answers false and leaves the seam as it was.
bool install(const char* locale);
}  // namespace cli_catalogs
}  // namespace hobbycad
#endif

int main(int argc, char* argv[])
{
    const std::string program = (argc > 0 && argv[0]) ? argv[0] : "hobbycad-cli";
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.push_back(argv[i]);

    for (const std::string& a : args) {
        if (a == "--version" || a == "-v") { printVersion(); return 0; }
        if (a == "--help" || a == "-h")    { printUsage(program); return 0; }
    }

#if HOBBYCAD_CLI_CATALOGS
    // The catalogs are compiled in (scripts/ts2cpp.py); pick a language
    // before anything prints. An explicit --lang wins, then the
    // environment and the platform (hobbycad/locale.h). Nothing found
    // means English, which is what the seam answers with no translator.
    {
        std::vector<std::string> wanted;
        for (std::size_t i = 0; i + 1 < args.size(); ++i) {
            if (args[i] != "--lang") continue;
            wanted = hobbycad::localeCandidates(args[i + 1]);
            args.erase(args.begin() + static_cast<std::ptrdiff_t>(i),
                       args.begin() + static_cast<std::ptrdiff_t>(i) + 2);
            break;
        }
        if (wanted.empty()) wanted = hobbycad::preferredLocales();
        for (const std::string& name : wanted) {
            if (hobbycad::cli_catalogs::install(name.c_str())) break;
        }
    }
#endif

    // The library comes up before any command runs, exactly as the
    // application brings it up: a command that quietly acted on an
    // uninitialized core would be worse than refusing to start.
    if (!hobbycad::initialize()) {
        std::cerr << "Fatal: failed to initialize HobbyCAD core library."
                  << std::endl;
        return 1;
    }

    int result = 0;

    if (!args.empty() && args[0] == "convert") {
        std::vector<std::string> rest(args.begin() + 1, args.end());
        std::string input, output;
        for (const std::string& a : rest) {
            if (!a.empty() && a[0] == '-') continue;      // flags, not paths
            if (input.empty())       input = a;
            else if (output.empty()) output = a;
        }
        if (input.empty() || output.empty()) {
            std::cerr << "Error: convert requires input and output arguments.\n"
                      << "Run '" << program << " --help' for usage.\n";
            hobbycad::shutdown();
            return 1;
        }
        hobbycad::CliMode cli;
        result = cli.runConvert(input, output);
    } else if (!args.empty() && args[0] == "script") {
        std::string path;
        bool checkOnly = false;
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "--check" || args[i] == "--dry-run") checkOnly = true;
            else if (!args[i].empty() && args[i][0] != '-')     path = args[i];
        }
        hobbycad::CliMode cli;
        result = cli.runScript(path, checkOnly);
    } else if (!args.empty()) {
        std::cerr << "Unknown command: " << args[0] << "\n"
                  << "Run '" << program << " --help' for usage.\n";
        hobbycad::shutdown();
        return 1;
    } else {
        hobbycad::CliMode cli;
        result = cli.runInteractive();
    }

    hobbycad::shutdown();
    return result;
}
