// =====================================================================
//  src/libhobbycad/image_buffer.cpp — Qt-free image buffer (stb_image)
// =====================================================================
//
//  Implements image loading and manipulation using stb_image.
//  Only compiled when HOBBYCAD_HAS_QT is false; when Qt is available,
//  background.cpp uses QImage/QImageReader directly.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include "hobbycad/image_buffer.h"

#include <cstring>    // memcpy

// stb_image — single-header image loader (public domain)
// The implementation is compiled exactly once, here.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO          // We'll use our own file I/O for loadImageFile
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#define STBI_ONLY_TGA
#define STBI_ONLY_PSD
#define STBI_ONLY_HDR
#define STBI_ONLY_PNM

// Silence compiler warnings in third-party header
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

#include <stb/stb_image.h>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#if HOBBYCAD_HAS_WEBP
#include <webp/decode.h>
#endif

#include <algorithm>
#include <fstream>
#include <vector>

namespace hobbycad {

// =====================================================================
//  File loading helper — reads entire file into a byte vector
// =====================================================================

static std::vector<uint8_t> readFileBytes(const std::string& filePath)
{
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file) return {};

    auto fileSize = file.tellg();
    if (fileSize <= 0) return {};

    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> bytes(static_cast<size_t>(fileSize));
    file.read(reinterpret_cast<char*>(bytes.data()),
              static_cast<std::streamsize>(fileSize));

    return bytes;
}

// =====================================================================
//  Image loading
// =====================================================================

ImageBuffer loadImageFile(const std::string& filePath)
{
    std::vector<uint8_t> fileData = readFileBytes(filePath);
    if (fileData.empty()) return {};

    return loadImageFromMemory(fileData.data(), fileData.size());
}

ImageBuffer loadImageFromMemory(const uint8_t* data, size_t length)
{
    ImageBuffer buf;

    int w = 0, h = 0, channels = 0;
    // Request 4 channels (RGBA) regardless of source format
    unsigned char* pixels = stbi_load_from_memory(
        data, static_cast<int>(length),
        &w, &h, &channels, 4);

    if (pixels && w > 0 && h > 0) {
        buf.width  = w;
        buf.height = h;

        size_t totalBytes = static_cast<size_t>(w) * static_cast<size_t>(h) * 4;
        buf.pixels.resize(totalBytes);
        std::memcpy(buf.pixels.data(), pixels, totalBytes);

        stbi_image_free(pixels);
        return buf;
    }
    if (pixels) stbi_image_free(pixels);

#if HOBBYCAD_HAS_WEBP
    // stb_image doesn't support WebP — try libwebp as fallback
    {
        uint8_t* webpPixels = WebPDecodeRGBA(data, length, &w, &h);
        if (webpPixels && w > 0 && h > 0) {
            buf.width  = w;
            buf.height = h;

            size_t totalBytes = static_cast<size_t>(w) * static_cast<size_t>(h) * 4;
            buf.pixels.resize(totalBytes);
            std::memcpy(buf.pixels.data(), webpPixels, totalBytes);

            WebPFree(webpPixels);
            return buf;
        }
        if (webpPixels) WebPFree(webpPixels);
    }
#endif  // HOBBYCAD_HAS_WEBP

    return buf;
}

bool queryImageDimensions(const std::string& filePath, int& width, int& height)
{
    std::vector<uint8_t> fileData = readFileBytes(filePath);
    if (fileData.empty()) return false;

    return queryImageDimensionsFromMemory(fileData.data(), fileData.size(),
                                         width, height);
}

bool queryImageDimensionsFromMemory(const uint8_t* data, size_t length,
                                    int& width, int& height)
{
    int channels = 0;
    int ok = stbi_info_from_memory(data, static_cast<int>(length),
                                   &width, &height, &channels);
    if (ok) return true;

#if HOBBYCAD_HAS_WEBP
    // stb_image doesn't recognize WebP — try libwebp header probe
    if (WebPGetInfo(data, length, &width, &height)) {
        return true;
    }
#endif  // HOBBYCAD_HAS_WEBP

    return false;
}

// =====================================================================
//  Image manipulation
// =====================================================================

ImageBuffer flipHorizontal(const ImageBuffer& src)
{
    if (src.isNull()) return {};

    ImageBuffer dst = ImageBuffer::create(src.width, src.height);

    for (int y = 0; y < src.height; ++y) {
        for (int x = 0; x < src.width; ++x) {
            int srcOff = src.offset(x, y);
            int dstOff = dst.offset(src.width - 1 - x, y);
            dst.pixels[static_cast<size_t>(dstOff)]     = src.pixels[static_cast<size_t>(srcOff)];
            dst.pixels[static_cast<size_t>(dstOff + 1)] = src.pixels[static_cast<size_t>(srcOff + 1)];
            dst.pixels[static_cast<size_t>(dstOff + 2)] = src.pixels[static_cast<size_t>(srcOff + 2)];
            dst.pixels[static_cast<size_t>(dstOff + 3)] = src.pixels[static_cast<size_t>(srcOff + 3)];
        }
    }

    return dst;
}

ImageBuffer flipVertical(const ImageBuffer& src)
{
    if (src.isNull()) return {};

    ImageBuffer dst = ImageBuffer::create(src.width, src.height);
    size_t rowBytes = static_cast<size_t>(src.width) * 4;

    for (int y = 0; y < src.height; ++y) {
        const uint8_t* srcRow = src.pixels.data() + static_cast<size_t>(y) * rowBytes;
        uint8_t* dstRow = dst.pixels.data() + static_cast<size_t>(src.height - 1 - y) * rowBytes;
        std::memcpy(dstRow, srcRow, rowBytes);
    }

    return dst;
}

}  // namespace hobbycad
