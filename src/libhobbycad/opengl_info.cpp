// =====================================================================
//  src/libhobbycad/opengl_info.cpp — OpenGL capability detection
// =====================================================================

#include "hobbycad/opengl_info.h"

#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QSurfaceFormat>

namespace hobbycad {

// Format a label + value pair, wrapping long values at maxWidth with
// continuation lines indented to the value column.
static QString formatField(const QString& label, const QString& value,
                           int labelWidth = 19, int maxWidth = 80)
{
    QString padded = label.leftJustified(labelWidth);
    int valueWidth = maxWidth - labelWidth;
    if (valueWidth <= 0 || value.length() <= valueWidth) {
        return padded + value + QStringLiteral("\n");
    }

    QString result;
    QString indent = QString(labelWidth, QLatin1Char(' '));
    int pos = 0;
    bool first = true;
    while (pos < value.length()) {
        int chunkLen = qMin(valueWidth, value.length() - pos);

        // Try to break at a word boundary (space or slash)
        if (pos + chunkLen < value.length()) {
            int breakAt = -1;
            for (int i = pos + chunkLen - 1; i > pos; --i) {
                QChar ch = value[i];
                if (ch == QLatin1Char(' ') || ch == QLatin1Char('/') ||
                    ch == QLatin1Char(',')) {
                    breakAt = i + 1;
                    break;
                }
            }
            if (breakAt > pos) chunkLen = breakAt - pos;
        }

        result += (first ? padded : indent) +
                  value.mid(pos, chunkLen) + QStringLiteral("\n");
        pos += chunkLen;
        first = false;
    }
    return result;
}

QString OpenGLInfo::summary() const
{
    QString text;
    text += formatField(QStringLiteral("OpenGL Version:"),
                        version.isEmpty() ? QStringLiteral("N/A") : version);
    text += formatField(QStringLiteral("GLSL Version:"),
                        glslVersion.isEmpty() ? QStringLiteral("N/A") : glslVersion);
    text += formatField(QStringLiteral("Renderer:"),
                        renderer.isEmpty() ? QStringLiteral("N/A") : renderer);
    text += formatField(QStringLiteral("Vendor:"),
                        vendor.isEmpty() ? QStringLiteral("N/A") : vendor);

    QString status;
    if (contextCreated) {
        status = QStringLiteral("success");
    } else {
        status = QStringLiteral("failed");
        if (!errorMessage.isEmpty())
            status += QStringLiteral(" — ") + errorMessage;
    }
    text += formatField(QStringLiteral("Context Creation:"), status);

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

