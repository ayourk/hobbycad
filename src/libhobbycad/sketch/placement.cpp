// =====================================================================
//  src/libhobbycad/sketch/placement.cpp — placing an entity by clicks
// =====================================================================
//
//  What every placement shares: the stage tables (prompts, fields), the
//  dispatch to each tool's rules, the Bezier pen, and the shape builders.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include "placement_detail.h"

#include <hobbycad/geometry/intersections.h>
#include <hobbycad/geometry/utils.h>
#include <hobbycad/sketch/bezier.h>
#include <hobbycad/sketch/dimension_input.h>
#include <hobbycad/sketch/handles.h>
#include <hobbycad/sketch/operations.h>
#include <hobbycad/sketch/queries.h>
#include <hobbycad/translate.h>
#include <hobbycad/units.h>

#include <cmath>

namespace hobbycad {
namespace sketch {

using namespace placement_detail;

// ---- Which placement --------------------------------------------------

bool isPlacementTool(SketchTool tool)
{
    switch (tool) {
    case SketchTool::Line:
    case SketchTool::Rectangle:
    case SketchTool::Circle:
    case SketchTool::Arc:
    case SketchTool::Spline:
    case SketchTool::Polygon:
    case SketchTool::Slot:
    case SketchTool::Ellipse:
        return true;
    default:
        return false;
    }
}

namespace {

EllipsePlacement ellipsePlacementOf(CreationMode m)
{
    switch (m) {
    case CreationMode::EllipseThreePoint:   return EllipsePlacement::ThreePoint;
    case CreationMode::EllipseArc:          return EllipsePlacement::Arc;
    case CreationMode::EllipseSpanRiseArc:  return EllipsePlacement::SpanRise;
    case CreationMode::EllipseCornerArc:    return EllipsePlacement::Corner;
    case CreationMode::EllipseEndpointsArc: return EllipsePlacement::Endpoints;
    default:                                return EllipsePlacement::CenterAxes;
    }
}

}  // namespace

int placementClicks(PlacementKind kind)
{
    const CreationMode m = kind.mode;
    switch (kind.tool) {
    case SketchTool::Line:
        return 2;
    case SketchTool::Rectangle:
        return (m == CreationMode::RectThreePoint || m == CreationMode::RectParallelogram) ? 3 : 2;
    case SketchTool::Circle:
        return m == CreationMode::CircleThreePoint ? 3 : 2;
    case SketchTool::Arc:
        return m == CreationMode::ArcTangent ? 2 : 3;
    case SketchTool::Slot:
        return (m == CreationMode::SlotArcRadius || m == CreationMode::SlotArcEnds) ? 3 : 2;
    case SketchTool::Polygon:
        return m == CreationMode::PolygonFreeform ? 0 : 2;
    case SketchTool::Ellipse:
        return ellipsePlacementClicks(ellipsePlacementOf(m));
    case SketchTool::Spline:
        return m == CreationMode::SplineConic ? 4 : 0;
    default:
        return 0;
    }
}

int placementTargets(PlacementKind kind)
{
    switch (kind.tool) {
    case SketchTool::Line:
        return kind.mode == CreationMode::LineTangent ? 1 : 0;
    case SketchTool::Arc:
        return kind.mode == CreationMode::ArcTangent ? 1 : 0;
    case SketchTool::Circle:
        if (kind.mode == CreationMode::CircleTwoTangent) return 2;
        if (kind.mode == CreationMode::CircleThreeTangent) return 3;
        return 0;
    default:
        return 0;
    }
}

EntityType placementEntityType(PlacementKind kind)
{
    switch (kind.tool) {
    case SketchTool::Rectangle:
        return kind.mode == CreationMode::RectParallelogram ? EntityType::Parallelogram
                                                           : EntityType::Rectangle;
    case SketchTool::Circle:  return EntityType::Circle;
    case SketchTool::Arc:     return EntityType::Arc;
    case SketchTool::Slot:    return EntityType::Slot;
    case SketchTool::Polygon: return EntityType::Polygon;
    case SketchTool::Ellipse: return EntityType::Ellipse;
    case SketchTool::Spline:  return EntityType::Spline;
    default:                  return EntityType::Line;
    }
}

// ---- Where the placement stands ----------------------------------------

const char* placementContext()
{
    return "hobbycad::SketchCanvas";
}

namespace {

const char* linePrompt(CreationMode m, const PlacementStage& s)
{
    // Tangent line is a 3-stage tool: (1) select the arc/circle to be tangent
    // to, (2) place the start point, (3) place the end point.
    if (m == CreationMode::LineTangent) {
        if (s.targets == 0) {
            return HOBBYCAD_TRANSLATE_NOOP(
                "hobbycad::SketchCanvas",
                "Tangent line: click the arc or circle to be tangent to");
        }
        if (s.placed == 0) {
            return HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas",
                                           "Tangent line: click the start point");
        }
        return HOBBYCAD_TRANSLATE_NOOP(
            "hobbycad::SketchCanvas",
            "Tangent line: click the end point  (snaps to the tangent)");
    }
    if (s.placed < 1) {
        return HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas", "Line: click the start point");
    }
    if (s.chainsArc) {
        return HOBBYCAD_TRANSLATE_NOOP(
            "hobbycad::SketchCanvas",
            "Line: click for the next segment, or drag for a tangent arc");
    }
    return HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas",
                                   "Line: click the end point, or type a length");
}

const char* rectanglePrompt(CreationMode m, const PlacementStage& s)
{
    switch (m) {
    case CreationMode::RectCenter:
        return stageText({
            HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas", "Rectangle: click the center"),
            HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas",
                                    "Rectangle: click a corner, or type a width"),
        }, s.placed);
    case CreationMode::RectThreePoint:
        return stageText({
            HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas",
                                    "Rectangle (3-point): click the first corner"),
            HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas",
                                    "Rectangle (3-point): click the end of the first edge"),
            HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas",
                                    "Rectangle (3-point): click to set the width, or type one"),
        }, s.placed);
    case CreationMode::RectParallelogram:
        return stageText({
            HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas",
                                    "Parallelogram: click the first corner"),
            HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas",
                                    "Parallelogram: click the end of the first edge"),
            HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas",
                                    "Parallelogram: click the end of the second edge"),
        }, s.placed);
    default:
        return stageText({
            HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas", "Rectangle: click the first corner"),
            HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas",
                                    "Rectangle: click the opposite corner, or type a width"),
        }, s.placed);
    }
}

