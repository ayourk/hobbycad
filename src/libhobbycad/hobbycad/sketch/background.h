// =====================================================================
//  src/libhobbycad/hobbycad/sketch/background.h — Sketch background image
// =====================================================================
//
//  Support for background images in sketches. Useful for tracing
//  reference images, blueprints, or photographs.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_SKETCH_BACKGROUND_H
#define HOBBYCAD_SKETCH_BACKGROUND_H

#include "../core.h"
#include "../geometry/types.h"

#include <QString>
#include <QPointF>
#include <QImage>
#include <QByteArray>

namespace hobbycad {
namespace sketch {

// =====================================================================
//  Background Image Data
// =====================================================================

/// How the background image is stored
enum class BackgroundStorage {
    FilePath,       ///< Reference to external file
    Embedded        ///< Image data embedded in project
};

/// Background image for a sketch
struct BackgroundImage {
    bool enabled = false;              ///< Whether background is visible

    // Image source
    BackgroundStorage storage = BackgroundStorage::FilePath;
    QString filePath;                  ///< Path to image file (if FilePath storage)
    QByteArray imageData;              ///< Embedded image data (if Embedded storage)
    QString mimeType;                  ///< MIME type for embedded data (e.g., "image/png")

    // Position and size in sketch coordinates (mm)
    QPointF position = QPointF(0, 0);  ///< Top-left corner position
    double width = 100.0;              ///< Width in mm
    double height = 100.0;             ///< Height in mm
    double rotation = 0.0;             ///< Rotation in degrees (around center)

    // Display options
    double opacity = 0.5;              ///< Opacity (0.0 = transparent, 1.0 = opaque)
    bool lockAspectRatio = true;       ///< Maintain aspect ratio when resizing
    bool flipHorizontal = false;       ///< Mirror horizontally
    bool flipVertical = false;         ///< Mirror vertically
    bool grayscale = false;            ///< Convert to grayscale for tracing
    double contrast = 1.0;             ///< Contrast adjustment (1.0 = normal)
    double brightness = 0.0;           ///< Brightness adjustment (0.0 = normal)

    /// Get opacity as percentage (0-100)
    int opacityPercent() const { return static_cast<int>(opacity * 100.0 + 0.5); }

    /// Set opacity from percentage (0-100)
    void setOpacityPercent(int percent) { opacity = qBound(0, percent, 100) / 100.0; }

    /// Normalize rotation to 0-360 range
    void normalizeRotation() {
        rotation = fmod(rotation, 360.0);
        if (rotation < 0) rotation += 360.0;
    }

    /// Set rotation with automatic normalization to 0-360
    void setRotation(double degrees) {
        rotation = fmod(degrees, 360.0);
        if (rotation < 0) rotation += 360.0;
    }

    /// Get scale factor relative to original image dimensions
    /// Returns 1.0 if image dimensions match the loaded size at 96 DPI
    double getScaleFactor() const;

    /// Set size by scale factor (relative to original image at 96 DPI)
    /// Maintains aspect ratio
    void setScaleFactor(double scale);

    /// Original image dimensions in pixels (for scale calculations)
    int originalPixelWidth = 0;
    int originalPixelHeight = 0;

    // Calibration (for scaling from known dimensions)
    bool calibrated = false;           ///< Whether calibration has been set
    double calibrationScale = 1.0;     ///< Pixels per mm after calibration

    /// Get bounding box in sketch coordinates
    geometry::BoundingBox bounds() const;

    /// Get center point
    QPointF center() const;

