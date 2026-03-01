// =====================================================================
//  src/hobbycad/gui/reduced/reducedmodewindow.cpp — Reduced Mode window
// =====================================================================

#include "reducedmodewindow.h"
#include "reducedviewport.h"
#include "diagnosticdialog.h"
#include "gui/changelogpanel.h"
#include "gui/clipanel.h"
#include "gui/modeltoolbar.h"
#include "gui/toolbarbutton.h"
#include "gui/toolbardropdown.h"
#include "gui/timelinewidget.h"
#include "gui/formulafield.h"
#include "gui/sketchtoolbar.h"
#include "gui/sketchcanvas.h"
#include "gui/sketchactionbar.h"
#include "gui/sketchplanedialog.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtMath>
#include <cmath>

namespace hobbycad {

ReducedModeWindow::ReducedModeWindow(const OpenGLInfo& glInfo,
                                     QWidget* parent)
    : MainWindow(glInfo, parent)
{
    setObjectName(QStringLiteral("ReducedModeWindow"));

    // Create central widget container with toolbar + viewport + timeline
    auto* container = new QWidget(this);
    m_mainLayout = new QVBoxLayout(container);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);

    // Toolbar stack (normal toolbar vs sketch toolbar)
    m_toolbarStack = new QStackedWidget(container);

    m_toolbar = new ModelToolbar(m_toolbarStack);
    m_toolbarStack->addWidget(m_toolbar);

    m_sketchToolbar = new SketchToolbar(m_toolbarStack);
    m_toolbarStack->addWidget(m_sketchToolbar);

    m_mainLayout->addWidget(m_toolbarStack);

    // Connect View > Toolbar toggle
    if (toolbarToggleAction()) {
        connect(toolbarToggleAction(), &QAction::toggled,
                m_toolbarStack, &QWidget::setVisible);
    }

    // Viewport stack (normal splitter vs sketch canvas)
    m_viewportStack = new QStackedWidget(container);

    // Normal mode: vertical splitter with viewport + CLI
    m_splitter = new QSplitter(Qt::Vertical, m_viewportStack);

    m_viewport = new ReducedViewport(m_splitter);
    m_splitter->addWidget(m_viewport);

    m_centralCli = new CliPanel(m_splitter);
    m_splitter->addWidget(m_centralCli);

    // Exit command in the central CLI panel closes the app
    connect(m_centralCli, &CliPanel::exitRequested,
            this, &QWidget::close);

    // Give most space to the CLI panel
    m_splitter->setStretchFactor(0, 1);   // viewport: small
    m_splitter->setStretchFactor(1, 3);   // CLI: large

    m_viewportStack->addWidget(m_splitter);

    // Sketch mode: 2D canvas
    m_sketchCanvas = new SketchCanvas(m_viewportStack);
    m_sketchCanvas->setUnitSuffix(unitSuffix());
    m_viewportStack->addWidget(m_sketchCanvas);

    m_mainLayout->addWidget(m_viewportStack, 1);  // stretch factor 1

    // Connect sketch canvas constraint selection (ReducedMode-specific)
    connect(m_sketchCanvas, &SketchCanvas::constraintSelectionChanged,
            this, &ReducedModeWindow::onConstraintSelectionChanged);

    // Timeline below the viewport stack
    m_timeline = new TimelineWidget(container);
    m_mainLayout->addWidget(m_timeline);
    createTimeline();

    setCentralWidget(container);
    finalizeLayout();

    // Initialize shared sketch signal/slot connections from base class
    initSketchConnections();

    // Initialize default parameters
    initDefaultParameters();

    connect(m_viewport, &ReducedViewport::viewportClicked,
            this, &ReducedModeWindow::onViewportClicked);

    // Hook into the View > Terminal toggle from MainWindow.
    // In Reduced Mode, toggling the terminal shows/hides the
    // central CLI panel (not the dock — we hide the dock entirely
    // since the central panel serves that role).
    if (auto* action = findChild<QAction*>(
            QString(), Qt::FindDirectChildrenOnly)) {
        // We need the specific toggle action — use the one we stored
    }

    // Connect to the toggle action created in MainWindow::createMenus()
    // The action is m_actionToggleTerminal, accessible via the menu.
    // We override the dock behavior: in Reduced Mode, toggle controls
    // the central CLI panel instead.
    connect(terminalToggleAction(), &QAction::toggled,
            this, &ReducedModeWindow::onTerminalToggled);

    // Start with terminal visible and action checked
    terminalToggleAction()->setChecked(true);

