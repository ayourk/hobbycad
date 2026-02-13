// =====================================================================
//  src/hobbycad/cli/terminalinput.cpp — zsh-style terminal line editor
// =====================================================================

#include "terminalinput.h"
#include "clihistory.h"
#include "cliengine.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>

#include <iostream>
#include <cstdio>

#if defined(_WIN32)
  #include <windows.h>
  #include <io.h>
  #define STDIN_FD  0
  #define STDOUT_FD 1
#else
  #include <termios.h>
  #include <unistd.h>
  #include <sys/ioctl.h>
  #include <csignal>
  #define STDIN_FD  STDIN_FILENO
  #define STDOUT_FD STDOUT_FILENO
#endif

namespace hobbycad {

// ---- Key codes / escape identifiers ---------------------------------

enum Key {
    KEY_NULL      = 0,
    KEY_CTRL_A    = 1,
    KEY_CTRL_B    = 2,
    KEY_CTRL_C    = 3,
    KEY_CTRL_D    = 4,
    KEY_CTRL_E    = 5,
    KEY_CTRL_F    = 6,
    KEY_CTRL_K    = 11,
    KEY_CTRL_L    = 12,
    KEY_ENTER     = 13,
    KEY_CTRL_N    = 14,
    KEY_CTRL_P    = 16,
    KEY_CTRL_R    = 18,
    KEY_CTRL_T    = 20,
    KEY_CTRL_U    = 21,
    KEY_CTRL_W    = 23,
    KEY_CTRL_Y    = 25,
    KEY_ESC       = 27,
    KEY_BACKSPACE = 127,
    // Virtual keys from escape sequences
    KEY_ARROW_UP    = 1000,
    KEY_ARROW_DOWN,
    KEY_ARROW_RIGHT,
    KEY_ARROW_LEFT,
    KEY_HOME,
    KEY_END,
    KEY_DELETE,
    KEY_ALT_B,
    KEY_ALT_D,
    KEY_ALT_F,
    KEY_TAB = 9,
};

// =====================================================================
//  Construction / destruction
// =====================================================================

TerminalInput::TerminalInput(CliHistory& history)
    : m_history(history)
{
#if !defined(_WIN32)
    m_isTty = ::isatty(STDIN_FD);
    m_origTermios = new struct ::termios;
#else
    m_isTty = _isatty(STDIN_FD);
#endif
}

TerminalInput::~TerminalInput()
{
    exitRawMode();
#if !defined(_WIN32)
    delete m_origTermios;
#endif
}

bool TerminalInput::isInteractive() const
{
    return m_isTty;
}

void TerminalInput::setCommands(const QStringList& commands)
{
    m_commands = commands;
}

void TerminalInput::setEngine(CliEngine* engine)
{
    m_engine = engine;
}

// =====================================================================
//  Raw terminal mode (POSIX)
// =====================================================================

#if !defined(_WIN32)

bool TerminalInput::enterRawMode()
{
    if (m_rawMode || !m_isTty) return false;

    if (::tcgetattr(STDIN_FD, m_origTermios) == -1) return false;

    struct ::termios raw = *m_origTermios;

    // Input: no break processing, no CR-to-NL, no parity, no strip,
    // no start/stop flow control
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);

    // Output: keep default (processed output for \n -> \r\n)
    raw.c_oflag |= (OPOST);

    // Control: 8-bit characters
    raw.c_cflag |= (CS8);

    // Local: no echo, no canonical mode, no signals, no extended
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

    // Read returns after 1 byte, no timeout
    raw.c_cc[VMIN]  = 1;
    raw.c_cc[VTIME] = 0;

    if (::tcsetattr(STDIN_FD, TCSAFLUSH, &raw) == -1) return false;

    m_rawMode = true;
    return true;
}

void TerminalInput::exitRawMode()
{
    if (m_rawMode && m_isTty) {
        ::tcsetattr(STDIN_FD, TCSAFLUSH, m_origTermios);
        m_rawMode = false;
    }
}

int TerminalInput::readByte()
{
    unsigned char c;
    ssize_t n = ::read(STDIN_FD, &c, 1);
    return (n <= 0) ? -1 : static_cast<int>(c);
}

#else  // Windows

bool TerminalInput::enterRawMode()
{
    if (m_rawMode || !m_isTty) return false;

    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;
    GetConsoleMode(hIn, &mode);
    mode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT);
    mode |= ENABLE_VIRTUAL_TERMINAL_INPUT;
    SetConsoleMode(hIn, mode);

    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD outMode;
    GetConsoleMode(hOut, &outMode);
    outMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, outMode);

    m_rawMode = true;
    return true;
}

