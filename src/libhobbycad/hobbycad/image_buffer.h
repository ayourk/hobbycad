// =====================================================================
//  src/libhobbycad/hobbycad/image_buffer.h — Qt-free image buffer
// =====================================================================
//
//  Simple RGBA image buffer for background image operations when Qt is
//  not available.  Backed by stb_image for decoding.
//
//  When Qt IS available, the GUI uses QImage directly; this type is only
//  compiled into the non-Qt path.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_IMAGE_BUFFER_H
#define HOBBYCAD_IMAGE_BUFFER_H

#include "core.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace hobbycad {

// =====================================================================
//  ImageBuffer — flat RGBA pixel buffer
// =====================================================================

/// A simple RGBA8 image buffer.
///
/// Pixel layout: row-major, 4 bytes per pixel (R, G, B, A).
/// The buffer owns the pixel data via a std::vector.
struct HOBBYCAD_EXPORT ImageBuffer {
    std::vector<uint8_t> pixels;   ///< RGBA8 pixel data (width * height * 4)
    int width  = 0;                ///< Image width in pixels
    int height = 0;                ///< Image height in pixels

    /// True if the buffer contains valid image data.
    bool isValid() const { return width > 0 && height > 0 && !pixels.empty(); }

    /// True if the buffer is empty / invalid.
    bool isNull() const { return !isValid(); }

    /// Total number of pixels.
    int pixelCount() const { return width * height; }

    /// Byte offset into `pixels` for pixel (x, y).
    int offset(int x, int y) const { return (y * width + x) * 4; }

    // --- Per-pixel channel accessors ---

    uint8_t red  (int x, int y) const { return pixels[static_cast<size_t>(offset(x, y))];     }
    uint8_t green(int x, int y) const { return pixels[static_cast<size_t>(offset(x, y) + 1)]; }
    uint8_t blue (int x, int y) const { return pixels[static_cast<size_t>(offset(x, y) + 2)]; }
    uint8_t alpha(int x, int y) const { return pixels[static_cast<size_t>(offset(x, y) + 3)]; }

    void setPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        auto off = static_cast<size_t>(offset(x, y));
        pixels[off]     = r;
        pixels[off + 1] = g;
        pixels[off + 2] = b;
        pixels[off + 3] = a;
    }

    /// Luminance-based gray value (ITU-R BT.601 weights).
    static uint8_t grayValue(uint8_t r, uint8_t g, uint8_t b) {
        return static_cast<uint8_t>((r * 299 + g * 587 + b * 114) / 1000);
    }

    // --- Factory helpers ---

    /// Create an empty buffer of the given size (zeroed pixels).
    static ImageBuffer create(int w, int h) {
        ImageBuffer buf;
        buf.width  = w;
        buf.height = h;
        buf.pixels.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4, 0);
        return buf;
    }
};

// =====================================================================
//  Image loading (stb_image wrappers)
// =====================================================================

/// Load an image from a file on disk.
/// Returns a valid ImageBuffer on success, an empty one on failure.
HOBBYCAD_EXPORT ImageBuffer loadImageFile(const std::string& filePath);

/// Load an image from a raw byte buffer (e.g., embedded project data).
/// Returns a valid ImageBuffer on success, an empty one on failure.
HOBBYCAD_EXPORT ImageBuffer loadImageFromMemory(const uint8_t* data, size_t length);

/// Convenience overload for std::vector.
inline ImageBuffer loadImageFromMemory(const std::vector<uint8_t>& data) {
    return loadImageFromMemory(data.data(), data.size());
}

/// Query image dimensions without fully decoding the pixel data.
/// Returns true on success and fills width/height.
HOBBYCAD_EXPORT bool queryImageDimensions(const std::string& filePath,
                                          int& width, int& height);

/// Query image dimensions from raw bytes.
HOBBYCAD_EXPORT bool queryImageDimensionsFromMemory(const uint8_t* data, size_t length,
                                                    int& width, int& height);

// =====================================================================
//  Image manipulation helpers
// =====================================================================

/// Flip the image horizontally (mirror left ↔ right).
HOBBYCAD_EXPORT ImageBuffer flipHorizontal(const ImageBuffer& src);

/// Flip the image vertically (mirror top ↔ bottom).
HOBBYCAD_EXPORT ImageBuffer flipVertical(const ImageBuffer& src);

}  // namespace hobbycad

#endif  // HOBBYCAD_IMAGE_BUFFER_H
