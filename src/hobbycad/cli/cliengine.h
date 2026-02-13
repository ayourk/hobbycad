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

    /// Get completion hints for a command's arguments.
    /// Returns possible completions or a hint message (prefixed with "?")
    /// for the current argument position.
    /// @param tokens The tokens entered so far (first token is the command)
    /// @param prefix The partial text of the current argument being typed
    /// @return List of completions, or single "?hint message" for help
    QStringList completeArguments(const QStringList& tokens,
                                   const QString& prefix) const;

    /// Build a prompt string showing the current directory.
    QString buildPrompt() const;

    /// Returns true if currently in sketch editing mode.
    bool inSketchMode() const;

    /// Returns the name of the current sketch (empty if not in sketch mode).
    QString currentSketchName() const;

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
    CliResult cmdSelect(const QStringList& args);
    CliResult cmdCreate(const QStringList& args);
    CliResult cmdFinish();
    CliResult cmdDiscard();

    // Sketch mode geometry commands
    CliResult cmdSketchPoint(const QStringList& args);
    CliResult cmdSketchLine(const QStringList& args);
    CliResult cmdSketchCircle(const QStringList& args);
    CliResult cmdSketchRectangle(const QStringList& args);
    CliResult cmdSketchArc(const QStringList& args);

    CliHistory& m_history;

    // Sketch mode state
    bool    m_inSketchMode = false;
    QString m_currentSketchName;
    int     m_sketchCounter = 0;  // For auto-naming sketches

    // TODO: These will come from the document later
    QStringList m_parameters = {
        QStringLiteral("width"),
        QStringLiteral("height"),
        QStringLiteral("depth"),
        QStringLiteral("radius"),
        QStringLiteral("diameter"),
        QStringLiteral("thickness"),
        QStringLiteral("offset"),
        QStringLiteral("spacing")
    };
};

}  // namespace hobbycad

#endif  // HOBBYCAD_CLIENGINE_H

