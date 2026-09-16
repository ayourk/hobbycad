// =====================================================================
//  src/libhobbycad/sketch/bezier.cpp — editing stored cubic Bezier splines
//  SPDX-License-Identifier: GPL-3.0-only
//  Part of HobbyCAD (ayourk/hobbycad)
// =====================================================================

#include <hobbycad/sketch/bezier.h>
#include <hobbycad/geometry/utils.h>
#include <hobbycad/units.h>

#include <algorithm>
#include <cmath>

namespace hobbycad {
namespace sketch {

namespace {

Point3 at(const Point2D& p) { return Point3{p.x, p.y, 0.0}; }

}  // namespace

bool isBezierEntity(const Entity& e)
{
    return e.type == EntityType::Spline && e.splineBezier;
}

bool bezierAnchorInfo(const Entity& e, int a, BezierAnchorInfo& out)
{
    if (!isBezierEntity(e)) return false;
    const int n = static_cast<int>(e.points.size());
    if (a < 0 || a >= n) return false;
    const Point2D anchor(e.points[a]);
    const Point2D fwd = (a < n - 1) ? Point2D(e.points[a + 1]) - anchor
                                    : anchor - Point2D(e.points[a - 1]);
    out.angleDeg = normalizeAngle360(geometry::vectorAngle(fwd));
    out.inLen  = (a - 1 >= 0) ? geometry::lineLength(anchor, e.points[a - 1]) : 0.0;
    out.outLen = (a + 1 <  n) ? geometry::lineLength(anchor, e.points[a + 1]) : 0.0;
    out.rational = e.splineRational && e.weights.size() == e.points.size();
    out.weight = (out.rational && a < static_cast<int>(e.weights.size())) ? e.weights[a] : 1.0;
    return true;
}

bool setBezierAnchorAngle(Entity& e, int a, double angleDeg)
{
    if (!isBezierEntity(e)) return false;
    const int n = static_cast<int>(e.points.size());
    if (a < 0 || a >= n) return false;
    const Point2D anchor(e.points[a]);
    const double rad = degreesToRadians(angleDeg);
    const Point2D u(std::cos(rad), std::sin(rad));
    if (a + 1 < n) {
        const double L = geometry::lineLength(anchor, e.points[a + 1]);
        e.points[a + 1] = at(anchor + u * L);
    }
    if (a - 1 >= 0) {
        const double L = geometry::lineLength(anchor, e.points[a - 1]);
        e.points[a - 1] = at(anchor - u * L);
    }
    return true;
}

bool setBezierAnchorHandleLength(Entity& e, int a, bool outHandle, double len)
{
    if (!isBezierEntity(e)) return false;
    const int n = static_cast<int>(e.points.size());
    const int h = outHandle ? a + 1 : a - 1;
    if (a < 0 || a >= n || h < 0 || h >= n || len < 0) return false;
    const Point2D anchor(e.points[a]);
    const Point2D dir = Point2D(e.points[h]) - anchor;
    const double cur = geometry::length(dir);
    if (cur < geometry::kZeroEps) return false;
    e.points[h] = at(anchor + dir * (len / cur));
    return true;
}

bool setBezierAnchorWeight(Entity& e, int a, double weight)
{
    if (!isBezierEntity(e)) return false;
    if (!(weight > 0.0)) return false;
    const int n = static_cast<int>(e.points.size());
    if (a < 0 || a >= n) return false;
    if (!e.splineRational || e.weights.size() != e.points.size()) {
        e.splineRational = true;
        e.weights.assign(e.points.size(), 1.0);
    }
    e.weights[a] = weight;
    return true;
}

int deleteBezierAnchor(Entity& e, int a)
{
    if (!isBezierEntity(e)) return -1;
    const int n = static_cast<int>(e.points.size());
    const int nseg = (n - 1) / 3;
    if (nseg < 2) return -1;
    if (a < 0 || a >= n || a % 3 != 0) return -1;
    int lo;
    if (a == 0)            lo = 0;
    else if (a == n - 1)   lo = n - 3;
    else                   lo = a - 1;
    e.points.erase(e.points.begin() + lo, e.points.begin() + lo + 3);
    if (e.splineRational && static_cast<int>(e.weights.size()) == n)
        e.weights.erase(e.weights.begin() + lo, e.weights.begin() + lo + 3);
    return lo;
}

int insertBezierFitPoint(Entity& e, const Point2D& near)
{
    if (!isBezierEntity(e)) return -1;
    const int n = static_cast<int>(e.points.size());
    const int nseg = (n - 1) / 3;
    if (nseg < 1) return -1;
    auto cubic = [&](int s, double t) {
        const double u = 1 - t;
        const Point2D b0(e.points[3*s]), b1(e.points[3*s+1]), b2(e.points[3*s+2]), b3(e.points[3*s+3]);
        return b0 * (u*u*u) + b1 * (3*u*u*t) + b2 * (3*u*t*t) + b3 * (t*t*t);
    };
    constexpr int kSamples = 32;
    int bestSeg = 0; double bestT = 0.5, bestD = 1e30;
    for (int s = 0; s < nseg; ++s)
        for (int i = 1; i < kSamples; ++i) {
            const double t = static_cast<double>(i) / kSamples;
            const double d = geometry::lineLength(cubic(s, t), near);
            if (d < bestD) { bestD = d; bestSeg = s; bestT = t; }
        }
    const int b = 3 * bestSeg;
    const Point2D b0(e.points[b]), b1(e.points[b+1]), b2(e.points[b+2]), b3(e.points[b+3]);
    const double t = bestT;
    const Point2D p01 = geometry::lerp(b0, b1, t), p12 = geometry::lerp(b1, b2, t), p23 = geometry::lerp(b2, b3, t);
    const Point2D p012 = geometry::lerp(p01, p12, t), p123 = geometry::lerp(p12, p23, t);
    const Point2D p0123 = geometry::lerp(p012, p123, t);
    const std::vector<Point3> repl = { at(p01), at(p012), at(p0123), at(p123), at(p23) };
    e.points.erase(e.points.begin() + b + 1, e.points.begin() + b + 3);
    e.points.insert(e.points.begin() + b + 1, repl.begin(), repl.end());
    if (e.splineRational && static_cast<int>(e.weights.size()) == n) {
        const double w = e.weights[b + 1];
        e.weights.erase(e.weights.begin() + b + 1, e.weights.begin() + b + 3);
        e.weights.insert(e.weights.begin() + b + 1, 5, w);
    }
    return b + 1;
}

bool setBezierLegLength(Entity& e, int i0, int i1, double len)
{
    if (!isBezierEntity(e)) return false;
    const int n = static_cast<int>(e.points.size());
    if (i0 < 0 || i1 >= n || len < 0) return false;
    int fixed = i0, moved = i1;
    if (i1 % 3 == 0 && i0 % 3 != 0) { fixed = i1; moved = i0; }
    const Point2D f(e.points[fixed]);
    const Point2D dir = Point2D(e.points[moved]) - f;
    const double cur = geometry::length(dir);
    if (cur < geometry::kZeroEps) return false;
    e.points[moved] = at(f + dir * (len / cur));
    return true;
}

bool toggleBezierClosed(Entity& e)
{
    if (!isBezierEntity(e)) return false;
    const int n = static_cast<int>(e.points.size());
    if (!e.splineClosed) {
        if (n < 4 || (n - 1) % 3 != 0) return false;
        const Point2D P0(e.points[0]);
        const Point2D P1(e.points[n >= 4 ? 3 : 0]);
        const Point2D Plast(e.points[n - 1]);
        const Point2D Pprev(e.points[n >= 4 ? n - 4 : 0]);
        const Point2D lastOut = Plast + (P0 - Pprev) / 6.0;
        const Point2D firstIn = P0 - (P1 - Plast) / 6.0;
        e.points.push_back(at(lastOut));
        e.points.push_back(at(firstIn));
        if (e.splineRational && static_cast<int>(e.weights.size()) == n) {
            e.weights.push_back(1.0); e.weights.push_back(1.0);
        }
        e.splineClosed = true;
    } else {
        if (n < 6) return false;
        e.points.pop_back(); e.points.pop_back();
        if (e.splineRational && static_cast<int>(e.weights.size()) == n) {
            e.weights.pop_back(); e.weights.pop_back();
        }
        e.splineClosed = false;
    }
    return true;
}

bool remapSplinePointIndices(Constraint& c, int splineId, int lo, int count)
{
    for (std::size_t k = 0; k < c.entityIds.size(); ++k) {
        if (c.entityIds[k] != splineId) continue;
        if (k >= c.pointIndices.size()) continue;
        int& idx = c.pointIndices[k];
        if (count > 0) {
            if (idx >= lo && idx < lo + count) return true;
            if (idx >= lo + count) idx -= count;
        } else {
            if (idx >= lo) idx += -count;
        }
    }
    return false;
}

void autoBezierHandles(std::vector<BezierAnchor>& anchors, const std::vector<bool>& manual)
{
    const int n = static_cast<int>(anchors.size());
    for (int i = 0; i < n; ++i) {
        if (i < static_cast<int>(manual.size()) && manual[i]) continue;
        const Point2D Pi = anchors[i].pos;
        const Point2D Pp = (i > 0)     ? anchors[i - 1].pos : Pi;
        const Point2D Pn = (i < n - 1) ? anchors[i + 1].pos : Pi;
        const Point2D t{ (Pn.x - Pp.x) / 6.0, (Pn.y - Pp.y) / 6.0 };
        BezierAnchor& a = anchors[i];
        if (i < n - 1) { a.hasOut = true;  a.outHandle = { Pi.x + t.x, Pi.y + t.y }; }
        else           { a.hasOut = false; }
        if (i > 0)     { a.hasIn = true;   a.inHandle  = { Pi.x - t.x, Pi.y - t.y }; }
        else           { a.hasIn = false; }
    }
}

}  // namespace sketch
}  // namespace hobbycad
