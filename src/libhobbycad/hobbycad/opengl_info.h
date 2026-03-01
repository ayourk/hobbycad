// =====================================================================
//  src/libhobbycad/hobbycad/opengl_info.h — OpenGL capability detection
// =====================================================================
//
//  Queries the system's OpenGL support without creating a visible
//  window.  Used by the startup dispatcher to decide between Full
//  Mode and Reduced Mode.
//
//  Qt path:     Uses QOpenGLContext + QOffscreenSurface.
//  Non-Qt path: Uses EGL if available, otherwise returns stub data.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_OPENGL_INFO_H
#define HOBBYCAD_OPENGL_INFO_H

#include "core.h"
#include "types.h"

#include <string>

namespace hobbycad {

/// Information gathered from an OpenGL context probe.
struct HOBBYCAD_EXPORT OpenGLInfo {
    bool    contextCreated = false;  ///< True if a context was created
    std::string version;             ///< GL_VERSION string
    std::string glslVersion;         ///< GL_SHADING_LANGUAGE_VERSION
    std::string renderer;            ///< GL_RENDERER string
    std::string vendor;              ///< GL_VENDOR string
    std::string errorMessage;        ///< Error if context creation failed
    int     majorVersion = 0;        ///< Parsed major version number
    int     minorVersion = 0;        ///< Parsed minor version number

    /// True if the detected version meets the minimum (3.3).
    bool meetsMinimum() const {
        return contextCreated &&
               (majorVersion > 3 ||
                (majorVersion == 3 && minorVersion >= 3));
    }

    /// Human-readable summary for diagnostics.
    std::string summary() const;
};

/// Probe the system for OpenGL capabilities.
///
/// Qt path: Creates a temporary offscreen QOpenGLContext, queries GL
/// strings, and destroys it.  Does NOT require a visible window.
/// NOTE: A QApplication (or QGuiApplication) must exist before calling
/// this function when using the Qt path.
///
/// Non-Qt path: Uses EGL for headless probing if available, otherwise
/// returns a stub with errorMessage set.
HOBBYCAD_EXPORT OpenGLInfo probeOpenGL();

}  // namespace hobbycad

#endif  // HOBBYCAD_OPENGL_INFO_H
