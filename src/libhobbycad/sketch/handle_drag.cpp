// =====================================================================
//  src/libhobbycad/sketch/handle_drag.cpp — dragging a grab handle
// =====================================================================
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include <hobbycad/sketch/handle_drag.h>

#include <hobbycad/geometry/utils.h>
#include <hobbycad/project.h>
#include <hobbycad/units.h>

#include <cmath>

namespace hobbycad {
namespace sketch {

namespace {
/// Two ends this close are one point: the cut of an opened circle.
constexpr double kWeld = 1e-6;
}  // namespace

DragAxis dragAxisForKey(SketchPlane plane, char key)
{
    // Each plane's two model axes are the sketch's u and v; its normal is
    // neither.
    switch (plane) {
    case SketchPlane::XY:
        if (key == 'X') return DragAxis::Horizontal;
        if (key == 'Y') return DragAxis::Vertical;
        return DragAxis::None;
    case SketchPlane::XZ:
        if (key == 'X') return DragAxis::Horizontal;
        if (key == 'Z') return DragAxis::Vertical;
        return DragAxis::None;
    case SketchPlane::YZ:
        if (key == 'Y') return DragAxis::Horizontal;
        if (key == 'Z') return DragAxis::Vertical;
        return DragAxis::None;
    default:
        return DragAxis::None;
    }
}

char dragAxisLetter(SketchPlane plane, DragAxis axis)
{
    for (char key : {'X', 'Y', 'Z'}) {
        if (axis != DragAxis::None && dragAxisForKey(plane, key) == axis) return key;
    }
    return 0;
}

Point2D handleDragTarget(const Point2D& raw, const Point2D& snapped, const Point2D& original,
                         DragAxis axis, bool snapWithAxis)
{
    if (axis == DragAxis::None) return snapped;
    const Point2D from = snapWithAxis ? snapped : raw;
    return geometry::constrainToAxis(
        from, original, axis == DragAxis::Horizontal ? geometry::Axis::X : geometry::Axis::Y);
}

void HandleDrag::end()
{
    m_active = false;
    m_handle = -1;
    m_before = Entity();
    m_groupBefore.clear();
    m_groupConstraintsBefore.clear();
    m_axis = DragAxis::None;
    m_opensFullArc = false;
    m_openArcDraggedIndex = -1;
    m_openArcSweep = 0.0;
    m_openArcFixedAngle = 0.0;
}

void HandleDrag::startWith(const Entity& e, int handle)
{
    m_active = true;
    m_handle = handle;
    m_before = e;
    m_original = e.points[static_cast<std::size_t>(handle)];
    // A circle opened at a cut is one 360-degree arc whose ends coincide;
    // grabbing an end shrinks it from the full turn.
    m_opensFullArc = e.type == EntityType::Arc && e.points.size() >= 3
                  && (handle == 1 || handle == 2)
                  && std::fabs(std::fabs(e.sweepAngle) - 360.0) < 0.5
                  && geometry::lineLength(e.points[1], e.points[2]) <= kWeld;
    if (m_opensFullArc) {
        m_openArcSweep = e.sweepAngle;   // +/- 360 seeds the direction
        m_openArcDraggedIndex = handle;
        m_openArcFixedAngle = radiansToDegrees(
            std::atan2(e.points[1].y - e.points[0].y, e.points[1].x - e.points[0].x));
    }
}

bool HandleDrag::changed(const Entity& before, const Entity& after)
{
    return after.points != before.points || after.radius != before.radius
        || after.startAngle != before.startAngle || after.sweepAngle != before.sweepAngle;
}

bool HandleDrag::labelMoved(const Constraint& before, const Constraint& after)
{
    return after.labelPosition.x != before.labelPosition.x
        || after.labelPosition.y != before.labelPosition.y;
}

}  // namespace sketch
}  // namespace hobbycad
