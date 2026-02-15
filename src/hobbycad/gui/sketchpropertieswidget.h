// =====================================================================
//  src/hobbycad/gui/sketchpropertieswidget.h — Sketch properties widget
// =====================================================================
//
//  Dockable widget for viewing and editing sketch properties including:
//  - Background image settings (opacity, position, size)
//  - Grid settings
//  - Selected entity properties
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_SKETCHPROPERTIESWIDGET_H
#define HOBBYCAD_SKETCHPROPERTIESWIDGET_H

#include <hobbycad/sketch/background.h>

#include <QWidget>

class QLabel;
class QLineEdit;
class QSlider;
class QSpinBox;
class QDoubleSpinBox;
class QCheckBox;
class QPushButton;
class QGroupBox;
class QStackedWidget;

namespace hobbycad {

class SketchCanvas;

/// Widget for displaying and editing sketch properties
class SketchPropertiesWidget : public QWidget {
    Q_OBJECT

public:
    explicit SketchPropertiesWidget(QWidget* parent = nullptr);

    /// Set the sketch canvas to monitor/edit
    void setSketchCanvas(SketchCanvas* canvas);

    /// Set the current background image for editing
    void setBackgroundImage(const sketch::BackgroundImage& bg);

    /// Get the current background image settings
    sketch::BackgroundImage backgroundImage() const { return m_background; }

    /// Set the project directory (for relative path handling)
    void setProjectDirectory(const QString& projectDir) { m_projectDir = projectDir; }

signals:
    /// Emitted when background image settings change
    void backgroundImageChanged(const sketch::BackgroundImage& bg);

    /// Emitted when user requests to add/change background image
    void addBackgroundImageRequested();

    /// Emitted when user requests to remove background image
    void removeBackgroundImageRequested();

    /// Emitted when user toggles background edit mode
    void backgroundEditModeRequested(bool enabled);

    /// Emitted when user requests background scale calibration
    void calibrateBackgroundRequested();

public slots:
    /// Update display when selection changes
    void updateForSelection();

    /// Update the Edit Position button state
    void setBackgroundEditMode(bool enabled);

private slots:
    void onOpacityChanged(int percent);
    void onPositionXChanged(double x);
    void onPositionYChanged(double y);
    void onWidthChanged(double w);
    void onHeightChanged(double h);
    void onRotationChanged(double deg);
    void onScaleFactorChanged(double scale);
    void onLockAspectChanged(bool locked);
    void onGrayscaleChanged(bool grayscale);
    void onContrastChanged(double contrast);
    void onBrightnessChanged(double brightness);
    void onFlipHorizontal();
    void onFlipVertical();
    void onRotate90CW();
    void onRotate90CCW();
    void onRotate180();
    void onBrowseForImage();
    void onExportToProject();
    void onEditPositionToggled(bool checked);

private:
    void setupUi();
    void setupBackgroundSection();
    void setupEntitySection();
    void updateBackgroundUi();
    void emitBackgroundChanged();

    // Reference to canvas
    SketchCanvas* m_canvas = nullptr;

    // Background image data
    sketch::BackgroundImage m_background;
    QString m_projectDir;       // Project root directory
    bool m_updatingUi = false;  // Prevent feedback loops

    // UI elements - Background section
    QGroupBox* m_backgroundGroup = nullptr;
    QLabel* m_bgFilePathLabel = nullptr;
    QLineEdit* m_bgFilePathEdit = nullptr;
    QPushButton* m_bgBrowseButton = nullptr;
    QPushButton* m_bgExportButton = nullptr;
    QPushButton* m_bgRemoveButton = nullptr;
    QPushButton* m_bgEditPositionButton = nullptr;
    QPushButton* m_bgCalibrateButton = nullptr;

    QSlider* m_bgOpacitySlider = nullptr;
    QSpinBox* m_bgOpacitySpinBox = nullptr;

    QDoubleSpinBox* m_bgPositionX = nullptr;
    QDoubleSpinBox* m_bgPositionY = nullptr;
    QDoubleSpinBox* m_bgWidth = nullptr;
    QDoubleSpinBox* m_bgHeight = nullptr;
    QDoubleSpinBox* m_bgRotation = nullptr;
    QDoubleSpinBox* m_bgScaleFactor = nullptr;
    QCheckBox* m_bgLockAspect = nullptr;

    QCheckBox* m_bgGrayscale = nullptr;
    QDoubleSpinBox* m_bgContrast = nullptr;
    QDoubleSpinBox* m_bgBrightness = nullptr;

    // Flip/rotate buttons
    QPushButton* m_bgFlipHButton = nullptr;
    QPushButton* m_bgFlipVButton = nullptr;
    QPushButton* m_bgRotateCWButton = nullptr;
    QPushButton* m_bgRotateCCWButton = nullptr;
    QPushButton* m_bgRotate180Button = nullptr;

    // UI elements - Entity section
    QGroupBox* m_entityGroup = nullptr;
    QStackedWidget* m_entityStack = nullptr;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_SKETCHPROPERTIESWIDGET_H
