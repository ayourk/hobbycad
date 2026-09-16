// =====================================================================
//  src/libhobbycad/brep/slice.cpp — mesh cross-section slicing
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#include <hobbycad/brep/slice.h>
#include <hobbycad/geometry/utils.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

#include <gp_Vec.hxx>
#include <gp_Dir.hxx>
#include <Poly_Triangle.hxx>

namespace hobbycad {
namespace brep {

namespace {
inline double dist2d(const Point2D& a, const Point2D& b) {
    return std::hypot(a.x - b.x, a.y - b.y);
}

// Triangle-plane intersection -> the segment(s) as 2D points in the plane frame.
// Collects segments from a triangle list, then chains them into loops.
std::vector<SliceLoop> chainSegments(
    const std::vector<std::pair<Point2D, Point2D>>& segs, double weld) {
    std::vector<SliceLoop> loops;
    if (segs.empty()) return loops;

    const double inv = 1.0 / weld;
    auto key = [&](const Point2D& p) {
        return std::make_pair(static_cast<long long>(std::llround(p.x * inv)),
                              static_cast<long long>(std::llround(p.y * inv)));
    };
    // point key -> segment indices touching it
    std::map<std::pair<long long, long long>, std::vector<int>> touch;
    for (int i = 0; i < static_cast<int>(segs.size()); ++i) {
        touch[key(segs[i].first)].push_back(i);
        touch[key(segs[i].second)].push_back(i);
    }
    std::vector<bool> used(segs.size(), false);

    for (int s = 0; s < static_cast<int>(segs.size()); ++s) {
        if (used[s]) continue;
        SliceLoop loop;
        used[s] = true;
        const Point2D start = segs[s].first;
        Point2D cur = segs[s].second;
        loop.points.push_back(start);
        loop.points.push_back(cur);
        bool closed = false;
        for (std::size_t guard = 0; guard < segs.size() + 2; ++guard) {
            if (dist2d(cur, start) <= weld) { closed = true; break; }
            // find the next unused segment sharing `cur`
            int next = -1;
            auto it = touch.find(key(cur));
            if (it != touch.end())
                for (int cand : it->second)
                    if (!used[cand]) { next = cand; break; }
            if (next < 0) break;   // open chain (non-watertight)
            used[next] = true;
            // step to the far endpoint of `next`
            cur = (dist2d(segs[next].first, cur) <= weld) ? segs[next].second
                                                          : segs[next].first;
            loop.points.push_back(cur);
        }
        // drop the duplicated closing point
        if (closed && loop.points.size() > 1 &&
            dist2d(loop.points.front(), loop.points.back()) <= weld)
            loop.points.pop_back();
        loop.closed = closed;
        if (loop.points.size() >= 2) loops.push_back(std::move(loop));
    }
    return loops;
}
}  // namespace

std::vector<SliceLoop> sliceTriangles(
    const std::vector<std::array<gp_Pnt, 3>>& tris, const gp_Pln& plane,
    double weld) {
    const gp_Pnt o = plane.Location();
    const gp_Vec n(plane.Axis().Direction());
    const gp_Vec xd(plane.Position().XDirection());
    const gp_Vec yd(plane.Position().YDirection());
    auto sdist = [&](const gp_Pnt& p) { return gp_Vec(o, p).Dot(n); };
    auto to2d = [&](const gp_Pnt& p) -> Point2D {
        const gp_Vec v(o, p);
        return { v.Dot(xd), v.Dot(yd) };
    };

    std::vector<std::pair<Point2D, Point2D>> segs;
    for (const auto& t : tris) {
        const double d[3] = { sdist(t[0]), sdist(t[1]), sdist(t[2]) };
        std::vector<Point2D> xs;
        for (int e = 0; e < 3; ++e) {
            const int a = e, b = (e + 1) % 3;
            const double da = d[a], db = d[b];
            if ((da < 0 && db > 0) || (da > 0 && db < 0)) {
                const double tt = da / (da - db);
                const gp_Pnt p(t[a].X() + tt * (t[b].X() - t[a].X()),
                               t[a].Y() + tt * (t[b].Y() - t[a].Y()),
                               t[a].Z() + tt * (t[b].Z() - t[a].Z()));
                xs.push_back(to2d(p));
            } else if (da == 0.0) {
                xs.push_back(to2d(t[a]));   // vertex exactly on the plane
            }
        }
        if (xs.size() >= 2 && dist2d(xs[0], xs[1]) > weld)
            segs.push_back({ xs[0], xs[1] });
        else if (xs.size() >= 3 && dist2d(xs[0], xs[2]) > weld)
            segs.push_back({ xs[0], xs[2] });
    }
    return chainSegments(segs, weld);
}

std::vector<SliceLoop> sliceMesh(const Handle(Poly_Triangulation)& mesh,
                                 const gp_Pln& plane, double weld) {
    std::vector<std::array<gp_Pnt, 3>> tris;
    if (mesh.IsNull()) return {};
    const int nt = mesh->NbTriangles();
    tris.reserve(static_cast<std::size_t>(nt));
    for (int i = 1; i <= nt; ++i) {
        int n1, n2, n3;
        mesh->Triangle(i).Get(n1, n2, n3);
        tris.push_back({ mesh->Node(n1), mesh->Node(n2), mesh->Node(n3) });
    }
    return sliceTriangles(tris, plane, weld);
}


std::vector<SliceContour> classifySection(const std::vector<SliceLoop>& loops) {
    // Keep only real loops (>= 3 points).
    std::vector<const SliceLoop*> in;
    in.reserve(loops.size());
    for (const auto& l : loops) if (l.points.size() >= 3) in.push_back(&l);
    const int n = static_cast<int>(in.size());

    // Representative point per loop for nesting tests: a point ON the loop's own
    // boundary (its first vertex). Loop i is inside loop j iff a boundary point
    // of i lies inside the FILLED polygon j (slice loops never cross). A vertex,
    // not the centroid, is essential: a ring's centroid sits in its hole,
    // so it would test as "inside" its own inner loop and misclassify annuli.
    std::vector<Point2D> rep(n);
    for (int i = 0; i < n; ++i) rep[i] = in[i]->points[0];

    // depth[i] = number of OTHER loops that contain loop i's interior point.
    std::vector<int> depth(n, 0);
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j)
            if (i != j && geometry::pointInPolygon(rep[i], in[j]->points))
                ++depth[i];

