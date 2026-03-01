// =====================================================================
//  src/libhobbycad/sketch/background.cpp — Sketch background image
// =====================================================================
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include <hobbycad/sketch/background.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#if HOBBYCAD_HAS_QT
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#else
#if HOBBYCAD_HAS_STB_IMAGE
#include <hobbycad/image_buffer.h>
#endif
#include <hobbycad/base64.h>
#include <nlohmann/json.hpp>
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

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
        position.x,
        position.y,
        position.x + width,
        position.y + height
    );
}

Point2D BackgroundImage::center() const
{
    return Point2D(position.x + width / 2.0,
                   position.y + height / 2.0);
}

bool BackgroundImage::containsPoint(const Point2D& point) const
{
    if (!enabled) return false;

    // Simple check without rotation
    // TODO: Handle rotation properly
    return point.x >= position.x &&
           point.x <= position.x + width &&
           point.y >= position.y &&
           point.y <= position.y + height;
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

BackgroundImage loadBackgroundImage(const std::string& filePath, bool embed)
{
    BackgroundImage bg;
    bg.enabled = false;

#if HOBBYCAD_HAS_QT
    QString qFilePath = QString::fromStdString(filePath);

    if (!QFile::exists(qFilePath)) {
        return bg;
    }

    QImageReader reader(qFilePath);
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
    std::filesystem::path fsPath(filePath);
    std::string ext = fsPath.extension().string();
    // Remove the leading dot and convert to lowercase
    if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (ext == "png") {
        bg.mimeType = "image/png";
    } else if (ext == "jpg" || ext == "jpeg") {
        bg.mimeType = "image/jpeg";
    } else if (ext == "bmp") {
        bg.mimeType = "image/bmp";
    } else if (ext == "gif") {
        bg.mimeType = "image/gif";
    } else if (ext == "webp") {
        bg.mimeType = "image/webp";
    } else {
        bg.mimeType = "image/png";
    }

    // Set default size based on image dimensions (assume 96 DPI for initial sizing)
    // 1 inch = 25.4 mm, 96 pixels = 1 inch
    const double pixelsPerMm = 96.0 / 25.4;
    bg.width = size.width() / pixelsPerMm;
    bg.height = size.height() / pixelsPerMm;

    if (embed) {
        // Read and embed the image data
        std::ifstream file(filePath, std::ios::binary);
        if (file) {
            bg.storage = BackgroundStorage::Embedded;
            file.seekg(0, std::ios::end);
            auto fileSize = file.tellg();
            file.seekg(0, std::ios::beg);
            bg.imageData.resize(static_cast<size_t>(fileSize));
            file.read(reinterpret_cast<char*>(bg.imageData.data()),
                      static_cast<std::streamsize>(fileSize));
        }
    } else {
        bg.storage = BackgroundStorage::FilePath;
    }
#else
    // Non-Qt path
    if (!std::filesystem::exists(filePath)) {
        return bg;
    }

    bg.filePath = filePath;

    // Determine MIME type from extension
    std::filesystem::path fsPath(filePath);
    std::string ext = fsPath.extension().string();
    if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (ext == "png") {
        bg.mimeType = "image/png";
    } else if (ext == "jpg" || ext == "jpeg") {
        bg.mimeType = "image/jpeg";
    } else if (ext == "bmp") {
        bg.mimeType = "image/bmp";
    } else if (ext == "gif") {
        bg.mimeType = "image/gif";
    } else if (ext == "tga") {
        bg.mimeType = "image/x-tga";
    } else if (ext == "psd") {
        bg.mimeType = "image/vnd.adobe.photoshop";
    } else if (ext == "hdr") {
        bg.mimeType = "image/vnd.radiance";
    } else if (ext == "webp") {
        bg.mimeType = "image/webp";
    } else {
        bg.mimeType = "image/png";
    }

#if HOBBYCAD_HAS_STB_IMAGE
    // Query image dimensions via stb_image (does not fully decode)
    int imgW = 0, imgH = 0;
    if (!hobbycad::queryImageDimensions(filePath, imgW, imgH) ||
        imgW <= 0 || imgH <= 0) {
        return bg;
    }

    // Store original pixel dimensions for scale factor calculations
    bg.originalPixelWidth  = imgW;
    bg.originalPixelHeight = imgH;

    // Set default size based on image dimensions (assume 96 DPI)
    const double pixelsPerMm = 96.0 / 25.4;
    bg.width  = imgW / pixelsPerMm;
    bg.height = imgH / pixelsPerMm;
#endif  // HOBBYCAD_HAS_STB_IMAGE

    bg.enabled = true;

    if (embed) {
        std::ifstream file(filePath, std::ios::binary);
        if (file) {
            bg.storage = BackgroundStorage::Embedded;
            file.seekg(0, std::ios::end);
            auto fileSize = file.tellg();
            file.seekg(0, std::ios::beg);
            bg.imageData.resize(static_cast<size_t>(fileSize));
            file.read(reinterpret_cast<char*>(bg.imageData.data()),
                      static_cast<std::streamsize>(fileSize));
        }
    } else {
        bg.storage = BackgroundStorage::FilePath;
    }
#endif

    return bg;
}

BackgroundImage loadBackgroundImageFromData(
    const std::vector<uint8_t>& data,
    const std::string& mimeType)
{
    BackgroundImage bg;
    bg.enabled = false;

    if (data.empty()) {
        return bg;
    }

#if HOBBYCAD_HAS_QT
    // Load image to get dimensions
    QByteArray qData(reinterpret_cast<const char*>(data.data()),
                     static_cast<int>(data.size()));
    QImage image;
    if (!image.loadFromData(qData)) {
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
#else
    // Non-Qt path
#if HOBBYCAD_HAS_STB_IMAGE
    // Query dimensions via stb_image
    int imgW = 0, imgH = 0;
    if (hobbycad::queryImageDimensionsFromMemory(data.data(), data.size(),
                                                 imgW, imgH) &&
        imgW > 0 && imgH > 0) {
        bg.originalPixelWidth  = imgW;
        bg.originalPixelHeight = imgH;

        // Set default size (assume 96 DPI)
        const double pixelsPerMm = 96.0 / 25.4;
        bg.width  = imgW / pixelsPerMm;
        bg.height = imgH / pixelsPerMm;
    }
#endif  // HOBBYCAD_HAS_STB_IMAGE

    bg.enabled = true;
    bg.storage = BackgroundStorage::Embedded;
    bg.imageData = data;
    bg.mimeType = mimeType;
#endif

    return bg;
}

// =====================================================================
//  Image Retrieval
// =====================================================================

#if HOBBYCAD_HAS_QT
QImage getBackgroundQImage(const BackgroundImage& background)
{
    if (!background.enabled) {
        return QImage();
    }

    QImage image;

    if (background.storage == BackgroundStorage::Embedded) {
        QByteArray qData(reinterpret_cast<const char*>(background.imageData.data()),
                         static_cast<int>(background.imageData.size()));
        image.loadFromData(qData);
    } else {
        image.load(QString::fromStdString(background.filePath));
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
    if (std::abs(background.contrast - 1.0) > 0.001 ||
        std::abs(background.brightness) > 0.001) {

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
                r = std::clamp(static_cast<int>((r - 128) * contrast + 128 + brightness), 0, 255);
                g = std::clamp(static_cast<int>((g - 128) * contrast + 128 + brightness), 0, 255);
                b = std::clamp(static_cast<int>((b - 128) * contrast + 128 + brightness), 0, 255);

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
#elif HOBBYCAD_HAS_STB_IMAGE  // stb_image path

ImageBuffer getBackgroundImage(const BackgroundImage& background)
{
    if (!background.enabled) {
        return {};
    }

    if (background.storage == BackgroundStorage::Embedded) {
        return hobbycad::loadImageFromMemory(background.imageData);
    } else {
        return hobbycad::loadImageFile(background.filePath);
    }
}

ImageBuffer applyBackgroundAdjustments(
    const ImageBuffer& image,
    const BackgroundImage& background)
{
    if (image.isNull()) {
        return image;
    }

    ImageBuffer result = image;

    // Apply flip/mirror transformations
    if (background.flipHorizontal) {
        result = hobbycad::flipHorizontal(result);
    }
    if (background.flipVertical) {
        result = hobbycad::flipVertical(result);
    }

    // Apply grayscale conversion
    if (background.grayscale) {
        for (int y = 0; y < result.height; ++y) {
            for (int x = 0; x < result.width; ++x) {
                uint8_t r = result.red(x, y);
                uint8_t g = result.green(x, y);
                uint8_t b = result.blue(x, y);
                uint8_t a = result.alpha(x, y);
                uint8_t gray = ImageBuffer::grayValue(r, g, b);
                result.setPixel(x, y, gray, gray, gray, a);
            }
        }
    }

    // Apply contrast and brightness
    if (std::abs(background.contrast - 1.0) > 0.001 ||
        std::abs(background.brightness) > 0.001) {

        double contrast   = background.contrast;
        double brightness = background.brightness * 255;  // Convert to 0-255 range

        for (int y = 0; y < result.height; ++y) {
            for (int x = 0; x < result.width; ++x) {
                int r = result.red(x, y);
                int g = result.green(x, y);
                int b = result.blue(x, y);
                uint8_t a = result.alpha(x, y);

                // Apply contrast around mid-gray, then add brightness
                r = std::clamp(static_cast<int>((r - 128) * contrast + 128 + brightness), 0, 255);
                g = std::clamp(static_cast<int>((g - 128) * contrast + 128 + brightness), 0, 255);
                b = std::clamp(static_cast<int>((b - 128) * contrast + 128 + brightness), 0, 255);

                result.setPixel(x, y,
                                static_cast<uint8_t>(r),
                                static_cast<uint8_t>(g),
                                static_cast<uint8_t>(b), a);
            }
        }
    }

    // Apply opacity
    if (background.opacity < 1.0) {
        int alphaMultiplier = static_cast<int>(background.opacity * 255);
        for (int y = 0; y < result.height; ++y) {
            for (int x = 0; x < result.width; ++x) {
                int a = (result.alpha(x, y) * alphaMultiplier) / 255;
                result.setPixel(x, y,
                                result.red(x, y),
                                result.green(x, y),
                                result.blue(x, y),
                                static_cast<uint8_t>(a));
            }
        }
    }

    return result;
}

#endif  // HOBBYCAD_HAS_QT

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
    const Point2D& point1,
    const Point2D& point2,
    double realDistance)
{
    BackgroundImage result = background;

    if (realDistance <= 0) {
        return result;
    }

    // Calculate pixel distance
    double dx = point2.x - point1.x;
    double dy = point2.y - point1.y;
    double pixelDistance = std::sqrt(dx * dx + dy * dy);

    if (pixelDistance < 1.0) {
        return result;
    }

    // Calculate scale: pixels per mm
    result.calibrationScale = pixelDistance / realDistance;
    result.calibrated = true;

#if HOBBYCAD_HAS_QT
    // Adjust width/height based on calibration
    QImage image = getBackgroundQImage(background);
    if (!image.isNull()) {
        result.width = image.width() / result.calibrationScale;
        result.height = image.height() / result.calibrationScale;
    }
#else
    // Non-Qt path: use stored pixel dimensions for calibration
    if (background.originalPixelWidth > 0 && background.originalPixelHeight > 0) {
        result.width  = background.originalPixelWidth  / result.calibrationScale;
        result.height = background.originalPixelHeight / result.calibrationScale;
    }
#endif

    return result;
}

Point2D sketchToImageCoords(
    const BackgroundImage& background,
    const Point2D& sketchPoint)
{
#if HOBBYCAD_HAS_QT
    // Convert from sketch coordinates (mm) to image pixel coordinates
    QImage image = getBackgroundQImage(background);
    if (image.isNull()) {
        return Point2D(0, 0);
    }

    // Calculate offset from background position
    double offsetX = sketchPoint.x - background.position.x;
    double offsetY = sketchPoint.y - background.position.y;

    // Convert mm to pixels
    double scaleX = image.width() / background.width;
    double scaleY = image.height() / background.height;

    return Point2D(offsetX * scaleX, offsetY * scaleY);
#else
    // Without Qt, approximate using stored pixel dimensions
    if (background.originalPixelWidth <= 0 || background.width <= 0) {
        return Point2D(0, 0);
    }

    double offsetX = sketchPoint.x - background.position.x;
    double offsetY = sketchPoint.y - background.position.y;

    double scaleX = background.originalPixelWidth / background.width;
    double scaleY = background.originalPixelHeight / background.height;

    return Point2D(offsetX * scaleX, offsetY * scaleY);
#endif
}

Point2D imageToSketchCoords(
    const BackgroundImage& background,
    const Point2D& imagePoint)
{
#if HOBBYCAD_HAS_QT
    // Convert from image pixel coordinates to sketch coordinates (mm)
    QImage image = getBackgroundQImage(background);
    if (image.isNull()) {
        return background.position;
    }

    // Convert pixels to mm
    double scaleX = background.width / image.width();
    double scaleY = background.height / image.height();

    double offsetX = imagePoint.x * scaleX;
    double offsetY = imagePoint.y * scaleY;

    return Point2D(background.position.x + offsetX,
                   background.position.y + offsetY);
#else
    // Without Qt, approximate using stored pixel dimensions
    if (background.originalPixelWidth <= 0) {
        return background.position;
    }

    double scaleX = background.width / background.originalPixelWidth;
    double scaleY = background.height / background.originalPixelHeight;

    double offsetX = imagePoint.x * scaleX;
    double offsetY = imagePoint.y * scaleY;

    return Point2D(background.position.x + offsetX,
                   background.position.y + offsetY);
#endif
}

// =====================================================================
//  Alignment Utilities
// =====================================================================

double calculateLineAngle(const Point2D& point1, const Point2D& point2)
{
    double dx = point2.x - point1.x;
    double dy = point2.y - point1.y;
    return std::atan2(dy, dx) * 180.0 / M_PI;
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

bool isFileInProject(const std::string& filePath, const std::string& projectDir)
{
    if (projectDir.empty() || filePath.empty()) {
        return false;
    }

    std::filesystem::path absFilePath = std::filesystem::absolute(filePath);
    std::filesystem::path absProjectDir = std::filesystem::absolute(projectDir);

    // Normalize paths
    absFilePath = absFilePath.lexically_normal();
    absProjectDir = absProjectDir.lexically_normal();

    // Ensure project dir ends with separator for proper prefix matching
    std::string projectStr = absProjectDir.string();
    if (!projectStr.empty() && projectStr.back() != '/' && projectStr.back() != '\\') {
        projectStr += '/';
    }

    std::string fileStr = absFilePath.string();
    return fileStr.compare(0, projectStr.size(), projectStr) == 0;
}

std::string toRelativePath(const std::string& absolutePath, const std::string& projectDir)
{
    if (projectDir.empty() || absolutePath.empty()) {
        return absolutePath;
    }

    std::filesystem::path absPath = std::filesystem::absolute(absolutePath);
    std::filesystem::path projPath = std::filesystem::absolute(projectDir);

    return absPath.lexically_relative(projPath).string();
}

std::string toAbsolutePath(const std::string& relativePath, const std::string& projectDir)
{
    if (projectDir.empty() || relativePath.empty()) {
        return relativePath;
    }

    // If already absolute, return as-is
    std::filesystem::path relPath(relativePath);
    if (relPath.is_absolute()) {
        return relativePath;
    }

    std::filesystem::path projPath(projectDir);
    return (projPath / relPath).lexically_normal().string();
}

BackgroundImage exportBackgroundToProject(
    const BackgroundImage& background,
    const std::string& projectDir,
    const std::string& sketchName)
{
    BackgroundImage result = background;

    if (!background.enabled || projectDir.empty()) {
        return result;
    }

    // Create backgrounds directory if needed
    std::string bgDir = projectDir + "/sketches/backgrounds";
    std::filesystem::create_directories(bgDir);

    // Determine file extension from MIME type
    std::string ext = "png";
    if (background.mimeType == "image/jpeg") {
        ext = "jpg";
    } else if (background.mimeType == "image/bmp") {
        ext = "bmp";
    } else if (background.mimeType == "image/gif") {
        ext = "gif";
    } else if (background.mimeType == "image/webp") {
        ext = "webp";
    }

    // Generate filename from sketch name
    std::string safeName = sketchName;
    std::regex unsafeChars("[^a-zA-Z0-9_-]");
    safeName = std::regex_replace(safeName, unsafeChars, "_");
    if (safeName.empty()) {
        safeName = "background";
    }

    std::string fileName = safeName + "_bg." + ext;
    std::string fullPath = bgDir + "/" + fileName;

    // Handle name conflicts
    int counter = 1;
    while (std::filesystem::exists(fullPath)) {
        fileName = safeName + "_bg_" + std::to_string(counter++) + "." + ext;
        fullPath = bgDir + "/" + fileName;
    }

    // Get image data to save
    std::vector<uint8_t> imageDataToWrite;
    if (background.storage == BackgroundStorage::Embedded) {
        imageDataToWrite = background.imageData;
    } else {
        // Read from file
        std::ifstream file(background.filePath, std::ios::binary);
        if (file) {
            file.seekg(0, std::ios::end);
            auto fileSize = file.tellg();
            file.seekg(0, std::ios::beg);
            imageDataToWrite.resize(static_cast<size_t>(fileSize));
            file.read(reinterpret_cast<char*>(imageDataToWrite.data()),
                      static_cast<std::streamsize>(fileSize));
        }
    }

    if (imageDataToWrite.empty()) {
        return result;
    }

    // Write to project
    std::ofstream outFile(fullPath, std::ios::binary);
    if (outFile) {
        outFile.write(reinterpret_cast<const char*>(imageDataToWrite.data()),
                      static_cast<std::streamsize>(imageDataToWrite.size()));
        outFile.close();

        // Update result to use file path storage
        result.storage = BackgroundStorage::FilePath;
        result.filePath = toRelativePath(fullPath, projectDir);
        result.imageData.clear();  // Clear embedded data
    }

    return result;
}

BackgroundImage updateBackgroundFromFile(
    const std::string& filePath,
    const std::string& projectDir)
{
    // Load the image
    BackgroundImage bg = loadBackgroundImage(filePath, false);

    if (!bg.enabled) {
        return bg;
    }

    // Check if file is inside project
    if (!projectDir.empty() && isFileInProject(filePath, projectDir)) {
        // Inside project - store as relative path
        bg.storage = BackgroundStorage::FilePath;
        bg.filePath = toRelativePath(filePath, projectDir);
        bg.imageData.clear();
    } else {
        // Outside project - embed the data
        std::ifstream file(filePath, std::ios::binary);
        if (file) {
            bg.storage = BackgroundStorage::Embedded;
            file.seekg(0, std::ios::end);
            auto fileSize = file.tellg();
            file.seekg(0, std::ios::beg);
            bg.imageData.resize(static_cast<size_t>(fileSize));
            file.read(reinterpret_cast<char*>(bg.imageData.data()),
                      static_cast<std::streamsize>(fileSize));
            // Keep absolute path for display purposes
            bg.filePath = filePath;
        }
    }

    return bg;
}

// =====================================================================
//  Serialization
// =====================================================================

#if HOBBYCAD_HAS_QT

std::string backgroundToJson(
    const BackgroundImage& background,
    bool includeImageData)
{
    QJsonObject obj;

    obj["enabled"] = background.enabled;
    obj["storage"] = static_cast<int>(background.storage);
    obj["filePath"] = QString::fromStdString(background.filePath);
    obj["mimeType"] = QString::fromStdString(background.mimeType);

    obj["positionX"] = background.position.x;
    obj["positionY"] = background.position.y;
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
        QByteArray qData(reinterpret_cast<const char*>(background.imageData.data()),
                         static_cast<int>(background.imageData.size()));
        obj["imageData"] = QString::fromLatin1(qData.toBase64());
    }

    QJsonDocument doc(obj);
    return doc.toJson(QJsonDocument::Compact).toStdString();
}

BackgroundImage backgroundFromJson(const std::string& json)
{
    BackgroundImage bg;

    QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(json));
    if (!doc.isObject()) {
        return bg;
    }

    QJsonObject obj = doc.object();

    bg.enabled = obj["enabled"].toBool(false);
    bg.storage = static_cast<BackgroundStorage>(obj["storage"].toInt(0));
    bg.filePath = obj["filePath"].toString().toStdString();
    bg.mimeType = obj["mimeType"].toString().toStdString();

    bg.position = Point2D(obj["positionX"].toDouble(0),
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
        QByteArray decoded = QByteArray::fromBase64(
            obj["imageData"].toString().toLatin1());
        bg.imageData.assign(
            reinterpret_cast<const uint8_t*>(decoded.constData()),
            reinterpret_cast<const uint8_t*>(decoded.constData()) + decoded.size());
    }

    return bg;
}

#else  // !HOBBYCAD_HAS_QT — nlohmann/json path

std::string backgroundToJson(
    const BackgroundImage& background,
    bool includeImageData)
{
    nlohmann::json obj;

    obj["enabled"]   = background.enabled;
    obj["storage"]   = static_cast<int>(background.storage);
    obj["filePath"]  = background.filePath;
    obj["mimeType"]  = background.mimeType;

    obj["positionX"] = background.position.x;
    obj["positionY"] = background.position.y;
    obj["width"]     = background.width;
    obj["height"]    = background.height;
    obj["rotation"]  = background.rotation;

    obj["opacity"]          = background.opacity;
    obj["lockAspectRatio"]  = background.lockAspectRatio;
    obj["flipHorizontal"]   = background.flipHorizontal;
    obj["flipVertical"]     = background.flipVertical;
    obj["grayscale"]        = background.grayscale;
    obj["contrast"]         = background.contrast;
    obj["brightness"]       = background.brightness;

    obj["calibrated"]       = background.calibrated;
    obj["calibrationScale"] = background.calibrationScale;

    obj["originalPixelWidth"]  = background.originalPixelWidth;
    obj["originalPixelHeight"] = background.originalPixelHeight;

    if (includeImageData && background.storage == BackgroundStorage::Embedded &&
        !background.imageData.empty()) {
        obj["imageData"] = hobbycad::base64Encode(background.imageData);
    }

    return obj.dump();
}

BackgroundImage backgroundFromJson(const std::string& json)
{
    BackgroundImage bg;

    nlohmann::json obj;
    try {
        obj = nlohmann::json::parse(json);
    } catch (...) {
        return bg;
    }

    if (!obj.is_object()) {
        return bg;
    }

    bg.enabled  = obj.value("enabled", false);
    bg.storage  = static_cast<BackgroundStorage>(obj.value("storage", 0));
    bg.filePath = obj.value("filePath", std::string{});
    bg.mimeType = obj.value("mimeType", std::string{});

    bg.position = Point2D(obj.value("positionX", 0.0),
                          obj.value("positionY", 0.0));
    bg.width    = obj.value("width", 100.0);
    bg.height   = obj.value("height", 100.0);
    bg.rotation = obj.value("rotation", 0.0);

    bg.opacity         = obj.value("opacity", 0.5);
    bg.lockAspectRatio = obj.value("lockAspectRatio", true);
    bg.flipHorizontal  = obj.value("flipHorizontal", false);
    bg.flipVertical    = obj.value("flipVertical", false);
    bg.grayscale       = obj.value("grayscale", false);
    bg.contrast        = obj.value("contrast", 1.0);
    bg.brightness      = obj.value("brightness", 0.0);

    bg.calibrated       = obj.value("calibrated", false);
    bg.calibrationScale = obj.value("calibrationScale", 1.0);

    bg.originalPixelWidth  = obj.value("originalPixelWidth", 0);
    bg.originalPixelHeight = obj.value("originalPixelHeight", 0);

    if (obj.contains("imageData") && obj["imageData"].is_string()) {
        bg.imageData = hobbycad::base64Decode(obj["imageData"].get<std::string>());
    }

    return bg;
}

#endif  // HOBBYCAD_HAS_QT

// =====================================================================
//  Supported Formats
// =====================================================================

#if HOBBYCAD_HAS_QT

std::vector<std::string> supportedImageFormats()
{
    std::vector<std::string> formats;
    for (const QByteArray& format : QImageReader::supportedImageFormats()) {
        formats.push_back(QString::fromLatin1(format).toStdString());
    }
    return formats;
}

std::string imageFileFilter()
{
    std::vector<std::string> formats = supportedImageFormats();
    std::string patterns;
    for (size_t i = 0; i < formats.size(); ++i) {
        if (i > 0) patterns += ' ';
        patterns += "*." + formats[i];
    }
    return "Images (" + patterns + ")";
}

bool isImageFormatSupported(const std::string& filePath)
{
    std::filesystem::path fsPath(filePath);
    std::string ext = fsPath.extension().string();
    if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    std::vector<std::string> formats = supportedImageFormats();
    for (const auto& fmt : formats) {
        if (fmt == ext) return true;
    }
    return false;
}

#else  // !HOBBYCAD_HAS_QT — stb_image (+ optional libwebp) supported formats

std::vector<std::string> supportedImageFormats()
{
    // Formats supported by stb_image
    std::vector<std::string> formats = {
        "png", "jpg", "jpeg", "bmp", "gif", "tga", "psd", "hdr", "pnm"
    };
#if HOBBYCAD_HAS_WEBP
    formats.push_back("webp");
#endif
    return formats;
}

std::string imageFileFilter()
{
    std::string filter = "Images (*.png *.jpg *.jpeg *.bmp *.gif *.tga *.psd *.hdr *.pnm";
#if HOBBYCAD_HAS_WEBP
    filter += " *.webp";
#endif
    filter += ")";
    return filter;
}

bool isImageFormatSupported(const std::string& filePath)
{
    std::filesystem::path fsPath(filePath);
    std::string ext = fsPath.extension().string();
    if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    std::vector<std::string> formats = supportedImageFormats();
    for (const auto& fmt : formats) {
        if (fmt == ext) return true;
    }
    return false;
}

#endif  // HOBBYCAD_HAS_QT

}  // namespace sketch
}  // namespace hobbycad
