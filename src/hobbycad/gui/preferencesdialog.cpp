// =====================================================================
//  src/hobbycad/gui/preferencesdialog.cpp — Application preferences
// =====================================================================

#include "preferencesdialog.h"
#include "settingvalue.h"
#include "arrangementstore.h"
#include "bindingsdialog.h"
#include "customizedialog.h"

#include <QMessageBox>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace hobbycad {

PreferencesDialog::PreferencesDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Preferences"));
    setMinimumSize(560, 400);

    // --- Main layout: page list | page content -----------------------
    auto* mainLayout = new QHBoxLayout;

    m_pageList = new QListWidget;
    m_pageList->setFixedWidth(140);
    m_pageList->setObjectName(QStringLiteral("PreferencesPageList"));
    mainLayout->addWidget(m_pageList);

    auto* rightSide = new QVBoxLayout;

    m_pageStack = new QStackedWidget;
    m_pageStack->setObjectName(QStringLiteral("PreferencesPageStack"));
    rightSide->addWidget(m_pageStack, 1);

    // --- Button box --------------------------------------------------
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel |
        QDialogButtonBox::Apply);
    connect(buttons, &QDialogButtonBox::accepted, this, &PreferencesDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Apply),
            &QPushButton::clicked, this, &PreferencesDialog::apply);
    rightSide->addWidget(buttons);

    mainLayout->addLayout(rightSide, 1);
    setLayout(mainLayout);

    createPages();
    loadSettings();

    // Page switching
    connect(m_pageList, &QListWidget::currentRowChanged,
            m_pageStack, &QStackedWidget::setCurrentIndex);
    m_pageList->setCurrentRow(0);
}

// ---- Pages ----------------------------------------------------------

void PreferencesDialog::createPages()
{
    // Navigation page
    m_pageList->addItem(tr("Navigation"));
    m_pageStack->addWidget(createNavigationPage());

    // Bindings page
    m_pageList->addItem(tr("Bindings"));
    m_pageStack->addWidget(createBindingsPage());

    // General page
    m_pageList->addItem(tr("General"));
    m_pageStack->addWidget(createGeneralPage());
}

QWidget* PreferencesDialog::createNavigationPage()
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);

    // Mouse preset group
    auto* mouseGroup = new QGroupBox(tr("Mouse Bindings"));
    auto* mouseForm = new QFormLayout(mouseGroup);

    m_mousePreset = new QComboBox;
    m_mousePreset->addItem(tr("HobbyCAD (RMB rotate, MMB pan)"),
                           QStringLiteral("hobbycad"));
    m_mousePreset->addItem(tr("Fusion 360 (MMB pan, Shift+MMB rotate)"),
                           QStringLiteral("fusion360"));
    m_mousePreset->addItem(tr("FreeCAD (MMB rotate, Shift+MMB pan)"),
                           QStringLiteral("freecad"));
    m_mousePreset->addItem(tr("Blender (MMB rotate, Shift+MMB pan)"),
                           QStringLiteral("blender"));
    mouseForm->addRow(tr("Preset:"), m_mousePreset);

    layout->addWidget(mouseGroup);

    // Rotation group
    auto* rotGroup = new QGroupBox(tr("Rotation"));
    auto* rotForm = new QFormLayout(rotGroup);

    m_defaultAxis = new QComboBox;
    m_defaultAxis->addItem(tr("X"), 0);
    m_defaultAxis->addItem(tr("Y"), 1);
    m_defaultAxis->addItem(tr("Z"), 2);
    rotForm->addRow(tr("Default axis:"), m_defaultAxis);

    m_pgUpStepDeg = new QSpinBox;
    m_pgUpStepDeg->setRange(1, 45);
    m_pgUpStepDeg->setSuffix(tr("°"));
    m_pgUpStepDeg->setToolTip(tr("Degrees per step for PgUp/PgDn rotation"));
    rotForm->addRow(tr("PgUp/PgDn step:"), m_pgUpStepDeg);

    m_spinInterval = new QSpinBox;
    m_spinInterval->setRange(1, 1000);
    m_spinInterval->setSuffix(tr(" ms"));
    m_spinInterval->setSingleStep(10);
    m_spinInterval->setToolTip(tr("Interval between PgUp/PgDn steps"));
    rotForm->addRow(tr("PgUp/PgDn interval:"), m_spinInterval);

    layout->addWidget(rotGroup);

    // Arrow key animation group
    auto* animGroup = new QGroupBox(tr("Arrow Key Animation"));
    auto* animForm = new QFormLayout(animGroup);

    m_snapStepDeg = new QSpinBox;
    m_snapStepDeg->setRange(1, 15);
    m_snapStepDeg->setSuffix(tr("°"));
    m_snapStepDeg->setToolTip(tr("Degrees per frame for Left/Right arrow snap"));
    animForm->addRow(tr("Step size:"), m_snapStepDeg);

    m_snapInterval = new QSpinBox;
    m_snapInterval->setRange(1, 100);
    m_snapInterval->setSuffix(tr(" ms"));
    m_snapInterval->setSingleStep(5);
    m_snapInterval->setToolTip(tr("Interval between animation frames"));
    animForm->addRow(tr("Frame interval:"), m_snapInterval);

    // Preview label showing total animation time
    auto* previewLabel = new QLabel;
    previewLabel->setObjectName(QStringLiteral("SnapPreviewLabel"));
    animForm->addRow(tr("90° duration:"), previewLabel);

    // Update preview when values change
    auto updatePreview = [=]() {
        int step = m_snapStepDeg->value();
        int interval = m_snapInterval->value();
        int frames = 90 / step;
        double totalMs = frames * interval;
        previewLabel->setText(tr("%1 ms (%2 frames)")
            .arg(totalMs, 0, 'f', 0).arg(frames));
    };
    connect(m_snapStepDeg, qOverload<int>(&QSpinBox::valueChanged),
            this, updatePreview);
    connect(m_snapInterval, qOverload<int>(&QSpinBox::valueChanged),
            this, updatePreview);

    layout->addWidget(animGroup);
    layout->addStretch();

    return page;
}

