// =====================================================================
//  src/libhobbycad/hobbycad/sketch/inference.h — Drawing-time inference
// =====================================================================
//
//  While a straight segment is being placed or dragged, infer the
//  alignment constraints a user almost certainly intends: horizontal,
//  vertical, parallel to another line, perpendicular to another line.
//  The moving end is snapped onto the inferred line and the constraint
//  is reported so the caller can add it when the segment is committed.
//
//  Point snapping, point-on-entity and midpoint are the snap system's
//  job (snap.h); this module only covers straight-line alignment, which
//  the snap system does not express. Tangent from a curve endpoint is a
//  tool-specific gesture, handled in the GUI, not here.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_SKETCH_INFERENCE_H
#define HOBBYCAD_SKETCH_INFERENCE_H

#include "constraint.h"
#include "entity.h"
#include "../core.h"
#include "../types.h"

#include <vector>

namespace hobbycad {
namespace sketch {

/// The kind of alignment inferred for a segment.
enum class InferenceKind {
    Horizontal,     ///< Segment lies along the X direction
    Vertical,       ///< Segment lies along the Y direction
    Parallel,       ///< Segment is parallel to another line
    Perpendicular   ///< Segment is perpendicular to another line
};

/// One inferred alignment for the segment being drawn.
struct HOBBYCAD_EXPORT Inference {
    InferenceKind  kind = InferenceKind::Horizontal;
    /// The constraint to add when the segment is committed. Horizontal and
    /// Vertical name the new line alone; Parallel and Perpendicular name the
    /// new line and refEntityId.
    ConstraintType constraint = ConstraintType::Horizontal;
    int            refEntityId = -1;   ///< Reference line for Parallel/Perpendicular; -1 for H/V

    Point2D        adjusted;           ///< The moving end snapped onto the inferred line
    Point2D        guideA;             ///< Dashed guide line to draw, endpoint A (world)
    Point2D        guideB;             ///< Dashed guide line to draw, endpoint B (world)
    Point2D        glyphAt;            ///< Suggested world position for a preview glyph
    double         residualDeg = 0.0;  ///< Angular distance from the target before snapping
};

/// The result of inferring for one segment: the (possibly snapped) end point
/// and the winning inference, if any. At most one inference is returned
/// (the strongest alignment within tolerance), so competing hints never
/// fight over the same point.
struct HOBBYCAD_EXPORT InferenceResult {
    Point2D              adjusted;      ///< p1 after the winning inference (== p1 if none)
    std::vector<Inference> inferences;  ///< 0 or 1 entries
    bool empty() const { return inferences.empty(); }
};

/// Infer an alignment for the segment from the fixed start p0 to the moving
/// end p1. angleTolDeg is the half-window in degrees (e.g. 3.0): a segment
/// within that of horizontal snaps to horizontal, and so on. Horizontal and
/// vertical are preferred over parallel/perpendicular when both apply.
/// excludeId omits an entity from the reference search (the segment's own
/// entity when dragging an existing line). Only Line entities are references.
HOBBYCAD_EXPORT InferenceResult inferSegment(
    const std::vector<Entity>& entities,
    const Point2D& p0,
    const Point2D& p1,
    double angleTolDeg,
    int excludeId);

}  // namespace sketch
}  // namespace hobbycad

#endif  // HOBBYCAD_SKETCH_INFERENCE_H
