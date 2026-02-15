// =====================================================================
//  src/hobbycad/gui/sketchpropertieswidget.cpp — Sketch properties widget
// =====================================================================
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include "sketchpropertieswidget.h"
#include "sketchcanvas.h"

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
#include <QFileDialog>
#include <QMessageBox>

namespace hobbycad {

SketchPropertiesWidget::SketchPropertiesWidget(QWidget* parent)
    : QWidget(parent)
{
    setupUi();
}

void SketchPropertiesWidget::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(8);

    // Wrap in scroll area for small screens
    auto* scrollArea = new QScrollArea;
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto* contentWidget = new QWidget;
    auto* contentLayout = new QVBoxLayout(contentWidget);
    contentLayout->setContentsMargins(0, 0, 0, 0);

    setupBackgroundSection();
    contentLayout->addWidget(m_backgroundGroup);

    setupEntitySection();
    contentLayout->addWidget(m_entityGroup);

    contentLayout->addStretch();

    scrollArea->setWidget(contentWidget);
    mainLayout->addWidget(scrollArea);
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

    // Action buttons row 1
    auto* buttonLayout1 = new QHBoxLayout;

    m_bgEditPositionButton = new QPushButton(tr("Edit Position"));
    m_bgEditPositionButton->setCheckable(true);
    m_bgEditPositionButton->setToolTip(tr("Enable interactive repositioning and resizing of the background image"));
    connect(m_bgEditPositionButton, &QPushButton::toggled, this, &SketchPropertiesWidget::onEditPositionToggled);
    buttonLayout1->addWidget(m_bgEditPositionButton);

    m_bgRemoveButton = new QPushButton(tr("Remove"));
    m_bgRemoveButton->setToolTip(tr("Remove the background image"));
    connect(m_bgRemoveButton, &QPushButton::clicked, this, &SketchPropertiesWidget::removeBackgroundImageRequested);
    buttonLayout1->addWidget(m_bgRemoveButton);

    layout->addLayout(buttonLayout1);

    // Action buttons row 2
    auto* buttonLayout2 = new QHBoxLayout;

    m_bgCalibrateButton = new QPushButton(tr("Calibrate Scale"));
    m_bgCalibrateButton->setToolTip(tr("Set the scale by picking two points with a known distance"));
    connect(m_bgCalibrateButton, &QPushButton::clicked, this, &SketchPropertiesWidget::calibrateBackgroundRequested);
    buttonLayout2->addWidget(m_bgCalibrateButton);

    m_bgExportButton = new QPushButton(tr("Export to Project"));
    m_bgExportButton->setToolTip(tr("Save the image file to the project directory"));
    connect(m_bgExportButton, &QPushButton::clicked, this, &SketchPropertiesWidget::onExportToProject);
    buttonLayout2->addWidget(m_bgExportButton);

    layout->addLayout(buttonLayout2);

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

    auto* posLayout = new QHBoxLayout;
    m_bgPositionX = new QDoubleSpinBox;
    m_bgPositionX->setRange(-10000, 10000);
    m_bgPositionX->setDecimals(2);
    m_bgPositionX->setSuffix(tr(" mm"));
    connect(m_bgPositionX, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &SketchPropertiesWidget::onPositionXChanged);
    posLayout->addWidget(new QLabel(tr("X:")));
    posLayout->addWidget(m_bgPositionX);

    m_bgPositionY = new QDoubleSpinBox;
    m_bgPositionY->setRange(-10000, 10000);
    m_bgPositionY->setDecimals(2);
    m_bgPositionY->setSuffix(tr(" mm"));
    connect(m_bgPositionY, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &SketchPropertiesWidget::onPositionYChanged);
    posLayout->addWidget(new QLabel(tr("Y:")));
    posLayout->addWidget(m_bgPositionY);
    positionForm->addRow(tr("Position:"), posLayout);

    auto* sizeLayout = new QHBoxLayout;
    m_bgWidth = new QDoubleSpinBox;
    m_bgWidth->setRange(0.1, 10000);
    m_bgWidth->setDecimals(2);
    m_bgWidth->setSuffix(tr(" mm"));
    connect(m_bgWidth, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &SketchPropertiesWidget::onWidthChanged);
    sizeLayout->addWidget(new QLabel(tr("W:")));
    sizeLayout->addWidget(m_bgWidth);

