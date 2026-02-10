// =====================================================================
//  src/hobbycad/cli/cliengine.cpp — Shared command dispatch engine
// =====================================================================

#include "cliengine.h"
#include "clihistory.h"

#include <hobbycad/core.h>
#include <hobbycad/brep_io.h>
#include <hobbycad/document.h>

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

namespace hobbycad {

CliEngine::CliEngine(CliHistory& history)
    : m_history(history)
{
}

CliEngine::~CliEngine() = default;

QStringList CliEngine::commandNames() const
{
    return {
        QStringLiteral("help"),
        QStringLiteral("version"),
        QStringLiteral("open"),
        QStringLiteral("save"),
        QStringLiteral("info"),
        QStringLiteral("new"),
        QStringLiteral("cd"),
        QStringLiteral("pwd"),
        QStringLiteral("history"),
        QStringLiteral("exit"),
        QStringLiteral("quit"),
    };
}

QString CliEngine::buildPrompt() const
{
    QString cwd = QDir::currentPath();

#if !defined(_WIN32)
    QString home = QDir::homePath();
    if (!home.isEmpty() && cwd.startsWith(home)) {
        cwd = QStringLiteral("~") + cwd.mid(home.length());
    }
#endif

    return QStringLiteral("hobbycad:") + cwd + QStringLiteral("> ");
}

// ---- Main dispatch --------------------------------------------------

CliResult CliEngine::execute(const QString& line)
{
    QStringList tokens = line.split(
        QRegularExpression(QStringLiteral("\\s+")),
        Qt::SkipEmptyParts);

    if (tokens.isEmpty()) return {};

    QString cmd = tokens.first().toLower();

    if (cmd == QLatin1String("exit") ||
        cmd == QLatin1String("quit")) {
        CliResult r;
        r.requestExit = true;
        return r;
    }

    if (cmd == QLatin1String("help"))    return cmdHelp();
    if (cmd == QLatin1String("version")) return cmdVersion();
    if (cmd == QLatin1String("new"))     return cmdNew();
    if (cmd == QLatin1String("open"))    return cmdOpen(tokens.mid(1));
    if (cmd == QLatin1String("save"))    return cmdSave(tokens.mid(1));
    if (cmd == QLatin1String("cd"))      return cmdCd(tokens.mid(1));
    if (cmd == QLatin1String("pwd"))     return cmdPwd();
    if (cmd == QLatin1String("info"))    return cmdInfo();
    if (cmd == QLatin1String("history")) return cmdHistory(tokens.mid(1));

    CliResult r;
    r.exitCode = 1;
    r.error = QStringLiteral("Unknown command: ") + cmd +
              QStringLiteral("\nType 'help' for available commands.");
    return r;
}

// ---- Individual commands --------------------------------------------

CliResult CliEngine::cmdHelp() const
{
    CliResult r;
    r.output = QStringLiteral(
        "Available commands:\n"
        "\n"
        "  help              Show this help message\n"
        "  version           Show HobbyCAD version\n"
        "  open <file>       Open a BREP file (.brep added if no extension)\n"
        "  save <file>       Save to a BREP file (.brep added if no extension)\n"
        "  info              Show current document info\n"
        "  new               Create a new document with a test solid\n"
        "  cd [dir]          Change working directory (no arg = home)\n"
        "  pwd               Print working directory\n"
        "  history           Show command history\n"
        "  history clear     Clear command history\n"
        "  history max <n>   Set max history lines (current: %1)\n"
        "  exit / quit       Exit HobbyCAD\n")
        .arg(m_history.maxLines());
    return r;
}

CliResult CliEngine::cmdVersion() const
{
    CliResult r;
    r.output = QStringLiteral("HobbyCAD ") +
               QString::fromLatin1(hobbycad::version());
    return r;
}

CliResult CliEngine::cmdNew()
{
    Document doc;
    doc.createTestSolid();
    CliResult r;
    r.output = QStringLiteral("Created new document with test solid (%1 shape(s)).")
                   .arg(doc.shapes().size());
    return r;
}

CliResult CliEngine::cmdOpen(const QStringList& args)
{
    CliResult r;
    if (args.isEmpty()) {
        r.exitCode = 1;
        r.error = QStringLiteral("Usage: open <filename>");
        return r;
    }

    QString path = args.join(QStringLiteral(" "));

    // Try the path as given first
    if (!QFileInfo::exists(path)) {
        // If no extension, try appending .brep
        QString suffix = QFileInfo(path).suffix().toLower();
        if (suffix.isEmpty()) {
            QString withExt = path + QStringLiteral(".brep");
            if (QFileInfo::exists(withExt))
                path = withExt;
            // else fall through with original path — readBrep will
            // report the error
        }
    }

    QString err;
    auto shapes = brep_io::readBrep(path, &err);
    if (shapes.isEmpty()) {
        r.exitCode = 1;
        r.error = QStringLiteral("Error: ") + err;
        return r;
    }

    r.output = QStringLiteral("Opened: %1 (%2 shape(s))")
                   .arg(path).arg(shapes.size());
    return r;
}

CliResult CliEngine::cmdSave(const QStringList& args)
{
    CliResult r;
    if (args.isEmpty()) {
        r.exitCode = 1;
        r.error = QStringLiteral("Usage: save <filename>");
        return r;
    }

    Document doc;
    doc.createTestSolid();
    QString path = args.join(QStringLiteral(" "));

    // Auto-append .brep if no extension provided
    QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix.isEmpty())
        path += QStringLiteral(".brep");

