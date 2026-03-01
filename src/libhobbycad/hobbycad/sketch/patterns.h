// =====================================================================
//  src/libhobbycad/hobbycad/sketch/patterns.h — Pattern operations
// =====================================================================
//
//  Functions for creating rectangular and circular patterns of entities.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_SKETCH_PATTERNS_H
#define HOBBYCAD_SKETCH_PATTERNS_H

#include "entity.h"
#include "../types.h"

#include <functional>
#include <string>
#include <vector>

namespace hobbycad {
namespace sketch {

// =====================================================================
//  Rectangular Pattern
// =====================================================================

/// Parameters for rectangular pattern
struct RectPatternParams {
    int countX = 2;           ///< Number of copies in X direction (including original)
    int countY = 2;           ///< Number of copies in Y direction (including original)
    double spacingX = 10.0;   ///< Spacing between copies in X direction (mm)
    double spacingY = 10.0;   ///< Spacing between copies in Y direction (mm)
    bool includeOriginal = true;  ///< Include original in result
};

/// Result of a rectangular pattern operation
struct RectPatternResult {
    bool success = false;
    std::vector<Entity> entities;     ///< All pattern copies (not including originals)
    std::string errorMessage;
};

/// Create a rectangular pattern of entities
/// @param sourceEntities Entities to copy
/// @param params Pattern parameters
/// @param nextId Function to get next entity ID
HOBBYCAD_EXPORT RectPatternResult createRectangularPattern(
    const std::vector<Entity>& sourceEntities,
    const RectPatternParams& params,
    std::function<int()> nextId);

// =====================================================================
//  Circular Pattern
// =====================================================================

/// Parameters for circular pattern
struct CircPatternParams {
    Point2D center;           ///< Center point of rotation
    int count = 6;            ///< Total number of copies (including original)
    double totalAngle = 360.0;///< Total angle to distribute copies over (degrees)
    bool includeOriginal = true;  ///< Include original in result
};

/// Result of a circular pattern operation
struct CircPatternResult {
    bool success = false;
    std::vector<Entity> entities;     ///< All pattern copies (not including originals)
    std::string errorMessage;
};

/// Create a circular pattern of entities
/// @param sourceEntities Entities to copy
/// @param params Pattern parameters
/// @param nextId Function to get next entity ID
HOBBYCAD_EXPORT CircPatternResult createCircularPattern(
    const std::vector<Entity>& sourceEntities,
    const CircPatternParams& params,
    std::function<int()> nextId);

// =====================================================================
//  Linear Pattern (single direction)
// =====================================================================

/// Parameters for linear pattern
struct LinearPatternParams {
    int count = 3;            ///< Number of copies (including original)
    double spacing = 10.0;    ///< Spacing between copies (mm)
    double angle = 0.0;       ///< Direction angle (degrees, 0 = +X)
    bool includeOriginal = true;
};

/// Result of a linear pattern operation
struct LinearPatternResult {
    bool success = false;
    std::vector<Entity> entities;
    std::string errorMessage;
};

/// Create a linear pattern of entities
HOBBYCAD_EXPORT LinearPatternResult createLinearPattern(
    const std::vector<Entity>& sourceEntities,
    const LinearPatternParams& params,
    std::function<int()> nextId);

// =====================================================================
//  Mirror Pattern
// =====================================================================

/// Parameters for mirror pattern
struct MirrorPatternParams {
    Point2D linePoint1;       ///< First point on mirror line
    Point2D linePoint2;       ///< Second point on mirror line
    bool keepOriginal = true; ///< Keep original entities
};

/// Result of a mirror pattern operation
struct MirrorPatternResult {
    bool success = false;
    std::vector<Entity> entities;     ///< Mirrored copies
    std::string errorMessage;
};

/// Create mirrored copies of entities
HOBBYCAD_EXPORT MirrorPatternResult createMirrorPattern(
    const std::vector<Entity>& sourceEntities,
    const MirrorPatternParams& params,
    std::function<int()> nextId);

}  // namespace sketch
}  // namespace hobbycad

#endif  // HOBBYCAD_SKETCH_PATTERNS_H
