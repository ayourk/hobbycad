// =====================================================================
//  src/hobbycad/cli/climode.cpp — Command-line mode
// =====================================================================
//
//  The standalone CLI REPL.  Delegates command dispatch to CliEngine
//  and uses TerminalInput for zsh-style line editing.
//
// =====================================================================

#include "climode.h"

#include <hobbycad/core.h>
#include <hobbycad/brep_io.h>

#include <QFileInfo>

#include <iostream>

namespace hobbycad {

CliMode::CliMode()
    : m_engine(m_history)
    , m_terminal(m_history)
{
    m_history.load();
    m_terminal.setCommands(m_engine.commandNames());
    m_terminal.setEngine(&m_engine);
}

CliMode::~CliMode()
{
    m_history.save();
}

// ---- Single-command: convert ----------------------------------------

int CliMode::runConvert(const QString& input, const QString& output)
{
    std::cout << "Converting: " << input.toStdString()
              << " -> " << output.toStdString() << std::endl;

    // Phase 0: only BREP-to-BREP copy is supported.
    QString inputExt  = QFileInfo(input).suffix().toLower();
    QString outputExt = QFileInfo(output).suffix().toLower();

    if (inputExt != QLatin1String("brep") &&
        inputExt != QLatin1String("brp")) {
        std::cerr << "Error: Phase 0 only supports BREP input files."
                  << std::endl;
        std::cerr << "  Supported extensions: .brep, .brp" << std::endl;
        return 1;
    }

    if (outputExt != QLatin1String("brep") &&
        outputExt != QLatin1String("brp")) {
        std::cerr << "Error: Phase 0 only supports BREP output files."
                  << std::endl;
        std::cerr << "  STEP/STL/IGES export will be available in "
                     "future phases." << std::endl;
        return 1;
    }

    QString errorMsg;
    auto shapes = brep_io::readBrep(input, &errorMsg);
    if (shapes.isEmpty()) {
        std::cerr << "Error reading input: "
                  << errorMsg.toStdString() << std::endl;
        return 1;
    }

    if (!brep_io::writeBrep(output, shapes, &errorMsg)) {
        std::cerr << "Error writing output: "
                  << errorMsg.toStdString() << std::endl;
        return 1;
    }

    std::cout << "Done. Wrote " << shapes.size() << " shape(s)."
              << std::endl;
    return 0;
}

// ---- Single-command: script -----------------------------------------

int CliMode::runScript(const QString& scriptPath)
{
    std::cerr << "Error: Python scripting is not yet available "
                 "(planned for Phase 3)." << std::endl;
    std::cerr << "  Script: " << scriptPath.toStdString() << std::endl;
    return 1;
}

// ---- Interactive REPL -----------------------------------------------

int CliMode::runInteractive()
{
    std::cout << "HobbyCAD " << hobbycad::version()
              << " — Command-Line Mode" << std::endl;
    std::cout << "Type 'help' for available commands, "
                 "or 'exit' to quit." << std::endl;

    if (m_terminal.isInteractive()) {
        std::cout << "Line editing active (Ctrl+R search, Tab "
                     "completion, Up/Down history)." << std::endl;
    }

    std::cout << "History: " << m_history.count() << " entries loaded from "
              << m_history.filePath().toStdString() << std::endl;
    std::cout << std::endl;

    while (true) {
        QString prompt = m_engine.buildPrompt();
        bool cancelled = false;
        QString line = m_terminal.readLine(prompt, &cancelled);

        if (line.isNull() && !cancelled) {
            // EOF
            std::cout << std::endl;
            break;
        }

        if (cancelled) {
            continue;  // Ctrl+C — discard line, show new prompt
        }

        QString cmd = line.trimmed();
        if (cmd.isEmpty()) continue;

        m_history.append(cmd);

        CliResult result = m_engine.execute(cmd);

        // Update available commands (may change with context, e.g., sketch mode)
        m_terminal.setCommands(m_engine.commandNames());

        if (!result.output.isEmpty()) {
            std::cout << result.output.toStdString() << std::endl;
        }
        if (!result.error.isEmpty()) {
            std::cerr << result.error.toStdString() << std::endl;
        }
        if (result.requestExit) {
            break;
        }
    }

    m_history.save();
    return 0;
}

}  // namespace hobbycad

