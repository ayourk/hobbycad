// =====================================================================
//  src/hobbycad/gui/bindingsdialog.cpp — Keyboard and mouse bindings
// =====================================================================

#include "bindingsdialog.h"
#include "bindingeditrow.h"

#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequence>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSizePolicy>
#include <QSplitter>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

namespace hobbycad {

// Grey color for non-selectable binding items (50% of black)
static const QColor kBindingTextColor(128, 128, 128);

// ---- Default action bindings ----------------------------------------

QHash<QString, ActionBinding> BindingsDialog::defaultBindings()
{
    QHash<QString, ActionBinding> defaults;

    // File menu actions
    defaults.insert("file.new", ActionBinding(
        "file.new", tr("New Document"), tr("File"),
        QKeySequence(QKeySequence::New).toString()));

    defaults.insert("file.open", ActionBinding(
        "file.open", tr("Open..."), tr("File"),
        QKeySequence(QKeySequence::Open).toString()));

    defaults.insert("file.save", ActionBinding(
        "file.save", tr("Save"), tr("File"),
        QKeySequence(QKeySequence::Save).toString()));

    defaults.insert("file.saveAs", ActionBinding(
        "file.saveAs", tr("Save As..."), tr("File"),
        QKeySequence(QKeySequence::SaveAs).toString()));

    defaults.insert("file.close", ActionBinding(
        "file.close", tr("Close"), tr("File"),
        QKeySequence(QKeySequence::Close).toString()));

    defaults.insert("file.quit", ActionBinding(
        "file.quit", tr("Quit"), tr("File"),
        QKeySequence(QKeySequence::Quit).toString()));

    // Edit menu actions
    defaults.insert("edit.cut", ActionBinding(
        "edit.cut", tr("Cut"), tr("Edit"),
        QKeySequence(QKeySequence::Cut).toString()));

    defaults.insert("edit.copy", ActionBinding(
        "edit.copy", tr("Copy"), tr("Edit"),
        QKeySequence(QKeySequence::Copy).toString()));

    defaults.insert("edit.paste", ActionBinding(
        "edit.paste", tr("Paste"), tr("Edit"),
        QKeySequence(QKeySequence::Paste).toString()));

    defaults.insert("edit.delete", ActionBinding(
        "edit.delete", tr("Delete"), tr("Edit"),
        QKeySequence(QKeySequence::Delete).toString()));

    defaults.insert("edit.selectAll", ActionBinding(
        "edit.selectAll", tr("Select All"), tr("Edit"),
        QKeySequence(QKeySequence::SelectAll).toString()));

    // View menu actions
    defaults.insert("view.terminal", ActionBinding(
        "view.terminal", tr("Toggle Terminal"), tr("View"),
        QKeySequence(Qt::CTRL | Qt::Key_QuoteLeft).toString()));

    defaults.insert("view.project", ActionBinding(
        "view.project", tr("Toggle Project"), tr("View"),
        QKeySequence(Qt::CTRL | Qt::Key_R).toString()));

    defaults.insert("view.properties", ActionBinding(
        "view.properties", tr("Toggle Properties"), tr("View"),
        QKeySequence(Qt::CTRL | Qt::Key_P).toString()));

    defaults.insert("view.resetView", ActionBinding(
        "view.resetView", tr("Reset View"), tr("View"),
        QKeySequence(Qt::Key_Home).toString()));

    defaults.insert("view.rotateLeft", ActionBinding(
        "view.rotateLeft", tr("Rotate Left 90\xC2\xB0"), tr("View")));

    defaults.insert("view.rotateRight", ActionBinding(
        "view.rotateRight", tr("Rotate Right 90\xC2\xB0"), tr("View")));

    // Sketch canvas view rotation (2D)
    defaults.insert("sketch.rotateCCW", ActionBinding(
        "sketch.rotateCCW", tr("Rotate Canvas CCW"), tr("Sketch"),
        QKeySequence(Qt::Key_Q).toString()));

    defaults.insert("sketch.rotateCW", ActionBinding(
        "sketch.rotateCW", tr("Rotate Canvas CW"), tr("Sketch"),
        QKeySequence(Qt::Key_E).toString()));

    defaults.insert("sketch.rotateReset", ActionBinding(
        "sketch.rotateReset", tr("Reset Canvas Rotation"), tr("Sketch"),
        QKeySequence(Qt::CTRL | Qt::Key_0).toString()));

    // Sketch tools
    defaults.insert("sketch.select", ActionBinding(
        "sketch.select", tr("Select Tool"), tr("Sketch"),
        QKeySequence(Qt::Key_S).toString()));

    defaults.insert("sketch.line", ActionBinding(
        "sketch.line", tr("Line Tool"), tr("Sketch"),
        QKeySequence(Qt::Key_L).toString()));

    defaults.insert("sketch.rectangle", ActionBinding(
        "sketch.rectangle", tr("Rectangle Tool"), tr("Sketch"),
        QKeySequence(Qt::Key_R).toString()));

    defaults.insert("sketch.circle", ActionBinding(
        "sketch.circle", tr("Circle Tool"), tr("Sketch"),
        QKeySequence(Qt::Key_C).toString()));

    defaults.insert("sketch.arc", ActionBinding(
        "sketch.arc", tr("Arc Tool"), tr("Sketch"),
        QKeySequence(Qt::Key_A).toString()));

    defaults.insert("sketch.point", ActionBinding(
        "sketch.point", tr("Point Tool"), tr("Sketch"),
        QKeySequence(Qt::Key_P).toString()));

    defaults.insert("sketch.dimension", ActionBinding(
        "sketch.dimension", tr("Dimension Tool"), tr("Sketch"),
        QKeySequence(Qt::Key_D).toString()));

    defaults.insert("sketch.construction", ActionBinding(
        "sketch.construction", tr("Toggle Construction Mode"), tr("Sketch"),
        QKeySequence(Qt::Key_X).toString()));

    defaults.insert("sketch.offset", ActionBinding(
        "sketch.offset", tr("Offset"), tr("Sketch"),
        QKeySequence(Qt::Key_O).toString()));

    defaults.insert("sketch.trim", ActionBinding(
        "sketch.trim", tr("Trim"), tr("Sketch"),
        QKeySequence(Qt::Key_T).toString()));

    defaults.insert("sketch.toggleGrid", ActionBinding(
        "sketch.toggleGrid", tr("Toggle Grid"), tr("Sketch"),
        QKeySequence(Qt::Key_G).toString()));

    // Design/3D workspace (reserved for future)
    defaults.insert("design.extrude", ActionBinding(
        "design.extrude", tr("Extrude"), tr("Design"),
        QKeySequence(Qt::Key_E).toString()));

    defaults.insert("design.move", ActionBinding(
        "design.move", tr("Move"), tr("Design"),
        QKeySequence(Qt::Key_M).toString()));

    defaults.insert("design.fillet", ActionBinding(
        "design.fillet", tr("Fillet"), tr("Design"),
        QKeySequence(Qt::Key_F).toString()));

    defaults.insert("design.chamfer", ActionBinding(
        "design.chamfer", tr("Chamfer"), tr("Design")));

    defaults.insert("design.hole", ActionBinding(
        "design.hole", tr("Hole"), tr("Design"),
        QKeySequence(Qt::Key_H).toString()));

    defaults.insert("design.joint", ActionBinding(
        "design.joint", tr("Joint"), tr("Design"),
        QKeySequence(Qt::Key_J).toString()));

    defaults.insert("design.measure", ActionBinding(
        "design.measure", tr("Measure"), tr("Design"),
        QKeySequence(Qt::Key_I).toString()));

    defaults.insert("design.toggleVisibility", ActionBinding(
        "design.toggleVisibility", tr("Toggle Visibility"), tr("Design"),
        QKeySequence(Qt::Key_V).toString()));

    // Global commands
    defaults.insert("global.commandSearch", ActionBinding(
        "global.commandSearch", tr("Command Search"), tr("Global"),
        QKeySequence(Qt::Key_Slash).toString()));

    defaults.insert("view.showGrid", ActionBinding(
        "view.showGrid", tr("Show Grid"), tr("View"),
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_G).toString()));