    // Hide the dock-based terminal — not needed in Reduced Mode
    // since we have the central one
    hideDockTerminal();

    // Show the diagnostic dialog on first launch
    showDiagnosticDialog();

    // Focus the CLI panel
    m_centralCli->focusInput();
}

void ReducedModeWindow::onTerminalToggled(bool visible)
{
    m_centralCli->setVisible(visible);
    if (visible) {
        m_centralCli->focusInput();
    }
}

void ReducedModeWindow::applyPreferences()
{
    // Call base class to apply standard bindings
    MainWindow::applyPreferences();

    // Reload sketch canvas key bindings
    if (m_sketchCanvas) {
        m_sketchCanvas->reloadBindings();
    }
}

void ReducedModeWindow::onViewportClicked()
{
    showDiagnosticDialog();
}

void ReducedModeWindow::showDiagnosticDialog()
{
    if (m_suppressDialog) {
        QApplication::beep();
        return;
    }

    DiagnosticDialog dlg(m_glInfo, this);
    int result = dlg.exec();

    if (result == 2) {
        QApplication::quit();
        return;
    }

    if (dlg.dontShowAgain()) {
        m_suppressDialog = true;
        m_viewport->setSuppressDialog(true);
    }
}

// createToolbar() is no longer needed - ModelToolbar handles all button setup internally

void ReducedModeWindow::createTimeline()
{
    // Add example timeline items to demonstrate scrolling behavior
    // These will be replaced with actual feature history
    m_timeline->addItem(TimelineFeature::Origin, tr("Origin"));
    m_timeline->addItem(TimelineFeature::Sketch, tr("Sketch1"));
    m_timeline->addItem(TimelineFeature::Extrude, tr("Extrude1"));
    m_timeline->addItem(TimelineFeature::Sketch, tr("Sketch2"));
    m_timeline->addItem(TimelineFeature::Extrude, tr("Extrude2"));
    m_timeline->addItem(TimelineFeature::Fillet, tr("Fillet1"));
    m_timeline->addItem(TimelineFeature::Hole, tr("Hole1"));
    m_timeline->addItem(TimelineFeature::Mirror, tr("Mirror1"));
    m_timeline->addItem(TimelineFeature::Chamfer, tr("Chamfer1"));
    m_timeline->addItem(TimelineFeature::Pattern, tr("Pattern1"));

    // Connect timeline item selection to properties panel
    connect(m_timeline, &TimelineWidget::itemClicked,
            this, &ReducedModeWindow::showFeatureProperties);
}

void ReducedModeWindow::onCreateSketchClicked()
{
    // Show plane selection dialog
    SketchPlaneDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) {
        return;  // User cancelled
    }

    SketchPlane plane = dialog.selectedPlane();
    double offset = dialog.offset();

    // Store offset for display in properties
    m_pendingSketchOffset = offset;

    enterSketchMode(plane);
}

