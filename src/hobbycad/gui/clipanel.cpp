// =====================================================================
//  src/hobbycad/gui/clipanel.cpp — Embedded CLI terminal panel
// =====================================================================

#include "clipanel.h"

#include "cli/clihistory.h"
#include "cli/cliengine.h"

#include <hobbycad/core.h>

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QTextCursor>

namespace hobbycad {

CliPanel::CliPanel(QWidget* parent)
    : QPlainTextEdit(parent)
{
    setObjectName(QStringLiteral("CliPanel"));

    m_history = new CliHistory();
    m_history->load();

    m_engine = new CliEngine(*m_history);

    // Monospace font
    QFont monoFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    monoFont.setPointSize(10);
    setFont(monoFont);

    // Terminal appearance
    setLineWrapMode(QPlainTextEdit::WidgetWidth);
    setMaximumBlockCount(10000);
    setUndoRedoEnabled(false);
    setCursorWidth(8);  // block cursor

    // Dark terminal styling via stylesheet
    setStyleSheet(QStringLiteral(
        "QPlainTextEdit#CliPanel {"
        "  background-color: #1e2126;"
        "  color: #cccccc;"
        "  border: none;"
        "  selection-background-color: #3a4250;"
        "  selection-color: #ffffff;"
        "}"));

    // Welcome message
    QTextCursor cur = textCursor();
    cur.movePosition(QTextCursor::End);
    cur.insertText(
        QStringLiteral("HobbyCAD ") +
        QString::fromLatin1(hobbycad::version()) +
        QStringLiteral(" — Embedded Terminal\n"
            "Type 'help' for available commands.\n\n"));
    setTextCursor(cur);

    // Show first prompt with cursor at the end
    showPrompt();
}

CliPanel::~CliPanel()
{
    m_history->save();
    delete m_engine;
    delete m_history;
}

void CliPanel::focusInput()
{
    setFocus();
    moveCursorToEnd();
}

// ---- Key handling ---------------------------------------------------
//
//  All text before m_promptEnd is read-only.  The user can only edit
//  text after m_promptEnd (the current input after the prompt).

void CliPanel::keyPressEvent(QKeyEvent* event)
{
    QTextCursor cur = textCursor();
    int pos = cur.position();

    // Enter/Return — execute the command
    if (event->key() == Qt::Key_Return ||
        event->key() == Qt::Key_Enter) {
        moveCursorToEnd();
        executeCurrentLine();
        return;
    }

    // Up arrow — history previous
    if (event->key() == Qt::Key_Up) {
        historyUp();
        return;
    }

    // Down arrow — history next
    if (event->key() == Qt::Key_Down) {
        historyDown();
        return;
    }

    // Home — jump to start of input (after prompt), not start of line
    if (event->key() == Qt::Key_Home) {
        QTextCursor c = textCursor();
        if (event->modifiers() & Qt::ShiftModifier) {
            c.setPosition(m_promptEnd, QTextCursor::KeepAnchor);
        } else {
            c.setPosition(m_promptEnd);
        }
        setTextCursor(c);
        return;
    }

    // Ctrl+A — select all input (not all text)
    if (event->key() == Qt::Key_A &&
        event->modifiers() == Qt::ControlModifier) {
        QTextCursor c = textCursor();
        c.setPosition(m_promptEnd);
        c.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
        setTextCursor(c);
        return;
    }

    // Ctrl+C — copy selection, or if no selection, cancel input
    if (event->key() == Qt::Key_C &&
        event->modifiers() == Qt::ControlModifier) {
        if (cur.hasSelection()) {
            copy();
        } else {
            // Cancel current input and show a new prompt
            moveCursorToEnd();
            textCursor().insertText(QStringLiteral("\n"));
            showPrompt();
        }
        return;
    }

    // Ctrl+V — paste at cursor (only in editable region)
    if (event->key() == Qt::Key_V &&
        event->modifiers() == Qt::ControlModifier) {
        moveCursorToEnd();
        QString clip = QApplication::clipboard()->text();
        if (!clip.isEmpty()) {
            // Only take the first line to avoid multi-line paste issues
            int nl = clip.indexOf('\n');
            if (nl >= 0) clip = clip.left(nl);
            insertPlainText(clip);
        }
        return;
    }

    // Ctrl+U — clear input line
    if (event->key() == Qt::Key_U &&
        event->modifiers() == Qt::ControlModifier) {
        setCurrentInput(QString());
        return;
    }

    // Backspace — don't delete past the prompt
    if (event->key() == Qt::Key_Backspace) {
        if (pos <= m_promptEnd) return;
        QPlainTextEdit::keyPressEvent(event);
        return;
    }

    // Delete — only in editable region
    if (event->key() == Qt::Key_Delete) {
        if (pos < m_promptEnd) return;
        QPlainTextEdit::keyPressEvent(event);
        return;
    }

    // Left arrow — don't move past prompt
    if (event->key() == Qt::Key_Left) {
        if (pos <= m_promptEnd &&
            !(event->modifiers() & Qt::ShiftModifier)) {
            return;
        }
        QPlainTextEdit::keyPressEvent(event);
        // Clamp position
        if (textCursor().position() < m_promptEnd) {
            QTextCursor c = textCursor();
            c.setPosition(m_promptEnd);
            setTextCursor(c);
        }
        return;
    }

    // For all other keys — ensure cursor is in the editable region
    if (pos < m_promptEnd) {
        moveCursorToEnd();
    }

    // Reject any remaining modifier combos that might modify text
    // in unexpected ways (but allow Shift for uppercase, etc.)
    if (event->modifiers() & Qt::ControlModifier &&
        event->key() != Qt::Key_Shift) {
        return;
    }

    // Allow normal character input
    if (!event->text().isEmpty()) {
        QPlainTextEdit::keyPressEvent(event);
    }
}

// ---- Mouse handling -------------------------------------------------

void CliPanel::mousePressEvent(QMouseEvent* event)
{
    // Allow clicking for selection, but always keep cursor
    // in the editable area for actual editing
    QPlainTextEdit::mousePressEvent(event);
}

void CliPanel::mouseDoubleClickEvent(QMouseEvent* event)
{
    QPlainTextEdit::mouseDoubleClickEvent(event);
}

void CliPanel::contextMenuEvent(QContextMenuEvent* event)
{
    // Custom context menu with only Copy and Paste
    QMenu* menu = new QMenu(this);

    QAction* copyAction = menu->addAction(tr("Copy"));
    copyAction->setEnabled(textCursor().hasSelection());
    connect(copyAction, &QAction::triggered, this, &QPlainTextEdit::copy);

    QAction* pasteAction = menu->addAction(tr("Paste"));
    connect(pasteAction, &QAction::triggered, this, [this]() {
        moveCursorToEnd();
        QString clip = QApplication::clipboard()->text();
        if (!clip.isEmpty()) {
            int nl = clip.indexOf('\n');
            if (nl >= 0) clip = clip.left(nl);
            insertPlainText(clip);
        }
    });

    menu->exec(event->globalPos());
    delete menu;
}

// ---- Command execution ----------------------------------------------

void CliPanel::executeCurrentLine()
{
    QString input = currentInput().trimmed();

    // Move to end and add a newline after the user's input
    moveCursorToEnd();
    textCursor().insertText(QStringLiteral("\n"));

    if (input.isEmpty()) {
        showPrompt();
        return;
    }

    m_history->append(input);
    m_historyIndex = -1;

    CliResult result = m_engine->execute(input);

    if (!result.output.isEmpty()) {
        appendOutput(result.output);
    }
    if (!result.error.isEmpty()) {
        appendError(result.error);
    }

    if (result.requestExit) {
        emit exitRequested();
        return;
    }

    // Handle viewport actions (emit signals for full mode to connect)
    if (result.viewportAction != ViewportAction::None) {
        if (m_sketchModeActive) {
            // In sketch mode, 3D viewport is not visible
            appendError(tr("Warning: 3D viewport commands are not available in Sketch Mode. "
                           "Finish or discard the sketch first."));
        } else if (!m_viewportConnected && m_guiMode) {
            // Only warn in GUI mode (Reduced Mode) - in pure CLI mode, silently ignore
            appendError(tr("Warning: No 3D viewport available. "
                           "3D viewport commands only work in Full Mode."));
        } else if (m_viewportConnected) {
            switch (result.viewportAction) {
                case ViewportAction::ZoomPercent:
                    emit zoomRequested(result.vpArg1);
                    break;
                case ViewportAction::ZoomHome:
                    emit zoomHomeRequested();
                    break;
                case ViewportAction::PanTo:
                    emit panToRequested(result.vpArg1, result.vpArg2, result.vpArg3);
                    break;
                case ViewportAction::PanHome:
                    emit panHomeRequested();
                    break;
                case ViewportAction::RotateAxis:
                    emit rotateRequested(result.vpAxis, result.vpArg1);
                    break;
                case ViewportAction::RotateHome:
                    emit rotateHomeRequested();
                    break;
                case ViewportAction::None:
                default:
                    break;
            }
        }
    }

    showPrompt();
}

// ---- History navigation ---------------------------------------------

void CliPanel::historyUp()
{
    const auto& entries = m_history->entries();
    if (entries.isEmpty()) return;

    if (m_historyIndex == -1) {
        m_savedInput = currentInput();
        m_historyIndex = entries.size() - 1;
    } else if (m_historyIndex > 0) {
        m_historyIndex--;
    }
    setCurrentInput(entries[m_historyIndex]);
}

void CliPanel::historyDown()
{
    if (m_historyIndex == -1) return;

    const auto& entries = m_history->entries();
    if (m_historyIndex < entries.size() - 1) {
        m_historyIndex++;
        setCurrentInput(entries[m_historyIndex]);
    } else {
        m_historyIndex = -1;
        setCurrentInput(m_savedInput);
    }
}

// ---- Output helpers -------------------------------------------------

void CliPanel::appendOutput(const QString& text)
{
    QTextCursor cur = textCursor();
    cur.movePosition(QTextCursor::End);
    cur.insertText(text + QStringLiteral("\n"));
    setTextCursor(cur);
}

void CliPanel::appendError(const QString& text)
{
    QTextCursor cur = textCursor();
    cur.movePosition(QTextCursor::End);
    cur.insertText(text + QStringLiteral("\n"));
    setTextCursor(cur);
}

void CliPanel::showPrompt()
{
    QString prompt = m_engine->buildPrompt();

    // Append the prompt text.  We use textCursor() to insert
    // without the automatic newline that appendPlainText adds.
    QTextCursor cur = textCursor();
    cur.movePosition(QTextCursor::End);
    cur.insertText(prompt);
    setTextCursor(cur);

    // Record where the editable region starts
    m_promptEnd = cur.position();

    ensureCursorVisible();
}

void CliPanel::moveCursorToEnd()
{
    QTextCursor cur = textCursor();
    cur.movePosition(QTextCursor::End);
    setTextCursor(cur);
    ensureCursorVisible();
}

QString CliPanel::currentInput() const
{
    QString allText = toPlainText();
    if (m_promptEnd >= allText.length()) return QString();
    return allText.mid(m_promptEnd);
}

void CliPanel::setCurrentInput(const QString& text)
{
    QTextCursor cur = textCursor();
    cur.setPosition(m_promptEnd);
    cur.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    cur.removeSelectedText();
    cur.insertText(text);
    setTextCursor(cur);
    ensureCursorVisible();
}

void CliPanel::setViewportConnected(bool connected)
{
    m_viewportConnected = connected;
}

void CliPanel::setGuiMode(bool guiMode)
{
    m_guiMode = guiMode;
}

void CliPanel::setSketchModeActive(bool active)
{
    m_sketchModeActive = active;
}

}  // namespace hobbycad

