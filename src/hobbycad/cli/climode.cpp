// =====================================================================
//  src/hobbycad/cli/climode.cpp — Command-line mode
// =====================================================================
//
//  The standalone CLI REPL.  Delegates command dispatch to CliEngine
//  and uses TerminalInput for zsh-style line editing.
//
// =====================================================================

#include "climode.h"
#include "pagernav.h"
#include <optional>
#include <sstream>
#include <algorithm>
#include <vector>

#include <hobbycad/core.h>
#include <hobbycad/brep_io.h>

#include <hobbycad/strutil.h>

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>

#include <iostream>

namespace hobbycad {

namespace {

/// Page output the way `less` does.
///
/// Aaron, 2026-08-27: *"Please take clues from the less program about
/// pagination commands."* The bindings below are less's, minus searching:
///
///     SPACE, f      forward one screen
///     b             back one screen
///     ENTER, j      forward one line
///     k             back one line
///     g             first line          G   last line
///     q             stop
///     h             this list
///
/// Backward movement is cheap here because the whole text is already in
/// memory (unlike less, which may be reading a stream). So there is no
/// reason to offer only forward paging.
void writePaged(const std::string& text, bool paginate,
                TerminalInput& terminal)
{
    // Paging is for a person at a terminal. A piped or redirected run must
    // never stop for a keypress: "hobbycad --no-gui < script" would hang
    // forever, and the caller would see a program that never finished.
    if (!paginate || !terminal.isInteractive()) {
        std::cout << text << std::endl;
        return;
    }

    std::vector<std::string> lines;
    {
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line)) {
            lines.push_back(line);
        }
    }

    const int total = static_cast<int>(lines.size());

    // Height is read fresh on every screen, not once. A terminal can be
    // resized while the pager is open (and it commonly is, precisely
    // because the output did not fit), so a page measured at the old size
    // would skip lines after the window grew, or overflow after it shrank.
    // The "-- more --" prompt takes a line, so paging N+1 lines into an
    // N-line page costs the same room as printing them all. Do not start
    // the pager for a single line it cannot save.
    if (terminal.terminalHeight() >= total) {
        std::cout << text << std::endl;               // it already fits
        return;
    }

    int top = 0;
    int lastTop = 0;          ///< where the previous screen started
    int shownThrough = 0;     ///< one past the last line already printed
    bool showHelp = false;

    for (;;) {
        // Re-measured every screen, and at least one content line however
        // small the window gets. An earlier version dumped the remainder
        // when the window fell below four rows, but flooding the terminal
        // is precisely what paging exists to prevent, and it throws away
        // the reader's position at the moment they can least afford it.
        const int rows = std::max(1, terminal.terminalHeight() - 1);

        // Clamp after a resize. Shrinking is safe on its own (a smaller
        // page only raises the last valid top), but GROWING lowers it, so
        // a top that was fine a moment ago can now sit past the last
        // screenful and show a screen that is mostly blank.
        top = std::max(0, std::min(top, std::max(0, total - rows)));

        // Print only what has not been printed yet, when moving forward
        // over ground we already covered.
        //
        // A streaming pager cannot unscroll, so it repaints by printing
        // again, but printing a whole page for a ONE-LINE step made "j"
        // scroll the terminal by a full screen of mostly repeated text.
        // `more` gets this right: Enter reveals one line. Forward movement
        // that stays contiguous now prints just the newly exposed lines,
        // so "j" shows one line and SPACE shows a page. Backward movement
        // and jumps still reprint, because there is nothing else a stream
        // can do.
        const int wantEnd = std::min(top + rows, total);
        const int from = (top >= lastTop && top <= shownThrough)
                             ? std::max(top, shownThrough)
                             : top;
        for (int i = from; i < wantEnd; ++i) {
            std::cout << lines[i] << "\n";
        }
        // The frontier is where THIS screen ends, not the furthest we have
        // ever reached. Keeping the maximum meant that after paging back
        // and then forward again, the next page was considered "already
        // printed" and nothing appeared; the user pressed space and the
        // screen sat there.
        shownThrough = wantEnd;
        lastTop = top;

        const int last = std::min(top + rows, total);
        const bool atEnd = last >= total;

        if (showHelp) {
            std::cout << "  SPACE/f page  b back  ENTER/j line  k up  "
                         "g top  G end  q quit\n";
            showHelp = false;
        }

        // less shows (END) when there is nothing more; so does this, since
        // that is the thing a user actually looks for.
        std::cout << (atEnd ? "(END)" : ":")
                  << " [" << last << "/" << total << "] " << std::flush;

        const int c = terminal.readKey();

        // Erase the pager prompt. When it said (END) the pager is finished,
        // so leave the row blank as a separator instead of letting the
        // shell prompt land on it: output and prompt running together
        // makes a long result look like it is still going.
        std::cout << "\r\033[K" << std::flush;
        if (atEnd) {
            std::cout << "\n";
        }

        if (c == 'h' || c == 'H') {
            showHelp = true;
            continue;
        }
        const int next = pagerStep(c, top, rows, total);
        if (next == kPagerQuit) {
            return;
        }
        top = next;
    }
}

}  // namespace

