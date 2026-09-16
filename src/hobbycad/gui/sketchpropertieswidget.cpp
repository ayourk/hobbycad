// =====================================================================
//  src/hobbycad/gui/sketchpropertieswidget.cpp — Sketch properties widget
// =====================================================================
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include "sketchpropertieswidget.h"
#include <hobbycad/sketch/undo.h>
#include <hobbycad/units.h>
#include <hobbycad/geometry/utils.h>
#include <hobbycad/project.h>
#include <QLineF>
#include <QTimer>
#include <QShortcut>
#include <QDockWidget>
#include <QComboBox>
#include "backgroundimagedialog.h"
#include "sketchcanvas.h"

#include <cmath>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSlider>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QGroupBox>
#include <QStackedWidget>
#include <QScrollArea>
#include <QFileInfo>
#include <QMessageBox>
#include <QSettings>

namespace hobbycad {

SketchPropertiesWidget::SketchPropertiesWidget(QWidget* parent)
    : QWidget(parent)
{
    setupUi();
}

// Make a QGroupBox collapsible: its title checkbox toggles the body. Qt only
// DISABLES children on uncheck, so we also hide them to reclaim the space.
// (The Transform section manages its own checkable state; it is not wrapped.)
static void makeGroupCollapsible(QGroupBox* box, bool defaultExpanded = true)
{
    if (!box) return;
    // Capture the plain title BEFORE decorating it: it is both the persistence
    // key and the base for the disclosure triangle. [properties backlog #2]
    const QString base = box->objectName().isEmpty() ? box->title()
                                                     : box->objectName();
    const QString key = QStringLiteral("SketchProperties/collapse/") + base;
    box->setCheckable(true);
    // Fusion-style arrow-disclosure header: hide the checkbox indicator and show
    // a triangle in the title instead; the whole title row stays clickable.
    box->setStyleSheet(box->styleSheet() +
        QStringLiteral("QGroupBox::indicator { width: 0px; height: 0px; }"));
    auto setArrow = [box, base](bool on) {
        box->setTitle((on ? QStringLiteral("▾  ")   // down triangle = expanded
                          : QStringLiteral("▸  "))   // right triangle = collapsed
                      + base);
    };
    QSettings settings;
    const bool expanded = settings.value(key, defaultExpanded).toBool();
    box->setChecked(expanded);
    setArrow(expanded);
    // Apply the restored visibility immediately (toggled only fires on change).
    const auto kids0 = box->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
    for (QWidget* w : kids0) w->setVisible(expanded);
    QObject::connect(box, &QGroupBox::toggled, box, [box, key, setArrow](bool on) {
        const auto kids = box->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
        for (QWidget* w : kids) w->setVisible(on);
        setArrow(on);
        QSettings().setValue(key, on);
    });
}

void SketchPropertiesWidget::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(8);

    // No internal scroll area: the host Properties panel provides a single
    // vertical scroll for the whole surface (tree + these sections). A nested
    // scroll area here would scroll only this widget, not the panel.
    setupBackgroundSection();
    mainLayout->addWidget(m_backgroundGroup);

    setupEntitySection();
    mainLayout->addWidget(m_entityGroup);

    setupBezierAnchorSection();
    mainLayout->addWidget(m_bezierAnchorGroup);
    mainLayout->addWidget(m_bezierLegGroup);

    setupTransformSection();
    mainLayout->addWidget(m_transformGroup);

    makeGroupCollapsible(m_backgroundGroup);
    makeGroupCollapsible(m_entityGroup, /*defaultExpanded=*/false);
    makeGroupCollapsible(m_bezierAnchorGroup);
    makeGroupCollapsible(m_bezierLegGroup);
}

