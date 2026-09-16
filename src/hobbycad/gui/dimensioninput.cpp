// =====================================================================
//  src/hobbycad/gui/dimensioninput.cpp
// =====================================================================
//
//  The dimension-field input subsystem, extracted from SketchCanvas.
//  See dimensioninput.h.
//
//  Part of HobbyCAD.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include "dimensioninput.h"

#include <hobbycad/parameters.h>
#include <hobbycad/units.h>

#include <QKeyEvent>
#include <QPainter>
#include <QFontMetricsF>
#include <QPointF>

namespace hobbycad {

// ---- Lifecycle ----------------------------------------------------------

void DimensionInput::clearAll()
{
    m_fields.clear();
    m_states.clear();
    m_active = -1;
    m_locked.clear();
}

void DimensionInput::addField(const QString& label, bool isAngle)
{
    m_fields.append({label, isAngle, 0.0});
}

void DimensionInput::flushLocked()
{
    for (int i = 0; i < m_fields.size(); ++i) {
        if (i < m_states.size() && m_states[i].locked)
            m_locked.append({m_fields[i].label, m_states[i].lockedValue});
    }
}

void DimensionInput::clearLocked()
{
    m_locked.clear();
}

void DimensionInput::reinitForNextStage()
{
    // Save any locked fields from the previous stage before reinitializing.
    flushLocked();
    m_fields.clear();
    m_states.clear();
    m_active = -1;
}

void DimensionInput::beginStates()
{
    for (int i = 0; i < m_fields.size(); ++i)
        m_states.append({QString(), false, 0.0, 0, false});
    if (!m_fields.isEmpty())
        m_active = 0;
}

// ---- Data ---------------------------------------------------------------

bool DimensionInput::allLocked() const
{
    if (m_states.isEmpty()) return false;
    for (const auto& st : m_states)
        if (!st.locked) return false;
    return true;
}

double DimensionInput::getLocked(int i) const
{
    if (i >= 0 && i < m_states.size() && m_states[i].locked)
        return m_states[i].lockedValue;
    return -1.0;
}

// ---- Input --------------------------------------------------------------

void DimensionInput::prefill(int fieldIndex)
{
    if (fieldIndex < 0 || fieldIndex >= m_fields.size()) return;
    if (fieldIndex >= m_states.size()) return;
    auto& field = m_fields[fieldIndex];
    auto& state = m_states[fieldIndex];
    if (state.locked) return;

    if (field.isAngle) {
        state.inputBuffer = QString::fromStdString(formatValue(field.currentValue));
    } else {
        state.inputBuffer = QString::fromStdString(
            formatValue(mmToUnit(field.currentValue, m_unit)));
    }
    state.cursorPos = state.inputBuffer.length();
    state.selectAll = true;
}

double DimensionInput::evaluate(const QString& buffer, bool isAngle) const
{
    double exprResult;
    QString exprStr = buffer.trimmed();
    std::string exprStdStr = exprStr.toStdString();
    if (isAngle) {
        // Angles: try DMS format first, then expression, then plain number
        double dmsResult;
        if (parseDMS(exprStdStr, dmsResult)) {
            return dmsResult;
        } else if (m_paramEngine && m_paramEngine->evaluateExpression(exprStdStr, exprResult)) {
            return exprResult;
        } else {
            return exprStr.toDouble();  // Fallback
        }
    }
    // Lengths: unit-aware evaluation, result in display units → mm
    if (m_paramEngine && m_paramEngine->evaluateExpression(exprStdStr, exprResult, m_unit)) {
        return unitToMm(exprResult, m_unit);
    }
    return parseValueWithUnit(exprStdStr, m_unit);  // Fallback
}

bool DimensionInput::handleKey(QKeyEvent* event)
{
    if (m_active < 0 || m_active >= m_states.size()) return false;

    auto& state = m_states[m_active];
    int key = event->key();

    // Helper: replace entire buffer if selectAll, otherwise insert at cursor
    auto replaceOrInsert = [&](QChar ch) {
        if (state.selectAll) {
            state.inputBuffer = QString(ch);
            state.cursorPos = 1;
            state.selectAll = false;
        } else {
            state.inputBuffer.insert(state.cursorPos, ch);
            state.cursorPos++;
        }
    };

    // Accept any character valid in an expression:
    // digits, letters (functions/params), operators, parens, period, comma, space
    // Skip if the active field is locked (all fields locked; no typing allowed)
    if (!state.locked) {
        QString text = event->text();
        if (!text.isEmpty()) {
            QChar ch = text[0];
            if (ch.isDigit() || ch.isLetter() || ch == QLatin1Char('_') ||
                ch == QLatin1Char('.') || ch == QLatin1Char('-') || ch == QLatin1Char('+') ||
                ch == QLatin1Char('*') || ch == QLatin1Char('/') || ch == QLatin1Char('^') ||
                ch == QLatin1Char('(') || ch == QLatin1Char(')') || ch == QLatin1Char(',') ||
                ch == QLatin1Char(' ') || ch == QLatin1Char('%') ||
                ch == QChar(0x00B0) ||    // ° degree sign
                ch == QLatin1Char('\'') || // ' arcminute
                ch == QLatin1Char('"') ||  // " arcsecond
                ch == QChar(0x2032) ||     // ′ prime (Unicode)
                ch == QChar(0x2033)) {     // ″ double prime (Unicode)
                replaceOrInsert(ch);
                m_host->dimRepaint();
                return true;
            }
        }
    }
    // Backspace
    if (key == Qt::Key_Backspace) {
        if (state.selectAll) {
            state.inputBuffer.clear();
            state.cursorPos = 0;
            state.selectAll = false;
            m_host->dimRepaint();
            return true;
        } else if (state.cursorPos > 0) {
            state.inputBuffer.remove(state.cursorPos - 1, 1);
            state.cursorPos--;
            m_host->dimRepaint();
            return true;
        }
        // If cursorPos == 0 and not selectAll, fall through to normal Backspace handling
    }
    // Delete key
    if (key == Qt::Key_Delete) {
        if (state.selectAll) {
            state.inputBuffer.clear();
            state.cursorPos = 0;
            state.selectAll = false;
            m_host->dimRepaint();
            return true;
        } else if (state.cursorPos < state.inputBuffer.length()) {
            state.inputBuffer.remove(state.cursorPos, 1);
            m_host->dimRepaint();
            return true;
        }
    }
    // Arrow keys: deselect and move cursor
    // When deselecting, refresh buffer with current live value first
    if (key == Qt::Key_Left) {
        if (state.selectAll) {
            prefill(m_active);  // Refresh buffer to current value
            state.cursorPos = 0;
            state.selectAll = false;
        } else if (state.cursorPos > 0) {
            state.cursorPos--;
        }
        m_host->dimRepaint();
        return true;
    }
    if (key == Qt::Key_Right) {
        if (state.selectAll) {
            prefill(m_active);  // Refresh buffer to current value
            state.selectAll = false;
            // cursorPos already at end from prefill
        } else if (state.cursorPos < state.inputBuffer.length()) {
            state.cursorPos++;
        }
        m_host->dimRepaint();
        return true;
    }
    // Home/End
    if (key == Qt::Key_Home) {
        if (state.selectAll) prefill(m_active);
        state.selectAll = false;
        state.cursorPos = 0;
        m_host->dimRepaint();
        return true;
    }
    if (key == Qt::Key_End) {
        if (state.selectAll) prefill(m_active);
        state.selectAll = false;
        state.cursorPos = state.inputBuffer.length();
        m_host->dimRepaint();
        return true;
    }
    // Enter/Return: lock the value
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        // Empty Enter on a chaining tool (Line) ends the chain: fall through
        // to the canvas's Return/Enter case, which cancels the in-progress
        // segment and keeps the committed ones (Fusion review C3). Every
        // other case is handled here.
        const bool endChainOnEmptyEnter =
            state.inputBuffer.isEmpty() && m_host->dimChainsFromLastPoint();
        if (!endChainOnEmptyEnter)
        if (!state.inputBuffer.isEmpty()) {
            double val;
            if (state.selectAll) {
                // selectAll = live tracking: lock the current live value directly
                val = m_fields[m_active].currentValue;  // Already in mm/degrees
            } else {
                val = evaluate(state.inputBuffer, m_fields[m_active].isAngle);
            }
            if (m_fields[m_active].isAngle || val > 0.0) {
                state.locked = true;
                state.lockedValue = val;
                state.inputBuffer.clear();
                state.cursorPos = 0;
                state.selectAll = false;
                // Advance to next unlocked field and pre-fill
                for (int i = 1; i < m_fields.size(); ++i) {
                    int next = (m_active + i) % m_fields.size();
                    if (!m_states[next].locked) {
                        m_active = next;
                        prefill(next);
                        break;
                    }
                }
                // Tools that derive state from "all fields locked" capture it
                // now (Rectangle's rotation reference), then refresh preview.
                m_host->dimAfterLock();
            }
        }
        if (!endChainOnEmptyEnter) {
            m_host->dimRepaint();
            return true;
        }
        // fall through: the canvas ends the chain
        return false;
    }
    // Tab / Shift+Tab: cycle to next/previous unlocked field, pre-fill with current value
    if (key == Qt::Key_Tab || key == Qt::Key_Backtab) {
        state.inputBuffer.clear();
        state.cursorPos = 0;
        state.selectAll = false;
        // Advance (Tab) or retreat (Shift+Tab) to next unlocked field
        if (m_fields.size() > 1) {
            int n = m_fields.size();
            int step = (key == Qt::Key_Backtab) ? (n - 1) : 1;  // n-1 ≡ -1 mod n
            for (int i = 1; i <= n; ++i) {
                int next = (m_active + step * i) % n;
                if (!m_states[next].locked) {
                    m_active = next;
                    break;
                }
            }
        }
        prefill(m_active);
        m_host->dimRepaint();
        return true;
    }
    // Escape: clear buffer or unlock last locked field
    if (key == Qt::Key_Escape) {
        if (!state.inputBuffer.isEmpty()) {
            state.inputBuffer.clear();
            state.cursorPos = 0;
            state.selectAll = false;
            m_host->dimRepaint();
            return true;
        }
        // Unlock in reverse priority: angles first, then lengths
        int unlockIdx = -1;
        // First pass: find last locked angle field
        for (int i = m_fields.size() - 1; i >= 0; --i) {
            if (i < m_states.size() && m_states[i].locked && m_fields[i].isAngle) {
                unlockIdx = i;
                break;
            }
        }
        // Second pass: if no locked angles, find last locked length field
        if (unlockIdx < 0) {
            for (int i = m_fields.size() - 1; i >= 0; --i) {
                if (i < m_states.size() && m_states[i].locked && !m_fields[i].isAngle) {
                    unlockIdx = i;
                    break;
                }
            }
        }
        if (unlockIdx >= 0) {
            m_states[unlockIdx].locked = false;
            m_states[unlockIdx].lockedValue = 0.0;
            m_active = unlockIdx;
            // Remove from accumulated locked-for-constraints list
            QString label = m_fields[unlockIdx].label;
            for (int i = m_locked.size() - 1; i >= 0; --i) {
                if (m_locked[i].first == label) {
                    m_locked.removeAt(i);
                    break;
                }
            }
            m_host->dimReapplyPreview();  // Re-apply without the constraint
            m_host->dimRepaint();
            return true;
        }
        // Fall through to normal Escape handling (cancel entity)
        return false;
    }

