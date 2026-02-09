// =====================================================================
//  src/hobbycad/cli/clihistory.h — Command history for CLI mode
// =====================================================================
//
//  Manages a persistent command history file for the interactive
//  REPL.  Stores up to a configurable number of lines in:
//
//    Linux:   ~/.config/hobbycad/cli_history
//    macOS:   ~/Library/Application Support/hobbycad/cli_history
//    Windows: %APPDATA%/hobbycad/cli_history
//
//  The maximum number of stored lines defaults to 500 and can be
//  changed at runtime or via the REPL "history" command.
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_CLIHISTORY_H
#define HOBBYCAD_CLIHISTORY_H

#include <QString>
#include <QStringList>

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
    const QStringList& entries() const;

    /// Number of entries.
    int count() const;

    /// Add a command to the history.  Duplicates of the most recent
    /// entry are suppressed (consecutive dedup).
    void append(const QString& command);

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
    QString filePath() const;

private:
    void trim();

    QStringList m_entries;
    int         m_maxLines;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_CLIHISTORY_H

