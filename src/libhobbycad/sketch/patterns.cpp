// =====================================================================
//  src/libhobbycad/sketch/patterns.cpp — Pattern operations implementation
// =====================================================================
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include <hobbycad/sketch/patterns.h>
#include <hobbycad/geometry/utils.h>
#include <hobbycad/geometry/intersections.h>

#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace hobbycad {
namespace sketch {

using namespace geometry;

// =====================================================================
//  Rectangular Pattern
// =====================================================================

RectPatternResult createRectangularPattern(
    const std::vector<Entity>& sourceEntities,
    const RectPatternParams& params,
    std::function<int()> nextId)
{
    RectPatternResult result;

    if (sourceEntities.empty()) {
        result.errorMessage = "No entities to pattern";
        return result;
    }

    if (params.countX < 1 || params.countY < 1) {
        result.errorMessage = "Pattern count must be at least 1";
        return result;
    }

    // Create pattern copies
    for (int i = 0; i < params.countX; ++i) {
        for (int j = 0; j < params.countY; ++j) {
            // Skip original position
            if (i == 0 && j == 0 && !params.includeOriginal) {
                continue;
            }
            if (i == 0 && j == 0) {
                continue;  // Original stays in place
            }

            Point2D offset(i * params.spacingX, j * params.spacingY);
            Transform2D transform = Transform2D::translation(offset.x, offset.y);

            for (const Entity& source : sourceEntities) {
                Entity copy = source.transformed(transform);
                copy.id = nextId();
                result.entities.push_back(copy);
            }
        }
    }

    result.success = true;
    return result;
}

// =====================================================================
//  Circular Pattern
// =====================================================================

CircPatternResult createCircularPattern(
    const std::vector<Entity>& sourceEntities,
    const CircPatternParams& params,
    std::function<int()> nextId)
{
    CircPatternResult result;

    if (sourceEntities.empty()) {
        result.errorMessage = "No entities to pattern";
        return result;
    }

    if (params.count < 2) {
        result.errorMessage = "Pattern count must be at least 2";
        return result;
    }

    double angleStep = params.totalAngle / params.count;

    // Create pattern copies
    for (int i = 1; i < params.count; ++i) {  // Start at 1 to skip original
        double angle = i * angleStep;
        Transform2D transform = Transform2D::rotation(angle, params.center);

        for (const Entity& source : sourceEntities) {
            Entity copy = source.transformed(transform);
            copy.id = nextId();

            // Adjust arc angles
            if (copy.type == EntityType::Arc) {
                copy.startAngle = normalizeAngle(copy.startAngle + angle);
            }

            result.entities.push_back(copy);
        }
    }

    result.success = true;
    return result;
}

// =====================================================================
//  Linear Pattern
// =====================================================================

LinearPatternResult createLinearPattern(
    const std::vector<Entity>& sourceEntities,
    const LinearPatternParams& params,
    std::function<int()> nextId)
{
    LinearPatternResult result;

    if (sourceEntities.empty()) {
        result.errorMessage = "No entities to pattern";
        return result;
    }

    if (params.count < 1) {
        result.errorMessage = "Pattern count must be at least 1";
        return result;
    }

    // Calculate direction vector
    double rad = params.angle * M_PI / 180.0;
    Point2D dir(std::cos(rad), std::sin(rad));

    // Create pattern copies
    for (int i = 1; i < params.count; ++i) {  // Start at 1 to skip original
        Point2D offset = dir * (i * params.spacing);
        Transform2D transform = Transform2D::translation(offset.x, offset.y);

        for (const Entity& source : sourceEntities) {
            Entity copy = source.transformed(transform);
            copy.id = nextId();
            result.entities.push_back(copy);
        }
    }

    result.success = true;
    return result;
}

// =====================================================================
//  Mirror Pattern
// =====================================================================

MirrorPatternResult createMirrorPattern(
    const std::vector<Entity>& sourceEntities,
    const MirrorPatternParams& params,
    std::function<int()> nextId)
{
    MirrorPatternResult result;

    if (sourceEntities.empty()) {
        result.errorMessage = "No entities to mirror";
        return result;
    }

    // Calculate mirror line properties
    Point2D lineDir = params.linePoint2 - params.linePoint1;
    double lineLen = length(lineDir);

    if (lineLen < DEFAULT_TOLERANCE) {
        result.errorMessage = "Mirror line has zero length";
        return result;
    }

    lineDir = lineDir / lineLen;  // Normalize

    // For each entity, create a mirrored copy
    for (const Entity& source : sourceEntities) {
        Entity copy = source;
        copy.id = nextId();

        // Mirror each point
        for (Point2D& p : copy.points) {
            // Project point onto line
            Point2D toPoint = p - params.linePoint1;
            double proj = dot(toPoint, lineDir);
            Point2D projPoint = params.linePoint1 + lineDir * proj;

            // Mirror: reflect across the projection point
            p = projPoint * 2.0 - p;
        }

        // Handle special cases
        if (copy.type == EntityType::Arc) {
            // Mirror arc angles
            // The start angle needs to be reflected and sweep reversed
            double lineAngle = std::atan2(lineDir.y, lineDir.x) * 180.0 / M_PI;

            // Reflect start angle about the line
            double startRel = copy.startAngle - lineAngle;
            copy.startAngle = lineAngle - startRel - copy.sweepAngle;
            copy.startAngle = normalizeAngle(copy.startAngle);

            // Sweep direction reverses
            copy.sweepAngle = -copy.sweepAngle;
        }

        result.entities.push_back(copy);
    }

    result.success = true;
    return result;
}

}  // namespace sketch
}  // namespace hobbycad
