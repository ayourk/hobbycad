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
    QStringList commands = {
        QStringLiteral("help"),
        QStringLiteral("version"),
        QStringLiteral("open"),
        QStringLiteral("save"),
        QStringLiteral("info"),
        QStringLiteral("new"),
        QStringLiteral("cd"),
        QStringLiteral("pwd"),
        QStringLiteral("history"),
        QStringLiteral("select"),
        QStringLiteral("create"),
        QStringLiteral("exit"),
        QStringLiteral("quit"),
    };

    // Add context-specific commands
    if (m_inSketchMode) {
        commands << QStringLiteral("point")
                 << QStringLiteral("line")
                 << QStringLiteral("circle")
                 << QStringLiteral("rectangle")
                 << QStringLiteral("arc")
                 << QStringLiteral("finish")
                 << QStringLiteral("discard");
    }

    return commands;
}

QString CliEngine::buildPrompt() const
{
    // If in sketch mode, show sketch context instead of directory
    if (m_inSketchMode) {
        return QStringLiteral("Sketch ") + m_currentSketchName + QStringLiteral("> ");
    }

    QString cwd = QDir::currentPath();

#if !defined(_WIN32)
    QString home = QDir::homePath();
    if (!home.isEmpty() && cwd.startsWith(home)) {
        cwd = QStringLiteral("~") + cwd.mid(home.length());
    }
#endif

    return QStringLiteral("hobbycad:") + cwd + QStringLiteral("> ");
}

bool CliEngine::inSketchMode() const
{
    return m_inSketchMode;
}

QString CliEngine::currentSketchName() const
{
    return m_currentSketchName;
}

// Helper to check if prefix looks like start of a parameter name
static bool looksLikeParamStart(const QString& prefix)
{
    if (prefix.isEmpty()) return false;
    // Starts with letter or open paren
    return prefix[0].isLetter() || prefix[0] == '(';
}

