// =====================================================================
//  src/hobbycad/gui/colorpicker.cpp — HSL-authoritative color picker
// =====================================================================
//  See colorpicker.h. Ported from cherryrgb-qt (GPL-3.0-only); the brightness
//  axis and the translation/settings-store dependencies are removed.
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================

#include "colorpicker.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabBar>
#include <QVBoxLayout>

#include <algorithm>

namespace hobbycad {

// --- saturation / lightness square ------------------------------------------

SlSquare::SlSquare(QWidget* parent) : QWidget(parent) {
    setCursor(Qt::CrossCursor);
    setMinimumSize(140, 100);
}

void SlSquare::setHsl(int hue, int saturation, int lightness) {
    m_hue = hue;
    m_saturation = saturation;
    m_lightness = lightness;
    update();
}

void SlSquare::paintEvent(QPaintEvent*) {
    if (m_cachedHue != m_hue || m_cachedSize != size()) {
        m_cache = QImage(size(), QImage::Format_RGB32);
        for (int y = 0; y < height(); ++y) {
            const int lightness = 255 - (y * 255 / std::max(1, height() - 1));
            auto* line = reinterpret_cast<QRgb*>(m_cache.scanLine(y));
            for (int x = 0; x < width(); ++x) {
                const int saturation = x * 255 / std::max(1, width() - 1);
                line[x] = QColor::fromHsl(m_hue, saturation, lightness).rgb();
            }
        }
        m_cachedHue = m_hue;
        m_cachedSize = size();
    }
    QPainter painter(this);
    painter.drawImage(0, 0, m_cache);
    const qreal x = m_saturation / 255.0 * (width() - 1);
    const qreal y = (1.0 - m_lightness / 255.0) * (height() - 1);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(Qt::black, 3));
    painter.drawEllipse(QPointF(x, y), 5, 5);
    painter.setPen(QPen(Qt::white, 1.5));
    painter.drawEllipse(QPointF(x, y), 5, 5);
}

void SlSquare::pickAt(const QPointF& pos) {
    const int saturation = qBound(0, qRound(pos.x() / std::max(1, width() - 1) * 255), 255);
    const int lightness =
        qBound(0, qRound((1.0 - pos.y() / std::max(1, height() - 1)) * 255), 255);
    emit picked(saturation, lightness);
}

void SlSquare::mousePressEvent(QMouseEvent* event) { pickAt(event->position()); }
void SlSquare::mouseMoveEvent(QMouseEvent* event) {
    if (event->buttons() & Qt::LeftButton) pickAt(event->position());
}

// --- hue strip ---------------------------------------------------------------

HueStrip::HueStrip(QWidget* parent) : QWidget(parent) {
    setCursor(Qt::CrossCursor);
    setMinimumWidth(18);
}

void HueStrip::setHue(int hue) { m_hue = hue; update(); }

void HueStrip::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    for (int y = 0; y < height(); ++y) {
        const int hue = y * 359 / std::max(1, height() - 1);
        painter.setPen(QColor::fromHsl(hue, 255, 128));
        painter.drawLine(0, y, width(), y);
    }
    const qreal y = m_hue / 359.0 * (height() - 1);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(Qt::black, 3));
    painter.drawLine(QPointF(0, y), QPointF(width(), y));
    painter.setPen(QPen(Qt::white, 1.5));
    painter.drawLine(QPointF(0, y), QPointF(width(), y));
}

void HueStrip::pickAt(const QPointF& pos) {
    emit picked(qBound(0, qRound(pos.y() / std::max(1, height() - 1) * 359), 359));
}

void HueStrip::mousePressEvent(QMouseEvent* event) { pickAt(event->position()); }
void HueStrip::mouseMoveEvent(QMouseEvent* event) {
    if (event->buttons() & Qt::LeftButton) pickAt(event->position());
}

// --- gradient slider ---------------------------------------------------------