QWidget* PreferencesDialog::createBindingsPage()
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);

    auto* descLabel = new QLabel(
        tr("Customize keyboard shortcuts and mouse bindings for all actions. "
           "Each action can have up to three bindings."));
    descLabel->setWordWrap(true);
    layout->addWidget(descLabel);

    layout->addSpacing(20);

    auto* openBtn = new QPushButton(tr("Open Bindings Editor..."));
    openBtn->setMinimumHeight(40);
    connect(openBtn, &QPushButton::clicked,
            this, &PreferencesDialog::openBindingsDialog);
    layout->addWidget(openBtn);

    layout->addSpacing(20);

    // The keys and the layout are one arrangement, kept in one file, so
    // the way back to HobbyCAD's own is here beside the editors.
    auto* layoutLabel = new QLabel(
        tr("Menus and toolbars are rearranged in the Customize dialog: what a menu shows, "
           "where a tool sits, what it is called and whether it is shown at all."));
    layoutLabel->setWordWrap(true);
    layout->addWidget(layoutLabel);

    auto* customizeBtn = new QPushButton(tr("Open Customize Dialog..."));
    customizeBtn->setMinimumHeight(40);
    connect(customizeBtn, &QPushButton::clicked,
            this, &PreferencesDialog::openCustomizeDialog);
    layout->addWidget(customizeBtn);

    layout->addSpacing(20);

    auto* restoreBtn = new QPushButton(tr("Restore All Defaults..."));
    connect(restoreBtn, &QPushButton::clicked,
            this, &PreferencesDialog::restoreArrangement);
    layout->addWidget(restoreBtn);

    layout->addStretch();

    return page;
}

void PreferencesDialog::openBindingsDialog()
{
    BindingsDialog dlg(this);
    connect(&dlg, &BindingsDialog::bindingsChanged,
            this, &PreferencesDialog::bindingsChanged);
    dlg.exec();
}

void PreferencesDialog::openCustomizeDialog()
{
    CustomizeDialog dlg(this);
    dlg.exec();
}

void PreferencesDialog::restoreArrangement()
{
    if (QMessageBox::question(this, tr("Restore All Defaults"),
                              tr("Give HobbyCAD's own menus, toolbars and keys back? Every "
                                 "change you have made to them is dropped."))
        != QMessageBox::Yes) {
        return;
    }
    ArrangementStore::instance().restore(layout::RestoreArea::All);
    emit bindingsChanged();
}

