// =====================================================================
//  src/libhobbycad/gl_facts_common.h — GL fact gathering shared by platforms
// =====================================================================
//
//  Internal to libhobbycad, not installed. Used by every GL fact gatherer
//  except Windows, which reads its environment through the Win32 API.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_GL_FACTS_COMMON_H
#define HOBBYCAD_GL_FACTS_COMMON_H

#include <hobbycad/gl_diagnostics.h>

#include <initializer_list>
#include <set>
#include <string>

namespace hobbycad {
namespace gldiag {
namespace detail {

/// Record each named environment variable that is set, in order.
void collectEnvironment(GlDriverFacts& f, std::initializer_list<const char*> names);

/// Mark the facts as an SSH session when SSH_CONNECTION is set.
void collectSshSession(GlDriverFacts& f);

/// Add the last component of `path` to `names` when it names a GL
/// component (classifyGlLibrary). A macOS bundle binary, ".../Foo.bundle/
/// Contents/MacOS/Foo", and a shared object both end in the name.
void noteGlLibrary(std::set<std::string>& names, const std::string& path);

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__DragonFly__)
/// A string sysctl, or "" when it does not exist.
std::string sysctlString(const char* name);
#endif

}  // namespace detail
}  // namespace gldiag
}  // namespace hobbycad

#endif  // HOBBYCAD_GL_FACTS_COMMON_H
