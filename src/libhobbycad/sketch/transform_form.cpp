// =====================================================================
//  src/libhobbycad/sketch/transform_form.cpp — the move/copy form of a
//  sketch selection
// =====================================================================
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include <hobbycad/sketch/transform_form.h>

#include <hobbycad/translate.h>

namespace hobbycad {
namespace sketch {

void TransformForm::reset(const Point2D& newPivot)
{
    translate = {};
    angle = 0.0;
    factor = 1.0;
    freeMove = {};
    freeMoveAngle = 0.0;
    relativeTarget = false;
    from.reset();
    to.reset();
    onSelection.reset();
    mirrorA.reset();
    mirrorB.reset();
    reference.reset();
    pivot = newPivot;
}

bool TransformForm::shows(TransformFormRow row, bool wholeGroup) const
{
    switch (row) {
    case TransformFormRow::Translate:       return type == MoveType::Translate;
    case TransformFormRow::Rotate:          return type == MoveType::Rotate;
    case TransformFormRow::Scale:           return type == MoveType::Scale;
    case TransformFormRow::Mirror:          return type == MoveType::Mirror;
    case TransformFormRow::MirrorLine:
        return type == MoveType::Mirror && mirrorAxis == MirrorAxis::PickedLine;
    case TransformFormRow::PointToPoint:    return type == MoveType::PointToPoint;
    case TransformFormRow::PointToPosition: return type == MoveType::PointToPosition;
    case TransformFormRow::Reference:
        return type == MoveType::PointToPosition && relativeTarget;
    case TransformFormRow::FreeMove:        return type == MoveType::FreeMove;
    case TransformFormRow::Pivot: {
        // Where the pivot is used, and for a whole group, whose pivot it is.
        const bool used = type == MoveType::Rotate || type == MoveType::Scale
                       || type == MoveType::FreeMove
                       || (type == MoveType::Mirror && mirrorAxis != MirrorAxis::PickedLine);
        return used || wholeGroup;
    }
    }
    return false;
}

bool TransformForm::turns() const
{
    return type == MoveType::Rotate || type == MoveType::FreeMove;
}

TransformFormNeed TransformForm::params(GroupTransformParams& p) const
{
    p = GroupTransformParams{};
    switch (type) {
    case MoveType::Translate:
        p.kind = GroupTransformKind::Translate;
        p.delta = translate;
        break;
    case MoveType::Rotate:
        p.kind = GroupTransformKind::Rotate;
        p.angleDeg = angle;
        p.centerGiven = true;
        p.center = pivot;
        break;
    case MoveType::Scale:
        p.kind = GroupTransformKind::Scale;
        p.factor = factor;
        p.centerGiven = true;
        p.center = pivot;
        break;
    case MoveType::Mirror:
        p.kind = GroupTransformKind::Mirror;
        p.centerGiven = true;
        p.center = pivot;
        if (mirrorAxis == MirrorAxis::PickedLine) {
            if (!mirrorA || !mirrorB) return TransformFormNeed::MirrorLine;
            p.mirrorLineGiven = true;
            p.mirrorA = *mirrorA;
            p.mirrorB = *mirrorB;
        } else {
            p.mirrorAcrossHorizontal = mirrorAxis == MirrorAxis::Horizontal;
        }
        break;
    case MoveType::PointToPoint:
        if (!from || !to) return TransformFormNeed::FromAndTo;
        p.kind = GroupTransformKind::Translate;
        p.delta = *to - *from;
        break;
    case MoveType::PointToPosition: {
        if (!onSelection) return TransformFormNeed::PointOnSelection;
        Point2D position = target;
        if (relativeTarget) {
            if (!reference) return TransformFormNeed::Reference;
            position = *reference + target;
        }
        p.kind = GroupTransformKind::Translate;
        p.delta = position - *onSelection;
        break;
    }
    case MoveType::FreeMove:
        // A turn about the pivot, then the drag: one transform, one undo step.
        p.kind = GroupTransformKind::Rotate;
        p.angleDeg = freeMoveAngle;
        p.centerGiven = true;
        p.center = pivot;
        p.delta = freeMove;
        break;
    }
    return TransformFormNeed::None;
}

std::optional<TransformPoint> TransformForm::picked(TransformPoint which, const Point2D& at)
{
    switch (which) {
    case TransformPoint::From:
        from = at;
        if (!to) return TransformPoint::To;
        break;
    case TransformPoint::To:
        to = at;
        break;
    case TransformPoint::OnSelection:
        onSelection = at;
        // The target starts where the point is now: the position itself, or
        // for a relative target its offset from the reference.
        if (!relativeTarget) {
            target = at;
        } else if (reference) {
            target = at - *reference;
        }
        break;
    case TransformPoint::MirrorA:
        mirrorA = at;
        if (!mirrorB) return TransformPoint::MirrorB;
        break;
    case TransformPoint::MirrorB:
        mirrorB = at;
        break;
    case TransformPoint::Reference:
        reference = at;
        if (onSelection) target = *onSelection - at;
        break;
    }
    return std::nullopt;
}

std::optional<Point2D> TransformForm::step() const
{
    if (!from || !to) return std::nullopt;
    return *to - *from;
}

TransformForm TransformForm::forCommand(TransformType command)
{
    TransformForm f;
    switch (command) {
    case TransformType::Move:   f.type = MoveType::Translate; f.copy = false; break;
    case TransformType::Copy:   f.type = MoveType::Translate; f.copy = true; break;
    case TransformType::Rotate: f.type = MoveType::Rotate; break;
    case TransformType::Scale:  f.type = MoveType::Scale; break;
    case TransformType::Mirror: f.type = MoveType::Mirror; break;
    }
    return f;
}

const char* transformFormNeedText(TransformFormNeed need)
{
    switch (need) {
    case TransformFormNeed::MirrorLine:
        return HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchPropertiesWidget",
                                       "pick both points of the mirror line");
    case TransformFormNeed::FromAndTo:
        return HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchPropertiesWidget",
                                       "pick the from-point and the to-point");
    case TransformFormNeed::PointOnSelection:
        return HOBBYCAD_TRANSLATE_NOOP(
            "hobbycad::SketchPropertiesWidget",
            "pick the point on the selection that should land on the target");
    case TransformFormNeed::Reference:
        return HOBBYCAD_TRANSLATE_NOOP("hobbycad::SketchPropertiesWidget",
                                       "pick the reference point the offset is measured from");
    case TransformFormNeed::None:
        break;
    }
    return "";
}

}  // namespace sketch
}  // namespace hobbycad
