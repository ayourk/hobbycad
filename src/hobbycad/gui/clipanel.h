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
#include <QStringList>

namespace hobbycad {

class CliEngine;
class CliHistory;

class CliPanel : public QPlainTextEdit {
    Q_OBJECT

public:
    /// The dispatch engine, so the host window can attach itself as the
    /// document undo host; `undo` at the prompt must drive the SAME
    /// history as Edit > Undo, not a private one.
    CliEngine* engine() const { return m_engine; }

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

public:
    /// Feed paginated output directly, for tests.
    ///
    /// The normal path runs a command through the engine; this skips that
    /// so the paging behavior can be exercised on its own.
    void appendPaginatedForTest(const QString& text) { appendOutput(text, true); }

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

    /// Resizing changes how much fits, so it changes how much to reveal.
    void resizeEvent(QResizeEvent* event) override;

private:
    /// Append command output.
    ///
    /// @param scrollToStart  For output a person is meant to read, put the
    ///        view at the FIRST line of it rather than the last.
    void appendOutput(const QString& text, bool scrollToStart = false);

    /// Lines that fit in the viewport right now.
    int visibleLineCount() const;

    /// Reveal up to one viewport's worth of held-back output.
    ///
    /// Aaron, 2026-08-27: a 50-line result in a 25-line viewport, shrunk to
    /// 20, should adjust rather than be left mismatched, and if the
    /// viewport grows past the whole remainder, the pager should show the
    /// rest and finish. A GUI owns its widget, so unlike a terminal pager
    /// it can respond to a resize instead of leaving it to whatever the
    /// terminal did.
    /// @param all  Reveal everything remaining, not just one page.
    /// @param all      Reveal everything remaining, not just one page.
    /// @param markEnd  Write "(END)" where the marker was, so a pager that
    ///        finished on its own (because the panel grew) leaves a
    ///        visible record of where the output stopped.
    void revealMore(bool all = false, bool markEnd = false);

    /// True while output is being held back a page at a time.
    bool paging() const { return !m_pending.isEmpty(); }
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

    /// Output not yet shown, oldest first. Empty when not paging.
    QStringList m_pending;

    /// Where the "-- more --" marker was written, so it can be replaced.
    int         m_moreMarkerStart = -1;

    /// Where "(END)" was written, or -1. Transient: it is acknowledgement,
    /// not content, so the next keypress clears it and the prompt takes
    /// its line.
    int         m_endMarkerStart = -1;

    int         m_historyIndex = -1;
    QString     m_savedInput;

    bool        m_viewportConnected = false;
    bool        m_guiMode = false;
    bool        m_sketchModeActive = false;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_CLIPANEL_H

