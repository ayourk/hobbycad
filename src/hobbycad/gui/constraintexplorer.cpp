// =====================================================================
//  src/hobbycad/gui/constraintexplorer.cpp — Constraint list panel
// =====================================================================
//
//  Part of HobbyCAD.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include <hobbycad/sketch/solver.h>
#include <QApplication>
#include "constraintexplorer.h"

#include "sketchcanvas.h"

#include <hobbycad/sketch/constraint.h>

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace hobbycad {

namespace {

/// Role holding the constraint id on each row.
constexpr int kConstraintIdRole = Qt::UserRole + 1;

/// Human-readable row text: "3. Distance = 25.4 mm" / "4. Perpendicular".
QString describe(const SketchConstraint& c, int ordinal)
{
    QString text = QStringLiteral("%1. %2")
                       .arg(ordinal)
                       .arg(QString::fromUtf8(sketch::constraintTypeName(c.type)));

    // Only dimensional constraints carry a meaningful value; showing "= 0" on
    // a Perpendicular would be noise.
    switch (c.type) {
    case sketch::ConstraintType::Distance:
    case sketch::ConstraintType::Radius:
    case sketch::ConstraintType::Diameter:
        text += QStringLiteral(" = %1 mm").arg(c.value, 0, 'g', 6);
        break;
    case sketch::ConstraintType::Angle:
    case sketch::ConstraintType::FixedAngle:
        text += QStringLiteral(" = %1°").arg(c.value, 0, 'g', 6);
        break;
    default:
        break;
    }
    return text;
}

}  // namespace

ConstraintExplorer::ConstraintExplorer(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    layout->addWidget(m_summary);

    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("ConstraintList"));
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setAlternatingRowColors(true);
    layout->addWidget(m_list, 1);

    auto* buttons = new QHBoxLayout();
    buttons->setContentsMargins(0, 0, 0, 0);
    // Deliberately a BUTTON and not something that runs automatically: the
    // search re-solves the sketch once per constraint, so it is far too
    // expensive to run on every edit. Enabled only when there is actually a
    // redundancy to find.
    m_findRedundantButton = new QPushButton(this);
    m_findRedundantButton->setEnabled(false);
    buttons->addWidget(m_findRedundantButton);

    m_deleteButton = new QPushButton(this);
    m_deleteButton->setEnabled(false);
    buttons->addStretch(1);
    buttons->addWidget(m_deleteButton);
    layout->addLayout(buttons);

    connect(m_list, &QListWidget::itemActivated,
            this, &ConstraintExplorer::onRowActivated);
    connect(m_list, &QListWidget::itemClicked,
            this, &ConstraintExplorer::onRowActivated);
    connect(m_list, &QListWidget::itemSelectionChanged,
            this, &ConstraintExplorer::onSelectionChanged);
    connect(m_deleteButton, &QPushButton::clicked,
            this, &ConstraintExplorer::onDeleteClicked);
    connect(m_findRedundantButton, &QPushButton::clicked,
            this, &ConstraintExplorer::onFindRedundantClicked);

    retranslate();
    refresh();
}

void ConstraintExplorer::setSketchCanvas(SketchCanvas* canvas)
{
    if (m_canvas == canvas) return;

    if (m_canvas) m_canvas->disconnect(this);
    m_canvas = canvas;

    if (m_canvas) {
        // Any of these can change what the list should show.
        connect(m_canvas, &SketchCanvas::constraintCreated,
                this, &ConstraintExplorer::refresh);
        connect(m_canvas, &SketchCanvas::constraintDeleted,
                this, &ConstraintExplorer::refresh);
        connect(m_canvas, &SketchCanvas::constraintModified,
                this, &ConstraintExplorer::refresh);
        connect(m_canvas, &SketchCanvas::sketchConstraintStateChanged,
                this, [this](sketch::SketchState, int) { refresh(); });
    }
    refresh();
}

