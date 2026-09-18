// =====================================================================
//  src/libhobbycad/sketch/property_schema.cpp — what a properties sheet
//  shows for an entity
//  SPDX-License-Identifier: GPL-3.0-only
//  Part of HobbyCAD (ayourk/hobbycad)
// =====================================================================

#include <hobbycad/sketch/property_schema.h>

#include <hobbycad/geometry/utils.h>
#include <hobbycad/translate.h>

#include <cmath>
#include <limits>

namespace hobbycad {
namespace sketch {

namespace {

/// Every label, named here so the code below reads by meaning.
enum Label {
    Position, Start, End, CornerN, Center, CenterN, MajorAxis, MinorAxis,
    HandleDerived, PointN, Length, Width, Height, Radius, Diameter,
    StartAngle, SweepAngle, Sides, MajorRadius, MinorRadius, ControlPoints,
    TextLabel, FontSize, Rotation,
};

/// The labels, in Label order, marked for extraction one per line (the
/// extraction tool needs the context written out at each).
const char* const kLabels[] = {
    HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchProperties", "Position"),
    HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchProperties", "Start"),
    HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchProperties", "End"),
    HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchProperties", "Corner %1"),
    HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchProperties", "Center"),
    HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchProperties", "Center %1"),
    HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchProperties", "Major axis"),
    HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchProperties", "Minor axis"),
    HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchProperties", "Handle (derived)"),
    HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchProperties", "Point %1"),
    HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchProperties", "Length"),
    HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchProperties", "Width"),
    HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchProperties", "Height"),
    HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchProperties", "Radius"),
    HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchProperties", "Diameter"),
    HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchProperties", "Start Angle"),
    HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchProperties", "Sweep Angle"),
    HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchProperties", "Sides"),
    HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchProperties", "Major Radius"),
    HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchProperties", "Minor Radius"),
    HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchProperties", "Control Points"),
    HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchProperties", "Text"),
    HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchProperties", "Font Size"),
    HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchProperties", "Rotation"),
};

FieldLabel label(Label which, int number = 0)
{
    return FieldLabel{kLabels[which], number};
}

PropertyField pointField(const Entity& e, int index, bool editable)
{
    PropertyField f;
    f.key = "point" + std::to_string(index);
    f.kind = FieldKind::Point;
    f.label = pointLabels(e)[static_cast<std::size_t>(index)];
    f.pointIndex = index;
    f.editable = editable;
    return f;
}

PropertyField numberField(const char* key, FieldKind kind, Label which)
{
    PropertyField f;
    f.key = key;
    f.kind = kind;
    f.label = label(which);
    f.editable = true;
    return f;
}

/// A readout: shown, never edited, and with no setter behind it.
PropertyField readout(FieldKind kind, Label which)
{
    PropertyField f;
    f.kind = kind;
    f.label = label(which);
    return f;
}

bool isConic(const Entity& e)
{
    return e.type == EntityType::Spline && e.conicRho > 0.0 && e.points.size() == 4;
}

}  // namespace

const char* propertyLabelContext()
{
    return "hobbycad::SketchProperties";
}

const char* entityTypeContext()
{
    return "hobbycad::SketchEntity";
}

std::vector<FieldLabel> pointLabels(const Entity& e)
{
    std::vector<FieldLabel> known;
    switch (e.type) {
    case EntityType::Point:
    case EntityType::Text:
        known = {label(Position)};
        break;
    case EntityType::Line:
        known = {label(Start), label(End)};
        break;
    case EntityType::Rectangle:
    case EntityType::Parallelogram:
        for (std::size_t i = 0; i < e.points.size(); ++i) {
            known.push_back(label(CornerN, static_cast<int>(i) + 1));
        }
        break;
    case EntityType::Circle:
    case EntityType::Polygon:
        known = {label(Center)};
        break;
    case EntityType::Arc:
        known = {label(Center), label(Start), label(End)};
        break;
    case EntityType::Slot:
        known = {label(CenterN, 1), label(CenterN, 2)};
        break;
    case EntityType::Ellipse:
        // The axes are real points, so they are named for what they are.
        known = {label(Center), label(MajorAxis), label(MinorAxis)};
        break;
    case EntityType::Spline:
        // A conic's inner control points follow from rho and the apex.
        if (isConic(e)) {
            known = {label(Start), label(HandleDerived), label(HandleDerived), label(End)};
        }
        break;
    case EntityType::Dimension:
        break;
    }
    std::vector<FieldLabel> out;
    for (std::size_t i = 0; i < e.points.size(); ++i) {
        out.push_back(i < known.size() ? known[i] : label(PointN, static_cast<int>(i) + 1));
    }
    return out;
}

std::vector<PropertyField> entityGeometryFields(const Entity& e)
{
    std::vector<PropertyField> out;
    const std::size_t n = e.points.size();
    switch (e.type) {
    case EntityType::Point:
        if (n >= 1) out.push_back(pointField(e, 0, true));
        break;
    case EntityType::Line:
        if (n >= 2) {
            out.push_back(pointField(e, 0, true));
            out.push_back(pointField(e, 1, true));
            out.push_back(numberField("length", FieldKind::Length, Length));
        }
        break;
    case EntityType::Rectangle:
    case EntityType::Parallelogram:
        if (n >= 4) {
            // Four stored corners (turned, or a parallelogram): width and
            // height describe only an axis-aligned rectangle.
            for (int i = 0; i < 4; ++i) out.push_back(pointField(e, i, false));
        } else if (n >= 2) {
            out.push_back(pointField(e, 0, false));
            out.push_back(pointField(e, 1, false));
            out.push_back(numberField("width", FieldKind::Length, Width));
            out.push_back(numberField("height", FieldKind::Length, Height));
        }
        break;
    case EntityType::Circle:
        if (n >= 1) {
            out.push_back(pointField(e, 0, true));
            out.push_back(numberField("radius", FieldKind::Length, Radius));
            out.push_back(numberField("diameter", FieldKind::Length, Diameter));
        }
        break;
    case EntityType::Arc:
        if (n >= 1) {
            out.push_back(pointField(e, 0, false));
            out.push_back(numberField("radius", FieldKind::Length, Radius));
            out.push_back(numberField("startAngle", FieldKind::Angle, StartAngle));
            out.push_back(numberField("sweepAngle", FieldKind::Angle, SweepAngle));
        }
        break;
    case EntityType::Polygon:
        if (n >= 1) {
            out.push_back(pointField(e, 0, true));
            out.push_back(numberField("sides", FieldKind::Count, Sides));
            out.push_back(numberField("radius", FieldKind::Length, Radius));
        }
        break;
    case EntityType::Slot:
        if (n >= 2) {
            out.push_back(pointField(e, 0, true));
            out.push_back(pointField(e, 1, true));
            out.push_back(readout(FieldKind::Length, Length));
            out.push_back(numberField("width", FieldKind::Length, Width));
        }
        break;
    case EntityType::Ellipse:
        if (n >= 1) {
            out.push_back(pointField(e, 0, true));
            out.push_back(numberField("majorRadius", FieldKind::Length, MajorRadius));
            out.push_back(numberField("minorRadius", FieldKind::Length, MinorRadius));
        }
        break;
    case EntityType::Spline:
        if (n >= 1) {
            out.push_back(readout(FieldKind::Count, ControlPoints));
            const bool conic = isConic(e);
            for (std::size_t i = 0; i < n; ++i) {
                const bool derived = conic && (i == 1 || i == 2);
                out.push_back(pointField(e, static_cast<int>(i), !derived));
            }
        }
        break;
    case EntityType::Text:
        if (n >= 1) {
            out.push_back(pointField(e, 0, true));
            PropertyField text;
            text.key = "text";
            text.kind = FieldKind::Text;
            text.label = label(TextLabel);
            text.editable = true;
            out.push_back(text);
            out.push_back(numberField("fontSize", FieldKind::Length, FontSize));
            out.push_back(numberField("textRotation", FieldKind::Angle, Rotation));
        }
        break;
    case EntityType::Dimension:
        break;
    }
    return out;
}

double fieldNumber(const Entity& e, const PropertyField& field)
{
    const double none = std::numeric_limits<double>::quiet_NaN();
    if (field.kind == FieldKind::Point || field.kind == FieldKind::Text) return none;
    const bool twoPoints = e.points.size() >= 2;
    const double span =
        twoPoints ? geometry::length(Point2D(e.points[1]) - Point2D(e.points[0])) : 0.0;
    const std::string& k = field.key;
    if (k.empty()) {
        // Readouts: a slot's center-to-center length, a spline's point count.
        if (e.type == EntityType::Slot) return span;
        if (e.type == EntityType::Spline) return static_cast<double>(e.points.size());
        return none;
    }
    if (k == "length") return span;
    if (k == "width") {
        if (e.type == EntityType::Slot) return e.radius * 2.0;
        return twoPoints ? std::fabs(e.points[1].x - e.points[0].x) : none;
    }
    if (k == "height") return twoPoints ? std::fabs(e.points[1].y - e.points[0].y) : none;
    if (k == "radius") return e.radius;
    if (k == "diameter") return e.radius * 2.0;
    if (k == "startAngle") return e.startAngle;
    if (k == "sweepAngle") return e.sweepAngle;
    if (k == "sides") return static_cast<double>(e.sides);
    if (k == "majorRadius") return e.majorRadius;
    if (k == "minorRadius") return e.minorRadius;
    if (k == "ellipseRotation") return e.ellipseRotation;
    if (k == "ellipseStart") return e.ellipseStart;
    if (k == "ellipseSweep") return e.ellipseSweep;
    if (k == "fontSize") return e.fontSize;
    if (k == "textRotation") return e.textRotation;
    return none;
}

int fieldDrivingPoint(const Entity& e, const PropertyField& field)
{
    if (e.type != EntityType::Ellipse) return -1;
    // A Distance to axis point 1 owns the major radius, to point 2 the minor.
    if (field.key == "majorRadius") return 1;
    if (field.key == "minorRadius") return 2;
    return -1;
}

}  // namespace sketch
}  // namespace hobbycad
