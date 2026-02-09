// =====================================================================
//  src/hobbycad/cli/clihistory.cpp — Command history for CLI mode
// =====================================================================

#include "clihistory.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTextStream>

namespace hobbycad {

static const char* HistoryFileName = "cli_history";
static const char* AppDirName      = "hobbycad";

CliHistory::CliHistory(int maxLines)
    : m_maxLines(maxLines < 1 ? DefaultMaxLines : maxLines)
{
}

CliHistory::~CliHistory() = default;

// ---- Configuration --------------------------------------------------

int CliHistory::maxLines() const { return m_maxLines; }

void CliHistory::setMaxLines(int maxLines)
{
    m_maxLines = (maxLines < 1) ? 1 : maxLines;
    trim();
}

// ---- History access -------------------------------------------------

const QStringList& CliHistory::entries() const { return m_entries; }
int CliHistory::count() const { return m_entries.size(); }

void CliHistory::append(const QString& command)
{
    QString trimmed = command.trimmed();
    if (trimmed.isEmpty()) return;

    // Consecutive dedup: skip if identical to the last entry
    if (!m_entries.isEmpty() && m_entries.last() == trimmed) {
        return;
    }

    m_entries.append(trimmed);
    trim();
}

void CliHistory::clear()
{
    m_entries.clear();
}

// ---- Persistence ----------------------------------------------------

QString CliHistory::filePath() const
{
    QString configDir = QStandardPaths::writableLocation(
        QStandardPaths::GenericConfigLocation);

    return configDir + QDir::separator()
           + QLatin1String(AppDirName)
           + QDir::separator()
           + QLatin1String(HistoryFileName);
}

bool CliHistory::load()
{
    QString path = filePath();
    QFile file(path);

    if (!file.exists()) {
        // No history yet — not an error
        return true;
    }

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream in(&file);
    m_entries.clear();

    while (!in.atEnd()) {
        QString line = in.readLine();
        if (!line.isEmpty()) {
            m_entries.append(line);
        }
    }

    file.close();
    trim();
    return true;
}

bool CliHistory::save() const
{
    QString path = filePath();

    // Ensure the directory exists
    QFileInfo fi(path);
    QDir dir = fi.absoluteDir();
    if (!dir.exists()) {
        if (!dir.mkpath(QStringLiteral("."))) {
            return false;
        }
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text
                   | QIODevice::Truncate)) {
        return false;
    }

    QTextStream out(&file);
    for (const QString& entry : m_entries) {
        out << entry << '\n';
    }

    file.close();
    return true;
}

// ---- Internal -------------------------------------------------------

void CliHistory::trim()
{
    while (m_entries.size() > m_maxLines) {
        m_entries.removeFirst();
    }
}

}  // namespace hobbycad

