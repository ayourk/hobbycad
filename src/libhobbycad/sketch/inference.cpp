// =====================================================================
//  src/libhobbycad/sketch/inference.cpp — Drawing-time inference
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================

#include "hobbycad/sketch/inference.h"
#include <hobbycad/units.h>

#include <cmath>

namespace hobbycad {
namespace sketch {

namespace {

double lengthOf(const Point2D& v) { return std::sqrt(v.x * v.x + v.y * v.y); }
double dot(const Point2D& a, const Point2D& b) { return a.x * b.x + a.y * b.y; }

/// Smallest angle in [0, 90] between two directions given as degrees, treating
/// a line and its reverse as the same (so 179 degrees is 1 degree off 180).
double lineAngleGapDeg(double aDeg, double bDeg)
{
    double d = std::fmod(std::fabs(aDeg - bDeg), 180.0);
    if (d > 90.0) d = 180.0 - d;
    return d;
}

/// Project (p1 - p0) onto the unit direction u and return the point on the
/// line through p0 along u. Preserves the along-axis extent the user drew.
Point2D projectOnto(const Point2D& p0, const Point2D& p1, const Point2D& u)
{
    const double t = dot(p1 - p0, u);
    return p0 + u * t;
}

/// A guide line through p0 along direction u, long enough to read on screen.
/// The length is derived from the segment so it scales with the drawing.
void makeGuide(const Point2D& p0, const Point2D& p1, const Point2D& u,
               Point2D& a, Point2D& b)
{
    double half = lengthOf(p1 - p0) * 1.5;
    if (half < geometry::kDegenerateLen) half = 1.0;
    a = p0 - u * half;
    b = p0 + u * half;
}

}  // namespace

InferenceResult inferSegment(
    const std::vector<Entity>& entities,
    const Point2D& p0,
    const Point2D& p1,
    double angleTolDeg,
    int excludeId)
{
    InferenceResult result;
    result.adjusted = p1;

    const Point2D dir = p1 - p0;
    const double len = lengthOf(dir);
    if (len < geometry::kZeroEps) return result;  // no direction yet

    const double angDeg = radiansToDegrees(std::atan2(dir.y, dir.x));

    // Gather every candidate within tolerance, then pick the best. Axis
    // alignments (H/V) are weighted to win ties against parallel/perp, which
    // matches how Fusion and Onshape prefer the axes.
    struct Cand {
        InferenceKind kind;
        ConstraintType constraint;
        int refId;
        Point2D u;        // unit direction the segment should take
        double gap;       // angular distance to the target, degrees
        double score;     // gap adjusted by preference (lower is better)
    };
    std::vector<Cand> cands;

    // Horizontal: target direction (1,0).
    {
        const double gap = lineAngleGapDeg(angDeg, 0.0);
        if (gap <= angleTolDeg)
            cands.push_back({InferenceKind::Horizontal, ConstraintType::Horizontal,
                             -1, {1.0, 0.0}, gap, gap * 0.5});
    }
    // Vertical: target direction (0,1).
    {
        const double gap = lineAngleGapDeg(angDeg, 90.0);
        if (gap <= angleTolDeg)
            cands.push_back({InferenceKind::Vertical, ConstraintType::Vertical,
                             -1, {0.0, 1.0}, gap, gap * 0.5});
    }

    // Parallel / perpendicular to each existing line.
    for (const auto& e : entities) {
        if (e.type != EntityType::Line) continue;
        if (e.id == excludeId) continue;
        if (e.points.size() < 2) continue;
        const Point2D lv = e.points[1] - e.points[0];
        const double llen = lengthOf(lv);
        if (llen < geometry::kZeroEps) continue;
        const double lineDeg = radiansToDegrees(std::atan2(lv.y, lv.x));
        const Point2D u = lv / llen;

        const double gapPar = lineAngleGapDeg(angDeg, lineDeg);
        if (gapPar <= angleTolDeg) {
            cands.push_back({InferenceKind::Parallel, ConstraintType::Parallel,
                             e.id, u, gapPar, gapPar});
        }
        const double gapPerp = lineAngleGapDeg(angDeg, lineDeg + 90.0);
        if (gapPerp <= angleTolDeg) {
            const Point2D uPerp{-u.y, u.x};
            cands.push_back({InferenceKind::Perpendicular, ConstraintType::Perpendicular,
                             e.id, uPerp, gapPerp, gapPerp});
        }
    }

    if (cands.empty()) return result;

    const Cand* best = &cands.front();
    for (const auto& c : cands)
        if (c.score < best->score) best = &c;

    Inference inf;
    inf.kind = best->kind;
    inf.constraint = best->constraint;
    inf.refEntityId = best->refId;
    inf.residualDeg = best->gap;
    // Snap the moving end onto the inferred line, keeping the extent drawn.
    inf.adjusted = projectOnto(p0, p1, best->u);
    makeGuide(p0, inf.adjusted, best->u, inf.guideA, inf.guideB);
    inf.glyphAt = inf.adjusted;

    result.adjusted = inf.adjusted;
    result.inferences.push_back(inf);
    return result;
}

}  // namespace sketch
}  // namespace hobbycad
