// =====================================================================
//  src/libhobbycad/hobbycad/sketch/property_schema.h — what a properties
//  sheet shows for an entity
//  SPDX-License-Identifier: GPL-3.0-only
//  Part of HobbyCAD (ayourk/hobbycad)
// =====================================================================
//  Capability tier of the front-end support layer. For each entity: which
//  geometry fields exist, what each holds, whether it can be edited, and
//  what its points are called. properties.h applies an edit by the same
//  names, so a sheet built from this and a command line agree.
//
//  Labels are English with a translation context; a front end translates
//  them when it shows them. Which fields a sheet groups together, and in
//  what section, is the front end's arrangement.
// =====================================================================

#ifndef HOBBYCAD_SKETCH_PROPERTY_SCHEMA_H
#define HOBBYCAD_SKETCH_PROPERTY_SCHEMA_H

#include "../core.h"
#include "constraint.h"
#include "entity.h"

#include <string>
#include <vector>

namespace hobbycad {
namespace sketch {

/// What a field holds, which decides how it is shown and read back.
enum class FieldKind {
    Point,    ///< one of the entity's points
    Length,   ///< millimeters
    Angle,    ///< degrees
    Count,    ///< a whole number
    Text,
};

/// A label with an optional number ("Corner %1" and 2).
struct FieldLabel {
    const char* source = "";   ///< English, may hold "%1"
    int number = 0;            ///< what "%1" stands for; 0 when unused
};

/// One field of an entity's geometry.
struct PropertyField {
    /// The name an edit goes through: a setEntityNumber() name, "pointN"
    /// for setEntityPoint(), or "text". Empty for a readout.
    std::string key;
    FieldKind kind = FieldKind::Length;
    FieldLabel label;
    int pointIndex = -1;     ///< for a Point field
    bool editable = false;   ///< before the entity's own locks (fieldLocked)
};

/// The translation context of every label here.
HOBBYCAD_EXPORT const char* propertyLabelContext();

/// The translation context of entityTypeName() (undo.h).
HOBBYCAD_EXPORT const char* entityTypeContext();

/// What each of the entity's points is called, in order.
HOBBYCAD_EXPORT std::vector<FieldLabel> pointLabels(const Entity& e);

/// The geometry fields a sheet shows for the entity, in order.
HOBBYCAD_EXPORT std::vector<PropertyField> entityGeometryFields(const Entity& e);

/// The number a Length, Angle or Count field shows (NaN for the others).
HOBBYCAD_EXPORT double fieldNumber(const Entity& e, const PropertyField& field);

/// The entity point a dimension would drive to own this field (an
/// ellipse's major radius is its axis point 1), or -1.
HOBBYCAD_EXPORT int fieldDrivingPoint(const Entity& e, const PropertyField& field);

/// True when the field cannot be edited now: it is a readout, the entity
/// is projected from another sketch (its source drives it), or a dimension
/// drives it (edit the dimension instead). Takes any container of
/// constraints.
template <typename Constraints>
bool fieldLocked(const Entity& e, const PropertyField& field, const Constraints& constraints)
{
    if (!field.editable || e.projectionSourceId >= 0) return true;
    const int point = fieldDrivingPoint(e, field);
    if (point < 0) return false;
    for (const Constraint& c : constraints) {
        if (dimensionDrivesPoint(c, e.id, point)) return true;
    }
    return false;
}

}  // namespace sketch
}  // namespace hobbycad

#endif  // HOBBYCAD_SKETCH_PROPERTY_SCHEMA_H