CliMode::CliMode()
    : m_engine(m_history)
    , m_terminal(m_history)
{
    // Give the commands a document to act on. Without this the CLI could
    // parse everything and change nothing, which is exactly what it did.
    m_engine.setDocumentHost(&m_docHost);
    // The same object is both hosts. Headless had no undo at all before
    // this: nothing implemented DocumentUndoHost outside MainWindow, so
    // "undo" in a scripted session always answered "no document".
    m_engine.setUndoHost(&m_docHost);

    m_history.load();
    m_terminal.setCommands(m_engine.commandNames());
    m_terminal.setEngine(&m_engine);
}

CliMode::~CliMode()
{
    m_history.save();
}

// ---- Single-command: convert ----------------------------------------

int CliMode::runConvert(const std::string& input, const std::string& output)
{
    std::cout << "Converting: " << input
              << " -> " << output << std::endl;

    // One command layer behind both the interactive CLI and this single-shot
    // entry point: delegate to the engine's convert so BREP<->project,
    // STL->STEP (the HobbyMesh slice+loft pipeline) and every future format
    // live in exactly one place (CliEngine::cmdConvert) rather than drifting
    // between two handlers. The tokenizer strips the quotes and keeps each
    // quoted path as a single token, so paths with spaces are safe.
    const CliResult r = m_engine.execute(
        subst("convert \"%1\" \"%2\"", input, output));
    if (!r.output.empty())
        std::cout << r.output << std::endl;
    if (!r.error.empty())
        std::cerr << r.error << std::endl;
    return r.exitCode;
}

// ---- Single-command: script -----------------------------------------

