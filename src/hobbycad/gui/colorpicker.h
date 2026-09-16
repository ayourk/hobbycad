// =====================================================================
//  src/hobbycad/gui/colorpicker.h — HSL-authoritative color picker
// =====================================================================
//
//  A self-contained color picker: a saturation/lightness square with a hue
//  strip, plus RGB / HSL / HEX tabs, and a current-vs-previous swatch. HSL is
//  the authoritative state; RGB is a derived view, so winding lightness to an
//  extreme and back does not lose the hue.
//
//  Ported from the cherryrgb-qt picker (GPL-3.0-only, same project family) and
//  adapted for HobbyCAD: the keyboard-specific brightness axis is dropped, and
//  the translation/settings-store dependencies are removed.
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_GUI_COLORPICKER_H
#define HOBBYCAD_GUI_COLORPICKER_H

#include <QColor>
#include <QImage>
#include <QVector>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QLabel;
class QLineEdit;
class QSpinBox;
class QStackedWidget;
class QTabBar;
QT_END_NAMESPACE

namespace hobbycad {

/// Saturation/lightness square. Hue comes from outside; the widget reports the
/// saturation and lightness the user drags to.
class SlSquare : public QWidget {
    Q_OBJECT
public:
    explicit SlSquare(QWidget* parent = nullptr);
    void setHsl(int hue, int saturation, int lightness);
    QSize sizeHint() const override { return QSize(210, 140); }
signals:
    void picked(int saturation, int lightness);
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
private:
    void pickAt(const QPointF& pos);
    int m_hue = 0;
    int m_saturation = 255;
    int m_lightness = 128;
    QImage m_cache;
    int m_cachedHue = -1;
    QSize m_cachedSize;
};

/// Vertical hue strip.
class HueStrip : public QWidget {
    Q_OBJECT
public:
    explicit HueStrip(QWidget* parent = nullptr);
    void setHue(int hue);
    QSize sizeHint() const override { return QSize(20, 140); }
signals:
    void picked(int hue);
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
private:
    void pickAt(const QPointF& pos);
    int m_hue = 0;
};

/// Horizontal slider whose groove is painted with the colors the channel
/// would actually produce, the way GIMP's color scales work.
class GradientSlider : public QWidget {
    Q_OBJECT
public:
    explicit GradientSlider(int maximum, QWidget* parent = nullptr);
    int value() const { return m_value; }
    void setValue(int value);
    void setStops(const QVector<QColor>& stops);
    QSize sizeHint() const override { return QSize(160, 20); }
signals:
    void valueChanged(int value);
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
private:
    void pickAt(const QPointF& pos);
    int m_maximum;
    int m_value = 0;
    QVector<QColor> m_stops;
};

/// Two swatches side by side: the color being edited and the one it started
/// as. Clicking the previous (right) half reverts to it.
class ColorCompare : public QWidget {
    Q_OBJECT
public:
    explicit ColorCompare(QWidget* parent = nullptr);
    void setColors(const QColor& current, const QColor& previous);
    QSize sizeHint() const override { return QSize(120, 28); }
signals:
    void revertRequested();
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* event) override;
private:
    QColor m_current = Qt::red;
    QColor m_previous = Qt::red;
};

/// Color picker with RGB, HSL, HEX and a saturation/lightness square on tabs.
/// HSL is the authoritative state; RGB is derived from it.
class ColorPicker : public QWidget {
    Q_OBJECT
public:
    explicit ColorPicker(QWidget* parent = nullptr);

    QColor color() const;
    void setColor(const QColor& color);

    /// Marks the current color as the "previous" one shown for comparison.
    void anchorPrevious();

signals:
    void colorChanged(const QColor& color);

private:
    void setHsl(int hue, int saturation, int lightness, QObject* source);
    void syncWidgets(QObject* source);
    void adoptRgb(int r, int g, int b, QObject* source);
    void loadTab();
    void saveTab() const;

    QColor m_color = QColor::fromHsl(0, 255, 128);
    int m_hue = 0;
    int m_saturation = 255;
    int m_lightness = 128;
    QColor m_previous = QColor(Qt::red);

    SlSquare* m_square = nullptr;
    HueStrip* m_strip = nullptr;
    ColorCompare* m_compare = nullptr;
    QTabBar* m_tabBar = nullptr;
    QStackedWidget* m_pages = nullptr;

    QSpinBox* m_rSpin = nullptr;
    QSpinBox* m_gSpin = nullptr;
    QSpinBox* m_bSpin = nullptr;
    GradientSlider* m_rBar = nullptr;
    GradientSlider* m_gBar = nullptr;
    GradientSlider* m_bBar = nullptr;

    QSpinBox* m_hSpin = nullptr;
    QSpinBox* m_sSpin = nullptr;
    QSpinBox* m_lSpin = nullptr;
    GradientSlider* m_hBar = nullptr;
    GradientSlider* m_sBar = nullptr;
    GradientSlider* m_lBar = nullptr;

    QLineEdit* m_hex = nullptr;
    QLabel* m_hexLabel = nullptr;

    bool m_updating = false;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_GUI_COLORPICKER_H
