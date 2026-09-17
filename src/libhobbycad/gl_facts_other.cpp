// =====================================================================
//  src/libhobbycad/gl_facts_other.cpp — GL driver facts, fallback
// =====================================================================
//
//  Built where no platform gatherer applies (see CMakeLists.txt). It
//  reports only what is portable, so the report still says that nothing
//  more could be learned rather than failing to build.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include <hobbycad/gl_diagnostics.h>

#include "gl_facts_common.h"

namespace hobbycad {
namespace gldiag {

GlDriverFacts gatherGlDriverFacts(const void*)
{
    GlDriverFacts f;
    f.platform = "this platform";
    detail::collectEnvironment(f, {"DISPLAY", "WAYLAND_DISPLAY", "QT_QPA_PLATFORM"});
    f.adaptersNote = "no driver information is gathered on this platform";
    return f;
}

}  // namespace gldiag
}  // namespace hobbycad
