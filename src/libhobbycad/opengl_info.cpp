// =====================================================================
//  src/libhobbycad/opengl_info.cpp — OpenGL capability detection
// =====================================================================

#include "hobbycad/opengl_info.h"

#include <algorithm>
#include <cstdio>
#include <string>

#if HOBBYCAD_HAS_QT
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QSurfaceFormat>
#elif HOBBYCAD_HAS_EGL
#include <EGL/egl.h>
#include <GL/gl.h>
#endif

namespace hobbycad {

// =====================================================================
//  summary() — always compiled (no Qt dependency)
// =====================================================================

// Format a label + value pair, wrapping long values at maxWidth with
// continuation lines indented to the value column.
static std::string formatField(const std::string& label, const std::string& value,
                               int labelWidth = 19, int maxWidth = 80)
{
    std::string padded = label;
    if (static_cast<int>(padded.size()) < labelWidth)
        padded.append(static_cast<size_t>(labelWidth) - padded.size(), ' ');

    int valueWidth = maxWidth - labelWidth;
    if (valueWidth <= 0 || static_cast<int>(value.size()) <= valueWidth) {
        return padded + value + "\n";
    }

    std::string result;
    std::string indent(static_cast<size_t>(labelWidth), ' ');
    int pos = 0;
    bool first = true;
    while (pos < static_cast<int>(value.size())) {
        int chunkLen = std::min(valueWidth, static_cast<int>(value.size()) - pos);

        // Try to break at a word boundary (space, slash, or comma)
        if (pos + chunkLen < static_cast<int>(value.size())) {
            int breakAt = -1;
            for (int i = pos + chunkLen - 1; i > pos; --i) {
                char ch = value[static_cast<size_t>(i)];
                if (ch == ' ' || ch == '/' || ch == ',') {
                    breakAt = i + 1;
                    break;
                }
            }
            if (breakAt > pos) chunkLen = breakAt - pos;
        }

        result += (first ? padded : indent) +
                  value.substr(static_cast<size_t>(pos), static_cast<size_t>(chunkLen)) + "\n";
        pos += chunkLen;
        first = false;
    }
    return result;
}

std::string OpenGLInfo::summary() const
{
    std::string text;
    text += formatField("OpenGL Version:", version.empty() ? "N/A" : version);
    text += formatField("GLSL Version:", glslVersion.empty() ? "N/A" : glslVersion);
    text += formatField("Renderer:", renderer.empty() ? "N/A" : renderer);
    text += formatField("Vendor:", vendor.empty() ? "N/A" : vendor);

    std::string status;
    if (contextCreated) {
        status = "success";
    } else {
        status = "failed";
        if (!errorMessage.empty())
            status += " \xe2\x80\x94 " + errorMessage;  // em dash (UTF-8)
    }
    text += formatField("Context Creation:", status);

    return text;
}

// =====================================================================
//  probeOpenGL() — Qt path
// =====================================================================

#if HOBBYCAD_HAS_QT

OpenGLInfo probeOpenGL()
{
    OpenGLInfo info;

    // Request a 3.3 Core profile context
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setRenderableType(QSurfaceFormat::OpenGL);

    QOpenGLContext ctx;
    ctx.setFormat(fmt);

    if (!ctx.create()) {
        info.contextCreated = false;
        info.errorMessage   = "Failed to create OpenGL 3.3 Core context";

        // Try again without version constraint to gather diagnostics
        QSurfaceFormat fallbackFmt;
        fallbackFmt.setRenderableType(QSurfaceFormat::OpenGL);
        QOpenGLContext fallbackCtx;
        fallbackCtx.setFormat(fallbackFmt);

        if (fallbackCtx.create()) {
            QOffscreenSurface surface;
            surface.setFormat(fallbackCtx.format());
            surface.create();

            if (fallbackCtx.makeCurrent(&surface)) {
                auto* gl = fallbackCtx.functions();
                const char* str;
                str = reinterpret_cast<const char*>(gl->glGetString(GL_VERSION));
                if (str) info.version = str;
                str = reinterpret_cast<const char*>(gl->glGetString(GL_RENDERER));
                if (str) info.renderer = str;
                str = reinterpret_cast<const char*>(gl->glGetString(GL_VENDOR));
                if (str) info.vendor = str;
                str = reinterpret_cast<const char*>(gl->glGetString(GL_SHADING_LANGUAGE_VERSION));
                if (str) info.glslVersion = str;

                // Parse version from fallback context
                auto actualFmt = fallbackCtx.format();
                info.majorVersion = actualFmt.majorVersion();
                info.minorVersion = actualFmt.minorVersion();

                fallbackCtx.doneCurrent();
            }
            surface.destroy();
        }
        return info;
    }

    // Context created successfully
    info.contextCreated = true;

    QOffscreenSurface surface;
    surface.setFormat(ctx.format());
    surface.create();

    if (ctx.makeCurrent(&surface)) {
        auto* gl = ctx.functions();
        const char* str;
        str = reinterpret_cast<const char*>(gl->glGetString(GL_VERSION));
        if (str) info.version = str;
        str = reinterpret_cast<const char*>(gl->glGetString(GL_RENDERER));
        if (str) info.renderer = str;
        str = reinterpret_cast<const char*>(gl->glGetString(GL_VENDOR));
        if (str) info.vendor = str;
        str = reinterpret_cast<const char*>(gl->glGetString(GL_SHADING_LANGUAGE_VERSION));
        if (str) info.glslVersion = str;

        auto actualFmt = ctx.format();
        info.majorVersion = actualFmt.majorVersion();
        info.minorVersion = actualFmt.minorVersion();

        ctx.doneCurrent();
    } else {
        info.errorMessage = "Context created but makeCurrent() failed";
    }

    surface.destroy();
    return info;
}

// =====================================================================
//  probeOpenGL() — non-Qt path (EGL or stub)
// =====================================================================

#elif HOBBYCAD_HAS_EGL

OpenGLInfo probeOpenGL()
{
    OpenGLInfo info;

    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY) {
        info.errorMessage = "EGL: No display available";
        return info;
    }

