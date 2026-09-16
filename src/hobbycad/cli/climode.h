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

#include <string>

#include "clihistory.h"
#include "cliengine.h"
#include "headlesshost.h"
#include "terminalinput.h"

namespace hobbycad {

class CliMode {
public:
    CliMode();
    ~CliMode();

    /// Convert a file from one format to another and exit.
    /// Returns 0 on success, 1 on failure.
    int runConvert(const std::string& input, const std::string& output);

    /// Run a script file and exit.
    /// @param scriptPath Path to script file, "-" for stdin, or empty for stdin
    /// @param checkOnly If true, only validate syntax without executing
    /// Returns 0 on success, 1 on failure.
    int runScript(const std::string& scriptPath, bool checkOnly = false);

    /// Run the interactive REPL.
    /// Returns 0 on normal exit.
    int runInteractive();

private:
    CliHistory     m_history;
    /// The document commands act on when there is no GUI. Declared BEFORE
    /// the engine so it outlives the pointer the engine holds to it.
    HeadlessDocumentHost m_docHost;
    CliEngine      m_engine;
    TerminalInput  m_terminal;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_CLIMODE_H