    defaults.insert("view.snapToGrid", ActionBinding(
        "view.snapToGrid", tr("Snap to Grid"), tr("View"),
        QKeySequence(Qt::CTRL | Qt::Key_G).toString()));

    defaults.insert("view.zUpOrientation", ActionBinding(
        "view.zUpOrientation", tr("Z-Up Orientation"), tr("View")));

    defaults.insert("view.orbitSelected", ActionBinding(
        "view.orbitSelected", tr("Orbit Selected Object"), tr("View")));

    defaults.insert("view.preferences", ActionBinding(
        "view.preferences", tr("Preferences..."), tr("View"),
        QKeySequence(QKeySequence::Preferences).toString()));

    // Navigation — Continuous rotation
    defaults.insert("nav.rotateUp", ActionBinding(
        "nav.rotateUp", tr("Rotate Up (continuous)"), tr("Navigation"),
        QKeySequence(Qt::Key_Up).toString()));

    defaults.insert("nav.rotateDown", ActionBinding(
        "nav.rotateDown", tr("Rotate Down (continuous)"), tr("Navigation"),
        QKeySequence(Qt::Key_Down).toString()));

    // Navigation — Rotation axis
    defaults.insert("nav.axisX", ActionBinding(
        "nav.axisX", tr("Set Rotation Axis to X"), tr("Navigation"),
        QKeySequence(Qt::Key_X).toString()));