void SketchPropertiesWidget::setupBackgroundSection()
{
    m_backgroundGroup = new QGroupBox(tr("Background Image"));
    auto* layout = new QVBoxLayout(m_backgroundGroup);

    // File path display
    m_bgFilePathLabel = new QLabel(tr("File:"));
    layout->addWidget(m_bgFilePathLabel);

    auto* filePathLayout = new QHBoxLayout;
    m_bgFilePathEdit = new QLineEdit;
    m_bgFilePathEdit->setReadOnly(true);
    m_bgFilePathEdit->setPlaceholderText(tr("No image selected"));
    filePathLayout->addWidget(m_bgFilePathEdit, 1);

    m_bgBrowseButton = new QPushButton(tr("..."));
    m_bgBrowseButton->setFixedWidth(30);
    m_bgBrowseButton->setToolTip(tr("Browse for a different image file"));
    connect(m_bgBrowseButton, &QPushButton::clicked, this, &SketchPropertiesWidget::onBrowseForImage);
    filePathLayout->addWidget(m_bgBrowseButton);
    layout->addLayout(filePathLayout);

    // Action buttons. Stacked rather than paired: two translated labels
    // side by side set a minimum width the dock cannot honor, which is
    // what forced the horizontal scrollbar.

    m_bgEditPositionButton = new QPushButton(tr("Edit Position"));
    m_bgEditPositionButton->setCheckable(true);
    m_bgEditPositionButton->setToolTip(tr("Enable interactive repositioning and resizing of the background image"));
    connect(m_bgEditPositionButton, &QPushButton::toggled, this, &SketchPropertiesWidget::onEditPositionToggled);
    layout->addWidget(m_bgEditPositionButton);

    m_bgRemoveButton = new QPushButton(tr("Remove"));
    m_bgRemoveButton->setToolTip(tr("Remove the background image"));
    connect(m_bgRemoveButton, &QPushButton::clicked, this, &SketchPropertiesWidget::removeBackgroundImageRequested);
    layout->addWidget(m_bgRemoveButton);


    m_bgCalibrateButton = new QPushButton(tr("Calibrate Scale"));
    m_bgCalibrateButton->setToolTip(tr("Set the scale by picking two points with a known distance"));
    connect(m_bgCalibrateButton, &QPushButton::clicked, this, &SketchPropertiesWidget::calibrateBackgroundRequested);
    layout->addWidget(m_bgCalibrateButton);

    m_bgExportButton = new QPushButton(tr("Export to Project"));
    m_bgExportButton->setToolTip(tr("Save the image file to the project directory"));
    connect(m_bgExportButton, &QPushButton::clicked, this, &SketchPropertiesWidget::onExportToProject);
    layout->addWidget(m_bgExportButton);

    // Opacity
    auto* opacityLayout = new QHBoxLayout;
    opacityLayout->addWidget(new QLabel(tr("Opacity:")));

    m_bgOpacitySlider = new QSlider(Qt::Horizontal);
    m_bgOpacitySlider->setRange(0, 100);
    m_bgOpacitySlider->setValue(50);
    opacityLayout->addWidget(m_bgOpacitySlider, 1);

    m_bgOpacitySpinBox = new QSpinBox;
    m_bgOpacitySpinBox->setRange(0, 100);
    m_bgOpacitySpinBox->setValue(50);
    m_bgOpacitySpinBox->setSuffix(tr("%"));
    m_bgOpacitySpinBox->setFixedWidth(60);
    opacityLayout->addWidget(m_bgOpacitySpinBox);

    connect(m_bgOpacitySlider, &QSlider::valueChanged, this, [this](int value) {
        m_bgOpacitySpinBox->blockSignals(true);
        m_bgOpacitySpinBox->setValue(value);
        m_bgOpacitySpinBox->blockSignals(false);
        onOpacityChanged(value);
    });
    connect(m_bgOpacitySpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        m_bgOpacitySlider->blockSignals(true);
        m_bgOpacitySlider->setValue(value);
        m_bgOpacitySlider->blockSignals(false);
        onOpacityChanged(value);
    });

    layout->addLayout(opacityLayout);

    // Position and size
    auto* positionForm = new QFormLayout;
    positionForm->setSpacing(4);
    // Let a row put its label above the field when the dock is narrow,
    // rather than forcing the panel wider than the dock and producing a
    // horizontal scrollbar. Matters more now that labels are translated.
    positionForm->setRowWrapPolicy(QFormLayout::WrapLongRows);

    m_bgPositionX = new QDoubleSpinBox;
    m_bgPositionX->setMinimumWidth(72);
    m_bgPositionX->setRange(-10000, 10000);
    m_bgPositionX->setDecimals(2);
    m_bgPositionX->setSuffix(tr(" mm"));
    connect(m_bgPositionX, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &SketchPropertiesWidget::onPositionXChanged);
    positionForm->addRow(tr("Position X:"), m_bgPositionX);

    m_bgPositionY = new QDoubleSpinBox;
    m_bgPositionY->setMinimumWidth(72);
    m_bgPositionY->setRange(-10000, 10000);
    m_bgPositionY->setDecimals(2);
    m_bgPositionY->setSuffix(tr(" mm"));
    connect(m_bgPositionY, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &SketchPropertiesWidget::onPositionYChanged);
    positionForm->addRow(tr("Position Y:"), m_bgPositionY);

    m_bgWidth = new QDoubleSpinBox;
    m_bgWidth->setMinimumWidth(72);
    m_bgWidth->setRange(0.1, 10000);
    m_bgWidth->setDecimals(2);
    m_bgWidth->setSuffix(tr(" mm"));
    connect(m_bgWidth, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &SketchPropertiesWidget::onWidthChanged);
    positionForm->addRow(tr("Size W:"), m_bgWidth);

    m_bgHeight = new QDoubleSpinBox;
    m_bgHeight->setMinimumWidth(72);
    m_bgHeight->setRange(0.1, 10000);
    m_bgHeight->setDecimals(2);
    m_bgHeight->setSuffix(tr(" mm"));
    connect(m_bgHeight, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &SketchPropertiesWidget::onHeightChanged);
    positionForm->addRow(tr("Size H:"), m_bgHeight);

    m_bgRotation = new QDoubleSpinBox;
    m_bgRotation->setMinimumWidth(72);
    m_bgRotation->setRange(-360, 360);
    m_bgRotation->setDecimals(1);
    m_bgRotation->setSuffix(tr("\u00B0"));
    m_bgRotation->setToolTip(tr("Rotation angle (will be normalized to 0-360°)"));
    connect(m_bgRotation, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &SketchPropertiesWidget::onRotationChanged);
    positionForm->addRow(tr("Rotation:"), m_bgRotation);

    // The picture's scale, in the units a drawing is measured in: millimeters
    // of sketch per image pixel. Typing it sets the size from the pixel
    // dimensions (a 300 DPI scan is 25.4/300 = 0.0847 mm/px); Calibrate
    // Scale measures it from two points of known separation.
    m_bgScaleFactor = new QDoubleSpinBox;
    m_bgScaleFactor->setMinimumWidth(72);
    m_bgScaleFactor->setRange(0.0001, 1000.0);
    m_bgScaleFactor->setDecimals(4);
    m_bgScaleFactor->setSingleStep(0.01);
    m_bgScaleFactor->setValue(1.0);
    m_bgScaleFactor->setSuffix(tr(" mm/px"));
    m_bgScaleFactor->setToolTip(tr("Millimeters of sketch per image pixel: the picture's scale.\n"
                                   "Type it when you know it (a 300 DPI scan is 25.4/300 = 0.0847),\n"
                                   "or use Calibrate Scale to measure it from two known points."));
    connect(m_bgScaleFactor, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &SketchPropertiesWidget::onScaleChanged);
    positionForm->addRow(tr("Scale:"), m_bgScaleFactor);

    m_bgScaleInfo = new QLabel;
    m_bgScaleInfo->setWordWrap(true);
    m_bgScaleInfo->setToolTip(tr("The image's pixel size and its scale. Two scales appear when the\n"
                                 "aspect lock is off and the picture has been stretched."));
    positionForm->addRow(QString(), m_bgScaleInfo);

    layout->addLayout(positionForm);

    // Lock aspect ratio
    m_bgLockAspect = new QCheckBox(tr("Lock aspect ratio"));
    m_bgLockAspect->setChecked(true);
    m_bgLockAspect->setToolTip(tr("Keep the picture's own proportions (one scale for both axes).\n"
                                  "Off, width and height move independently and the scale splits into X and Y."));
    connect(m_bgLockAspect, &QCheckBox::toggled, this, &SketchPropertiesWidget::onLockAspectChanged);
    layout->addWidget(m_bgLockAspect);

    // Image adjustments
    auto* adjustForm = new QFormLayout;
    adjustForm->setSpacing(4);
    adjustForm->setRowWrapPolicy(QFormLayout::WrapLongRows);

    m_bgGrayscale = new QCheckBox(tr("Grayscale"));
    m_bgGrayscale->setToolTip(tr("Convert image to grayscale for easier tracing"));
    connect(m_bgGrayscale, &QCheckBox::toggled, this, &SketchPropertiesWidget::onGrayscaleChanged);
    adjustForm->addRow(QString(), m_bgGrayscale);

    m_bgContrast = new QDoubleSpinBox;
    m_bgContrast->setMinimumWidth(72);
    m_bgContrast->setRange(0.1, 3.0);
    m_bgContrast->setDecimals(2);
    m_bgContrast->setSingleStep(0.1);
    m_bgContrast->setValue(1.0);
    connect(m_bgContrast, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &SketchPropertiesWidget::onContrastChanged);
    adjustForm->addRow(tr("Contrast:"), m_bgContrast);

    m_bgBrightness = new QDoubleSpinBox;
    m_bgBrightness->setMinimumWidth(72);
    m_bgBrightness->setRange(-1.0, 1.0);
    m_bgBrightness->setDecimals(2);
    m_bgBrightness->setSingleStep(0.05);
    m_bgBrightness->setValue(0.0);
    connect(m_bgBrightness, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &SketchPropertiesWidget::onBrightnessChanged);
    adjustForm->addRow(tr("Brightness:"), m_bgBrightness);

    layout->addLayout(adjustForm);

    // Flip/Rotate controls
    auto* flipRotateLayout = new QHBoxLayout;

    m_bgFlipHButton = new QPushButton(tr("↔"));
    m_bgFlipHButton->setFixedWidth(28);
    m_bgFlipHButton->setToolTip(tr("Flip horizontally (mirror)"));
    connect(m_bgFlipHButton, &QPushButton::clicked, this, &SketchPropertiesWidget::onFlipHorizontal);
    flipRotateLayout->addWidget(m_bgFlipHButton);

    m_bgFlipVButton = new QPushButton(tr("↕"));
    m_bgFlipVButton->setFixedWidth(28);
    m_bgFlipVButton->setToolTip(tr("Flip vertically"));
    connect(m_bgFlipVButton, &QPushButton::clicked, this, &SketchPropertiesWidget::onFlipVertical);
    flipRotateLayout->addWidget(m_bgFlipVButton);

    flipRotateLayout->addSpacing(8);

    m_bgRotateCCWButton = new QPushButton(tr("↺"));
    m_bgRotateCCWButton->setFixedWidth(28);
    m_bgRotateCCWButton->setToolTip(tr("Rotate 90° counter-clockwise"));
    connect(m_bgRotateCCWButton, &QPushButton::clicked, this, &SketchPropertiesWidget::onRotate90CCW);
    flipRotateLayout->addWidget(m_bgRotateCCWButton);

    m_bgRotateCWButton = new QPushButton(tr("↻"));
    m_bgRotateCWButton->setFixedWidth(28);
    m_bgRotateCWButton->setToolTip(tr("Rotate 90° clockwise"));
    connect(m_bgRotateCWButton, &QPushButton::clicked, this, &SketchPropertiesWidget::onRotate90CW);
    flipRotateLayout->addWidget(m_bgRotateCWButton);

    m_bgRotate180Button = new QPushButton(tr("180°"));
    m_bgRotate180Button->setFixedWidth(36);
    m_bgRotate180Button->setToolTip(tr("Rotate 180°"));
    connect(m_bgRotate180Button, &QPushButton::clicked, this, &SketchPropertiesWidget::onRotate180);
    flipRotateLayout->addWidget(m_bgRotate180Button);

    flipRotateLayout->addStretch();

    layout->addLayout(flipRotateLayout);
}

void SketchPropertiesWidget::setupEntitySection()
{
    m_entityGroup = new QGroupBox(tr("Selected Entity"));
    auto* layout = new QVBoxLayout(m_entityGroup);

    m_entityStack = new QStackedWidget;

    // Page 0: No selection
    auto* noSelectionLabel = new QLabel(tr("No entity selected"));
    noSelectionLabel->setAlignment(Qt::AlignCenter);
    noSelectionLabel->setStyleSheet("QLabel { color: #666; font-style: italic; }");
    m_entityStack->addWidget(noSelectionLabel);

    // Page 1: Entity properties
    auto* entityPage = new QWidget;
    m_entityForm = new QFormLayout(entityPage);
    auto* entityLayout = m_entityForm;
    entityLayout->setContentsMargins(0, 0, 0, 0);

    m_entityTypeLabel = new QLabel;
    entityLayout->addRow(tr("Type:"), m_entityTypeLabel);

    m_entityIdLabel = new QLabel;
    entityLayout->addRow(tr("ID:"), m_entityIdLabel);

    m_entityConstructionLabel = new QLabel;
    entityLayout->addRow(tr("Construction:"), m_entityConstructionLabel);

    // Coordinate rows (labels set per plane in updateForSelection). Editable
    // spin boxes; edits write back through SketchCanvas::applyPointEdit.
    auto makeCoordSpin = [this]() {
        auto* sp = new QDoubleSpinBox;
        sp->setRange(-100000.0, 100000.0);
        sp->setDecimals(3);
        sp->setSingleStep(1.0);
        sp->setKeyboardTracking(false);   // commit on Enter/focus-out, not per keystroke
        connect(sp, qOverload<double>(&QDoubleSpinBox::valueChanged),
                this, &SketchPropertiesWidget::onCoordChanged);
        return sp;
    };
    // Point selector: multi-point entities (line, arc, slot, polygon...) expose
    // each point here so any vertex can be edited, not just the first.
    m_coordPointLabel = new QLabel(tr("Point:"));
    m_coordPointCombo = new QComboBox;
    connect(m_coordPointCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &SketchPropertiesWidget::onCoordPointChanged);
    entityLayout->addRow(m_coordPointLabel, m_coordPointCombo);

    m_coordULabel = new QLabel; m_coordUSpin = makeCoordSpin();
    entityLayout->addRow(m_coordULabel, m_coordUSpin);
    m_coordVLabel = new QLabel; m_coordVSpin = makeCoordSpin();
    entityLayout->addRow(m_coordVLabel, m_coordVSpin);
    m_coordWLabel = new QLabel; m_coordWSpin = makeCoordSpin();
    entityLayout->addRow(m_coordWLabel, m_coordWSpin);

    // Projection source row (shown only for projected/reference geometry).
    m_projectionRowLabel = new QLabel(tr("Projection:"));
    m_projectionValue = new QLabel;
    m_projectionValue->setWordWrap(true);
    entityLayout->addRow(m_projectionRowLabel, m_projectionValue);

    m_entityStack->addWidget(entityPage);

    // Page 2: Constraint properties
    auto* constraintPage = new QWidget;
    auto* constraintLayout = new QFormLayout(constraintPage);
    constraintLayout->setContentsMargins(0, 0, 0, 0);

    m_constraintTypeLabel = new QLabel;
    constraintLayout->addRow(tr("Type:"), m_constraintTypeLabel);

    m_constraintValueLabel = new QLabel;
    constraintLayout->addRow(tr("Value:"), m_constraintValueLabel);

    m_constraintDrivingLabel = new QLabel;
    constraintLayout->addRow(tr("Constrained:"), m_constraintDrivingLabel);

    m_constraintLabelAngleSpin = new QDoubleSpinBox;
    m_constraintLabelAngleSpin->setRange(-180.0, 360.0);
    m_constraintLabelAngleSpin->setDecimals(1);
    m_constraintLabelAngleSpin->setSuffix(QStringLiteral("\u00B0"));
    m_constraintLabelAngleSpin->setWrapping(true);
    m_constraintLabelAngleRow = new QLabel(tr("Label Angle:"));
    constraintLayout->addRow(m_constraintLabelAngleRow, m_constraintLabelAngleSpin);

    connect(m_constraintLabelAngleSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, &SketchPropertiesWidget::onLabelAngleChanged);

    m_entityStack->addWidget(constraintPage);

    layout->addWidget(m_entityStack);
}

