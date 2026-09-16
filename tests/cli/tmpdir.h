// =====================================================================
//  tests/cli/tmpdir.h — a temporary directory that cleans up after itself
// =====================================================================
//  SPDX-License-Identifier: GPL-3.0-only
//
//  What QTemporaryDir gave these tests, without Qt: a unique directory
//  that is removed when the object goes out of scope. Without the
//  removal a failing run would leave a project tree behind every time,
//  which is how a machine fills up quietly.
//
//  mkdtemp is POSIX. The suites are run by the Linux workflow and the
//  Debian package build, and MSYS2 (the documented Windows developer
//  path) has it; MSVC does not, so a suite runner there would need
//  another way to make the directory.
// =====================================================================

#ifndef HOBBYCAD_TESTS_TMPDIR_H
#define HOBBYCAD_TESTS_TMPDIR_H

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include <unistd.h>

namespace hobbycad {
namespace test {

class TempDir {
public:
    TempDir()
    {
        std::string pattern =
            (std::filesystem::temp_directory_path() / "hobbycad-test-XXXXXX").string();
        std::vector<char> buf(pattern.begin(), pattern.end());
        buf.push_back('\0');
        const char* made = ::mkdtemp(buf.data());
        if (made) m_path = made;
    }

    ~TempDir()
    {
        if (m_path.empty()) return;
        std::error_code ec;
        std::filesystem::remove_all(m_path, ec);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    /// True when the directory was created; a test must not proceed
    /// without one, or it would write into the current directory.
    bool isValid() const { return !m_path.empty(); }

    const std::string& path() const { return m_path; }

private:
    std::string m_path;
};

}  // namespace test
}  // namespace hobbycad

#endif  // HOBBYCAD_TESTS_TMPDIR_H
