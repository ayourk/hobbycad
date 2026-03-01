// =====================================================================
//  src/hobbycad/gui/changelogpanel.cpp — Undo history panel
// =====================================================================
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include "changelogpanel.h"
#include "sketchcanvas.h"

#include <QLabel>
#include <QListWidget>
#include <QVBoxLayout>

namespace hobbycad {

// =====================================================================
//  Construction
// =====================================================================

ChangelogPanel::ChangelogPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_listWidget = new QListWidget(this);
    m_listWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_listWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(m_listWidget);

    m_emptyLabel = new QLabel(tr("No history"), this);
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setStyleSheet(
        QStringLiteral("color: #888888; font-style: italic; padding: 16px;"));
    layout->addWidget(m_emptyLabel);

    connect(m_listWidget, &QListWidget::currentRowChanged,
            this, &ChangelogPanel::onItemClicked);
}

// =====================================================================
//  Canvas connection
// =====================================================================

void ChangelogPanel::setSketchCanvas(SketchCanvas* canvas)
{
    if (m_canvas) {
        disconnect(m_canvas, nullptr, this, nullptr);
    }

    m_canvas = canvas;

    if (m_canvas) {
        connect(m_canvas, &SketchCanvas::undoStackChanged,
                this, &ChangelogPanel::refresh);
        refresh();
    } else {
        m_listWidget->clear();
        m_listWidget->setVisible(false);
        m_emptyLabel->setVisible(true);
    }
}

void ChangelogPanel::clearSketchCanvas()
{
    setSketchCanvas(nullptr);
}

// =====================================================================
//  Refresh
// =====================================================================

void ChangelogPanel::refresh()
{
    buildList();
}

void ChangelogPanel::buildList()
{
    // Block signals to prevent onItemClicked during rebuild
    m_listWidget->blockSignals(true);
    m_listWidget->clear();
    m_undoCount = 0;

    if (!m_canvas) {
        m_listWidget->setVisible(false);
        m_emptyLabel->setVisible(true);
        m_listWidget->blockSignals(false);
        return;
    }

    QStringList undoDescs = m_canvas->undoDescriptions();  // Most recent first
    QStringList redoDescs = m_canvas->redoDescriptions();  // Most recent first

    bool hasEntries = !undoDescs.isEmpty() || !redoDescs.isEmpty();
    m_listWidget->setVisible(hasEntries);
    m_emptyLabel->setVisible(!hasEntries);

    if (!hasEntries) {
        m_listWidget->blockSignals(false);
        return;
    }

    // --- Undo entries (past): most recent first at the top ---
    m_undoCount = undoDescs.size();
    for (int i = 0; i < undoDescs.size(); ++i) {
        auto* item = new QListWidgetItem(undoDescs[i], m_listWidget);
        if (i == 0) {
            // Highlight the most recent (current) entry
            item->setBackground(QColor(0, 120, 212, 40));
            QFont font = item->font();
            font.setBold(true);
            item->setFont(font);
        }
    }

    // --- Redo entries (future): shown after undo entries, grayed out ---
    // redoDescs is most-recent-first, so the first element is the next
    // operation to redo (closest to the current position).
    for (int i = 0; i < redoDescs.size(); ++i) {
        auto* item = new QListWidgetItem(redoDescs[i], m_listWidget);
        item->setForeground(QColor(128, 128, 128));
        QFont font = item->font();
        font.setItalic(true);
        item->setFont(font);
    }

    // Select the current position (first undo entry, row 0)
    if (m_undoCount > 0) {
        m_listWidget->setCurrentRow(0);
    }

    m_listWidget->blockSignals(false);
}

// =====================================================================
//  Click-to-navigate
// =====================================================================

void ChangelogPanel::onItemClicked(int row)
{
    if (!m_canvas || row < 0) return;

    // Current position is row 0 (the most recent undo entry).
    // Rows 0..undoCount-1 are undo entries (most recent first).
    // Rows undoCount..end are redo entries (most recent redo first).

    if (row == 0 && m_undoCount > 0) {
        // Already at current position, nothing to do
        return;
    }

    if (row < m_undoCount) {
        // Clicked an older undo entry — need to undo 'row' times
        // (row 1 = undo once, row 2 = undo twice, etc.)
        m_canvas->undoMultiple(row);
    } else {
        // Clicked a redo entry — need to redo
        // First redo entry is at row m_undoCount
        int redoIndex = row - m_undoCount;
        // Need to redo (redoIndex + 1) times to reach that point
        m_canvas->redoMultiple(redoIndex + 1);
    }
    // refresh() will be called automatically via undoStackChanged signal
}

}  // namespace hobbycad
