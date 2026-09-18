// =====================================================================
//  src/libhobbycad/hobbycad/sketch/placement.h — placing an entity by
//  clicks
// =====================================================================
//
//  Capability tier of the front-end support layer. What a drawing tool
//  does while an entity is being placed, for every tool and creation
//  mode: how many clicks it takes, what each stage asks for (the prompt
//  and the typed-value fields), where the cursor may go (locked values,
//  a tangent path), the entity the clicks describe, and the preview to
//  draw, as shapes in sketch coordinates. A front end turns its events
//  into clicks and a cursor, and paints the shapes; it decides nothing
//  about the geometry.
//
//  The preview and the commit go through the same rules, so what is
//  shown is what is stored.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_SKETCH_PLACEMENT_H
#define HOBBYCAD_SKETCH_PLACEMENT_H

#include "../commands.h"
#include "../core.h"
#include "../types.h"
#include "dimension_field.h"
#include "entity.h"

#include <optional>
#include <string>
#include <vector>

namespace hobbycad {
namespace sketch {

class DimensionInput;

// ---- Which placement --------------------------------------------------

/// A drawing tool and its creation mode.
struct PlacementKind {
    SketchTool tool = SketchTool::Line;
    CreationMode mode = CreationMode::Default;
};

inline bool operator==(const PlacementKind& a, const PlacementKind& b)
{
    return a.tool == b.tool && a.mode == b.mode;
}

/// True for a tool placed through this interface.
HOBBYCAD_EXPORT bool isPlacementTool(SketchTool tool);

/// Clicks that complete the entity, or 0 when the user decides (a spline,
/// a freeform polygon, the Bezier pen). A regular polygon and the two-click
/// tools count both clicks.
HOBBYCAD_EXPORT int placementClicks(PlacementKind kind);

/// Existing curves the placement is built on, picked before its clicks:
/// one for a tangent line or arc, two or three for a tangent circle.
HOBBYCAD_EXPORT int placementTargets(PlacementKind kind);

/// The entity type the placement makes.
HOBBYCAD_EXPORT EntityType placementEntityType(PlacementKind kind);

// ---- Where the placement stands ----------------------------------------

/// How far the user is.
struct PlacementStage {
    int placed = 0;          ///< clicks placed
    int targets = 0;         ///< curves picked so far (tangent modes)
    bool chainsArc = false;  ///< a line chain may turn into a tangent arc here
    bool anchors = false;    ///< the Bezier pen has at least one anchor
    bool canFinish = false;  ///< an open-ended placement has enough points
};

/// The translation context of every prompt and note here.
HOBBYCAD_EXPORT const char* placementContext();

/// The status-bar prompt, untranslated (context placementContext()). A
/// "%1" in it is the number of targets picked so far.
HOBBYCAD_EXPORT const char* placementPrompt(PlacementKind kind, const PlacementStage& stage);

/// The short prompt shown by the cursor, or nullptr when the front end
/// shortens placementPrompt() itself.
HOBBYCAD_EXPORT const char* placementCursorPrompt(PlacementKind kind,
                                                  const PlacementStage& stage);

/// The typed-value fields the stage offers, in order.
HOBBYCAD_EXPORT std::vector<DimField> placementFields(PlacementKind kind,
                                                      const PlacementStage& stage);

/// True when Shift may flip the arc being placed the long way round.
HOBBYCAD_EXPORT bool placementFlippable(PlacementKind kind, const PlacementStage& stage);

/// True when Ctrl snaps the direction from the first click to 45 degrees.
HOBBYCAD_EXPORT bool placementAngleSnaps(PlacementKind kind);

/// True when a finished segment starts the next one (a line chain).
HOBBYCAD_EXPORT bool placementChains(PlacementKind kind);

/// True when the scroll wheel adjusts the placement (a slot's width, a
/// regular polygon's side count) instead of zooming.
HOBBYCAD_EXPORT bool placementUsesWheel(PlacementKind kind);

/// True when the mode may change to `to` with clicks already placed,
/// because they keep their meaning (two-point and construction lines).
HOBBYCAD_EXPORT bool placementCanSwitch(PlacementKind from, CreationMode to);

// ---- What the placement reads ------------------------------------------

/// A stage's locked field values, by field index; empty when free.
using StageLocks = std::vector<std::optional<double>>;

/// The current stage's locks, as a DimensionInput holds them.
HOBBYCAD_EXPORT StageLocks stageLocks(const DimensionInput& input);

/// A corner rectangle's turn once both its sides are locked: the cursor
/// direction when they were, and the directions of the two sides then.
struct LockedTurn {
    double reference = 0.0;     ///< radians
    double widthAngle = 0.0;    ///< radians
    double heightAngle = 0.0;   ///< radians
};

/// The turn to keep when both sides of a corner rectangle lock with the
/// cursor at `cursor`.
HOBBYCAD_EXPORT LockedTurn captureLockedTurn(const Point2D& origin, const Point2D& cursor);

/// True when the placement keeps a turn once every field of the stage is
/// locked (the corner rectangle).
HOBBYCAD_EXPORT bool placementTurnsWhenLocked(PlacementKind kind);

/// Everything the placement rules read.
struct PlacementInput {
    std::vector<Point2D> clicks;      ///< placed so far
    Point2D cursor;                   ///< where the next click would go
    StageLocks locks;                 ///< the current stage's locked values
    bool flipped = false;             ///< the long way round (Shift)
    bool semicircle = false;          ///< exactly half a circle (Ctrl, start-end arc)
    double slotRadius = 5.0;          ///< a slot's half width
    int sides = 6;                    ///< a regular polygon's side count
    std::vector<Entity> targets;      ///< the picked curves, in pick order
    std::optional<LockedTurn> turn;   ///< a corner rectangle's kept turn
    double closeDistance = 0.0;       ///< how near the first vertex closes a loop
};

/// The value a stage's lock holds for field `index`, if locked.
inline std::optional<double> lockAt(const PlacementInput& in, std::size_t index)
{
    return index < in.locks.size() ? in.locks[index] : std::nullopt;
}

// ---- Rules ----------------------------------------------------------------

/// Where the cursor goes, from `in.cursor`: onto the path the mode allows
/// (a tangent, an arc, an axis) and to the locked values. `raw` is the
/// cursor before any snap, for modes that project it; `keepSnap` is true
/// when `in.cursor` is an entity snap the path may keep if it already lies
/// on it. Returns `in.cursor` when nothing applies. A lock can move a
/// placed click too (a tangent line's start slides round its circle to a
/// locked angle); `clicks`, when given, receives the clicks as they then
/// are.
HOBBYCAD_EXPORT Point2D placementCursor(PlacementKind kind, const PlacementInput& in,
                                        const Point2D& raw, bool keepSnap,
                                        std::vector<Point2D>* clicks = nullptr);

/// Where a click at `at` lands: projected onto the placement's path where
/// it has one, with the locks applied. `at` is already snapped.
HOBBYCAD_EXPORT Point2D placementClick(PlacementKind kind, const PlacementInput& in,
                                       const Point2D& at);

/// The entity the clicks describe, with the cursor as the click still to
/// come when there are too few. False when they describe nothing usable.
/// `out` keeps its id and construction flag; the rest is replaced.
HOBBYCAD_EXPORT bool placementEntity(PlacementKind kind, const PlacementInput& in, Entity& out);

/// True when a click at `at` closes a freeform polygon instead of adding a
/// vertex.
HOBBYCAD_EXPORT bool placementClosesLoop(PlacementKind kind, const PlacementInput& in,
                                         const Point2D& at);

/// Where a tangent line or arc starts on its target, for a click at
/// `click`: on the curve, pulled to an end or the middle of the edge
/// within `snapDistance` unless `snapEnds` is false.
HOBBYCAD_EXPORT Point2D tangentStart(const Entity& target, const Point2D& click,
                                     double snapDistance, bool snapEnds);

// ---- Preview --------------------------------------------------------------

/// How a path is stroked.
enum class PreviewStroke {
    Pen,           ///< the tool's preview pen
    Solid,         ///< the preview pen, solid: the curve that will be stored
    Guide,         ///< thin gray dashes: a construction aid
    Faint,         ///< translucent gray dashes: an alternative, a closing edge
    Construction,  ///< construction color, dashed
    Target,        ///< a picked curve, emphasized
    Accent,        ///< orange: an angle guide
    Handle,        ///< a Bezier handle line
};

/// A point marker.
enum class PreviewMark {
    Click,         ///< a placed point, a small filled dot
    BigClick,      ///< a placed point that stays fixed, a larger dot
    Center,        ///< a small cross
    Snapped,       ///< a larger cross in a ring: a snapped position
    Cursor,        ///< a hollow ring where the constrained cursor sits
    Corner,        ///< an orange dot on a placed corner
    Tip,           ///< a small red dot at a slot's true end
    CloseLoop,     ///< a green dot: a click here closes the loop
    Anchor,        ///< a Bezier anchor
    NextAnchor,    ///< the Bezier anchor the next click would place
    HandleEnd,     ///< the end of a Bezier handle
};

/// Where a dimension's value sits on screen.
enum class LabelPlace {
    Along,    ///< under the midpoint, turned with the dimension; rows stack
    Below,    ///< under the midpoint, level; rows stack
    Right,    ///< to the right of the midpoint, level
    Across,   ///< off the midpoint across the dimension, turned with it
    Outside,  ///< beyond points[1] seen from points[0] (an arc's middle), level; rows stack
    Angle,    ///< beyond the angle mark at points[0], between points[1] and points[2]
};

/// What a dimension shows when no field is being typed in.
enum class IdleLabel {
    Line,        ///< a dimension line with its value
    Value,       ///< the value alone, where the field would be
    ValueAside,  ///< the value alone, a little up and to the right of the midpoint
    ArcValue,    ///< an arc's length and sweep
    AngleValue,  ///< an angle's value by its mark
    None,
};

/// What a note is for, which decides how it looks.
enum class NoteStyle {
    Plain,  ///< gray text
    Boxed,  ///< accent text on a light box
};

/// One thing to draw.
struct PreviewShape {
    enum class Kind { Path, Mark, Dimension, Note };
    Kind kind = Kind::Path;

