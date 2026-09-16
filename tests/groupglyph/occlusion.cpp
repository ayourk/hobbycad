// =====================================================================
//  tests/groupglyph/occlusion.cpp — the group indicator's missing corner
//  Repository: ayourk/hobbycad
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//
//  drawGroupGlyph() draws a square and a circle that overlap, with the
//  CIRCLE WHOLE and the SQUARE CUT where the circle covers it, so the
//  square has no lower-right corner. That occlusion is the entire reason
//  the two shapes read as two objects with one in front rather than as
//  crossing line noise, and it is the first thing someone "tidying" the
//  glyph would undo, because a square missing a corner looks like a bug.
//
//  This test fails if that happens. Drawing both shapes complete inks the
//  corner; clipping the circle instead of the square breaks the arc.
//
// =====================================================================

#include "constraintglyphs.h"

#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QRectF>
#include <QtMath>

#include <cstdio>

namespace {

int g_failures = 0;

void check(bool ok, const char* what)
{
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_failures;
}

/// Is there any ink within `rad` pixels of (cx, cy)?
bool inkNear(const QImage& img, double cx, double cy, int rad)
{
    for (int y = int(cy) - rad; y <= int(cy) + rad; ++y) {
        for (int x = int(cx) - rad; x <= int(cx) + rad; ++x) {
            if (x < 0 || y < 0 || x >= img.width() || y >= img.height())
                continue;
            if (qAlpha(img.pixel(x, y)) > 60) return true;
        }
    }
    return false;
}

}  // namespace

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);

    // Big enough that a few pixels of antialiasing cannot decide a result.
    const int S = 200;
    QImage img(S, S, QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    {
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing, true);
        QPen pen(QColor(0, 120, 215), 3.0);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::MiterJoin);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        hobbycad::drawGroupGlyph(p, QRectF(0, 0, S, S));
    }

    // Same geometry the glyph uses, recomputed here so the test states the
    // contract independently rather than trusting the drawing code.
    const double inset = S * 0.20;
    const double boxL = inset, boxT = inset;
    const double s = S - 2 * inset;
    const double sqL = boxL + s * 0.036;
    const double sqT = boxT + s * 0.036;
    const double sqSide = s * 0.738;
    const double cx = boxL + s * 0.607;
    const double cy = boxT + s * 0.607;
    const double rad = s * 0.345;

    // --- The defining property -------------------------------------------
    check(!inkNear(img, sqL + sqSide, sqT + sqSide, 4),
          "square's lower-right corner is absent (occluded by the circle)");

    // --- The other three corners must still be there ---------------------
    check(inkNear(img, sqL, sqT, 4), "square's top-left corner is drawn");
    check(inkNear(img, sqL + sqSide, sqT, 4), "square's top-right corner is drawn");
    check(inkNear(img, sqL, sqT + sqSide, 4), "square's bottom-left corner is drawn");

    // --- The circle is whole, including where it crosses the square ------
    bool arcWhole = true;
    for (int deg = 0; deg < 360; deg += 15) {
        const double a = qDegreesToRadians(double(deg));
        if (!inkNear(img, cx + rad * qCos(a), cy + rad * qSin(a), 3)) {
            std::printf("        gap in circle at %d degrees\n", deg);
            arcWhole = false;
        }
    }
    check(arcWhole, "circle is unbroken all the way round");

    // --- Corner brackets, one sample per corner --------------------------
    const double arm = S * 0.22;
    check(inkNear(img, arm * 0.5, 0, 3), "top-left bracket present");
    check(inkNear(img, S - arm * 0.5, 0, 3), "top-right bracket present");
    check(inkNear(img, arm * 0.5, S, 3), "bottom-left bracket present");
    check(inkNear(img, S - arm * 0.5, S, 3), "bottom-right bracket present");

    // --- The middle of the box must NOT be a solid blob ------------------
    // Cheap guard against a fill leaking out of the path subtraction.
    check(!inkNear(img, sqL + sqSide * 0.25, sqT + sqSide * 0.25, 0),
          "square's interior is not filled");

    if (g_failures == 0) {
        std::printf("groupglyph occlusion: ALL PASS\n");
        return 0;
    }
    std::printf("groupglyph occlusion: %d FAILURE(S)\n", g_failures);
    return 1;
}
