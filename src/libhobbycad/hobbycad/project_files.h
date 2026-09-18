// =====================================================================
//  src/libhobbycad/hobbycad/project_files.h — the files in a project
//  folder, and its .gitignore
// =====================================================================
//
//  Capability tier of the front-end support layer. What a file in a
//  project folder is to the project (one of its CAD files, a foreign file
//  it keeps, ignored by git, or none of these), and the project's
//  .gitignore as a document: read, matched the way git matches it, and
//  edited without losing the comments, blank lines and order a person
//  wrote.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_PROJECT_FILES_H
#define HOBBYCAD_PROJECT_FILES_H

#include "core.h"

#include <set>
#include <string>
#include <vector>

namespace hobbycad {

/// True when `path` (relative, '/' separated) matches the gitignore glob
/// `pattern` as a whole: `*` and `?` stay within one directory, `[...]` is
/// a character class, `**` crosses directories, and `\` escapes.
HOBBYCAD_EXPORT bool gitGlobMatch(const std::string& pattern, const std::string& path);

/// A .gitignore file, kept line for line.
///
/// Matching follows git: blank lines and `#` comments match nothing; `!`
/// re-includes; a pattern ending in `/` matches directories only; a pattern
/// with a `/` other than at its end is relative to the project folder,
/// otherwise it matches a name at any depth; the last matching line wins;
/// and nothing under an ignored directory can be re-included.
class HOBBYCAD_EXPORT GitIgnore {
public:
    GitIgnore() = default;
    /// The file's text as read (any line ending).
    static GitIgnore parse(const std::string& text);
    /// The text to write: every line as it was, in order, with the edits.
    std::string text() const;
    bool empty() const { return m_lines.empty(); }

    /// True when git ignores `path` (relative to the project folder).
    bool ignores(const std::string& path, bool isDirectory) const;
    /// True when a folder above `path` is ignored, so nothing can take
    /// `path` back.
    bool insideIgnoredFolder(const std::string& path) const;

    /// The pattern lines, without comments and blanks.
    std::vector<std::string> patterns() const;
    /// True when `pattern` is one of the pattern lines.
    bool lists(const std::string& pattern) const;

    /// Ignore `path`: a line for it at the end, unless git already ignores
    /// it. Any line re-including exactly it is removed first.
    bool ignore(const std::string& path, bool isDirectory);
    /// Stop ignoring `path`: its own lines go; when a broader pattern still
    /// ignores it, a `!` line re-includes it. Refused, changing nothing,
    /// when a folder above it is ignored.
    bool unignore(const std::string& path, bool isDirectory);

private:
    std::vector<std::string> m_lines;
};

/// What a file in the project folder is to the project.
enum class ProjectFileStatus {
    CadFile,       ///< listed in the project's manifest
    ForeignFile,   ///< kept by the project though not its own
    GitIgnored,    ///< ignored by git
    Untracked,     ///< none of these
};

/// True when `path` is one of `foreignFiles`, or inside one of them that
/// names a folder (ending in '/').
HOBBYCAD_EXPORT bool isForeignPath(const std::string& path,
                                   const std::vector<std::string>& foreignFiles);

/// The status of `path`, in that order of precedence.
HOBBYCAD_EXPORT ProjectFileStatus projectFileStatus(const std::string& path, bool isDirectory,
                                                    const std::set<std::string>& cadFiles,
                                                    const std::vector<std::string>& foreignFiles,
                                                    const GitIgnore& gitIgnore);

/// The status's description, untranslated (context "hobbycad::ProjectBrowserWidget").
HOBBYCAD_EXPORT const char* projectFileStatusText(ProjectFileStatus status);

}  // namespace hobbycad

#endif  // HOBBYCAD_PROJECT_FILES_H