    /// Path: the polyline. Mark: the point. Dimension: from and to, or
    /// for Outside the center and the arc's middle, or for Angle the vertex
    /// and a point on each side. Note: the anchor, or for an Along note the
    /// two ends of the line it sits on.
    std::vector<Point2D> points;

    PreviewStroke stroke = PreviewStroke::Pen;   ///< Path
    bool closed = false;                         ///< Path
    PreviewMark mark = PreviewMark::Click;       ///< Mark

    double value = 0.0;          ///< Dimension: the length, radius or angle
    double sweep = 0.0;          ///< Dimension, ArcValue: the sweep in degrees
    int field = -1;              ///< Dimension: the field index it edits, or -1
    LabelPlace place = LabelPlace::Along;
    IdleLabel idle = IdleLabel::Line;
    int row = 0;                 ///< Dimension: stacking at the same place
    double gap = 0.0;            ///< screen pixels: Across, Right, Outside, a Note's drop

    NoteStyle noteStyle = NoteStyle::Plain;
    bool noteAlong = false;      ///< the note sits above the line points[0]..points[1]
    std::vector<const char*> lines;   ///< Note text, context placementContext()
};

/// A placement's preview: its shapes, painted in order, and the live value
/// of each field of the stage.
struct PlacementPreview {
    std::vector<PreviewShape> shapes;
    std::vector<double> fieldValues;
};

/// The preview for the placement at `in`, whose cursor is already where
/// placementCursor() put it. A picked curve is shown as soon as it exists,
/// even before the first click.
HOBBYCAD_EXPORT PlacementPreview placementPreview(PlacementKind kind, const PlacementInput& in);

// ---- The Bezier pen ---------------------------------------------------------

/// The Bezier spline pen: a click places an anchor with smooth handles, a
/// drag from it pulls both its handles symmetrically.
class HOBBYCAD_EXPORT BezierPen {
public:
    void clear();
    bool empty() const { return m_anchors.empty(); }
    std::size_t size() const { return m_anchors.size(); }
    const std::vector<BezierAnchor>& anchors() const { return m_anchors; }
    bool dragging() const { return m_dragging; }

