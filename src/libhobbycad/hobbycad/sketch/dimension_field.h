// =====================================================================
//  src/libhobbycad/hobbycad/sketch/dimension_field.h — typed-value fields
// =====================================================================
//
//  A drawing tool offers fields a value can be typed into while it is
//  placing an entity (a radius, a width, a sweep angle). What a locked
//  field means, which constraint it becomes, is decided by the field's
//  identity, never by its label: the label is translated for display, so
//  matching on it breaks in every language but English.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_SKETCH_DIMENSION_FIELD_H
#define HOBBYCAD_SKETCH_DIMENSION_FIELD_H

#include "../core.h"

#include <utility>
#include <vector>

namespace hobbycad {
namespace sketch {

/// Every dimension field a drawing tool can offer.
enum class DimField {
    Length,
    Angle,
    Radius,
    Diameter,
    SweepAngle,
    ChordLength,
    ChordAngle,
    Width,
    Height,
    EdgeLength,
    EdgeAngle,
    Edge1,
    Edge1Angle,
    Edge2,
    Edge2Angle,
    MajorRadius,
    MinorRadius,
    ArcStart,
    ArcSweep,
    Span,
    Rise,
    FirstAxis,
    SecondAxis,
};

/// The field's label in English, for display after translation with the
/// context dimFieldContext().
HOBBYCAD_EXPORT const char* dimFieldLabel(DimField field);

/// The translation context the labels are extracted under.
HOBBYCAD_EXPORT const char* dimFieldContext();

/// True when the field holds an angle in degrees; false for a length in mm.
HOBBYCAD_EXPORT bool isAngleDimField(DimField field);

/// Every field, in declaration order (for tests and front ends).
HOBBYCAD_EXPORT std::vector<DimField> allDimFields();

/// Fields a user locked while placing an entity, with their values (mm or
/// degrees), in the order they were locked.
using LockedDims = std::vector<std::pair<DimField, double>>;

}  // namespace sketch
}  // namespace hobbycad

#endif  // HOBBYCAD_SKETCH_DIMENSION_FIELD_H