const char* circlePrompt(CreationMode m, const PlacementStage& s)
{
    switch (m) {
    case CreationMode::CircleTwoPoint:
        return stageText({
            HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas",
                                    "Circle (2-point): click one end of the diameter"),
            HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas",
                                    "Circle (2-point): click the other end, or type a diameter"),
        }, s.placed);
    case CreationMode::CircleThreePoint:
        return stageText({
            HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas",
                                    "Circle (3-point): click the first point"),
            HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas",
                                    "Circle (3-point): click the second point"),
            HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas",
                                    "Circle (3-point): click the third point, or type a radius"),
        }, s.placed);
    case CreationMode::CircleTwoTangent:
        if (s.targets < 2) {
            return HOBBYCAD_TRANSLATE_NOOP(
                "hobbycad::SketchCanvas",
                "Tangent circle: click 2 curves to be tangent to (%1 of 2)");
        }
        return HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas", "Tangent circle: click to place");
    case CreationMode::CircleThreeTangent:
        if (s.targets < 3) {
            return HOBBYCAD_TRANSLATE_NOOP(
                "hobbycad::SketchCanvas",
                "Tangent circle: click 3 curves to be tangent to (%1 of 3)");
        }
        return HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas", "Tangent circle: click to place");
    default:
        return stageText({
            HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas", "Circle: click the center"),
            HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas",
                                    "Circle: click to set the radius, or type one"),
        }, s.placed);
    }
}

const char* arcPrompt(CreationMode m, const PlacementStage& s)
{
    switch (m) {
    case CreationMode::ArcCenterStartEnd:
        return stageText({
            HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas", "Arc: click the center"),
            HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas",
                                    "Arc: click the start point, or type a radius"),
            HOBBYCAD_TRANSLATE_NOOP(
                "hobbycad::SketchCanvas",
                "Arc: click the end point, or type a sweep angle  (Shift = long way round)"),
        }, s.placed);
    case CreationMode::ArcStartEndRadius:
        return stageText({
            HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas", "Arc: click the start point"),
            HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas",
                                    "Arc: click the end point, or type a chord length"),
            HOBBYCAD_TRANSLATE_NOOP(
                "hobbycad::SketchCanvas",
                "Arc: set the bulge, or type a sweep angle  (Shift = long way round)"),
        }, s.placed);
    case CreationMode::ArcTangent:
        if (s.targets == 0) {
            return HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas",
                                           "Tangent arc: click the curve to be tangent to");
        }
        return HOBBYCAD_TRANSLATE_NOOP(
            "hobbycad::SketchCanvas",
            "Tangent arc: click the end point  (Shift = long way round)");
    default:
        return stageText({
            HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas",
                                    "Arc (3-point): click the start point"),
            HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas", "Arc (3-point): click the end point"),
            HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas",
                                    "Arc (3-point): click a point on the arc"),
        }, s.placed);
    }
}

