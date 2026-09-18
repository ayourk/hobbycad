// =====================================================================
//  src/hobbycad/gui/dimensioninput.cpp
// =====================================================================
//
//  Qt side of the dimension fields: key translation and painting. See
//  dimensioninput.h.
//
//  Part of HobbyCAD.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include "dimensioninput.h"

#include <QKeyEvent>
#include <QPainter>
#include <QFontMetricsF>
#include <QPointF>

namespace hobbycad {

namespace {

sketch::DimKey dimKey(int key)
{
    switch (key) {
    case Qt::Key_Backspace: return sketch::DimKey::Backspace;
    case Qt::Key_Delete:    return sketch::DimKey::Delete;
    case Qt::Key_Left:      return sketch::DimKey::Left;
    case Qt::Key_Right:     return sketch::DimKey::Right;
    case Qt::Key_Home:      return sketch::DimKey::Home;
    case Qt::Key_End:       return sketch::DimKey::End;
    case Qt::Key_Return:
    case Qt::Key_Enter:     return sketch::DimKey::Enter;
    case Qt::Key_Tab:       return sketch::DimKey::Tab;
    case Qt::Key_Backtab:   return sketch::DimKey::Backtab;
    case Qt::Key_Escape:    return sketch::DimKey::Escape;
    default:                return sketch::DimKey::None;
    }
}

}  // namespace

// ---- Input --------------------------------------------------------------

bool DimensionInput::handleKey(QKeyEvent* event)
{
    sketch::DimKeyPress press;
    press.key = dimKey(event->key());
    const std::u32string typed = event->text().toStdU32String();
    if (!typed.empty()) press.character = typed.front();

    const sketch::DimKeyOutcome outcome =
        m_input.handleKey(press, m_host->dimChainsFromLastPoint());
    // Tools that derive state from "all fields locked" capture it now
    // (Rectangle's rotation reference), then refresh the preview.
    if (outcome.locked) m_host->dimAfterLock();
    if (outcome.unlocked) m_host->dimReapplyPreview();   // without the constraint
    if (outcome.repaint) m_host->dimRepaint();
    return outcome.consumed;
}

// ---- Render -------------------------------------------------------------

void DimensionInput::draw(QPainter& painter, const QPointF& position,
                          int fieldIndex, double rotation) const
{
    if (fieldIndex < 0 || fieldIndex >= m_input.fieldCount()) return;

    const sketch::DimFieldEntry& field = m_input.field(fieldIndex);

    painter.save();

    QFont font = painter.font();
    font.setPointSize(9);
    painter.setFont(font);
    QFontMetricsF fm(font);

    // Colors by how the field looks
    QColor bgColor;
    QColor textColor;
    QColor borderColor = Qt::transparent;

    QString displayText = QString::fromStdString(m_input.text(fieldIndex));
    switch (m_input.look(fieldIndex)) {
    case sketch::DimFieldLook::Locked:
        displayText += QStringLiteral(" ✓");
        bgColor = QColor(200, 255, 200);
        textColor = QColor(30, 100, 30);
        borderColor = QColor(80, 180, 80);
        break;
    case sketch::DimFieldLook::Selected:
        // Blue highlight + white text = standard "text selected, type to replace"
        bgColor = QColor(51, 153, 255);
        textColor = Qt::white;
        borderColor = QColor(30, 100, 200);
        break;
    case sketch::DimFieldLook::Typing: {
        // The caret; the library counts characters, Qt counts UTF-16 units.
        const std::u32string& typed = field.buffer;
        displayText = QString::fromStdU32String(typed);
        const qsizetype caret =
            QString::fromStdU32String(typed.substr(0, m_input.typedCursor(fieldIndex))).length();
        displayText.insert(caret, QStringLiteral("│"));
        bgColor = QColor(255, 235, 160);
        textColor = Qt::black;
        borderColor = QColor(210, 170, 50);
        break;
    }
    case sketch::DimFieldLook::Live:
        bgColor = Qt::white;
        textColor = Qt::black;
        borderColor = QColor(100, 100, 100);
        break;
    case sketch::DimFieldLook::Inactive:
        bgColor = Qt::white;
        textColor = Qt::black;
        break;
    }

    // Add field label prefix for multi-field tools
    if (m_input.fieldCount() > 1) {
        displayText = QString::fromStdString(field.label) + QStringLiteral(": ") + displayText;
    }

    QRectF textBounds = fm.boundingRect(displayText);

    // Apply rotation if provided (for dimension labels along edges)
    painter.translate(position);
    if (qAbs(rotation) > 0.01) {
        painter.rotate(rotation);
    }

    // Draw background
    QRectF labelRect(-textBounds.width() / 2.0 - 5,
                     -textBounds.height() / 2.0 - 2,
                     textBounds.width() + 10,
                     textBounds.height() + 4);
    painter.fillRect(labelRect, bgColor);

    // Draw border for active/locked fields (NoBrush so drawRect doesn't fill over the background)
    if (borderColor != Qt::transparent) {
        painter.setPen(QPen(borderColor, 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(labelRect);
    }

    // Draw text
    painter.setPen(textColor);
    painter.drawText(labelRect, Qt::AlignCenter, displayText);

    painter.restore();
}

}  // namespace hobbycad
