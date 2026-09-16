// =====================================================================
//  src/libhobbycad/sketch/align.cpp — aligning and distributing items
//  SPDX-License-Identifier: GPL-3.0-only
//  Part of HobbyCAD (ayourk/hobbycad)
// =====================================================================

#include <hobbycad/sketch/align.h>

#include <algorithm>
#include <limits>
#include <numeric>

namespace hobbycad {
namespace sketch {

std::vector<Point2D> alignOffsets(const std::vector<AlignItem>& items, AlignmentType type)
{
    std::vector<Point2D> offsets(items.size(), Point2D(0.0, 0.0));
    if (items.size() < 2) return offsets;

    auto center = [](const geometry::BoundingBox& b) { return b.center(); };

    if (type == AlignmentType::DistributeHorizontal || type == AlignmentType::DistributeVertical) {
        if (items.size() < 3) return offsets;
        const bool horiz = (type == AlignmentType::DistributeHorizontal);
        auto pos = [&](size_t i) { const Point2D c = center(items[i].bounds); return horiz ? c.x : c.y; };
        std::vector<size_t> order(items.size());
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return pos(a) < pos(b); });
        const double firstPos = pos(order.front());
        const double lastPos = pos(order.back());
        const double spacing = (lastPos - firstPos) / static_cast<double>(items.size() - 1);
        for (size_t k = 1; k + 1 < order.size(); ++k) {
            const double delta = (firstPos + static_cast<double>(k) * spacing) - pos(order[k]);
            offsets[order[k]] = horiz ? Point2D(delta, 0.0) : Point2D(0.0, delta);
        }
        return offsets;
    }

    // Targets over the items. "Top" is the largest minY and "Bottom" the
    // smallest maxY, matching the canvas's long-standing reading of its
    // world rectangles.
    double targetLeft = std::numeric_limits<double>::max();
    double targetRight = std::numeric_limits<double>::lowest();
    double targetTop = std::numeric_limits<double>::lowest();
    double targetBottom = std::numeric_limits<double>::max();
    double targetHCenter = 0, targetVCenter = 0;
    for (const AlignItem& it : items) {
        targetLeft   = std::min(targetLeft, it.bounds.minX);
        targetRight  = std::max(targetRight, it.bounds.maxX);
        targetTop    = std::max(targetTop, it.bounds.minY);
        targetBottom = std::min(targetBottom, it.bounds.maxY);
        const Point2D c = center(it.bounds);
        targetHCenter += c.x;
        targetVCenter += c.y;
    }
    targetHCenter /= static_cast<double>(items.size());
    targetVCenter /= static_cast<double>(items.size());

    for (size_t i = 0; i < items.size(); ++i) {
        const geometry::BoundingBox& b = items[i].bounds;
        const Point2D c = center(b);
        Point2D off(0.0, 0.0);
        switch (type) {
        case AlignmentType::Left:             off.x = targetLeft - b.minX; break;
        case AlignmentType::Right:            off.x = targetRight - b.maxX; break;
        case AlignmentType::Top:              off.y = targetTop - b.minY; break;
        case AlignmentType::Bottom:           off.y = targetBottom - b.maxY; break;
        case AlignmentType::HorizontalCenter: off.x = targetHCenter - c.x; break;
        case AlignmentType::VerticalCenter:   off.y = targetVCenter - c.y; break;
        default: break;
        }
        offsets[i] = off;
    }
    return offsets;
}

}  // namespace sketch
}  // namespace hobbycad