    if (!doc.saveBrep(path)) {
        r.exitCode = 1;
        r.error = QStringLiteral("Error: could not save to ") + path;
        return r;
    }

    r.output = QStringLiteral("Saved: ") + path;
    return r;
}

CliResult CliEngine::cmdCd(const QStringList& args)
{
    CliResult r;
    QString target;

    if (args.isEmpty()) {
        target = QDir::homePath();
    } else {
        target = args.join(QStringLiteral(" "));
#if !defined(_WIN32)
        if (target.startsWith(QLatin1String("~/"))) {
            target = QDir::homePath() + target.mid(1);
        } else if (target == QLatin1String("~")) {
            target = QDir::homePath();
        }
#endif
    }

    QDir dir(target);
    if (!dir.exists()) {
        r.exitCode = 1;
        r.error = QStringLiteral("cd: no such directory: ") + target;
        return r;
    }
    if (!QDir::setCurrent(dir.absolutePath())) {
        r.exitCode = 1;
        r.error = QStringLiteral("cd: failed to change to: ") + target;
        return r;
    }

    return r;
}

CliResult CliEngine::cmdPwd() const
{
    CliResult r;
    r.output = QDir::currentPath();
    return r;
}

CliResult CliEngine::cmdInfo() const
{
    CliResult r;
    r.output = QStringLiteral("HobbyCAD ") +
               QString::fromLatin1(hobbycad::version()) +
               QStringLiteral("\nPhase 0 — Foundation\n"
                              "Supported formats: BREP (.brep, .brp)");
    return r;
}

CliResult CliEngine::cmdHistory(const QStringList& args)
{
    CliResult r;

    if (args.isEmpty()) {
        const auto& entries = m_history.entries();
        if (entries.isEmpty()) {
            r.output = QStringLiteral("History is empty.");
            return r;
        }

        QString text;
        int width = QString::number(entries.size()).length();
        for (int i = 0; i < entries.size(); ++i) {
            text += QStringLiteral("  ") +
                    QString::number(i + 1).rightJustified(width, ' ') +
                    QStringLiteral("  ") + entries[i] +
                    QStringLiteral("\n");
        }
        text += QStringLiteral("(%1 of %2 max)")
                    .arg(entries.size()).arg(m_history.maxLines());
        r.output = text;
        return r;
    }

    QString subcmd = args.first().toLower();

    if (subcmd == QLatin1String("clear")) {
        m_history.clear();
        r.output = QStringLiteral("History cleared.");
        return r;
    }

    if (subcmd == QLatin1String("max") && args.size() >= 2) {
        bool ok = false;
        int newMax = args[1].toInt(&ok);
        if (!ok || newMax < 1) {
            r.exitCode = 1;
            r.error = QStringLiteral("Error: max must be a positive integer.");
            return r;
        }
        m_history.setMaxLines(newMax);
        r.output = QStringLiteral("History max set to %1 lines.").arg(newMax);
        return r;
    }

    r.exitCode = 1;
    r.error = QStringLiteral("Usage: history [clear | max <n>]");
    return r;
}

}  // namespace hobbycad

