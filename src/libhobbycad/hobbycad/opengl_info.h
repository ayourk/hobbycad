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

    /// True when no 3.3 Core context could be created and the version above
    /// came from an unconstrained (compatibility) context instead. Not a
    /// failure (OCCT owns its own context and accepts compatibility), but
    /// worth surfacing in diagnostics.
    bool    usedCompatibilityProfile = false;

    /// True when OCCT itself was asked whether it can bring up a viewer, and
    /// answered. `occtViewerWorks` is meaningless unless this is set.
    bool    occtViewerProbed = false;
    /// The authoritative answer: OCCT successfully created a graphic driver
    /// and initialized a GL context. The version numbers above are only a
    /// proxy for this.
    bool    occtViewerWorks = false;

    /// Whether the version reported by the probe reaches 3.3.
    ///
    /// **This is not the viewport gate**; see canRunViewport(). It is a
    /// plain fact about what the GL probe read, kept because it is useful
    /// in diagnostics and because a library consumer may want the version
    /// test on its own terms. Deciding anything with it is what the OCCT
    /// probe exists to stop.
    bool meetsMinimum() const {
        return contextCreated &&
               (majorVersion > 3 ||
                (majorVersion == 3 && minorVersion >= 3));
    }

    /// Whether the 3D viewport should be attempted.
    ///
    /// OCCT is the component that has to succeed, so **its answer is the
    /// answer**: there is no version fallback. Aaron, 2026-08-27: *"If
    /// OCCT won't work, that gives the best answer."*
    ///
    /// The fallback that used to sit here was already unreachable:
    /// probeOcctViewer() is compiled unconditionally and every path through
    /// it, both catch blocks included, records a verdict. So the GL version
    /// never actually decided anything; it only looked as though it might,
    /// which is worse than not being there.
    ///
    /// An unprobed info therefore answers "no". Guessing from a version
    /// string is exactly the proxy the OCCT probe replaced, and degrading to
    /// Reduced Mode is the safe direction: the 2D workspace works, whereas
    /// attempting a viewport that cannot initialize does not.
    bool canRunViewport() const {
        return occtViewerProbed && occtViewerWorks;
    }

    /// Human-readable summary for diagnostics.
    std::string summary() const;
};

/// Ask OCCT directly whether it can initialize a GL context, and record the
/// answer in `info`. This is the authoritative test; probeOpenGL()'s version
/// numbers are only a proxy for it. Never throws.
void probeOcctViewer(OpenGLInfo& info);

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
