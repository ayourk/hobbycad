// =====================================================================
//  src/libhobbycad/hobbycad/sketch/transform_form.h — the move/copy form
//  of a sketch selection
// =====================================================================
//
//  Capability tier of the front-end support layer. The values a person
//  fills in to move, turn, scale or mirror a selection (or to move a picked
//  point onto another, or onto a position), which of them the chosen move
//  type shows, what is still missing before it can be applied, which point
//  to pick next, and the transform the form stands for. A front end binds
//  its fields to this and asks the sketch to preview or apply the result.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_SKETCH_TRANSFORM_FORM_H
#define HOBBYCAD_SKETCH_TRANSFORM_FORM_H

#include "../core.h"
#include "../types.h"
#include "transform.h"
#include "undo.h"

#include <optional>

namespace hobbycad {
namespace sketch {

/// What the form does, in the order a front end lists them.
enum class MoveType {
    Translate,
    Rotate,
    Scale,
    Mirror,
    PointToPoint,      ///< by the step from one picked point to another
    PointToPosition,   ///< a picked point onto a position
    FreeMove,          ///< dragged and turned on the canvas
};

/// The line a mirror crosses.
enum class MirrorAxis {
    Horizontal,   ///< through the pivot
    Vertical,     ///< through the pivot
    PickedLine,   ///< through two picked points
};

/// A point the form asks the person to pick on the canvas.
enum class TransformPoint {
    From,
    To,
    OnSelection,
    MirrorA,
    MirrorB,
    Reference,
};

/// A group of fields a move type shows.
enum class TransformFormRow {
    Translate,
    Rotate,
    Scale,
    Mirror,
    MirrorLine,        ///< the two points of a picked mirror line
    PointToPoint,
    PointToPosition,
    Reference,         ///< the point a relative target is measured from
    FreeMove,
    Pivot,
};

/// What the form still lacks before it stands for a transform.
enum class TransformFormNeed {
    None,
    MirrorLine,
    FromAndTo,
    PointOnSelection,
    Reference,
};

/// The form's values.
struct HOBBYCAD_EXPORT TransformForm {
    MoveType type = MoveType::Translate;
    Point2D translate;             ///< Translate: the step
    double angle = 0.0;            ///< Rotate: degrees, counter-clockwise
    double factor = 1.0;           ///< Scale
    MirrorAxis mirrorAxis = MirrorAxis::Horizontal;
    bool relativeTarget = false;   ///< PointToPosition: the target is an offset from `reference`
    Point2D target;                ///< PointToPosition: the position, or the offset
    Point2D freeMove;              ///< FreeMove: the drag
    double freeMoveAngle = 0.0;    ///< FreeMove: the turn about the pivot
    Point2D pivot;
    bool copy = false;             ///< apply to a copy

    std::optional<Point2D> from, to, onSelection, mirrorA, mirrorB, reference;

    /// Start over for the same move type: the steps, turns and picks go,
    /// the target is absolute again, and the pivot is `pivot`.
    void reset(const Point2D& pivot);

    /// True when `row` shows for the move type. A whole group's pivot always
    /// shows; it is stored on the group.
    bool shows(TransformFormRow row, bool wholeGroup) const;
    /// True when the canvas's turning ring belongs to the move type.
    bool turns() const;

    /// The transform the form stands for, or what it still needs.
    TransformFormNeed params(GroupTransformParams& out) const;

    /// A point was picked. Returns the point to pick next, if one follows
    /// on its own (the second of a pair).
    std::optional<TransformPoint> picked(TransformPoint which, const Point2D& at);

    /// The step from `from` to `to`, once both are picked.
    std::optional<Point2D> step() const;

    /// The form a menu's transform command opens with.
    static TransformForm forCommand(TransformType command);
};

/// A need's explanation, untranslated (context "hobbycad::SketchPropertiesWidget").
HOBBYCAD_EXPORT const char* transformFormNeedText(TransformFormNeed need);

}  // namespace sketch
}  // namespace hobbycad

#endif  // HOBBYCAD_SKETCH_TRANSFORM_FORM_H
