// =====================================================================
//  src/hobbycad/gui/preferencesdialog.cpp — Application preferences
// =====================================================================

#include "preferencesdialog.h"
#include "bindingsdialog.h"

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
    layout->addStretch();

    return page;
}

// ---- Settings persistence -------------------------------------------

void PreferencesDialog::loadSettings()
{
    QSettings s;
    s.beginGroup(QStringLiteral("preferences"));

    // Navigation
    QString preset = s.value(QStringLiteral("mousePreset"),
                             QStringLiteral("hobbycad")).toString();
    int presetIdx = m_mousePreset->findData(preset);
    m_mousePreset->setCurrentIndex(presetIdx >= 0 ? presetIdx : 0);

    int axis = s.value(QStringLiteral("defaultAxis"), 0).toInt();
    m_defaultAxis->setCurrentIndex(qBound(0, axis, 2));

    m_pgUpStepDeg->setValue(s.value(QStringLiteral("pgUpStepDeg"), 10).toInt());
    m_spinInterval->setValue(s.value(QStringLiteral("spinInterval"), 10).toInt());
    m_snapStepDeg->setValue(s.value(QStringLiteral("snapStepDeg"), 10).toInt());
    m_snapInterval->setValue(s.value(QStringLiteral("snapInterval"), 10).toInt());

    // General
    m_showGridOnStart->setChecked(
        s.value(QStringLiteral("showGrid"), true).toBool());
    m_restoreSession->setChecked(
        s.value(QStringLiteral("restoreSession"), true).toBool());
    m_zUpOrientation->setChecked(
        s.value(QStringLiteral("zUpOrientation"), true).toBool());
    m_orbitSelected->setChecked(
        s.value(QStringLiteral("orbitSelected"), false).toBool());

    s.endGroup();
}

void PreferencesDialog::saveSettings()
{
    QSettings s;
    s.beginGroup(QStringLiteral("preferences"));

    s.setValue(QStringLiteral("mousePreset"),
              m_mousePreset->currentData().toString());
    s.setValue(QStringLiteral("defaultAxis"),
              m_defaultAxis->currentIndex());
    s.setValue(QStringLiteral("pgUpStepDeg"), m_pgUpStepDeg->value());
    s.setValue(QStringLiteral("spinInterval"), m_spinInterval->value());
    s.setValue(QStringLiteral("snapStepDeg"), m_snapStepDeg->value());
    s.setValue(QStringLiteral("snapInterval"), m_snapInterval->value());

    s.setValue(QStringLiteral("showGrid"), m_showGridOnStart->isChecked());
    s.setValue(QStringLiteral("restoreSession"),
              m_restoreSession->isChecked());
    s.setValue(QStringLiteral("zUpOrientation"),
              m_zUpOrientation->isChecked());
    s.setValue(QStringLiteral("orbitSelected"),
              m_orbitSelected->isChecked());

    s.endGroup();
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