GradientSlider::GradientSlider(int maximum, QWidget* parent)
    : QWidget(parent), m_maximum(std::max(1, maximum)) {
    setCursor(Qt::PointingHandCursor);
    setMinimumHeight(18);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void GradientSlider::setValue(int value) {
    value = qBound(0, value, m_maximum);
    if (value == m_value) return;
    m_value = value;
    update();
}

void GradientSlider::setStops(const QVector<QColor>& stops) { m_stops = stops; update(); }

void GradientSlider::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF track = rect().adjusted(1, 3, -1, -3);
    QLinearGradient gradient(track.topLeft(), track.topRight());
    if (m_stops.isEmpty()) {
        gradient.setColorAt(0, Qt::black);
        gradient.setColorAt(1, Qt::white);
    } else if (m_stops.size() == 1) {
        gradient.setColorAt(0, m_stops.first());
        gradient.setColorAt(1, m_stops.first());
    } else {
        for (int i = 0; i < m_stops.size(); ++i)
            gradient.setColorAt(qreal(i) / (m_stops.size() - 1), m_stops.at(i));
    }
    painter.setPen(QPen(QColor(0, 0, 0, 90), 1));
    painter.setBrush(gradient);
    painter.drawRoundedRect(track, 3, 3);
    const qreal x = track.left() + qreal(m_value) / m_maximum * track.width();
    painter.setPen(QPen(Qt::black, 3));
    painter.drawLine(QPointF(x, track.top() - 2), QPointF(x, track.bottom() + 2));
    painter.setPen(QPen(Qt::white, 1.5));
    painter.drawLine(QPointF(x, track.top() - 2), QPointF(x, track.bottom() + 2));
}

void GradientSlider::pickAt(const QPointF& pos) {
    const qreal width = std::max(1, this->width() - 2);
    const int value = qBound(0, qRound((pos.x() - 1) / width * m_maximum), m_maximum);
    if (value == m_value) return;
    m_value = value;
    update();
    emit valueChanged(value);
}

void GradientSlider::mousePressEvent(QMouseEvent* event) { pickAt(event->position()); }
void GradientSlider::mouseMoveEvent(QMouseEvent* event) {
    if (event->buttons() & Qt::LeftButton) pickAt(event->position());
}

// --- current / previous comparison -------------------------------------------

ColorCompare::ColorCompare(QWidget* parent) : QWidget(parent) {
    setToolTip(tr("Left: current color. Right: what it was. Click the right "
                  "half to go back to it."));
}

void ColorCompare::setColors(const QColor& current, const QColor& previous) {
    m_current = current;
    m_previous = previous;
    update();
}