int CliMode::runScript(const std::string& scriptPath, bool checkOnly)
{
    // Support reading from stdin: "hobbycad script -" or "hobbycad script"
    // Also works with: cat script.txt | hobbycad script -
    const bool readFromStdin = scriptPath.empty() || scriptPath == "-";

    std::ifstream fileStream;
    std::istream* in = &std::cin;

    if (readFromStdin) {
        std::cerr << "Reading script from stdin..." << std::endl;
    } else {
        std::error_code ec;
        if (!std::filesystem::exists(scriptPath, ec)) {
            std::cerr << "Error: Script file not found: "
                      << scriptPath << std::endl;
            return 1;
        }

        fileStream.open(scriptPath);
        if (!fileStream) {
            std::cerr << "Error: Could not open script file: "
                      << std::strerror(errno) << std::endl;
            return 1;
        }
        in = &fileStream;

        if (checkOnly) {
            std::cout << "Checking script: " << scriptPath << std::endl;
        } else {
            std::cout << "Running script: " << scriptPath << std::endl;
        }
    }

    int lineNum = 0;
    int commandCount = 0;
    int errorCount = 0;
    std::vector<std::string> validCmds = m_engine.commandNames();

    // Also add sketch commands for syntax checking
    for (const char* extra : {"point", "line", "circle", "rectangle",
                              "arc", "finish", "discard"}) {
        validCmds.push_back(extra);
    }

    std::string rawLine;
    while (std::getline(*in, rawLine)) {
        // trim also drops the carriage return a script written on
        // Windows carries, which QTextStream used to strip.
        const std::string line = trim(rawLine);
        lineNum++;

        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') {
            continue;
        }

        commandCount++;

        if (checkOnly) {
            // Syntax check only - validate command name exists
            const std::vector<std::string> tokens = splitWhitespace(line);
            if (tokens.empty()) continue;

            const std::string cmd = toLower(tokens.front());

            if (std::find(validCmds.begin(), validCmds.end(), cmd)
                == validCmds.end()) {
                std::cerr << "[" << lineNum << "] ERROR: Unknown command '"
                          << cmd << "'" << std::endl;
                errorCount++;
            } else {
                std::cout << "[" << lineNum << "] OK: "
                          << line << std::endl;
            }
        } else {
            // Execute the command
            CliResult result = m_engine.execute(line);

            if (!result.output.empty()) {
                std::cout << "[" << lineNum << "] "
                          << result.output << std::endl;
            }

            if (result.exitCode != 0) {
                std::cerr << "Error at line " << lineNum << ": "
                          << result.error << std::endl;
                return 1;
            }

            if (result.requestExit) {
                // Script requested exit
                break;
            }
        }
    }

    if (checkOnly) {
        if (errorCount > 0) {
            std::cerr << "\nSyntax check failed: " << errorCount
                      << " error(s) in " << commandCount << " command(s)"
                      << std::endl;
            return 1;
        } else {
            std::cout << "\nSyntax check passed: " << commandCount
                      << " command(s) OK" << std::endl;
            return 0;
        }
    } else {
        std::cout << "\nScript completed: " << commandCount
                  << " command(s) executed." << std::endl;
        return 0;
    }
}

// ---- Interactive REPL -----------------------------------------------

int CliMode::runInteractive()
{
    // ASCII on purpose: this line goes to whatever console the process
    // inherited, and a Windows console code page renders UTF-8 punctuation
    // as three garbage characters.
    std::cout << "HobbyCAD " << hobbycad::version()
              << " - Command-Line Mode" << std::endl;
    std::cout << "Type 'help' for available commands, "
                 "or 'exit' to quit." << std::endl;

    if (m_terminal.isInteractive()) {
        std::cout << "Line editing active (Ctrl+R search, Tab "
                     "completion, Up/Down history)." << std::endl;
    }

    std::cout << "History: " << m_history.count() << " entries loaded from "
              << m_history.filePath() << std::endl;
    std::cout << std::endl;

    while (true) {
        const std::string prompt = m_engine.buildPrompt();
        bool canceled = false;
        const std::optional<std::string> line =
            m_terminal.readLine(prompt, &canceled);

        if (!line && !canceled) {
            // EOF
            std::cout << std::endl;
            break;
        }

        if (canceled) {
            continue;  // Ctrl+C: discard line, show new prompt
        }

        const std::string cmd = trim(*line);
        if (cmd.empty()) continue;

        m_history.append(cmd);

        CliResult result = m_engine.execute(cmd);

        // Update available commands (may change with context, e.g., sketch mode)
        m_terminal.setCommands(m_engine.commandNames());

        if (!result.output.empty()) {
            writePaged(result.output, result.paginate, m_terminal);
        }
        if (!result.error.empty()) {
            std::cerr << result.error << std::endl;
        }
        if (result.requestExit) {
            break;
        }
    }

    m_history.save();
    return 0;
}

}  // namespace hobbycad