    defaults.insert("nav.axisY", ActionBinding(
        "nav.axisY", tr("Set Rotation Axis to Y"), tr("Navigation"),
        QKeySequence(Qt::Key_Y).toString()));

    defaults.insert("nav.axisZ", ActionBinding(
        "nav.axisZ", tr("Set Rotation Axis to Z"), tr("Navigation"),
        QKeySequence(Qt::Key_Z).toString()));

    // Navigation — Snap rotations (grouped together)
    defaults.insert("nav.rotateLeft", ActionBinding(
        "nav.rotateLeft", tr("Snap Rotate Left 90\xC2\xB0"), tr("Navigation"),
        QKeySequence(Qt::Key_Left).toString()));

    defaults.insert("nav.rotateRight", ActionBinding(
        "nav.rotateRight", tr("Snap Rotate Right 90\xC2\xB0"), tr("Navigation"),
        QKeySequence(Qt::Key_Right).toString()));

    // Viewport actions (can have both keyboard and mouse bindings)
    defaults.insert("viewport.rotate", ActionBinding(
        "viewport.rotate", tr("Rotate View"), tr("Viewport"),
        "RightButton+Drag"));

    defaults.insert("viewport.pan", ActionBinding(
        "viewport.pan", tr("Pan View"), tr("Viewport"),
        "MiddleButton+Drag"));

    defaults.insert("viewport.zoom", ActionBinding(
        "viewport.zoom", tr("Zoom View"), tr("Viewport"),
        "Wheel"));

    return defaults;
}

// ---- Load/save bindings ---------------------------------------------

QHash<QString, ActionBinding> BindingsDialog::loadBindings()
{
    QHash<QString, ActionBinding> bindings = defaultBindings();

    QSettings s;
    s.beginGroup(QStringLiteral("bindings"));

    for (auto it = bindings.begin(); it != bindings.end(); ++it) {
        QString key = it.key();
        ActionBinding& ab = it.value();

        // Load custom bindings if present (overrides defaults)
        if (s.contains(key + "/1"))
            ab.binding1 = s.value(key + "/1").toString();
        if (s.contains(key + "/2"))
            ab.binding2 = s.value(key + "/2").toString();
        if (s.contains(key + "/3"))
            ab.binding3 = s.value(key + "/3").toString();
    }

    s.endGroup();
    return bindings;
}

