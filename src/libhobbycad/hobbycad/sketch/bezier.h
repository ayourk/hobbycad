// =====================================================================
//  src/libhobbycad/hobbycad/sketch/bezier.h — editing stored cubic Bezier splines
//  SPDX-License-Identifier: GPL-3.0-only
//  Part of HobbyCAD (ayourk/hobbycad)
// =====================================================================
//  A stored Bezier spline is an Entity of type Spline with splineBezier set
//  and a control polygon [P0, out0, in1, P1, ...] (3N+1 points). These are
//  the edits a properties panel or a command performs on one: anchor angle,
//  handle lengths, weights, fit points in and out, open/closed. Each returns
//  false (or -1) and leaves the entity untouched when the request does not
//  apply; the caller owns undo, selection and redraw.
// =====================================================================

#ifndef HOBBYCAD_SKETCH_BEZIER_H
#define HOBBYCAD_SKETCH_BEZIER_H

#include "../core.h"
#include "../types.h"
#include "entity.h"
#include "constraint.h"

#include <vector>

namespace hobbycad {
namespace sketch {

/// True for a stored cubic Bezier spline.
HOBBYCAD_EXPORT bool isBezierEntity(const Entity& e);

/// What a properties panel shows for anchor `a` (a control index, any index
/// is accepted): the tangent direction in degrees 0..360, the two handle
/// lengths (0 at an open end), and the rational weight.
struct BezierAnchorInfo {
    double angleDeg = 0.0;
    double inLen = 0.0;
    double outLen = 0.0;
    double weight = 1.0;
    bool rational = false;
};
HOBBYCAD_EXPORT bool bezierAnchorInfo(const Entity& e, int a, BezierAnchorInfo& out);

/// Rotate both handles of anchor `a` to `angleDeg`, keeping their lengths.
HOBBYCAD_EXPORT bool setBezierAnchorAngle(Entity& e, int a, double angleDeg);

/// Set the length of anchor `a`'s out (or in) handle, keeping its direction.
/// Refused for a zero-length handle (no direction to keep) or len < 0.
HOBBYCAD_EXPORT bool setBezierAnchorHandleLength(Entity& e, int a, bool outHandle, double len);

/// Set anchor `a`'s rational weight (> 0), making the spline rational if it
/// was not (all other weights 1).
HOBBYCAD_EXPORT bool setBezierAnchorWeight(Entity& e, int a, double weight);

/// Remove anchor `a` (a fit point, index 3k) with its two handles; refused
/// on a spline of fewer than two segments. Returns the index of the first
/// of the three removed control points, or -1. Pass it to
/// remapSplinePointIndices(c, id, lo, 3) for each constraint on the spline.
HOBBYCAD_EXPORT int deleteBezierAnchor(Entity& e, int a);

/// Insert a fit point at the curve point nearest `near` (de Casteljau
/// split of that segment: two control points become five). Returns the
/// index of the first new control point, or -1. Pass it to
/// remapSplinePointIndices(c, id, at, -3) for each constraint on the spline.
HOBBYCAD_EXPORT int insertBezierFitPoint(Entity& e, const Point2D& near);

/// Set the length of the leg between consecutive control points i0 and i1,
/// moving the handle end (the anchor end stays). Refused for a zero-length
/// leg or len < 0.
HOBBYCAD_EXPORT bool setBezierLegLength(Entity& e, int i0, int i1, double len);

/// Toggle open/closed. Closing appends the wrap segment's two handles
/// (a smooth Catmull-Rom style join); opening removes them.
HOBBYCAD_EXPORT bool toggleBezierClosed(Entity& e);

/// After `count` control points were removed at `lo` (count > 0) or -count
/// inserted at `lo` (count < 0): shift the constraint's point indices on
/// spline `splineId`. Returns true when the constraint named a removed
/// point and should be dropped.
HOBBYCAD_EXPORT bool remapSplinePointIndices(Constraint& c, int splineId, int lo, int count);

/// Default handles for an authoring path: each anchor not marked manual
/// gets handles along the chord between its neighbors, one sixth of it
/// each way (Catmull-Rom tangents), none past an open end.
HOBBYCAD_EXPORT void autoBezierHandles(std::vector<BezierAnchor>& anchors,
                                       const std::vector<bool>& manual);

}  // namespace sketch
}  // namespace hobbycad

#endif  // HOBBYCAD_SKETCH_BEZIER_H
