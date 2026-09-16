// =====================================================================
//  src/libhobbycad/hobbycad/strutil.h — Qt-free string manipulation
// =====================================================================
//
//  What QString gave the command layer, without Qt: case folding,
//  splitting, joining, trimming, padding and positional substitution.
//
//  The behavior here deliberately reproduces Qt's, because the command
//  layer's messages were written against it and its tests assert on
//  them. QString::arg(double) formats with printf's "%g", six
//  significant digits, so numToString does the same; the one difference
//  is negative zero, which Qt prints as "0".
//
//  format.h keeps printf-style formatting and display number
//  formatting; this header is the string handling around it.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_STRUTIL_H
#define HOBBYCAD_STRUTIL_H

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace hobbycad {

// =====================================================================
//  Number text
// =====================================================================

/// Text for a double as QString::arg(double) and QString::number(double)
/// write it: "%g", six significant digits. Qt prints negative zero as
/// "0" where printf prints "-0", and a coordinate that rounds to zero
/// reading as negative is a real complaint, so zero is normalized.
inline std::string numToString(double value)
{
    if (value == 0.0) return "0";
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%g", value);
    return std::string(buf);
}

/// Text for a double in fixed notation, as QString::number(v, 'f', n).
inline std::string numToStringFixed(double value, int decimals)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, value);
    return std::string(buf);
}

inline std::string numToString(int value)                { return std::to_string(value); }
inline std::string numToString(long value)               { return std::to_string(value); }
inline std::string numToString(long long value)          { return std::to_string(value); }
inline std::string numToString(unsigned value)           { return std::to_string(value); }
inline std::string numToString(unsigned long value)      { return std::to_string(value); }
inline std::string numToString(unsigned long long value) { return std::to_string(value); }
inline std::string numToString(char value)               { return std::string(1, value); }
inline std::string numToString(const char* value)        { return value ? std::string(value) : std::string(); }
inline std::string numToString(std::string value)        { return value; }

// =====================================================================
//  Case, prefixes, membership
// =====================================================================

inline std::string toLower(std::string s)
{
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

inline std::string toUpper(std::string s)
{
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

inline bool startsWith(const std::string& s, const std::string& prefix)
{
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

/// Prefix match ignoring ASCII case, for completing a command the user
/// typed in whatever case they liked.
inline bool startsWithIgnoreCase(const std::string& s, const std::string& prefix)
{
    if (s.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(s[i]))
            != std::tolower(static_cast<unsigned char>(prefix[i]))) return false;
    }
    return true;
}

inline bool endsWith(const std::string& s, const std::string& suffix)
{
    return s.size() >= suffix.size()
        && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

inline bool contains(const std::string& s, const std::string& what)
{
    return s.find(what) != std::string::npos;
}

inline bool contains(const std::string& s, char what)
{
    return s.find(what) != std::string::npos;
}

inline bool startsWith(const std::string& s, char c)
{
    return !s.empty() && s.front() == c;
}

inline bool endsWith(const std::string& s, char c)
{
    return !s.empty() && s.back() == c;
}

// =====================================================================
//  Sublists: what QStringList::mid() did
// =====================================================================

/// Elements from `pos` onward, or `count` of them. Past the end yields
/// an empty list rather than throwing, which is what the command
/// dispatcher relies on when a command is typed with no arguments.
template <typename T>
inline std::vector<T> slice(const std::vector<T>& v, size_t pos,
                            size_t count = static_cast<size_t>(-1))
{
    if (pos >= v.size()) return {};
    const size_t n = std::min(count, v.size() - pos);
    return std::vector<T>(v.begin() + static_cast<long>(pos),
                          v.begin() + static_cast<long>(pos + n));
}

// =====================================================================
//  Trimming and whitespace
// =====================================================================

/// Leading and trailing whitespace removed. isspace, so it also covers
/// the vertical tab and form feed a " \t\r\n" set misses.
inline std::string trim(const std::string& s)
{
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

/// Trimmed, with every internal run of whitespace collapsed to one
/// space, as QString::simplified() does.
inline std::string simplify(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    bool pending = false;
    for (char c : s) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!out.empty()) pending = true;
        } else {
            if (pending) { out += ' '; pending = false; }
            out += c;
        }
    }
    return out;
}

// =====================================================================
//  Text to number: what QString::toInt() and toDouble() did
// =====================================================================
//
//  Measured against Qt rather than assumed. Surrounding whitespace is
//  allowed, any trailing character is not: "5x", "0x10" and "1,5" all
//  fail, " 5 " does not. A failed parse answers 0 and sets ok to false,
//  which is what the command layer checks before complaining to the
//  user about what they typed.

inline int toInt(const std::string& s, bool* ok = nullptr, int base = 10)
{
    if (ok) *ok = false;
    const std::string t = trim(s);
    if (t.empty()) return 0;
    errno = 0;
    char* end = nullptr;
    const long v = std::strtol(t.c_str(), &end, base);
    if (end != t.c_str() + t.size()) return 0;   // trailing characters
    if (errno == ERANGE || v < INT_MIN || v > INT_MAX) return 0;
    if (ok) *ok = true;
    return static_cast<int>(v);
}

