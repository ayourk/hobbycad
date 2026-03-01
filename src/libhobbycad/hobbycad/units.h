// =====================================================================
//  src/libhobbycad/hobbycad/units.h — Unit conversion utilities
// =====================================================================
//
//  Provides unit conversion for length measurements in HobbyCAD.
//
//  **Storage Convention**: All internal measurements and save files use
//  millimeters (mm) as the base unit. Display conversion happens only
//  at the GUI layer using these utilities.
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_UNITS_H
#define HOBBYCAD_UNITS_H

#include "format.h"

#include <array>
#include <cctype>
#include <cmath>
#include <string>

namespace hobbycad {

/// Supported length units.
/// Index values match the unit combo box in Document Settings.
enum class LengthUnit {
    Millimeters = 0,  ///< mm (base unit for storage)
    Centimeters = 1,  ///< cm
    Meters = 2,       ///< m
    Inches = 3,       ///< in
    Feet = 4          ///< ft
};

/// Number of supported length units.
constexpr int LengthUnitCount = 5;

/// Conversion factors from millimeters to each unit.
/// To convert mm to unit: value_in_unit = value_in_mm / factor
/// To convert unit to mm: value_in_mm = value_in_unit * factor
constexpr std::array<double, LengthUnitCount> mmConversionFactors = {{
    1.0,      // mm
    10.0,     // cm (10 mm per cm)
    1000.0,   // m (1000 mm per m)
    25.4,     // in (25.4 mm per inch)
    304.8     // ft (304.8 mm per foot)
}};

/// Unit suffix strings for display.
constexpr std::array<const char*, LengthUnitCount> unitSuffixes = {{
    "mm",
    "cm",
    "m",
    "in",
    "ft"
}};

/// Get the conversion factor for a unit (mm per unit).
/// @param unit The length unit
/// @return Conversion factor (multiply by this to convert to mm)
inline double unitScale(LengthUnit unit)
{
    int idx = static_cast<int>(unit);
    if (idx >= 0 && idx < LengthUnitCount)
        return mmConversionFactors[static_cast<size_t>(idx)];
    return 1.0;  // Default to mm
}

/// Get the display suffix for a unit.
/// @param unit The length unit
/// @return Suffix string (e.g., "mm", "in")
inline const char* unitSuffix(LengthUnit unit)
{
    int idx = static_cast<int>(unit);
    if (idx >= 0 && idx < LengthUnitCount)
        return unitSuffixes[static_cast<size_t>(idx)];
    return "mm";  // Default to mm
}

/// Convert a length value from millimeters to another unit.
/// @param mm Value in millimeters (base unit)
/// @param toUnit Target unit
/// @return Value in the target unit
inline double mmToUnit(double mm, LengthUnit toUnit)
{
    return mm / unitScale(toUnit);
}

/// Convert a length value from a unit to millimeters.
/// @param value Value in the source unit
/// @param fromUnit Source unit
/// @return Value in millimeters
inline double unitToMm(double value, LengthUnit fromUnit)
{
    return value * unitScale(fromUnit);
}

/// Convert a length value between any two units.
/// @param value Value in the source unit
/// @param fromUnit Source unit
/// @param toUnit Target unit
/// @return Value in the target unit
inline double convertLength(double value, LengthUnit fromUnit, LengthUnit toUnit)
{
    if (fromUnit == toUnit)
        return value;
    // Convert to mm first, then to target unit
    double mm = unitToMm(value, fromUnit);
    return mmToUnit(mm, toUnit);
}

/// Parse a unit suffix string to get the unit enum.
/// @param suffix Unit suffix string (case-insensitive)
/// @return The corresponding LengthUnit, or Millimeters if not recognized
inline LengthUnit parseUnitSuffix(const std::string& suffix)
{
    // toLower + trim
    std::string lower;
    for (char c : suffix) {
        if (!std::isspace(static_cast<unsigned char>(c)))
            lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (lower == "mm")
        return LengthUnit::Millimeters;
    if (lower == "cm")
        return LengthUnit::Centimeters;
    if (lower == "m")
        return LengthUnit::Meters;
    if (lower == "in" || lower == "inch" || lower == "inches")
        return LengthUnit::Inches;
    if (lower == "ft" || lower == "foot" || lower == "feet")
        return LengthUnit::Feet;
    return LengthUnit::Millimeters;  // Default
}

/// Get a LengthUnit from an index (0-4).
/// @param index Unit index (matches Document Settings combo box)
/// @return The corresponding LengthUnit
inline LengthUnit lengthUnitFromIndex(int index)
{
    if (index >= 0 && index < LengthUnitCount)
        return static_cast<LengthUnit>(index);
    return LengthUnit::Millimeters;
}

/// Get the index for a LengthUnit (0-4).
/// @param unit The length unit
/// @return Index value (matches Document Settings combo box)
inline int lengthUnitToIndex(LengthUnit unit)
{
    return static_cast<int>(unit);
}

/// Maximum decimal precision for display.
/// Values are formatted with up to this many decimal places, but trailing
/// zeros are trimmed for cleaner display.
constexpr int DisplayPrecision = 4;

/// Decimal precision for internal storage (serialization).
/// Uses 15 decimal places to preserve full double precision (~15-16 significant digits).
constexpr int StoragePrecision = 15;

/// Get the display precision (returns DisplayPrecision).
/// Provided for callers who want to format values manually without trimming zeros.
/// @param unit The length unit (currently unused, all units use same precision)
/// @return Number of decimal places for display (4)
inline int unitDisplayPrecision([[maybe_unused]] LengthUnit unit)
{
    return DisplayPrecision;
}

/// Format a numeric value for display with up to DisplayPrecision decimal places.
/// Trailing zeros are trimmed (e.g., "1.5000" becomes "1.5", "1.0000" becomes "1").
/// @param value The numeric value to format
/// @return Formatted string with trailing zeros removed
inline std::string formatValue(double value)
{
    return hobbycad::formatDouble(value);
}

/// Format a numeric value for file storage with full precision.
/// @param value The numeric value to format
/// @return Formatted string with StoragePrecision decimal places
inline std::string formatStorageValue(double value)
{
    return hobbycad::formatStorageDouble(value);
}

/// Format a length value with unit suffix for display.
/// Converts from mm to the specified unit and appends the suffix.
/// @param mm Value in millimeters
/// @param unit Target display unit
/// @return Formatted string (e.g., "25.4 mm", "1 in")
inline std::string formatValueWithUnit(double mm, LengthUnit unit)
{
    return formatValue(mmToUnit(mm, unit)) + " " + unitSuffix(unit);
}

/// Parse a value string that may include a unit suffix.
/// Returns the value converted to millimeters.
/// Supports formats like "25.4", "25.4mm", "25.4 mm", "1 in", "1in"
/// @param input Input string with optional unit suffix
/// @param defaultUnit Unit to assume if no suffix is present
/// @return Value in millimeters, or 0.0 if parsing fails
inline double parseValueWithUnit(const std::string& input, LengthUnit defaultUnit = LengthUnit::Millimeters)
{
    // Trim whitespace
    size_t start = 0;
    while (start < input.size() && std::isspace(static_cast<unsigned char>(input[start])))
        ++start;
    size_t end = input.size();
    while (end > start && std::isspace(static_cast<unsigned char>(input[end - 1])))
        --end;
    std::string str = input.substr(start, end - start);

    if (str.empty())
        return 0.0;

    // Try to find where the number ends and unit begins
    int unitStart = -1;
    for (size_t i = 0; i < str.size(); ++i) {
        char c = str[i];
        if (!std::isdigit(static_cast<unsigned char>(c)) && c != '.' && c != '-' && c != '+') {
            unitStart = static_cast<int>(i);
            break;
        }
    }

    double value = 0.0;
    LengthUnit unit = defaultUnit;

    if (unitStart > 0) {
        // Has both number and unit
        value = std::stod(str.substr(0, static_cast<size_t>(unitStart)));
        unit = parseUnitSuffix(str.substr(static_cast<size_t>(unitStart)));
    } else if (unitStart == 0) {
        // Starts with non-digit, invalid
        return 0.0;
    } else {
        // No unit, just a number
        value = std::stod(str);
    }

    return unitToMm(value, unit);
}

/// Check if a unit is metric (mm, cm, m).
/// @param unit The length unit
/// @return True if metric, false if imperial
inline bool isMetric(LengthUnit unit)
{
    return unit == LengthUnit::Millimeters ||
           unit == LengthUnit::Centimeters ||
           unit == LengthUnit::Meters;
}

/// Check if a unit is imperial (in, ft).
/// @param unit The length unit
/// @return True if imperial, false if metric
inline bool isImperial(LengthUnit unit)
{
    return unit == LengthUnit::Inches || unit == LengthUnit::Feet;
}

// ============================================================================
// Angle utilities
// ============================================================================

/// Pi constant for angle conversions.
constexpr double Pi = 3.14159265358979323846;

/// Convert degrees to radians.
/// @param degrees Angle in degrees
/// @return Angle in radians
inline double degreesToRadians(double degrees)
{
    return degrees * Pi / 180.0;
}

/// Convert radians to degrees.
/// @param radians Angle in radians
/// @return Angle in degrees
inline double radiansToDegrees(double radians)
{
    return radians * 180.0 / Pi;
}

/// Normalize an angle to the range [0, 360).
/// @param degrees Angle in degrees (any value)
/// @return Angle normalized to [0, 360)
inline double normalizeAngle360(double degrees)
{
    double result = std::fmod(degrees, 360.0);
    if (result < 0)
        result += 360.0;
    return result;
}

/// Normalize an angle to the range [-180, 180).
/// @param degrees Angle in degrees (any value)
/// @return Angle normalized to [-180, 180)
inline double normalizeAngle180(double degrees)
{
    double result = std::fmod(degrees + 180.0, 360.0);
    if (result < 0)
        result += 360.0;
    return result - 180.0;
}

/// Format an angle for display with degree symbol.
/// Uses formatValue() for consistent precision and trailing zero trimming.
/// @param degrees Angle in degrees
/// @return Formatted string (e.g., "45°", "22.5°")
inline std::string formatAngle(double degrees)
{
    return formatValue(degrees) + "\xC2\xB0";  // UTF-8 degree sign °
}

/// Parse a degrees-minutes-seconds string into decimal degrees.
/// Supported formats: 45°30'15", 45°30', 45°, -45°30'15.5"
/// Also accepts Unicode prime ′ (U+2032) and double prime ″ (U+2033).
/// The input must contain at least one DMS indicator (°, ', or ") to be
/// recognized. Returns false if the input is not valid DMS format, allowing
/// callers to fall through to other parsers (expression evaluator, toDouble).
/// @param input  The string to parse
/// @param degrees Output: the angle in decimal degrees
/// @return true if parsed successfully
inline bool parseDMS(const std::string& input, double& degrees)
{
    // Trim whitespace
    size_t start = 0;
    while (start < input.size() && std::isspace(static_cast<unsigned char>(input[start])))
        ++start;
    size_t end = input.size();
    while (end > start && std::isspace(static_cast<unsigned char>(input[end - 1])))
        --end;
    std::string s = input.substr(start, end - start);

    if (s.empty())
        return false;

    // Must contain at least one DMS indicator to be recognized as DMS
    // ° is UTF-8 0xC2 0xB0, ′ is UTF-8 0xE2 0x80 0xB2, ″ is UTF-8 0xE2 0x80 0xB3
    bool hasDeg = s.find("\xC2\xB0") != std::string::npos;                                  // °
    bool hasMin = s.find('\'') != std::string::npos ||
                  s.find("\xE2\x80\xB2") != std::string::npos;                               // ' or ′
    bool hasSec = s.find('"') != std::string::npos ||
                  s.find("\xE2\x80\xB3") != std::string::npos;                               // " or ″
    if (!hasDeg && !hasMin && !hasSec)
        return false;

    size_t pos = 0;

    // Optional sign
    double sign = 1.0;
    if (pos < s.size() && s[pos] == '-') {
        sign = -1.0;
        ++pos;
    } else if (pos < s.size() && s[pos] == '+') {
        ++pos;
    }

    auto skipSpaces = [&]() {
        while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos])))
            ++pos;
    };

    auto readNumber = [&]() -> double {
        skipSpaces();
        size_t numStart = pos;
        while (pos < s.size() && (std::isdigit(static_cast<unsigned char>(s[pos])) || s[pos] == '.'))
            ++pos;
        if (pos == numStart)
            return 0.0;
        return std::stod(s.substr(numStart, pos - numStart));
    };

    // Try to skip a degree/minute/second marker at current position.
    // Returns true if a marker was found and skipped.
    auto skipDegreeSign = [&]() -> bool {
        if (pos >= s.size())
            return false;
        // Check for UTF-8 ° (0xC2 0xB0)
        if (pos + 1 < s.size() &&
            static_cast<unsigned char>(s[pos]) == 0xC2 &&
            static_cast<unsigned char>(s[pos + 1]) == 0xB0) {
            pos += 2;
            return true;
        }
        return false;
    };

    auto skipMinuteSign = [&]() -> bool {
        if (pos >= s.size())
            return false;
        // Check for UTF-8 ′ (0xE2 0x80 0xB2)
        if (pos + 2 < s.size() &&
            static_cast<unsigned char>(s[pos]) == 0xE2 &&
            static_cast<unsigned char>(s[pos + 1]) == 0x80 &&
            static_cast<unsigned char>(s[pos + 2]) == 0xB2) {
            pos += 3;
            return true;
        }
        // Check for ASCII '
        if (s[pos] == '\'') {
            ++pos;
            return true;
        }
        return false;
    };

    auto skipSecondSign = [&]() -> bool {
        if (pos >= s.size())
            return false;
        // Check for UTF-8 ″ (0xE2 0x80 0xB3)
        if (pos + 2 < s.size() &&
            static_cast<unsigned char>(s[pos]) == 0xE2 &&
            static_cast<unsigned char>(s[pos + 1]) == 0x80 &&
            static_cast<unsigned char>(s[pos + 2]) == 0xB3) {
            pos += 3;
            return true;
        }
        // Check for ASCII "
        if (s[pos] == '"') {
            ++pos;
            return true;
        }
        return false;
    };

    // Parse degrees
    double deg = readNumber();
    skipDegreeSign();  // ° (optional — might be absent if only ' or " used)

    // Parse minutes
    skipSpaces();
    double min = 0.0;
    if (pos < s.size() && (std::isdigit(static_cast<unsigned char>(s[pos])) || s[pos] == '.')) {
        min = readNumber();
    }
    skipMinuteSign();  // ' or ′

    // Parse seconds
    skipSpaces();
    double sec = 0.0;
    if (pos < s.size() && (std::isdigit(static_cast<unsigned char>(s[pos])) || s[pos] == '.')) {
        sec = readNumber();
    }
    skipSecondSign();  // " or ″

    // Should be at end of string (after optional whitespace)
    skipSpaces();
    if (pos != s.size())
        return false;  // Trailing junk — not a clean DMS value

    degrees = sign * (deg + min / 60.0 + sec / 3600.0);
    return true;
}