void BindingsDialog::saveBindings(const QHash<QString, ActionBinding>& bindings)
{
    QSettings s;
    s.beginGroup(QStringLiteral("bindings"));

    // Clear previous bindings
    s.remove(QString());

    QHash<QString, ActionBinding> defaults = defaultBindings();

    for (auto it = bindings.constBegin(); it != bindings.constEnd(); ++it) {
        const QString& key = it.key();
        const ActionBinding& ab = it.value();
        const ActionBinding& def = defaults.value(key);

        // Only save if different from defaults
        if (ab.binding1 != def.default1)
            s.setValue(key + "/1", ab.binding1);
        if (ab.binding2 != def.default2)
            s.setValue(key + "/2", ab.binding2);
        if (ab.binding3 != def.default3)
            s.setValue(key + "/3", ab.binding3);
    }

    s.endGroup();
    s.sync();
}

// ---- Dialog construction --------------------------------------------

BindingsDialog::BindingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Bindings"));
    setMinimumSize(750, 550);

    m_bindings = loadBindings();
    m_originalBindings = m_bindings;  // Store original for change detection
    createLayout();
    populateActions();
}

void BindingsDialog::createLayout()
{
    auto* mainLayout = new QVBoxLayout(this);

    // Splitter: action tree on left, binding editors on right
    auto* splitter = new QSplitter(Qt::Horizontal);

    // Left side: action tree
    m_actionTree = new QTreeWidget;
    m_actionTree->setHeaderLabel(tr("Actions / Bindings"));
    m_actionTree->setRootIsDecorated(true);
    m_actionTree->setAlternatingRowColors(true);
    m_actionTree->header()->setStretchLastSection(true);
    m_actionTree->setMinimumWidth(250);

    connect(m_actionTree, &QTreeWidget::itemSelectionChanged,
            this, &BindingsDialog::onSelectionChanged);

    splitter->addWidget(m_actionTree);

    // Right side: binding editors
    auto* editorWidget = new QWidget;
    auto* editorLayout = new QVBoxLayout(editorWidget);

    // Label showing which action is selected
    m_actionLabel = new QLabel(tr("Select an action to edit bindings"));
    m_actionLabel->setWordWrap(true);
    m_actionLabel->setStyleSheet(
        QStringLiteral("QLabel { font-weight: bold; }"));
    editorLayout->addWidget(m_actionLabel);

    // Three binding editors
    auto* bindGroup1 = new QGroupBox(tr("Binding 1"));
    auto* bindLayout1 = new QVBoxLayout(bindGroup1);
    bindLayout1->setContentsMargins(8, 8, 8, 8);
    m_bindingRow1 = new BindingEditRow;
    bindLayout1->addWidget(m_bindingRow1);
    editorLayout->addWidget(bindGroup1);

    auto* bindGroup2 = new QGroupBox(tr("Binding 2"));
    auto* bindLayout2 = new QVBoxLayout(bindGroup2);
    bindLayout2->setContentsMargins(8, 8, 8, 8);
    m_bindingRow2 = new BindingEditRow;
    bindLayout2->addWidget(m_bindingRow2);
    editorLayout->addWidget(bindGroup2);

    auto* bindGroup3 = new QGroupBox(tr("Binding 3"));
    auto* bindLayout3 = new QVBoxLayout(bindGroup3);
    bindLayout3->setContentsMargins(8, 8, 8, 8);
    m_bindingRow3 = new BindingEditRow;
    bindLayout3->addWidget(m_bindingRow3);
    editorLayout->addWidget(bindGroup3);

    connect(m_bindingRow1, &BindingEditRow::bindingChanged,
            this, &BindingsDialog::onBinding1Changed);
    connect(m_bindingRow2, &BindingEditRow::bindingChanged,
            this, &BindingsDialog::onBinding2Changed);
    connect(m_bindingRow3, &BindingEditRow::bindingChanged,
            this, &BindingsDialog::onBinding3Changed);

    // Conflict warning label
    m_conflictLabel = new QLabel;
    m_conflictLabel->setStyleSheet(
        QStringLiteral("QLabel { color: #cc4444; font-weight: bold; }"));
    m_conflictLabel->setWordWrap(true);
    m_conflictLabel->hide();
    editorLayout->addWidget(m_conflictLabel);

    // Restore defaults button
    auto* restoreLayout = new QHBoxLayout;
    m_restoreBtn = new QPushButton(tr("Restore Defaults"));
    connect(m_restoreBtn, &QPushButton::clicked,
            this, &BindingsDialog::onRestoreDefaults);
    restoreLayout->addWidget(m_restoreBtn);
    restoreLayout->addStretch();
    editorLayout->addLayout(restoreLayout);

    editorLayout->addStretch();

    splitter->addWidget(editorWidget);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);

    mainLayout->addWidget(splitter, 1);

    // Button box
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel |
        QDialogButtonBox::Apply);
    connect(buttons, &QDialogButtonBox::accepted,
            this, &BindingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected,
            this, &QDialog::reject);
    m_applyBtn = buttons->button(QDialogButtonBox::Apply);
    connect(m_applyBtn, &QPushButton::clicked, this, &BindingsDialog::apply);
    mainLayout->addWidget(buttons);

    // Initially disable editors (nothing selected)
    m_bindingRow1->setEnabled(false);
    m_bindingRow2->setEnabled(false);
    m_bindingRow3->setEnabled(false);
    m_restoreBtn->setEnabled(false);
    m_applyBtn->setEnabled(false);  // No changes yet
}