    /// Check if a point is inside the background bounds
    bool containsPoint(const QPointF& point) const;
};

// =====================================================================
//  Background Image Functions
// =====================================================================

/// Load a background image from file
/// @param filePath Path to image file (PNG, JPG, BMP, etc.)
/// @param embed If true, embed image data; if false, store as file reference
/// @return Background image data (check enabled flag for success)
HOBBYCAD_EXPORT BackgroundImage loadBackgroundImage(
    const QString& filePath,
    bool embed = false);

/// Load a background image from raw data
/// @param data Raw image data
/// @param mimeType MIME type (e.g., "image/png", "image/jpeg")
/// @return Background image data
HOBBYCAD_EXPORT BackgroundImage loadBackgroundImageFromData(
    const QByteArray& data,
    const QString& mimeType);

/// Get the actual QImage for rendering
/// @param background Background image data
/// @return QImage for rendering (may be null if loading fails)
HOBBYCAD_EXPORT QImage getBackgroundQImage(const BackgroundImage& background);

/// Apply display adjustments (opacity, grayscale, contrast, brightness)
/// @param image Source image
/// @param background Background settings
/// @return Adjusted image
HOBBYCAD_EXPORT QImage applyBackgroundAdjustments(
    const QImage& image,
    const BackgroundImage& background);

/// Calculate image dimensions maintaining aspect ratio
/// @param originalWidth Original image width in pixels
/// @param originalHeight Original image height in pixels
/// @param targetWidth Desired width in mm
/// @param targetHeight Desired height in mm (output, adjusted for aspect ratio)
/// @param lockAspect If true, adjust height to maintain aspect ratio
HOBBYCAD_EXPORT void calculateAspectRatio(
    int originalWidth,
    int originalHeight,
    double targetWidth,
    double& targetHeight,
    bool lockAspect = true);

/// Calibrate background image scale from two known points
/// @param background Background to calibrate
/// @param point1 First point in image (pixels from top-left)
/// @param point2 Second point in image (pixels from top-left)
/// @param realDistance Real-world distance between points (mm)
/// @return Updated background with calibration applied
HOBBYCAD_EXPORT BackgroundImage calibrateBackground(
    const BackgroundImage& background,
    const QPointF& point1,
    const QPointF& point2,
    double realDistance);

/// Convert a point from sketch coordinates to image pixel coordinates
/// @param background Background image
/// @param sketchPoint Point in sketch coordinates (mm)
/// @return Point in image pixel coordinates
HOBBYCAD_EXPORT QPointF sketchToImageCoords(
    const BackgroundImage& background,
    const QPointF& sketchPoint);

/// Convert a point from image pixel coordinates to sketch coordinates
/// @param background Background image
/// @param imagePoint Point in image pixel coordinates
/// @return Point in sketch coordinates (mm)
HOBBYCAD_EXPORT QPointF imageToSketchCoords(
    const BackgroundImage& background,
    const QPointF& imagePoint);

// =====================================================================
//  Alignment Utilities
// =====================================================================

/// Calculate the angle of a line defined by two points
/// @param point1 First point
/// @param point2 Second point
/// @return Angle in degrees (-180 to 180, 0 = horizontal right)
HOBBYCAD_EXPORT double calculateLineAngle(const QPointF& point1, const QPointF& point2);

/// Calculate the rotation needed to align a line to a target angle
/// @param currentAngle Current angle of the line (degrees)
/// @param targetAngle Target angle to align to (degrees)
/// @return Rotation to apply (degrees, -180 to 180 for shortest path)
HOBBYCAD_EXPORT double calculateAlignmentRotation(double currentAngle, double targetAngle);

/// Normalize an angle to the range 0-360 degrees
/// @param degrees Angle in degrees (any value)
/// @return Normalized angle (0 to 360)
HOBBYCAD_EXPORT double normalizeAngle360(double degrees);

/// Normalize an angle to the range -180 to 180 degrees
/// @param degrees Angle in degrees (any value)
/// @return Normalized angle (-180 to 180)
HOBBYCAD_EXPORT double normalizeAngle180(double degrees);

// =====================================================================
//  Project Integration
// =====================================================================

/// Export background image to project directory
/// Copies or saves the image file to the project's backgrounds folder
/// @param background Background image to export
/// @param projectDir Project root directory
/// @param sketchName Name of the sketch (used for filename)
/// @return Updated background with new file path, or original if export failed
HOBBYCAD_EXPORT BackgroundImage exportBackgroundToProject(
    const BackgroundImage& background,
    const QString& projectDir,
    const QString& sketchName);

/// Check if a file path is inside the project directory
/// @param filePath Absolute file path to check
/// @param projectDir Project root directory
/// @return True if file is inside project directory
HOBBYCAD_EXPORT bool isFileInProject(
    const QString& filePath,
    const QString& projectDir);

/// Convert absolute path to relative path within project
/// @param absolutePath Absolute file path
/// @param projectDir Project root directory
/// @return Relative path, or original if not in project
HOBBYCAD_EXPORT QString toRelativePath(
    const QString& absolutePath,
    const QString& projectDir);

/// Convert relative path to absolute path within project
/// @param relativePath Relative file path
/// @param projectDir Project root directory
/// @return Absolute path
HOBBYCAD_EXPORT QString toAbsolutePath(
    const QString& relativePath,
    const QString& projectDir);

/// Update background image from a new file
/// If file is outside project, embeds the image data as base64
/// If file is inside project, stores as relative path reference
/// @param filePath Path to new image file
/// @param projectDir Project root directory (empty if project not saved yet)
/// @return Updated background image
HOBBYCAD_EXPORT BackgroundImage updateBackgroundFromFile(
    const QString& filePath,
    const QString& projectDir);

// =====================================================================
//  Serialization
// =====================================================================

/// Serialize background image to JSON
/// @param background Background to serialize
/// @param includeImageData If true, include embedded image data
/// @return JSON object as string
HOBBYCAD_EXPORT QString backgroundToJson(
    const BackgroundImage& background,
    bool includeImageData = true);

/// Deserialize background image from JSON
/// @param json JSON string
/// @return Background image data
HOBBYCAD_EXPORT BackgroundImage backgroundFromJson(const QString& json);

// =====================================================================
//  Supported Formats
// =====================================================================

/// Get list of supported image format extensions
/// @return List of extensions (e.g., "png", "jpg", "bmp")
HOBBYCAD_EXPORT QStringList supportedImageFormats();

/// Get file filter string for open dialogs
/// @return Filter string (e.g., "Images (*.png *.jpg *.bmp)")
HOBBYCAD_EXPORT QString imageFileFilter();

/// Check if a file format is supported
/// @param filePath Path to check
/// @return True if format is supported
HOBBYCAD_EXPORT bool isImageFormatSupported(const QString& filePath);

}  // namespace sketch
}  // namespace hobbycad

#endif  // HOBBYCAD_SKETCH_BACKGROUND_H