    return false;
}

// ---- Render -------------------------------------------------------------

void DimensionInput::draw(QPainter& painter, const QPointF& position,
                          int fieldIndex, double rotation) const
{
    if (fieldIndex < 0 || fieldIndex >= m_fields.size()) return;

    const auto& field = m_fields[fieldIndex];
    const auto& state = m_states[fieldIndex];
    bool isActive = (fieldIndex == m_active);

    painter.save();

    QFont font = painter.font();
    font.setPointSize(9);
    painter.setFont(font);
    QFontMetricsF fm(font);

    // Determine display text and colors
    QString displayText;
    QColor bgColor;
    QColor textColor;
    QColor borderColor = Qt::transparent;

    if (state.locked) {
        // Locked: green, show formatted value with checkmark
        if (field.isAngle) {
            displayText = QString::fromStdString(formatAngle(state.lockedValue)) + QStringLiteral(" ✓");
        } else {
            displayText = QString::fromStdString(formatValueWithUnit(state.lockedValue, m_unit)) + QStringLiteral(" ✓");
        }
        bgColor = QColor(200, 255, 200);
        textColor = QColor(30, 100, 30);
        borderColor = QColor(80, 180, 80);
    } else if (isActive && !state.inputBuffer.isEmpty()) {
        if (state.selectAll) {
            // Active + selected: show LIVE currentValue with selection highlight
            // Blue highlight + white text = standard "text selected, type to replace"
            if (field.isAngle) {
                displayText = QString::fromStdString(formatValue(field.currentValue));
            } else {
                displayText = QString::fromStdString(formatValue(mmToUnit(field.currentValue, m_unit)));
            }
            bgColor = QColor(51, 153, 255);        // Selection blue
            textColor = Qt::white;
            borderColor = QColor(30, 100, 200);
        } else {
            // Active + typing with cursor: yellow, insert │ at cursorPos
            int safePos = qBound(0, state.cursorPos, state.inputBuffer.length());
            displayText = state.inputBuffer;
            displayText.insert(safePos, QStringLiteral("│"));  // │ cursor
            bgColor = QColor(255, 235, 160);
            textColor = Qt::black;
            borderColor = QColor(210, 170, 50);
        }
    } else if (isActive) {
        // Active + empty: live measurement, not yet editing
        if (field.isAngle) {
            displayText = QString::fromStdString(formatAngle(field.currentValue));
        } else {
            displayText = QString::fromStdString(formatValueWithUnit(field.currentValue, m_unit));
        }
        bgColor = Qt::white;
        textColor = Qt::black;
        borderColor = QColor(100, 100, 100);
    } else {
        // Inactive: black text on white background
        if (field.isAngle) {
            displayText = QString::fromStdString(formatAngle(field.currentValue));
        } else {
            displayText = QString::fromStdString(formatValueWithUnit(field.currentValue, m_unit));
        }
        bgColor = Qt::white;
        textColor = Qt::black;
    }

    // Add field label prefix for multi-field tools
    if (m_fields.size() > 1) {
        displayText = field.label + QStringLiteral(": ") + displayText;
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
