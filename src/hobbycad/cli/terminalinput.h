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

#include <QString>
#include <QStringList>

#if !defined(_WIN32)
  #include <termios.h>
#endif

namespace hobbycad {

class CliHistory;

class TerminalInput {
public:
    explicit TerminalInput(CliHistory& history);
    ~TerminalInput();

    /// Set the list of known commands for tab completion.
    void setCommands(const QStringList& commands);

    /// Read one line from the terminal with editing and history.
    /// Returns the entered text, or a null QString on EOF (Ctrl+D
    /// on empty line).  cancelled is set to true if Ctrl+C was
    /// pressed.
    QString readLine(const QString& prompt, bool* cancelled = nullptr);

    /// True if stdin is an interactive terminal.
    bool isInteractive() const;

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
    void   insertChar(QChar ch);
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
    QStringList completeFilenames(const QString& prefix) const;
    QStringList completeCommands(const QString& prefix) const;

    // ---- Bang expansion ---------------------------------------------
    QString expandBangs(const QString& line) const;

    // ---- State ------------------------------------------------------
    CliHistory&  m_history;
    QStringList  m_commands;

    QString      m_line;             // Current edit buffer
    int          m_cursor = 0;       // Cursor position in m_line
    QString      m_prompt;           // Current prompt string
    QString      m_killRing;         // Last killed text (for yank)

    int          m_historyIndex = -1; // -1 = current input
    QString      m_savedLine;         // Saved input when browsing history

    bool         m_rawMode = false;
    bool         m_isTty   = false;

#if !defined(_WIN32)
    struct ::termios* m_origTermios = nullptr;
#endif
};

}  // namespace hobbycad

#endif  // HOBBYCAD_TERMINALINPUT_H

