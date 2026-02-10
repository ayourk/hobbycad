// =====================================================================
//  src/hobbycad/gui/full/scalebarwidget.cpp — 2D scale bar overlay
// =====================================================================

#include "scalebarwidget.h"

#include <QPainter>
#include <QPaintEvent>
#include <QPalette>

#include <cmath>

namespace hobbycad {

ScaleBarWidget::ScaleBarWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("ScaleBar"));

    // Receive mouse events but don't consume them
    setAttribute(Qt::WA_TransparentForMouseEvents, true);

    // Dark background — transparency doesn't work over WA_PaintOnScreen
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(30, 34, 40));
    setPalette(pal);

    setFixedHeight(36);
}

void ScaleBarWidget::setView(const Handle(V3d_View)& view)
{
    m_view = view;
}

void ScaleBarWidget::updateScale()
{
    if (m_view.IsNull()) return;

    double worldPerPixel = m_view->Convert(1);  // mm per pixel
    if (worldPerPixel <= 0.0) return;

    // Maximum bar pixel width — leave room for "0", label, margins
    QFontMetrics fm(font());
    int zeroW   = fm.horizontalAdvance(QStringLiteral("0"));
    int margin  = 10;
    int gap     = 4;
    int maxBarPx = 120;  // hard cap on bar pixel length

    // Target bar width: roughly 50 pixels
    int targetPx = 50;
    double rawWorld = worldPerPixel * targetPx;

    // Snap to a "nice" round number
    m_worldLength = niceNumber(rawWorld);
    m_pixelLength = static_cast<int>(m_worldLength / worldPerPixel);

    // If the bar would be too wide, step down to smaller nice numbers
    // until it fits.  Iterate through 5, 2, 1 within the same decade,
    // then drop a decade.
    while (m_pixelLength > maxBarPx && m_worldLength > 0.001) {
        m_worldLength = niceNumberBelow(m_worldLength);
        m_pixelLength = static_cast<int>(m_worldLength / worldPerPixel);
    }

    // Floor at minimum visible size
    if (m_pixelLength < 20) m_pixelLength = 20;

    // Build label with appropriate unit
    if (m_worldLength >= 1000.0) {
        double meters = m_worldLength / 1000.0;
        if (meters == std::floor(meters))
            m_label = QString::number(static_cast<int>(meters)) +
                      QStringLiteral(" m");
        else
            m_label = QString::number(meters, 'g', 3) +
                      QStringLiteral(" m");
    } else if (m_worldLength >= 10.0) {
        if (m_worldLength == std::floor(m_worldLength))
            m_label = QString::number(static_cast<int>(m_worldLength)) +
                      QStringLiteral(" mm");
        else
            m_label = QString::number(m_worldLength, 'g', 4) +
                      QStringLiteral(" mm");
    } else if (m_worldLength >= 1.0) {
        m_label = QString::number(m_worldLength, 'g', 3) +
                  QStringLiteral(" mm");
    } else {
        double um = m_worldLength * 1000.0;
        m_label = QString::number(um, 'g', 3) +
                  QStringLiteral(" µm");
    }

    // Resize widget width to fit: margin + "0" + gap + bar + gap + label + margin
    int labelW = fm.horizontalAdvance(m_label);
    int totalW = margin + zeroW + gap + m_pixelLength + gap + labelW + margin;
    setFixedWidth(totalW);

    update();
}

double ScaleBarWidget::niceNumber(double value) const
{
    // Round to a "nice" number: 1, 2, 5, 10, 20, 50, 100, ...
    double exponent = std::floor(std::log10(value));
    double fraction = value / std::pow(10.0, exponent);

    double nice;
    if (fraction < 1.5)
        nice = 1.0;
    else if (fraction < 3.5)
        nice = 2.0;
    else if (fraction < 7.5)
        nice = 5.0;
    else
        nice = 10.0;

    return nice * std::pow(10.0, exponent);
}

double ScaleBarWidget::niceNumberBelow(double value) const
{
    // Step down to the next smaller nice number in the 1-2-5 sequence.
    // E.g. 100 → 50, 50 → 20, 20 → 10, 10 → 5, ...
    double exponent = std::floor(std::log10(value));
    double fraction = value / std::pow(10.0, exponent);

    double nice;
    if (fraction > 5.5)
        nice = 5.0;
    else if (fraction > 2.5)
        nice = 2.0;
    else if (fraction > 1.5)
        nice = 1.0;
    else {
        // Drop a decade: 1 → 0.5
        nice = 5.0;
        exponent -= 1.0;
    }

    return nice * std::pow(10.0, exponent);
}

void ScaleBarWidget::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // Font setup
    QFont font = p.font();
    font.setPixelSize(12);
    p.setFont(font);
    QFontMetrics fm(font);

    const int margin  = 10;
    const int tickH   = 8;     // end tick height (total, extends above+below)

    // Measure text widths
    int zeroW = fm.horizontalAdvance(QStringLiteral("0"));
    int labelW = fm.horizontalAdvance(m_label);
    int textGap = 4;  // gap between text and tick

    // Layout:  "0" [gap] |---bar---| [gap] "label"
    // The bar line runs horizontally, centered vertically in widget.
    // "0" is left of the bar, "label" is right of the bar.

    int barY = height() / 2;  // center line — splits text in half

    int x0 = margin + zeroW + textGap;         // bar left edge
    int x1 = x0 + m_pixelLength;               // bar right edge

    // Colors
    QColor barColor(220, 220, 220);
    QColor textColor(220, 220, 220);
    QColor shadow(0, 0, 0, 140);

    QPen barPen(barColor, 1.5);
    QPen shadowPen(shadow, 2.5);

    // ---- Shadow pass (offset +1,+1 for contrast) ----
    p.setPen(shadowPen);
    p.drawLine(x0 + 1, barY + 1, x1 + 1, barY + 1);
    p.drawLine(x0 + 1, barY - tickH / 2 + 1, x0 + 1, barY + tickH / 2 + 1);
    p.drawLine(x1 + 1, barY - tickH / 2 + 1, x1 + 1, barY + tickH / 2 + 1);

    // ---- Main bar ----
    p.setPen(barPen);
    // Horizontal bar
    p.drawLine(x0, barY, x1, barY);
    // Left end tick
    p.drawLine(x0, barY - tickH / 2, x0, barY + tickH / 2);
    // Right end tick
    p.drawLine(x1, barY - tickH / 2, x1, barY + tickH / 2);
    // Midpoint tick (shorter)
    int xMid = (x0 + x1) / 2;
    p.drawLine(xMid, barY - tickH / 4, xMid, barY + tickH / 4);

    // ---- "0" label, left of bar, vertically centered on bar line ----
    int textY = barY + fm.ascent() / 2 - 1;

    p.setPen(shadow);
    p.drawText(margin + 1, textY + 1, QStringLiteral("0"));
    p.setPen(textColor);
    p.drawText(margin, textY, QStringLiteral("0"));

    // ---- Value label, right of bar, vertically centered on bar line ----
    p.setPen(shadow);
    p.drawText(x1 + textGap + 1, textY + 1, m_label);
    p.setPen(textColor);
    p.drawText(x1 + textGap, textY, m_label);
}

}  // namespace hobbycad
