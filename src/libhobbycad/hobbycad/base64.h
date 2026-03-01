// =====================================================================
//  src/libhobbycad/hobbycad/base64.h — Base64 encode/decode utilities
// =====================================================================
//
//  Qt-free base64 encoding/decoding for embedding binary data in JSON.
//  Replaces QByteArray::toBase64() / QByteArray::fromBase64() when
//  building without Qt.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_BASE64_H
#define HOBBYCAD_BASE64_H

#include <cstdint>
#include <string>
#include <vector>

namespace hobbycad {

/// Encode binary data to a base64 string.
inline std::string base64Encode(const uint8_t* data, size_t length)
{
    static constexpr char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string result;
    result.reserve(((length + 2) / 3) * 4);

    for (size_t i = 0; i < length; i += 3) {
        uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < length) n |= static_cast<uint32_t>(data[i + 1]) << 8;
        if (i + 2 < length) n |= static_cast<uint32_t>(data[i + 2]);

        result.push_back(table[(n >> 18) & 0x3F]);
        result.push_back(table[(n >> 12) & 0x3F]);
        result.push_back((i + 1 < length) ? table[(n >> 6) & 0x3F] : '=');
        result.push_back((i + 2 < length) ? table[n & 0x3F] : '=');
    }
    return result;
}

/// Encode a vector of bytes to a base64 string.
inline std::string base64Encode(const std::vector<uint8_t>& data)
{
    return base64Encode(data.data(), data.size());
}

/// Decode a base64 string to binary data.
/// Returns an empty vector on invalid input.
inline std::vector<uint8_t> base64Decode(const std::string& encoded)
{
    static constexpr uint8_t lookup[256] = {
        // 0-42: invalid
        64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
        64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
        64,64,64,64,64,64,64,64,64,64,64,
        62,         // '+'  (43)
        64,64,64,   // 44-46
        63,         // '/'  (47)
        52,53,54,55,56,57,58,59,60,61, // '0'-'9' (48-57)
        64,64,64,64,64,64,64,          // 58-64
        0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25, // 'A'-'Z'
        64,64,64,64,64,64,             // 91-96
        26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51, // 'a'-'z'
        64,64,64,64,64,                // 123-127
        64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
        64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
        64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
        64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
        64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
        64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
        64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
        64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
    };

    std::vector<uint8_t> result;
    result.reserve((encoded.size() / 4) * 3);

    for (size_t i = 0; i < encoded.size(); i += 4) {
        if (i + 3 >= encoded.size()) break;

        uint8_t a = lookup[static_cast<uint8_t>(encoded[i])];
        uint8_t b = lookup[static_cast<uint8_t>(encoded[i + 1])];
        uint8_t c = lookup[static_cast<uint8_t>(encoded[i + 2])];
        uint8_t d = lookup[static_cast<uint8_t>(encoded[i + 3])];

        if (a == 64 || b == 64) break;  // Invalid

        uint32_t n = (static_cast<uint32_t>(a) << 18) |
                     (static_cast<uint32_t>(b) << 12);

        result.push_back(static_cast<uint8_t>((n >> 16) & 0xFF));

        if (encoded[i + 2] != '=' && c != 64) {
            n |= static_cast<uint32_t>(c) << 6;
            result.push_back(static_cast<uint8_t>((n >> 8) & 0xFF));

            if (encoded[i + 3] != '=' && d != 64) {
                n |= static_cast<uint32_t>(d);
                result.push_back(static_cast<uint8_t>(n & 0xFF));
            }
        }
    }
    return result;
}

}  // namespace hobbycad

#endif  // HOBBYCAD_BASE64_H