    m_bgHeight = new QDoubleSpinBox;
    m_bgHeight->setRange(0.1, 10000);
    m_bgHeight->setDecimals(2);
    m_bgHeight->setSuffix(tr(" mm"));
    connect(m_bgHeight, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &SketchPropertiesWidget::onHeightChanged);
    sizeLayout->addWidget(new QLabel(tr("H:")));
    sizeLayout->addWidget(m_bgHeight);
    positionForm->addRow(tr("Size:"), sizeLayout);

    m_bgRotation = new QDoubleSpinBox;
    m_bgRotation->setRange(-360, 360);
    m_bgRotation->setDecimals(1);
    m_bgRotation->setSuffix(tr("\u00B0"));
    m_bgRotation->setToolTip(tr("Rotation angle (will be normalized to 0-360°)"));
    connect(m_bgRotation, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &SketchPropertiesWidget::onRotationChanged);
    positionForm->addRow(tr("Rotation:"), m_bgRotation);

    m_bgScaleFactor = new QDoubleSpinBox;
    m_bgScaleFactor->setRange(0.01, 100.0);
    m_bgScaleFactor->setDecimals(2);
    m_bgScaleFactor->setSingleStep(0.1);
    m_bgScaleFactor->setValue(1.0);
    m_bgScaleFactor->setToolTip(tr("Scale factor relative to original image size at 96 DPI (1.0 = 100%)"));
    connect(m_bgScaleFactor, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &SketchPropertiesWidget::onScaleFactorChanged);
    positionForm->addRow(tr("Scale:"), m_bgScaleFactor);

    layout->addLayout(positionForm);

    // Lock aspect ratio
    m_bgLockAspect = new QCheckBox(tr("Lock aspect ratio"));
    m_bgLockAspect->setChecked(true);
    connect(m_bgLockAspect, &QCheckBox::toggled, this, &SketchPropertiesWidget::onLockAspectChanged);
    layout->addWidget(m_bgLockAspect);

    // Image adjustments
    auto* adjustForm = new QFormLayout;
    adjustForm->setSpacing(4);

    m_bgGrayscale = new QCheckBox(tr("Grayscale"));
    m_bgGrayscale->setToolTip(tr("Convert image to grayscale for easier tracing"));
    connect(m_bgGrayscale, &QCheckBox::toggled, this, &SketchPropertiesWidget::onGrayscaleChanged);
    adjustForm->addRow(QString(), m_bgGrayscale);

    m_bgContrast = new QDoubleSpinBox;
    m_bgContrast->setRange(0.1, 3.0);
    m_bgContrast->setDecimals(2);
    m_bgContrast->setSingleStep(0.1);
    m_bgContrast->setValue(1.0);
    connect(m_bgContrast, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &SketchPropertiesWidget::onContrastChanged);
    adjustForm->addRow(tr("Contrast:"), m_bgContrast);

    m_bgBrightness = new QDoubleSpinBox;
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
    m_bgFlipHButton->setFixedWidth(32);
    m_bgFlipHButton->setToolTip(tr("Flip horizontally (mirror)"));
    connect(m_bgFlipHButton, &QPushButton::clicked, this, &SketchPropertiesWidget::onFlipHorizontal);
    flipRotateLayout->addWidget(m_bgFlipHButton);

    m_bgFlipVButton = new QPushButton(tr("↕"));
    m_bgFlipVButton->setFixedWidth(32);
    m_bgFlipVButton->setToolTip(tr("Flip vertically"));
    connect(m_bgFlipVButton, &QPushButton::clicked, this, &SketchPropertiesWidget::onFlipVertical);
    flipRotateLayout->addWidget(m_bgFlipVButton);

    flipRotateLayout->addSpacing(8);

    m_bgRotateCCWButton = new QPushButton(tr("↺"));
    m_bgRotateCCWButton->setFixedWidth(32);
    m_bgRotateCCWButton->setToolTip(tr("Rotate 90° counter-clockwise"));
    connect(m_bgRotateCCWButton, &QPushButton::clicked, this, &SketchPropertiesWidget::onRotate90CCW);
    flipRotateLayout->addWidget(m_bgRotateCCWButton);

    m_bgRotateCWButton = new QPushButton(tr("↻"));
    m_bgRotateCWButton->setFixedWidth(32);
    m_bgRotateCWButton->setToolTip(tr("Rotate 90° clockwise"));
    connect(m_bgRotateCWButton, &QPushButton::clicked, this, &SketchPropertiesWidget::onRotate90CW);
    flipRotateLayout->addWidget(m_bgRotateCWButton);

    m_bgRotate180Button = new QPushButton(tr("180°"));
    m_bgRotate180Button->setFixedWidth(40);
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

