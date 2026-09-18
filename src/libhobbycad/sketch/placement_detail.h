// =====================================================================
//  src/libhobbycad/sketch/placement_detail.h — shared by the placement
//  rules of each tool (not installed)
// =====================================================================
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_SKETCH_PLACEMENT_DETAIL_H
#define HOBBYCAD_SKETCH_PLACEMENT_DETAIL_H

#include <hobbycad/sketch/placement.h>

#include <initializer_list>
#include <vector>

namespace hobbycad {
namespace sketch {
namespace placement_detail {

// Prompts and notes are marked HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchCanvas",
// ...) where they are written, the context the handlers' strings already had,
// so their translations stay attached. A wrapper macro would hide them from
// lupdate.

/// The text for stage `placed`: one entry per stage, the last repeating.
const char* stageText(std::initializer_list<const char*> stages, int placed);

/// Click `index`, or the cursor when it has not been placed.
inline Point2D pointAt(const PlacementInput& in, std::size_t index)
{
    return index < in.clicks.size() ? in.clicks[index] : in.cursor;
}

inline int placedCount(const PlacementInput& in) { return static_cast<int>(in.clicks.size()); }

/// A length lock: set and positive.
inline std::optional<double> lengthLock(const PlacementInput& in, std::size_t index)
{
    const std::optional<double> v = lockAt(in, index);
    return (v && *v > 0.0) ? v : std::nullopt;
}

/// Degrees from `from` toward `to`.
double angleDeg(const Point2D& from, const Point2D& to);

/// A point `radius` from `center` at `deg`.
Point2D atAngle(const Point2D& center, double radius, double deg);

std::vector<Point2D> circlePoints(const Point2D& center, double radius, int segments = 96);

// ---- Shape builders --------------------------------------------------------

PreviewShape path(std::vector<Point2D> points, PreviewStroke stroke = PreviewStroke::Pen,
                  bool closed = false);
PreviewShape segment(const Point2D& a, const Point2D& b,
                     PreviewStroke stroke = PreviewStroke::Pen);
PreviewShape mark(const Point2D& at, PreviewMark m);
PreviewShape dimension(const Point2D& from, const Point2D& to, double value, int field,
                       LabelPlace place, IdleLabel idle, int row = 0, double gap = 0.0);
/// An arc's label beyond `arcMiddle`, `gap` pixels out from `center`.
PreviewShape arcLabel(const Point2D& center, const Point2D& arcMiddle, double arcLength,
                      double sweepDeg, int field, IdleLabel idle, int row, double gap);
PreviewShape note(const Point2D& anchor, std::vector<const char*> lines, NoteStyle style,
                  double gap);
PreviewShape noteAlong(const Point2D& a, const Point2D& b, const char* line);

/// The placed clicks as dots, and a rubber line from the last to the cursor
/// unless `rubberLine` is false.
void placedClicks(const PlacementInput& in, std::vector<PreviewShape>& out,
                  bool rubberLine = true);

/// The live value of each field of the stage, sized to `count`.
void setValues(PlacementPreview& out, std::initializer_list<double> values);

// ---- Per tool ---------------------------------------------------------------
//  Each tool's rules, with its own mode. `raw` and `keepSnap` as
//  placementCursor(); `clicks` may be null.

Point2D lineCursor(CreationMode m, const PlacementInput& in, const Point2D& raw, bool keepSnap,
                   std::vector<Point2D>* clicks);
bool lineEntity(CreationMode m, const PlacementInput& in, Entity& out);
PlacementPreview linePreview(CreationMode m, const PlacementInput& in);

Point2D rectangleCursor(CreationMode m, const PlacementInput& in);
bool rectangleEntity(CreationMode m, const PlacementInput& in, Entity& out);
PlacementPreview rectanglePreview(CreationMode m, const PlacementInput& in);

Point2D circleCursor(CreationMode m, const PlacementInput& in);
bool circleEntity(CreationMode m, const PlacementInput& in, Entity& out);
PlacementPreview circlePreview(CreationMode m, const PlacementInput& in);

Point2D arcCursor(CreationMode m, const PlacementInput& in, const Point2D& raw, bool keepSnap);
bool arcEntity(CreationMode m, const PlacementInput& in, Entity& out);
PlacementPreview arcPreview(CreationMode m, const PlacementInput& in);

Point2D slotCursor(CreationMode m, const PlacementInput& in);
bool slotEntity(CreationMode m, const PlacementInput& in, Entity& out);
PlacementPreview slotPreview(CreationMode m, const PlacementInput& in);

Point2D polygonCursor(CreationMode m, const PlacementInput& in);
bool polygonEntity(CreationMode m, const PlacementInput& in, Entity& out);
PlacementPreview polygonPreview(CreationMode m, const PlacementInput& in);

Point2D ellipseCursor(CreationMode m, const PlacementInput& in);
bool ellipseEntity(CreationMode m, const PlacementInput& in, Entity& out);
PlacementPreview ellipsePreview(CreationMode m, const PlacementInput& in);

Point2D splineCursor(CreationMode m, const PlacementInput& in);
bool splineEntity(CreationMode m, const PlacementInput& in, Entity& out);
PlacementPreview splinePreview(CreationMode m, const PlacementInput& in);

}  // namespace placement_detail
}  // namespace sketch
}  // namespace hobbycad

#endif  // HOBBYCAD_SKETCH_PLACEMENT_DETAIL_H