const char* slotPrompt(CreationMode m, const PlacementStage& s)
{
    switch (m) {
    case CreationMode::SlotOverall:
        return stageText({
            HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas",
                                    "Slot: click one end  (scroll = thickness)"),
            HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas",
                                    "Slot: click the other end, or type an overall length"),
        }, s.placed);
    case CreationMode::SlotArcRadius:
        return stageText({
            HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas",
                                    "Arc slot: click the arc center  (scroll = thickness)"),
            HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas",
                                    "Arc slot: click the start point, or type a radius"),
            HOBBYCAD_TRANSLATE_NOOP(
                "hobbycad::SketchCanvas",
                "Arc slot: click the end point, or type a sweep angle  (Shift = long way round)"),
        }, s.placed);
    case CreationMode::SlotArcEnds:
        return stageText({
            HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas",
                                    "Arc slot: click the start point  (scroll = thickness)"),
            HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas", "Arc slot: click the end point"),
            HOBBYCAD_TRANSLATE_NOOP(
                "hobbycad::SketchCanvas",
                "Arc slot: set the bulge, or type a sweep angle  (Shift = long way round)"),
        }, s.placed);
    default:
        return stageText({
            HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas",
                                    "Slot: click the first center  (scroll = thickness)"),
            HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas",
                                    "Slot: click the second center, or type a length"),
        }, s.placed);
    }
}

const char* polygonPrompt(CreationMode m, const PlacementStage& s)
{
    if (m == CreationMode::PolygonFreeform) {
        return s.placed < 3
            ? HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas", "Polygon: click to add vertices")
            : HOBBYCAD_TRANSLATE_NOOP(
                  "hobbycad::SketchCanvas",
                  "Polygon: click to add vertices, or click the first to close");
    }
    return stageText({
        HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas", "Polygon: click the center"),
        HOBBYCAD_TRANSLATE_NOOP(
            "hobbycad::SketchCanvas",
            "Polygon: click to set the radius, or type one  (scroll = side count)"),
    }, s.placed);
}

const char* ellipsePrompt(CreationMode m, const PlacementStage& s)
{
    switch (ellipsePlacementOf(m)) {
    case EllipsePlacement::CenterAxes:
    case EllipsePlacement::Arc:
        return stageText({
            HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas", "Ellipse: click the center"),
            HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas",
                                    "Ellipse: click the major-axis end, or type a radius"),
            HOBBYCAD_TRANSLATE_NOOP(
                "hobbycad::SketchCanvas",
                "Ellipse: click a point the ellipse passes through, or type the minor radius"),
            HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas",
                                    "Elliptical arc: click where the arc starts"),
            HOBBYCAD_TRANSLATE_NOOP(
                "hobbycad::SketchCanvas",
                "Elliptical arc: click where the arc ends (Shift: the long way around)"),
        }, s.placed);
    case EllipsePlacement::ThreePoint:
        return stageText({
            HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas",
                                    "Ellipse: click one end of the major axis"),
            HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas",
                                    "Ellipse: click the other end of the major axis"),
            HOBBYCAD_TRANSLATE_NOOP(
                "hobbycad::SketchCanvas",
                "Ellipse: click a point the ellipse passes through, or type the minor radius"),
        }, s.placed);
    case EllipsePlacement::SpanRise:
        return stageText({
            HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas",
                                    "Span + Rise arc: click one end of the span"),
            HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas",
                                    "Span + Rise arc: click the other end of the span"),
            HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas",
                                    "Span + Rise arc: click the apex (the rise), or type it"),
        }, s.placed);
    case EllipsePlacement::Corner:
        return stageText({
            HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas", "Corner arc: click the corner"),
            HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas",
                                    "Corner arc: click where the arc meets the first leg"),
            HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas",
                                    "Corner arc: click where the arc meets the second leg"),
        }, s.placed);
    case EllipsePlacement::Endpoints:
        return stageText({
            HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas",
                                    "Endpoints arc: click the first point on the curve"),
            HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas",
                                    "Endpoints arc: click the second point on the curve"),
            HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas", "Endpoints arc: click the center"),
            HOBBYCAD_TRANSLATE_NOOP(
                "hobbycad::SketchCanvas",
                "Endpoints arc: click to set the axis direction (Shift: the long way around)"),
        }, s.placed);
    }
    return "";
}