void SketchPropertiesWidget::setSketchCanvas(SketchCanvas* canvas)
{
    if (m_canvas) {
        disconnect(m_canvas, nullptr, this, nullptr);
    }

    m_canvas = canvas;

    if (m_canvas) {
        connect(m_canvas, &SketchCanvas::selectionChanged,
                this, &SketchPropertiesWidget::updateForSelection);
        connect(m_canvas, &SketchCanvas::constraintModified,
                this, &SketchPropertiesWidget::updateForSelection);
        connect(m_canvas, &SketchCanvas::entityModified,
                this, &SketchPropertiesWidget::onCanvasEntityModified);
        connect(m_canvas, &SketchCanvas::transformSectionRequested,
                this, &SketchPropertiesWidget::onTransformSectionRequested);
        connect(m_canvas, &SketchCanvas::transformPivotChanged,
                this, &SketchPropertiesWidget::onCanvasPivotChanged);
        connect(m_canvas, &SketchCanvas::transformPickCompleted,
                this, &SketchPropertiesWidget::onCanvasPickCompleted);
        connect(m_canvas, &SketchCanvas::freeMoveChanged,
                this, &SketchPropertiesWidget::onCanvasFreeMoveChanged);
        connect(m_canvas, &SketchCanvas::transformApplyRequested,
                this, &SketchPropertiesWidget::applyTransform);
        connect(m_canvas, &SketchCanvas::transformCanceled,
                this, &SketchPropertiesWidget::onCanvasTransformCanceled);
    }
}

void SketchPropertiesWidget::setBackgroundImage(const sketch::BackgroundImage& bg)
{
    m_background = bg;
    updateBackgroundUi();
}

void SketchPropertiesWidget::updateBackgroundUi()
{
    m_updatingUi = true;

    bool hasImage = m_background.enabled;

    // Update file path display
    if (hasImage && !m_background.filePath.empty()) {
        m_bgFilePathEdit->setText(QString::fromStdString(m_background.filePath));
        m_bgFilePathEdit->setToolTip(QString::fromStdString(m_background.filePath));

        // Show storage type in label
        if (m_background.storage == sketch::BackgroundStorage::Embedded) {
            m_bgFilePathLabel->setText(tr("File (embedded):"));
        } else {
            m_bgFilePathLabel->setText(tr("File:"));
        }
    } else if (hasImage) {
        m_bgFilePathEdit->setText(tr("(embedded image)"));
        m_bgFilePathLabel->setText(tr("File (embedded):"));
    } else {
        m_bgFilePathEdit->clear();
        m_bgFilePathLabel->setText(tr("File:"));
    }

    // Enable/disable controls based on whether we have an image
    m_bgBrowseButton->setEnabled(true);  // Always allow browsing
    m_bgRemoveButton->setEnabled(hasImage);
    m_bgEditPositionButton->setEnabled(hasImage);
    m_bgCalibrateButton->setEnabled(hasImage);
    m_bgExportButton->setEnabled(hasImage && !m_projectDir.isEmpty());
    m_bgOpacitySlider->setEnabled(hasImage);
    m_bgOpacitySpinBox->setEnabled(hasImage);
    m_bgPositionX->setEnabled(hasImage);
    m_bgPositionY->setEnabled(hasImage);
    m_bgWidth->setEnabled(hasImage);
    m_bgHeight->setEnabled(hasImage);
    m_bgRotation->setEnabled(hasImage);
    m_bgScaleFactor->setEnabled(hasImage && m_background.originalPixelWidth > 0);
    m_bgLockAspect->setEnabled(hasImage);
    m_bgGrayscale->setEnabled(hasImage);
    m_bgContrast->setEnabled(hasImage);
    m_bgBrightness->setEnabled(hasImage);
    m_bgFlipHButton->setEnabled(hasImage);
    m_bgFlipVButton->setEnabled(hasImage);
    m_bgRotateCWButton->setEnabled(hasImage);
    m_bgRotateCCWButton->setEnabled(hasImage);
    m_bgRotate180Button->setEnabled(hasImage);

    if (hasImage) {
        m_bgOpacitySlider->setValue(m_background.opacityPercent());
        m_bgOpacitySpinBox->setValue(m_background.opacityPercent());
        m_bgPositionX->setValue(m_background.position.x);
        m_bgPositionY->setValue(m_background.position.y);
        m_bgWidth->setValue(m_background.width);
        m_bgHeight->setValue(m_background.height);
        m_bgRotation->setValue(m_background.rotation);
        refreshScaleReadout();
        m_bgLockAspect->setChecked(m_background.lockAspectRatio);
        m_bgGrayscale->setChecked(m_background.grayscale);
        m_bgContrast->setValue(m_background.contrast);
        m_bgBrightness->setValue(m_background.brightness);
    }

    m_updatingUi = false;
}

