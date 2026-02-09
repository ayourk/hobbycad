// =====================================================================
//  src/hobbycad/cli/climode.h — Command-line mode
// =====================================================================
//
//  Provides headless operation: single-command mode (convert, script)
//  and interactive REPL mode.  Uses libhobbycad directly with no
//  GUI dependencies.
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_CLIMODE_H
#define HOBBYCAD_CLIMODE_H

#include <QString>

#include "clihistory.h"
#include "cliengine.h"
#include "terminalinput.h"

namespace hobbycad {

class CliMode {
public:
    CliMode();
    ~CliMode();

    /// Convert a file from one format to another and exit.
    /// Returns 0 on success, 1 on failure.
    int runConvert(const QString& input, const QString& output);

    /// Run a Python script and exit.  (Stub for Phase 0.)
    /// Returns 0 on success, 1 on failure.
    int runScript(const QString& scriptPath);

    /// Run the interactive REPL.
    /// Returns 0 on normal exit.
    int runInteractive();

private:
    CliHistory     m_history;
    CliEngine      m_engine;
    TerminalInput  m_terminal;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_CLIMODE_H