    /// Press: a new anchor at `at`.
    void press(const Point2D& at);
    /// Move while pressed: pull the handles toward `to`; back at the anchor,
    /// smooth handles again.
    void drag(const Point2D& to);
    /// Release: the anchor is done.
    void release() { m_dragging = false; }

    /// The anchors with the cursor as one more, smoothed, unless a handle
    /// is being dragged.
    std::vector<BezierAnchor> withCursor(const Point2D& cursor) const;
    /// The preview: the curve, the handles and the anchors.
    PlacementPreview preview(const Point2D& cursor) const;
    /// The spline, rational when `rational`. False with fewer than two
    /// anchors.
    bool entity(bool rational, Entity& out) const;

private:
    void smooth();

    std::vector<BezierAnchor> m_anchors;
    std::vector<bool> m_manual;   ///< a dragged anchor keeps its handles
    Point2D m_pressAt;
    bool m_dragging = false;
};

// ---- Choosing what to build on -----------------------------------------------

/// Why a pick for a tool was refused, for the front end to explain.
enum class PickRefusal {
    None,
    NoEntities,       ///< the sketch is empty
    NothingHit,       ///< the click hit nothing
    WrongKind,        ///< the entity cannot be used this way
    NoCorner,         ///< no second line meets the picked one there
};

/// Can the tangent arc start on `target` (null when nothing was hit), in a
/// sketch of `entityCount` entities.
HOBBYCAD_EXPORT PickRefusal tangentArcTarget(const Entity* target, std::size_t entityCount);

/// Can the tangent line use `target`: a circle or an arc.
HOBBYCAD_EXPORT bool tangentLineTarget(const Entity& target);

/// Can `target` be offset: a line, a circle or an arc.
HOBBYCAD_EXPORT PickRefusal offsetTarget(const Entity* target);

/// A corner for a fillet or chamfer: the picked line and the line it meets
/// nearest `click`.
struct CornerPick {
    PickRefusal refusal = PickRefusal::None;
    int lineId = -1;
    int otherId = -1;
};

HOBBYCAD_EXPORT CornerPick cornerPick(const std::vector<Entity>& entities, int hitId,
                                      const Point2D& click);

// ---- Shapes ---------------------------------------------------------------

/// Points along an arc, `sweepDeg` from `startDeg`, both included.
HOBBYCAD_EXPORT std::vector<Point2D> arcPoints(const Point2D& center, double radius,
                                               double startDeg, double sweepDeg,
                                               int segments = 64);

}  // namespace sketch
}  // namespace hobbycad

#endif  // HOBBYCAD_SKETCH_PLACEMENT_H
