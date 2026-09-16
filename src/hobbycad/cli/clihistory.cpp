// =====================================================================
//  src/hobbycad/cli/clihistory.cpp — Command history for CLI mode
// =====================================================================

#include "clihistory.h"

#include <hobbycad/strutil.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace hobbycad {

static const char* HistoryFileName = "cli_history";
static const char* AppDirName      = "hobbycad";

namespace {

/// Where QStandardPaths::GenericConfigLocation pointed, reproduced so a
/// history file written by an earlier build is still the one read:
///
///   Linux, BSD: $XDG_CONFIG_HOME, else $HOME/.config
///   macOS:      $HOME/Library/Preferences
///   Windows:    %LOCALAPPDATA%, else %USERPROFILE%/AppData/Local
///
/// Empty when the environment says nothing, which leaves the path
/// relative and load() simply finding no file. Losing history is not
/// worth refusing to start a session over.
std::string configHome()
{
#if defined(_WIN32)
    if (const char* local = std::getenv("LOCALAPPDATA"); local && *local)
        return std::string(local);
    if (const char* profile = std::getenv("USERPROFILE"); profile && *profile)
        return std::string(profile) + "/AppData/Local";
    return {};
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME"); home && *home)
        return std::string(home) + "/Library/Preferences";
    return {};
#else
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg)
        return std::string(xdg);
    if (const char* home = std::getenv("HOME"); home && *home)
        return std::string(home) + "/.config";
    return {};
#endif
}

}  // namespace

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

const std::vector<std::string>& CliHistory::entries() const { return m_entries; }
int CliHistory::count() const { return static_cast<int>(m_entries.size()); }

void CliHistory::append(const std::string& command)
{
    const std::string entry = hobbycad::trim(command);
    if (entry.empty()) return;

    // Consecutive dedup: skip if identical to the last entry
    if (!m_entries.empty() && m_entries.back() == entry) {
        return;
    }

    m_entries.push_back(entry);
    trim();
}

void CliHistory::clear()
{
    m_entries.clear();
}

// ---- Persistence ----------------------------------------------------

std::string CliHistory::filePath() const
{
    const std::string dir  = configHome();
    const std::string base = std::string(AppDirName) + "/" + HistoryFileName;
    return dir.empty() ? base : dir + "/" + base;
}

bool CliHistory::load()
{
    namespace fs = std::filesystem;
    const std::string path = filePath();

    std::error_code ec;
    if (!fs::exists(path, ec)) {
        // No history yet: not an error
        return true;
    }

    std::ifstream in(path);
    if (!in) {
        return false;
    }

    m_entries.clear();

    std::string line;
    while (std::getline(in, line)) {
        // A file written on Windows keeps its carriage return when read
        // as text here; QTextStream used to drop it.
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) {
            m_entries.push_back(line);
        }
    }

    trim();
    return true;
}

bool CliHistory::save() const
{
    namespace fs = std::filesystem;
    const std::string path = filePath();

    // Ensure the directory exists
    const fs::path parent = fs::path(path).parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        fs::create_directories(parent, ec);
        if (ec && !fs::is_directory(parent)) {
            return false;
        }
    }

    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        return false;
    }

    for (const std::string& entry : m_entries) {
        out << entry << '\n';
    }

    return out.good();
}

// ---- Internal -------------------------------------------------------

void CliHistory::trim()
{
    while (static_cast<int>(m_entries.size()) > m_maxLines) {
        m_entries.erase(m_entries.begin());
    }
}

}  // namespace hobbycad
