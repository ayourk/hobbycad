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
#include <QTimer>
#include <QTreeWidget>
#include <QtMath>
#include <cmath>

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
        connect(m_toolbar, &SketchToolbar::toolChanged,
                this, &SketchModeController::onToolSelected);
    }
}

void SketchModeController::setTimeline(TimelineWidget* timeline)
{
    m_timeline = timeline;
}

void SketchModeController::setPropertiesTree(QTreeWidget* tree)
{
    // Disconnect from old tree
    if (m_propsTree) {
        disconnect(m_propsTree, nullptr, this, nullptr);
    }

    m_propsTree = tree;

    // Connect to handle checkbox changes
    if (m_propsTree) {
        connect(m_propsTree, &QTreeWidget::itemChanged,
                this, &SketchModeController::onPropertyItemChanged);
    }
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

    // Check for multi-selection
    QSet<int> selectedIds = m_canvas->selectedEntityIds();
    if (selectedIds.size() > 1) {
        showMultiSelectionProperties(selectedIds);
        return;
    }

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
        if (!entity->points.empty()) {
            auto* posItem = new QTreeWidgetItem(geomHeader);
            posItem->setText(0, tr("Position"));
            posItem->setText(1, QStringLiteral("(%1, %2) %3")
                             .arg(entity->points[0].x, 0, 'f', 2)
                             .arg(entity->points[0].y, 0, 'f', 2)
                             .arg(m_unitSuffix));
            posItem->setFlags(posItem->flags() | Qt::ItemIsEditable);
            posItem->setData(0, Qt::UserRole, entityId);
            posItem->setData(0, Qt::UserRole + 1, QStringLiteral("point0"));
        }
        break;

    case SketchEntityType::Line:
        if (entity->points.size() >= 2) {
            auto* p1Item = new QTreeWidgetItem(geomHeader);
            p1Item->setText(0, tr("Start"));
            p1Item->setText(1, QStringLiteral("(%1, %2) %3")
                             .arg(entity->points[0].x, 0, 'f', 2)
                             .arg(entity->points[0].y, 0, 'f', 2)
                             .arg(m_unitSuffix));
            p1Item->setFlags(p1Item->flags() | Qt::ItemIsEditable);
            p1Item->setData(0, Qt::UserRole, entityId);
            p1Item->setData(0, Qt::UserRole + 1, QStringLiteral("point0"));

            auto* p2Item = new QTreeWidgetItem(geomHeader);
            p2Item->setText(0, tr("End"));
            p2Item->setText(1, QStringLiteral("(%1, %2) %3")
                             .arg(entity->points[1].x, 0, 'f', 2)
                             .arg(entity->points[1].y, 0, 'f', 2)
                             .arg(m_unitSuffix));
            p2Item->setFlags(p2Item->flags() | Qt::ItemIsEditable);
            p2Item->setData(0, Qt::UserRole, entityId);
            p2Item->setData(0, Qt::UserRole + 1, QStringLiteral("point1"));

            auto* lenItem = new QTreeWidgetItem(geomHeader);
            lenItem->setText(0, tr("Length"));
            double len = QLineF(QPointF(entity->points[0].x, entity->points[0].y),
                               QPointF(entity->points[1].x, entity->points[1].y)).length();
            lenItem->setText(1, QStringLiteral("%1 %2").arg(len, 0, 'f', 2).arg(m_unitSuffix));
            lenItem->setFlags(lenItem->flags() | Qt::ItemIsEditable);
            lenItem->setData(0, Qt::UserRole, entityId);
            lenItem->setData(0, Qt::UserRole + 1, QStringLiteral("length"));
        }
        break;

    case SketchEntityType::Rectangle:
        if (entity->points.size() >= 2) {
            auto* p1Item = new QTreeWidgetItem(geomHeader);
            p1Item->setText(0, tr("Corner 1"));
            p1Item->setText(1, QStringLiteral("(%1, %2) %3")
                             .arg(entity->points[0].x, 0, 'f', 2)
                             .arg(entity->points[0].y, 0, 'f', 2)
                             .arg(m_unitSuffix));

            auto* p2Item = new QTreeWidgetItem(geomHeader);
            p2Item->setText(0, tr("Corner 2"));
            p2Item->setText(1, QStringLiteral("(%1, %2) %3")
                             .arg(entity->points[1].x, 0, 'f', 2)
                             .arg(entity->points[1].y, 0, 'f', 2)
                             .arg(m_unitSuffix));

            auto* widthItem = new QTreeWidgetItem(geomHeader);
            widthItem->setText(0, tr("Width"));
            double w = qAbs(entity->points[1].x - entity->points[0].x);
            widthItem->setText(1, QStringLiteral("%1 %2").arg(w, 0, 'f', 2).arg(m_unitSuffix));
            widthItem->setFlags(widthItem->flags() | Qt::ItemIsEditable);
            widthItem->setData(0, Qt::UserRole, entityId);
            widthItem->setData(0, Qt::UserRole + 1, QStringLiteral("width"));

            auto* heightItem = new QTreeWidgetItem(geomHeader);
            heightItem->setText(0, tr("Height"));
            double h = qAbs(entity->points[1].y - entity->points[0].y);
            heightItem->setText(1, QStringLiteral("%1 %2").arg(h, 0, 'f', 2).arg(m_unitSuffix));
            heightItem->setFlags(heightItem->flags() | Qt::ItemIsEditable);
            heightItem->setData(0, Qt::UserRole, entityId);
            heightItem->setData(0, Qt::UserRole + 1, QStringLiteral("height"));
        }
        break;

    case SketchEntityType::Circle:
        if (!entity->points.empty()) {
            auto* centerItem = new QTreeWidgetItem(geomHeader);
            centerItem->setText(0, tr("Center"));
            centerItem->setText(1, QStringLiteral("(%1, %2) %3")
                                .arg(entity->points[0].x, 0, 'f', 2)
                                .arg(entity->points[0].y, 0, 'f', 2)
                                .arg(m_unitSuffix));
            centerItem->setFlags(centerItem->flags() | Qt::ItemIsEditable);
            centerItem->setData(0, Qt::UserRole, entityId);
            centerItem->setData(0, Qt::UserRole + 1, QStringLiteral("point0"));

            auto* radiusItem = new QTreeWidgetItem(geomHeader);
            radiusItem->setText(0, tr("Radius"));
            radiusItem->setText(1, QStringLiteral("%1 %2").arg(entity->radius, 0, 'f', 2).arg(m_unitSuffix));
            radiusItem->setFlags(radiusItem->flags() | Qt::ItemIsEditable);
            radiusItem->setData(0, Qt::UserRole, entityId);
            radiusItem->setData(0, Qt::UserRole + 1, QStringLiteral("radius"));

            auto* diamItem = new QTreeWidgetItem(geomHeader);
            diamItem->setText(0, tr("Diameter"));
            diamItem->setText(1, QStringLiteral("%1 %2").arg(entity->radius * 2, 0, 'f', 2).arg(m_unitSuffix));
            diamItem->setFlags(diamItem->flags() | Qt::ItemIsEditable);
            diamItem->setData(0, Qt::UserRole, entityId);
            diamItem->setData(0, Qt::UserRole + 1, QStringLiteral("diameter"));
        }
        break;

    case SketchEntityType::Arc:
        if (!entity->points.empty()) {
            auto* centerItem = new QTreeWidgetItem(geomHeader);
            centerItem->setText(0, tr("Center"));
            centerItem->setText(1, QStringLiteral("(%1, %2) %3")
                                .arg(entity->points[0].x, 0, 'f', 2)
                                .arg(entity->points[0].y, 0, 'f', 2)
                                .arg(m_unitSuffix));

            auto* radiusItem = new QTreeWidgetItem(geomHeader);
            radiusItem->setText(0, tr("Radius"));
            radiusItem->setText(1, QStringLiteral("%1 %2").arg(entity->radius, 0, 'f', 2).arg(m_unitSuffix));
            radiusItem->setFlags(radiusItem->flags() | Qt::ItemIsEditable);
            radiusItem->setData(0, Qt::UserRole, entityId);
            radiusItem->setData(0, Qt::UserRole + 1, QStringLiteral("radius"));

            auto* startItem = new QTreeWidgetItem(geomHeader);
            startItem->setText(0, tr("Start Angle"));
            startItem->setText(1, QStringLiteral("%1%2").arg(entity->startAngle, 0, 'f', 1).arg(QChar(0x00B0)));
            startItem->setFlags(startItem->flags() | Qt::ItemIsEditable);
            startItem->setData(0, Qt::UserRole, entityId);
            startItem->setData(0, Qt::UserRole + 1, QStringLiteral("startAngle"));

            auto* sweepItem = new QTreeWidgetItem(geomHeader);
            sweepItem->setText(0, tr("Sweep Angle"));
            sweepItem->setText(1, QStringLiteral("%1%2").arg(entity->sweepAngle, 0, 'f', 1).arg(QChar(0x00B0)));
            sweepItem->setFlags(sweepItem->flags() | Qt::ItemIsEditable);
            sweepItem->setData(0, Qt::UserRole, entityId);
            sweepItem->setData(0, Qt::UserRole + 1, QStringLiteral("sweepAngle"));
        }
        break;

    case SketchEntityType::Text:
        if (!entity->points.empty()) {
            auto* posItem = new QTreeWidgetItem(geomHeader);
            posItem->setText(0, tr("Position"));
            posItem->setText(1, QStringLiteral("(%1, %2) %3")
                             .arg(entity->points[0].x, 0, 'f', 2)
                             .arg(entity->points[0].y, 0, 'f', 2)
                             .arg(m_unitSuffix));
            posItem->setFlags(posItem->flags() | Qt::ItemIsEditable);
            posItem->setData(0, Qt::UserRole, entityId);
            posItem->setData(0, Qt::UserRole + 1, QStringLiteral("point0"));

            auto* textItem = new QTreeWidgetItem(geomHeader);
            textItem->setText(0, tr("Text"));
            textItem->setText(1, QString::fromStdString(entity->text));
            textItem->setFlags(textItem->flags() | Qt::ItemIsEditable);
            textItem->setData(0, Qt::UserRole, entityId);
            textItem->setData(0, Qt::UserRole + 1, QStringLiteral("text"));

            auto* sizeItem = new QTreeWidgetItem(geomHeader);
            sizeItem->setText(0, tr("Font Size"));
            sizeItem->setText(1, QStringLiteral("%1 %2").arg(entity->fontSize, 0, 'f', 1).arg(m_unitSuffix));
            sizeItem->setFlags(sizeItem->flags() | Qt::ItemIsEditable);
            sizeItem->setData(0, Qt::UserRole, entityId);
            sizeItem->setData(0, Qt::UserRole + 1, QStringLiteral("fontSize"));

            auto* rotItem = new QTreeWidgetItem(geomHeader);
            rotItem->setText(0, tr("Rotation"));
            rotItem->setText(1, QStringLiteral("%1%2").arg(entity->textRotation, 0, 'f', 1).arg(QChar(0x00B0)));
            rotItem->setFlags(rotItem->flags() | Qt::ItemIsEditable);
            rotItem->setData(0, Qt::UserRole, entityId);
            rotItem->setData(0, Qt::UserRole + 1, QStringLiteral("textRotation"));
        }
        break;

    default:
        break;
    }

    // Constraints
    auto* constraintItem = new QTreeWidgetItem(m_propsTree);
    constraintItem->setText(0, tr("Constrained"));
    constraintItem->setText(1, entity->constrained ? tr("Yes") : tr("No"));

    // Construction geometry (with checkbox)
    auto* constructionItem = new QTreeWidgetItem(m_propsTree);
    constructionItem->setText(0, tr("Construction"));
    constructionItem->setCheckState(1, entity->isConstruction ? Qt::Checked : Qt::Unchecked);
    constructionItem->setData(0, Qt::UserRole, entityId);  // Store entity ID for callback
    constructionItem->setData(0, Qt::UserRole + 1, QStringLiteral("construction"));  // Property name

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