QWidget* PreferencesDialog::createGeneralPage()
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);

    auto* startupGroup = new QGroupBox(tr("Startup"));
    auto* startupForm = new QFormLayout(startupGroup);

    m_showGridOnStart = new QCheckBox(tr("Show grid on startup"));
    startupForm->addRow(m_showGridOnStart);

    m_restoreSession = new QCheckBox(tr("Restore window layout on startup"));
    startupForm->addRow(m_restoreSession);

    layout->addWidget(startupGroup);

    // Coordinate system group
    auto* coordGroup = new QGroupBox(tr("Coordinate System"));
    auto* coordForm = new QFormLayout(coordGroup);

    m_zUpOrientation = new QCheckBox(tr("Z-Up orientation (CAD convention)"));
    m_zUpOrientation->setToolTip(
        tr("When checked, Z axis points up (CAD/engineering).\n"
           "When unchecked, Y axis points up (game engine/3D graphics)."));
    coordForm->addRow(m_zUpOrientation);

    layout->addWidget(coordGroup);

    // Orbit behavior group
    auto* orbitGroup = new QGroupBox(tr("Orbit Behavior"));
    auto* orbitForm = new QFormLayout(orbitGroup);

    m_orbitSelected = new QCheckBox(tr("Orbit around selected object"));
    m_orbitSelected->setToolTip(
        tr("When checked, ViewCube rotations orbit around the\n"
           "center of selected objects instead of the pan position."));
    orbitForm->addRow(m_orbitSelected);

    layout->addWidget(orbitGroup);

    // Terminal group
    auto* termGroup = new QGroupBox(tr("Terminal"));
    auto* termForm = new QFormLayout(termGroup);

    m_cliScrollback = new QSpinBox;
    m_cliScrollback->setRange(0, 1000000);
    m_cliScrollback->setSingleStep(1000);
    m_cliScrollback->setSpecialValueText(tr("Unlimited"));
    m_cliScrollback->setSuffix(tr(" lines"));
    m_cliScrollback->setToolTip(
        tr("Lines of output the CLI panel keeps.\n"
           "Unlimited grows without bound; it is a deliberate choice,\n"
           "not the default."));
    termForm->addRow(tr("Scrollback:"), m_cliScrollback);

    layout->addWidget(termGroup);

    // Sketch group
    auto* sketchGroup = new QGroupBox(tr("Sketch"));
    auto* sketchForm = new QFormLayout(sketchGroup);

    m_showCursorHints = new QCheckBox(tr("Show cursor hints"));
    m_showCursorHints->setToolTip(
        tr("Show the short hint that trails the cursor while a\n"
           "drawing tool is active (e.g. \"Click to place center\")."));
    sketchForm->addRow(m_showCursorHints);

    layout->addWidget(sketchGroup);
    layout->addStretch();

    return page;
}

// ---- Settings persistence -------------------------------------------

void PreferencesDialog::loadSettings()
{
    // Defaults and ranges: hobbycad/settings_schema.h.
    namespace keys = settings::keys;

    // Navigation
    const int presetIdx = m_mousePreset->findData(settingChoice(keys::MousePreset));
    m_mousePreset->setCurrentIndex(presetIdx >= 0 ? presetIdx : 0);

    m_defaultAxis->setCurrentIndex(settingInt(keys::DefaultAxis));

    m_pgUpStepDeg->setValue(settingInt(keys::PageStepDeg));
    m_spinInterval->setValue(settingInt(keys::SpinInterval));
    m_snapStepDeg->setValue(settingInt(keys::SnapStepDeg));
    m_snapInterval->setValue(settingInt(keys::SnapInterval));

    // General
    m_showGridOnStart->setChecked(settingBool(keys::ShowGrid));
    m_restoreSession->setChecked(settingBool(keys::RestoreSession));
    m_cliScrollback->setValue(settingInt(keys::CliScrollback));
    m_zUpOrientation->setChecked(settingBool(keys::ZUpOrientation));
    m_orbitSelected->setChecked(settingBool(keys::OrbitSelected));
    m_showCursorHints->setChecked(settingBool(keys::ShowCursorHints));
}

void PreferencesDialog::saveSettings()
{
    namespace keys = settings::keys;
    QSettings s;
    const auto set = [&s](const char* key, const QVariant& value) {
        s.setValue(QLatin1String(key), value);
    };

    set(keys::MousePreset, m_mousePreset->currentData().toString());
    set(keys::DefaultAxis, m_defaultAxis->currentIndex());
    set(keys::PageStepDeg, m_pgUpStepDeg->value());
    set(keys::SpinInterval, m_spinInterval->value());
    set(keys::SnapStepDeg, m_snapStepDeg->value());
    set(keys::SnapInterval, m_snapInterval->value());

    set(keys::ShowGrid, m_showGridOnStart->isChecked());
    set(keys::RestoreSession, m_restoreSession->isChecked());
    set(keys::CliScrollback, m_cliScrollback->value());
    set(keys::ZUpOrientation, m_zUpOrientation->isChecked());
    set(keys::OrbitSelected, m_orbitSelected->isChecked());
    set(keys::ShowCursorHints, m_showCursorHints->isChecked());

    s.sync();
}

void PreferencesDialog::apply()
{
    saveSettings();
}

void PreferencesDialog::accept()
{
    saveSettings();
    QDialog::accept();
}

}  // namespace hobbycad