const char* splinePrompt(CreationMode m, const PlacementStage& s)
{
    if (m == CreationMode::SplineConic) {
        return stageText({
            HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas", "Conic arc: click the start"),
            HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas", "Conic arc: click the end"),
            HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas",
                                    "Conic arc: click the apex, where the two end tangents meet"),
            HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas",
                                    "Conic arc: slide to set rho (0.5 parabola, less elliptical, "
                                    "more hyperbolic), click to place"),
        }, s.placed);
    }
    if (m == CreationMode::SplineFitPoints) {
        return s.placed < 2
            ? HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas", "Spline: click to add fit points")
            : HOBBYCAD_TRANSLATE_NOOP(
                  "hobbycad::SketchCanvas",
                  "Spline: click to add points; Enter, Esc, or right-click to finish");
    }
    return !s.anchors
        ? HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas",
                                  "Bezier: click for a corner, click-drag to pull tangent handles")
        : HOBBYCAD_TRANSLATE_NOOP(
              "hobbycad::SketchCanvas",
              "Bezier: click/drag to add anchors; Enter, Esc, or right-click to finish");
}

}  // namespace

const char* placementPrompt(PlacementKind kind, const PlacementStage& stage)
{
    switch (kind.tool) {
    case SketchTool::Line:      return linePrompt(kind.mode, stage);
    case SketchTool::Rectangle: return rectanglePrompt(kind.mode, stage);
    case SketchTool::Circle:    return circlePrompt(kind.mode, stage);
    case SketchTool::Arc:       return arcPrompt(kind.mode, stage);
    case SketchTool::Slot:      return slotPrompt(kind.mode, stage);
    case SketchTool::Polygon:   return polygonPrompt(kind.mode, stage);
    case SketchTool::Ellipse:   return ellipsePrompt(kind.mode, stage);
    case SketchTool::Spline:    return splinePrompt(kind.mode, stage);
    default:                    return "";
    }
}

const char* placementCursorPrompt(PlacementKind kind, const PlacementStage& stage)
{
    if (kind.tool == SketchTool::Line && kind.mode == CreationMode::LineTangent) {
        if (stage.targets == 0) {
            return HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas",
                                           "(select arc/circle for tangent)");
        }
        if (stage.placed == 0) {
            return HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas", "(click the start point)");
        }
        return HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas", "(click the end point)");
    }
    if (kind.tool == SketchTool::Spline) {
        if (kind.mode == CreationMode::SplineConic) {
            return stage.placed >= 3
                ? HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas",
                                          "(Slide to set rho, click to place)")
                : HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas", "(Click to place)");
        }
        if (stage.canFinish) {
            return HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas", "(Right-click to finish)");
        }
        return kind.mode == CreationMode::SplineFitPoints
            ? HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas", "(Click to add points)")
            : HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas",
                                      "(Click to add points, drag for handles)");
    }
    return nullptr;
}