void TerminalInput::exitRawMode()
{
    if (m_rawMode && m_isTty) {
        HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
        DWORD mode;
        GetConsoleMode(hIn, &mode);
        mode |= (ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT);
        SetConsoleMode(hIn, mode);
        m_rawMode = false;
    }
}

int TerminalInput::readByte()
{
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    char c;
    DWORD read;
    if (!ReadFile(hIn, &c, 1, &read, nullptr) || read == 0) return -1;
    return static_cast<int>(static_cast<unsigned char>(c));
}

#endif  // _WIN32

// =====================================================================
//  Escape sequence parser
// =====================================================================

int TerminalInput::readEscapeSequence()
{
    int seq1 = readByte();
    if (seq1 == -1) return KEY_ESC;

    if (seq1 == '[') {
        // CSI sequence: ESC [ ...
        int seq2 = readByte();
        if (seq2 == -1) return KEY_ESC;

        if (seq2 >= '0' && seq2 <= '9') {
            int seq3 = readByte();
            if (seq3 == '~') {
                switch (seq2) {
                    case '1': return KEY_HOME;
                    case '3': return KEY_DELETE;
                    case '4': return KEY_END;
                    case '7': return KEY_HOME;
                    case '8': return KEY_END;
                }
            }
            return KEY_ESC;  // Unknown extended sequence
        }

        switch (seq2) {
            case 'A': return KEY_ARROW_UP;
            case 'B': return KEY_ARROW_DOWN;
            case 'C': return KEY_ARROW_RIGHT;
            case 'D': return KEY_ARROW_LEFT;
            case 'H': return KEY_HOME;
            case 'F': return KEY_END;
        }
        return KEY_ESC;  // Unknown CSI sequence
    }

    if (seq1 == 'O') {
        // SS3 sequence: ESC O ...
        int seq2 = readByte();
        switch (seq2) {
            case 'H': return KEY_HOME;
            case 'F': return KEY_END;
        }
        return KEY_ESC;
    }

    // Alt+key: ESC followed by a printable character
    switch (seq1) {
        case 'b': return KEY_ALT_B;
        case 'f': return KEY_ALT_F;
        case 'd': return KEY_ALT_D;
    }

    return KEY_ESC;
}

// =====================================================================
//  Display
// =====================================================================

int TerminalInput::terminalWidth() const
{
#if defined(_WIN32)
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        return csbi.srWindow.Right - csbi.srWindow.Left + 1;
    }
    return 80;
#else
    struct winsize ws;
    if (::ioctl(STDOUT_FD, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        return ws.ws_col;
    }
    return 80;
#endif
}

void TerminalInput::refreshLine()
{
    // Move cursor to column 0, write prompt + buffer, clear rest,
    // reposition cursor
    std::string buf;
    buf += '\r';                                  // Carriage return
    buf += m_prompt.toStdString();                // Prompt
    buf += m_line.toStdString();                  // Buffer
    buf += "\033[0K";                             // Clear to end of line

    // Reposition cursor: carriage return + forward to prompt + cursor
    buf += '\r';
    int cursorCol = m_prompt.length() + m_cursor;
    if (cursorCol > 0) {
        buf += "\033[" + std::to_string(cursorCol) + "C";
    }

    ::write(STDOUT_FD, buf.c_str(), buf.size());
}

void TerminalInput::clearScreen()
{
    // ANSI: clear screen, move cursor to top-left
    const char* seq = "\033[H\033[2J";
    ::write(STDOUT_FD, seq, 7);
}

// =====================================================================
//  Line editing
// =====================================================================

void TerminalInput::insertChar(QChar ch)
{
    m_line.insert(m_cursor, ch);
    m_cursor++;
}

void TerminalInput::deleteCharBack()
{
    if (m_cursor > 0) {
        m_line.remove(m_cursor - 1, 1);
        m_cursor--;
    }
}

void TerminalInput::deleteCharForward()
{
    if (m_cursor < m_line.length()) {
        m_line.remove(m_cursor, 1);
    }
}

void TerminalInput::moveCursorLeft()
{
    if (m_cursor > 0) m_cursor--;
}

void TerminalInput::moveCursorRight()
{
    if (m_cursor < m_line.length()) m_cursor++;
}

void TerminalInput::moveToStart()
{
    m_cursor = 0;
}

