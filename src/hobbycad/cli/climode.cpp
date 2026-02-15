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

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>

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

int CliMode::runScript(const QString& scriptPath, bool checkOnly)
{
    // Support reading from stdin: "hobbycad script -" or "hobbycad script"
    // Also works with: cat script.txt | hobbycad script -
    bool readFromStdin = scriptPath.isEmpty() || scriptPath == QLatin1String("-");

    QFile file;
    QTextStream in;

    if (readFromStdin) {
        // Read from stdin
        if (!file.open(stdin, QIODevice::ReadOnly | QIODevice::Text)) {
            std::cerr << "Error: Could not open stdin for reading."
                      << std::endl;
            return 1;
        }
        in.setDevice(&file);
        std::cerr << "Reading script from stdin..." << std::endl;
    } else {
        // Read from file
        file.setFileName(scriptPath);

        if (!file.exists()) {
            std::cerr << "Error: Script file not found: "
                      << scriptPath.toStdString() << std::endl;
            return 1;
        }

        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            std::cerr << "Error: Could not open script file: "
                      << file.errorString().toStdString() << std::endl;
            return 1;
        }
        in.setDevice(&file);

        if (checkOnly) {
            std::cout << "Checking script: " << scriptPath.toStdString() << std::endl;
        } else {
            std::cout << "Running script: " << scriptPath.toStdString() << std::endl;
        }
    }

    int lineNum = 0;
    int commandCount = 0;
    int errorCount = 0;
    QStringList validCmds = m_engine.commandNames();

    // Also add sketch commands for syntax checking
    validCmds << QStringLiteral("point") << QStringLiteral("line")
              << QStringLiteral("circle") << QStringLiteral("rectangle")
              << QStringLiteral("arc") << QStringLiteral("finish")
              << QStringLiteral("discard");

    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        lineNum++;

        // Skip empty lines and comments
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }

        commandCount++;

        if (checkOnly) {
            // Syntax check only - validate command name exists
            QStringList tokens = line.split(QRegularExpression(QStringLiteral("\\s+")),
                                            Qt::SkipEmptyParts);
            if (tokens.isEmpty()) continue;

            QString cmd = tokens.first().toLower();

            if (!validCmds.contains(cmd)) {
                std::cerr << "[" << lineNum << "] ERROR: Unknown command '"
                          << cmd.toStdString() << "'" << std::endl;
                errorCount++;
            } else {
                std::cout << "[" << lineNum << "] OK: "
                          << line.toStdString() << std::endl;
            }
        } else {
            // Execute the command
            CliResult result = m_engine.execute(line);

            if (!result.output.isEmpty()) {
                std::cout << "[" << lineNum << "] "
                          << result.output.toStdString() << std::endl;
            }

            if (result.exitCode != 0) {
                std::cerr << "Error at line " << lineNum << ": "
                          << result.error.toStdString() << std::endl;
                return 1;
            }

            if (result.requestExit) {
                // Script requested exit
                break;
            }
        }
    }

    if (checkOnly) {
        if (errorCount > 0) {
            std::cerr << "\nSyntax check failed: " << errorCount
                      << " error(s) in " << commandCount << " command(s)"
                      << std::endl;
            return 1;
        } else {
            std::cout << "\nSyntax check passed: " << commandCount
                      << " command(s) OK" << std::endl;
            return 0;
        }
    } else {
        std::cout << "\nScript completed: " << commandCount
                  << " command(s) executed." << std::endl;
        return 0;
    }
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