std::vector<DimField> placementFields(PlacementKind kind, const PlacementStage& stage)
{
    const CreationMode m = kind.mode;
    const int placed = stage.placed;
    switch (kind.tool) {
    case SketchTool::Line:
        // Every mode, from the first point on. Locking either makes a real
        // constraint at commit (Distance, Angle).
        if (placed >= 1) return {DimField::Length, DimField::Angle};
        return {};

    case SketchTool::Rectangle:
        switch (m) {
        case CreationMode::RectThreePoint:
            if (placed == 1) return {DimField::EdgeLength, DimField::EdgeAngle};
            if (placed >= 2) return {DimField::Width};
            return {};
        case CreationMode::RectParallelogram:
            if (placed == 1) return {DimField::Edge1, DimField::Edge1Angle};
            if (placed >= 2) return {DimField::Edge2, DimField::Edge2Angle};
            return {};
        default:
            if (placed >= 1) return {DimField::Width, DimField::Height};
            return {};
        }

    case SketchTool::Circle:
        switch (m) {
        case CreationMode::CircleCenterRadius:
            if (placed >= 1) return {DimField::Radius};
            return {};
        case CreationMode::CircleTwoPoint:
            // Two clicks span a DIAMETER.
            if (placed >= 1) return {DimField::Diameter};
            return {};
        case CreationMode::CircleThreePoint:
            // With fewer than two points on it the radius means nothing yet;
            // locked, it makes the third click choose between two centers.
            if (placed >= 2) return {DimField::Radius};
            return {};
        default:
            return {};   // tangent circles are picked, not typed
        }

    case SketchTool::Arc:
        switch (m) {
        case CreationMode::ArcCenterStartEnd:
            if (placed == 1) return {DimField::Radius};
            if (placed >= 2) return {DimField::SweepAngle};
            return {};
        case CreationMode::ArcStartEndRadius:
            if (placed == 1) return {DimField::ChordLength, DimField::ChordAngle};
            if (placed >= 2) return {DimField::SweepAngle};
            return {};
        case CreationMode::ArcTangent:
            // The cursor is the second point, so the fields come with the
            // target rather than at a stage boundary.
            if (stage.targets >= 1) return {DimField::Radius, DimField::SweepAngle};
            return {};
        default:
            return {};   // the 3-point arc takes no typed values
        }

    case SketchTool::Slot:
        switch (m) {
        case CreationMode::SlotArcRadius:
            if (placed == 1) return {DimField::Radius};
            if (placed >= 2) return {DimField::SweepAngle};
            return {};
        case CreationMode::SlotArcEnds:
            // The sweep means something only once both ends are placed.
            if (placed >= 2) return {DimField::SweepAngle};
            return {};
        default:
            if (placed >= 1) return {DimField::Length};
            return {};
        }

    case SketchTool::Polygon:
        // For Circumscribed the radius is the inscribed one (the apothem).
        if (m != CreationMode::PolygonFreeform && placed >= 1) return {DimField::Radius};
        return {};

    case SketchTool::Ellipse:
        // One field per stage, named for what the stage fixes. Endpoints has
        // none: its radii are solved, not chosen.
        switch (ellipsePlacementOf(m)) {
        case EllipsePlacement::CenterAxes:
        case EllipsePlacement::ThreePoint:
        case EllipsePlacement::Arc:
            if (placed == 1) return {DimField::MajorRadius};
            if (placed == 2) return {DimField::MinorRadius};
            if (placed == 3) return {DimField::ArcStart};
            if (placed >= 4) return {DimField::ArcSweep};
            return {};
        case EllipsePlacement::SpanRise:
            if (placed == 1) return {DimField::Span};
            if (placed == 2) return {DimField::Rise};
            return {};
        case EllipsePlacement::Corner:
            if (placed == 1) return {DimField::FirstAxis};
            if (placed == 2) return {DimField::SecondAxis};
            return {};
        case EllipsePlacement::Endpoints:
            return {};
        }
        return {};

    default:
        return {};
    }
}

bool placementFlippable(PlacementKind kind, const PlacementStage& stage)
{
    const CreationMode m = kind.mode;
    switch (kind.tool) {
    case SketchTool::Arc:
        if (m == CreationMode::ArcTangent) return stage.placed >= 1;
        return (m == CreationMode::ArcCenterStartEnd || m == CreationMode::ArcStartEndRadius)
            && stage.placed >= 2;
    case SketchTool::Slot:
        return (m == CreationMode::SlotArcRadius || m == CreationMode::SlotArcEnds)
            && stage.placed >= 2;
    case SketchTool::Ellipse:
        if (m == CreationMode::EllipseArc) return stage.placed >= 4;
        if (m == CreationMode::EllipseEndpointsArc) return stage.placed >= 3;
        return false;
    default:
        return false;
    }
}

bool placementAngleSnaps(PlacementKind kind)
{
    switch (kind.tool) {
    case SketchTool::Line:
        return kind.mode != CreationMode::LineHorizontal
            && kind.mode != CreationMode::LineVertical
            && kind.mode != CreationMode::LineTangent;
    case SketchTool::Rectangle:
        return true;
    case SketchTool::Slot:
        return kind.mode == CreationMode::SlotCenterToCenter
            || kind.mode == CreationMode::SlotOverall;
    default:
        return false;
    }
}

bool placementChains(PlacementKind kind)
{
    return kind.tool == SketchTool::Line && kind.mode != CreationMode::LineTangent;
}

bool placementUsesWheel(PlacementKind kind)
{
    if (kind.tool == SketchTool::Slot) return true;
    return kind.tool == SketchTool::Polygon && kind.mode != CreationMode::PolygonFreeform;
}

bool placementCanSwitch(PlacementKind from, CreationMode to)
{
    // Two-point and construction lines both place two free points; only the
    // result's construction flag differs.
    if (from.tool != SketchTool::Line) return false;
    const auto free = [](CreationMode m) {
        return m == CreationMode::LineTwoPoint || m == CreationMode::LineConstruction;
    };
    return free(from.mode) && free(to);
}

bool placementTurnsWhenLocked(PlacementKind kind)
{
    return kind.tool == SketchTool::Rectangle && kind.mode == CreationMode::RectCorner;
}

// ---- What the placement reads ------------------------------------------

StageLocks stageLocks(const DimensionInput& input)
{
    StageLocks locks;
    for (int i = 0; i < input.fieldCount(); ++i) {
        const DimFieldEntry& f = input.field(i);
        locks.push_back(f.locked ? std::optional<double>(f.lockedValue) : std::nullopt);
    }
    return locks;
}