void TerminalInput::moveToEnd()
{
    m_cursor = m_line.length();
}

void TerminalInput::moveWordForward()
{
    int len = m_line.length();
    // Skip current word characters
    while (m_cursor < len && !m_line[m_cursor].isSpace()) m_cursor++;
    // Skip whitespace
    while (m_cursor < len && m_line[m_cursor].isSpace()) m_cursor++;
}

void TerminalInput::moveWordBackward()
{
    // Skip whitespace behind cursor
    while (m_cursor > 0 && m_line[m_cursor - 1].isSpace()) m_cursor--;
    // Skip word characters
    while (m_cursor > 0 && !m_line[m_cursor - 1].isSpace()) m_cursor--;
}

void TerminalInput::killToEnd()
{
    if (m_cursor < m_line.length()) {
        m_killRing = m_line.mid(m_cursor);
        m_line.truncate(m_cursor);
    }
}

void TerminalInput::killToStart()
{
    if (m_cursor > 0) {
        m_killRing = m_line.left(m_cursor);
        m_line.remove(0, m_cursor);
        m_cursor = 0;
    }
}

void TerminalInput::killWordForward()
{
    int start = m_cursor;
    int len = m_line.length();
    int end = m_cursor;
    // Skip whitespace
    while (end < len && m_line[end].isSpace()) end++;
    // Skip word
    while (end < len && !m_line[end].isSpace()) end++;
    if (end > start) {
        m_killRing = m_line.mid(start, end - start);
        m_line.remove(start, end - start);
    }
}

void TerminalInput::killWordBackward()
{
    int end = m_cursor;
    int start = m_cursor;
    // Skip whitespace behind cursor
    while (start > 0 && m_line[start - 1].isSpace()) start--;
    // Skip word characters
    while (start > 0 && !m_line[start - 1].isSpace()) start--;
    if (start < end) {
        m_killRing = m_line.mid(start, end - start);
        m_line.remove(start, end - start);
        m_cursor = start;
    }
}

void TerminalInput::yank()
{
    if (!m_killRing.isEmpty()) {
        m_line.insert(m_cursor, m_killRing);
        m_cursor += m_killRing.length();
    }
}

void TerminalInput::transposeChars()
{
    if (m_cursor > 0 && m_line.length() >= 2) {
        // If at end, transpose the two characters before cursor
        // Otherwise, transpose char at cursor with char before it
        int pos = (m_cursor == m_line.length()) ? m_cursor - 1 : m_cursor;
        if (pos > 0) {
            QChar tmp = m_line[pos];
            m_line[pos] = m_line[pos - 1];
            m_line[pos - 1] = tmp;
            if (m_cursor < m_line.length()) m_cursor++;
        }
    }
}

// =====================================================================
//  History navigation
// =====================================================================

void TerminalInput::historyPrev()
{
    const auto& entries = m_history.entries();
    if (entries.isEmpty()) return;

    if (m_historyIndex == -1) {
        // First time pressing up: save current input
        m_savedLine = m_line;
        m_historyIndex = entries.size() - 1;
    } else if (m_historyIndex > 0) {
        m_historyIndex--;
    } else {
        return;  // Already at oldest entry
    }

    m_line = entries[m_historyIndex];
    m_cursor = m_line.length();
}

void TerminalInput::historyNext()
{
    if (m_historyIndex == -1) return;  // Not browsing history

    const auto& entries = m_history.entries();

    if (m_historyIndex < entries.size() - 1) {
        m_historyIndex++;
        m_line = entries[m_historyIndex];
    } else {
        // Past the newest entry: restore saved input
        m_historyIndex = -1;
        m_line = m_savedLine;
    }

    m_cursor = m_line.length();
}

