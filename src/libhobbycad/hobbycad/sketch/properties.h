// =====================================================================
//  src/libhobbycad/hobbycad/sketch/properties.h — editing an entity property by name
//  SPDX-License-Identifier: GPL-3.0-only
//  Part of HobbyCAD (ayourk/hobbycad)
// =====================================================================
//  The model half of "edit an entity property by name": what a properties
//  panel, a command line or a script sends after it has read the user's
//  text. The rules per property (positivity, ranges, what else moves when
//  a value changes) live here once, so every front end agrees.
// =====================================================================

#ifndef HOBBYCAD_SKETCH_PROPERTIES_H
#define HOBBYCAD_SKETCH_PROPERTIES_H

#include "../core.h"
#include "../types.h"
#include "entity.h"

#include <string>

namespace hobbycad {
namespace sketch {

enum class PropertyProblem {
    None,
    UnknownProperty,   ///< no such numeric property
    NotANumber,        ///< the value is NaN or infinite
    NotPositive,       ///< the property needs a value above zero
    OutOfRange,        ///< outside the property's range (sides: 3..100)
    NoSuchPoint,       ///< point index outside the entity's points
    Degenerate         ///< the entity cannot take it (a zero-length line's length)
};

struct PropertyEdit {
    PropertyProblem problem = PropertyProblem::None;
    bool changed = false;
    int editedPointIndex = -1;   ///< the point a setEntityPoint moved, for a pinned solve
};

/// Set a numeric property: "radius", "diameter", "startAngle",
/// "sweepAngle", "length", "width", "height", "sides", "majorRadius",
/// "minorRadius", "ellipseRotation", "ellipseStart", "ellipseSweep",
/// "fontSize", "textRotation". Lengths in millimeters, angles in degrees.
/// Dependent geometry follows: an arc's endpoints resync from its radius
/// and angles, a circle rescales about its center, an ellipse's axis points
/// follow its numbers, a text's rotation handle is recomputed. Length keeps
/// the line's direction and first point; width and height move a
/// rectangle's second corner on the side it already is; a slot's width is
/// twice its radius. An ellipse edit reports the axis point it moved in
/// editedPointIndex (1 for the major axis and rotation, 2 for the minor).
HOBBYCAD_EXPORT PropertyEdit setEntityNumber(Entity& e, const std::string& property, double value);

/// Move one of the entity's points.
HOBBYCAD_EXPORT PropertyEdit setEntityPoint(Entity& e, int index, const Point2D& p);

/// Set a text entity's caption (the rotation handle follows).
HOBBYCAD_EXPORT PropertyEdit setEntityText(Entity& e, const std::string& text);

/// True for the names setEntityNumber understands.
HOBBYCAD_EXPORT bool isNumericEntityProperty(const std::string& property);

}  // namespace sketch
}  // namespace hobbycad

#endif  // HOBBYCAD_SKETCH_PROPERTIES_H
