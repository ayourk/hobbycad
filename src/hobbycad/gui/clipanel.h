// =====================================================================
//  src/hobbycad/gui/clipanel.h — Embedded CLI terminal panel
// =====================================================================
//
//  A single QPlainTextEdit that behaves like a terminal emulator.
//  The prompt and user input appear on the same line at the bottom
//  of the document.  Text above the current prompt is read-only.
//  The cursor sits at the end of the prompt line, ready for input.
//
//  Uses CliEngine for command dispatch and CliHistory for arrow-key
//  history navigation.
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_CLIPANEL_H
#define HOBBYCAD_CLIPANEL_H

#include <QPlainTextEdit>

namespace hobbycad {

class CliEngine;
class CliHistory;

class CliPanel : public QPlainTextEdit {
    Q_OBJECT

public:
    explicit CliPanel(QWidget* parent = nullptr);
    ~CliPanel() override;

    /// Give keyboard focus to this widget and place the cursor
    /// at the end of the current prompt line.
    void focusInput();

signals:
    /// Emitted when the user types "exit" or "quit".
    void exitRequested();

    // ---- Viewport command signals (full mode only) ----

    /// Emitted for "zoom <percent>" command.
    void zoomRequested(double percent);

    /// Emitted for "zoom home" command (reset to fit all).
    void zoomHomeRequested();

    /// Emitted for "panto <x>,<y>,<z>" command.
    void panToRequested(double x, double y, double z);

    /// Emitted for "panto home" command (pan to origin).
    void panHomeRequested();

    /// Emitted for "rotate on <axis> <degrees>" command.
    void rotateRequested(char axis, double degrees);

    /// Emitted for "rotate home" command (reset to isometric).
    void rotateHomeRequested();

public slots:
    /// Call this to indicate that a viewport is connected and commands will work.
    void setViewportConnected(bool connected);

    /// Call this to indicate we're running in GUI mode (show warnings for missing viewport).
    void setGuiMode(bool guiMode);

    /// Call this to indicate we're in sketch mode (viewport commands unavailable).
    void setSketchModeActive(bool active);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    void appendOutput(const QString& text);
    void appendError(const QString& text);
    void showPrompt();
    void executeCurrentLine();
    void moveCursorToEnd();
    void historyUp();
    void historyDown();

    /// Returns the text the user has typed after the prompt.
    QString currentInput() const;

    /// Replaces the text after the prompt with the given string.
    void setCurrentInput(const QString& text);

    /// Character position where the editable region begins
    /// (immediately after the prompt text).
    int m_promptEnd = 0;

    CliHistory* m_history      = nullptr;
    CliEngine*  m_engine       = nullptr;

    int         m_historyIndex = -1;
    QString     m_savedInput;

    bool        m_viewportConnected = false;
    bool        m_guiMode = false;
    bool        m_sketchModeActive = false;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_CLIPANEL_H