void BindingsDialog::populateActions()
{
    m_actionTree->clear();

    // Define display order for actions
    static const QStringList actionOrder = {
        // Global
        "global.commandSearch",
        // File
        "file.new", "file.open", "file.save", "file.saveAs",
        "file.close", "file.quit",
        // Edit
        "edit.cut", "edit.copy", "edit.paste", "edit.delete",
        "edit.selectAll",
        // View
        "view.terminal", "view.project", "view.properties", "view.resetView",
        "view.rotateLeft", "view.rotateRight", "view.preferences",
        // Sketch - Tools
        "sketch.select", "sketch.line", "sketch.rectangle", "sketch.circle",
        "sketch.arc", "sketch.point", "sketch.dimension",
        // Sketch - Modifiers
        "sketch.construction", "sketch.offset", "sketch.trim",
        // Sketch - View
        "sketch.rotateCCW", "sketch.rotateCW", "sketch.rotateReset",
        "sketch.toggleGrid",
        // Design (3D workspace - reserved)
        "design.extrude", "design.move", "design.fillet", "design.chamfer",
        "design.hole", "design.joint", "design.measure", "design.toggleVisibility",
        // Navigation - Continuous rotation
        "nav.rotateUp", "nav.rotateDown",
        // Navigation - Rotation axis
        "nav.axisX", "nav.axisY", "nav.axisZ",
        // Navigation - Snap rotations
        "nav.rotateLeft", "nav.rotateRight",
        // Viewport
        "viewport.rotate", "viewport.pan", "viewport.zoom"
    };

    // Group actions by category
    QHash<QString, QTreeWidgetItem*> categoryItems;

    for (const QString& actionId : actionOrder) {
        if (!m_bindings.contains(actionId)) continue;
        const ActionBinding& ab = m_bindings.value(actionId);

        // Create category item if needed
        if (!categoryItems.contains(ab.category)) {
            auto* catItem = new QTreeWidgetItem(m_actionTree);
            catItem->setText(0, ab.category);
            catItem->setFlags(catItem->flags() & ~Qt::ItemIsSelectable);
            catItem->setExpanded(true);
            categoryItems.insert(ab.category, catItem);
        }

        // Create action item under its category (selectable)
        auto* actionItem = new QTreeWidgetItem(categoryItems[ab.category]);
        actionItem->setText(0, ab.displayName);
        actionItem->setData(0, Qt::UserRole, ab.actionId);

        // Check if any bindings are set
        bool hasAnyBinding = !ab.binding1.isEmpty() ||
                             !ab.binding2.isEmpty() ||
                             !ab.binding3.isEmpty();

        // Create binding child items under the action
        if (!ab.binding1.isEmpty()) {
            auto* bind1Item = new QTreeWidgetItem(actionItem);
            bind1Item->setText(0, ab.binding1);
            bind1Item->setData(0, Qt::UserRole, ab.actionId);
            bind1Item->setFlags(bind1Item->flags() & ~Qt::ItemIsSelectable);
            bind1Item->setForeground(0, kBindingTextColor);
        } else if (!hasAnyBinding) {
            // Show (none) only for binding 1 if all bindings are empty
            auto* bind1Item = new QTreeWidgetItem(actionItem);
            bind1Item->setText(0, tr("(none)"));
            bind1Item->setData(0, Qt::UserRole, ab.actionId);
            bind1Item->setFlags(bind1Item->flags() & ~Qt::ItemIsSelectable);
            bind1Item->setForeground(0, kBindingTextColor);
        }

        if (!ab.binding2.isEmpty()) {
            auto* bind2Item = new QTreeWidgetItem(actionItem);
            bind2Item->setText(0, ab.binding2);
            bind2Item->setData(0, Qt::UserRole, ab.actionId);
            bind2Item->setFlags(bind2Item->flags() & ~Qt::ItemIsSelectable);
            bind2Item->setForeground(0, kBindingTextColor);
        }

        if (!ab.binding3.isEmpty()) {
            auto* bind3Item = new QTreeWidgetItem(actionItem);
            bind3Item->setText(0, ab.binding3);
            bind3Item->setData(0, Qt::UserRole, ab.actionId);
            bind3Item->setFlags(bind3Item->flags() & ~Qt::ItemIsSelectable);
            bind3Item->setForeground(0, kBindingTextColor);
        }

        // Collapse items with no bindings, expand those with bindings
        actionItem->setExpanded(hasAnyBinding);
    }

    // Resize column to fit content
    m_actionTree->resizeColumnToContents(0);
}

