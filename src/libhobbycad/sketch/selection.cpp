// =====================================================================
//  src/libhobbycad/sketch/selection.cpp — what is selected in a sketch
// =====================================================================
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include <hobbycad/sketch/selection.h>

#include <hobbycad/geometry/intersections.h>
#include <hobbycad/geometry/utils.h>
#include <hobbycad/sketch/queries.h>

#include <cmath>

namespace hobbycad {
namespace sketch {

bool SelectionState::has(int entityId) const
{
    return std::find(entities.begin(), entities.end(), entityId) != entities.end();
}

void SelectionState::add(int entityId)
{
    if (entityId >= 0 && !has(entityId)) entities.push_back(entityId);
}

void SelectionState::remove(int entityId)
{
    entities.erase(std::remove(entities.begin(), entities.end(), entityId), entities.end());
    // A deselected primary hands over to the most recently selected entity.
    if (primary == entityId) primary = entities.empty() ? -1 : entities.back();
}

void SelectionState::clearEntities()
{
    entities.clear();
    primary = -1;
}

void clearSelection(SelectionState& s)
{
    s.clearEntities();
    s.points.clear();
    s.constraint = -1;
    s.midpoint = -1;
    s.slotAnchor = {-1, -1};
}

void selectConstraint(SelectionState& s, int constraintId)
{
    s.clearEntities();
    s.constraint = constraintId;
}

void selectPoint(SelectionState& s, int entityId, int pointIndex, ClickSelect mode)
{
    const std::pair<int, int> point(entityId, pointIndex);
    const auto at = std::find(s.points.begin(), s.points.end(), point);
    switch (mode) {
    case ClickSelect::Replace:
        clearSelection(s);
        s.points.push_back(point);
        break;
    case ClickSelect::Toggle:
        if (at != s.points.end()) {
            s.points.erase(at);
        } else {
            s.points.push_back(point);
        }
        break;
    case ClickSelect::Add:
        if (at == s.points.end()) s.points.push_back(point);
        break;
    }
}

void enterGroup(SelectionState& s, int groupId)
{
    s.enteredGroup = groupId;
    clearSelection(s);
}

namespace {

/// True when `p` is inside (or on) the convex quadrilateral `q`.
bool insideQuad(const Point2D& p, const std::array<Point2D, 4>& q)
{
    int sign = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        const double c = geometry::cross(q[(i + 1) % 4] - q[i], p - q[i]);
        if (std::fabs(c) < 1e-12) continue;
        const int s = c > 0 ? 1 : -1;
        if (sign == 0) {
            sign = s;
        } else if (s != sign) {
            return false;
        }
    }
    return true;
}

bool crossesQuad(const Point2D& a, const Point2D& b, const std::array<Point2D, 4>& q)
{
    for (std::size_t i = 0; i < 4; ++i) {
        const auto hit = geometry::lineLineIntersection(a, b, q[i], q[(i + 1) % 4]);
        if (hit.intersects && hit.withinSegment1 && hit.withinSegment2) return true;
    }
    return false;
}

}  // namespace

bool entityInWindow(const Entity& e, const std::array<Point2D, 4>& corners, bool axisAligned,
                    bool crossing)
{
    if (axisAligned) {
        double minX = corners[0].x, maxX = corners[0].x;
        double minY = corners[0].y, maxY = corners[0].y;
        for (const Point2D& c : corners) {
            minX = std::min(minX, c.x);
            maxX = std::max(maxX, c.x);
            minY = std::min(minY, c.y);
            maxY = std::max(maxY, c.y);
        }
        const Rect2D rect(minX, minY, maxX - minX, maxY - minY);
        return crossing ? entityIntersectsRect(e, rect) : entityEnclosedByRect(e, rect);
    }

    // A turned view: the window is a turned rectangle in the sketch, so the
    // entity's outline is tested against it.
    std::vector<Point2D> outline = tessellate(e, 64);
    if (outline.empty()) outline.assign(e.points.begin(), e.points.end());
    if (outline.empty()) return false;
    if (!crossing) {
        return std::all_of(outline.begin(), outline.end(),
                           [&](const Point2D& p) { return insideQuad(p, corners); });
    }
    for (std::size_t i = 0; i < outline.size(); ++i) {
        if (insideQuad(outline[i], corners)) return true;
        if (i + 1 < outline.size() && crossesQuad(outline[i], outline[i + 1], corners)) {
            return true;
        }
    }
    // A window wholly inside a closed outline touches it too.
    const bool closed = outline.size() > 2
        && geometry::lineLength(outline.front(), outline.back()) < 1e-9;
    return closed && geometry::pointInPolygon(corners[0], outline);
}

}  // namespace sketch
}  // namespace hobbycad
