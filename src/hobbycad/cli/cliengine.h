// =====================================================================
//  src/hobbycad/cli/cliengine.h — Shared command dispatch engine
// =====================================================================
//
//  Provides command parsing and execution used by both the standalone
//  CLI REPL (CliMode) and the embedded GUI terminal panel (CliPanel).
//
//  All output is returned as QString rather than printed to stdout,
//  so callers can direct it wherever they need (terminal, QTextEdit,
//  log file, etc.).
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_CLIENGINE_H
#define HOBBYCAD_CLIENGINE_H

#include <QString>
#include <QStringList>

namespace hobbycad {

class CliHistory;

/// Result of executing a command.
struct CliResult {
    int     exitCode = 0;    ///< 0 = success, non-zero = error
    QString output;          ///< Normal output text
    QString error;           ///< Error output text (if any)
    bool    requestExit = false;  ///< True if exit/quit was entered
};

class CliEngine {
public:
    explicit CliEngine(CliHistory& history);
    ~CliEngine();

    /// Execute a single command line.  Returns result with output.
    CliResult execute(const QString& line);

    /// Get the list of known command names (for tab completion).
    QStringList commandNames() const;

    /// Build a prompt string showing the current directory.
    QString buildPrompt() const;

private:
    CliResult cmdHelp() const;
    CliResult cmdVersion() const;
    CliResult cmdNew();
    CliResult cmdOpen(const QStringList& args);
    CliResult cmdSave(const QStringList& args);
    CliResult cmdCd(const QStringList& args);
    CliResult cmdPwd() const;
    CliResult cmdInfo() const;
    CliResult cmdHistory(const QStringList& args);

    CliHistory& m_history;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_CLIENGINE_H