void BindingsDialog::onSelectionChanged()
{
    QList<QTreeWidgetItem*> items = m_actionTree->selectedItems();

    if (items.isEmpty()) {
        m_selectedAction.clear();
        m_actionLabel->setText(tr("Select an action to edit bindings"));
        m_bindingRow1->setEnabled(false);
        m_bindingRow2->setEnabled(false);
        m_bindingRow3->setEnabled(false);
        m_restoreBtn->setEnabled(false);
        m_conflictLabel->hide();
        return;
    }

    QTreeWidgetItem* item = items.first();
    QString actionId = item->data(0, Qt::UserRole).toString();

    if (actionId.isEmpty()) {
        // Category item selected (no actionId)
        m_selectedAction.clear();
        m_actionLabel->setText(tr("Select an action to edit bindings"));
        m_bindingRow1->setEnabled(false);
        m_bindingRow2->setEnabled(false);
        m_bindingRow3->setEnabled(false);
        m_restoreBtn->setEnabled(false);
        m_conflictLabel->hide();
        return;
    }

    m_selectedAction = actionId;

    const ActionBinding& ab = m_bindings.value(actionId);
    m_actionLabel->setText(ab.displayName);

    updateBindingEditors();

    m_bindingRow1->setEnabled(true);
    m_bindingRow2->setEnabled(true);
    m_bindingRow3->setEnabled(true);
    m_restoreBtn->setEnabled(true);
}

void BindingsDialog::updateBindingEditors()
{
    if (m_selectedAction.isEmpty()) return;

    const ActionBinding& ab = m_bindings.value(m_selectedAction);

    m_bindingRow1->blockSignals(true);
    m_bindingRow1->setBinding(ab.binding1);
    m_bindingRow1->blockSignals(false);

    m_bindingRow2->blockSignals(true);
    m_bindingRow2->setBinding(ab.binding2);
    m_bindingRow2->blockSignals(false);

    m_bindingRow3->blockSignals(true);
    m_bindingRow3->setBinding(ab.binding3);
    m_bindingRow3->blockSignals(false);

    m_conflictLabel->hide();
}

