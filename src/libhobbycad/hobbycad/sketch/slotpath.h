// =====================================================================
//  src/libhobbycad/hobbycad/sketch/slotpath.h — slots built on a path
//  Repository: ayourk/hobbycad
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//
//  A slot is a round profile swept along a path. The path may be one
//  entity, a chain of them, a closed loop, or a set that BRANCHES from a
//  shared vertex. Aaron, 2026-08-28: "I am talking about a Y; granted it
//  wouldn't be a usual way for a slot to be formed, but I see no reason it
//  couldn't be done... this also makes it so that other branching off of a
//  spine is possible."
//
//  The three topologies need different treatment, which is why they are
//  classified before anything is swept:
//
//    open path    offset each side, cap both free ends
//    closed loop  offset each side, no caps at all
//    branching    UNION the branches; the hub fills itself
//
//  Connectivity alone cannot tell them apart. findConnectedChain() is a
//  plain BFS and returns the three arms of a Y as readily as a polyline,
//  so the classification here is by VERTEX DEGREE.
//
// =====================================================================

#ifndef HOBBYCAD_SKETCH_SLOTPATH_H
#define HOBBYCAD_SKETCH_SLOTPATH_H

#include "../core.h"
#include "../types.h"
#include "entity.h"

#include <string>
#include <vector>

namespace hobbycad {
namespace sketch {

/// What shape a set of entities makes when treated as a slot path.
enum class SlotPathKind {
    Invalid,      ///< Not usable; see SlotPathInfo::reason
    OpenPath,     ///< A chain with two free ends
    ClosedLoop,   ///< A ring; no free ends, so no end caps
    Branching,    ///< At least one vertex where three or more meet
};

/// One vertex of the path graph, with how many entities meet there.
struct SlotPathVertex {
    Point2D position;
    int degree = 0;
};

/// The result of looking at a candidate path.
struct HOBBYCAD_EXPORT SlotPathInfo {
    SlotPathKind kind = SlotPathKind::Invalid;
    bool valid = false;
    std::string reason;                    ///< Why not, when invalid

    std::vector<int> orderedIds;           ///< Open/closed only: walk order
    std::vector<SlotPathVertex> vertices;  ///< Every junction and free end
    int hubCount = 0;                      ///< Vertices of degree >= 3

    /// Smallest angle between two branches at any hub, in degrees, or 0
    /// when there are no hubs. The notch between adjacent branches sits at
    /// halfWidth / sin(angle/2) from the hub, so a small angle throws it
    /// far past the branch tips.
    double minBranchAngle = 0.0;

    /// Smallest radius of curvature anywhere on the path, or infinity for
    /// a path made only of straight lines. The inner offset inverts where
    /// the half-width exceeds this.
    double minCurveRadius = 0.0;
};

/// Classify a candidate slot path.
///
/// @param all        Every entity in the sketch.
/// @param pathIds    The entities to treat as the path.
/// @param tolerance  How close two endpoints must be to count as joined.
HOBBYCAD_EXPORT SlotPathInfo analyzeSlotPath(
    const std::vector<Entity>& all,
    const std::vector<int>& pathIds,
    double tolerance = 1e-6);

/// Whether a path of this shape can carry a slot of this width, and why not.
///
/// Separate from analyzeSlotPath() because the answer depends on the width,
/// and the same path may take a narrow slot and refuse a wide one.
///
/// @param minBranchAngleOut  Set to the minimum angle a branch needs, when
///        that is what fails.
/// @return empty when the width fits; otherwise the reason it does not.
HOBBYCAD_EXPORT std::string slotWidthProblem(const SlotPathInfo& info,
                                             double halfWidth);

/// An advisory about a path that will build but probably should not.
///
/// Kept apart from slotWidthProblem() deliberately: Aaron, 2026-08-28,
/// "even if a slot isn't meaningful on a branch, I consider it allowable.
/// Just not recommended." Refusing a constructible shape because it looks
/// odd is the program overruling the person; saying so is not.
///
/// @return empty when there is nothing to warn about.
HOBBYCAD_EXPORT std::string slotWidthWarning(const SlotPathInfo& info,
                                             double halfWidth);

/// The outline of the swept slot, as a closed polygon in CCW order.
///
/// Branching paths are unioned branch by branch. NOTE: the union requires
/// counter-clockwise input, and a sweep walked up one side and back down
/// the other traces CLOCKWISE; fed the wrong way round it returns a
/// small sliver and reports success, so the winding is fixed here rather
/// than left to callers.
///
/// @return an empty vector if the path cannot be swept.
HOBBYCAD_EXPORT std::vector<Point2D> slotOutline(
    const std::vector<Entity>& all,
    const std::vector<int>& pathIds,
    double halfWidth,
    int arcSegments = 24);

/// Re-derive a multi-segment slot's cached outline from its path segments.
///
/// The GUI/CLI counterpart of updateSlotFromPath() for a slot that follows a
/// SET of centerline entities (slot.pathEntityIds) rather than one: after the
/// solve moves the segments, recompute slot.outlineCache = slotOutline(all,
/// slot.pathEntityIds, slot.radius). slot.radius is the half-width.
///
/// @return true when a non-empty outline was produced and stored; false (and
///         outlineCache left unchanged) when the path cannot be swept.
HOBBYCAD_EXPORT bool updateSlotOutlineFromPaths(
    Entity& slot,
    const std::vector<Entity>& all,
    int arcSegments = 24);

}  // namespace sketch
}  // namespace hobbycad

#endif  // HOBBYCAD_SKETCH_SLOTPATH_H