    // TODO: Add pages for different entity types (line, circle, arc, etc.)
    // Each page would show editable properties for that entity type

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
    if (hasImage && !m_background.filePath.isEmpty()) {
        m_bgFilePathEdit->setText(m_background.filePath);
        m_bgFilePathEdit->setToolTip(m_background.filePath);

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
        m_bgPositionX->setValue(m_background.position.x());
        m_bgPositionY->setValue(m_background.position.y());
        m_bgWidth->setValue(m_background.width);
        m_bgHeight->setValue(m_background.height);
        m_bgRotation->setValue(m_background.rotation);
        m_bgScaleFactor->setValue(m_background.getScaleFactor());
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

    auto selected = m_canvas->selectedEntities();
    if (selected.isEmpty()) {
        m_entityStack->setCurrentIndex(0);  // No selection page
    } else {
        // TODO: Show entity-specific properties
        m_entityStack->setCurrentIndex(0);
    }
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
    m_background.position.setX(x);
    emitBackgroundChanged();
}

void SketchPropertiesWidget::onPositionYChanged(double y)
{
    if (m_updatingUi) return;
    m_background.position.setY(y);
    emitBackgroundChanged();
}

void SketchPropertiesWidget::onWidthChanged(double w)
{
    if (m_updatingUi) return;

    if (m_background.lockAspectRatio && m_background.width > 0) {
        double aspectRatio = m_background.height / m_background.width;
        m_background.height = w * aspectRatio;

        m_updatingUi = true;
        m_bgHeight->setValue(m_background.height);
        m_updatingUi = false;
    }

    m_background.width = w;

    // Update scale factor display
    m_updatingUi = true;
    m_bgScaleFactor->setValue(m_background.getScaleFactor());
    m_updatingUi = false;

    emitBackgroundChanged();
}

void SketchPropertiesWidget::onHeightChanged(double h)
{
    if (m_updatingUi) return;

    if (m_background.lockAspectRatio && m_background.height > 0) {
        double aspectRatio = m_background.width / m_background.height;
        m_background.width = h * aspectRatio;

        m_updatingUi = true;
        m_bgWidth->setValue(m_background.width);
        m_updatingUi = false;
    }

    m_background.height = h;

    // Update scale factor display
    m_updatingUi = true;
    m_bgScaleFactor->setValue(m_background.getScaleFactor());
    m_updatingUi = false;

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

void SketchPropertiesWidget::onScaleFactorChanged(double scale)
{
    if (m_updatingUi) return;

    m_background.setScaleFactor(scale);

    // Update width/height spinboxes to reflect new size
    m_updatingUi = true;
    m_bgWidth->setValue(m_background.width);
    m_bgHeight->setValue(m_background.height);
    m_updatingUi = false;

    emitBackgroundChanged();
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
    QString filter = sketch::imageFileFilter();
    QString filePath = QFileDialog::getOpenFileName(
        this,
        tr("Select Background Image"),
        QString(),
        filter
    );

    if (filePath.isEmpty()) {
        return;
    }

    // Preserve current position/size/opacity settings if we already have an image
    QPointF oldPosition = m_background.position;
    double oldWidth = m_background.width;
    double oldHeight = m_background.height;
    double oldOpacity = m_background.opacity;
    double oldRotation = m_background.rotation;
    bool hadImage = m_background.enabled;

    // Load new image using project-aware function
    m_background = sketch::updateBackgroundFromFile(filePath, m_projectDir);

    if (!m_background.enabled) {
        QMessageBox::warning(this, tr("Load Failed"),
            tr("Failed to load the image file."));
        return;
    }

    // Restore position/opacity if we had an image before
    if (hadImage) {
        m_background.position = oldPosition;
        m_background.opacity = oldOpacity;
        m_background.rotation = oldRotation;
        // Optionally restore size (uncomment if desired)
        // m_background.width = oldWidth;
        // m_background.height = oldHeight;
    }

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
        m_background, m_projectDir, sketchName);

    if (exported.storage == sketch::BackgroundStorage::FilePath &&
        !exported.filePath.isEmpty()) {
        m_background = exported;
        updateBackgroundUi();
        emitBackgroundChanged();

        QMessageBox::information(this, tr("Export Complete"),
            tr("Background image exported to:\n%1").arg(exported.filePath));
    } else {
        QMessageBox::warning(this, tr("Export Failed"),
            tr("Failed to export the background image to the project directory."));
    }
}

}  // namespace hobbycad