    EGLint major, minor;
    if (!eglInitialize(display, &major, &minor)) {
        info.errorMessage = "EGL: Initialization failed";
        return info;
    }

    // Request desktop OpenGL (not OpenGL ES)
    if (!eglBindAPI(EGL_OPENGL_API)) {
        info.errorMessage = "EGL: Desktop OpenGL API not supported";
        eglTerminate(display);
        return info;
    }

    // Choose a config that supports pbuffer rendering
    EGLint configAttribs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_NONE
    };
    EGLConfig config;
    EGLint numConfigs;
    if (!eglChooseConfig(display, configAttribs, &config, 1, &numConfigs) ||
        numConfigs == 0) {
        info.errorMessage = "EGL: No suitable config found";
        eglTerminate(display);
        return info;
    }

    // Create a 1x1 pbuffer surface for the context
    EGLint pbufferAttribs[] = { EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE };
    EGLSurface surface = eglCreatePbufferSurface(display, config, pbufferAttribs);

    // First try: request a 3.3 Core context
    EGLint contextAttribs[] = {
        EGL_CONTEXT_MAJOR_VERSION, 3,
        EGL_CONTEXT_MINOR_VERSION, 3,
        EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
        EGL_NONE
    };
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttribs);

    if (context == EGL_NO_CONTEXT) {
        info.contextCreated = false;
        info.errorMessage = "Failed to create OpenGL 3.3 Core context";

        // Fallback: try unconstrained context for diagnostics
        EGLint fallbackAttribs[] = { EGL_NONE };
        context = eglCreateContext(display, config, EGL_NO_CONTEXT, fallbackAttribs);
    } else {
        info.contextCreated = true;
    }

    if (context != EGL_NO_CONTEXT) {
        eglMakeCurrent(display, surface, surface, context);

        auto getString = [](GLenum name) -> std::string {
            const char* s = reinterpret_cast<const char*>(glGetString(name));
            return s ? s : "";
        };

        info.version = getString(GL_VERSION);
        info.renderer = getString(GL_RENDERER);
        info.vendor = getString(GL_VENDOR);
        info.glslVersion = getString(GL_SHADING_LANGUAGE_VERSION);

        // Parse major.minor from the version string
        if (!info.version.empty()) {
            std::sscanf(info.version.c_str(), "%d.%d",
                        &info.majorVersion, &info.minorVersion);
        }

        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroyContext(display, context);
    }

    if (surface != EGL_NO_SURFACE)
        eglDestroySurface(display, surface);
    eglTerminate(display);

    return info;
}

#else  // No Qt or EGL — return stub

OpenGLInfo probeOpenGL()
{
    OpenGLInfo info;
    info.errorMessage = "OpenGL probing not available (no Qt or EGL)";
    return info;
}

#endif  // HOBBYCAD_HAS_QT / HOBBYCAD_HAS_EGL

}  // namespace hobbycad