void ColorCompare::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    const int half = width() / 2;
    painter.fillRect(0, 0, half, height(), m_current);
    painter.fillRect(half, 0, width() - half, height(), m_previous);
    painter.setPen(QPen(QColor(0, 0, 0, 120), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
}

void ColorCompare::mousePressEvent(QMouseEvent* event) {
    if (event->position().x() > width() / 2.0) emit revertRequested();
}

// --- picker ------------------------------------------------------------------

ColorPicker::ColorPicker(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(6);

    m_square = new SlSquare(this);
    m_strip = new HueStrip(this);

    m_compare = new ColorCompare(this);
    root->addWidget(m_compare);
    connect(m_compare, &ColorCompare::revertRequested, this, [this] {
        setColor(m_previous);
        emit colorChanged(color());
    });

    connect(m_square, &SlSquare::picked, this, [this](int saturation, int lightness) {
        setHsl(m_hue, saturation, lightness, m_square);
    });
    connect(m_strip, &HueStrip::picked, this, [this](int hue) {
        setHsl(hue, m_saturation, m_lightness, m_strip);
    });

    m_tabBar = new QTabBar(this);
    m_tabBar->setDrawBase(false);
    m_tabBar->setExpanding(false);
    m_pages = new QStackedWidget(this);
    root->addWidget(m_tabBar);
    root->addWidget(m_pages);

    auto makeSpin = [this](int maximum) {
        auto* spin = new QSpinBox(this);
        spin->setRange(0, maximum);
        spin->setFixedWidth(64);
        spin->setKeyboardTracking(false);
        return spin;
    };
    auto addChannelRow = [](QGridLayout* grid, int row, const QString& text,
                            GradientSlider* bar, QSpinBox* spin) {
        grid->addWidget(new QLabel(text, bar->parentWidget()), row, 0);
        grid->addWidget(bar, row, 1);
        grid->addWidget(spin, row, 2);
    };

    // ---- RGB tab ----
    auto* rgbPage = new QWidget(m_pages);
    auto* rgbGrid = new QGridLayout(rgbPage);
    rgbGrid->setContentsMargins(0, 2, 0, 0);
    rgbGrid->setVerticalSpacing(3);
    rgbGrid->setColumnStretch(1, 1);
    m_rBar = new GradientSlider(255, rgbPage);
    m_gBar = new GradientSlider(255, rgbPage);
    m_bBar = new GradientSlider(255, rgbPage);
    m_rSpin = makeSpin(255);
    m_gSpin = makeSpin(255);
    m_bSpin = makeSpin(255);
    addChannelRow(rgbGrid, 0, tr("R"), m_rBar, m_rSpin);
    addChannelRow(rgbGrid, 1, tr("G"), m_gBar, m_gSpin);
    addChannelRow(rgbGrid, 2, tr("B"), m_bBar, m_bSpin);
    m_tabBar->addTab(tr("RGB"));
    m_pages->addWidget(rgbPage);

    // ---- HSL tab ----
    auto* hslPage = new QWidget(m_pages);
    auto* hslGrid = new QGridLayout(hslPage);
    hslGrid->setContentsMargins(0, 2, 0, 0);
    hslGrid->setVerticalSpacing(3);
    hslGrid->setColumnStretch(1, 1);
    m_hBar = new GradientSlider(359, hslPage);
    m_sBar = new GradientSlider(255, hslPage);
    m_lBar = new GradientSlider(255, hslPage);
    m_hSpin = makeSpin(359);
    m_sSpin = makeSpin(255);
    m_lSpin = makeSpin(255);
    addChannelRow(hslGrid, 0, tr("H"), m_hBar, m_hSpin);
    addChannelRow(hslGrid, 1, tr("S"), m_sBar, m_sSpin);
    addChannelRow(hslGrid, 2, tr("L"), m_lBar, m_lSpin);
    m_tabBar->addTab(tr("HSL"));
    m_pages->addWidget(hslPage);

    // ---- HEX tab ----
    auto* hexPage = new QWidget(m_pages);
    auto* hexLayout = new QVBoxLayout(hexPage);
    auto* hexRow = new QHBoxLayout;
    m_hex = new QLineEdit(hexPage);
    m_hex->setMaxLength(7);
    m_hex->setFixedWidth(100);
    static const QRegularExpression hexPattern(QStringLiteral("#?[0-9A-Fa-f]{0,6}"));
    m_hex->setValidator(new QRegularExpressionValidator(hexPattern, m_hex));
    m_hexLabel = new QLabel(tr("HTML notation:"), hexPage);
    hexRow->addWidget(m_hexLabel);
    hexRow->addWidget(m_hex);
    hexRow->addStretch(1);
    hexLayout->addLayout(hexRow);
    hexLayout->addStretch(1);
    m_tabBar->addTab(tr("HEX"));
    m_pages->addWidget(hexPage);

    // ---- Grad tab (the square + hue strip) ----
    auto* gradientPage = new QWidget(m_pages);
    auto* gradientLayout = new QHBoxLayout(gradientPage);
    gradientLayout->setContentsMargins(0, 4, 0, 0);
    m_square->setParent(gradientPage);
    m_strip->setParent(gradientPage);
    m_square->setMinimumHeight(90);
    gradientLayout->addWidget(m_square, 1);
    gradientLayout->addWidget(m_strip, 0);
    m_tabBar->addTab(tr("Grad"));
    m_pages->addWidget(gradientPage);

    auto followCurrentPage = [this](int current) {
        for (int i = 0; i < m_pages->count(); ++i) {
            m_pages->widget(i)->setSizePolicy(
                QSizePolicy::Preferred,
                i == current ? QSizePolicy::Preferred : QSizePolicy::Ignored);
        }
        if (QWidget* page = m_pages->widget(current)) page->adjustSize();
        m_pages->adjustSize();
    };
    connect(m_tabBar, &QTabBar::currentChanged, this, [this, followCurrentPage](int index) {
        m_pages->setCurrentIndex(index);
        followCurrentPage(index);
        saveTab();
    });

    // ---- wiring ----
    const auto onRgbSpin = [this] {
        if (m_updating) return;
        adoptRgb(m_rSpin->value(), m_gSpin->value(), m_bSpin->value(), m_rSpin);
    };
    connect(m_rSpin, &QSpinBox::valueChanged, this, onRgbSpin);
    connect(m_gSpin, &QSpinBox::valueChanged, this, onRgbSpin);
    connect(m_bSpin, &QSpinBox::valueChanged, this, onRgbSpin);
    connect(m_rBar, &GradientSlider::valueChanged, this, [this](int v) {
        if (!m_updating) adoptRgb(v, m_gBar->value(), m_bBar->value(), m_rBar);
    });
    connect(m_gBar, &GradientSlider::valueChanged, this, [this](int v) {
        if (!m_updating) adoptRgb(m_rBar->value(), v, m_bBar->value(), m_gBar);
    });
    connect(m_bBar, &GradientSlider::valueChanged, this, [this](int v) {
        if (!m_updating) adoptRgb(m_rBar->value(), m_gBar->value(), v, m_bBar);
    });

    const auto onHslSpin = [this] {
        if (m_updating) return;
        setHsl(m_hSpin->value(), m_sSpin->value(), m_lSpin->value(), m_hSpin);
    };
    connect(m_hSpin, &QSpinBox::valueChanged, this, onHslSpin);
    connect(m_sSpin, &QSpinBox::valueChanged, this, onHslSpin);
    connect(m_lSpin, &QSpinBox::valueChanged, this, onHslSpin);
    connect(m_hBar, &GradientSlider::valueChanged, this, [this](int v) {
        if (!m_updating) setHsl(v, m_saturation, m_lightness, m_hBar);
    });
    connect(m_sBar, &GradientSlider::valueChanged, this, [this](int v) {
        if (!m_updating) setHsl(m_hue, v, m_lightness, m_sBar);
    });
    connect(m_lBar, &GradientSlider::valueChanged, this, [this](int v) {
        if (!m_updating) setHsl(m_hue, m_saturation, v, m_lBar);
    });

    const auto onHexEdited = [this] {
        if (m_updating) return;
        QString text = m_hex->text().trimmed();
        if (text.startsWith(QLatin1Char('#'))) text.remove(0, 1);
        const QColor parsed(QStringLiteral("#") + text);
        if (text.size() != 6 || !parsed.isValid()) { syncWidgets(nullptr); return; }
        adoptRgb(parsed.red(), parsed.green(), parsed.blue(), m_hex);
    };
    connect(m_hex, &QLineEdit::editingFinished, this, onHexEdited);
    connect(m_hex, &QLineEdit::returnPressed, this, onHexEdited);

    loadTab();
    followCurrentPage(m_tabBar->currentIndex());
    syncWidgets(nullptr);
}

QColor ColorPicker::color() const { return m_color; }

void ColorPicker::adoptRgb(int r, int g, int b, QObject* source) {
    const QColor rgb(qBound(0, r, 255), qBound(0, g, 255), qBound(0, b, 255));
    int hue = rgb.hslHue();
    int saturation = rgb.hslSaturation();
    const int lightness = rgb.lightness();
    if (hue < 0 || saturation == 0) hue = m_hue;
    if (lightness == 0 || lightness == 255) saturation = m_saturation;
    if (rgb == m_color && hue == m_hue && saturation == m_saturation && lightness == m_lightness)
        return;
    m_color = rgb;
    m_hue = hue;
    m_saturation = saturation;
    m_lightness = lightness;
    syncWidgets(source);
    emit colorChanged(color());
}

void ColorPicker::setColor(const QColor& color) {
    if (!color.isValid()) return;
    int hue = color.hslHue();
    int saturation = color.hslSaturation();
    const int lightness = color.lightness();
    if (hue < 0 || saturation == 0) hue = m_hue;
    if (lightness == 0 || lightness == 255) saturation = m_saturation;
    if (color.rgb() == m_color.rgb() && hue == m_hue && saturation == m_saturation
        && lightness == m_lightness)
        return;
    m_color = color.toRgb();
    m_hue = hue;
    m_saturation = saturation;
    m_lightness = lightness;
    syncWidgets(nullptr);
}

void ColorPicker::anchorPrevious() {
    m_previous = color();
    m_compare->setColors(color(), m_previous);
}

void ColorPicker::setHsl(int hue, int saturation, int lightness, QObject* source) {
    hue = qBound(0, hue, 359);
    saturation = qBound(0, saturation, 255);
    lightness = qBound(0, lightness, 255);
    const QColor exact = QColor::fromHsl(hue, saturation, lightness);
    if (hue == m_hue && saturation == m_saturation && lightness == m_lightness && exact == m_color)
        return;
    m_hue = hue;
    m_saturation = saturation;
    m_lightness = lightness;
    m_color = exact;
    syncWidgets(source);
    emit colorChanged(color());
}

void ColorPicker::syncWidgets(QObject* source) {
    m_updating = true;
    const QColor current = color();
    const int r = current.red();
    const int g = current.green();
    const int b = current.blue();

    m_square->setHsl(m_hue, m_saturation, m_lightness);
    m_strip->setHue(m_hue);
    m_compare->setColors(current, m_previous);

    m_rBar->setStops({QColor(0, g, b), QColor(255, g, b)});
    m_gBar->setStops({QColor(r, 0, b), QColor(r, 255, b)});
    m_bBar->setStops({QColor(r, g, 0), QColor(r, g, 255)});

    QVector<QColor> hueStops;
    hueStops.reserve(13);
    for (int i = 0; i <= 12; ++i)
        hueStops.append(QColor::fromHsl(i * 359 / 12, m_saturation, m_lightness));
    m_hBar->setStops(hueStops);
    m_sBar->setStops({QColor::fromHsl(m_hue, 0, m_lightness),
                      QColor::fromHsl(m_hue, 255, m_lightness)});
    m_lBar->setStops({QColor::fromHsl(m_hue, m_saturation, 0),
                      QColor::fromHsl(m_hue, m_saturation, 128),
                      QColor::fromHsl(m_hue, m_saturation, 255)});

    if (source != m_rSpin && source != m_gSpin && source != m_bSpin) {
        m_rSpin->setValue(r);
        m_gSpin->setValue(g);
        m_bSpin->setValue(b);
    }
    if (source != m_rBar && source != m_gBar && source != m_bBar) {
        m_rBar->setValue(r);
        m_gBar->setValue(g);
        m_bBar->setValue(b);
    }
    if (source != m_hSpin && source != m_sSpin && source != m_lSpin) {
        m_hSpin->setValue(m_hue);
        m_sSpin->setValue(m_saturation);
        m_lSpin->setValue(m_lightness);
    }
    if (source != m_hBar && source != m_sBar && source != m_lBar) {
        m_hBar->setValue(m_hue);
        m_sBar->setValue(m_saturation);
        m_lBar->setValue(m_lightness);
    }
    if (source != m_hex) m_hex->setText(current.name(QColor::HexRgb).toUpper());

    m_updating = false;
}

void ColorPicker::loadTab() {
    QSettings settings;
    const int tab = settings.value(QStringLiteral("colorpicker/tab"), 3).toInt();
    if (tab >= 0 && tab < m_tabBar->count()) {
        const QSignalBlocker blocker(m_tabBar);
        m_tabBar->setCurrentIndex(tab);
        m_pages->setCurrentIndex(tab);
    }
}

void ColorPicker::saveTab() const {
    QSettings settings;
    settings.setValue(QStringLiteral("colorpicker/tab"), m_tabBar->currentIndex());
}

}  // namespace hobbycad
