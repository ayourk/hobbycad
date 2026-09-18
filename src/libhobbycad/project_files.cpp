// =====================================================================
//  src/libhobbycad/project_files.cpp — the files in a project folder, and
//  its .gitignore
// =====================================================================
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include <hobbycad/project_files.h>

#include <hobbycad/translate.h>

#include <algorithm>

namespace hobbycad {

namespace {

/// `[...]` at `p[i]` against `c`; `i` moves past the class. False with `i`
/// unchanged when the class is not closed (the `[` is then a literal).
bool classMatch(const std::string& p, std::size_t& i, char c, bool& closed)
{
    std::size_t k = i + 1;
    bool negate = false;
    if (k < p.size() && (p[k] == '!' || p[k] == '^')) {
        negate = true;
        ++k;
    }
    bool matched = false;
    bool first = true;
    while (k < p.size() && (p[k] != ']' || first)) {
        first = false;
        char lo = p[k];
        if (lo == '\\' && k + 1 < p.size()) lo = p[++k];
        char hi = lo;
        if (k + 2 < p.size() && p[k + 1] == '-' && p[k + 2] != ']') {
            hi = p[k + 2];
            k += 2;
        }
        if (lo <= c && c <= hi) matched = true;
        ++k;
    }
    closed = k < p.size();
    if (!closed) return false;
    i = k + 1;
    return matched != negate;
}

bool globFrom(const std::string& p, std::size_t i, const std::string& t, std::size_t j)
{
    while (i < p.size()) {
        if (p.compare(i, 3, "**/") == 0) {
            // Zero or more whole directories.
            if (globFrom(p, i + 3, t, j)) return true;
            for (std::size_t k = j; k < t.size(); ++k) {
                if (t[k] == '/' && globFrom(p, i + 3, t, k + 1)) return true;
            }
            return false;
        }
        if (p.compare(i, 2, "**") == 0) {
            // Anything at all, across directories.
            for (std::size_t k = t.size() + 1; k-- > j;) {
                if (globFrom(p, i + 2, t, k)) return true;
            }
            return false;
        }
        const char c = p[i];
        if (c == '*') {
            for (std::size_t k = j;; ++k) {
                if (globFrom(p, i + 1, t, k)) return true;
                if (k >= t.size() || t[k] == '/') return false;
            }
        }
        if (j >= t.size()) return false;
        if (c == '?') {
            if (t[j] == '/') return false;
            ++i;
            ++j;
            continue;
        }
        if (c == '[') {
            bool closed = false;
            std::size_t at = i;
            const bool ok = classMatch(p, at, t[j], closed);
            if (closed) {
                if (!ok || t[j] == '/') return false;
                i = at;
                ++j;
                continue;
            }
        }
        char literal = c;
        if (c == '\\' && i + 1 < p.size()) literal = p[++i];
        if (t[j] != literal) return false;
        ++i;
        ++j;
    }
    return j == t.size();
}

/// One pattern line, read the way git reads it.
struct Rule {
    std::string glob;
    bool negate = false;
    bool dirOnly = false;
    bool anchored = false;   ///< relative to the project folder
};

/// The rule a line stands for, or false for a blank or a comment.
bool ruleOf(const std::string& line, Rule& r)
{
    std::string s = line;
    if (!s.empty() && s.back() == '\r') s.pop_back();
    // Trailing spaces go unless escaped.
    while (!s.empty() && s.back() == ' ' && !(s.size() >= 2 && s[s.size() - 2] == '\\')) {
        s.pop_back();
    }
    if (s.empty() || s[0] == '#') return false;
    r = Rule();
    if (s[0] == '!') {
        r.negate = true;
        s.erase(0, 1);
    } else if (s[0] == '\\' && s.size() > 1 && (s[1] == '#' || s[1] == '!')) {
        s.erase(0, 1);
    }
    if (!s.empty() && s.back() == '/') {
        r.dirOnly = true;
        s.pop_back();
    }
    if (s.empty()) return false;
    r.anchored = s.find('/') != std::string::npos;
    if (s[0] == '/') s.erase(0, 1);
    r.glob = s;
    return true;
}

bool ruleMatches(const Rule& r, const std::string& path, bool isDirectory)
{
    if (r.dirOnly && !isDirectory) return false;
    if (r.anchored) return gitGlobMatch(r.glob, path);
    const std::size_t slash = path.rfind('/');
    const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    return gitGlobMatch(r.glob, name);
}

std::vector<std::string> splitLines(const std::string& text)
{
    std::vector<std::string> lines;
    std::string line;
    for (char c : text) {
        if (c == '\n') {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(line);
            line.clear();
        } else {
            line.push_back(c);
        }
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

std::string normalized(std::string path)
{
    std::replace(path.begin(), path.end(), '\\', '/');
    while (!path.empty() && path.back() == '/') path.pop_back();
    while (path.rfind("./", 0) == 0) path.erase(0, 2);
    return path;
}

/// The line that stands for exactly `path`, as ignore() writes it.
std::string lineFor(const std::string& path, bool isDirectory)
{
    std::string line = "/";
    for (char c : path) {
        // Characters a pattern would read specially are escaped.
        if (c == '*' || c == '?' || c == '[' || c == '\\') line.push_back('\\');
        line.push_back(c);
    }
    if (isDirectory) line.push_back('/');
    return line;
}

}  // namespace

bool gitGlobMatch(const std::string& pattern, const std::string& path)
{
    return globFrom(pattern, 0, path, 0);
}

GitIgnore GitIgnore::parse(const std::string& text)
{
    GitIgnore g;
    g.m_lines = splitLines(text);
    return g;
}

std::string GitIgnore::text() const
{
    std::string out;
    for (const std::string& line : m_lines) {
        out += line;
        out += '\n';
    }
    return out;
}

bool GitIgnore::ignores(const std::string& rawPath, bool isDirectory) const
{
    const std::string path = normalized(rawPath);
    if (path.empty()) return false;
    std::vector<Rule> rules;
    for (const std::string& line : m_lines) {
        Rule r;
        if (ruleOf(line, r)) rules.push_back(r);
    }
    const auto decide = [&rules](const std::string& p, bool dir) {
        bool ignored = false;
        for (const Rule& r : rules) {
            if (ruleMatches(r, p, dir)) ignored = !r.negate;
        }
        return ignored;
    };
    // A folder git leaves out takes everything under it along.
    for (std::size_t slash = path.find('/'); slash != std::string::npos;
         slash = path.find('/', slash + 1)) {
        if (decide(path.substr(0, slash), true)) return true;
    }
    return decide(path, isDirectory);
}

bool GitIgnore::insideIgnoredFolder(const std::string& rawPath) const
{
    const std::string path = normalized(rawPath);
    for (std::size_t slash = path.find('/'); slash != std::string::npos;
         slash = path.find('/', slash + 1)) {
        if (ignores(path.substr(0, slash), true)) return true;
    }
    return false;
}

std::vector<std::string> GitIgnore::patterns() const
{
    std::vector<std::string> out;
    for (const std::string& line : m_lines) {
        Rule r;
        if (ruleOf(line, r)) out.push_back(line);
    }
    return out;
}

bool GitIgnore::lists(const std::string& pattern) const
{
    const std::vector<std::string> all = patterns();
    return std::find(all.begin(), all.end(), pattern) != all.end();
}

bool GitIgnore::ignore(const std::string& rawPath, bool isDirectory)
{
    const std::string path = normalized(rawPath);
    if (path.empty()) return false;
    const std::string negation = "!" + lineFor(path, isDirectory);
    const auto before = m_lines.size();
    m_lines.erase(std::remove(m_lines.begin(), m_lines.end(), negation), m_lines.end());
    if (ignores(path, isDirectory)) return m_lines.size() != before;
    m_lines.push_back(lineFor(path, isDirectory));
    return true;
}

bool GitIgnore::unignore(const std::string& rawPath, bool isDirectory)
{
    const std::string path = normalized(rawPath);
    if (path.empty() || !ignores(path, isDirectory) || insideIgnoredFolder(path)) return false;
    // Its own lines, however they were spelled.
    std::vector<std::string> kept;
    for (const std::string& line : m_lines) {
        Rule r;
        const bool own = ruleOf(line, r) && !r.negate
                      && (normalized(line) == path || line == lineFor(path, isDirectory)
                          || line == lineFor(path, !isDirectory));
        if (!own) kept.push_back(line);
    }
    m_lines = kept;
    if (!ignores(path, isDirectory)) return true;
    m_lines.push_back("!" + lineFor(path, isDirectory));
    return !ignores(path, isDirectory);
}

bool isForeignPath(const std::string& path, const std::vector<std::string>& foreignFiles)
{
    for (const std::string& f : foreignFiles) {
        if (f == path) return true;
        if (!f.empty() && f.back() == '/' && path.compare(0, f.size(), f) == 0) return true;
    }
    return false;
}

ProjectFileStatus projectFileStatus(const std::string& path, bool isDirectory,
                                    const std::set<std::string>& cadFiles,
                                    const std::vector<std::string>& foreignFiles,
                                    const GitIgnore& gitIgnore)
{
    if (path.empty()) return ProjectFileStatus::Untracked;
    if (cadFiles.count(path)) return ProjectFileStatus::CadFile;
    if (isForeignPath(path, foreignFiles)) return ProjectFileStatus::ForeignFile;
    if (gitIgnore.ignores(path, isDirectory)) return ProjectFileStatus::GitIgnored;
    return ProjectFileStatus::Untracked;
}

const char* projectFileStatusText(ProjectFileStatus status)
{
    switch (status) {
    case ProjectFileStatus::CadFile:
        return HOBBYCAD_TRANSLATE_NOOP("hobbycad::ProjectBrowserWidget", "CAD File (in manifest)");
    case ProjectFileStatus::ForeignFile:
        return HOBBYCAD_TRANSLATE_NOOP("hobbycad::ProjectBrowserWidget",
                                       "Foreign File (tracked separately)");
    case ProjectFileStatus::GitIgnored:
        return HOBBYCAD_TRANSLATE_NOOP("hobbycad::ProjectBrowserWidget", "Git Ignored");
    case ProjectFileStatus::Untracked:
        break;
    }
    return HOBBYCAD_TRANSLATE_NOOP("hobbycad::ProjectBrowserWidget",
                                   "Untracked (not in manifest)");
}

}  // namespace hobbycad