void SketchPropertiesWidget::updateForSelection()
{
    if (!m_canvas) return;
    m_coordEntityId = -1;   // cleared unless an entity with points is shown

    // The Background Image section is only relevant when nothing else is
    // selected (the sketch background itself). Hide it once any entity,
    // constraint, or point is picked.
    if (m_backgroundGroup) {
        const bool nothingSelected =
            m_canvas->selectedEntities().isEmpty()
            && m_canvas->selectedConstraintId() < 0
            && m_canvas->selectedPoints().isEmpty();
        m_backgroundGroup->setVisible(nothingSelected);
    }

    // Bezier anchor editor: shown when exactly one splineBezier anchor is picked.
    {
        int baS = -1, baA = -1;
        const bool anchorSel = m_canvas->selectedBezierAnchor(baS, baA);
        if (m_bezierAnchorGroup) m_bezierAnchorGroup->setVisible(anchorSel);
        if (anchorSel) {
            double ang = 0, inL = 0, outL = 0, wt = 1; bool rat = false;
            if (m_canvas->bezierAnchorProps(baS, baA, ang, inL, outL, wt, rat)) {
                m_baSplineId = baS; m_baAnchorIdx = baA;
                m_updatingUi = true;
                m_baAngle->setValue(ang);
                m_baInLen->setValue(inL);   m_baInLen->setEnabled(inL > 0.0);
                m_baOutLen->setValue(outL); m_baOutLen->setEnabled(outL > 0.0);
                m_baWeight->setValue(wt);
                m_updatingUi = false;
                m_entityStack->setCurrentIndex(0);   // no whole-entity page here
                return;
            }
        }
    }

    // Bezier leg readout: two consecutive control points selected (a leg).
    {
        int lS = -1, lI0 = -1, lI1 = -1;
        const bool legSel = m_canvas->selectedBezierLeg(lS, lI0, lI1);
        if (m_bezierLegGroup) m_bezierLegGroup->setVisible(legSel);
        if (legSel) {
            const SketchEntity* e = m_canvas->entityById(lS);
            if (e && lI1 < static_cast<int>(e->points.size())) {
                m_blSplineId = lS; m_blI0 = lI0; m_blI1 = lI1;
                const double len = QLineF(QPointF(e->points[lI0]),
                                          QPointF(e->points[lI1])).length();
                m_updatingUi = true;
                m_blLength->setValue(len);
                m_updatingUi = false;
                m_entityStack->setCurrentIndex(0);
                return;
            }
        }
    }

    // Check if a constraint is selected
    int constraintId = m_canvas->selectedConstraintId();
    if (constraintId >= 0) {
        const SketchConstraint* c = m_canvas->constraintById(constraintId);
        if (c) {
            // Show constraint type
            static const char* typeNames[] = {
                "Distance", "Radius", "Diameter", "Angle", "Fixed Angle",
                "Horizontal", "Vertical", "Parallel", "Perpendicular",
                "Coincident", "Tangent", "Equal", "Midpoint", "Symmetric",
                "Concentric", "Collinear", "Fixed"
            };
            int typeIdx = static_cast<int>(c->type);
            if (typeIdx >= 0 && typeIdx < static_cast<int>(std::size(typeNames)))
                m_constraintTypeLabel->setText(tr(typeNames[typeIdx]));
            else
                m_constraintTypeLabel->setText(QString::number(typeIdx));

            m_constraintValueLabel->setText(QString::number(c->value, 'f', 4));
            m_constraintDrivingLabel->setText(c->isDriving ? tr("Yes") : tr("No"));

            // Show label angle for circle radius/diameter constraints
            bool showLabelAngle = false;
            if ((c->type == ConstraintType::Radius || c->type == ConstraintType::Diameter)
                && !c->entityIds.empty()) {
                const SketchEntity* ent = m_canvas->entityById(c->entityIds[0]);
                if (ent && ent->type == SketchEntityType::Circle) {
                    showLabelAngle = true;
                    m_updatingUi = true;
                    double angleDeg = std::isnan(c->labelAngle)
                        ? 0.0 : radiansToDegrees(c->labelAngle);
                    m_constraintLabelAngleSpin->setValue(angleDeg);
                    m_updatingUi = false;
                }
            }
            m_constraintLabelAngleRow->setVisible(showLabelAngle);
            m_constraintLabelAngleSpin->setVisible(showLabelAngle);

            m_entityStack->setCurrentIndex(2);  // Constraint page
            return;
        }
    }

    // Check for entity selection
    auto selected = m_canvas->selectedEntities();
    if (selected.isEmpty()) {
        m_entityStack->setCurrentIndex(0);  // No selection page
    } else {
        const SketchEntity* ent = selected.first();
        m_entityTypeLabel->setText(tr(sketch::entityTypeName(ent->type)));   // one table, in the library

        m_entityIdLabel->setText(QString::number(ent->id));
        m_entityConstructionLabel->setText(ent->isConstruction ? tr("Yes") : tr("No"));

        // Coordinates of the primary point, labeled by the plane's axes; the
        // off-plane (normal) row shows only in 3D mode. See planeAxisLabels.
        const hobbycad::PlaneAxisLabels ax =
            hobbycad::planeAxisLabels(m_canvas->sketchPlane());
        const bool hasPt = !ent->points.empty();
        const bool is3D = m_canvas->sketchMode();
        const bool projected = ent->projectionSourceId >= 0;
        if (hasPt) {
            m_coordULabel->setText(QStringLiteral("%1:").arg(ax.u));
            m_coordVLabel->setText(QStringLiteral("%1:").arg(ax.v));
            m_coordWLabel->setText(QStringLiteral("%1:").arg(ax.normal));
            // A projection is driven by its source; do not let the panel edit it.
            m_coordEntityId = projected ? -1 : ent->id;
            // Populate the point selector with role-aware labels where known.
            const int npts = static_cast<int>(ent->points.size());
            QStringList roles;
            switch (ent->type) {
            case SketchEntityType::Line: roles = {tr("Start"), tr("End")}; break;
            case SketchEntityType::Arc:  roles = {tr("Center"), tr("Start"), tr("End")}; break;
            case SketchEntityType::Circle: roles = {tr("Center")}; break;
            default: break;
            }
            m_updatingUi = true;
            m_coordPointCombo->clear();
            for (int i = 0; i < npts; ++i)
                m_coordPointCombo->addItem(i < roles.size() ? roles.at(i)
                                                            : tr("Point %1").arg(i + 1));
            m_coordPointCombo->setCurrentIndex(0);
            m_updatingUi = false;
            m_coordPointIndex = 0;
            loadCoordSpinsFromPoint(0);
        }
        m_entityForm->setRowVisible(m_coordPointCombo, hasPt && ent->points.size() > 1);
        m_entityForm->setRowVisible(m_coordUSpin, hasPt);
        m_entityForm->setRowVisible(m_coordVSpin, hasPt);
        m_entityForm->setRowVisible(m_coordWSpin, hasPt && is3D);
        m_coordUSpin->setEnabled(!projected);
        m_coordVSpin->setEnabled(!projected);
        m_coordWSpin->setEnabled(!projected);

        // Projection source: resolve a friendly label from the host's source
        // list; fall back to raw ids. Row hidden for ordinary geometry.
        if (projected) {
            QString src;
            for (const auto& psrc : m_canvas->availableProjectionSources())
                if (psrc.sketchId == ent->projectionSourceSketchId
                    && psrc.entityId == ent->projectionSourceId) { src = psrc.label; break; }
            if (src.isEmpty())
                src = tr("sketch %1, entity %2")
                          .arg(ent->projectionSourceSketchId).arg(ent->projectionSourceId);
            m_projectionValue->setText(
                tr("%1\n(reference: edit it in the source sketch)").arg(src));
        }
        m_entityForm->setRowVisible(m_projectionValue, projected);

        m_entityStack->setCurrentIndex(1);  // Entity page
    }

    // Transform section: present whenever something is selected.
    if (m_transformGroup) {
        m_transformGroup->setVisible(!selected.isEmpty());
        resetTransformForm();
    }
}

void SketchPropertiesWidget::onCoordChanged()
{
    if (m_updatingUi || !m_canvas) return;
    if (m_coordEntityId < 0 || m_coordPointIndex < 0) return;
    // The off-plane spin holds the current z when hidden (2D), so editing u/v
    // preserves the point's off-plane component.
    m_canvas->applyPointEdit(m_coordEntityId, m_coordPointIndex,
        hobbycad::Point3(m_coordUSpin->value(),
                         m_coordVSpin->value(),
                         m_coordWSpin->value()));
}

void SketchPropertiesWidget::loadCoordSpinsFromPoint(int index)
{
    if (!m_canvas) return;
    const auto selected = m_canvas->selectedEntities();
    if (selected.isEmpty()) return;
    const SketchEntity* ent = selected.first();
    if (!ent || index < 0 || index >= static_cast<int>(ent->points.size())) return;
    const auto& p = ent->points[index];
    m_updatingUi = true;
    m_coordUSpin->setValue(p.x);
    m_coordVSpin->setValue(p.y);
    m_coordWSpin->setValue(p.z);
    m_updatingUi = false;
}

void SketchPropertiesWidget::onCoordPointChanged(int index)
{
    if (m_updatingUi || index < 0) return;
    m_coordPointIndex = index;   // subsequent coord edits target this point
    loadCoordSpinsFromPoint(index);
}

void SketchPropertiesWidget::onLabelAngleChanged(double degrees)
{
    if (m_updatingUi || !m_canvas) return;

    int constraintId = m_canvas->selectedConstraintId();
    if (constraintId < 0) return;

    SketchConstraint* c = m_canvas->constraintById(constraintId);
    if (!c) return;

    if ((c->type != ConstraintType::Radius && c->type != ConstraintType::Diameter)
        || c->entityIds.empty()) return;

    const SketchEntity* ent = m_canvas->entityById(c->entityIds[0]);
    if (!ent || ent->type != SketchEntityType::Circle || ent->points.empty()) return;

    double angleRad = degreesToRadians(degrees);
    c->labelAngle = angleRad;

    // Reposition the label at the new angle, keeping the current distance from center
    QPointF center = ent->points[0];
    QPointF oldLabel = c->labelPosition;
    QPointF offset = oldLabel - center;
    double labelDist = geometry::length(offset);
    if (labelDist < geometry::kDegenerateLen) labelDist = ent->radius / 2.0;
    c->labelPosition = geometry::polarPoint(center, labelDist, angleRad);

    m_canvas->update();
}

void SketchPropertiesWidget::onOpacityChanged(int percent)
{
    if (m_updatingUi) return;
    m_background.setOpacityPercent(percent);
    emitBackgroundChanged();
}

void SketchPropertiesWidget::onPositionXChanged(double x)
{
    if (m_updatingUi) return;
    m_background.position.x = x;
    emitBackgroundChanged();
}

void SketchPropertiesWidget::onPositionYChanged(double y)
{
    if (m_updatingUi) return;
    m_background.position.y = y;
    emitBackgroundChanged();
}

void SketchPropertiesWidget::onWidthChanged(double w)
{
    if (m_updatingUi) return;

    if (m_background.lockAspectRatio) {
        // The lock holds the picture's OWN proportions (its pixel size), so
        // it cannot freeze a stretched shape from an unlocked moment.
        m_background.height = m_background.heightForWidthLocked(w);
        m_updatingUi = true;
        m_bgHeight->setValue(m_background.height);
        m_updatingUi = false;
    }
    m_background.width = w;
    refreshScaleReadout();
    emitBackgroundChanged();
}

void SketchPropertiesWidget::onHeightChanged(double h)
{
    if (m_updatingUi) return;

    if (m_background.lockAspectRatio) {
        m_background.width = m_background.widthForHeightLocked(h);
        m_updatingUi = true;
        m_bgWidth->setValue(m_background.width);
        m_updatingUi = false;
    }
    m_background.height = h;
    refreshScaleReadout();
    emitBackgroundChanged();
}

void SketchPropertiesWidget::onRotationChanged(double deg)
{
    if (m_updatingUi) return;

    // Use setRotation which normalizes to 0-360 range
    m_background.setRotation(deg);

    // Update spinbox to show normalized value
    m_updatingUi = true;
    m_bgRotation->setValue(m_background.rotation);
    m_updatingUi = false;

    emitBackgroundChanged();
}

void SketchPropertiesWidget::onScaleChanged(double mmPerPixel)
{
    if (m_updatingUi) return;

    // One scale for both axes: the size follows from the pixel dimensions.
    m_background.setMmPerPixel(mmPerPixel);

    m_updatingUi = true;
    m_bgWidth->setValue(m_background.width);
    m_bgHeight->setValue(m_background.height);
    m_updatingUi = false;
    refreshScaleReadout();

    emitBackgroundChanged();
}

