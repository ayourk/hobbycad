// =====================================================================
//  src/libhobbycad/gl_facts_common.cpp — GL fact gathering shared by platforms
// =====================================================================
//
//  Built with every gatherer except Windows (see CMakeLists.txt).
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include "gl_facts_common.h"

#include <cstdlib>
#include <cstring>

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__DragonFly__)
#  include <sys/types.h>
#  include <sys/sysctl.h>
#endif

namespace hobbycad {
namespace gldiag {
namespace detail {

void collectEnvironment(GlDriverFacts& f, std::initializer_list<const char*> names)
{
    for (const char* n : names) {
        const char* v = std::getenv(n);
        if (v && *v) f.environment.emplace_back(n, v);
    }
}

void collectSshSession(GlDriverFacts& f)
{
    const char* ssh = std::getenv("SSH_CONNECTION");
    if (ssh && *ssh) {
        f.remoteSession = true;
        f.remoteSessionKind = "SSH";
    }
}

void noteGlLibrary(std::set<std::string>& names, const std::string& path)
{
    const size_t slash = path.find_last_of('/');
    const std::string base = (slash == std::string::npos) ? path : path.substr(slash + 1);
    if (classifyGlLibrary(base).role != GlLibrary::Role::Unrelated) names.insert(base);
}

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__DragonFly__)
std::string sysctlString(const char* name)
{
    size_t len = 0;
    if (sysctlbyname(name, nullptr, &len, nullptr, 0) != 0 || len == 0) return std::string();
    std::string buf(len, '\0');
    if (sysctlbyname(name, &buf[0], &len, nullptr, 0) != 0) return std::string();
    buf.resize(std::strlen(buf.c_str()));
    return buf;
}
#endif

}  // namespace detail
}  // namespace gldiag
}  // namespace hobbycad
