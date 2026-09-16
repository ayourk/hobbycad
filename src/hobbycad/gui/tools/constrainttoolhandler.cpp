// =====================================================================
//  src/hobbycad/gui/tools/constrainttoolhandler.cpp
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#include "constrainttoolhandler.h"
#include "../sketchcanvas.h"

#include <QCoreApplication>
#include <QMouseEvent>

namespace hobbycad {

QString ConstraintToolHandler::hint(const SketchCanvas& canvas) const
{
    if (canvas.constraintToolType() >= 0) {
        const auto t = static_cast<sketch::ConstraintType>(canvas.constraintToolType());
        return QCoreApplication::translate("hobbycad::SketchCanvas", "%1: click geometry to apply")
            .arg(QString::fromUtf8(sketch::constraintTypeName(t)));
    }
    return QCoreApplication::translate(
        "hobbycad::SketchCanvas",
        "Constraint: select entities, then choose a constraint");
}

bool ConstraintToolHandler::mousePress(SketchCanvas& canvas, QMouseEvent* event,
                                       const QPointF& world)
{
    // Tool-first mode: a specific constraint type is armed (e.g. from the
    // Constraints menu with nothing selected). Click geometry to build up the
    // targets, apply the chosen constraint when enough are picked, then stay
    // active for the next one, the way Fusion's per-type tools work.
    if (canvas.constraintToolType() >= 0) {
        const auto t = static_cast<sketch::ConstraintType>(canvas.constraintToolType());
        int pe = -1, pi = -1;
        if (canvas.pickAnyPoint(world, pe, pi)) {
            canvas.addSelectedPoint(pe, pi);
        } else {
            const int hit = canvas.pick(world);
            if (hit < 0) { canvas.update(); return true; }   // empty click: keep waiting
            canvas.selectAddIndividual(hit);
        }
        canvas.update();
        const int need = sketch::requiredEntityCount(t);
        const int have = static_cast<int>(canvas.selectedPoints().size()) + canvas.selectionCount();
        if (have >= need) {
            canvas.applyTypedConstraint(t);
            canvas.clearSelection();   // ready for the next; the type stays armed
        }
        return true;
    }

    const int hitId = canvas.pick(world);
    if (hitId < 0) {
        canvas.selectClearIds();
        canvas.update();
        return true;
    }

    if (!(event->modifiers() & Qt::ControlModifier)
        && !canvas.isEntitySelected(hitId)) {
        // Plain click starts a fresh pair; Ctrl accumulates.
        if (canvas.selectionCount() >= 2) {
            canvas.selectClearIds();
        }
    }

    canvas.selectAddIndividual(hitId);
    canvas.update();

    if (canvas.selectionCount() >= 2) {
        canvas.applySelectionConstraint();
    }
    return true;
}

}  // namespace hobbycad