// The Scale field and the line under it: the picture's scale spelled out so
// the person knows what a millimeter on the picture is. Two scales appear
// when the lock is off and the picture has been stretched.
void SketchPropertiesWidget::refreshScaleReadout()
{
    const bool wasUpdating = m_updatingUi;
    m_updatingUi = true;
    const int pw = m_background.originalPixelWidth, ph = m_background.originalPixelHeight;
    const double sx = m_background.mmPerPixelX(), sy = m_background.mmPerPixelY();
    if (pw <= 0 || ph <= 0) {
        m_bgScaleInfo->setText(tr("Pixel size unknown until the image loads."));
    } else if (m_background.hasUniformScale(1e-6)) {
        m_bgScaleFactor->setValue(sx);
        m_bgScaleInfo->setText(tr("%1 x %2 px; 1 mm = %3 px%4")
            .arg(pw).arg(ph).arg(sx > 0 ? 1.0 / sx : 0.0, 0, 'f', 2)
            .arg(m_background.calibrated ? tr(" (calibrated)") : QString()));
    } else {
        m_bgScaleFactor->setValue(sx);
        m_bgScaleInfo->setText(tr("%1 x %2 px; stretched: X %3 mm/px, Y %4 mm/px")
            .arg(pw).arg(ph).arg(sx, 0, 'f', 4).arg(sy, 0, 'f', 4));
    }
    m_updatingUi = wasUpdating;
}

void SketchPropertiesWidget::onLockAspectChanged(bool locked)
{
    if (m_updatingUi) return;
    m_background.lockAspectRatio = locked;
    emitBackgroundChanged();
}

void SketchPropertiesWidget::onGrayscaleChanged(bool grayscale)
{
    if (m_updatingUi) return;
    m_background.grayscale = grayscale;
    emitBackgroundChanged();
}

void SketchPropertiesWidget::onContrastChanged(double contrast)
{
    if (m_updatingUi) return;
    m_background.contrast = contrast;
    emitBackgroundChanged();
}

void SketchPropertiesWidget::onBrightnessChanged(double brightness)
{
    if (m_updatingUi) return;
    m_background.brightness = brightness;
    emitBackgroundChanged();
}

void SketchPropertiesWidget::onFlipHorizontal()
{
    m_background.flipHorizontal = !m_background.flipHorizontal;
    emitBackgroundChanged();
}

void SketchPropertiesWidget::onFlipVertical()
{
    m_background.flipVertical = !m_background.flipVertical;
    emitBackgroundChanged();
}

void SketchPropertiesWidget::onRotate90CW()
{
    m_background.setRotation(m_background.rotation + 90.0);

    // Update the rotation spinbox
    m_updatingUi = true;
    m_bgRotation->setValue(m_background.rotation);
    m_updatingUi = false;

    emitBackgroundChanged();
}

void SketchPropertiesWidget::onRotate90CCW()
{
    m_background.setRotation(m_background.rotation - 90.0);

    // Update the rotation spinbox
    m_updatingUi = true;
    m_bgRotation->setValue(m_background.rotation);
    m_updatingUi = false;

    emitBackgroundChanged();
}

void SketchPropertiesWidget::onRotate180()
{
    m_background.setRotation(m_background.rotation + 180.0);

    // Update the rotation spinbox
    m_updatingUi = true;
    m_bgRotation->setValue(m_background.rotation);
    m_updatingUi = false;

    emitBackgroundChanged();
}

void SketchPropertiesWidget::emitBackgroundChanged()
{
    emit backgroundImageChanged(m_background);
}

void SketchPropertiesWidget::onBrowseForImage()
{
    // Placement is the user's, not the file's: they positioned, rotated and
    // adjusted the old image deliberately, and swapping the underlying file
    // is not a request to undo that. Opacity is deliberately NOT in this
    // list: the dialog offers an opacity control seeded from the current
    // value, so whatever comes back from it is an explicit choice.
    const Point2D oldPosition   = m_background.position;
    const double  oldRotation   = m_background.rotation;
    const bool    oldLockAspect = m_background.lockAspectRatio;
    const bool    oldGrayscale  = m_background.grayscale;
    const double  oldContrast   = m_background.contrast;
    const double  oldBrightness = m_background.brightness;
    const bool    hadImage      = m_background.enabled;

    BackgroundImageDialog dlg(this);
    dlg.setProjectDir(m_projectDir);
    if (hadImage) {
        dlg.setBackgroundImage(m_background);
    }
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }

    sketch::BackgroundImage picked = dlg.backgroundImage();
    if (!picked.enabled) {
        // accept() refuses to close without an image, so this guards a
        // future change rather than a path a user can reach today.
        QMessageBox::warning(this, tr("Load Failed"),
            tr("Failed to load the image file."));
        return;
    }

    if (hadImage) {
        picked.position        = oldPosition;
        picked.rotation        = oldRotation;
        picked.lockAspectRatio = oldLockAspect;
        picked.grayscale       = oldGrayscale;
        picked.contrast        = oldContrast;
        picked.brightness      = oldBrightness;
    }

    m_background = picked;
    updateBackgroundUi();
    emitBackgroundChanged();
}

void SketchPropertiesWidget::onEditPositionToggled(bool checked)
{
    emit backgroundEditModeRequested(checked);
}

void SketchPropertiesWidget::setBackgroundEditMode(bool enabled)
{
    // Update button state without triggering signal
    m_bgEditPositionButton->blockSignals(true);
    m_bgEditPositionButton->setChecked(enabled);
    m_bgEditPositionButton->blockSignals(false);
}

void SketchPropertiesWidget::onExportToProject()
{
    if (!m_background.enabled || m_projectDir.isEmpty()) {
        return;
    }

    // Get sketch name for filename generation
    QString sketchName = QStringLiteral("sketch");
    if (m_canvas) {
        // Could get sketch name from canvas or parent if available
    }

    sketch::BackgroundImage exported = sketch::exportBackgroundToProject(
        m_background, m_projectDir.toStdString(), sketchName.toStdString());

    if (exported.storage == sketch::BackgroundStorage::FilePath &&
        !exported.filePath.empty()) {
        m_background = exported;
        updateBackgroundUi();
        emitBackgroundChanged();

        QMessageBox::information(this, tr("Export Complete"),
            tr("Background image exported to:\n%1").arg(QString::fromStdString(exported.filePath)));
    } else {
        QMessageBox::warning(this, tr("Export Failed"),
            tr("Failed to export the background image to the project directory."));
    }
}


// ===========================================================================
//  Transform section: Fusion's Move/Copy, modeless, in the panel
// ===========================================================================

QString SketchPropertiesWidget::pointText(const QPointF& p)
{
    return QStringLiteral("(%1, %2)").arg(p.x(), 0, 'f', 3).arg(p.y(), 0, 'f', 3);
}

void SketchPropertiesWidget::setupBezierAnchorSection()
{
    m_bezierAnchorGroup = new QGroupBox(tr("Bezier Anchor"));
    auto* form = new QFormLayout(m_bezierAnchorGroup);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);

    m_baAngle = new QDoubleSpinBox;
    m_baAngle->setRange(0.0, 360.0); m_baAngle->setDecimals(2);
    m_baAngle->setWrapping(true); m_baAngle->setSuffix(QStringLiteral(" \xC2\xB0"));
    connect(m_baAngle, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &SketchPropertiesWidget::onBezierAngleChanged);
    form->addRow(tr("Tangent angle:"), m_baAngle);

    m_baInLen = new QDoubleSpinBox;
    m_baInLen->setRange(0.0, 1.0e6); m_baInLen->setDecimals(3);
    connect(m_baInLen, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &SketchPropertiesWidget::onBezierInLenChanged);
    form->addRow(tr("In handle length:"), m_baInLen);

    m_baOutLen = new QDoubleSpinBox;
    m_baOutLen->setRange(0.0, 1.0e6); m_baOutLen->setDecimals(3);
    connect(m_baOutLen, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &SketchPropertiesWidget::onBezierOutLenChanged);
    form->addRow(tr("Out handle length:"), m_baOutLen);

    m_baWeight = new QDoubleSpinBox;
    m_baWeight->setRange(0.01, 1.0e4); m_baWeight->setDecimals(3); m_baWeight->setValue(1.0);
    connect(m_baWeight, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &SketchPropertiesWidget::onBezierWeightChanged);
    form->addRow(tr("Weight (rational):"), m_baWeight);

    m_bezierAnchorGroup->setVisible(false);

    m_bezierLegGroup = new QGroupBox(tr("Bezier Leg"));
    auto* legForm = new QFormLayout(m_bezierLegGroup);
    m_blLength = new QDoubleSpinBox;
    m_blLength->setRange(0.0, 1.0e6); m_blLength->setDecimals(3);
    connect(m_blLength, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &SketchPropertiesWidget::onBezierLegLengthChanged);
    legForm->addRow(tr("Leg length:"), m_blLength);
    m_bezierLegGroup->setVisible(false);
}

void SketchPropertiesWidget::onBezierAngleChanged(double deg)
{
    if (m_updatingUi || !m_canvas || m_baSplineId < 0) return;
    m_canvas->setBezierAnchorAngle(m_baSplineId, m_baAnchorIdx, deg);
}
void SketchPropertiesWidget::onBezierInLenChanged(double len)
{
    if (m_updatingUi || !m_canvas || m_baSplineId < 0) return;
    m_canvas->setBezierAnchorHandleLen(m_baSplineId, m_baAnchorIdx, /*out=*/false, len);
}
void SketchPropertiesWidget::onBezierOutLenChanged(double len)
{
    if (m_updatingUi || !m_canvas || m_baSplineId < 0) return;
    m_canvas->setBezierAnchorHandleLen(m_baSplineId, m_baAnchorIdx, /*out=*/true, len);
}
void SketchPropertiesWidget::onBezierWeightChanged(double w)
{
    if (m_updatingUi || !m_canvas || m_baSplineId < 0) return;
    m_canvas->setBezierAnchorWeight(m_baSplineId, m_baAnchorIdx, w);
}

