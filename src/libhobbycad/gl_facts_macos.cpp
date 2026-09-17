// =====================================================================
//  src/libhobbycad/gl_facts_macos.cpp — GL driver facts on macOS
// =====================================================================
//
//  Built on macOS only (see CMakeLists.txt).
//
//  macOS has no user-installable GL drivers: OpenGL.framework loads a
//  renderer plug-in bundle per GPU (Intel, AMD and NVIDIA ones on Intel
//  Macs, a Metal-backed one on Apple silicon) and a software renderer as
//  the fallback. The images loaded into this process therefore say which
//  GPU path was taken. Beyond that: the model and CPU, the OS version,
//  whether the process runs under Rosetta, and whether it was started
//  over SSH, where it may have no access to the window server at all.
//  Only the dyld and sysctl C interfaces are used, so the library needs no
//  extra framework to link.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include <hobbycad/gl_diagnostics.h>

#include "gl_facts_common.h"

#include <set>
#include <string>

#include <mach-o/dyld.h>
#include <sys/sysctl.h>
#include <sys/types.h>

namespace hobbycad {
namespace gldiag {

namespace {

int sysctlInt(const char* name, int fallback)
{
    int value = 0;
    size_t len = sizeof value;
    if (sysctlbyname(name, &value, &len, nullptr, 0) != 0) return fallback;
    return value;
}

void collectLoadedImages(GlDriverFacts& f)
{
    // A bundle's binary is ".../Foo.bundle/Contents/MacOS/Foo"; a
    // framework's ".../OpenGL.framework/Versions/A/OpenGL". The last
    // component is the name either way.
    std::set<std::string> names;
    const uint32_t count = _dyld_image_count();
    for (uint32_t i = 0; i < count; ++i) {
        if (const char* path = _dyld_get_image_name(i)) detail::noteGlLibrary(names, path);
    }
    f.loadedLibraries.assign(names.begin(), names.end());
}

}  // namespace

GlDriverFacts gatherGlDriverFacts(const void*)
{
    GlDriverFacts f;
    const std::string os = detail::sysctlString("kern.osproductversion");
    const std::string machine = detail::sysctlString("hw.machine");
    f.platform = "macOS" + (os.empty() ? std::string() : " " + os)
               + (machine.empty() ? std::string() : " (" + machine + ")");
    f.hardwareModel = detail::sysctlString("hw.model");
    f.cpu = detail::sysctlString("machdep.cpu.brand_string");
    f.translated = sysctlInt("sysctl.proc_translated", 0) == 1;
    detail::collectEnvironment(f, {"QT_QPA_PLATFORM", "QT_MAC_WANTS_LAYER",
                                   "DYLD_LIBRARY_PATH", "DYLD_INSERT_LIBRARIES"});
    detail::collectSshSession(f);
    collectLoadedImages(f);
    f.adaptersKnown = false;
    f.adaptersNote = "the GPU is named by the renderer plug-in loaded below";
    return f;
}

}  // namespace gldiag
}  // namespace hobbycad