QStringList CliEngine::completeArguments(const QStringList& tokens,
                                          const QString& prefix) const
{
    if (tokens.isEmpty()) return {};

    QString cmd = tokens.first().toLower();
    int argIndex = tokens.size() - 1;  // Which argument we're completing (0 = command)

    // If there's a prefix being typed, we're still on the current argument
    // If prefix is empty, we're starting a new argument
    if (prefix.isEmpty() && tokens.size() > 1) {
        argIndex = tokens.size();  // Starting next argument
    }

    // Helper lambda to complete parameters when in a numeric field
    auto completeNumericField = [&](const QString& hint) -> QStringList {
        if (prefix.isEmpty()) {
            return { QStringLiteral("?%1  (or parameter name, or (expression))").arg(hint) };
        }
        if (prefix.startsWith('(')) {
            // Inside expression - no completion
            return { QStringLiteral("?...)  Complete the expression") };
        }
        if (prefix[0].isLetter()) {
            // Complete parameter names
            QStringList matches;
            for (const auto& p : m_parameters) {
                if (p.startsWith(prefix, Qt::CaseInsensitive)) {
                    matches.append(p);
                }
            }
            if (matches.isEmpty()) {
                return { QStringLiteral("?%1  (no matching parameters)").arg(hint) };
            }
            return matches;
        }
        // Typing a number - no completion needed
        return {};
    };

    // ---- select command ----
    if (cmd == QLatin1String("select")) {
        if (argIndex == 1) {
            // First argument: object type
            QStringList types = {
                QStringLiteral("sketch"),
                QStringLiteral("body"),
                QStringLiteral("face"),
                QStringLiteral("edge"),
                QStringLiteral("vertex")
            };

            if (prefix.isEmpty()) {
                // Show hint
                return { QStringLiteral("?<type>  Object type (sketch, body, face, edge, vertex)") };
            }

            // Filter by prefix
            QStringList matches;
            for (const auto& t : types) {
                if (t.startsWith(prefix, Qt::CaseInsensitive)) {
                    matches.append(t);
                }
            }
            return matches.isEmpty()
                ? QStringList{ QStringLiteral("?<type>  Object type (sketch, body, face, edge, vertex)") }
                : matches;
        }
        else if (argIndex == 2) {
            // Second argument: object name
            // TODO: Return actual object names from document
            return { QStringLiteral("?<name>  Name of the %1 to select").arg(
                tokens.size() > 1 ? tokens[1] : QStringLiteral("object")) };
        }
    }

    // ---- create command ----
    if (cmd == QLatin1String("create")) {
        if (argIndex == 1) {
            // First argument: object type to create
            QStringList types = { QStringLiteral("sketch") };

            if (prefix.isEmpty()) {
                return { QStringLiteral("?<type>  Object type to create (sketch)") };
            }

            QStringList matches;
            for (const auto& t : types) {
                if (t.startsWith(prefix, Qt::CaseInsensitive)) {
                    matches.append(t);
                }
            }
            return matches.isEmpty()
                ? QStringList{ QStringLiteral("?<type>  Object type to create (sketch)") }
                : matches;
        }
        else if (argIndex == 2) {
            QString type = tokens.size() > 1 ? tokens[1].toLower() : QString();
            if (type == QLatin1String("sketch")) {
                return { QStringLiteral("?[name]  Optional sketch name (default: auto-named)") };
            }
        }
    }

    // ---- open command ----
    if (cmd == QLatin1String("open")) {
        if (argIndex == 1 && prefix.isEmpty()) {
            return { QStringLiteral("?<file>  BREP file to open (.brep)") };
        }
        // Otherwise, let filename completion handle it
        return {};
    }

    // ---- save command ----
    if (cmd == QLatin1String("save")) {
        if (argIndex == 1 && prefix.isEmpty()) {
            return { QStringLiteral("?<file>  BREP file to save (.brep)") };
        }
        return {};
    }

    // ---- cd command ----
    if (cmd == QLatin1String("cd")) {
        if (argIndex == 1 && prefix.isEmpty()) {
            return { QStringLiteral("?[dir]  Directory to change to (default: home)") };
        }
        return {};
    }

    // ---- history command ----
    if (cmd == QLatin1String("history")) {
        if (argIndex == 1) {
            QStringList subcmds = {
                QStringLiteral("clear"),
                QStringLiteral("max")
            };

            if (prefix.isEmpty()) {
                return { QStringLiteral("?[clear|max]  Subcommand (or no args to show history)") };
            }

            QStringList matches;
            for (const auto& s : subcmds) {
                if (s.startsWith(prefix, Qt::CaseInsensitive)) {
                    matches.append(s);
                }
            }
            return matches;
        }
        else if (argIndex == 2 && tokens.size() > 1 &&
                 tokens[1].toLower() == QLatin1String("max")) {
            return { QStringLiteral("?<n>  Maximum number of history lines") };
        }
    }

    // ---- Sketch mode geometry commands ----
    if (m_inSketchMode) {
        // point [at] <x>,<y>
        if (cmd == QLatin1String("point")) {
            if (argIndex == 1) {
                if (prefix.isEmpty()) {
                    return { QStringLiteral("?[at] <x>,<y>  Point coordinates") };
                }
                if (QStringLiteral("at").startsWith(prefix, Qt::CaseInsensitive)) {
                    return { QStringLiteral("at") };
                }
                // Could be coordinates directly
                return { QStringLiteral("?<x>,<y>  Point coordinates") };
            }
            else if (argIndex == 2) {
                // Only reached if "at" was used
                return { QStringLiteral("?<x>,<y>  Point coordinates") };
            }
        }

        // line [from] <x>,<y> to <x>,<y>
        if (cmd == QLatin1String("line")) {
            if (argIndex == 1) {
                if (prefix.isEmpty()) {
                    return { QStringLiteral("?[from] <x>,<y>  Start point") };
                }
                if (QStringLiteral("from").startsWith(prefix, Qt::CaseInsensitive)) {
                    return { QStringLiteral("from") };
                }
                return { QStringLiteral("?<x>,<y>  Start point") };
            }
            else if (argIndex == 2) {
                // Could be coords (if no "from") or "to" keyword
                QString prev = tokens.size() > 1 ? tokens[1].toLower() : QString();
                if (prev == QLatin1String("from")) {
                    return { QStringLiteral("?<x>,<y>  Start point") };
                }
                if (prefix.isEmpty()) {
                    return { QStringLiteral("?to  End point follows") };
                }
                if (QStringLiteral("to").startsWith(prefix, Qt::CaseInsensitive)) {
                    return { QStringLiteral("to") };
                }
            }
            else if (argIndex == 3) {
                if (prefix.isEmpty()) {
                    return { QStringLiteral("?to  End point follows") };
                }
                if (QStringLiteral("to").startsWith(prefix, Qt::CaseInsensitive)) {
                    return { QStringLiteral("to") };
                }
            }
            else if (argIndex >= 3) {
                return { QStringLiteral("?<x>,<y>  End point") };
            }
        }

        // circle [at] <x>,<y> radius|diameter <value>
        if (cmd == QLatin1String("circle")) {
            if (argIndex == 1) {
                if (prefix.isEmpty()) {
                    return { QStringLiteral("?[at] <x>,<y>  Center point") };
                }
                if (QStringLiteral("at").startsWith(prefix, Qt::CaseInsensitive)) {
                    return { QStringLiteral("at") };
                }
                return { QStringLiteral("?<x>,<y>  Center point") };
            }
            else if (argIndex == 2) {
                QString prev = tokens.size() > 1 ? tokens[1].toLower() : QString();
                if (prev == QLatin1String("at")) {
                    return { QStringLiteral("?<x>,<y>  Center point") };
                }
                // Otherwise it's the size type
                if (prefix.isEmpty()) {
                    return { QStringLiteral("?radius|diameter <value>") };
                }
                QStringList opts = { QStringLiteral("radius"), QStringLiteral("diameter") };
                QStringList matches;
                for (const auto& o : opts) {
                    if (o.startsWith(prefix, Qt::CaseInsensitive)) {
                        matches.append(o);
                    }
                }
                return matches.isEmpty()
                    ? QStringList{ QStringLiteral("?radius|diameter  Size type") }
                    : matches;
            }
            else if (argIndex == 3) {
                QString token1 = tokens.size() > 1 ? tokens[1].toLower() : QString();
                if (token1 == QLatin1String("at")) {
                    // Size type comes next
                    if (prefix.isEmpty()) {
                        return { QStringLiteral("?radius|diameter <value>") };
                    }
                    QStringList opts = { QStringLiteral("radius"), QStringLiteral("diameter") };
                    QStringList matches;
                    for (const auto& o : opts) {
                        if (o.startsWith(prefix, Qt::CaseInsensitive)) {
                            matches.append(o);
                        }
                    }
                    return matches.isEmpty()
                        ? QStringList{ QStringLiteral("?radius|diameter  Size type") }
                        : matches;
                }
                // Otherwise it's the value
                QString sizeType = tokens.size() > 2 ? tokens[2].toLower() : QString();
                QString hint = (sizeType == QLatin1String("diameter"))
                    ? QStringLiteral("<d>  Diameter") : QStringLiteral("<r>  Radius");
                return completeNumericField(hint);
            }
            else if (argIndex == 4) {
                QString sizeType = tokens.size() > 3 ? tokens[3].toLower() : QString();
                QString hint = (sizeType == QLatin1String("diameter"))
                    ? QStringLiteral("<d>  Diameter") : QStringLiteral("<r>  Radius");
                return completeNumericField(hint);
            }
        }

        // rectangle [from] <x>,<y> to <x>,<y>
        if (cmd == QLatin1String("rectangle")) {
            if (argIndex == 1) {
                if (prefix.isEmpty()) {
                    return { QStringLiteral("?[from] <x>,<y>  First corner") };
                }
                if (QStringLiteral("from").startsWith(prefix, Qt::CaseInsensitive)) {
                    return { QStringLiteral("from") };
                }
                return { QStringLiteral("?<x>,<y>  First corner") };
            }
            else if (argIndex == 2) {
                QString prev = tokens.size() > 1 ? tokens[1].toLower() : QString();
                if (prev == QLatin1String("from")) {
                    return { QStringLiteral("?<x>,<y>  First corner") };
                }
                if (prefix.isEmpty()) {
                    return { QStringLiteral("?to  Opposite corner follows") };
                }
                if (QStringLiteral("to").startsWith(prefix, Qt::CaseInsensitive)) {
                    return { QStringLiteral("to") };
                }
            }
            else if (argIndex == 3) {
                if (prefix.isEmpty()) {
                    return { QStringLiteral("?to  Opposite corner follows") };
                }
                if (QStringLiteral("to").startsWith(prefix, Qt::CaseInsensitive)) {
                    return { QStringLiteral("to") };
                }
            }
            else if (argIndex >= 3) {
                return { QStringLiteral("?<x>,<y>  Opposite corner") };
            }
        }

        // arc [at] <x>,<y> radius <r> [angle] <start> to <end>
        if (cmd == QLatin1String("arc")) {
            if (argIndex == 1) {
                if (prefix.isEmpty()) {
                    return { QStringLiteral("?[at] <x>,<y>  Center point") };
                }
                if (QStringLiteral("at").startsWith(prefix, Qt::CaseInsensitive)) {
                    return { QStringLiteral("at") };
                }
                return { QStringLiteral("?<x>,<y>  Center point") };
            }
            // For arc, the structure varies based on optional keywords
            // Just provide contextual hints based on what's been typed
            else {
                // Check if we need radius keyword
                bool hasAt = tokens.size() > 1 && tokens[1].toLower() == QLatin1String("at");
                int radiusIdx = hasAt ? 3 : 2;

                if (argIndex == radiusIdx) {
                    if (prefix.isEmpty()) {
                        return { QStringLiteral("?radius  Specify radius") };
                    }
                    if (QStringLiteral("radius").startsWith(prefix, Qt::CaseInsensitive)) {
                        return { QStringLiteral("radius") };
                    }
                }
                else if (argIndex == radiusIdx + 1) {
                    return completeNumericField(QStringLiteral("<r>  Radius"));
                }
                else if (argIndex == radiusIdx + 2) {
                    if (prefix.isEmpty()) {
                        return { QStringLiteral("?[angle] <start>  Start angle (degrees)") };
                    }
                    if (QStringLiteral("angle").startsWith(prefix, Qt::CaseInsensitive)) {
                        return { QStringLiteral("angle") };
                    }
                    // Could be typing a number or parameter for start angle
                    return completeNumericField(QStringLiteral("<start>  Start angle"));
                }
                else {
                    // Could be start angle, "to", or end angle
                    if (prefix.isEmpty()) {
                        return { QStringLiteral("?to <end>  End angle follows") };
                    }
                    if (QStringLiteral("to").startsWith(prefix, Qt::CaseInsensitive)) {
                        return { QStringLiteral("to") };
                    }
                    // Could be typing end angle
                    return completeNumericField(QStringLiteral("<end>  End angle"));
                }
            }
        }
    }

    return {};
}