LockedTurn captureLockedTurn(const Point2D& origin, const Point2D& cursor)
{
    // The rectangle starts axis-aligned toward the cursor's quadrant and
    // turns with the cursor from here.
    const Point2D delta = cursor - origin;
    LockedTurn t;
    t.reference = std::atan2(delta.y, delta.x);
    t.widthAngle = (delta.x >= 0) ? 0.0 : M_PI;
    t.heightAngle = (delta.y >= 0) ? M_PI / 2.0 : -M_PI / 2.0;
    return t;
}

// ---- Rules ----------------------------------------------------------------

Point2D placementCursor(PlacementKind kind, const PlacementInput& in, const Point2D& raw,
                        bool keepSnap, std::vector<Point2D>* clicks)
{
    if (clicks) *clicks = in.clicks;
    const CreationMode m = kind.mode;
    switch (kind.tool) {
    case SketchTool::Line:      return lineCursor(m, in, raw, keepSnap, clicks);
    case SketchTool::Rectangle: return rectangleCursor(m, in);
    case SketchTool::Circle:    return circleCursor(m, in);
    case SketchTool::Arc:       return arcCursor(m, in, raw, keepSnap);
    case SketchTool::Slot:      return slotCursor(m, in);
    case SketchTool::Polygon:   return polygonCursor(m, in);
    case SketchTool::Ellipse:   return ellipseCursor(m, in);
    case SketchTool::Spline:    return splineCursor(m, in);
    default:                    return in.cursor;
    }
}

Point2D placementClick(PlacementKind kind, const PlacementInput& in, const Point2D& at)
{
    // A click lands where the cursor would be shown there, so what the
    // preview promised is what is placed.
    PlacementInput here = in;
    here.cursor = at;
    return placementCursor(kind, here, at, true);
}

bool placementEntity(PlacementKind kind, const PlacementInput& in, Entity& out)
{
    Entity e;
    e.id = out.id;
    e.isConstruction = out.isConstruction;
    e.type = placementEntityType(kind);
    const CreationMode m = kind.mode;
    bool ok = false;
    switch (kind.tool) {
    case SketchTool::Line:      ok = lineEntity(m, in, e); break;
    case SketchTool::Rectangle: ok = rectangleEntity(m, in, e); break;
    case SketchTool::Circle:    ok = circleEntity(m, in, e); break;
    case SketchTool::Arc:       ok = arcEntity(m, in, e); break;
    case SketchTool::Slot:      ok = slotEntity(m, in, e); break;
    case SketchTool::Polygon:   ok = polygonEntity(m, in, e); break;
    case SketchTool::Ellipse:   ok = ellipseEntity(m, in, e); break;
    case SketchTool::Spline:    ok = splineEntity(m, in, e); break;
    default:                    return false;
    }
    if (ok) out = e;
    return ok;
}

bool placementClosesLoop(PlacementKind kind, const PlacementInput& in, const Point2D& at)
{
    if (kind.tool != SketchTool::Polygon || kind.mode != CreationMode::PolygonFreeform) {
        return false;
    }
    return in.clicks.size() >= 3 && geometry::lineLength(at, in.clicks[0]) < in.closeDistance;
}

PlacementPreview placementPreview(PlacementKind kind, const PlacementInput& in)
{
    const CreationMode m = kind.mode;
    switch (kind.tool) {
    case SketchTool::Line:      return linePreview(m, in);
    case SketchTool::Rectangle: return rectanglePreview(m, in);
    case SketchTool::Circle:    return circlePreview(m, in);
    case SketchTool::Arc:       return arcPreview(m, in);
    case SketchTool::Slot:      return slotPreview(m, in);
    case SketchTool::Polygon:   return polygonPreview(m, in);
    case SketchTool::Ellipse:   return ellipsePreview(m, in);
    case SketchTool::Spline:    return splinePreview(m, in);
    default:                    return {};
    }
}

Point2D tangentStart(const Entity& target, const Point2D& click, double snapDistance,
                     bool snapEnds)
{
    // Onto the line, or the rectangle edge nearest the click.
    Point2D a, b;
    if (!closestTangentHostEdge(target, click, a, b)) return click;
    Point2D p = geometry::closestPointOnLine(click, a, b);
    if (!snapEnds) return p;
    const Point2D mid = geometry::lineMidpoint(a, b);
    if (geometry::lineLength(p, a) < snapDistance) {
        p = a;
    } else if (geometry::lineLength(p, b) < snapDistance) {
        p = b;
    } else if (geometry::lineLength(p, mid) < snapDistance) {
        p = mid;
    }
    return p;
}

