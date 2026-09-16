// =====================================================================
//  src/hobbycad/cli/terminalinput.h — zsh-style terminal line editor
// =====================================================================
//
//  Provides interactive line editing for the CLI REPL using raw
//  terminal mode (POSIX termios on Unix, Windows Console API on
//  Windows).  No external dependencies (no readline, no editline).
//
//  Supported features modeled after zsh:
//
//    Line editing:
//      Left/Right          Move cursor
//      Home / Ctrl+A       Move to start of line
//      End  / Ctrl+E       Move to end of line
//      Alt+F / Alt+Right   Move forward one word
//      Alt+B / Alt+Left    Move backward one word
//      Backspace           Delete character before cursor
//      Delete / Ctrl+D     Delete character at cursor (or EOF on empty)
//      Ctrl+K              Kill from cursor to end of line
//      Ctrl+U              Kill from start of line to cursor
//      Alt+D               Kill forward one word
//      Ctrl+W              Kill backward one word
//      Ctrl+Y              Yank (paste) last killed text
//      Ctrl+T              Transpose characters
//
//    History:
//      Up   / Ctrl+P       Previous history entry
//      Down / Ctrl+N       Next history entry
//      Ctrl+R              Reverse incremental search
//      !! / !n / !prefix   Bang expansion (processed after Enter)
//
//    Other:
//      Tab                 Filename and command completion
//      Ctrl+L              Clear screen, redraw prompt + line
//      Ctrl+C              Cancel current line
//      Ctrl+D              Exit on empty line
//      Enter               Accept line
//
//  On platforms without termios (or when stdin is not a terminal),
//  falls back to plain std::getline.
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_TERMINALINPUT_H
#define HOBBYCAD_TERMINALINPUT_H

#include <optional>
#include <string>
#include <vector>

#if !defined(_WIN32)
  #include <termios.h>
#endif

namespace hobbycad {

class CliHistory;
class CliEngine;

class TerminalInput {
public:
    explicit TerminalInput(CliHistory& history);
    ~TerminalInput();

    /// Set the list of known commands for tab completion.
    void setCommands(const std::vector<std::string>& commands);

    /// Set the CLI engine for argument completion hints.
    void setEngine(CliEngine* engine);

    /// Read one line from the terminal with editing and history.
    /// Returns the entered text, or nothing on EOF (Ctrl+D on an empty
    /// line).  canceled is set to true if Ctrl+C was pressed, which also
    /// returns nothing: an empty line and end of input are different
    /// answers, and std::string cannot tell them apart on its own.
    std::optional<std::string> readLine(const std::string& prompt,
                                        bool* canceled = nullptr);

    /// True if stdin is an interactive terminal.
    bool isInteractive() const;

    /// Rows in the terminal, or 24 when it cannot be determined.
    /// Public because the pager lives in the output layer, not here: only
    /// that layer knows whether a given result is worth paging.
    int terminalHeight() const;

    /// Read a single keypress, without waiting for Enter.
    ///
    /// Enters raw mode for the duration and restores the terminal
    /// afterwards, so callers do not have to manage it. Returns -1 when
    /// stdin is not interactive; a pager must not block a piped run.
    int readKey();

private:
    // ---- Raw terminal mode ------------------------------------------
    bool   enterRawMode();
    void   exitRawMode();
    int    readByte();
    int    readEscapeSequence();

    // ---- Display ----------------------------------------------------
    void   refreshLine();
    void   clearScreen();
    int    terminalWidth() const;

    // ---- Line editing -----------------------------------------------
    void   insertChar(char ch);
    void   deleteCharBack();
    void   deleteCharForward();
    void   moveCursorLeft();
    void   moveCursorRight();
    void   moveToStart();
    void   moveToEnd();
    void   moveWordForward();
    void   moveWordBackward();
    void   killToEnd();
    void   killToStart();
    void   killWordForward();
    void   killWordBackward();
    void   yank();
    void   transposeChars();

    // ---- History navigation -----------------------------------------
    void   historyPrev();
    void   historyNext();
    void   startIncrementalSearch();

    // ---- Tab completion ---------------------------------------------
    void   handleTab();
    std::vector<std::string> completeFilenames(const std::string& prefix) const;
    std::vector<std::string> completeCommands(const std::string& prefix) const;

    // ---- Bang expansion ---------------------------------------------
    std::string expandBangs(const std::string& line) const;

    // ---- State ------------------------------------------------------
    CliHistory&  m_history;
    CliEngine*   m_engine = nullptr;
    std::vector<std::string> m_commands;

    std::string  m_line;             // Current edit buffer, UTF-8
    int          m_cursor = 0;       // Byte offset into m_line; the
                                     // movement helpers step whole
                                     // UTF-8 characters, never into
                                     // the middle of one
    std::string  m_prompt;           // Current prompt string
    std::string  m_killRing;         // Last killed text (for yank)

    int          m_historyIndex = -1; // -1 = current input
    std::string  m_savedLine;        // Saved input when browsing history

    bool         m_rawMode = false;
    bool         m_isTty   = false;

#if !defined(_WIN32)
    struct ::termios* m_origTermios = nullptr;
#endif
};

}  // namespace hobbycad

#endif  // HOBBYCAD_TERMINALINPUT_H

