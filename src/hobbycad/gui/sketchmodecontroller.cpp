// =====================================================================
//  src/hobbycad/gui/sketchmodecontroller.cpp — Sketch mode controller
// =====================================================================

#include "sketchmodecontroller.h"
#include "sketchactionbar.h"
#include "sketchcanvas.h"
#include "sketchtoolbar.h"
#include "timelinewidget.h"

#include <QLineF>
#include <QStatusBar>
#include <QTreeWidget>

namespace hobbycad {

SketchModeController::SketchModeController(QObject* parent)
    : QObject(parent)
{
}

void SketchModeController::setSketchCanvas(SketchCanvas* canvas)
{
    // Disconnect from old canvas
    if (m_canvas) {
        disconnect(m_canvas, nullptr, this, nullptr);
    }

    m_canvas = canvas;

    // Connect to new canvas
    if (m_canvas) {
        connect(m_canvas, &SketchCanvas::selectionChanged,
                this, &SketchModeController::onSelectionChanged);
        connect(m_canvas, &SketchCanvas::entityCreated,
                this, &SketchModeController::onEntityCreated);
        connect(m_canvas, &SketchCanvas::mousePositionChanged,
                this, [this](const QPointF& pos) {
            if (m_statusBar) {
                m_statusBar->showMessage(
                    tr("X: %1  Y: %2").arg(pos.x(), 0, 'f', 2).arg(pos.y(), 0, 'f', 2));
            }
        });
    }
}

void SketchModeController::setSketchToolbar(SketchToolbar* toolbar)
{
    // Disconnect from old toolbar
    if (m_toolbar) {
        disconnect(m_toolbar, nullptr, this, nullptr);
    }

    m_toolbar = toolbar;

    // Connect to new toolbar
    if (m_toolbar) {
        connect(m_toolbar, &SketchToolbar::toolSelected,
                this, &SketchModeController::onToolSelected);
    }
}

void SketchModeController::setTimeline(TimelineWidget* timeline)
{
    m_timeline = timeline;
}

void SketchModeController::setPropertiesTree(QTreeWidget* tree)
{
    m_propsTree = tree;
}

void SketchModeController::setSketchActionBar(SketchActionBar* actionBar)
{
    // Disconnect from old action bar
    if (m_actionBar) {
        disconnect(m_actionBar, nullptr, this, nullptr);
    }

    m_actionBar = actionBar;

    // Connect to new action bar
    if (m_actionBar) {
        connect(m_actionBar, &SketchActionBar::saveClicked,
                this, &SketchModeController::onSaveClicked);
        connect(m_actionBar, &SketchActionBar::discardClicked,
                this, &SketchModeController::onCancelClicked);
    }
}

void SketchModeController::setStatusBar(QStatusBar* statusBar)
{
    m_statusBar = statusBar;
}

void SketchModeController::setUnitSuffix(const QString& units)
{
    m_unitSuffix = units;
}

SketchPlane SketchModeController::sketchPlane() const
{
    return m_canvas ? m_canvas->sketchPlane() : SketchPlane::XY;
}

void SketchModeController::enter(SketchPlane plane)
{
    if (m_active) return;

    m_active = true;

    // Set up canvas
    if (m_canvas) {
        m_canvas->setSketchPlane(plane);
        m_canvas->clear();
        m_canvas->resetView();
    }

    // Signal to switch UI
    emit showSketchUI();

    // Show action bar
    if (m_actionBar) {
        m_actionBar->setVisible(true);
    }

    // Add sketch to timeline
    QString sketchName;
    if (m_timeline) {
        int sketchCount = 0;
        for (int i = 0; i < m_timeline->itemCount(); ++i) {
            if (m_timeline->featureAt(i) == TimelineFeature::Sketch) {
                ++sketchCount;
            }
        }
        sketchName = tr("Sketch%1").arg(sketchCount + 1);
        m_timeline->addItem(TimelineFeature::Sketch, sketchName);
        m_timeline->setSelectedIndex(m_timeline->itemCount() - 1);
    }

    // Update properties
    updatePropertiesForSketch(sketchName, plane);

    // Update status
    if (m_statusBar) {
        m_statusBar->showMessage(tr("Sketch mode - Draw entities or press Escape to finish"));
    }

    // Focus canvas
    if (m_canvas) {
        m_canvas->setFocus();
    }

    emit entered(plane);
}

void SketchModeController::exit()
{
    if (!m_active) return;

    m_active = false;

    // Signal to switch UI back
    emit showNormalUI();

    // Hide action bar
    if (m_actionBar) {
        m_actionBar->setVisible(false);
    }

    // Clear properties
    if (m_propsTree) {
        m_propsTree->clear();
    }

    // Deselect timeline
    if (m_timeline) {
        m_timeline->setSelectedIndex(-1);
    }

    // Update status
    if (m_statusBar) {
        m_statusBar->showMessage(tr("Sketch finished"), 3000);
    }

    emit exited();
}

void SketchModeController::save()
{
    // Save the sketch entities to the document
    if (m_timeline && m_timeline->itemCount() > 0) {
        int lastIdx = m_timeline->itemCount() - 1;
        if (m_timeline->featureAt(lastIdx) == TimelineFeature::Sketch) {
            int entityCount = m_canvas ? m_canvas->entities().size() : 0;
            if (m_statusBar) {
                m_statusBar->showMessage(
                    tr("Sketch '%1' saved with %2 entities")
                        .arg(m_timeline->nameAt(lastIdx))
                        .arg(entityCount),
                    3000);
            }
        }
    }

    exit();
}

void SketchModeController::discard()
{
    // Discard the sketch - remove from timeline if empty
    if (m_timeline && m_timeline->itemCount() > 0) {
        int lastIdx = m_timeline->itemCount() - 1;
        if (m_timeline->featureAt(lastIdx) == TimelineFeature::Sketch) {
            int entityCount = m_canvas ? m_canvas->entities().size() : 0;
            if (entityCount == 0) {
                m_timeline->removeItem(lastIdx);
                if (m_statusBar) {
                    m_statusBar->showMessage(tr("Empty sketch discarded"), 3000);
                }
            } else {
                if (m_statusBar) {
                    m_statusBar->showMessage(
                        tr("Sketch changes discarded (%1 entities)").arg(entityCount),
                        3000);
                }
            }
        }
    }

    exit();
}

void SketchModeController::onToolSelected(SketchTool tool)
{
    if (m_canvas) {
        m_canvas->setActiveTool(tool);
    }

    // Update status bar with tool hint
    if (m_statusBar) {
        QString hint;
        switch (tool) {
        case SketchTool::Select:
            hint = tr("Click to select entities, drag to move");
            break;
        case SketchTool::Line:
            hint = tr("Click to start line, click again to end");
            break;
        case SketchTool::Rectangle:
            hint = tr("Click and drag to draw rectangle");
            break;
        case SketchTool::Circle:
            hint = tr("Click center, drag to set radius");
            break;
        case SketchTool::Arc:
            hint = tr("Click center, drag to set radius and arc");
            break;
        case SketchTool::Point:
            hint = tr("Click to place construction point");
            break;
        case SketchTool::Dimension:
            hint = tr("Click two points or an entity to add dimension");
            break;
        case SketchTool::Constraint:
            hint = tr("Select entities to add constraints");
            break;
        default:
            hint = tr("Select a tool to start drawing");
        }
        m_statusBar->showMessage(hint);
    }
}

void SketchModeController::onSelectionChanged(int entityId)
{
    if (entityId < 0) {
        // Deselected - show sketch properties
        if (m_active && m_canvas) {
            updatePropertiesForSketch(QString(), m_canvas->sketchPlane());
        }
        return;
    }

    showEntityProperties(entityId);
}

void SketchModeController::onEntityCreated(int entityId)
{
    updateEntityCount();
    showEntityProperties(entityId);
}

void SketchModeController::onFinishRequested()
{
    save();
}

void SketchModeController::onSaveClicked()
{
    save();
}

void SketchModeController::onCancelClicked()
{
    discard();
}

void SketchModeController::updatePropertiesForSketch(const QString& sketchName, SketchPlane plane)
{
    if (!m_propsTree || !m_canvas) return;

    m_propsTree->clear();

    // Sketch name
    auto* nameItem = new QTreeWidgetItem(m_propsTree);
    nameItem->setText(0, tr("Name"));
    nameItem->setText(1, sketchName.isEmpty() ? tr("Sketch") : sketchName);
    nameItem->setFlags(nameItem->flags() | Qt::ItemIsEditable);

    // Plane selection
    auto* planeItem = new QTreeWidgetItem(m_propsTree);
    planeItem->setText(0, tr("Plane"));
    QStringList planes = {tr("XY"), tr("XZ"), tr("YZ")};
    int planeIdx = static_cast<int>(plane);
    planeItem->setText(1, planes.value(planeIdx));
    planeItem->setData(1, Qt::UserRole, QStringLiteral("dropdown"));
    planeItem->setData(1, Qt::UserRole + 1, planes);
    planeItem->setData(1, Qt::UserRole + 2, planeIdx);

    // Grid settings
    auto* gridHeader = new QTreeWidgetItem(m_propsTree);
    gridHeader->setText(0, tr("Grid"));

    auto* showGridItem = new QTreeWidgetItem(gridHeader);
    showGridItem->setText(0, tr("Show Grid"));
    showGridItem->setText(1, m_canvas->isGridVisible() ? tr("Yes") : tr("No"));
    showGridItem->setData(1, Qt::UserRole, QStringLiteral("dropdown"));
    showGridItem->setData(1, Qt::UserRole + 1, QStringList{tr("Yes"), tr("No")});
    showGridItem->setData(1, Qt::UserRole + 2, m_canvas->isGridVisible() ? 0 : 1);

    auto* snapItem = new QTreeWidgetItem(gridHeader);
    snapItem->setText(0, tr("Snap to Grid"));
    snapItem->setText(1, m_canvas->snapToGrid() ? tr("Yes") : tr("No"));
    snapItem->setData(1, Qt::UserRole, QStringLiteral("dropdown"));
    snapItem->setData(1, Qt::UserRole + 1, QStringList{tr("Yes"), tr("No")});
    snapItem->setData(1, Qt::UserRole + 2, m_canvas->snapToGrid() ? 0 : 1);

    auto* spacingItem = new QTreeWidgetItem(gridHeader);
    spacingItem->setText(0, tr("Grid Spacing"));
    spacingItem->setText(1, QStringLiteral("%1 %2")
                         .arg(m_canvas->gridSpacing())
                         .arg(m_unitSuffix));
    spacingItem->setFlags(spacingItem->flags() | Qt::ItemIsEditable);

    // Entities count
    auto* entitiesItem = new QTreeWidgetItem(m_propsTree);
    entitiesItem->setText(0, tr("Entities"));
    entitiesItem->setText(1, QString::number(m_canvas->entities().size()));

    m_propsTree->expandAll();
}

void SketchModeController::showEntityProperties(int entityId)
{
    if (!m_propsTree || !m_canvas) return;

    const SketchEntity* entity = nullptr;
    for (const auto& e : m_canvas->entities()) {
        if (e.id == entityId) {
            entity = &e;
            break;
        }
    }

    if (!entity) return;

    m_propsTree->clear();

    // Entity type
    auto* typeItem = new QTreeWidgetItem(m_propsTree);
    typeItem->setText(0, tr("Type"));
    QString typeName;
    switch (entity->type) {
    case SketchEntityType::Point:     typeName = tr("Point"); break;
    case SketchEntityType::Line:      typeName = tr("Line"); break;
    case SketchEntityType::Rectangle: typeName = tr("Rectangle"); break;
    case SketchEntityType::Circle:    typeName = tr("Circle"); break;
    case SketchEntityType::Arc:       typeName = tr("Arc"); break;
    case SketchEntityType::Spline:    typeName = tr("Spline"); break;
    case SketchEntityType::Text:      typeName = tr("Text"); break;
    case SketchEntityType::Dimension: typeName = tr("Dimension"); break;
    }
    typeItem->setText(1, typeName);

    // Entity ID
    auto* idItem = new QTreeWidgetItem(m_propsTree);
    idItem->setText(0, tr("ID"));
    idItem->setText(1, QString::number(entity->id));

    // Geometry header
    auto* geomHeader = new QTreeWidgetItem(m_propsTree);
    geomHeader->setText(0, tr("Geometry"));

    // Entity-specific properties
    switch (entity->type) {
    case SketchEntityType::Point:
        if (!entity->points.isEmpty()) {
            auto* posItem = new QTreeWidgetItem(geomHeader);
            posItem->setText(0, tr("Position"));
            posItem->setText(1, QStringLiteral("(%1, %2) %3")
                             .arg(entity->points[0].x(), 0, 'f', 2)
                             .arg(entity->points[0].y(), 0, 'f', 2)
                             .arg(m_unitSuffix));
            posItem->setFlags(posItem->flags() | Qt::ItemIsEditable);
        }
        break;

    case SketchEntityType::Line:
        if (entity->points.size() >= 2) {
            auto* p1Item = new QTreeWidgetItem(geomHeader);
            p1Item->setText(0, tr("Start"));
            p1Item->setText(1, QStringLiteral("(%1, %2) %3")
                             .arg(entity->points[0].x(), 0, 'f', 2)
                             .arg(entity->points[0].y(), 0, 'f', 2)
                             .arg(m_unitSuffix));
            p1Item->setFlags(p1Item->flags() | Qt::ItemIsEditable);

            auto* p2Item = new QTreeWidgetItem(geomHeader);
            p2Item->setText(0, tr("End"));
            p2Item->setText(1, QStringLiteral("(%1, %2) %3")
                             .arg(entity->points[1].x(), 0, 'f', 2)
                             .arg(entity->points[1].y(), 0, 'f', 2)
                             .arg(m_unitSuffix));
            p2Item->setFlags(p2Item->flags() | Qt::ItemIsEditable);

            auto* lenItem = new QTreeWidgetItem(geomHeader);
            lenItem->setText(0, tr("Length"));
            double len = QLineF(entity->points[0], entity->points[1]).length();
            lenItem->setText(1, QStringLiteral("%1 %2").arg(len, 0, 'f', 2).arg(m_unitSuffix));
            lenItem->setFlags(lenItem->flags() | Qt::ItemIsEditable);
        }
        break;

    case SketchEntityType::Rectangle:
        if (entity->points.size() >= 2) {
            auto* p1Item = new QTreeWidgetItem(geomHeader);
            p1Item->setText(0, tr("Corner 1"));
            p1Item->setText(1, QStringLiteral("(%1, %2) %3")
                             .arg(entity->points[0].x(), 0, 'f', 2)
                             .arg(entity->points[0].y(), 0, 'f', 2)
                             .arg(m_unitSuffix));

            auto* p2Item = new QTreeWidgetItem(geomHeader);
            p2Item->setText(0, tr("Corner 2"));
            p2Item->setText(1, QStringLiteral("(%1, %2) %3")
                             .arg(entity->points[1].x(), 0, 'f', 2)
                             .arg(entity->points[1].y(), 0, 'f', 2)
                             .arg(m_unitSuffix));

            auto* widthItem = new QTreeWidgetItem(geomHeader);
            widthItem->setText(0, tr("Width"));
            double w = qAbs(entity->points[1].x() - entity->points[0].x());
            widthItem->setText(1, QStringLiteral("%1 %2").arg(w, 0, 'f', 2).arg(m_unitSuffix));
            widthItem->setFlags(widthItem->flags() | Qt::ItemIsEditable);

            auto* heightItem = new QTreeWidgetItem(geomHeader);
            heightItem->setText(0, tr("Height"));
            double h = qAbs(entity->points[1].y() - entity->points[0].y());
            heightItem->setText(1, QStringLiteral("%1 %2").arg(h, 0, 'f', 2).arg(m_unitSuffix));
            heightItem->setFlags(heightItem->flags() | Qt::ItemIsEditable);
        }
        break;

    case SketchEntityType::Circle:
        if (!entity->points.isEmpty()) {
            auto* centerItem = new QTreeWidgetItem(geomHeader);
            centerItem->setText(0, tr("Center"));
            centerItem->setText(1, QStringLiteral("(%1, %2) %3")
                                .arg(entity->points[0].x(), 0, 'f', 2)
                                .arg(entity->points[0].y(), 0, 'f', 2)
                                .arg(m_unitSuffix));
            centerItem->setFlags(centerItem->flags() | Qt::ItemIsEditable);

            auto* radiusItem = new QTreeWidgetItem(geomHeader);
            radiusItem->setText(0, tr("Radius"));
            radiusItem->setText(1, QStringLiteral("%1 %2").arg(entity->radius, 0, 'f', 2).arg(m_unitSuffix));
            radiusItem->setFlags(radiusItem->flags() | Qt::ItemIsEditable);

            auto* diamItem = new QTreeWidgetItem(geomHeader);
            diamItem->setText(0, tr("Diameter"));
            diamItem->setText(1, QStringLiteral("%1 %2").arg(entity->radius * 2, 0, 'f', 2).arg(m_unitSuffix));
            diamItem->setFlags(diamItem->flags() | Qt::ItemIsEditable);
        }
        break;

    case SketchEntityType::Arc:
        if (!entity->points.isEmpty()) {
            auto* centerItem = new QTreeWidgetItem(geomHeader);
            centerItem->setText(0, tr("Center"));
            centerItem->setText(1, QStringLiteral("(%1, %2) %3")
                                .arg(entity->points[0].x(), 0, 'f', 2)
                                .arg(entity->points[0].y(), 0, 'f', 2)
                                .arg(m_unitSuffix));

            auto* radiusItem = new QTreeWidgetItem(geomHeader);
            radiusItem->setText(0, tr("Radius"));
            radiusItem->setText(1, QStringLiteral("%1 %2").arg(entity->radius, 0, 'f', 2).arg(m_unitSuffix));
            radiusItem->setFlags(radiusItem->flags() | Qt::ItemIsEditable);

            auto* startItem = new QTreeWidgetItem(geomHeader);
            startItem->setText(0, tr("Start Angle"));
            startItem->setText(1, QStringLiteral("%1%2").arg(entity->startAngle, 0, 'f', 1).arg(QChar(0x00B0)));
            startItem->setFlags(startItem->flags() | Qt::ItemIsEditable);

            auto* sweepItem = new QTreeWidgetItem(geomHeader);
            sweepItem->setText(0, tr("Sweep Angle"));
            sweepItem->setText(1, QStringLiteral("%1%2").arg(entity->sweepAngle, 0, 'f', 1).arg(QChar(0x00B0)));
            sweepItem->setFlags(sweepItem->flags() | Qt::ItemIsEditable);
        }
        break;

    default:
        break;
    }

    // Constraints
    auto* constraintItem = new QTreeWidgetItem(m_propsTree);
    constraintItem->setText(0, tr("Constrained"));
    constraintItem->setText(1, entity->constrained ? tr("Yes") : tr("No"));

    m_propsTree->expandAll();
}

void SketchModeController::updateEntityCount()
{
    if (!m_propsTree || !m_canvas) return;

    for (int i = 0; i < m_propsTree->topLevelItemCount(); ++i) {
        auto* item = m_propsTree->topLevelItem(i);
        if (item && item->text(0) == tr("Entities")) {
            item->setText(1, QString::number(m_canvas->entities().size()));
            break;
        }
    }
}

}  // namespace hobbycad
