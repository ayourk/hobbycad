// =====================================================================
//  src/libhobbycad/sketch/background.cpp — Sketch background image
// =====================================================================
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include <hobbycad/sketch/background.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QBuffer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QtMath>

namespace hobbycad {
namespace sketch {

// =====================================================================
//  BackgroundImage Methods
// =====================================================================

geometry::BoundingBox BackgroundImage::bounds() const
{
    if (!enabled) {
        return geometry::BoundingBox();
    }

    // Simple bounds without rotation
    // TODO: Handle rotation properly
    return geometry::BoundingBox(
        position.x(),
        position.y(),
        position.x() + width,
        position.y() + height
    );
}

QPointF BackgroundImage::center() const
{
    return QPointF(position.x() + width / 2.0,
                   position.y() + height / 2.0);
}

bool BackgroundImage::containsPoint(const QPointF& point) const
{
    if (!enabled) return false;

    // Simple check without rotation
    // TODO: Handle rotation properly
    return point.x() >= position.x() &&
           point.x() <= position.x() + width &&
           point.y() >= position.y() &&
           point.y() <= position.y() + height;
}

double BackgroundImage::getScaleFactor() const
{
    if (originalPixelWidth <= 0 || originalPixelHeight <= 0) {
        return 1.0;
    }

    // Calculate what the "natural" size would be at 96 DPI
    // 96 pixels = 1 inch = 25.4 mm
    const double pixelsPerMm = 96.0 / 25.4;
    double naturalWidth = originalPixelWidth / pixelsPerMm;

    // Scale factor is current width divided by natural width
    if (naturalWidth > 0) {
        return width / naturalWidth;
    }
    return 1.0;
}

void BackgroundImage::setScaleFactor(double scale)
{
    if (originalPixelWidth <= 0 || originalPixelHeight <= 0 || scale <= 0) {
        return;
    }

    // Calculate the "natural" size at 96 DPI
    const double pixelsPerMm = 96.0 / 25.4;
    double naturalWidth = originalPixelWidth / pixelsPerMm;
    double naturalHeight = originalPixelHeight / pixelsPerMm;

    // Apply scale factor
    width = naturalWidth * scale;
    height = naturalHeight * scale;
}

// =====================================================================
//  Background Image Loading
// =====================================================================

BackgroundImage loadBackgroundImage(const QString& filePath, bool embed)
{
    BackgroundImage bg;
    bg.enabled = false;

    if (!QFile::exists(filePath)) {
        return bg;
    }

    QImageReader reader(filePath);
    if (!reader.canRead()) {
        return bg;
    }

    QSize size = reader.size();
    if (size.isEmpty()) {
        // Try reading the image to get size
        QImage img = reader.read();
        if (img.isNull()) {
            return bg;
        }
        size = img.size();
    }

    bg.enabled = true;
    bg.filePath = filePath;

    // Store original pixel dimensions for scale factor calculations
    bg.originalPixelWidth = size.width();
    bg.originalPixelHeight = size.height();

    // Determine MIME type from extension
    QFileInfo fileInfo(filePath);
    QString ext = fileInfo.suffix().toLower();
    if (ext == "png") {
        bg.mimeType = QStringLiteral("image/png");
    } else if (ext == "jpg" || ext == "jpeg") {
        bg.mimeType = QStringLiteral("image/jpeg");
    } else if (ext == "bmp") {
        bg.mimeType = QStringLiteral("image/bmp");
    } else if (ext == "gif") {
        bg.mimeType = QStringLiteral("image/gif");
    } else if (ext == "webp") {
        bg.mimeType = QStringLiteral("image/webp");
    } else {
        bg.mimeType = QStringLiteral("image/png");
    }

    // Set default size based on image dimensions (assume 96 DPI for initial sizing)
    // 1 inch = 25.4 mm, 96 pixels = 1 inch
    const double pixelsPerMm = 96.0 / 25.4;
    bg.width = size.width() / pixelsPerMm;
    bg.height = size.height() / pixelsPerMm;

    if (embed) {
        // Read and embed the image data
        QFile file(filePath);
        if (file.open(QIODevice::ReadOnly)) {
            bg.storage = BackgroundStorage::Embedded;
            bg.imageData = file.readAll();
        }
    } else {
        bg.storage = BackgroundStorage::FilePath;
    }

    return bg;
}

BackgroundImage loadBackgroundImageFromData(
    const QByteArray& data,
    const QString& mimeType)
{
    BackgroundImage bg;
    bg.enabled = false;

    if (data.isEmpty()) {
        return bg;
    }

    // Load image to get dimensions
    QImage image;
    if (!image.loadFromData(data)) {
        return bg;
    }

    bg.enabled = true;
    bg.storage = BackgroundStorage::Embedded;
    bg.imageData = data;
    bg.mimeType = mimeType;

    // Store original pixel dimensions for scale factor calculations
    bg.originalPixelWidth = image.width();
    bg.originalPixelHeight = image.height();

    // Set default size (assume 96 DPI)
    const double pixelsPerMm = 96.0 / 25.4;
    bg.width = image.width() / pixelsPerMm;
    bg.height = image.height() / pixelsPerMm;

    return bg;
}

// =====================================================================
//  Image Retrieval
// =====================================================================

QImage getBackgroundQImage(const BackgroundImage& background)
{
    if (!background.enabled) {
        return QImage();
    }

    QImage image;

    if (background.storage == BackgroundStorage::Embedded) {
        image.loadFromData(background.imageData);
    } else {
        image.load(background.filePath);
    }

    return image;
}

QImage applyBackgroundAdjustments(
    const QImage& image,
    const BackgroundImage& background)
{
    if (image.isNull()) {
        return image;
    }

    QImage result = image.convertToFormat(QImage::Format_ARGB32);

    // Apply flip/mirror transformations
    if (background.flipHorizontal || background.flipVertical) {
        result = result.mirrored(background.flipHorizontal, background.flipVertical);
    }

    // Apply grayscale conversion
    if (background.grayscale) {
        for (int y = 0; y < result.height(); ++y) {
            QRgb* line = reinterpret_cast<QRgb*>(result.scanLine(y));
            for (int x = 0; x < result.width(); ++x) {
                int gray = qGray(line[x]);
                int alpha = qAlpha(line[x]);
                line[x] = qRgba(gray, gray, gray, alpha);
            }
        }
    }

    // Apply contrast and brightness
    if (qAbs(background.contrast - 1.0) > 0.001 ||
        qAbs(background.brightness) > 0.001) {

        double contrast = background.contrast;
        double brightness = background.brightness * 255;  // Convert to 0-255 range

        for (int y = 0; y < result.height(); ++y) {
            QRgb* line = reinterpret_cast<QRgb*>(result.scanLine(y));
            for (int x = 0; x < result.width(); ++x) {
                int r = qRed(line[x]);
                int g = qGreen(line[x]);
                int b = qBlue(line[x]);
                int a = qAlpha(line[x]);

                // Apply contrast around mid-gray, then add brightness
                r = qBound(0, static_cast<int>((r - 128) * contrast + 128 + brightness), 255);
                g = qBound(0, static_cast<int>((g - 128) * contrast + 128 + brightness), 255);
                b = qBound(0, static_cast<int>((b - 128) * contrast + 128 + brightness), 255);

                line[x] = qRgba(r, g, b, a);
            }
        }
    }

    // Apply opacity
    if (background.opacity < 1.0) {
        int alphaMultiplier = static_cast<int>(background.opacity * 255);
        for (int y = 0; y < result.height(); ++y) {
            QRgb* line = reinterpret_cast<QRgb*>(result.scanLine(y));
            for (int x = 0; x < result.width(); ++x) {
                int a = (qAlpha(line[x]) * alphaMultiplier) / 255;
                line[x] = qRgba(qRed(line[x]), qGreen(line[x]), qBlue(line[x]), a);
            }
        }
    }

    return result;
}

// =====================================================================
//  Utility Functions
// =====================================================================

void calculateAspectRatio(
    int originalWidth,
    int originalHeight,
    double targetWidth,
    double& targetHeight,
    bool lockAspect)
{
    if (!lockAspect || originalWidth == 0 || originalHeight == 0) {
        return;
    }

    double aspect = static_cast<double>(originalHeight) / originalWidth;
    targetHeight = targetWidth * aspect;
}

BackgroundImage calibrateBackground(
    const BackgroundImage& background,
    const QPointF& point1,
    const QPointF& point2,
    double realDistance)
{
    BackgroundImage result = background;

    if (realDistance <= 0) {
        return result;
    }

    // Calculate pixel distance
    double dx = point2.x() - point1.x();
    double dy = point2.y() - point1.y();
    double pixelDistance = qSqrt(dx * dx + dy * dy);

    if (pixelDistance < 1.0) {
        return result;
    }

    // Calculate scale: pixels per mm
    result.calibrationScale = pixelDistance / realDistance;
    result.calibrated = true;

    // Adjust width/height based on calibration
    QImage image = getBackgroundQImage(background);
    if (!image.isNull()) {
        result.width = image.width() / result.calibrationScale;
        result.height = image.height() / result.calibrationScale;
    }

    return result;
}

QPointF sketchToImageCoords(
    const BackgroundImage& background,
    const QPointF& sketchPoint)
{
    // Convert from sketch coordinates (mm) to image pixel coordinates
    QImage image = getBackgroundQImage(background);
    if (image.isNull()) {
        return QPointF(0, 0);
    }

    // Calculate offset from background position
    double offsetX = sketchPoint.x() - background.position.x();
    double offsetY = sketchPoint.y() - background.position.y();

    // Convert mm to pixels
    double scaleX = image.width() / background.width;
    double scaleY = image.height() / background.height;

    return QPointF(offsetX * scaleX, offsetY * scaleY);
}

QPointF imageToSketchCoords(
    const BackgroundImage& background,
    const QPointF& imagePoint)
{
    // Convert from image pixel coordinates to sketch coordinates (mm)
    QImage image = getBackgroundQImage(background);
    if (image.isNull()) {
        return background.position;
    }

    // Convert pixels to mm
    double scaleX = background.width / image.width();
    double scaleY = background.height / image.height();

    double offsetX = imagePoint.x() * scaleX;
    double offsetY = imagePoint.y() * scaleY;

    return QPointF(background.position.x() + offsetX,
                   background.position.y() + offsetY);
}

// =====================================================================
//  Alignment Utilities
// =====================================================================

double calculateLineAngle(const QPointF& point1, const QPointF& point2)
{
    double dx = point2.x() - point1.x();
    double dy = point2.y() - point1.y();
    return qRadiansToDegrees(qAtan2(dy, dx));
}

double calculateAlignmentRotation(double currentAngle, double targetAngle)
{
    double rotation = targetAngle - currentAngle;

    // Normalize to -180 to +180 for shortest rotation path
    while (rotation > 180.0) rotation -= 360.0;
    while (rotation < -180.0) rotation += 360.0;

    return rotation;
}

double normalizeAngle360(double degrees)
{
    degrees = fmod(degrees, 360.0);
    if (degrees < 0) degrees += 360.0;
    return degrees;
}

double normalizeAngle180(double degrees)
{
    degrees = fmod(degrees, 360.0);
    if (degrees > 180.0) degrees -= 360.0;
    if (degrees < -180.0) degrees += 360.0;
    return degrees;
}

// =====================================================================
//  Project Integration
// =====================================================================

bool isFileInProject(const QString& filePath, const QString& projectDir)
{
    if (projectDir.isEmpty() || filePath.isEmpty()) {
        return false;
    }

    QFileInfo fileInfo(filePath);
    QFileInfo projectInfo(projectDir);

    QString absFilePath = fileInfo.absoluteFilePath();
    QString absProjectDir = projectInfo.absoluteFilePath();

    // Ensure project dir ends with separator for proper prefix matching
    if (!absProjectDir.endsWith('/') && !absProjectDir.endsWith('\\')) {
        absProjectDir += '/';
    }

    return absFilePath.startsWith(absProjectDir);
}

QString toRelativePath(const QString& absolutePath, const QString& projectDir)
{
    if (projectDir.isEmpty() || absolutePath.isEmpty()) {
        return absolutePath;
    }

    QDir dir(projectDir);
    return dir.relativeFilePath(absolutePath);
}

QString toAbsolutePath(const QString& relativePath, const QString& projectDir)
{
    if (projectDir.isEmpty() || relativePath.isEmpty()) {
        return relativePath;
    }

    // If already absolute, return as-is
    QFileInfo info(relativePath);
    if (info.isAbsolute()) {
        return relativePath;
    }

    QDir dir(projectDir);
    return dir.absoluteFilePath(relativePath);
}

BackgroundImage exportBackgroundToProject(
    const BackgroundImage& background,
    const QString& projectDir,
    const QString& sketchName)
{
    BackgroundImage result = background;

    if (!background.enabled || projectDir.isEmpty()) {
        return result;
    }

    // Create backgrounds directory if needed
    QString bgDir = projectDir + QStringLiteral("/sketches/backgrounds");
    QDir dir(bgDir);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    // Determine file extension from MIME type
    QString ext = QStringLiteral("png");
    if (background.mimeType == QStringLiteral("image/jpeg")) {
        ext = QStringLiteral("jpg");
    } else if (background.mimeType == QStringLiteral("image/bmp")) {
        ext = QStringLiteral("bmp");
    } else if (background.mimeType == QStringLiteral("image/gif")) {
        ext = QStringLiteral("gif");
    } else if (background.mimeType == QStringLiteral("image/webp")) {
        ext = QStringLiteral("webp");
    }

    // Generate filename from sketch name
    QString safeName = sketchName;
    safeName.replace(QRegularExpression(QStringLiteral("[^a-zA-Z0-9_-]")), QStringLiteral("_"));
    if (safeName.isEmpty()) {
        safeName = QStringLiteral("background");
    }

    QString fileName = QStringLiteral("%1_bg.%2").arg(safeName, ext);
    QString fullPath = bgDir + "/" + fileName;

    // Handle name conflicts
    int counter = 1;
    while (QFile::exists(fullPath)) {
        fileName = QStringLiteral("%1_bg_%2.%3").arg(safeName).arg(counter++).arg(ext);
        fullPath = bgDir + "/" + fileName;
    }

    // Get image data to save
    QByteArray imageData;
    if (background.storage == BackgroundStorage::Embedded) {
        imageData = background.imageData;
    } else {
        // Read from file
        QFile file(background.filePath);
        if (file.open(QIODevice::ReadOnly)) {
            imageData = file.readAll();
        }
    }

    if (imageData.isEmpty()) {
        return result;
    }

    // Write to project
    QFile outFile(fullPath);
    if (outFile.open(QIODevice::WriteOnly)) {
        outFile.write(imageData);
        outFile.close();

        // Update result to use file path storage
        result.storage = BackgroundStorage::FilePath;
        result.filePath = toRelativePath(fullPath, projectDir);
        result.imageData.clear();  // Clear embedded data
    }

    return result;
}

BackgroundImage updateBackgroundFromFile(
    const QString& filePath,
    const QString& projectDir)
{
    // Load the image
    BackgroundImage bg = loadBackgroundImage(filePath, false);

    if (!bg.enabled) {
        return bg;
    }

    // Check if file is inside project
    if (!projectDir.isEmpty() && isFileInProject(filePath, projectDir)) {
        // Inside project - store as relative path
        bg.storage = BackgroundStorage::FilePath;
        bg.filePath = toRelativePath(filePath, projectDir);
        bg.imageData.clear();
    } else {
        // Outside project - embed as base64
        QFile file(filePath);
        if (file.open(QIODevice::ReadOnly)) {
            bg.storage = BackgroundStorage::Embedded;
            bg.imageData = file.readAll();
            // Keep absolute path for display purposes
            bg.filePath = filePath;
        }
    }

    return bg;
}

// =====================================================================
//  Serialization
// =====================================================================

QString backgroundToJson(
    const BackgroundImage& background,
    bool includeImageData)
{
    QJsonObject obj;

    obj["enabled"] = background.enabled;
    obj["storage"] = static_cast<int>(background.storage);
    obj["filePath"] = background.filePath;
    obj["mimeType"] = background.mimeType;

    obj["positionX"] = background.position.x();
    obj["positionY"] = background.position.y();
    obj["width"] = background.width;
    obj["height"] = background.height;
    obj["rotation"] = background.rotation;

    obj["opacity"] = background.opacity;
    obj["lockAspectRatio"] = background.lockAspectRatio;
    obj["flipHorizontal"] = background.flipHorizontal;
    obj["flipVertical"] = background.flipVertical;
    obj["grayscale"] = background.grayscale;
    obj["contrast"] = background.contrast;
    obj["brightness"] = background.brightness;

    obj["calibrated"] = background.calibrated;
    obj["calibrationScale"] = background.calibrationScale;

    obj["originalPixelWidth"] = background.originalPixelWidth;
    obj["originalPixelHeight"] = background.originalPixelHeight;

    if (includeImageData && background.storage == BackgroundStorage::Embedded) {
        obj["imageData"] = QString::fromLatin1(background.imageData.toBase64());
    }

    QJsonDocument doc(obj);
    return QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
}

BackgroundImage backgroundFromJson(const QString& json)
{
    BackgroundImage bg;

    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isObject()) {
        return bg;
    }