void SketchPropertiesWidget::onBezierLegLengthChanged(double len)
{
    if (m_updatingUi || !m_canvas || m_blSplineId < 0) return;
    m_canvas->setBezierLegLength(m_blSplineId, m_blI0, m_blI1, len);
}

void SketchPropertiesWidget::setupTransformSection()
{
    m_transformGroup = new QGroupBox(tr("Transform"));
    m_transformGroup->setCheckable(true);     // the checkbox is the collapse
    m_transformGroup->setChecked(false);
    auto* outer = new QVBoxLayout(m_transformGroup);
    m_transformBody = new QWidget;
    m_transformBody->setVisible(false);
    outer->addWidget(m_transformBody);
    auto* form = new QFormLayout(m_transformBody);
    form->setContentsMargins(0, 0, 0, 0);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);

    auto spin = [this](double lo, double hi, int dp, double val, const QString& suffix) {
        auto* sb = new QDoubleSpinBox; sb->setRange(lo, hi); sb->setDecimals(dp); sb->setValue(val);
        if (!suffix.isEmpty()) sb->setSuffix(suffix);
        sb->setKeyboardTracking(false);
        connect(sb, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &SketchPropertiesWidget::onTransformFieldChanged);
        return sb;
    };
    auto row = [&](QList<QWidget*>& bucket, const QString& label, QWidget* w) {
        auto* l = new QLabel(label);
        form->addRow(l, w);
        bucket << l << w;
    };
    auto pair = [&](QWidget* a, QWidget* b) {
        auto* box = new QWidget; auto* hl = new QHBoxLayout(box); hl->setContentsMargins(0, 0, 0, 0);
        hl->addWidget(a); hl->addWidget(b, 1); return box;
    };
    auto muted = [](QLabel* l) { l->setStyleSheet("QLabel { color: #666; font-style: italic; }"); l->setText(QObject::tr("Not set")); return l; };

    m_moveType = new QComboBox;
    m_moveType->addItems({tr("Translate"), tr("Rotate"), tr("Scale"), tr("Mirror"),
                          tr("Point to point"), tr("Point to position"), tr("Free move")});
    form->addRow(tr("Move type:"), m_moveType);
    connect(m_moveType, qOverload<int>(&QComboBox::currentIndexChanged), this, &SketchPropertiesWidget::onMoveTypeChanged);

    // Translate
    m_txDx = spin(-10000, 10000, 3, 0, QStringLiteral(" mm")); m_txDy = spin(-10000, 10000, 3, 0, QStringLiteral(" mm"));
    row(m_rowsTranslate, tr("X distance:"), m_txDx); row(m_rowsTranslate, tr("Y distance:"), m_txDy);
    // Rotate
    m_rotAngle = spin(-360, 360, 2, 0, QStringLiteral("°"));
    row(m_rowsRotate, tr("Angle:"), m_rotAngle);
    // Scale
    m_scaleFactor = spin(0.001, 1000, 4, 1.0, QString());
    row(m_rowsScale, tr("Factor:"), m_scaleFactor);
    // Mirror
    m_mirrorAxis = new QComboBox;
    m_mirrorAxis->addItems({tr("Horizontal axis through pivot"), tr("Vertical axis through pivot"), tr("Picked line")});
    connect(m_mirrorAxis, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) { updateTransformRows(); onTransformFieldChanged(); });
    row(m_rowsMirror, tr("Axis:"), m_mirrorAxis);
    m_pickMirrorA = new QPushButton(tr("Pick line point A")); m_pickMirrorA->setCheckable(true);
    m_pickMirrorB = new QPushButton(tr("Pick line point B")); m_pickMirrorB->setCheckable(true);
    m_mirrorALabel = muted(new QLabel); m_mirrorBLabel = muted(new QLabel);
    row(m_rowsMirrorLine, tr("Line A:"), pair(m_pickMirrorA, m_mirrorALabel));
    row(m_rowsMirrorLine, tr("Line B:"), pair(m_pickMirrorB, m_mirrorBLabel));
    connect(m_pickMirrorA, &QPushButton::clicked, this, [this] { onPickButtonClicked(int(SketchCanvas::TransformPick::MirrorA)); });
    connect(m_pickMirrorB, &QPushButton::clicked, this, [this] { onPickButtonClicked(int(SketchCanvas::TransformPick::MirrorB)); });
    // Point to point
    m_pickFrom = new QPushButton(tr("Pick from point")); m_pickFrom->setCheckable(true);
    m_pickTo = new QPushButton(tr("Pick to point")); m_pickTo->setCheckable(true);
    m_fromLabel = muted(new QLabel); m_toLabel = muted(new QLabel); m_deltaLabel = new QLabel(QStringLiteral("—"));
    row(m_rowsP2P, tr("From:"), pair(m_pickFrom, m_fromLabel));
    row(m_rowsP2P, tr("To:"), pair(m_pickTo, m_toLabel));
    row(m_rowsP2P, tr("Δ (x, y):"), m_deltaLabel);
    connect(m_pickFrom, &QPushButton::clicked, this, [this] { onPickButtonClicked(int(SketchCanvas::TransformPick::FromPoint)); });
    connect(m_pickTo, &QPushButton::clicked, this, [this] { onPickButtonClicked(int(SketchCanvas::TransformPick::ToPoint)); });
    // Point to position
    m_pickPoint = new QPushButton(tr("Pick point on selection")); m_pickPoint->setCheckable(true);
    m_pickedLabel = muted(new QLabel);
    m_targetX = spin(-100000, 100000, 3, 0, QStringLiteral(" mm")); m_targetY = spin(-100000, 100000, 3, 0, QStringLiteral(" mm"));
    row(m_rowsP2Pos, tr("Point:"), pair(m_pickPoint, m_pickedLabel));
    // Target: absolute by default, or an offset from a picked reference point
    // (the Absolute/Relative switch of Draft and QCAD, with the reference chosen on canvas).
    m_targetMode = new QComboBox;
    m_targetMode->addItems({tr("Absolute"), tr("Relative to reference")});
    connect(m_targetMode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) { updateTransformRows(); onTransformFieldChanged(); });
    row(m_rowsP2Pos, tr("Target is:"), m_targetMode);
    m_pickRef = new QPushButton(tr("Pick reference")); m_pickRef->setCheckable(true);
    m_refLabel = muted(new QLabel);
    row(m_rowsP2PosRef, tr("Reference:"), pair(m_pickRef, m_refLabel));
    connect(m_pickRef, &QPushButton::clicked, this, [this] { onPickButtonClicked(int(SketchCanvas::TransformPick::ReferencePoint)); });
    m_targetXLabel = new QLabel(tr("Target X:")); m_targetYLabel = new QLabel(tr("Target Y:"));
    form->addRow(m_targetXLabel, m_targetX); form->addRow(m_targetYLabel, m_targetY);
    m_rowsP2Pos << m_targetXLabel << m_targetX << m_targetYLabel << m_targetY;
    connect(m_pickPoint, &QPushButton::clicked, this, [this] { onPickButtonClicked(int(SketchCanvas::TransformPick::PointOnSelection)); });
    // Free move
    m_fmDx = spin(-10000, 10000, 3, 0, QStringLiteral(" mm")); m_fmDy = spin(-10000, 10000, 3, 0, QStringLiteral(" mm"));
    m_fmAngle = spin(-360, 360, 2, 0, QStringLiteral("°"));
    m_fmHint = new QLabel(tr("Drag the selection to move it; drag the ring to turn it about the star. Ctrl = axis lock / 15° steps."));
    m_fmHint->setWordWrap(true); m_fmHint->setStyleSheet("QLabel { color: #666; }");
    row(m_rowsFreeMove, tr("dx:"), m_fmDx); row(m_rowsFreeMove, tr("dy:"), m_fmDy); row(m_rowsFreeMove, tr("Angle:"), m_fmAngle);
    form->addRow(m_fmHint); m_rowsFreeMove << m_fmHint;
    for (auto* sb : {m_fmDx, m_fmDy, m_fmAngle}) {
        disconnect(sb, nullptr, this, nullptr);
        connect(sb, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) {
            if (m_updatingUi || !m_canvas) return;
            m_canvas->setFreeMove(QPointF(m_fmDx->value(), m_fmDy->value()), m_fmAngle->value());
            onTransformFieldChanged();
        });
    }
    // Pivot
    m_pivotX = spin(-100000, 100000, 3, 0, QStringLiteral(" mm")); m_pivotY = spin(-100000, 100000, 3, 0, QStringLiteral(" mm"));
    for (auto* sb : {m_pivotX, m_pivotY}) { disconnect(sb, nullptr, this, nullptr); connect(sb, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &SketchPropertiesWidget::onPivotFieldChanged); }
    m_setPivotBtn = new QPushButton(tr("Set pivot")); m_setPivotBtn->setCheckable(true);
    m_centerPivotBtn = new QPushButton(tr("Center"));
    m_pivotSourceLabel = new QLabel; m_pivotSourceLabel->setStyleSheet("QLabel { color: #666; font-style: italic; }");
    row(m_rowsPivot, tr("Pivot X:"), m_pivotX); row(m_rowsPivot, tr("Pivot Y:"), m_pivotY);
    row(m_rowsPivot, QString(), pair(m_setPivotBtn, m_centerPivotBtn));
    row(m_rowsPivot, tr("Pivot is:"), m_pivotSourceLabel);
    connect(m_setPivotBtn, &QPushButton::clicked, this, &SketchPropertiesWidget::onSetPivotClicked);
    connect(m_centerPivotBtn, &QPushButton::clicked, this, &SketchPropertiesWidget::onCenterPivotClicked);
    // Copy / Apply / status
    m_createCopy = new QCheckBox(tr("Create copy"));
    connect(m_createCopy, &QCheckBox::toggled, this, &SketchPropertiesWidget::onTransformFieldChanged);
    m_applyBtn = new QPushButton(tr("Apply"));
    m_applyBtn->setDefault(true);
    connect(m_applyBtn, &QPushButton::clicked, this, &SketchPropertiesWidget::applyTransform);
    form->addRow(m_createCopy, m_applyBtn);
    m_transformStatus = new QLabel; m_transformStatus->setWordWrap(true);
    m_transformStatus->setMinimumHeight(2 * m_transformStatus->fontMetrics().height() + 4);   // no row jumping
    form->addRow(m_transformStatus);

    // Enter anywhere in the section applies (spin boxes commit their edit first).
    for (auto key : {Qt::Key_Return, Qt::Key_Enter}) {
        auto* sc = new QShortcut(QKeySequence(key), m_transformGroup);
        sc->setContext(Qt::WidgetWithChildrenShortcut);
        connect(sc, &QShortcut::activated, this, &SketchPropertiesWidget::applyTransform);
    }

    connect(m_transformGroup, &QGroupBox::toggled, this, &SketchPropertiesWidget::onTransformToggled);
    m_transformGroup->setVisible(false);
    updateTransformRows();
}

