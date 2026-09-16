// =====================================================================
//  src/libhobbycad/sketch/slotpath.cpp — slots built on a path
//  Repository: ayourk/hobbycad
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================

#include "hobbycad/sketch/slotpath.h"

#include "hobbycad/geometry/algorithms.h"
#include "hobbycad/sketch/queries.h"
#include "hobbycad/units.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace hobbycad {
namespace sketch {

namespace {

bool same(const Point2D& p, const Point2D& q, double tol);

/// The two ends of a path element, and the direction it leaves each by.
struct Ends {
    bool ok = false;
    Point2D a, b;          ///< endpoints
    Point2D dirA, dirB;    ///< unit direction leaving a, and leaving b
};

Ends endsOf(const Entity& e)
{
    Ends r;
    if (e.type == EntityType::Line) {
        if (e.points.size() < 2) return r;
        r.a = e.points[0];
        r.b = e.points[1];
        const double dx = r.b.x - r.a.x, dy = r.b.y - r.a.y;
        const double len = std::hypot(dx, dy);
        if (len < geometry::kExactEps) return r;
        r.dirA = { dx / len, dy / len };
        r.dirB = { -dx / len, -dy / len };
        r.ok = true;
        return r;
    }
    if (e.type == EntityType::Arc) {
        if (e.points.empty() || e.radius <= 0.0) return r;
        const geometry::Arc arc = e.toArc();
        const double s0 = degreesToRadians(e.startAngle);
        const double s1 = degreesToRadians(e.startAngle + e.sweepAngle);
        r.a = arc.startPoint();
        r.b = arc.endPoint();
        // Tangent at each end, pointing along the sweep and back along it.
        const double sign = e.sweepAngle >= 0.0 ? 1.0 : -1.0;
        r.dirA = { -sign * std::sin(s0), sign * std::cos(s0) };
        r.dirB = {  sign * std::sin(s1), -sign * std::cos(s1) };
        r.ok = true;
        return r;
    }

    // Anything else that tessellates: take the ends off the polyline. A
    // closed shape reports both ends at the same place, which is what
    // makes it read as a loop rather than a chain.
    const std::vector<Point2D> pts = tessellate(e, 0.05);
    if (pts.size() < 2) return r;
    r.a = pts.front();
    r.b = pts.back();
    auto dir = [](const Point2D& from, const Point2D& to) {
        const double dx = to.x - from.x, dy = to.y - from.y;
        const double len = std::hypot(dx, dy);
        return len < geometry::kExactEps ? Point2D{0, 0} : Point2D{ dx / len, dy / len };
    };
    r.dirA = dir(pts.front(), pts[1]);
    r.dirB = dir(pts.back(), pts[pts.size() - 2]);
    r.ok = true;
    return r;
}

bool same(const Point2D& p, const Point2D& q, double tol)
{
    return std::hypot(p.x - q.x, p.y - q.y) <= tol;
}

/// Sample an element into a polyline, start to end.
///
/// Through tessellate(), so ANY entity that can be drawn can carry a slot,
/// not just lines and arcs: splines, ellipses, polygons and rectangles
/// included. The sweep below only needs a polyline; it does not care what
/// produced it.
std::vector<Point2D> samplePath(const Entity& e, int arcSegments)
{
    // Deviation scaled to the segment count the caller asked for, so a
    // coarse preview and a fine outline both come from one control.
    const double tol = std::max(1e-4, 1.0 / std::max(4, arcSegments));
    std::vector<Point2D> out = tessellate(e, tol);

    // A closed primitive comes back with its first point repeated at the
    // end; the sweep wants an open run of distinct points and closes it
    // itself.
    if (out.size() > 2 && same(out.front(), out.back(), geometry::kZeroEps)) out.pop_back();
    return out;
}

/// True for entity types whose outline closes on itself, so a slot along
/// one has no free ends and takes no caps.
bool isClosedShape(const Entity& e)
{
    switch (e.type) {
    case EntityType::Circle:
    case EntityType::Ellipse:
    case EntityType::Rectangle:
    case EntityType::Parallelogram:
    case EntityType::Polygon:
        return true;
    default:
        return false;
    }
}

/// A CCW disc.
std::vector<Point2D> disc(const Point2D& c, double r, int segs)
{
    std::vector<Point2D> p;
    const int n = std::max(8, segs);
    for (int i = 0; i < n; ++i) {
        const double a = 2.0 * M_PI * static_cast<double>(i) / n;
        p.push_back({ c.x + r * std::cos(a), c.y + r * std::sin(a) });
    }
    return p;
}

/// Force CCW winding.
///
/// polygonUnion() requires it, and a band walked up one side and back down
/// the other traces CLOCKWISE. Fed the wrong way it returns a sliver and
/// reports success: a failure that looks like an answer.
void makeCCW(std::vector<Point2D>& p)
{
    double area = 0.0;
    for (size_t i = 0; i < p.size(); ++i) {
        const Point2D& u = p[i];
        const Point2D& v = p[(i + 1) % p.size()];
        area += u.x * v.y - v.x * u.y;
    }
    if (area < 0.0) std::reverse(p.begin(), p.end());
}

/// The pieces one element contributes: the band along it, plus a disc at
/// each end.
///
/// Discs rather than half-round caps, because a cap is exactly a half
/// circle and two of them at a shared joint meet edge to edge WITHOUT
/// overlapping, and polygonUnion() returns two disjoint regions for
/// polygons that merely touch. Full discs at the joint coincide, so they
/// overlap and merge. This is also the classical way to stroke a path.
std::vector<std::vector<Point2D>> sweepPieces(const Entity& e, double halfWidth,
                                              int arcSegments)
{
    std::vector<std::vector<Point2D>> out;
    const std::vector<Point2D> spine = samplePath(e, arcSegments);
    if (spine.size() < 2) return out;

    // One band per straight run between samples; a curve is many short
    // ones, and the discs at the joints round them off.
    for (size_t i = 0; i + 1 < spine.size(); ++i) {
        const double dx = spine[i + 1].x - spine[i].x;
        const double dy = spine[i + 1].y - spine[i].y;
        const double len = std::hypot(dx, dy);
        if (len < geometry::kZeroEps) continue;
        const Point2D n{ -dy / len * halfWidth, dx / len * halfWidth };
        std::vector<Point2D> band{
            { spine[i].x + n.x,     spine[i].y + n.y },
            { spine[i + 1].x + n.x, spine[i + 1].y + n.y },
            { spine[i + 1].x - n.x, spine[i + 1].y - n.y },
            { spine[i].x - n.x,     spine[i].y - n.y },
        };
        makeCCW(band);
        out.push_back(std::move(band));
        out.push_back(disc(spine[i], halfWidth, arcSegments));
    }
    out.push_back(disc(spine.back(), halfWidth, arcSegments));
    return out;
}

}  // namespace

SlotPathInfo analyzeSlotPath(const std::vector<Entity>& all,
                             const std::vector<int>& pathIds,
                             double tolerance)
{
    SlotPathInfo info;

    if (pathIds.empty()) {
        info.reason = "no path given";
        return info;
    }

    struct Elem { int id; Ends ends; const Entity* e; };
    std::vector<Elem> elems;
    for (int id : pathIds) {
        const Entity* e = findEntityById(all, id);
        if (!e) {
            info.reason = "entity " + std::to_string(id) + " is not in this sketch";
            return info;
        }
        if (e->type == EntityType::Text || e->type == EntityType::Dimension ||
            e->type == EntityType::Point) {
            info.reason = "entity " + std::to_string(id) +
                          " has no length for a slot to follow";
            return info;
        }
        const Ends ends = endsOf(*e);
        if (!ends.ok) {
            info.reason = "entity " + std::to_string(id) + " has no usable length";
            return info;
        }
        elems.push_back({ id, ends, e });
    }

    // Build the vertex list by merging endpoints that coincide.
    struct V { Point2D p; std::vector<std::pair<size_t, int>> arms; };  // (elem, which end)
    std::vector<V> verts;
    auto vertexFor = [&](const Point2D& p) -> size_t {
        for (size_t i = 0; i < verts.size(); ++i) {
            if (same(verts[i].p, p, tolerance)) return i;
        }
        verts.push_back({ p, {} });
        return verts.size() - 1;
    };
    for (size_t i = 0; i < elems.size(); ++i) {
        verts[vertexFor(elems[i].ends.a)].arms.push_back({ i, 0 });
        verts[vertexFor(elems[i].ends.b)].arms.push_back({ i, 1 });
    }

    for (const auto& v : verts) {
        info.vertices.push_back({ v.p, static_cast<int>(v.arms.size()) });
        if (v.arms.size() >= 3) ++info.hubCount;
    }

    // Everything must hang together: a slot is one shape, not several.
    std::vector<bool> seen(elems.size(), false);
    std::vector<size_t> stack{ 0 };
    seen[0] = true;
    size_t reached = 1;
    while (!stack.empty()) {
        const size_t cur = stack.back();
        stack.pop_back();
        for (const auto& v : verts) {
            bool touches = false;
            for (const auto& [ei, which] : v.arms) if (ei == cur) touches = true;
            if (!touches) continue;
            for (const auto& [ei, which] : v.arms) {
                if (!seen[ei]) { seen[ei] = true; ++reached; stack.push_back(ei); }
            }
        }
    }
    if (reached != elems.size()) {
        info.reason = "the entities are not all connected; a slot follows one path";
        return info;
    }

    int freeEnds = 0;
    for (const auto& v : verts) if (v.arms.size() == 1) ++freeEnds;

    // A closed primitive puts both its ends at one point, which reads as a
    // single vertex of degree 2: correct, and it means no free ends.
    for (const auto& el : elems) {
        if (isClosedShape(*el.e)) { freeEnds = 0; break; }
    }

    if (info.hubCount > 0) {
        info.kind = SlotPathKind::Branching;
    } else if (freeEnds == 0) {
        info.kind = SlotPathKind::ClosedLoop;
    } else if (freeEnds == 2) {
        info.kind = SlotPathKind::OpenPath;
    } else {
        info.reason = "the path has " + std::to_string(freeEnds) +
                      " loose ends; expected none (a loop) or two (a chain)";
        return info;
    }

    // Smallest angle between two arms at any hub. The notch between
    // adjacent branches sits at halfWidth / sin(angle/2) from the hub.
    info.minBranchAngle = 360.0;
    bool anyHub = false;
    for (const auto& v : verts) {
        if (v.arms.size() < 3) continue;
        anyHub = true;
        std::vector<double> angles;
        for (const auto& [ei, which] : v.arms) {
            const Point2D d = which == 0 ? elems[ei].ends.dirA : elems[ei].ends.dirB;
            angles.push_back(std::atan2(d.y, d.x));
        }
        std::sort(angles.begin(), angles.end());
        for (size_t i = 0; i < angles.size(); ++i) {
            double gap = (i + 1 < angles.size())
                ? angles[i + 1] - angles[i]
                : angles.front() + 2 * M_PI - angles.back();
            info.minBranchAngle = std::min(info.minBranchAngle,
                                           radiansToDegrees(gap));
        }
    }
    if (!anyHub) info.minBranchAngle = 0.0;

    info.minCurveRadius = std::numeric_limits<double>::infinity();
    for (const auto& el : elems) {
        if (el.e->type == EntityType::Arc) {
            info.minCurveRadius = std::min(info.minCurveRadius, el.e->radius);
        }
    }

    // Walk order, for the shapes that have one.
    if (info.kind != SlotPathKind::Branching) {
        size_t startElem = 0, startEnd = 0;
        for (size_t vi = 0; vi < verts.size(); ++vi) {
            if (verts[vi].arms.size() == 1) {
                startElem = verts[vi].arms[0].first;
                startEnd = static_cast<size_t>(verts[vi].arms[0].second);
                break;
            }
        }
        std::vector<bool> used(elems.size(), false);
        size_t cur = startElem;
        Point2D at = startEnd == 0 ? elems[cur].ends.b : elems[cur].ends.a;
        used[cur] = true;
        info.orderedIds.push_back(elems[cur].id);
        for (size_t step = 1; step < elems.size(); ++step) {
            bool moved = false;
            for (size_t i = 0; i < elems.size() && !moved; ++i) {
                if (used[i]) continue;
                if (same(elems[i].ends.a, at, tolerance)) {
                    at = elems[i].ends.b; used[i] = true;
                    info.orderedIds.push_back(elems[i].id); moved = true;
                } else if (same(elems[i].ends.b, at, tolerance)) {
                    at = elems[i].ends.a; used[i] = true;
                    info.orderedIds.push_back(elems[i].id); moved = true;
                }
            }
            if (!moved) break;
        }
    }

    info.valid = true;
    return info;
}

std::string slotWidthProblem(const SlotPathInfo& info, double halfWidth)
{
    if (!info.valid) return info.reason;
    if (!slotWidthIsPositive(2.0 * halfWidth)) return "the width must be greater than zero";

    // Equality is allowed (Aaron: "width/2 is less than or equal to the arc
    // radius"): the inner edge then reaches the center, which the disc sweep
    // draws without trouble. Only past it is the shape inside out.
    if (halfWidth > info.minCurveRadius) {
        return "a half-width of " + formatValue(halfWidth) +
               " does not fit on a curve of radius " +
               formatValue(info.minCurveRadius) +
               ": the inner edge would pass through the center";
    }

    // A sharp branch is NOT refused. Aaron, 2026-08-28: "even if a slot
    // isn't meaningful on a branch, I consider it allowable. Just not
    // recommended." The shape is constructible; it is only unlikely to be
    // what someone wanted. See slotWidthWarning().
    return {};
}

std::string slotWidthWarning(const SlotPathInfo& info, double halfWidth)
{
    if (!info.valid || !slotWidthIsPositive(2.0 * halfWidth)) return {};
    if (info.kind != SlotPathKind::Branching || info.minBranchAngle <= 0.0)
        return {};

    // The notch between adjacent branches sits at halfWidth / sin(a/2)
    // from the hub. Past a few half-widths it reaches beyond the branches
    // themselves and the shape stops resembling branches meeting.
    const double sn = std::sin(degreesToRadians(info.minBranchAngle) / 2.0);
    if (sn <= geometry::kZeroEps) return "branches are collinear; the junction has no notch";
    const double notch = halfWidth / sn;
    if (notch > halfWidth * 4.0) {
        return "branches meet at " + formatValue(info.minBranchAngle) +
               " degrees, so the notch between them reaches " +
               formatValue(notch) + " from the junction; the slot is "
               "buildable but probably not what was wanted";
    }
    return {};
}

std::vector<Point2D> slotOutline(const std::vector<Entity>& all,
                                 const std::vector<int>& pathIds,
                                 double halfWidth,
                                 int arcSegments)
{
    const SlotPathInfo info = analyzeSlotPath(all, pathIds);
    if (!info.valid || !slotWidthIsPositive(2.0 * halfWidth)) return {};

    std::vector<std::vector<Point2D>> parts;
    for (int id : pathIds) {
        const Entity* e = findEntityById(all, id);
        if (!e) return {};
        for (auto& p : sweepPieces(*e, halfWidth, arcSegments)) {
            if (p.size() >= 3) parts.push_back(std::move(p));
        }
    }
    if (parts.empty()) return {};

    // Union in order. Pieces from one element always overlap their
    // neighbors, and elements meeting at a vertex share a disc, so the
    // running result stays a single region throughout.
    std::vector<Point2D> acc = parts.front();
    for (size_t i = 1; i < parts.size(); ++i) {
        const geometry::BooleanResult r = geometry::polygonUnion(acc, parts[i]);
        // Refuse rather than salvage. Picking the largest region when a
        // union splits looks like it recovers, but it silently DROPS area:
        // measured against known shapes it gave a circle-path slot 63
        // square mm where 1508 was right. A wrong outline that renders is
        // worse than none.
        if (!r.success || r.polygons.size() != 1) return {};
        acc = r.polygons.front().outer;
    }
    return acc;
}

bool updateSlotOutlineFromPaths(Entity& slot, const std::vector<Entity>& all,
                                int arcSegments)
{
    if (slot.type != EntityType::Slot) return false;
    if (slot.pathEntityIds.empty()) return false;
    // slot.radius is the half-width (see createSlot/createArcSlot).
    std::vector<Point2D> outline =
        slotOutline(all, slot.pathEntityIds, slot.radius, arcSegments);
    if (outline.size() < 3) return false;   // unsweepable: leave the cache be
    slot.outlineCache = std::move(outline);
    return true;
}

}  // namespace sketch
}  // namespace hobbycad
