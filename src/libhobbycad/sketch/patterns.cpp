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

namespace hobbycad {
namespace sketch {

using namespace geometry;

// =====================================================================
//  Rectangular Pattern
// =====================================================================

RectPatternResult createRectangularPattern(
    const QVector<Entity>& sourceEntities,
    const RectPatternParams& params,
    std::function<int()> nextId)
{
    RectPatternResult result;

    if (sourceEntities.isEmpty()) {
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

            QPointF offset(i * params.spacingX, j * params.spacingY);
            Transform2D transform = Transform2D::translation(offset.x(), offset.y());

            for (const Entity& source : sourceEntities) {
                Entity copy = source.transformed(transform);
                copy.id = nextId();
                result.entities.append(copy);
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
    const QVector<Entity>& sourceEntities,
    const CircPatternParams& params,
    std::function<int()> nextId)
{
    CircPatternResult result;

    if (sourceEntities.isEmpty()) {
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

            result.entities.append(copy);
        }
    }

    result.success = true;
    return result;
}

// =====================================================================
//  Linear Pattern
// =====================================================================

LinearPatternResult createLinearPattern(
    const QVector<Entity>& sourceEntities,
    const LinearPatternParams& params,
    std::function<int()> nextId)
{
    LinearPatternResult result;

    if (sourceEntities.isEmpty()) {
        result.errorMessage = "No entities to pattern";
        return result;
    }

    if (params.count < 1) {
        result.errorMessage = "Pattern count must be at least 1";
        return result;
    }

    // Calculate direction vector
    double rad = qDegreesToRadians(params.angle);
    QPointF dir(qCos(rad), qSin(rad));

    // Create pattern copies
    for (int i = 1; i < params.count; ++i) {  // Start at 1 to skip original
        QPointF offset = dir * (i * params.spacing);
        Transform2D transform = Transform2D::translation(offset.x(), offset.y());

        for (const Entity& source : sourceEntities) {
            Entity copy = source.transformed(transform);
            copy.id = nextId();
            result.entities.append(copy);
        }
    }

    result.success = true;
    return result;
}

// =====================================================================
//  Mirror Pattern
// =====================================================================

MirrorPatternResult createMirrorPattern(
    const QVector<Entity>& sourceEntities,
    const MirrorPatternParams& params,
    std::function<int()> nextId)
{
    MirrorPatternResult result;

    if (sourceEntities.isEmpty()) {
        result.errorMessage = "No entities to mirror";
        return result;
    }

    // Calculate mirror line properties
    QPointF lineDir = params.linePoint2 - params.linePoint1;
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
        for (QPointF& p : copy.points) {
            // Project point onto line
            QPointF toPoint = p - params.linePoint1;
            double proj = dot(toPoint, lineDir);
            QPointF projPoint = params.linePoint1 + lineDir * proj;

            // Mirror: reflect across the projection point
            p = projPoint * 2.0 - p;
        }

        // Handle special cases
        if (copy.type == EntityType::Arc) {
            // Mirror arc angles
            // The start angle needs to be reflected and sweep reversed
            double lineAngle = qRadiansToDegrees(qAtan2(lineDir.y(), lineDir.x()));

            // Reflect start angle about the line
            double startRel = copy.startAngle - lineAngle;
            copy.startAngle = lineAngle - startRel - copy.sweepAngle;
            copy.startAngle = normalizeAngle(copy.startAngle);

            // Sweep direction reverses
            copy.sweepAngle = -copy.sweepAngle;
        }

        result.entities.append(copy);
    }

    result.success = true;
    return result;
}

}  // namespace sketch
}  // namespace hobbycad