void SketchPropertiesWidget::updateTransformRows()
{
    const auto type = static_cast<MoveType>(m_moveType->currentIndex());
    auto show = [](const QList<QWidget*>& ws, bool on) { for (auto* w : ws) w->setVisible(on); };
    show(m_rowsTranslate, type == MoveType::Translate);
    show(m_rowsRotate, type == MoveType::Rotate);
    show(m_rowsScale, type == MoveType::Scale);
    show(m_rowsMirror, type == MoveType::Mirror);
    show(m_rowsMirrorLine, type == MoveType::Mirror && m_mirrorAxis->currentIndex() == 2);
    show(m_rowsP2P, type == MoveType::PointToPoint);
    show(m_rowsP2Pos, type == MoveType::PointToPosition);
    const bool relTarget = m_targetMode->currentIndex() == 1;
    show(m_rowsP2PosRef, type == MoveType::PointToPosition && relTarget);
    m_targetXLabel->setText(relTarget ? tr("Offset X:") : tr("Target X:"));
    m_targetYLabel->setText(relTarget ? tr("Offset Y:") : tr("Target Y:"));
    show(m_rowsFreeMove, type == MoveType::FreeMove);
    const bool wholeGroup = m_canvas && m_canvas->selectedWholeGroupId() >= 0;
    const bool pivotMatters = type == MoveType::Rotate || type == MoveType::Scale
                           || (type == MoveType::Mirror && m_mirrorAxis->currentIndex() != 2) || type == MoveType::FreeMove;
    show(m_rowsPivot, pivotMatters || wholeGroup);
    if (m_canvas) m_canvas->setTransformGlyph(m_transformGroup->isChecked() && m_transformGroup->isVisible(),
                                             type == MoveType::Rotate || type == MoveType::FreeMove);
}

void SketchPropertiesWidget::resetTransformForm()
{
    if (!m_canvas) return;
    m_updatingUi = true;
    m_txDx->setValue(0); m_txDy->setValue(0); m_rotAngle->setValue(0); m_scaleFactor->setValue(1.0);
    m_fmDx->setValue(0); m_fmDy->setValue(0); m_fmAngle->setValue(0);
    m_haveFrom = m_haveTo = m_havePicked = m_haveMirrorA = m_haveMirrorB = m_haveRef = false;
    for (auto* l : {m_fromLabel, m_toLabel, m_pickedLabel, m_mirrorALabel, m_mirrorBLabel, m_refLabel}) l->setText(tr("Not set"));
    m_targetMode->setCurrentIndex(0);
    m_deltaLabel->setText(QStringLiteral("—"));
    for (auto* b : {m_pickFrom, m_pickTo, m_pickPoint, m_pickMirrorA, m_pickMirrorB, m_pickRef, m_setPivotBtn}) b->setChecked(false);
    const QPointF pv = m_canvas->transformPivot();
    m_pivotX->setValue(pv.x()); m_pivotY->setValue(pv.y());
    const int gid = m_canvas->selectedWholeGroupId();
    m_pivotSourceLabel->setText(gid < 0 ? tr("transient (no group)")
                                : m_canvas->transformPivotStored() ? tr("stored on the group") : tr("geometric center"));
    m_transformStatus->clear();
    m_updatingUi = false;
    updateTransformRows();
    if (m_transformGroup->isChecked() && static_cast<MoveType>(m_moveType->currentIndex()) == MoveType::FreeMove)
        m_canvas->beginTransformPick(SketchCanvas::TransformPick::FreeMove);
}

void SketchPropertiesWidget::onTransformToggled(bool on)
{
    m_transformBody->setVisible(on);
    if (!on && m_canvas) { m_canvas->cancelTransformPick(); m_canvas->clearTransformPreview(); }
    updateTransformRows();
    if (on) resetTransformForm();
}

void SketchPropertiesWidget::onMoveTypeChanged(int)
{
    if (m_canvas) { m_canvas->cancelTransformPick(); m_canvas->clearTransformPreview(); }
    resetTransformForm();
}

bool SketchPropertiesWidget::currentTransformParams(sketch::GroupTransformParams& p, QString* whyNot) const
{
    const auto type = static_cast<MoveType>(m_moveType->currentIndex());
    const Point2D pivot{m_pivotX->value(), m_pivotY->value()};
    p = sketch::GroupTransformParams{};
    switch (type) {
    case MoveType::Translate:
        p.kind = sketch::GroupTransformKind::Translate; p.delta = {m_txDx->value(), m_txDy->value()}; break;
    case MoveType::Rotate:
        p.kind = sketch::GroupTransformKind::Rotate; p.angleDeg = m_rotAngle->value(); p.centerGiven = true; p.center = pivot; break;
    case MoveType::Scale:
        p.kind = sketch::GroupTransformKind::Scale; p.factor = m_scaleFactor->value(); p.centerGiven = true; p.center = pivot; break;
    case MoveType::Mirror:
        p.kind = sketch::GroupTransformKind::Mirror; p.centerGiven = true; p.center = pivot;
        if (m_mirrorAxis->currentIndex() == 2) {
            if (!(m_haveMirrorA && m_haveMirrorB)) { if (whyNot) *whyNot = tr("pick both points of the mirror line"); return false; }
            p.mirrorLineGiven = true; p.mirrorA = {m_mirrorAPt.x(), m_mirrorAPt.y()}; p.mirrorB = {m_mirrorBPt.x(), m_mirrorBPt.y()};
        } else {
            p.mirrorAcrossHorizontal = (m_mirrorAxis->currentIndex() == 0);
        }
        break;
    case MoveType::PointToPoint:
        if (!(m_haveFrom && m_haveTo)) { if (whyNot) *whyNot = tr("pick the from-point and the to-point"); return false; }
        p.kind = sketch::GroupTransformKind::Translate; p.delta = {m_toPt.x() - m_fromPt.x(), m_toPt.y() - m_fromPt.y()}; break;
    case MoveType::PointToPosition: {
        if (!m_havePicked) { if (whyNot) *whyNot = tr("pick the point on the selection that should land on the target"); return false; }
        QPointF target(m_targetX->value(), m_targetY->value());
        if (m_targetMode->currentIndex() == 1) {
            if (!m_haveRef) { if (whyNot) *whyNot = tr("pick the reference point the offset is measured from"); return false; }
            target += m_refPt;
        }
        p.kind = sketch::GroupTransformKind::Translate; p.delta = {target.x() - m_pickedPt.x(), target.y() - m_pickedPt.y()}; break;
    }
    case MoveType::FreeMove:
        p.kind = sketch::GroupTransformKind::Rotate; p.angleDeg = m_fmAngle->value(); p.centerGiven = true; p.center = pivot;
        p.delta = {m_fmDx->value(), m_fmDy->value()}; break;
    }
    return true;
}

void SketchPropertiesWidget::showTransformStatus(const QString& text, bool isError)
{
    m_transformStatus->setStyleSheet(isError ? "QLabel { color: #b00020; }" : "QLabel { color: #666; }");
    m_transformStatus->setText(text);
}

void SketchPropertiesWidget::refreshTransformPreview()
{
    if (!m_canvas || m_updatingUi || !m_transformGroup->isChecked()) return;
    sketch::GroupTransformParams p; QString why;
    if (!currentTransformParams(p, &why)) { m_canvas->clearTransformPreview(); showTransformStatus(why, false); return; }
    const auto res = m_canvas->previewTransform(p, m_createCopy->isChecked());
    if (!res.applied) { showTransformStatus(tr("Not possible: %1.").arg(QString::fromStdString(res.refusal)), true); return; }
    QStringList notes; for (const auto& n : res.notes) notes << QString::fromStdString(n);
    showTransformStatus(notes.isEmpty() ? tr("Preview shown; Apply to commit.") : notes.join(QLatin1Char('\n')), false);
}

