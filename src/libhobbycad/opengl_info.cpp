// =====================================================================
//  src/libhobbycad/opengl_info.cpp — OpenGL capability detection
// =====================================================================

#include "hobbycad/opengl_info.h"

#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QSurfaceFormat>

namespace hobbycad {

QString OpenGLInfo::summary() const
{
    QString text;
    text += QStringLiteral("OpenGL Version:    ") +
            (version.isEmpty() ? QStringLiteral("N/A") : version) +
            QStringLiteral("\n");
    text += QStringLiteral("GLSL Version:      ") +
            (glslVersion.isEmpty() ? QStringLiteral("N/A") : glslVersion) +
            QStringLiteral("\n");
    text += QStringLiteral("Renderer:          ") +
            (renderer.isEmpty() ? QStringLiteral("N/A") : renderer) +
            QStringLiteral("\n");
    text += QStringLiteral("Vendor:            ") +
            (vendor.isEmpty() ? QStringLiteral("N/A") : vendor) +
            QStringLiteral("\n");
    text += QStringLiteral("Context Creation:  ");

    if (contextCreated) {
        text += QStringLiteral("success");
    } else {
        text += QStringLiteral("failed");
        if (!errorMessage.isEmpty()) {
            text += QStringLiteral(" — ") + errorMessage;
        }
    }
    text += QStringLiteral("\n");

    return text;
}

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
        info.errorMessage   = QStringLiteral(
            "Failed to create OpenGL 3.3 Core context");

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
                info.version     = reinterpret_cast<const char*>(
                                       gl->glGetString(GL_VERSION));
                info.renderer    = reinterpret_cast<const char*>(
                                       gl->glGetString(GL_RENDERER));
                info.vendor      = reinterpret_cast<const char*>(
                                       gl->glGetString(GL_VENDOR));
                info.glslVersion = reinterpret_cast<const char*>(
                                       gl->glGetString(GL_SHADING_LANGUAGE_VERSION));

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
        info.version     = reinterpret_cast<const char*>(
                               gl->glGetString(GL_VERSION));
        info.renderer    = reinterpret_cast<const char*>(
                               gl->glGetString(GL_RENDERER));
        info.vendor      = reinterpret_cast<const char*>(
                               gl->glGetString(GL_VENDOR));
        info.glslVersion = reinterpret_cast<const char*>(
                               gl->glGetString(GL_SHADING_LANGUAGE_VERSION));

        auto actualFmt = ctx.format();
        info.majorVersion = actualFmt.majorVersion();
        info.minorVersion = actualFmt.minorVersion();

        ctx.doneCurrent();
    } else {
        info.errorMessage = QStringLiteral(
            "Context created but makeCurrent() failed");
    }

    surface.destroy();
    return info;
}

}  // namespace hobbycad

