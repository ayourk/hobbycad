// =====================================================================
//  src/hobbycad/gui/tools/optoolhandlers.cpp
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#include "optoolhandlers.h"
#include <functional>

#include "../sketchcanvas.h"
#include "../sketchutils.h"

#include <hobbycad/sketch/operations.h>
#include <hobbycad/geometry/intersections.h>

#include <QCoreApplication>
#include <QInputDialog>
#include <QMessageBox>
#include <QDialog>
#include <QVBoxLayout>
#include <QListWidget>
#include <QDialogButtonBox>
#include <QLabel>
#include <QAbstractItemView>
#include <QMouseEvent>
#include <QPointF>

#include <cmath>

namespace hobbycad {

QString TrimHandler::hint(const SketchCanvas&) const
{
    return tr("Trim: click a segment to trim (or delete if unbounded); "
               "drag across several to trim each");
}

bool TrimHandler::mousePress(SketchCanvas& canvas, QMouseEvent* event, const QPointF& worldPos)
{
    (void)event;
    // Start a drag-through gesture. A click trims to the next intersection or,
    // with no intersection, deletes the geometry, the way Fusion's Trim
    // does; there is no dialog.
    m_dragging = true;
    m_handled.clear();
    const int hitId = canvas.pick(worldPos);
    if (hitId >= 0) {
        canvas.trimEntityAt(hitId, worldPos, /*deleteIfNoIntersection=*/true);
        m_handled.insert(hitId);
        canvas.clearSelection();
    }
    return true;
}

bool TrimHandler::mouseMove(SketchCanvas& canvas, QMouseEvent* event, const QPointF& worldPos)
{
    if (!m_dragging || !(event->buttons() & Qt::LeftButton)) return false;
    const int hitId = canvas.pick(worldPos);
    if (hitId >= 0 && !m_handled.contains(hitId)) {
        // Drag-through: trim each segment the cursor touches. A segment with no
        // intersection is left alone during a drag (only a deliberate click
        // deletes an unbounded curve), so brushing past one does not wipe it.
        if (canvas.trimEntityAt(hitId, worldPos, /*deleteIfNoIntersection=*/false)) {
            m_handled.insert(hitId);
        }
    }
    return true;   // consume moves for the duration of the trim drag
}

bool TrimHandler::mouseRelease(SketchCanvas&, QMouseEvent*, const QPointF&)
{
    const bool was = m_dragging;
    m_dragging = false;
    m_handled.clear();
    return was;
}

QString ExtendHandler::hint(const SketchCanvas&) const
{
    return tr("Extend: click the end to extend to the next entity");
}

bool ExtendHandler::mousePress(SketchCanvas& canvas, QMouseEvent* event, const QPointF& worldPos)
{
    (void)event;
    // Extend to the next boundary. Fail-safe (Fusion F-64): with no boundary
    // in the extension direction there is nothing to reach, so the geometry is
    // left untouched: a quiet no-op, no dialog.
    const int hitId = canvas.pick(worldPos);
    if (hitId >= 0) {
        canvas.extendEntityTo(hitId, worldPos);
    }
    return true;
}

QString SplitHandler::hint(const SketchCanvas&) const
{
    return tr("Split: click where the entity should be divided");
}

bool SplitHandler::mousePress(SketchCanvas& canvas, QMouseEvent* event, const QPointF& worldPos)
{
    (void)event; (void)worldPos;
                    // Split tool behavior depends on selection state:
                    // 1. If a line and point are selected, split line at point
                    // 2. If two entities are selected, split at their intersection
                    // 3. Otherwise, click to split at that point or at all intersections
                    int hitId = canvas.pick(worldPos);

                    // Check if we have pre-selected entities for targeted split
                    if (canvas.selectedEntityIds().size() == 2) {
                        // Two entities selected - find their intersection and split there
                        QList<int> ids = canvas.selectedEntityIds().values();
                        int id1 = ids[0];
                        int id2 = ids[1];

                        const SketchEntity* e1 = canvas.findEntity(id1);
                        const SketchEntity* e2 = canvas.findEntity(id2);

                        if (e1 && e2) {
                            // Check if one is a point - split the other at that point
                            if (e1->type == SketchEntityType::Point && !e1->points.empty()) {
                                QVector<int> newIds = canvas.splitEntityAt(id2, e1->points[0]);
                                if (!newIds.isEmpty()) {
                                    canvas.clearSelection();
                                    for (int id : newIds) canvas.selectEntity(id, true);
                                }
                            } else if (e2->type == SketchEntityType::Point && !e2->points.empty()) {
                                QVector<int> newIds = canvas.splitEntityAt(id1, e2->points[0]);
                                if (!newIds.isEmpty()) {
                                    canvas.clearSelection();
                                    for (int id : newIds) canvas.selectEntity(id, true);
                                }
                            } else {
                                // Split both entities at EVERY point where they
                                // cross, not just the first. A secant line gains
                                // both cuts, and a circle divides into arcs
                                // (two crossings -> two arcs summing 360).
                                QVector<SketchCanvas::Intersection> allIntersections = canvas.findAllIntersections();
                                QVector<QPointF> splitPoints;

                                for (const SketchCanvas::Intersection& inter : allIntersections) {
                                    if ((inter.entityId1 == id1 && inter.entityId2 == id2) ||
                                        (inter.entityId1 == id2 && inter.entityId2 == id1)) {
                                        splitPoints.append(inter.point);
                                    }
                                }

                                if (!splitPoints.isEmpty()) {
                                    // Split both entities at their crossings.
                                    QVector<int> newIds1 = canvas.splitEntityAtPoints(id1, splitPoints);
                                    QVector<int> newIds2 = canvas.splitEntityAtPoints(id2, splitPoints);
                                    canvas.clearSelection();
                                    for (int id : newIds1) canvas.selectEntity(id, true);
                                    for (int id : newIds2) canvas.selectEntity(id, true);
                                } else {
                                    QMessageBox::information(&canvas, tr("Split"),
                                        tr("No intersection found between selected entities."));
                                }
                            }
                        }
                    } else if (hitId >= 0) {
                        // No pre-selection or single selection - split clicked entity
                        QVector<int> newIds = canvas.splitEntityAtIntersections(hitId);
                        if (newIds.isEmpty()) {
                            // No intersections - try splitting at click point
                            newIds = canvas.splitEntityAt(hitId, worldPos);
                            if (newIds.isEmpty()) {
                                QMessageBox::information(&canvas, tr("Split"),
                                    tr("Could not split entity at this location."));
                            }
                        }
                        if (!newIds.isEmpty()) {
                            canvas.clearSelection();  // Deselect since original entity is deleted
                        }
    }
    return true;
}

QString OffsetHandler::hint(const SketchCanvas&) const
{
    return tr("Offset: click an entity, then drag to set the distance");
}

bool OffsetHandler::mousePress(SketchCanvas& canvas, QMouseEvent* event, const QPointF& worldPos)
{
    (void)event; (void)worldPos;
                    // Offset tool: click on entity to create parallel geometry
                    int hitId = canvas.pick(worldPos);
                    if (hitId >= 0) {
                        const SketchEntity* entity = canvas.findEntity(hitId);
                        if (entity && (entity->type == SketchEntityType::Line ||
                                       entity->type == SketchEntityType::Circle ||
                                       entity->type == SketchEntityType::Arc)) {
                            // Prompt for offset distance
                            bool ok = false;
                            double distance = QInputDialog::getDouble(
                                &canvas,
                                tr("Offset Distance"),
                                tr("Enter offset distance (mm):"),
                                5.0,    // default
                                0.1,    // minimum
                                1000.0, // maximum
                                2,      // decimals
                                &ok
                            );

                            if (ok) {
                                // Determine offset direction based on click position relative to entity
                                canvas.offsetEntity(hitId, distance, worldPos);
                            }
                        } else {
                            QMessageBox::information(&canvas, tr("Offset"),
                                tr("Offset is supported for lines, circles, and arcs."));
                        }
    }
    return true;
}

namespace {
// Fillet and chamfer share their click: a line near a corner, the line it
// meets there, a size from a dialog, then the operation. Only the words and
// the operation differ.
void cornerOperation(SketchCanvas& canvas, const QPointF& worldPos, const QString& title,
                     const QString& prompt, const QString& noCornerTitle, const QString& noCornerText,
                     const QString& needLinesText, const std::function<void(int, int, double)>& apply)
{
    int hitId = canvas.pick(worldPos);
    if (hitId < 0) return;
    const SketchEntity* entity = canvas.findEntity(hitId);
    if (!entity || entity->type != SketchEntityType::Line) {
        QMessageBox::information(&canvas, noCornerTitle, needLinesText);
        return;
    }
    // Find connected line at closest endpoint
    int connectedId = sketch::findConnectedLineAtCorner(
        toLibraryEntity(*entity), toLibraryEntities(canvas.entities()), worldPos);
    if (connectedId < 0) {
        QMessageBox::information(&canvas, noCornerTitle, noCornerText);
        return;
    }
    bool ok = false;
    double size = QInputDialog::getDouble(&canvas, title, prompt,
                                          5.0,     // default
                                          0.1,     // minimum
                                          1000.0,  // maximum
                                          2,       // decimals
                                          &ok);
    if (ok) apply(hitId, connectedId, size);
}
}  // namespace

QString FilletHandler::hint(const SketchCanvas&) const
{
    return tr("Fillet: click a corner between two lines to round it");
}

bool FilletHandler::mousePress(SketchCanvas& canvas, QMouseEvent* event, const QPointF& worldPos)
{
    (void)event;
    cornerOperation(canvas, worldPos, tr("Fillet Radius"), tr("Enter fillet radius (mm):"), tr("Fillet"), tr("Click on a corner where two lines meet."), tr("Fillet requires two connected lines. Click on a line near a corner."),
                    [&canvas](int lineId, int otherId, double size) { canvas.filletCorner(lineId, otherId, size); });
    return true;
}

QString ChamferHandler::hint(const SketchCanvas&) const
{
    return tr("Chamfer: click a corner between two lines to bevel it");
}

bool ChamferHandler::mousePress(SketchCanvas& canvas, QMouseEvent* event, const QPointF& worldPos)
{
    (void)event;
    cornerOperation(canvas, worldPos, tr("Chamfer Distance"), tr("Enter chamfer distance (mm):"), tr("Chamfer"), tr("Click on a corner where two lines meet."), tr("Chamfer requires two connected lines. Click on a line near a corner."),
                    [&canvas](int lineId, int otherId, double size) { canvas.chamferCorner(lineId, otherId, size); });
    return true;
}

QString RectPatternHandler::hint(const SketchCanvas&) const
{
    return tr("Rectangular pattern: select entities to repeat  (Ctrl = add)");
}

bool RectPatternHandler::mousePress(SketchCanvas& canvas, QMouseEvent* event, const QPointF& worldPos)
{
    (void)event; (void)worldPos;
                    // Rectangular pattern: select entities then configure pattern
                    int hitId = canvas.pick(worldPos);
                    if (hitId >= 0) {
                        // Add to selection for pattern
                        bool ctrlHeld = (event->modifiers() & Qt::ControlModifier);
                        canvas.selectEntity(hitId, ctrlHeld);
                        canvas.update();

                        // If we have a selection, offer to create pattern
                        if (!canvas.selectedEntityIds().isEmpty()) {
                            canvas.createRectangularPattern();
                        }
    }
    return true;
}

QString CircPatternHandler::hint(const SketchCanvas&) const
{
    return tr("Circular pattern: select entities to repeat  (Ctrl = add)");
}

bool CircPatternHandler::mousePress(SketchCanvas& canvas, QMouseEvent* event, const QPointF& worldPos)
{
    (void)event; (void)worldPos;
                    // Circular pattern: select entities then configure pattern
                    int hitId = canvas.pick(worldPos);
                    if (hitId >= 0) {
                        // Add to selection for pattern
                        bool ctrlHeld = (event->modifiers() & Qt::ControlModifier);
                        canvas.selectEntity(hitId, ctrlHeld);
                        canvas.update();

                        // If we have a selection, offer to create pattern
                        if (!canvas.selectedEntityIds().isEmpty()) {
                            canvas.createCircularPattern();
                        }
    }
    return true;
}

// Open a picker listing geometry in OTHER sketches and project the chosen
// entities into this sketch as reference. The canvas cannot draw or hit-test
// foreign geometry, so selection is a list rather than an on-canvas click.
static void openProjectionPicker(SketchCanvas& canvas)
{
    const auto sources = canvas.availableProjectionSources();
    if (sources.empty()) {
        QMessageBox::information(&canvas, ProjectHandler::tr("Projection"),
            ProjectHandler::tr("There is no other sketch geometry to project.\n\n"
               "Projection brings geometry from another sketch into this one as "
               "reference (it stays driven by its source). Create geometry in "
               "another sketch first."));
        return;
    }

    QDialog dlg(&canvas);
    dlg.setWindowTitle(ProjectHandler::tr("Project geometry"));
    auto* lay = new QVBoxLayout(&dlg);
    lay->addWidget(new QLabel(ProjectHandler::tr("Choose geometry from another sketch to "
        "project into this sketch as reference:"), &dlg));
    auto* list = new QListWidget(&dlg);
    list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    for (const auto& src : sources) {
        auto* item = new QListWidgetItem(src.label, list);
        item->setData(Qt::UserRole, src.sketchId);
        item->setData(Qt::UserRole + 1, src.entityId);
    }
    lay->addWidget(list);
    auto* box = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    lay->addWidget(box);
    QObject::connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return;

    int created = 0, refused = 0;
    for (QListWidgetItem* item : list->selectedItems()) {
        const int sid = item->data(Qt::UserRole).toInt();
        const int eid = item->data(Qt::UserRole + 1).toInt();
        if (canvas.createProjection(sid, eid) >= 0) ++created;
        else ++refused;
    }
    if (refused > 0) {
        QMessageBox::information(&canvas, ProjectHandler::tr("Projection"),
            ProjectHandler::tr("Projected %1 item(s); %2 could not be projected yet "
               "(circles and arcs project to ellipses, which is not supported "
               "yet).").arg(created).arg(refused));
    }
    // A projection is a one-shot command; return to Select afterwards.
    canvas.setActiveTool(SketchTool::Select);
}

QString ProjectHandler::hint(const SketchCanvas&) const
{
    return tr("Project: choose geometry from another sketch to bring in as reference");
}

void ProjectHandler::begin(SketchCanvas& canvas)
{
    // Selecting the tool immediately opens the picker; there is nothing on
    // this canvas to click, since foreign geometry is not drawn here.
    openProjectionPicker(canvas);
}

bool ProjectHandler::mousePress(SketchCanvas& canvas, QMouseEvent* event, const QPointF& worldPos)
{
    (void)event; (void)worldPos;
    openProjectionPicker(canvas);   // re-open if the user clicks
    return true;
}

std::vector<std::unique_ptr<SketchToolHandler>> makeOperationToolHandlers()
{
    std::vector<std::unique_ptr<SketchToolHandler>> v;
    v.push_back(std::make_unique<TrimHandler>());
    v.push_back(std::make_unique<ExtendHandler>());
    v.push_back(std::make_unique<SplitHandler>());
    v.push_back(std::make_unique<OffsetHandler>());
    v.push_back(std::make_unique<FilletHandler>());
    v.push_back(std::make_unique<ChamferHandler>());
    v.push_back(std::make_unique<RectPatternHandler>());
    v.push_back(std::make_unique<CircPatternHandler>());
    v.push_back(std::make_unique<ProjectHandler>());
    return v;
}

}  // namespace hobbycad
