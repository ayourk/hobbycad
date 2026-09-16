// The transform pivot glyph: a six-pointed star, and the hemisphere arrow
// over it for rotation. Rendered offscreen and sampled, Qt only.
#include "constraintglyphs.h"
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QtMath>
#include <cstdio>

static int g_failures = 0;
static void check(bool ok, const char* what) { std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what); if (!ok) ++g_failures; }
static bool inkNear(const QImage& img, double cx, double cy, int rad) {
    for (int dy = -rad; dy <= rad; ++dy) for (int dx = -rad; dx <= rad; ++dx) {
        const int x = int(cx) + dx, y = int(cy) + dy;
        if (x < 0 || y < 0 || x >= img.width() || y >= img.height()) continue;
        if (qAlpha(img.pixel(x, y)) > 60) return true;
    }
    return false;
}
static QImage render(bool arrow) {
    const int S = 200;
    QImage img(S, S, QImage::Format_ARGB32); img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(QColor(255, 200, 0), 3)); p.setBrush(QColor(255, 200, 0));
    hobbycad::drawPivotGlyph(p, QRectF(0, 0, S, S), arrow);
    return img;
}
static QPointF P(double deg, double rad, double c = 100) { const double a = qDegreesToRadians(deg); return {c + rad * qCos(a), c - rad * qSin(a)}; }

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    const double S = 200;
    {
        QImage img = render(false);
        bool tips = true, gaps = true;
        for (int k = 0; k < 6; ++k) { QPointF t = P(90 + 60 * k, 0.44 * S); if (!inkNear(img, t.x(), t.y(), 3)) tips = false; }
        for (int k = 0; k < 6; ++k) { QPointF g = P(120 + 60 * k, 0.44 * S); if (inkNear(img, g.x(), g.y(), 3)) gaps = false; }
        check(tips, "bare star: a point straight up and every 60 degrees");
        check(gaps, "bare star: empty between the points at the tip radius (not a hexagon or disc)");
        QPointF a = P(300, 0.44 * S); check(!inkNear(img, a.x(), a.y(), 3), "bare star: no arc at the arc radius (sampled between two points)");
        check(!inkNear(img, 100 + 0.44 * S, 100 + 0.14 * S, 4) && !inkNear(img, 100 - 0.44 * S, 100 + 0.14 * S, 4), "bare star: no arrow heads");
        check(inkNear(img, 100, 100, 0), "bare star: interior takes the brush");
    }
    {
        QImage img = render(true);
        bool inside = true;
        for (int k = 0; k < 6; ++k) { QPointF t = P(90 + 60 * k, 0.27 * S); if (!inkNear(img, t.x(), t.y(), 3)) inside = false; }
        check(inside, "rotate: the star is drawn inside the arc");
        bool upper = true; for (double d : {0.0, 45.0, 90.0, 135.0, 180.0}) { QPointF q = P(d, 0.44 * S); if (!inkNear(img, q.x(), q.y(), 3)) upper = false; }
        check(upper, "rotate: the arc runs over the top, from 3 o'clock to 9 o'clock");
        bool lower = true; for (double d : {225.0, 270.0, 315.0}) { QPointF q = P(d, 0.44 * S); if (inkNear(img, q.x(), q.y(), 3)) lower = false; }
        check(lower, "rotate: the lower half is open");
        check(inkNear(img, 100 + 0.44 * S, 100 + 0.14 * S, 4) && inkNear(img, 100 - 0.44 * S, 100 + 0.14 * S, 4), "rotate: both heads point down at the arc's ends");
    }
    std::printf("\n%s\n", g_failures ? "FAILURES" : "ALL PASS");
    return g_failures ? 1 : 0;
}
