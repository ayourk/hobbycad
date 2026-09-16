// =====================================================================
//  src/hobbycad/cli/clihistory.h — Command history for CLI mode
// =====================================================================
//
//  Manages a persistent command history file for the interactive
//  REPL.  Stores up to a configurable number of lines in:
//
//    Linux:   $XDG_CONFIG_HOME/hobbycad/cli_history, or
//             ~/.config/hobbycad/cli_history
//    macOS:   ~/Library/Preferences/hobbycad/cli_history
//    Windows: %LOCALAPPDATA%/hobbycad/cli_history
//
//  Those are the directories Qt's QStandardPaths GenericConfigLocation
//  returned when this class used it, reproduced exactly so an existing
//  history file is still found now that the command layer is Qt-free.
//  (The list above used to name Application Support and %APPDATA%; the
//  code never wrote there.)
//
//  The maximum number of stored lines defaults to 500 and can be
//  changed at runtime or via the REPL "history" command.
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_CLIHISTORY_H
#define HOBBYCAD_CLIHISTORY_H

#include <string>
#include <vector>

namespace hobbycad {

class CliHistory {
public:
    /// Default maximum number of history lines.
    static constexpr int DefaultMaxLines = 500;

    explicit CliHistory(int maxLines = DefaultMaxLines);
    ~CliHistory();

    // ---- Configuration ----------------------------------------------

    /// Current maximum number of stored lines.
    int maxLines() const;

    /// Change the maximum.  If the current history exceeds the new
    /// limit, the oldest entries are discarded.
    void setMaxLines(int maxLines);

    // ---- History access ---------------------------------------------

    /// All entries, oldest first.
    const std::vector<std::string>& entries() const;

    /// Number of entries.
    int count() const;

    /// Add a command to the history.  Duplicates of the most recent
    /// entry are suppressed (consecutive dedup).
    void append(const std::string& command);

    /// Clear all entries (does not delete the file until save).
    void clear();

    // ---- Persistence ------------------------------------------------

    /// Load history from the default file.  Returns true on success
    /// or if the file does not yet exist.
    bool load();

    /// Save history to the default file.  Creates the directory if
    /// needed.  Returns true on success.
    bool save() const;

    /// Full path to the history file.
    std::string filePath() const;

private:
    void trim();

    std::vector<std::string> m_entries;
    int         m_maxLines;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_CLIHISTORY_H