void TerminalInput::startIncrementalSearch()
{
    const auto& entries = m_history.entries();
    if (entries.isEmpty()) return;

    QString searchTerm;
    int matchIndex = -1;
    QString origLine = m_line;
    int origCursor = m_cursor;

    while (true) {
        // Display search prompt
        std::string display;
        display += '\r';
        display += "(reverse-i-search)`";
        display += searchTerm.toStdString();
        display += "': ";
        if (matchIndex >= 0 && matchIndex < entries.size()) {
            display += entries[matchIndex].toStdString();
        }
        display += "\033[0K";  // Clear to end of line

        // Position cursor after search term in the prompt
        display += '\r';
        int cursorPos = 20 + searchTerm.length();  // After the ': part
        display += "\033[" + std::to_string(cursorPos) + "C";

        ::write(STDOUT_FD, display.c_str(), display.size());

        int ch = readByte();
        if (ch == -1 || ch == KEY_ESC || ch == KEY_CTRL_C) {
            // Cancel search, restore original line
            m_line = origLine;
            m_cursor = origCursor;
            break;
        }

        if (ch == KEY_ENTER || ch == '\n') {
            // Accept the found entry
            if (matchIndex >= 0 && matchIndex < entries.size()) {
                m_line = entries[matchIndex];
                m_cursor = m_line.length();
                m_historyIndex = matchIndex;
            }
            break;
        }

        if (ch == KEY_CTRL_R) {
            // Search further back
            if (matchIndex > 0) {
                for (int i = matchIndex - 1; i >= 0; --i) {
                    if (entries[i].contains(searchTerm,
                            Qt::CaseInsensitive)) {
                        matchIndex = i;
                        break;
                    }
                }
            }
            continue;
        }

        if (ch == KEY_BACKSPACE || ch == 8) {
            if (!searchTerm.isEmpty()) {
                searchTerm.chop(1);
            }
        } else if (ch >= 32 && ch < 127) {
            searchTerm += QChar(ch);
        } else {
            continue;  // Ignore other control characters
        }

        // Search for the term
        matchIndex = -1;
        if (!searchTerm.isEmpty()) {
            for (int i = entries.size() - 1; i >= 0; --i) {
                if (entries[i].contains(searchTerm,
                        Qt::CaseInsensitive)) {
                    matchIndex = i;
                    break;
                }
            }
        }
    }
}

// =====================================================================
//  Tab completion
// =====================================================================

void TerminalInput::handleTab()
{
    // Determine what we're completing
    QString beforeCursor = m_line.left(m_cursor);
    QStringList tokens = beforeCursor.split(
        QChar(' '), Qt::SkipEmptyParts);

    QStringList completions;
    QString prefix;
    bool isArgumentCompletion = false;

    if (tokens.isEmpty() || (beforeCursor.endsWith(' ') && !tokens.isEmpty())) {
        // Completing a new token at the start of the next word
        if (beforeCursor.trimmed().isEmpty()) {
            // Complete commands
            prefix = QString();
            completions = completeCommands(prefix);
        } else {
            // After a command, try argument completion first
            prefix = QString();
            isArgumentCompletion = true;
            if (m_engine) {
                completions = m_engine->completeArguments(tokens, prefix);
            }
            // If no argument completions, try filenames
            if (completions.isEmpty()) {
                completions = completeFilenames(prefix);
                isArgumentCompletion = false;
            }
        }
    } else if (tokens.size() == 1 && !beforeCursor.endsWith(' ')) {
        // First token, not finished: complete commands
        prefix = tokens.last();
        completions = completeCommands(prefix);
        // Also try filenames in case it's a path
        completions += completeFilenames(prefix);
        completions.removeDuplicates();
    } else {
        // Subsequent token: try argument completion first
        prefix = tokens.last();
        isArgumentCompletion = true;
        if (m_engine) {
            // Pass all but the last token (the prefix being completed)
            QStringList prevTokens = tokens.mid(0, tokens.size() - 1);
            completions = m_engine->completeArguments(prevTokens, prefix);
        }
        // If no argument completions, try filenames
        if (completions.isEmpty()) {
            completions = completeFilenames(prefix);
            isArgumentCompletion = false;
        }
    }

    // Check if we got a hint message (starts with '?')
    if (completions.size() == 1 && completions.first().startsWith('?')) {
        // Display the hint message Cisco-style
        QString hint = completions.first().mid(1);  // Remove the '?' prefix
        std::string display = "\r\n  ";
        display += hint.toStdString();
        display += "\r\n";
        ::write(STDOUT_FD, display.c_str(), display.size());
        // Prompt and line will be redrawn by refreshLine
        return;
    }

    if (completions.isEmpty()) {
        // No matches — beep
        const char beep = '\a';
        ::write(STDOUT_FD, &beep, 1);
        return;
    }

    if (completions.size() == 1) {
        // Unique match — insert the remaining characters
        QString completion = completions.first();
        QString suffix = completion.mid(prefix.length());

        // If it's a directory, append separator; otherwise space
        QFileInfo fi(completion);
        if (fi.isDir()) {
            suffix += QDir::separator();
        } else {
            suffix += QChar(' ');
        }

        m_line.insert(m_cursor, suffix);
        m_cursor += suffix.length();
        return;
    }

    // Multiple matches — find the longest common prefix
    QString common = completions.first();
    for (int i = 1; i < completions.size(); ++i) {
        int len = qMin(common.length(), completions[i].length());
        int j = 0;
        while (j < len && common[j] == completions[i][j]) j++;
        common.truncate(j);
    }

    if (common.length() > prefix.length()) {
        // Can extend the input with the common prefix
        QString suffix = common.mid(prefix.length());
        m_line.insert(m_cursor, suffix);
        m_cursor += suffix.length();
    } else {
        // Show all matches (like zsh)
        std::string display = "\r\n";
        for (const auto& c : completions) {
            display += c.toStdString();
            display += "  ";
        }
        display += "\r\n";
        ::write(STDOUT_FD, display.c_str(), display.size());
        // Prompt and line will be redrawn by refreshLine
    }
}

