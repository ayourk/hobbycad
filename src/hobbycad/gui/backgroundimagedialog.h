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

    /// Tell the dialog where the project lives, so it can default the
    /// embed choice the way the rest of the app does.
    ///
    /// An image INSIDE the project is referenced by a relative path, which
    /// survives the project being moved; one outside has to be embedded,
    /// because an absolute path to someone else's home directory does not
    /// survive being handed to anyone. That is the rule
    /// `sketch::updateBackgroundFromFile()` applies, and this dialog must
    /// agree with it: offering the choice is fine, silently defaulting to
    /// the wrong side of it is not.
    ///
    /// Empty (the default) means the project has never been saved, so there
    /// is no directory to be inside of and embedding is the only safe answer.
    void setProjectDir(const QString& dir) { m_projectDir = dir; }

    /// Load a specific image, as if the user had picked it in Browse.
    ///
    /// Public so a caller can open the dialog already pointed at a file, and
    /// so the project-relative storage rule above is reachable without
    /// driving a native file dialog.
    void loadImage(const QString& filePath);

private slots:
    void browseForImage();
    void onOpacityChanged(int percent);
    void onEmbedChanged(bool embed);
    void updatePreview();
    void accept() override;

private:
    void setupUi();

    /// Rebuild m_background from m_sourcePath honoring the embed checkbox.
    ///
    /// Separate from loadImage() because toggling embed must not re-read and
    /// re-validate the file, and because both entry points have to apply the
    /// relative-path rule identically.
    void applyStorageChoice();

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

    /// Absolute path the user actually chose. m_background.filePath may hold
    /// a project-relative form, which cannot be re-opened without the project
    /// directory, so the source is kept separately for reloads.
    QString m_sourcePath;
    QString m_projectDir;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_BACKGROUNDIMAGEDIALOG_H
