// =====================================================================
//  src/hobbycad/gui/backgroundimagedialog.cpp — Background image dialog
// =====================================================================
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include "backgroundimagedialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QSlider>
#include <QSpinBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>
#include <QFileDialog>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QMessageBox>
#include <QImageReader>

namespace hobbycad {

BackgroundImageDialog::BackgroundImageDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Add Background Image"));
    setMinimumWidth(450);
    setupUi();
}

void BackgroundImageDialog::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);

    // File selection group
    auto* fileGroup = new QGroupBox(tr("Image File"));
    auto* fileLayout = new QHBoxLayout(fileGroup);

    m_filePathEdit = new QLineEdit;
    m_filePathEdit->setReadOnly(true);
    m_filePathEdit->setPlaceholderText(tr("No image selected..."));
    fileLayout->addWidget(m_filePathEdit);

    m_browseButton = new QPushButton(tr("Browse..."));
    connect(m_browseButton, &QPushButton::clicked, this, &BackgroundImageDialog::browseForImage);
    fileLayout->addWidget(m_browseButton);

    mainLayout->addWidget(fileGroup);

    // Preview group
    auto* previewGroup = new QGroupBox(tr("Preview"));
    auto* previewLayout = new QVBoxLayout(previewGroup);

    m_previewLabel = new QLabel;
    m_previewLabel->setMinimumSize(200, 150);
    m_previewLabel->setMaximumHeight(200);
    m_previewLabel->setAlignment(Qt::AlignCenter);
    m_previewLabel->setStyleSheet("QLabel { background-color: #f0f0f0; border: 1px solid #ccc; }");
    m_previewLabel->setText(tr("No image"));
    previewLayout->addWidget(m_previewLabel);

    m_imageSizeLabel = new QLabel;
    m_imageSizeLabel->setAlignment(Qt::AlignCenter);
    previewLayout->addWidget(m_imageSizeLabel);

    mainLayout->addWidget(previewGroup);

    // Settings group
    auto* settingsGroup = new QGroupBox(tr("Settings"));
    auto* settingsLayout = new QFormLayout(settingsGroup);

    // Opacity slider with spinbox
    auto* opacityLayout = new QHBoxLayout;

    m_opacitySlider = new QSlider(Qt::Horizontal);
    m_opacitySlider->setRange(0, 100);
    m_opacitySlider->setValue(50);
    m_opacitySlider->setTickPosition(QSlider::TicksBelow);
    m_opacitySlider->setTickInterval(10);
    opacityLayout->addWidget(m_opacitySlider, 1);

    m_opacitySpinBox = new QSpinBox;
    m_opacitySpinBox->setRange(0, 100);
    m_opacitySpinBox->setValue(50);
    m_opacitySpinBox->setSuffix(tr("%"));
    m_opacitySpinBox->setFixedWidth(70);
    opacityLayout->addWidget(m_opacitySpinBox);

    // Synchronize slider and spinbox
    connect(m_opacitySlider, &QSlider::valueChanged, this, [this](int value) {
        m_opacitySpinBox->blockSignals(true);
        m_opacitySpinBox->setValue(value);
        m_opacitySpinBox->blockSignals(false);
        onOpacityChanged(value);
    });
    connect(m_opacitySpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        m_opacitySlider->blockSignals(true);
        m_opacitySlider->setValue(value);
        m_opacitySlider->blockSignals(false);
        onOpacityChanged(value);
    });

    settingsLayout->addRow(tr("Opacity:"), opacityLayout);

    // Embed checkbox
    m_embedCheckBox = new QCheckBox(tr("Embed image in project"));
    m_embedCheckBox->setToolTip(tr("If checked, the image data is stored in the project file.\n"
                                   "If unchecked, only the file path is stored."));
    connect(m_embedCheckBox, &QCheckBox::toggled, this, &BackgroundImageDialog::onEmbedChanged);
    settingsLayout->addRow(QString(), m_embedCheckBox);

    mainLayout->addWidget(settingsGroup);

    // Dialog buttons
    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &BackgroundImageDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);

    // Initialize background with default opacity
    m_background.setOpacityPercent(50);
}