// ---- The Bezier pen ---------------------------------------------------------

void BezierPen::clear()
{
    m_anchors.clear();
    m_manual.clear();
    m_dragging = false;
}

void BezierPen::smooth()
{
    autoBezierHandles(m_anchors, m_manual);
}

void BezierPen::press(const Point2D& at)
{
    BezierAnchor a;
    a.pos = at;   // smooth handles until a drag says otherwise
    m_anchors.push_back(a);
    m_manual.push_back(false);
    m_pressAt = at;
    m_dragging = true;
    smooth();     // two anchors already show handles and a curve
}

void BezierPen::drag(const Point2D& to)
{
    if (!m_dragging || m_anchors.empty()) return;
    const Point2D d = to - m_pressAt;
    BezierAnchor& a = m_anchors.back();
    if (geometry::length(d) > geometry::kDegenerateLen) {
        // The direction is the tangent, the length the pull on each side:
        // the out handle leads the next segment and the in handle mirrors it.
        // The first anchor shows both, so a path can start with a curve.
        m_manual.back() = true;
        a.hasOut = true;
        a.outHandle = m_pressAt + d;
        a.hasIn = true;
        a.inHandle = m_pressAt - d;
    } else {
        m_manual.back() = false;
        smooth();
    }
}

std::vector<BezierAnchor> BezierPen::withCursor(const Point2D& cursor) const
{
    std::vector<BezierAnchor> anchors = m_anchors;
    if (m_dragging) return anchors;
    // While moving to the next click the curve bends to the cursor.
    std::vector<bool> manual = m_manual;
    BezierAnchor next;
    next.pos = cursor;
    anchors.push_back(next);
    manual.push_back(false);
    autoBezierHandles(anchors, manual);
    return anchors;
}

PlacementPreview BezierPen::preview(const Point2D& cursor) const
{
    PlacementPreview out;
    if (m_anchors.empty()) return out;
    const std::vector<BezierAnchor> anchors = withCursor(cursor);
    const bool provisional = !m_dragging;

    const std::vector<Point2D> poly = bezierControlPolygon(anchors);
    if (poly.size() >= 4) {
        const std::vector<Point3> ctrl(poly.begin(), poly.end());
        const std::vector<Point3> tess = tessellateSpline(ctrl, 16, true);
        if (tess.size() >= 2) out.shapes.push_back(path({tess.begin(), tess.end()}));
    }
    for (std::size_t i = 0; i < anchors.size(); ++i) {
        const BezierAnchor& a = anchors[i];
        for (const auto& [has, handle] : {std::pair<bool, Point2D>{a.hasOut, a.outHandle},
                                          std::pair<bool, Point2D>{a.hasIn, a.inHandle}}) {
            if (!has) continue;
            out.shapes.push_back(segment(a.pos, handle, PreviewStroke::Handle));
            out.shapes.push_back(mark(handle, PreviewMark::HandleEnd));
        }
        const bool next = provisional && i + 1 == anchors.size();
        out.shapes.push_back(mark(a.pos, next ? PreviewMark::NextAnchor : PreviewMark::Anchor));
    }
    return out;
}

bool BezierPen::entity(bool rational, Entity& out) const
{
    if (m_anchors.size() < 2) return false;   // at least one segment
    const std::vector<Point2D> poly = bezierControlPolygon(m_anchors);
    out.type = EntityType::Spline;
    out.splineBezier = true;
    out.splineRational = rational;
    out.points.assign(poly.begin(), poly.end());
    out.weights.clear();
    if (rational) out.weights = bezierControlPolygonWeights(m_anchors);   // all 1 until edited
    return true;
}

// ---- Choosing what to build on -----------------------------------------------

PickRefusal tangentArcTarget(const Entity* target, std::size_t entityCount)
{
    if (entityCount == 0) return PickRefusal::NoEntities;
    if (!target) return PickRefusal::NothingHit;
    if (target->type != EntityType::Line && target->type != EntityType::Rectangle) {
        return PickRefusal::WrongKind;
    }
    return target->points.size() >= 2 ? PickRefusal::None : PickRefusal::WrongKind;
}

bool tangentLineTarget(const Entity& target)
{
    return (target.type == EntityType::Circle || target.type == EntityType::Arc)
        && !target.points.empty();
}

PickRefusal offsetTarget(const Entity* target)
{
    if (!target) return PickRefusal::NothingHit;
    switch (target->type) {
    case EntityType::Line:
    case EntityType::Circle:
    case EntityType::Arc:
        return PickRefusal::None;
    default:
        return PickRefusal::WrongKind;
    }
}

