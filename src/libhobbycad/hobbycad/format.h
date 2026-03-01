// =====================================================================
//  src/libhobbycad/hobbycad/format.h — String formatting utilities
// =====================================================================
//
//  Qt-free string formatting functions to replace QString::number(),
//  QString::arg(), and formatValue().
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_FORMAT_H
#define HOBBYCAD_FORMAT_H

#include <cstdio>
#include <string>

namespace hobbycad {

// =====================================================================
//  Number Formatting
// =====================================================================

/// Format a double with trailing zeros trimmed.
/// Default precision is 4 decimal places.
/// Examples: 1.5000 → "1.5", 1.0000 → "1", 3.1416 → "3.1416"
inline std::string formatDouble(double value, int precision = 4)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", precision, value);
    std::string str(buf);

    // Trim trailing zeros after decimal point
    auto dot = str.find('.');
    if (dot != std::string::npos) {
        auto last = str.find_last_not_of('0');
        if (last != std::string::npos && last > dot) {
            str.erase(last + 1);
        } else if (last == dot) {
            str.erase(dot);  // Remove the dot too
        }
    }
    return str;
}

/// Format a double for file storage with full precision (no trimming).
/// Default precision is 15 decimal places.
inline std::string formatStorageDouble(double value, int precision = 15)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", precision, value);
    return std::string(buf);
}

// =====================================================================
//  Printf-style Formatting
// =====================================================================

/// Format a string using printf-style syntax.
/// Returns a std::string. Buffer auto-sizes for strings up to 4KB.
template<typename... Args>
std::string format(const char* fmt, Args... args)
{
    // First, determine the required size
    int needed = std::snprintf(nullptr, 0, fmt, args...);
    if (needed <= 0)
        return {};
    std::string result(static_cast<size_t>(needed), '\0');
    std::snprintf(result.data(), static_cast<size_t>(needed) + 1, fmt, args...);
    return result;
}

}  // namespace hobbycad

#endif  // HOBBYCAD_FORMAT_H