inline double toDouble(const std::string& s, bool* ok = nullptr)
{
    if (ok) *ok = false;
    const std::string t = trim(s);
    if (t.empty()) return 0.0;
    errno = 0;
    char* end = nullptr;
    const double v = std::strtod(t.c_str(), &end);
    if (end != t.c_str() + t.size()) return 0.0;
    if (errno == ERANGE) return 0.0;
    if (ok) *ok = true;
    return v;
}

// =====================================================================
//  Splitting and joining
// =====================================================================

/// Split on a separator character. Empty fields are kept unless asked
/// otherwise, matching Qt::KeepEmptyParts and Qt::SkipEmptyParts.
inline std::vector<std::string> split(const std::string& s, char sep,
                                      bool keepEmpty = true)
{
    std::vector<std::string> parts;
    std::string current;
    for (char c : s) {
        if (c == sep) {
            if (keepEmpty || !current.empty()) parts.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    if (keepEmpty || !current.empty()) parts.push_back(current);
    return parts;
}

/// Split on runs of whitespace, dropping empty fields: what
/// split(QRegularExpression("\\s+"), Qt::SkipEmptyParts) did.
inline std::vector<std::string> splitWhitespace(const std::string& s)
{
    std::vector<std::string> parts;
    std::string current;
    for (char c : s) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!current.empty()) { parts.push_back(current); current.clear(); }
        } else {
            current += c;
        }
    }
    if (!current.empty()) parts.push_back(current);
    return parts;
}

inline std::string join(const std::vector<std::string>& parts,
                        const std::string& sep)
{
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) out += sep;
        out += parts[i];
    }
    return out;
}

inline std::string join(const std::vector<std::string>& parts, char sep)
{
    return join(parts, std::string(1, sep));
}

// =====================================================================
//  Padding and replacement
// =====================================================================

inline std::string padLeft(const std::string& s, size_t width, char fill = ' ')
{
    return s.size() >= width ? s : std::string(width - s.size(), fill) + s;
}

inline std::string padRight(const std::string& s, size_t width, char fill = ' ')
{
    return s.size() >= width ? s : s + std::string(width - s.size(), fill);
}

/// Qt's field-width convention, so a converted call site keeps its sign:
/// a positive width right justifies, a negative width left justifies,
/// and text at or over the width is untouched.
inline std::string pad(const std::string& s, int fieldWidth, char fill = ' ')
{
    if (fieldWidth == 0) return s;
    if (fieldWidth > 0) return padLeft(s, static_cast<size_t>(fieldWidth), fill);
    return padRight(s, static_cast<size_t>(-fieldWidth), fill);
}

inline std::string replaceAll(std::string s, const std::string& from,
                              const std::string& to)
{
    if (from.empty()) return s;
    size_t at = 0;
    while ((at = s.find(from, at)) != std::string::npos) {
        s.replace(at, from.size(), to);
        at += to.size();
    }
    return s;
}

inline std::string removeAll(const std::string& s, const std::string& what)
{
    return replaceAll(s, what, std::string());
}

inline std::string removeAll(const std::string& s, char what)
{
    return removeAll(s, std::string(1, what));
}

/// Membership in a list, which QStringList::contains() gave.
inline bool contains(const std::vector<std::string>& v, const std::string& what)
{
    return std::find(v.begin(), v.end(), what) != v.end();
}

/// The element at `i`, or an empty string when there is none: what
/// QStringList::value() did. The command layer leans on this to read an
/// argument that may simply not have been typed.
inline std::string valueAt(const std::vector<std::string>& v, size_t i)
{
    return i < v.size() ? v[i] : std::string();
}

// =====================================================================
//  Positional substitution: what QString::arg() did
// =====================================================================

/// Replace %1 through %99 with the given values.
///
/// One left to right pass over the format, so a value containing "%2"
/// is never itself substituted into. Qt behaves the same way for its
/// multi-argument arg(), and a single pass makes it true for every
/// call. Every occurrence of a placeholder is replaced, as Qt does, and
/// a placeholder with no value is left standing rather than blanked, so
/// a miscounted call shows up in the output instead of vanishing.
inline std::string substList(const std::string& fmt,
                             const std::vector<std::string>& values)
{
    std::string out;
    out.reserve(fmt.size() + 32);
    for (size_t i = 0; i < fmt.size(); ++i) {
        if (fmt[i] == '%' && i + 1 < fmt.size()
            && fmt[i + 1] >= '1' && fmt[i + 1] <= '9') {
            // Qt reads a second digit when one follows, so "%10" is the
            // tenth value and not the first followed by a zero. Matching
            // that here keeps a future message with ten placeholders from
            // quietly meaning something else.
            size_t digits = 1;
            int number = fmt[i + 1] - '0';
            if (i + 2 < fmt.size() && fmt[i + 2] >= '0' && fmt[i + 2] <= '9') {
                number = number * 10 + (fmt[i + 2] - '0');
                digits = 2;
            }
            const size_t index = static_cast<size_t>(number - 1);
            if (index < values.size()) {
                out += values[index];
                i += digits;
                continue;
            }
        }
        out += fmt[i];
    }
    return out;
}

template <typename... Args>
inline std::string subst(const std::string& fmt, Args&&... args)
{
    return substList(fmt, { numToString(std::forward<Args>(args))... });
}

}  // namespace hobbycad

#endif  // HOBBYCAD_STRUTIL_H