void SketchModeController::showMultiSelectionProperties(const QSet<int>& selectedIds)
{
    if (!m_propsTree || !m_canvas) return;

    m_propsTree->clear();

    // Gather information about selected entities
    QVector<const SketchEntity*> selectedEntities;
    for (const auto& e : m_canvas->entities()) {
        if (selectedIds.contains(e.id)) {
            selectedEntities.append(&e);
        }
    }

    if (selectedEntities.isEmpty()) return;

    // Selection count
    auto* countItem = new QTreeWidgetItem(m_propsTree);
    countItem->setText(0, tr("Selected"));
    countItem->setText(1, tr("%1 entities").arg(selectedEntities.size()));

    // Gather type statistics
    QMap<SketchEntityType, int> typeCounts;
    int constructionCount = 0;
    int normalCount = 0;

    for (const SketchEntity* entity : selectedEntities) {
        typeCounts[entity->type]++;
        if (entity->isConstruction) {
            constructionCount++;
        } else {
            normalCount++;
        }
    }

    // Types breakdown
    auto* typesHeader = new QTreeWidgetItem(m_propsTree);
    typesHeader->setText(0, tr("Types"));

    for (auto it = typeCounts.constBegin(); it != typeCounts.constEnd(); ++it) {
        QString typeName;
        switch (it.key()) {
        case SketchEntityType::Point:     typeName = tr("Points"); break;
        case SketchEntityType::Line:      typeName = tr("Lines"); break;
        case SketchEntityType::Rectangle: typeName = tr("Rectangles"); break;
        case SketchEntityType::Circle:    typeName = tr("Circles"); break;
        case SketchEntityType::Arc:       typeName = tr("Arcs"); break;
        case SketchEntityType::Spline:    typeName = tr("Splines"); break;
        case SketchEntityType::Text:      typeName = tr("Text"); break;
        default:                          typeName = tr("Other"); break;
        }
        auto* typeItem = new QTreeWidgetItem(typesHeader);
        typeItem->setText(0, typeName);
        typeItem->setText(1, QString::number(it.value()));
    }

    // Common properties section
    auto* commonHeader = new QTreeWidgetItem(m_propsTree);
    commonHeader->setText(0, tr("Common Properties"));

    // Construction geometry (with checkbox for bulk toggle)
    auto* constructionItem = new QTreeWidgetItem(commonHeader);
    constructionItem->setText(0, tr("Construction"));

    // Determine checkbox state: checked if all construction, unchecked if all normal, partial otherwise
    if (constructionCount == selectedEntities.size()) {
        constructionItem->setCheckState(1, Qt::Checked);
    } else if (normalCount == selectedEntities.size()) {
        constructionItem->setCheckState(1, Qt::Unchecked);
    } else {
        constructionItem->setCheckState(1, Qt::PartiallyChecked);
    }
    constructionItem->setData(0, Qt::UserRole, -1);  // -1 indicates multi-selection
    constructionItem->setData(0, Qt::UserRole + 1, QStringLiteral("construction_multi"));

    // Calculate bounding box for all selected
    QRectF bounds;
    bool first = true;
    for (const SketchEntity* entity : selectedEntities) {
        for (const auto& pt : entity->points) {
            QPointF qpt(pt.x, pt.y);
            if (first) {
                bounds = QRectF(qpt, QSizeF(0, 0));
                first = false;
            } else {
                bounds = bounds.united(QRectF(qpt, QSizeF(0, 0)));
            }
        }
        // Include radius for circles/arcs
        if ((entity->type == SketchEntityType::Circle || entity->type == SketchEntityType::Arc) &&
            !entity->points.empty()) {
            QPointF c(entity->points[0].x, entity->points[0].y);
            bounds = bounds.united(QRectF(c.x() - entity->radius, c.y() - entity->radius,
                                          entity->radius * 2, entity->radius * 2));
        }
    }

    // Bounding box info
    auto* boundsHeader = new QTreeWidgetItem(m_propsTree);
    boundsHeader->setText(0, tr("Bounding Box"));

    auto* minItem = new QTreeWidgetItem(boundsHeader);
    minItem->setText(0, tr("Min"));
    minItem->setText(1, QStringLiteral("(%1, %2) %3")
                     .arg(bounds.left(), 0, 'f', 2)
                     .arg(bounds.bottom(), 0, 'f', 2)
                     .arg(m_unitSuffix));

    auto* maxItem = new QTreeWidgetItem(boundsHeader);
    maxItem->setText(0, tr("Max"));
    maxItem->setText(1, QStringLiteral("(%1, %2) %3")
                     .arg(bounds.right(), 0, 'f', 2)
                     .arg(bounds.top(), 0, 'f', 2)
                     .arg(m_unitSuffix));

    auto* sizeItem = new QTreeWidgetItem(boundsHeader);
    sizeItem->setText(0, tr("Size"));
    sizeItem->setText(1, QStringLiteral("%1 x %2 %3")
                     .arg(bounds.width(), 0, 'f', 2)
                     .arg(bounds.height(), 0, 'f', 2)
                     .arg(m_unitSuffix));

    auto* centerItem = new QTreeWidgetItem(boundsHeader);
    centerItem->setText(0, tr("Center"));
    centerItem->setText(1, QStringLiteral("(%1, %2) %3")
                     .arg(bounds.center().x(), 0, 'f', 2)
                     .arg(bounds.center().y(), 0, 'f', 2)
                     .arg(m_unitSuffix));

    m_propsTree->expandAll();
}