void SketchPropertiesWidget::onTransformFieldChanged() { refreshTransformPreview(); }

void SketchPropertiesWidget::onPivotFieldChanged()
{
    if (m_updatingUi || !m_canvas) return;
    m_canvas->setTransformPivot(QPointF(m_pivotX->value(), m_pivotY->value()));
    refreshTransformPreview();
}

void SketchPropertiesWidget::onSetPivotClicked(bool checked)
{
    if (!m_canvas) return;
    if (checked) m_canvas->beginTransformPick(SketchCanvas::TransformPick::Pivot);
    else m_canvas->cancelTransformPick();
}

void SketchPropertiesWidget::onCenterPivotClicked()
{
    if (!m_canvas) return;
    const int gid = m_canvas->selectedWholeGroupId();
    if (gid >= 0 && !m_canvas->hasTransformPreview())
        m_canvas->setGroupPivot(gid, std::nullopt);      // standalone edit: its own undo step
    else
        m_canvas->resetTransformPivotToCenter();          // part of the transform being composed
    refreshTransformPreview();
}

void SketchPropertiesWidget::onPickButtonClicked(int pick)
{
    if (!m_canvas) return;
    for (auto* b : {m_pickFrom, m_pickTo, m_pickPoint, m_pickMirrorA, m_pickMirrorB, m_pickRef, m_setPivotBtn}) b->setChecked(false);
    switch (static_cast<SketchCanvas::TransformPick>(pick)) {
    case SketchCanvas::TransformPick::FromPoint: m_pickFrom->setChecked(true); break;
    case SketchCanvas::TransformPick::ToPoint: m_pickTo->setChecked(true); break;
    case SketchCanvas::TransformPick::PointOnSelection: m_pickPoint->setChecked(true); break;
    case SketchCanvas::TransformPick::MirrorA: m_pickMirrorA->setChecked(true); break;
    case SketchCanvas::TransformPick::MirrorB: m_pickMirrorB->setChecked(true); break;
    case SketchCanvas::TransformPick::ReferencePoint: m_pickRef->setChecked(true); break;
    default: break;
    }
    m_canvas->beginTransformPick(static_cast<SketchCanvas::TransformPick>(pick));
}

void SketchPropertiesWidget::onCanvasPivotChanged(const QPointF& world, bool stored)
{
    m_updatingUi = true;
    m_pivotX->setValue(world.x()); m_pivotY->setValue(world.y());
    const int gid = m_canvas ? m_canvas->selectedWholeGroupId() : -1;
    m_pivotSourceLabel->setText(gid < 0 ? tr("transient (no group)") : stored ? tr("stored on the group") : tr("geometric center (stored at Apply if you move it)"));
    m_updatingUi = false;
    if (m_canvas && m_canvas->hasTransformPreview()) refreshTransformPreview();
}

void SketchPropertiesWidget::onCanvasPickCompleted(int pick, const QPointF& world)
{
    using P = SketchCanvas::TransformPick;
    switch (static_cast<P>(pick)) {
    case P::FromPoint:
        m_fromPt = world; m_haveFrom = true; m_fromLabel->setText(pointText(world)); m_pickFrom->setChecked(false);
        if (!m_haveTo) { m_pickTo->setChecked(true); m_canvas->beginTransformPick(P::ToPoint); }
        break;
    case P::ToPoint:
        m_toPt = world; m_haveTo = true; m_toLabel->setText(pointText(world)); m_pickTo->setChecked(false); break;
    case P::PointOnSelection: {
        m_pickedPt = world; m_havePicked = true; m_pickPoint->setChecked(false);
        bool onSel = false;
        for (const SketchEntity* e : m_canvas->selectedEntities())
            for (const auto& q : e->points) if (QLineF(QPointF(q.x, q.y), world).length() < geometry::kDegenerateLen) onSel = true;
        m_pickedLabel->setText(pointText(world) + (onSel ? QString() : tr(" (not on selection)")));
        m_updatingUi = true; m_targetX->setValue(world.x()); m_targetY->setValue(world.y()); m_updatingUi = false;
        break;
    }
    case P::MirrorA:
        m_mirrorAPt = world; m_haveMirrorA = true; m_mirrorALabel->setText(pointText(world)); m_pickMirrorA->setChecked(false);
        if (!m_haveMirrorB) { m_pickMirrorB->setChecked(true); m_canvas->beginTransformPick(P::MirrorB); }
        break;
    case P::MirrorB:
        m_mirrorBPt = world; m_haveMirrorB = true; m_mirrorBLabel->setText(pointText(world)); m_pickMirrorB->setChecked(false); break;
    case P::ReferencePoint:
        m_refPt = world; m_haveRef = true; m_refLabel->setText(pointText(world)); m_pickRef->setChecked(false);
        if (m_havePicked) {   // offsets start at "where it is now", relative to the reference
            m_updatingUi = true;
            m_targetX->setValue(m_pickedPt.x() - world.x()); m_targetY->setValue(m_pickedPt.y() - world.y());
            m_updatingUi = false;
        }
        break;
    default: break;
    }
    if (m_haveFrom && m_haveTo) m_deltaLabel->setText(pointText(m_toPt - m_fromPt));
    refreshTransformPreview();
}

void SketchPropertiesWidget::onCanvasFreeMoveChanged(const QPointF& delta, double angleDeg)
{
    m_updatingUi = true;
    m_fmDx->setValue(delta.x()); m_fmDy->setValue(delta.y()); m_fmAngle->setValue(angleDeg);
    m_updatingUi = false;
    refreshTransformPreview();
}

void SketchPropertiesWidget::onCanvasTransformCanceled()
{
    for (auto* b : {m_pickFrom, m_pickTo, m_pickPoint, m_pickMirrorA, m_pickMirrorB, m_pickRef, m_setPivotBtn}) b->setChecked(false);
    m_updatingUi = true;
    m_fmDx->setValue(0); m_fmDy->setValue(0); m_fmAngle->setValue(0);
    m_updatingUi = false;
    showTransformStatus(tr("Canceled; selection kept."), false);
    if (m_canvas && m_transformGroup->isChecked() && static_cast<MoveType>(m_moveType->currentIndex()) == MoveType::FreeMove)
        m_canvas->beginTransformPick(SketchCanvas::TransformPick::FreeMove);
}

void SketchPropertiesWidget::onCanvasEntityModified(int)
{
    // Many modifications arrive per Apply; refresh the pivot rows once.
    if (m_entityModifiedQueued || !m_transformGroup || !m_transformGroup->isVisible()) return;
    m_entityModifiedQueued = true;
    QTimer::singleShot(0, this, [this] {
        m_entityModifiedQueued = false;
        if (!m_canvas) return;
        m_updatingUi = true;
        const QPointF pv = m_canvas->transformPivot();
        m_pivotX->setValue(pv.x()); m_pivotY->setValue(pv.y());
        m_updatingUi = false;
    });
}

void SketchPropertiesWidget::onTransformSectionRequested(int transformType)
{
    if (auto* dock = qobject_cast<QDockWidget*>(parentWidget())) { dock->show(); dock->raise(); }
    m_transformGroup->setVisible(true);
    m_transformGroup->setChecked(true);
    m_updatingUi = true;
    switch (static_cast<sketch::TransformType>(transformType)) {
    case sketch::TransformType::Move:   m_moveType->setCurrentIndex(int(MoveType::Translate)); m_createCopy->setChecked(false); break;
    case sketch::TransformType::Copy:   m_moveType->setCurrentIndex(int(MoveType::Translate)); m_createCopy->setChecked(true); break;
    case sketch::TransformType::Rotate: m_moveType->setCurrentIndex(int(MoveType::Rotate)); break;
    case sketch::TransformType::Scale:  m_moveType->setCurrentIndex(int(MoveType::Scale)); break;
    case sketch::TransformType::Mirror: m_moveType->setCurrentIndex(int(MoveType::Mirror)); break;
    }
    m_updatingUi = false;
    resetTransformForm();
    QWidget* first = nullptr;
    switch (static_cast<MoveType>(m_moveType->currentIndex())) {
    case MoveType::Translate: first = m_txDx; break;
    case MoveType::Rotate: first = m_rotAngle; break;
    case MoveType::Scale: first = m_scaleFactor; break;
    default: first = m_moveType; break;
    }
    if (first) { first->setFocus(); if (auto* sb = qobject_cast<QDoubleSpinBox*>(first)) sb->selectAll(); }
}

void SketchPropertiesWidget::applyTransform()
{
    if (!m_canvas || !m_transformGroup->isChecked() || !m_transformGroup->isVisible()) return;
    sketch::GroupTransformParams p; QString why;
    if (!currentTransformParams(p, &why)) { showTransformStatus(why, true); return; }
    const bool copy = m_createCopy->isChecked();
    const auto res = m_canvas->applyTransform(p, copy);
    if (!res.applied) { showTransformStatus(tr("Not applied: %1.").arg(QString::fromStdString(res.refusal)), true); return; }
    QStringList notes; for (const auto& n : res.notes) notes << QString::fromStdString(n);
    const auto type = static_cast<MoveType>(m_moveType->currentIndex());
    resetTransformForm();
    showTransformStatus(tr("Applied%1%2").arg(copy ? tr(" to a copy") : QString())
                            .arg(notes.isEmpty() ? QStringLiteral(".") : QStringLiteral(": ") + notes.join(QStringLiteral("; "))), false);
    if (type == MoveType::FreeMove) m_canvas->beginTransformPick(SketchCanvas::TransformPick::FreeMove);
}

}  // namespace hobbycad