void BindingsDialog::updateTreeForAction(const QString& actionId)
{
    if (actionId.isEmpty()) return;

    // Find the action item in the tree and rebuild its children
    QTreeWidgetItemIterator it(m_actionTree);
    while (*it) {
        if ((*it)->data(0, Qt::UserRole).toString() == actionId) {
            QTreeWidgetItem* actionItem = *it;

            // Remove existing binding children
            while (actionItem->childCount() > 0) {
                delete actionItem->takeChild(0);
            }

            // Add binding children
            const ActionBinding& ab = m_bindings.value(actionId);

            bool hasAnyBinding = !ab.binding1.isEmpty() ||
                                 !ab.binding2.isEmpty() ||
                                 !ab.binding3.isEmpty();

            if (!ab.binding1.isEmpty()) {
                auto* bind1Item = new QTreeWidgetItem(actionItem);
                bind1Item->setText(0, ab.binding1);
                bind1Item->setData(0, Qt::UserRole, actionId);
                bind1Item->setFlags(bind1Item->flags() & ~Qt::ItemIsSelectable);
                bind1Item->setForeground(0, kBindingTextColor);
            } else if (!hasAnyBinding) {
                // Show (none) only for binding 1 if all bindings are empty
                auto* bind1Item = new QTreeWidgetItem(actionItem);
                bind1Item->setText(0, tr("(none)"));
                bind1Item->setData(0, Qt::UserRole, actionId);
                bind1Item->setFlags(bind1Item->flags() & ~Qt::ItemIsSelectable);
                bind1Item->setForeground(0, kBindingTextColor);
            }

            if (!ab.binding2.isEmpty()) {
                auto* bind2Item = new QTreeWidgetItem(actionItem);
                bind2Item->setText(0, ab.binding2);
                bind2Item->setData(0, Qt::UserRole, actionId);
                bind2Item->setFlags(bind2Item->flags() & ~Qt::ItemIsSelectable);
                bind2Item->setForeground(0, kBindingTextColor);
            }

            if (!ab.binding3.isEmpty()) {
                auto* bind3Item = new QTreeWidgetItem(actionItem);
                bind3Item->setText(0, ab.binding3);
                bind3Item->setData(0, Qt::UserRole, actionId);
                bind3Item->setFlags(bind3Item->flags() & ~Qt::ItemIsSelectable);
                bind3Item->setForeground(0, kBindingTextColor);
            }

            // Keep expanded if it has bindings
            actionItem->setExpanded(hasAnyBinding);

            break;
        }
        ++it;
    }
}

QString BindingsDialog::getActionContext(const QString& actionId)
{
    // Extract context from action ID prefix (e.g., "sketch.line" -> "sketch")
    int dotIndex = actionId.indexOf(QLatin1Char('.'));
    if (dotIndex > 0) {
        return actionId.left(dotIndex);
    }
    return QString();
}

QString BindingsDialog::checkConflict(const QString& actionId,
                                       const QString& binding) const
{
    if (binding.isEmpty()) return QString();

    QString myContext = getActionContext(actionId);

    for (auto it = m_bindings.constBegin();
         it != m_bindings.constEnd(); ++it) {
        if (it.key() == actionId) continue;  // Skip self

        const ActionBinding& ab = it.value();

        // Check if bindings match
        bool hasConflict = (ab.binding1 == binding ||
                            ab.binding2 == binding ||
                            ab.binding3 == binding);

        if (!hasConflict) continue;

        // Now check if contexts conflict
        QString otherContext = getActionContext(it.key());

        // Global context conflicts with everything
        if (myContext == QStringLiteral("global") ||
            otherContext == QStringLiteral("global")) {
            return ab.actionId;
        }

        // Same context conflicts (e.g., sketch vs sketch)
        if (myContext == otherContext) {
            return ab.actionId;
        }

        // File, Edit, View, Navigation, Viewport are always active - they conflict
        // with each other and with mode-specific contexts
        static const QStringList alwaysActiveContexts = {
            QStringLiteral("file"),
            QStringLiteral("edit"),
            QStringLiteral("view"),
            QStringLiteral("nav"),
            QStringLiteral("viewport")
        };

        bool myContextAlwaysActive = alwaysActiveContexts.contains(myContext);
        bool otherContextAlwaysActive = alwaysActiveContexts.contains(otherContext);

        // If either is always-active, they conflict
        if (myContextAlwaysActive || otherContextAlwaysActive) {
            return ab.actionId;
        }

        // Different mode-specific contexts don't conflict
        // (e.g., sketch vs design are mutually exclusive)
    }

    return QString();
}

