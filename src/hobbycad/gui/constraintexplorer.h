// =====================================================================
//  src/hobbycad/gui/constraintexplorer.h — Constraint list panel
// =====================================================================
//
//  Lists the constraints on the active sketch, so they can be found,
//  inspected and removed without hunting for their glyph on the canvas.
//
//  This exists because canvas-only interaction is not sufficient: a
//  constraint whose glyph is hidden behind or overlapping another cannot be
//  picked at all, and an over-constrained sketch cannot be diagnosed without
//  seeing the whole list. Modeled on the constraint explorer in jsketcher
//  (MIT): design only, no code taken; see HobbyCAD-outside/reference/.
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================
#ifndef HOBBYCAD_CONSTRAINTEXPLORER_H
#define HOBBYCAD_CONSTRAINTEXPLORER_H

#include "../i18n/retranslatable.h"

#include <QVector>
#include <QWidget>

class QListWidget;
class QListWidgetItem;
class QLabel;
class QPushButton;

namespace hobbycad {

class SketchCanvas;

/// Panel listing the constraints of the active sketch.
class ConstraintExplorer : public QWidget, public Retranslatable {
    Q_OBJECT

public:
    explicit ConstraintExplorer(QWidget* parent = nullptr);

    /// Attach to a canvas. Safe to call with nullptr to detach.
    void setSketchCanvas(SketchCanvas* canvas);

    void retranslate() override;

public slots:
    /// Rebuild the list from the canvas. Cheap; sketches hold tens of
    /// constraints, not thousands.
    void refresh();

private slots:
    void onRowActivated(QListWidgetItem* item);
    /// Run the (expensive) redundancy search and mark the candidates.
    void onFindRedundantClicked();
    void onDeleteClicked();
    void onSelectionChanged();

private:
    int  selectedConstraintId() const;
    void updateButtons();

    SketchCanvas* m_canvas = nullptr;

    QLabel*       m_summary      = nullptr;
    QListWidget*  m_list         = nullptr;
    QPushButton*  m_deleteButton = nullptr;
    QPushButton*  m_findRedundantButton = nullptr;

    /// Candidates from the last search, cleared on every refresh so the
    /// marking can never outlive the sketch state that produced it.
    QVector<int>  m_redundant;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_CONSTRAINTEXPLORER_H
