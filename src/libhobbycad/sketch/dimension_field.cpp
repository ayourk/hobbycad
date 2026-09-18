// =====================================================================
//  src/libhobbycad/sketch/dimension_field.cpp — typed-value fields
// =====================================================================
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include <hobbycad/sketch/dimension_field.h>

#include <hobbycad/translate.h>

namespace hobbycad {
namespace sketch {

const char* dimFieldContext()
{
    // The context the labels already had in the catalogs, so their
    // translations stay attached.
    return "hobbycad::SketchCanvas";
}

const char* dimFieldLabel(DimField field)
{
    switch (field) {
    case DimField::Length:
        return HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas", "Length");
    case DimField::Angle:
        return HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas", "Angle");
    case DimField::Radius:
        return HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas", "Radius");
    case DimField::Diameter:
        return HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas", "Diameter");
    case DimField::SweepAngle:
        return HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas", "Sweep Angle");
    case DimField::ChordLength:
        return HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas", "Chord Length");
    case DimField::ChordAngle:
        return HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas", "Chord Angle");
    case DimField::Width:
        return HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas", "Width");
    case DimField::Height:
        return HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas", "Height");
    case DimField::EdgeLength:
        return HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas", "Edge Length");
    case DimField::EdgeAngle:
        return HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas", "Edge Angle");
    case DimField::Edge1:
        return HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas", "Edge1");
    case DimField::Edge1Angle:
        return HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas", "Edge1 Angle");
    case DimField::Edge2:
        return HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas", "Edge2");
    case DimField::Edge2Angle:
        return HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas", "Edge2 Angle");
    case DimField::MajorRadius:
        return HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas", "Major Radius");
    case DimField::MinorRadius:
        return HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas", "Minor Radius");
    case DimField::ArcStart:
        return HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas", "Arc Start");
    case DimField::ArcSweep:
        return HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas", "Arc Sweep");
    case DimField::Span:
        return HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas", "Span");
    case DimField::Rise:
        return HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas", "Rise");
    case DimField::FirstAxis:
        return HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas", "First Axis");
    case DimField::SecondAxis:
        return HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas", "Second Axis");
    }
    return "";
}

bool isAngleDimField(DimField field)
{
    switch (field) {
    case DimField::Angle:
    case DimField::SweepAngle:
    case DimField::ChordAngle:
    case DimField::EdgeAngle:
    case DimField::Edge1Angle:
    case DimField::Edge2Angle:
    case DimField::ArcStart:
    case DimField::ArcSweep:
        return true;
    default:
        return false;
    }
}

std::vector<DimField> allDimFields()
{
    std::vector<DimField> all;
    for (int i = 0; i <= static_cast<int>(DimField::SecondAxis); ++i) {
        all.push_back(static_cast<DimField>(i));
    }
    return all;
}

}  // namespace sketch
}  // namespace hobbycad