bool BindingsDialog::confirmConflict(const QString& conflictingActionId,
                                      const QString& binding)
{
    const ActionBinding& conflicting = m_bindings.value(conflictingActionId);

    // Determine why there's a conflict for a better message
    QString myContext = getActionContext(m_selectedAction);
    QString otherContext = getActionContext(conflictingActionId);

    QString contextInfo;
    if (myContext == otherContext) {
        contextInfo = tr("Both actions are in the %1 context.")
                      .arg(conflicting.category);
    } else if (myContext == QStringLiteral("global") ||
               otherContext == QStringLiteral("global")) {
        contextInfo = tr("Global bindings are active in all contexts.");
    } else {
        contextInfo = tr("The %1 context is always active.")
                      .arg(conflicting.category);
    }

    QMessageBox msgBox(this);
    msgBox.setWindowTitle(tr("Binding Conflict"));
    msgBox.setIcon(QMessageBox::Warning);
    msgBox.setText(tr("The binding \"%1\" is already assigned to \"%2\".")
                   .arg(binding, conflicting.displayName));
    msgBox.setInformativeText(
        tr("%1\n\nDo you want to remove it from \"%2\" and assign it here?")
        .arg(contextInfo, conflicting.displayName));
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    msgBox.setDefaultButton(QMessageBox::No);

    if (msgBox.exec() == QMessageBox::Yes) {
        // Remove the conflicting binding
        ActionBinding& ab = m_bindings[conflictingActionId];
        if (ab.binding1 == binding) ab.binding1.clear();
        if (ab.binding2 == binding) ab.binding2.clear();
        if (ab.binding3 == binding) ab.binding3.clear();

        // Refresh tree display
        populateActions();
        return true;
    }

    return false;
}

void BindingsDialog::handleBindingChange(int slot, const QString& binding)
{
    if (m_selectedAction.isEmpty()) return;

    QString conflict = checkConflict(m_selectedAction, binding);

    if (!conflict.isEmpty()) {
        if (!confirmConflict(conflict, binding)) {
            updateBindingEditors();
            return;
        }
    }

    // Update the binding in our data
    ActionBinding& ab = m_bindings[m_selectedAction];
    switch (slot) {
        case 1: ab.binding1 = binding; break;
        case 2: ab.binding2 = binding; break;
        case 3: ab.binding3 = binding; break;
    }

    // Update the tree and apply button
    updateTreeForAction(m_selectedAction);
    updateApplyButton();
}

void BindingsDialog::onBinding1Changed(const QString& binding)
{
    handleBindingChange(1, binding);
}

void BindingsDialog::onBinding2Changed(const QString& binding)
{
    handleBindingChange(2, binding);
}

void BindingsDialog::onBinding3Changed(const QString& binding)
{
    handleBindingChange(3, binding);
}

void BindingsDialog::onRestoreDefaults()
{
    if (m_selectedAction.isEmpty()) return;

    QHash<QString, ActionBinding> defaults = defaultBindings();
    if (!defaults.contains(m_selectedAction)) return;

    const ActionBinding& def = defaults.value(m_selectedAction);
    ActionBinding& ab = m_bindings[m_selectedAction];

    // Restore all binding slots
    ab.binding1 = def.default1;
    ab.binding2 = def.default2;
    ab.binding3 = def.default3;

    updateBindingEditors();
    updateTreeForAction(m_selectedAction);
    updateApplyButton();
}

void BindingsDialog::apply()
{
    saveBindings(m_bindings);
    m_originalBindings = m_bindings;  // Update baseline after saving
    updateApplyButton();
    emit bindingsChanged();
}

void BindingsDialog::accept()
{
    apply();
    QDialog::accept();
}

bool BindingsDialog::hasChanges() const
{
    for (auto it = m_bindings.constBegin(); it != m_bindings.constEnd(); ++it) {
        const QString& key = it.key();
        const ActionBinding& current = it.value();

        if (!m_originalBindings.contains(key)) return true;

        const ActionBinding& original = m_originalBindings.value(key);
        if (current.binding1 != original.binding1 ||
            current.binding2 != original.binding2 ||
            current.binding3 != original.binding3) {
            return true;
        }
    }
    return false;
}

void BindingsDialog::updateApplyButton()
{
    m_applyBtn->setEnabled(hasChanges());
}

}  // namespace hobbycad