QStringList TerminalInput::completeFilenames(const QString& prefix) const
{
    QStringList results;

    QString dir;
    QString base;

    if (prefix.contains(QDir::separator()) || prefix.contains('/')) {
        QFileInfo fi(prefix);
        dir  = fi.absolutePath();
        base = fi.fileName();
    } else {
        dir  = QDir::currentPath();
        base = prefix;
    }

    QDir d(dir);
    if (!d.exists()) return results;

    QStringList entries = d.entryList(
        QDir::AllEntries | QDir::NoDotAndDotDot);

    for (const auto& entry : entries) {
        if (entry.startsWith(base, Qt::CaseSensitive)) {
            if (prefix.contains(QDir::separator()) || prefix.contains('/')) {
                // Preserve the path prefix the user typed
                QFileInfo fi(prefix);
                results.append(fi.absolutePath() + QDir::separator() + entry);
            } else {
                results.append(entry);
            }
        }
    }

    results.sort();
    return results;
}

QStringList TerminalInput::completeCommands(const QString& prefix) const
{
    QStringList results;
    for (const auto& cmd : m_commands) {
        if (cmd.startsWith(prefix, Qt::CaseInsensitive)) {
            results.append(cmd);
        }
    }
    results.sort();
    return results;
}

// =====================================================================
//  Bang expansion
// =====================================================================

QString TerminalInput::expandBangs(const QString& line) const
{
    const auto& entries = m_history.entries();
    if (entries.isEmpty()) return line;

    QString result = line;

    // !! — repeat last command
    if (result.contains(QLatin1String("!!"))) {
        result.replace(QLatin1String("!!"), entries.last());
    }

    // !<prefix> — most recent command starting with <prefix>
    // !<n> — command at history index n
    // Process from right to left to preserve indices
    int i = result.length() - 1;
    while (i >= 0) {
        if (result[i] == '!' && i + 1 < result.length()) {
            // Don't expand if preceded by backslash
            if (i > 0 && result[i - 1] == '\\') {
                // Remove the backslash (literal !)
                result.remove(i - 1, 1);
                i -= 2;
                continue;
            }

            QChar next = result[i + 1];
            if (next == '!') {
                // Already handled above
                i--;
                continue;
            }

            if (next.isDigit()) {
                // !n — command at index n (1-based)
                int numStart = i + 1;
                int numEnd = numStart;
                while (numEnd < result.length() && result[numEnd].isDigit())
                    numEnd++;
                bool ok = false;
                int idx = result.mid(numStart, numEnd - numStart).toInt(&ok);
                if (ok && idx >= 1 && idx <= entries.size()) {
                    result.replace(i, numEnd - i, entries[idx - 1]);
                }
                i--;
                continue;
            }

            if (next.isLetter()) {
                // !prefix — most recent command starting with prefix
                int prefixStart = i + 1;
                int prefixEnd = prefixStart;
                while (prefixEnd < result.length()
                       && !result[prefixEnd].isSpace())
                    prefixEnd++;
                QString pfx = result.mid(prefixStart, prefixEnd - prefixStart);

                // Search history backward for a match
                for (int j = entries.size() - 1; j >= 0; --j) {
                    if (entries[j].startsWith(pfx)) {
                        result.replace(i, prefixEnd - i, entries[j]);
                        break;
                    }
                }
                i--;
                continue;
            }
        }
        i--;
    }

    return result;
}

// =====================================================================
//  Main readLine loop
// =====================================================================

