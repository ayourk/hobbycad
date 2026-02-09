// =====================================================================
//  src/libhobbycad/hobbycad/opengl_info.h — OpenGL capability detection
// =====================================================================
//
//  Queries the system's OpenGL support without creating a visible
//  window.  Used by the startup dispatcher to decide between Full
//  Mode and Reduced Mode.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_OPENGL_INFO_H
#define HOBBYCAD_OPENGL_INFO_H

#include "core.h"

#include <QString>

namespace hobbycad {

/// Information gathered from an OpenGL context probe.
struct HOBBYCAD_EXPORT OpenGLInfo {
    bool    contextCreated = false;  ///< True if a context was created
    QString version;                 ///< GL_VERSION string
    QString glslVersion;             ///< GL_SHADING_LANGUAGE_VERSION
    QString renderer;                ///< GL_RENDERER string
    QString vendor;                  ///< GL_VENDOR string
    QString errorMessage;            ///< Error if context creation failed
    int     majorVersion = 0;        ///< Parsed major version number
    int     minorVersion = 0;        ///< Parsed minor version number

    /// True if the detected version meets the minimum (3.3).
    bool meetsMinimum() const {
        return contextCreated &&
               (majorVersion > 3 ||
                (majorVersion == 3 && minorVersion >= 3));
    }

    /// Human-readable summary for diagnostics.
    QString summary() const;
};

/// Probe the system for OpenGL capabilities.
///
/// Creates a temporary offscreen QOpenGLContext, queries GL strings,
/// and destroys it.  Does NOT require a visible window.
///
/// NOTE: A QApplication (or QGuiApplication) must exist before
/// calling this function.
HOBBYCAD_EXPORT OpenGLInfo probeOpenGL();

}  // namespace hobbycad

#endif  // HOBBYCAD_OPENGL_INFO_H