/// Normalize an angle in radians to the range [0, 2*Pi).
/// @param radians Angle in radians (any value)
/// @return Angle normalized to [0, 2*Pi)
inline double normalizeRadians2Pi(double radians)
{
    double result = std::fmod(radians, 2.0 * Pi);
    if (result < 0)
        result += 2.0 * Pi;
    return result;
}

/// Normalize an angle in radians to the range [-Pi, Pi).
/// @param radians Angle in radians (any value)
/// @return Angle normalized to [-Pi, Pi)
inline double normalizeRadiansPi(double radians)
{
    double result = std::fmod(radians + Pi, 2.0 * Pi);
    if (result < 0)
        result += 2.0 * Pi;
    return result - Pi;
}

/// Compute the angle in degrees from a delta (atan2 wrapper).
/// @param dy Y component of the vector
/// @param dx X component of the vector
/// @return Angle in degrees [-180, 180)
inline double atan2Degrees(double dy, double dx)
{
    return std::atan2(dy, dx) * 180.0 / Pi;
}

/// Compute the angle in radians from a delta (atan2 wrapper, for convenience).
/// @param dy Y component of the vector
/// @param dx X component of the vector
/// @return Angle in radians [-Pi, Pi)
inline double atan2Radians(double dy, double dx)
{
    return std::atan2(dy, dx);
}