QString TerminalInput::readLine(const QString& prompt, bool* cancelled)
{
    if (cancelled) *cancelled = false;

    // Non-interactive: fall back to std::getline
    if (!m_isTty) {
        std::string line;
        if (!std::getline(std::cin, line)) {
            return QString();  // EOF
        }
        return QString::fromStdString(line);
    }

    // Enter raw mode
    if (!enterRawMode()) {
        // Raw mode failed — fall back
        std::string line;
        std::cout << prompt.toStdString() << std::flush;
        if (!std::getline(std::cin, line)) {
            return QString();
        }
        return QString::fromStdString(line);
    }

    m_prompt = prompt;
    m_line.clear();
    m_cursor = 0;
    m_historyIndex = -1;
    m_savedLine.clear();

    refreshLine();

    while (true) {
        int ch = readByte();
        if (ch == -1) {
            // EOF
            exitRawMode();
            return QString();
        }

        switch (ch) {
        case KEY_ENTER:
        case '\n': {
            // Accept line
            // Move cursor to end, print newline
            moveToEnd();
            refreshLine();
            const char nl = '\n';
            ::write(STDOUT_FD, &nl, 1);
            exitRawMode();

            // Expand bangs before returning
            QString result = m_line;
            if (result.contains('!') && !m_history.entries().isEmpty()) {
                QString expanded = expandBangs(result);
                if (expanded != result) {
                    // Print the expanded form like zsh does
                    std::cout << expanded.toStdString() << std::endl;
                    result = expanded;
                }
            }
            return result;
        }

        case KEY_CTRL_C:
            // Cancel current line
            m_line.clear();
            m_cursor = 0;
            {
                const char* msg = "^C\n";
                ::write(STDOUT_FD, msg, 3);
            }
            exitRawMode();
            if (cancelled) *cancelled = true;
            return QString();

        case KEY_CTRL_D:
            if (m_line.isEmpty()) {
                // EOF on empty line
                exitRawMode();
                return QString();  // null QString = EOF
            }
            deleteCharForward();
            refreshLine();
            break;

        case KEY_BACKSPACE:
        case 8:  // Some terminals send 8 for backspace
            deleteCharBack();
            refreshLine();
            break;

        case KEY_CTRL_A:
            moveToStart();
            refreshLine();
            break;

        case KEY_CTRL_E:
            moveToEnd();
            refreshLine();
            break;

        case KEY_CTRL_B:
            moveCursorLeft();
            refreshLine();
            break;

        case KEY_CTRL_F:
            moveCursorRight();
            refreshLine();
            break;

        case KEY_CTRL_K:
            killToEnd();
            refreshLine();
            break;

        case KEY_CTRL_U:
            killToStart();
            refreshLine();
            break;

        case KEY_CTRL_W:
            killWordBackward();
            refreshLine();
            break;

        case KEY_CTRL_Y:
            yank();
            refreshLine();
            break;

        case KEY_CTRL_T:
            transposeChars();
            refreshLine();
            break;

        case KEY_CTRL_P:
            historyPrev();
            refreshLine();
            break;

        case KEY_CTRL_N:
            historyNext();
            refreshLine();
            break;

        case KEY_CTRL_R:
            startIncrementalSearch();
            refreshLine();
            break;

        case KEY_CTRL_L:
            clearScreen();
            refreshLine();
            break;

        case KEY_TAB:
            handleTab();
            refreshLine();
            break;

        case KEY_ESC: {
            int key = readEscapeSequence();
            switch (key) {
                case KEY_ARROW_UP:    historyPrev();     break;
                case KEY_ARROW_DOWN:  historyNext();     break;
                case KEY_ARROW_LEFT:  moveCursorLeft();  break;
                case KEY_ARROW_RIGHT: moveCursorRight(); break;
                case KEY_HOME:        moveToStart();     break;
                case KEY_END:         moveToEnd();       break;
                case KEY_DELETE:      deleteCharForward(); break;
                case KEY_ALT_B:       moveWordBackward(); break;
                case KEY_ALT_F:       moveWordForward();  break;
                case KEY_ALT_D:       killWordForward();  break;
                default: break;  // Unknown escape — ignore
            }
            refreshLine();
            break;
        }

        default:
            // Printable character
            if (ch >= 32 && ch < 127) {
                // '?' at end of line triggers help (like Tab)
                if (ch == '?' && m_cursor == m_line.length()) {
                    handleTab();
                    refreshLine();
                } else {
                    insertChar(QChar(ch));
                    refreshLine();
                }
            }
            // TODO: UTF-8 multi-byte handling for characters > 127
            break;
        }
    }
}

}  // namespace hobbycad

