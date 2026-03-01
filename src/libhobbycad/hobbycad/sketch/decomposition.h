// =====================================================================
//  src/libhobbycad/hobbycad/sketch/decomposition.h — Entity decomposition
// =====================================================================
//
//  Decomposes compound entities (Rectangle, Parallelogram, Polygon)
//  into primitive Lines + geometric Constraints + a named Group.
//
//  This is a pure domain operation — no GUI dependencies.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_SKETCH_DECOMPOSITION_H
#define HOBBYCAD_SKETCH_DECOMPOSITION_H

#include "../core.h"
#include "entity.h"
#include "constraint.h"
#include "group.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace hobbycad {
namespace sketch {

// =====================================================================
//  Decomposition Result
// =====================================================================

/// Result of decomposing a compound entity
struct DecompositionResult {
    bool success = false;
    std::vector<Entity> entities;          ///< Lines + optional construction Circle
    std::vector<Constraint> constraints;   ///< Coincident, Equal, Parallel, etc.
    Group group;                           ///< Named group containing all produced IDs
};

// =====================================================================
//  Decomposition Function
// =====================================================================

/// Decompose a compound entity into primitive Lines + Constraints + Group.
///
/// Handles Rectangle, Parallelogram, and Polygon (both regular and freeform).
/// Returns a DecompositionResult; the caller is responsible for inserting
/// the entities/constraints/group into its own data structures.
///
/// @param compound       The compound entity to decompose
/// @param lockedDims     Locked dimension fields (label -> value) for constraint creation
/// @param nextEntityId   Callable returning the next unique entity ID
/// @param nextConstraintId Callable returning the next unique constraint ID
/// @param groupId        The group ID to assign
/// @param existingGroups Existing groups (for serial numbering the group name)
/// @param typeName       Human-readable type name (e.g., "Rectangle", "Polygon")
/// @param isFreeform     For Polygon: true = freeform (no construction circle, no Equal)
HOBBYCAD_EXPORT DecompositionResult decomposeEntity(
    const Entity& compound,
    const std::vector<std::pair<std::string, double>>& lockedDims,
    std::function<int()> nextEntityId,
    std::function<int()> nextConstraintId,
    int groupId,
    const std::vector<Group>& existingGroups,
    const std::string& typeName,
    bool isFreeform = false);

}  // namespace sketch
}  // namespace hobbycad

#endif  // HOBBYCAD_SKETCH_DECOMPOSITION_H