// ============================================================================
// Display-friendly number rounding (1-2-5 sequence)
// ============================================================================

/// Round a value up to the nearest "nice" number in the 1-2-5 sequence.
/// Useful for scale bars, grid spacing, axis labels, and dimension rounding.
/// @param value The value to round (must be positive)
/// @return The nearest nice number >= value
inline double niceNumber(double value)
{
    double exponent = std::floor(std::log10(value));
    double fraction = value / std::pow(10.0, exponent);

    double nice;
    if (fraction < 1.5)
        nice = 1.0;
    else if (fraction < 3.5)
        nice = 2.0;
    else if (fraction < 7.5)
        nice = 5.0;
    else
        nice = 10.0;

    return nice * std::pow(10.0, exponent);
}

/// Step down to the next smaller "nice" number in the 1-2-5 sequence.
/// @param value The value to step down from (must be positive)
/// @return The largest nice number < value
inline double niceNumberBelow(double value)
{
    double exponent = std::floor(std::log10(value));
    double fraction = value / std::pow(10.0, exponent);

    double nice;
    if (fraction > 5.5)
        nice = 5.0;
    else if (fraction > 2.5)
        nice = 2.0;
    else if (fraction > 1.5)
        nice = 1.0;
    else {
        nice = 5.0;
        exponent -= 1.0;
    }

    return nice * std::pow(10.0, exponent);
}

}  // namespace hobbycad

#endif  // HOBBYCAD_UNITS_H
