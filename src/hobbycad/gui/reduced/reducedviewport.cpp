// =====================================================================
//  src/hobbycad/gui/reduced/reducedviewport.cpp — Disabled viewport placeholder
// =====================================================================

#include "reducedviewport.h"

#include <QApplication>
#include <QMouseEvent>
#include <QPainter>

namespace hobbycad {

ReducedViewport::ReducedViewport(QWidget* parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("ReducedViewport"));
    setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
    setCursor(Qt::ArrowCursor);
    setMinimumSize(400, 300);

    // Dark background to suggest an inactive viewport
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(45, 48, 55));
    setPalette(pal);
}

void ReducedViewport::setSuppressDialog(bool suppress)
{
    m_suppressDialog = suppress;
}

void ReducedViewport::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        if (m_suppressDialog) {
            QApplication::beep();
        } else {
            emit viewportClicked();
        }
    }
    QFrame::mousePressEvent(event);
}

void ReducedViewport::paintEvent(QPaintEvent* event)
{
    QFrame::paintEvent(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Warning icon (triangle with exclamation)
    QFont iconFont = painter.font();
    iconFont.setPixelSize(64);
    painter.setFont(iconFont);
    painter.setPen(QColor(220, 180, 50));

    QRect iconRect = rect();
    iconRect.setBottom(rect().center().y());
    painter.drawText(iconRect, Qt::AlignHCenter | Qt::AlignBottom,
                     QStringLiteral("\u26A0"));

    // Message text
    QFont msgFont = painter.font();
    msgFont.setPixelSize(16);
    painter.setFont(msgFont);
    painter.setPen(QColor(180, 185, 195));

    QRect textRect = rect();
    textRect.setTop(rect().center().y() + 10);
    painter.drawText(textRect, Qt::AlignHCenter | Qt::AlignTop,
        tr("3D viewport disabled\n\n"
           "OpenGL 3.3 or higher is required for the 3D viewport.\n"
           "File operations and geometry tools remain available.\n\n"
           "Click here for details."));
}

}  // namespace hobbycad

