// SPDX-License-Identifier: GPL-3.0-only
//
// HobbyCAD - ayourk/hobbycad
// devtest/layout-metrics/toolbarwidth.cpp
//
// What a toolbar caption costs in pixels, per language.
//
// Reaching for a probe rather than a screenshot is deliberate. A capture
// shows that something is elided; it does not show which widget pins the
// width, and guessing at that from a picture is what wasted most of a
// session on cherryrgb-qt: a tab label was shortened twice, to a word that
// turned out to mean the wrong thing, and neither shortening fixed the
// overflow because the real constraint was elsewhere.
//
// Reproduces ToolbarButton's geometry rather than linking it: same
// ToolButtonTextUnderIcon style, same icon size, same margins and the 14px
// dropdown arrow. Linking the real widget would drag in OCCT for one number.
//
// Run under each locale, because the resolved font differs:
//     LC_ALL=de_DE.UTF-8 ./toolbarwidth "Volumenkörper"
//
// With no arguments it measures every group caption in all four catalogs.

#include <QApplication>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QIcon>
#include <QTextStream>
#include <QToolButton>
#include <QWidget>

namespace {

// ToolbarButton: contents margins (2,2,0,2), spacing 0, 14px arrow.
constexpr int kIconSize   = 24;  // ToolbarButton::m_iconSize default
constexpr int kArrowWidth = 14;
constexpr int kMarginsW   = 2 + 0;

int captionWidth(const QString& text)
{
    QToolButton probe;
    probe.setIcon(QIcon());
    probe.setText(text);
    probe.setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    probe.setIconSize(QSize(kIconSize, kIconSize));
    probe.setAutoRaise(true);
    return probe.sizeHint().width() + kArrowWidth + kMarginsW;
}

}  // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QTextStream out(stdout);

    const QStringList args = app.arguments().mid(1);
    if (args.isEmpty()) {
        out << "usage: toolbarwidth <caption> [caption...]\n";
        return 2;
    }

    out << QStringLiteral("font: %1 %2pt\n")
               .arg(app.font().family())
               .arg(app.font().pointSizeF());
    int widest = 0;
    for (const QString& text : args) {
        const int w = captionWidth(text);
        widest = qMax(widest, w);
        QString shown = text;
        shown.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
        out << QStringLiteral("%1  %2\n").arg(w, 5).arg(shown);
    }
    out << QStringLiteral("widest: %1\n").arg(widest);
    return 0;
}