    QJsonObject obj = doc.object();

    bg.enabled = obj["enabled"].toBool(false);
    bg.storage = static_cast<BackgroundStorage>(obj["storage"].toInt(0));
    bg.filePath = obj["filePath"].toString();
    bg.mimeType = obj["mimeType"].toString();

    bg.position = QPointF(obj["positionX"].toDouble(0),
                          obj["positionY"].toDouble(0));
    bg.width = obj["width"].toDouble(100);
    bg.height = obj["height"].toDouble(100);
    bg.rotation = obj["rotation"].toDouble(0);

    bg.opacity = obj["opacity"].toDouble(0.5);
    bg.lockAspectRatio = obj["lockAspectRatio"].toBool(true);
    bg.flipHorizontal = obj["flipHorizontal"].toBool(false);
    bg.flipVertical = obj["flipVertical"].toBool(false);
    bg.grayscale = obj["grayscale"].toBool(false);
    bg.contrast = obj["contrast"].toDouble(1.0);
    bg.brightness = obj["brightness"].toDouble(0.0);

    bg.calibrated = obj["calibrated"].toBool(false);
    bg.calibrationScale = obj["calibrationScale"].toDouble(1.0);

    bg.originalPixelWidth = obj["originalPixelWidth"].toInt(0);
    bg.originalPixelHeight = obj["originalPixelHeight"].toInt(0);

    if (obj.contains("imageData")) {
        bg.imageData = QByteArray::fromBase64(
            obj["imageData"].toString().toLatin1());
    }

    return bg;
}

// =====================================================================
//  Supported Formats
// =====================================================================

QStringList supportedImageFormats()
{
    QStringList formats;
    for (const QByteArray& format : QImageReader::supportedImageFormats()) {
        formats.append(QString::fromLatin1(format));
    }
    return formats;
}

QString imageFileFilter()
{
    QStringList formats = supportedImageFormats();
    QStringList patterns;
    for (const QString& format : formats) {
        patterns.append(QStringLiteral("*.%1").arg(format));
    }
    return QStringLiteral("Images (%1)").arg(patterns.join(' '));
}

bool isImageFormatSupported(const QString& filePath)
{
    QFileInfo info(filePath);
    QString ext = info.suffix().toLower();
    return supportedImageFormats().contains(ext);
}

}  // namespace sketch
}  // namespace hobbycad