// Parse a scalar value from text like "10.00 mm" or "90.0°"
static double parseScalar(const QString& text)
{
    // Strip everything except digits, dots, minus signs
    QString cleaned;
    for (const QChar& ch : text) {
        if (ch.isDigit() || ch == QLatin1Char('.') || ch == QLatin1Char('-'))
            cleaned += ch;
    }
    bool ok = false;
    double val = cleaned.toDouble(&ok);
    return ok ? val : std::numeric_limits<double>::quiet_NaN();
}

// Parse a point value from text like "(10.00, 20.00) mm"
static bool parsePoint(const QString& text, double& x, double& y)
{
    // Find content between parentheses
    int lp = text.indexOf(QLatin1Char('('));
    int rp = text.indexOf(QLatin1Char(')'));
    if (lp < 0 || rp < 0 || rp <= lp) return false;
    QString inner = text.mid(lp + 1, rp - lp - 1);
    QStringList parts = inner.split(QLatin1Char(','));
    if (parts.size() != 2) return false;
    bool okX = false, okY = false;
    x = parts[0].trimmed().toDouble(&okX);
    y = parts[1].trimmed().toDouble(&okY);
    return okX && okY;
}

void SketchModeController::onPropertyItemChanged(QTreeWidgetItem* item, int column)
{
    if (!item || !m_canvas || column != 1) return;

    // Check if this is a property we handle
    QString propertyName = item->data(0, Qt::UserRole + 1).toString();
    if (propertyName.isEmpty()) return;

    int entityId = item->data(0, Qt::UserRole).toInt();

    if (propertyName == QStringLiteral("construction")) {
        // Single entity construction toggle
        if (entityId <= 0) return;
        bool isConstruction = (item->checkState(1) == Qt::Checked);
        m_canvas->setEntityConstruction(entityId, isConstruction);
    }
    else if (propertyName == QStringLiteral("construction_multi")) {
        // Multi-selection construction toggle
        bool isConstruction = (item->checkState(1) == Qt::Checked);
        QSet<int> selectedIds = m_canvas->selectedEntityIds();
        for (int id : selectedIds) {
            m_canvas->setEntityConstruction(id, isConstruction);
        }
        // Refresh the properties panel
        if (!selectedIds.isEmpty()) {
            showMultiSelectionProperties(selectedIds);
        }
    }
    else {
        // Geometry property edit
        if (entityId <= 0) return;
        SketchEntity* entity = m_canvas->entityById(entityId);
        if (!entity) return;

        // Capture entity state before modification for undo
        const SketchEntity oldEntity = *entity;

        QString text = item->text(1);
        bool changed = false;

        if (propertyName.startsWith(QStringLiteral("point"))) {
            // Point property: "point0", "point1", etc.
            int idx = propertyName.mid(5).toInt();
            if (idx >= 0 && idx < entity->points.size()) {
                double x, y;
                if (parsePoint(text, x, y)) {
                    entity->points[idx] = {x, y};
                    changed = true;
                }
            }
        }
        else if (propertyName == QStringLiteral("radius")) {
            double val = parseScalar(text);
            if (!std::isnan(val) && val > 0) {
                entity->radius = val;
                // Recompute arc/circle perimeter points
                if (entity->type == SketchEntityType::Arc
                        && entity->points.size() >= 3) {
                    QPointF center(entity->points[0]);
                    double startRad = qDegreesToRadians(entity->startAngle);
                    double endRad = qDegreesToRadians(
                        entity->startAngle + entity->sweepAngle);
                    entity->points[1] = {
                        center.x() + val * qCos(startRad),
                        center.y() + val * qSin(startRad)};
                    entity->points[2] = {
                        center.x() + val * qCos(endRad),
                        center.y() + val * qSin(endRad)};
                } else if (entity->type == SketchEntityType::Circle) {
                    QPointF center(entity->points[0]);
                    for (int i = 1; i < entity->points.size(); ++i) {
                        QPointF dir = QPointF(entity->points[i]) - center;
                        double len = std::sqrt(dir.x() * dir.x()
                                             + dir.y() * dir.y());
                        if (len > 1e-6)
                            entity->points[i] = center + dir * (val / len);
                    }
                }
                changed = true;
            }
        }
        else if (propertyName == QStringLiteral("diameter")) {
            double val = parseScalar(text);
            if (!std::isnan(val) && val > 0) {
                double r = val / 2.0;
                entity->radius = r;
                if (entity->type == SketchEntityType::Circle) {
                    QPointF center(entity->points[0]);
                    for (int i = 1; i < entity->points.size(); ++i) {
                        QPointF dir = QPointF(entity->points[i]) - center;
                        double len = std::sqrt(dir.x() * dir.x()
                                             + dir.y() * dir.y());
                        if (len > 1e-6)
                            entity->points[i] = center + dir * (r / len);
                    }
                }
                changed = true;
            }
        }
        else if (propertyName == QStringLiteral("startAngle")) {
            double val = parseScalar(text);
            if (!std::isnan(val)) {
                entity->startAngle = val;
                // Recompute start and end points from angles
                if (entity->points.size() >= 3) {
                    QPointF center(entity->points[0]);
                    double r = entity->radius;
                    double startRad = qDegreesToRadians(val);
                    double endRad = qDegreesToRadians(
                        val + entity->sweepAngle);
                    entity->points[1] = {
                        center.x() + r * qCos(startRad),
                        center.y() + r * qSin(startRad)};
                    entity->points[2] = {
                        center.x() + r * qCos(endRad),
                        center.y() + r * qSin(endRad)};
                }
                changed = true;
            }
        }
        else if (propertyName == QStringLiteral("sweepAngle")) {
            double val = parseScalar(text);
            if (!std::isnan(val)) {
                entity->sweepAngle = val;
                // Recompute end point from angles
                if (entity->points.size() >= 3) {
                    QPointF center(entity->points[0]);
                    double r = entity->radius;
                    double endRad = qDegreesToRadians(
                        entity->startAngle + val);
                    entity->points[2] = {
                        center.x() + r * qCos(endRad),
                        center.y() + r * qSin(endRad)};
                }
                changed = true;
            }
        }
        else if (propertyName == QStringLiteral("length")) {
            double val = parseScalar(text);
            if (!std::isnan(val) && val > 0
                    && entity->points.size() >= 2) {
                // Keep start fixed, move end along same direction
                QPointF p0(entity->points[0]);
                QPointF p1(entity->points[1]);
                QPointF dir = p1 - p0;
                double curLen = std::sqrt(dir.x() * dir.x()
                                        + dir.y() * dir.y());
                if (curLen > 1e-6) {
                    QPointF unit = dir / curLen;
                    entity->points[1] = p0 + unit * val;
                    changed = true;
                }
            }
        }
        else if (propertyName == QStringLiteral("width")) {
            double val = parseScalar(text);
            if (!std::isnan(val) && val > 0
                    && entity->points.size() >= 2) {
                double sign = (entity->points[1].x >= entity->points[0].x)
                    ? 1.0 : -1.0;
                entity->points[1].x = entity->points[0].x + sign * val;
                changed = true;
            }
        }
        else if (propertyName == QStringLiteral("height")) {
            double val = parseScalar(text);
            if (!std::isnan(val) && val > 0
                    && entity->points.size() >= 2) {
                double sign = (entity->points[1].y >= entity->points[0].y)
                    ? 1.0 : -1.0;
                entity->points[1].y = entity->points[0].y + sign * val;
                changed = true;
            }
        }
        else if (propertyName == QStringLiteral("text")) {
            entity->text = text.toStdString();
            changed = true;
        }
        else if (propertyName == QStringLiteral("fontSize")) {
            double val = parseScalar(text);
            if (!std::isnan(val) && val > 0) {
                entity->fontSize = val;
                changed = true;
            }
        }
        else if (propertyName == QStringLiteral("textRotation")) {
            double val = parseScalar(text);
            if (!std::isnan(val)) {
                entity->textRotation = val;
                changed = true;
            }
        }

        if (changed) {
            // Keep text rotation handle in sync after property edits
            SketchCanvas::recomputeTextRotationHandle(*entity);

            // Push undo command for property edit
            m_canvas->pushUndoCommand(sketch::UndoCommand::modifyEntity(
                oldEntity, *entity,
                "Edit " + propertyName.toStdString()));

            m_canvas->notifyEntityChanged(entityId);
            // Defer tree refresh so Qt can close the inline editor
            // before we destroy its item via clear().
            QTimer::singleShot(0, this, [this, entityId]() {
                m_propsTree->blockSignals(true);
                showEntityProperties(entityId);
                m_propsTree->blockSignals(false);
            });
        }
    }
}

}  // namespace hobbycad