// Helper to tokenize a command line, keeping parenthesized expressions intact
// e.g., "circle (a + b),(c * d) radius (r * 2)" -> ["circle", "(a + b),(c * d)", "radius", "(r * 2)"]
static QStringList tokenizeLine(const QString& line)
{
    QStringList tokens;
    QString current;
    int parenDepth = 0;
    bool inToken = false;

    for (int i = 0; i < line.length(); ++i) {
        QChar c = line[i];

        if (c == '(') {
            parenDepth++;
            current += c;
            inToken = true;
        } else if (c == ')') {
            parenDepth--;
            current += c;
            inToken = true;
        } else if (c.isSpace() && parenDepth == 0) {
            // End of token (unless inside parentheses)
            if (inToken && !current.isEmpty()) {
                tokens.append(current);
                current.clear();
                inToken = false;
            }
        } else {
            current += c;
            inToken = true;
        }
    }

    // Don't forget the last token
    if (!current.isEmpty()) {
        tokens.append(current);
    }

    return tokens;
}

// ---- Main dispatch --------------------------------------------------

CliResult CliEngine::execute(const QString& line)
{
    QStringList tokens = tokenizeLine(line);

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
    if (cmd == QLatin1String("select"))  return cmdSelect(tokens.mid(1));
    if (cmd == QLatin1String("create"))  return cmdCreate(tokens.mid(1));

    // Sketch mode commands
    if (cmd == QLatin1String("finish"))  return cmdFinish();
    if (cmd == QLatin1String("discard")) return cmdDiscard();

    // Sketch geometry commands (only in sketch mode)
    if (m_inSketchMode) {
        if (cmd == QLatin1String("point"))     return cmdSketchPoint(tokens.mid(1));
        if (cmd == QLatin1String("line"))      return cmdSketchLine(tokens.mid(1));
        if (cmd == QLatin1String("circle"))    return cmdSketchCircle(tokens.mid(1));
        if (cmd == QLatin1String("rectangle")) return cmdSketchRectangle(tokens.mid(1));
        if (cmd == QLatin1String("arc"))       return cmdSketchArc(tokens.mid(1));
    }

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
    QString helpText = QStringLiteral(
        "Available commands:\n"
        "\n"
        "  help                    Show this help message\n"
        "  version                 Show HobbyCAD version\n"
        "  open <file>             Open a BREP file (.brep added if no extension)\n"
        "  save <file>             Save to a BREP file (.brep added if no extension)\n"
        "  info                    Show current document info\n"
        "  new                     Create a new document with a test solid\n"
        "  cd [dir]                Change working directory (no arg = home)\n"
        "  pwd                     Print working directory\n"
        "  history                 Show command history\n"
        "  history clear           Clear command history\n"
        "  history max <n>         Set max history lines (current: %1)\n"
        "\n"
        "Selection & Creation:\n"
        "  select <type> <name>    Select an object (e.g., select sketch Sketch1)\n"
        "  create sketch [name]    Create a new sketch (auto-named if no name given)\n")
        .arg(m_history.maxLines());

    if (m_inSketchMode) {
        helpText += QStringLiteral(
            "\n"
            "Sketch Geometry:\n"
            "  point [at] <x>,<y>\n"
            "  line [from] <x>,<y> to <x>,<y>\n"
            "  circle [at] <x>,<y> radius|diameter <value>\n"
            "  rectangle [from] <x>,<y> to <x>,<y>\n"
            "  arc [at] <x>,<y> radius <r> [angle] <start> to <end>\n"
            "\n"
            "  Values can be numbers, parameters, or (expressions):\n"
            "    circle 0,0 radius 25\n"
            "    circle 0,0 radius myRadius\n"
            "    circle (width/2),(height/2) radius (size*0.5)\n"
            "\n"
            "Sketch Mode:\n"
            "  finish                  Save and exit sketch mode\n"
            "  discard                 Discard changes and exit sketch mode\n");
    }

    helpText += QStringLiteral(
        "\n"
        "  exit / quit             Exit HobbyCAD\n");

    r.output = helpText;
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

CliResult CliEngine::cmdSelect(const QStringList& args)
{
    CliResult r;

    if (args.size() < 2) {
        r.exitCode = 1;
        r.error = QStringLiteral(
            "Usage: select <type> <name>\n"
            "\n"
            "Types: sketch, body, face, edge, vertex\n"
            "\n"
            "Examples:\n"
            "  select sketch Sketch1\n"
            "  select body Body1");
        return r;
    }

    QString type = args[0].toLower();
    QString name = args.mid(1).join(QStringLiteral(" "));

    // Validate type
    QStringList validTypes = {
        QStringLiteral("sketch"),
        QStringLiteral("body"),
        QStringLiteral("face"),
        QStringLiteral("edge"),
        QStringLiteral("vertex")
    };

    if (!validTypes.contains(type)) {
        r.exitCode = 1;
        r.error = QStringLiteral("Unknown type: ") + type +
                  QStringLiteral("\nValid types: sketch, body, face, edge, vertex");
        return r;
    }

    // TODO: Actually look up and select the object in the document
    // For now, just acknowledge the selection
    r.output = QStringLiteral("Selected %1 '%2'").arg(type, name);
    return r;
}

CliResult CliEngine::cmdCreate(const QStringList& args)
{
    CliResult r;

    if (args.isEmpty()) {
        r.exitCode = 1;
        r.error = QStringLiteral(
            "Usage: create <type> [name]\n"
            "\n"
            "Types: sketch\n"
            "\n"
            "Examples:\n"
            "  create sketch           (auto-named Sketch1, Sketch2, etc.)\n"
            "  create sketch MySketch");
        return r;
    }

    QString type = args[0].toLower();

    if (type == QLatin1String("sketch")) {
        // Generate name or use provided name
        QString sketchName;
        if (args.size() >= 2) {
            sketchName = args.mid(1).join(QStringLiteral(" "));
        } else {
            m_sketchCounter++;
            sketchName = QStringLiteral("Sketch%1").arg(m_sketchCounter);
        }

        // Enter sketch mode
        m_inSketchMode = true;
        m_currentSketchName = sketchName;

        r.output = QStringLiteral("Created sketch '%1'. Entering sketch mode.\n"
                                  "Use 'finish' to save or 'discard' to cancel.")
                       .arg(sketchName);
        return r;
    }

    r.exitCode = 1;
    r.error = QStringLiteral("Unknown type: ") + type +
              QStringLiteral("\nCurrently supported: sketch");
    return r;
}

CliResult CliEngine::cmdFinish()
{
    CliResult r;

    if (!m_inSketchMode) {
        r.exitCode = 1;
        r.error = QStringLiteral("Not in sketch mode. Use 'create sketch' first.");
        return r;
    }

    QString sketchName = m_currentSketchName;
    m_inSketchMode = false;
    m_currentSketchName.clear();

    r.output = QStringLiteral("Saved sketch '%1'. Exiting sketch mode.").arg(sketchName);
    return r;
}

CliResult CliEngine::cmdDiscard()
{
    CliResult r;

    if (!m_inSketchMode) {
        r.exitCode = 1;
        r.error = QStringLiteral("Not in sketch mode. Nothing to discard.");
        return r;
    }

    QString sketchName = m_currentSketchName;
    m_inSketchMode = false;
    m_currentSketchName.clear();

    // Decrement counter since we're discarding
    if (sketchName.startsWith(QLatin1String("Sketch")) && m_sketchCounter > 0) {
        // Only decrement if it was an auto-named sketch
        bool ok = false;
        int num = sketchName.mid(6).toInt(&ok);
        if (ok && num == m_sketchCounter) {
            m_sketchCounter--;
        }
    }

    r.output = QStringLiteral("Discarded sketch '%1'. Exiting sketch mode.").arg(sketchName);
    return r;
}

// Helper to parse a value that may be a number, parameter, or expression
// Returns the string representation (for later evaluation) and attempts numeric parse
static bool parseValue(const QString& str, double& value, QString& expr)
{
    expr = str.trimmed();
    if (expr.isEmpty()) return false;

    // If it starts with a digit, minus, or decimal point, try to parse as number
    QChar first = expr[0];
    if (first.isDigit() || first == '-' || first == '.') {
        bool ok = false;
        value = expr.toDouble(&ok);
        return ok;  // Must be a valid number if it starts like one
    }

    // If it's a parenthesized expression, accept it
    if (first == '(' && expr.endsWith(')')) {
        value = 0;  // Placeholder - will be evaluated later
        return true;
    }

    // Must be a parameter name: starts with letter, contains only letters/digits/underscore
    if (first.isLetter()) {
        bool validParam = true;
        for (const QChar& c : expr) {
            if (!c.isLetterOrNumber() && c != '_') {
                validParam = false;
                break;
            }
        }
        if (validParam) {
            value = 0;  // Placeholder - will be resolved later
            return true;
        }
    }

    return false;
}

// Helper to split coordinate string respecting parentheses
// e.g., "(a+b),(c*d)" splits into ["(a+b)", "(c*d)"]
static QStringList splitCoordinate(const QString& str)
{
    QStringList parts;
    QString current;
    int parenDepth = 0;

    for (const QChar& c : str) {
        if (c == '(') {
            parenDepth++;
            current += c;
        } else if (c == ')') {
            parenDepth--;
            current += c;
        } else if (c == ',' && parenDepth == 0) {
            parts.append(current.trimmed());
            current.clear();
        } else {
            current += c;
        }
    }

    if (!current.isEmpty()) {
        parts.append(current.trimmed());
    }

    return parts;
}

// Helper to parse "x,y" coordinate string (supports expressions in parentheses)
static bool parseCoord(const QString& str, double& x, double& y,
                       QString* xExpr = nullptr, QString* yExpr = nullptr)
{
    QStringList parts = splitCoordinate(str);
    if (parts.size() != 2) return false;

    QString exprX, exprY;
    bool okX = parseValue(parts[0], x, exprX);
    bool okY = parseValue(parts[1], y, exprY);

    if (xExpr) *xExpr = exprX;
    if (yExpr) *yExpr = exprY;

    return okX && okY;
}

CliResult CliEngine::cmdSketchPoint(const QStringList& args)
{
    CliResult r;

    if (args.isEmpty()) {
        r.exitCode = 1;
        r.error = QStringLiteral(
            "Usage: point [at] <x>,<y>\n"
            "\n"
            "Examples:\n"
            "  point at 10,20\n"
            "  point 10,20");
        return r;
    }

    // "at" keyword is optional
    int coordIdx = 0;
    if (args[0].toLower() == QLatin1String("at")) {
        coordIdx = 1;
        if (args.size() < 2) {
            r.exitCode = 1;
            r.error = QStringLiteral("Missing coordinates after 'at'");
            return r;
        }
    }

    double x, y;
    if (!parseCoord(args[coordIdx], x, y)) {
        r.exitCode = 1;
        r.error = QStringLiteral("Invalid coordinates. Use format: x,y (e.g., 10,20)");
        return r;
    }

    // TODO: Actually create point in sketch
    r.output = QStringLiteral("Created point at (%1, %2)").arg(x).arg(y);
    return r;
}

CliResult CliEngine::cmdSketchLine(const QStringList& args)
{
    CliResult r;

    if (args.size() < 3) {
        r.exitCode = 1;
        r.error = QStringLiteral(
            "Usage: line [from] <x1>,<y1> to <x2>,<y2>\n"
            "\n"
            "Examples:\n"
            "  line from 0,0 to 100,50\n"
            "  line 0,0 to 100,50");
        return r;
    }

    // "from" keyword is optional
    int idx = 0;
    if (args[0].toLower() == QLatin1String("from")) {
        idx = 1;
    }

    // Find "to" keyword
    int toIdx = -1;
    for (int i = idx; i < args.size(); ++i) {
        if (args[i].toLower() == QLatin1String("to")) {
            toIdx = i;
            break;
        }
    }

    if (toIdx < 0 || toIdx <= idx || toIdx + 1 >= args.size()) {
        r.exitCode = 1;
        r.error = QStringLiteral("Missing 'to' keyword or end coordinates");
        return r;
    }

    double x1, y1, x2, y2;
    if (!parseCoord(args[idx], x1, y1)) {
        r.exitCode = 1;
        r.error = QStringLiteral("Invalid start coordinates. Use format: x,y");
        return r;
    }
    if (!parseCoord(args[toIdx + 1], x2, y2)) {
        r.exitCode = 1;
        r.error = QStringLiteral("Invalid end coordinates. Use format: x,y");
        return r;
    }

    // TODO: Actually create line in sketch
    r.output = QStringLiteral("Created line from (%1, %2) to (%3, %4)")
                   .arg(x1).arg(y1).arg(x2).arg(y2);
    return r;
}

CliResult CliEngine::cmdSketchCircle(const QStringList& args)
{
    CliResult r;

    if (args.size() < 3) {
        r.exitCode = 1;
        r.error = QStringLiteral(
            "Usage: circle [at] <x>,<y> radius|diameter <value>\n"
            "\n"
            "Examples:\n"
            "  circle at 50,50 radius 25\n"
            "  circle 50,50 radius 25\n"
            "  circle 100,100 diameter 60");
        return r;
    }

    // "at" keyword is optional
    int idx = 0;
    if (args[0].toLower() == QLatin1String("at")) {
        idx = 1;
        if (args.size() < 4) {
            r.exitCode = 1;
            r.error = QStringLiteral("Missing arguments after 'at'");
            return r;
        }
    }

    double cx, cy;
    if (!parseCoord(args[idx], cx, cy)) {
        r.exitCode = 1;
        r.error = QStringLiteral("Invalid center coordinates. Use format: x,y");
        return r;
    }

    QString sizeType = args[idx + 1].toLower();
    if (sizeType != QLatin1String("radius") && sizeType != QLatin1String("diameter")) {
        r.exitCode = 1;
        r.error = QStringLiteral("Size type must be 'radius' or 'diameter'");
        return r;
    }

    if (idx + 2 >= args.size()) {
        r.exitCode = 1;
        r.error = QStringLiteral("Missing size value");
        return r;
    }

    double value;
    QString valueExpr;
    if (!parseValue(args[idx + 2], value, valueExpr)) {
        r.exitCode = 1;
        r.error = QStringLiteral("Invalid size value. Must be a number, parameter, or (expression).");
        return r;
    }

    // For now, only validate if it's a plain number
    bool isPlainNumber = (valueExpr == args[idx + 2].trimmed());
    if (isPlainNumber && value <= 0) {
        r.exitCode = 1;
        r.error = QStringLiteral("Size value must be positive.");
        return r;
    }

    double radius = (sizeType == QLatin1String("diameter")) ? value / 2.0 : value;
    QString radiusExpr = (sizeType == QLatin1String("diameter"))
        ? QStringLiteral("(%1)/2").arg(valueExpr)
        : valueExpr;

    // TODO: Actually create circle in sketch with expression support
    r.output = QStringLiteral("Created circle at (%1, %2) with radius %3")
                   .arg(cx).arg(cy).arg(isPlainNumber ? QString::number(radius) : radiusExpr);
    return r;
}

CliResult CliEngine::cmdSketchRectangle(const QStringList& args)
{
    CliResult r;

    if (args.size() < 3) {
        r.exitCode = 1;
        r.error = QStringLiteral(
            "Usage: rectangle [from] <x1>,<y1> to <x2>,<y2>\n"
            "\n"
            "Examples:\n"
            "  rectangle from 0,0 to 100,50\n"
            "  rectangle 0,0 to 100,50");
        return r;
    }

    // "from" keyword is optional
    int idx = 0;
    if (args[0].toLower() == QLatin1String("from")) {
        idx = 1;
    }

    // Find "to" keyword
    int toIdx = -1;
    for (int i = idx; i < args.size(); ++i) {
        if (args[i].toLower() == QLatin1String("to")) {
            toIdx = i;
            break;
        }
    }

    if (toIdx < 0 || toIdx <= idx || toIdx + 1 >= args.size()) {
        r.exitCode = 1;
        r.error = QStringLiteral("Missing 'to' keyword or second corner coordinates");
        return r;
    }

    double x1, y1, x2, y2;
    if (!parseCoord(args[idx], x1, y1)) {
        r.exitCode = 1;
        r.error = QStringLiteral("Invalid first corner coordinates. Use format: x,y");
        return r;
    }
    if (!parseCoord(args[toIdx + 1], x2, y2)) {
        r.exitCode = 1;
        r.error = QStringLiteral("Invalid second corner coordinates. Use format: x,y");
        return r;
    }

    // TODO: Actually create rectangle in sketch
    r.output = QStringLiteral("Created rectangle from (%1, %2) to (%3, %4)")
                   .arg(x1).arg(y1).arg(x2).arg(y2);
    return r;
}

CliResult CliEngine::cmdSketchArc(const QStringList& args)
{
    CliResult r;

    if (args.size() < 6) {
        r.exitCode = 1;
        r.error = QStringLiteral(
            "Usage: arc [at] <x>,<y> radius <r> [angle] <start> to <end>\n"
            "\n"
            "Examples:\n"
            "  arc at 50,50 radius 30 angle 0 to 90\n"
            "  arc 50,50 radius 30 0 to 90");
        return r;
    }

    // "at" keyword is optional
    int idx = 0;
    if (args[0].toLower() == QLatin1String("at")) {
        idx = 1;
    }

    double cx, cy;
    if (!parseCoord(args[idx], cx, cy)) {
        r.exitCode = 1;
        r.error = QStringLiteral("Invalid center coordinates. Use format: x,y");
        return r;
    }
    idx++;

    // "radius" keyword is required
    if (idx >= args.size() || args[idx].toLower() != QLatin1String("radius")) {
        r.exitCode = 1;
        r.error = QStringLiteral("Expected 'radius' keyword");
        return r;
    }
    idx++;

    if (idx >= args.size()) {
        r.exitCode = 1;
        r.error = QStringLiteral("Missing radius value");
        return r;
    }

    double radius;
    QString radiusExpr;
    if (!parseValue(args[idx], radius, radiusExpr)) {
        r.exitCode = 1;
        r.error = QStringLiteral("Invalid radius. Must be a number, parameter, or (expression).");
        return r;
    }
    idx++;

    // "angle" keyword is optional
    if (idx < args.size() && args[idx].toLower() == QLatin1String("angle")) {
        idx++;
    }

    if (idx >= args.size()) {
        r.exitCode = 1;
        r.error = QStringLiteral("Missing start angle");
        return r;
    }

    double startAngle;
    QString startExpr;
    if (!parseValue(args[idx], startAngle, startExpr)) {
        r.exitCode = 1;
        r.error = QStringLiteral("Invalid start angle. Must be a number, parameter, or (expression).");
        return r;
    }
    idx++;

    // "to" keyword is required
    if (idx >= args.size() || args[idx].toLower() != QLatin1String("to")) {
        r.exitCode = 1;
        r.error = QStringLiteral("Expected 'to' keyword");
        return r;
    }
    idx++;

    if (idx >= args.size()) {
        r.exitCode = 1;
        r.error = QStringLiteral("Missing end angle");
        return r;
    }

    double endAngle;
    QString endExpr;
    if (!parseValue(args[idx], endAngle, endExpr)) {
        r.exitCode = 1;
        r.error = QStringLiteral("Invalid end angle. Must be a number, parameter, or (expression).");
        return r;
    }

    // TODO: Actually create arc in sketch with expression support
    r.output = QStringLiteral("Created arc at (%1, %2) with radius %3 from %4° to %5°")
                   .arg(cx).arg(cy).arg(radiusExpr).arg(startExpr).arg(endExpr);
    return r;
}

}  // namespace hobbycad