void ReducedModeWindow::enterSketchMode(SketchPlane plane)
{
    MainWindow::enterSketchMode(plane);

    // Add new sketch to timeline
    int sketchCount = 0;
    for (int i = 0; i < m_timeline->itemCount(); ++i) {
        if (m_timeline->featureAt(i) == TimelineFeature::Sketch) {
            ++sketchCount;
        }
    }
    QString sketchName = tr("Sketch%1").arg(sketchCount + 1);
    m_timeline->addItem(TimelineFeature::Sketch, sketchName);

    // Select the new sketch in timeline
    m_timeline->setSelectedIndex(m_timeline->itemCount() - 1);

    // Update properties to show sketch settings
    QTreeWidget* propsTree = propertiesTree();
    if (propsTree) {
        propsTree->clear();

        // Sketch name
        auto* nameItem = new QTreeWidgetItem(propsTree);
        nameItem->setText(0, tr("Name"));
        nameItem->setText(1, sketchName);
        nameItem->setFlags(nameItem->flags() | Qt::ItemIsEditable);

        // Plane selection
        auto* planeItem = new QTreeWidgetItem(propsTree);
        planeItem->setText(0, tr("Plane"));
        QStringList planes = {tr("XY"), tr("XZ"), tr("YZ")};
        int planeIdx = static_cast<int>(plane);
        planeItem->setText(1, planes.value(planeIdx));
        planeItem->setData(1, Qt::UserRole, QStringLiteral("dropdown"));
        planeItem->setData(1, Qt::UserRole + 1, planes);
        planeItem->setData(1, Qt::UserRole + 2, planeIdx);

        // Plane offset
        auto* offsetItem = new QTreeWidgetItem(propsTree);
        offsetItem->setText(0, tr("Offset"));
        offsetItem->setText(1, tr("%1 mm").arg(m_pendingSketchOffset, 0, 'g', 6));

        // Grid settings
        auto* gridHeader = new QTreeWidgetItem(propsTree);
        gridHeader->setText(0, tr("Grid"));

        auto* showGridItem = new QTreeWidgetItem(gridHeader);
        showGridItem->setText(0, tr("Show Grid"));
        showGridItem->setText(1, m_sketchCanvas->isGridVisible() ? tr("Yes") : tr("No"));
        showGridItem->setData(1, Qt::UserRole, QStringLiteral("dropdown"));
        showGridItem->setData(1, Qt::UserRole + 1, QStringList{tr("Yes"), tr("No")});
        showGridItem->setData(1, Qt::UserRole + 2, m_sketchCanvas->isGridVisible() ? 0 : 1);

        auto* snapItem = new QTreeWidgetItem(gridHeader);
        snapItem->setText(0, tr("Snap to Grid"));
        snapItem->setText(1, m_sketchCanvas->snapToGrid() ? tr("Yes") : tr("No"));
        snapItem->setData(1, Qt::UserRole, QStringLiteral("dropdown"));
        snapItem->setData(1, Qt::UserRole + 1, QStringList{tr("Yes"), tr("No")});
        snapItem->setData(1, Qt::UserRole + 2, m_sketchCanvas->snapToGrid() ? 0 : 1);

        auto* spacingItem = new QTreeWidgetItem(gridHeader);
        spacingItem->setText(0, tr("Grid Spacing"));
        spacingItem->setText(1, QStringLiteral("%1 %2")
                             .arg(m_sketchCanvas->gridSpacing())
                             .arg(unitSuffix()));
        spacingItem->setFlags(spacingItem->flags() | Qt::ItemIsEditable);

        // Entities count
        auto* entitiesItem = new QTreeWidgetItem(propsTree);
        entitiesItem->setText(0, tr("Entities"));
        entitiesItem->setText(1, QString::number(m_sketchCanvas->entities().size()));

        propsTree->expandAll();
    }
}

void ReducedModeWindow::exitSketchMode()
{
    MainWindow::exitSketchMode();

    // Switch back to normal viewport (splitter with disabled viewport + CLI)
    m_viewportStack->setCurrentWidget(m_splitter);

    // Focus CLI
    m_centralCli->focusInput();
}

void ReducedModeWindow::saveCurrentSketch()
{
    // Save the sketch entities to the document
    // For now, just mark the sketch as saved in the timeline
    if (m_timeline->itemCount() > 0) {
        int lastIdx = m_timeline->itemCount() - 1;
        if (m_timeline->featureAt(lastIdx) == TimelineFeature::Sketch) {
            // The sketch is already in the timeline - entities are stored
            // TODO: Persist sketch entities to the document
            statusBar()->showMessage(
                tr("Sketch '%1' saved with %2 entities")
                    .arg(m_timeline->nameAt(lastIdx))
                    .arg(m_sketchCanvas->entities().size()),
                3000);
        }
    }
}

void ReducedModeWindow::discardCurrentSketch()
{
    // Discard the sketch - remove from timeline if it was newly created
    if (m_timeline->itemCount() > 0) {
        int lastIdx = m_timeline->itemCount() - 1;
        if (m_timeline->featureAt(lastIdx) == TimelineFeature::Sketch) {
            // Check if sketch has any entities
            if (m_sketchCanvas->entities().isEmpty()) {
                // Empty sketch - remove it from timeline
                m_timeline->removeItem(lastIdx);
                statusBar()->showMessage(tr("Empty sketch discarded"), 3000);
            } else {
                // Has entities but user cancelled - ask what to do
                // For now, just warn and keep the sketch
                statusBar()->showMessage(
                    tr("Sketch changes discarded (%1 entities)")
                        .arg(m_sketchCanvas->entities().size()),
                    3000);
                // TODO: Restore original sketch state if editing existing sketch
            }
        }
    }
}

void ReducedModeWindow::onConstraintSelectionChanged(int constraintId)
{
    if (constraintId < 0) {
        // Constraint deselected — revert to sketch-level properties
        if (m_inSketchMode) {
            enterSketchMode(m_sketchCanvas->sketchPlane());
        }
        return;
    }
    showSketchConstraintProperties(constraintId);
}

}  // namespace hobbycad