    // Immediate parent of loop i = the container with the greatest depth
    // (the innermost loop that still contains i); -1 if none.
    std::vector<int> parent(n, -1);
    for (int i = 0; i < n; ++i) {
        int best = -1, bestDepth = -1;
        for (int j = 0; j < n; ++j) {
            if (i == j) continue;
            if (!geometry::pointInPolygon(rep[i], in[j]->points)) continue;
            if (depth[j] > bestDepth) { bestDepth = depth[j]; best = j; }
        }
        parent[i] = best;
    }

    // Even depth = outer (solid); odd = hole in its immediate parent outer.
    // Build one SliceContour per outer, index by loop id for hole attachment.
    std::vector<SliceContour> out;
    std::vector<int> contourOf(n, -1);  // loop index -> index into `out`
    auto oriented = [](const SliceLoop& l, bool wantCCW) -> SliceLoop {
        SliceLoop r = l;
        const bool ccw = geometry::polygonArea(l.points) > 0.0;
        if (ccw != wantCCW) std::reverse(r.points.begin(), r.points.end());
        return r;
    };
    for (int i = 0; i < n; ++i) {
        if (depth[i] % 2 == 0) {              // outer
            SliceContour sc;
            sc.outer = oriented(*in[i], /*wantCCW=*/true);
            contourOf[i] = static_cast<int>(out.size());
            out.push_back(std::move(sc));
        }
    }
    for (int i = 0; i < n; ++i) {
        if (depth[i] % 2 == 1) {              // hole
            const int par = parent[i];
            if (par >= 0 && contourOf[par] >= 0)
                out[contourOf[par]].holes.push_back(oriented(*in[i], /*wantCCW=*/false));
        }
    }
    return out;
}

}  // namespace brep
}  // namespace hobbycad
