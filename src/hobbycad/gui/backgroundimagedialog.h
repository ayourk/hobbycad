// =====================================================================
//  src/hobbycad/gui/backgroundimagedialog.h — Background image dialog
// =====================================================================
//
//  Dialog for selecting and configuring the initial background image
//  for a sketch. After initial setup, further changes are made via
//  the properties widget.
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_BACKGROUNDIMAGEDIALOG_H
#define HOBBYCAD_BACKGROUNDIMAGEDIALOG_H

#include <hobbycad/sketch/background.h>

#include <QDialog>

class QLabel;
class QSlider;
class QSpinBox;
class QCheckBox;
class QLineEdit;
class QPushButton;

namespace hobbycad {

/// Dialog for selecting an initial background image for a sketch
class BackgroundImageDialog : public QDialog {
    Q_OBJECT

public:
    explicit BackgroundImageDialog(QWidget* parent = nullptr);

    /// Get the configured background image (call after exec() returns Accepted)
    sketch::BackgroundImage backgroundImage() const { return m_background; }

    /// Set an existing background for editing (optional, for "change image" flow)
    void setBackgroundImage(const sketch::BackgroundImage& bg);

private slots:
    void browseForImage();
    void onOpacityChanged(int percent);
    void onEmbedChanged(bool embed);
    void updatePreview();
    void accept() override;

private:
    void setupUi();
    void loadImage(const QString& filePath);

    // UI elements
    QLineEdit* m_filePathEdit = nullptr;
    QPushButton* m_browseButton = nullptr;
    QLabel* m_previewLabel = nullptr;
    QSlider* m_opacitySlider = nullptr;
    QSpinBox* m_opacitySpinBox = nullptr;
    QCheckBox* m_embedCheckBox = nullptr;
    QLabel* m_imageSizeLabel = nullptr;

    // Background image data
    sketch::BackgroundImage m_background;
    QImage m_previewImage;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_BACKGROUNDIMAGEDIALOG_H
