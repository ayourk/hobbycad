// =====================================================================
//  src/libhobbycad/sketch/placement_spline.cpp — placing a fit-point
//  spline or a conic arc (the Bezier pen is BezierPen)
// =====================================================================
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include "placement_detail.h"

#include <hobbycad/sketch/operations.h>
#include <hobbycad/sketch/queries.h>

namespace hobbycad {
namespace sketch {
namespace placement_detail {

namespace {

/// The conic's rho: from the fourth point once there is one, else 0.5 (a
/// parabola).
double conicRho(const std::vector<Point2D>& p)
{
    return p.size() >= 4 ? conicRhoFromPoint(p[0], p[1], p[2], p[3]) : 0.5;
}

std::vector<Point2D> withCursor(const PlacementInput& in)
{
    std::vector<Point2D> pts = in.clicks;
    pts.push_back(in.cursor);
    return pts;
}

}  // namespace

Point2D splineCursor(CreationMode m, const PlacementInput& in)
{
    // Choosing rho, the cursor rides the segment from the chord's midpoint
    // to the apex, onto the shoulder that is ON the curve. Nothing releases
    // it: rho is a fraction of that line and nothing else.
    if (m != CreationMode::SplineConic || in.clicks.size() < 3) return in.cursor;
    const std::vector<Point2D>& p = in.clicks;
    return conicShoulder(p[0], p[1], p[2], conicRhoFromPoint(p[0], p[1], p[2], in.cursor));
}

bool splineEntity(CreationMode m, const PlacementInput& in, Entity& out)
{
    if (m == CreationMode::SplineConic) {
        // One exact rational cubic with its rho, the rule the ghost used.
        std::vector<Point2D> p = in.clicks;
        if (p.size() < 4) p.push_back(in.cursor);
        if (p.size() < 3) return false;
        const bool construction = out.isConstruction;
        if (!conicFromRho(out.id, p[0], p[1], p[2], conicRho(p), out)) return false;
        out.isConstruction = construction;
        return true;
    }
    // Fit points: the clicks, a Catmull-Rom curve through them. The pen
    // builds its own entity (BezierPen::entity).
    out.type = EntityType::Spline;
    out.splineBezier = false;
    out.splineRational = false;
    out.points.assign(in.clicks.begin(), in.clicks.end());
    return in.clicks.size() >= 2;
}

PlacementPreview splinePreview(CreationMode m, const PlacementInput& in)
{
    PlacementPreview out;
    if (in.clicks.empty()) return out;
    const std::vector<Point2D> pts = withCursor(in);

    if (m == CreationMode::SplineConic) {
        Entity ghost;
        const bool choosingRho = in.clicks.size() >= 3
            && conicFromRho(0, pts[0], pts[1], pts[2], conicRho(pts), ghost);
        // Ends and apex being chosen: a rubber line from the last click.
        placedClicks(in, out.shapes, !choosingRho);
        if (!choosingRho) return out;
        // Rho being chosen: the two tangent legs, the curve solid, the
        // shoulder ringed where the cursor rides.
        out.shapes.push_back(path({pts[0], pts[2], pts[1]}));
        out.shapes.push_back(path(tessellate(ghost, 96), PreviewStroke::Solid));
        out.shapes.push_back(mark(in.cursor, PreviewMark::Cursor));
        return out;
    }

    // Fit points: the curve through the clicks and the cursor.
    if (pts.size() == 2) {
        out.shapes.push_back(path(pts));
    } else {
        const std::vector<Point3> ctrl(pts.begin(), pts.end());
        const std::vector<Point3> tess = tessellateSpline(ctrl, 16, false);
        out.shapes.push_back(path(std::vector<Point2D>(tess.begin(), tess.end())));
    }
    for (const Point2D& p : pts) out.shapes.push_back(mark(p, PreviewMark::Click));
    return out;
}

}  // namespace placement_detail
}  // namespace sketch
}  // namespace hobbycad