void BackgroundImageDialog::browseForImage()
{
    QString filter = sketch::imageFileFilter();
    QString filePath = QFileDialog::getOpenFileName(
        this,
        tr("Select Background Image"),
        QString(),
        filter
    );

    if (!filePath.isEmpty()) {
        loadImage(filePath);
    }
}

void BackgroundImageDialog::loadImage(const QString& filePath)
{
    // Load the image to verify it's valid
    QImageReader reader(filePath);
    if (!reader.canRead()) {
        QMessageBox::warning(this, tr("Invalid Image"),
            tr("The selected file could not be read as an image."));
        return;
    }

    m_previewImage = reader.read();
    if (m_previewImage.isNull()) {
        QMessageBox::warning(this, tr("Invalid Image"),
            tr("Failed to load the image: %1").arg(reader.errorString()));
        return;
    }

    // Load background using library function
    m_background = sketch::loadBackgroundImage(filePath, m_embedCheckBox->isChecked());

    if (!m_background.enabled) {
        QMessageBox::warning(this, tr("Load Failed"),
            tr("Failed to load the background image."));
        return;
    }

    // Apply current opacity setting
    m_background.setOpacityPercent(m_opacitySlider->value());

    // Update UI
    m_filePathEdit->setText(filePath);
    m_imageSizeLabel->setText(tr("%1 x %2 pixels").arg(m_previewImage.width()).arg(m_previewImage.height()));

    updatePreview();
}

void BackgroundImageDialog::onOpacityChanged(int percent)
{
    m_background.setOpacityPercent(percent);
    updatePreview();
}

void BackgroundImageDialog::onEmbedChanged(bool embed)
{
    if (!m_background.filePath.isEmpty()) {
        // Reload with new embed setting
        QString filePath = m_background.filePath;
        m_background = sketch::loadBackgroundImage(filePath, embed);
        m_background.setOpacityPercent(m_opacitySlider->value());
    }
}

void BackgroundImageDialog::updatePreview()
{
    if (m_previewImage.isNull()) {
        m_previewLabel->setText(tr("No image"));
        return;
    }

    // Apply opacity for preview
    QImage previewWithOpacity = m_previewImage.convertToFormat(QImage::Format_ARGB32);
    int alpha = static_cast<int>(m_background.opacity * 255);

    for (int y = 0; y < previewWithOpacity.height(); ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(previewWithOpacity.scanLine(y));
        for (int x = 0; x < previewWithOpacity.width(); ++x) {
            int a = (qAlpha(line[x]) * alpha) / 255;
            line[x] = qRgba(qRed(line[x]), qGreen(line[x]), qBlue(line[x]), a);
        }
    }

    // Scale to fit preview area
    QPixmap pixmap = QPixmap::fromImage(previewWithOpacity);
    pixmap = pixmap.scaled(m_previewLabel->size() - QSize(4, 4),
                           Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_previewLabel->setPixmap(pixmap);
}

void BackgroundImageDialog::setBackgroundImage(const sketch::BackgroundImage& bg)
{
    m_background = bg;

    if (bg.enabled) {
        m_filePathEdit->setText(bg.filePath);
        m_opacitySlider->setValue(bg.opacityPercent());
        m_opacitySpinBox->setValue(bg.opacityPercent());
        m_embedCheckBox->setChecked(bg.storage == sketch::BackgroundStorage::Embedded);

        // Load preview image
        m_previewImage = sketch::getBackgroundQImage(bg);
        if (!m_previewImage.isNull()) {
            m_imageSizeLabel->setText(tr("%1 x %2 pixels")
                .arg(m_previewImage.width()).arg(m_previewImage.height()));
            updatePreview();
        }
    }
}

void BackgroundImageDialog::accept()
{
    if (!m_background.enabled || m_background.filePath.isEmpty()) {
        QMessageBox::warning(this, tr("No Image Selected"),
            tr("Please select a background image before continuing."));
        return;
    }

    QDialog::accept();
}

}  // namespace hobbycad