CornerPick cornerPick(const std::vector<Entity>& entities, int hitId, const Point2D& click)
{
    CornerPick pick;
    const Entity* line = nullptr;
    for (const Entity& e : entities) {
        if (e.id == hitId) line = &e;
    }
    if (!line) {
        pick.refusal = PickRefusal::NothingHit;
        return pick;
    }
    if (line->type != EntityType::Line) {
        pick.refusal = PickRefusal::WrongKind;
        return pick;
    }
    pick.lineId = hitId;
    pick.otherId = findConnectedLineAtCorner(*line, entities, click);
    if (pick.otherId < 0) pick.refusal = PickRefusal::NoCorner;
    return pick;
}

// ---- Shapes ---------------------------------------------------------------

std::vector<Point2D> arcPoints(const Point2D& center, double radius, double startDeg,
                               double sweepDeg, int segments)
{
    std::vector<Point2D> pts;
    if (segments < 1) segments = 1;
    pts.reserve(static_cast<std::size_t>(segments) + 1);
    for (int i = 0; i <= segments; ++i) {
        pts.push_back(atAngle(center, radius, startDeg + sweepDeg * i / segments));
    }
    return pts;
}

namespace placement_detail {

const char* stageText(std::initializer_list<const char*> stages, int placed)
{
    const int last = static_cast<int>(stages.size()) - 1;
    if (placed < 0) placed = 0;
    return *(stages.begin() + (placed < last ? placed : last));
}

double angleDeg(const Point2D& from, const Point2D& to)
{
    return radiansToDegrees(std::atan2(to.y - from.y, to.x - from.x));
}

Point2D atAngle(const Point2D& center, double radius, double deg)
{
    return geometry::polarPoint(center, radius, degreesToRadians(deg));
}

std::vector<Point2D> circlePoints(const Point2D& center, double radius, int segments)
{
    std::vector<Point2D> pts = arcPoints(center, radius, 0.0, 360.0, segments);
    pts.back() = pts.front();
    return pts;
}

PreviewShape path(std::vector<Point2D> points, PreviewStroke stroke, bool closed)
{
    PreviewShape s;
    s.kind = PreviewShape::Kind::Path;
    s.points = std::move(points);
    s.stroke = stroke;
    s.closed = closed;
    return s;
}

PreviewShape segment(const Point2D& a, const Point2D& b, PreviewStroke stroke)
{
    return path({a, b}, stroke);
}

PreviewShape mark(const Point2D& at, PreviewMark m)
{
    PreviewShape s;
    s.kind = PreviewShape::Kind::Mark;
    s.points = {at};
    s.mark = m;
    return s;
}

PreviewShape dimension(const Point2D& from, const Point2D& to, double value, int field,
                       LabelPlace place, IdleLabel idle, int row, double gap)
{
    PreviewShape s;
    s.kind = PreviewShape::Kind::Dimension;
    s.points = {from, to};
    s.value = value;
    s.field = field;
    s.place = place;
    s.idle = idle;
    s.row = row;
    s.gap = gap;
    return s;
}

PreviewShape arcLabel(const Point2D& center, const Point2D& arcMiddle, double arcLength,
                      double sweepDeg, int field, IdleLabel idle, int row, double gap)
{
    PreviewShape s = dimension(center, arcMiddle, arcLength, field, LabelPlace::Outside, idle,
                               row, gap);
    s.sweep = sweepDeg;
    return s;
}

PreviewShape note(const Point2D& anchor, std::vector<const char*> lines, NoteStyle style,
                  double gap)
{
    PreviewShape s;
    s.kind = PreviewShape::Kind::Note;
    s.points = {anchor};
    s.lines = std::move(lines);
    s.noteStyle = style;
    s.gap = gap;
    return s;
}

PreviewShape noteAlong(const Point2D& a, const Point2D& b, const char* line)
{
    PreviewShape s = note(a, {line}, NoteStyle::Boxed, 0.0);
    s.points = {a, b};
    s.noteAlong = true;
    return s;
}

void placedClicks(const PlacementInput& in, std::vector<PreviewShape>& out, bool rubberLine)
{
    for (const Point2D& p : in.clicks) out.push_back(mark(p, PreviewMark::Click));
    if (rubberLine && !in.clicks.empty()) {
        out.push_back(segment(in.clicks.back(), in.cursor));
    }
}

void setValues(PlacementPreview& out, std::initializer_list<double> values)
{
    out.fieldValues.assign(values.begin(), values.end());
}

}  // namespace placement_detail

}  // namespace sketch
}  // namespace hobbycad