void ConstraintExplorer::refresh()
{
    // Preserve the selected constraint across a rebuild; the row index is not
    // stable but the id is.
    const int previous = selectedConstraintId();

    m_list->clear();

    if (!m_canvas) {
        m_summary->setText(tr("No sketch open."));
        updateButtons();
        return;
    }

    const QVector<SketchConstraint>& constraints = m_canvas->constraints();

    int ordinal = 0;
    int unsatisfied = 0;
    for (const SketchConstraint& c : constraints) {
        if (!c.enabled) continue;
        ++ordinal;

        auto* item = new QListWidgetItem(describe(c, ordinal), m_list);
        item->setData(kConstraintIdRole, c.id);

        QStringList tip;
        tip << tr("Type: %1").arg(QString::fromUtf8(sketch::constraintTypeName(c.type)));
        if (!c.entityIds.empty()) {
            QStringList ids;
            for (int id : c.entityIds) ids << QString::number(id);
            tip << tr("Entities: %1").arg(ids.join(QStringLiteral(", ")));
        }
        if (!c.isDriving) tip << tr("Reference (not driving)");
        if (!c.satisfied) tip << tr("NOT satisfied by the solver");
        item->setToolTip(tip.join(QChar('\n')));

        // Reference constraints are display-only; italic keeps them visually
        // distinct from the ones actually driving the geometry.
        if (!c.isDriving) {
            QFont f = item->font();
            f.setItalic(true);
            item->setFont(f);
        }
        if (!c.satisfied) {
            ++unsatisfied;
            item->setForeground(QBrush(QColor(200, 60, 60)));
        }

        // A redundancy candidate found by the last search. Amber, not red:
        // this constraint is not WRONG, it just is not adding anything, and
        // coloring it like a failure would misdescribe it.
        if (m_redundant.contains(c.id)) {
            item->setBackground(QBrush(QColor(255, 244, 214)));
            item->setToolTip(item->toolTip() + QChar('\n')
                + tr("Redundant: removing this would clear the "
                     "over-constraint. So would removing any other "
                     "highlighted constraint; pick one."));
        }

        if (c.id == previous) m_list->setCurrentItem(item);
    }

    // NOT tr("%n constraint(s)"): with no translation catalog loaded Qt
    // substitutes %n and leaves the literal "(s)" on screen, so the source
    // language reads badly. Same defect as the status bar had.
    const QString count = (ordinal == 1) ? tr("1 constraint")
                                         : tr("%1 constraints").arg(ordinal);
    if (ordinal == 0) {
        m_summary->setText(tr("No constraints on this sketch."));
    } else if (!m_redundant.isEmpty()) {
        m_summary->setText(
            tr("%1, %2 redundant. Removing any ONE of the highlighted "
               "constraints clears the over-constraint.")
                .arg(count).arg(m_redundant.size()));
    } else if (unsatisfied > 0) {
        m_summary->setText(tr("%1, %2 not satisfied").arg(count).arg(unsatisfied));
    } else {
        m_summary->setText(count);
    }

    updateButtons();
}

int ConstraintExplorer::selectedConstraintId() const
{
    const QListWidgetItem* item = m_list ? m_list->currentItem() : nullptr;
    if (!item || !item->isSelected()) return -1;
    return item->data(kConstraintIdRole).toInt();
}

void ConstraintExplorer::updateButtons()
{
    if (m_deleteButton) m_deleteButton->setEnabled(selectedConstraintId() >= 0);
    // Only offered when there is something to find. The tooltip says why it
    // is unavailable, so a grayed-out button is not a dead end.
    if (m_findRedundantButton) {
        const bool over = m_canvas
            && m_canvas->sketchState() == sketch::SketchState::OverConstrained;
        m_findRedundantButton->setEnabled(over);
        m_findRedundantButton->setToolTip(
            over ? tr("Re-solves the sketch once per constraint to find which "
                      "ones are redundant. May take a moment on a large sketch.")
                 : tr("Available when the sketch is over-constrained."));
    }
}

void ConstraintExplorer::onSelectionChanged()
{
    updateButtons();
}

void ConstraintExplorer::onRowActivated(QListWidgetItem* item)
{
    if (!item || !m_canvas) return;
    const int id = item->data(kConstraintIdRole).toInt();
    m_canvas->setSelectedConstraint(id);
}

void ConstraintExplorer::onFindRedundantClicked()
{
    if (!m_canvas) {
        return;
    }
    // One solve per constraint. Tens of constraints is imperceptible, but say
    // so with the cursor rather than letting a big sketch look frozen.
    QApplication::setOverrideCursor(Qt::WaitCursor);
    m_redundant = m_canvas->findRedundantConstraints();
    QApplication::restoreOverrideCursor();

    // Show them on the canvas too. The list says WHICH; the canvas says
    // where, which is what makes it possible to judge which one to remove.
    m_canvas->setRedundantCandidates(m_redundant);

    if (m_redundant.isEmpty()) {
        // Reachable: the state can change between the button being enabled
        // and being pressed, and a redundancy involving only DRIVEN
        // constraints has no candidate the user can act on.
        m_summary->setText(tr("No single constraint accounts for the "
                              "over-constraint."));
        return;
    }
    refresh();   // repaints the list with the candidates marked
}

void ConstraintExplorer::onDeleteClicked()
{
    const int id = selectedConstraintId();
    if (id < 0 || !m_canvas) return;
    m_canvas->deleteConstraintById(id);   // handles cleanup + undo itself
    // The set just changed, so the previous candidates are stale: they were
    // an answer about a constraint set that no longer exists. Leaving them
    // colored would point at constraints that may now be fine.
    m_redundant.clear();
    m_canvas->setRedundantCandidates({});
    refresh();
}

void ConstraintExplorer::retranslate()
{
    if (m_deleteButton) {
        m_deleteButton->setText(tr("Delete"));
        m_deleteButton->setToolTip(tr("Remove the selected constraint"));
    }
    if (m_findRedundantButton) {
        m_findRedundantButton->setText(tr("Find redundant"));
        // The tooltip is set by updateButtons(), which knows whether the
        // action is currently available; refresh() below calls it.
    }
    refresh();   // row text and the summary line are translated too
}

}  // namespace hobbycad
