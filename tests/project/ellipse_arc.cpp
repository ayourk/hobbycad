// =====================================================================
//  tests/project/ellipse_arc.cpp — rotation and arc range are honored
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  An ellipse carries five numbers: center, major, minor, rotation and an
//  arc range (ellipseStart / ellipseSweep). The renderer, save/load and DXF
//  all honored the last three; the geometry layer did not. A projected or
//  DXF-imported elliptical arc therefore DREW as an arc but was hit tested,
//  measured, tessellated and snapped as a full, unrotated ellipse, and was
//  offered as a closed profile.
//
//  Every check here fails against that older behavior, which is the point.
// =====================================================================
#include <hobbycad/geometry/utils.h>
#include <hobbycad/sketch/constraint.h>
#include <hobbycad/sketch/entity.h>
#include <hobbycad/sketch/queries.h>
#include <hobbycad/sketch/snap.h>
#include <hobbycad/sketch/profiles.h>
#include <cstdio>
#include <cmath>
#include <vector>
using namespace hobbycad;
using namespace hobbycad::sketch;

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what); if (!ok) ++failures;
}
static bool near(double a, double b, double e = 1e-6) { return std::fabs(a - b) < e; }

// The point at a parameter angle, computed independently of the library so a
// bug in the library cannot agree with itself.
static Point2D expectAt(const Entity& e, double pDeg)
{
    const double pr = pDeg * M_PI / 180.0;
    const double th = e.ellipseRotation * M_PI / 180.0;
    const double lx = e.majorRadius * std::cos(pr);
    const double ly = e.minorRadius * std::sin(pr);
    return Point2D(e.points[0].x + lx * std::cos(th) - ly * std::sin(th),
                   e.points[0].y + lx * std::sin(th) + ly * std::cos(th));
}

// Is this point on the ROTATED ellipse? Rotate it back into the ellipse's own
// frame and test the normalized equation there.
static bool onEllipse(const Entity& e, const Point2D& p, double tol = 1e-3)
{
    const double th = e.ellipseRotation * M_PI / 180.0;
    const double dx = p.x - e.points[0].x, dy = p.y - e.points[0].y;
    const double lx =  dx * std::cos(th) + dy * std::sin(th);
    const double ly = -dx * std::sin(th) + dy * std::cos(th);
    const double n = (lx * lx) / (e.majorRadius * e.majorRadius)
                   + (ly * ly) / (e.minorRadius * e.minorRadius);
    return std::fabs(n - 1.0) < tol;
}

int main()
{
    std::printf("ellipse rotation and arc range in the geometry layer\n");

    // A rotated FULL ellipse.
    Entity full = createEllipse(1, Point2D(2.0, -1.0), 5.0, 2.0, 30.0);

    // A rotated QUARTER arc of the same ellipse: parameter 0 to 90.
    Entity arc = createEllipse(2, Point2D(2.0, -1.0), 5.0, 2.0, 30.0);
    arc.ellipseStart = 0.0;
    arc.ellipseSweep = 90.0;

    // ---- pointAtParameter gained an Ellipse case at all ----------------
    {
        // Older code had no Ellipse case in pointAtParameter, so it answered
        // with a default-constructed point for every t.
        const Point2D p0 = pointAtParameter(full, 0.0);
        check(near(p0.x, expectAt(full, 0.0).x, 1e-9)
              && near(p0.y, expectAt(full, 0.0).y, 1e-9),
              "pointAtParameter(t=0) is the major-axis end, turned by the rotation");
        const Point2D pq = pointAtParameter(full, 0.25);
        check(near(pq.x, expectAt(full, 90.0).x, 1e-9)
              && near(pq.y, expectAt(full, 90.0).y, 1e-9),
              "pointAtParameter(t=0.25) is the minor-axis end, turned");
        check(!near(p0.y, -1.0, 1e-9),
              "a rotated ellipse's parameter-0 point is NOT on the center's row");
    }

    // ---- an arc's parameter range is its own, not the whole ellipse ----
    {
        const Point2D s = pointAtParameter(arc, 0.0);
        const Point2D e = pointAtParameter(arc, 1.0);
        check(near(s.x, expectAt(arc, 0.0).x, 1e-9) && near(s.y, expectAt(arc, 0.0).y, 1e-9),
              "the arc starts at its start parameter");
        check(near(e.x, expectAt(arc, 90.0).x, 1e-9) && near(e.y, expectAt(arc, 90.0).y, 1e-9),
              "the arc ends at start + sweep, not back at the beginning");
        check(!(near(s.x, e.x, 1e-6) && near(s.y, e.y, 1e-6)),
              "an arc's two ends are different points (a full ellipse's are not)");
    }

    // ---- tessellation honors both rotation and the range ---------------
    {
        const std::vector<Point2D> tf = tessellate(full, 0.01);
        bool allOn = !tf.empty();
        for (const Point2D& p : tf) if (!onEllipse(full, p)) allOn = false;
        check(allOn, "every tessellated point of a rotated ellipse lies on it");

        const std::vector<Point2D> ta = tessellate(arc, 0.01);
        check(ta.size() >= 2, "an arc tessellates to a polyline");
        const bool endsMatch = ta.size() >= 2
            && near(ta.front().x, expectAt(arc, 0.0).x, 1e-3)
            && near(ta.back().x,  expectAt(arc, 90.0).x, 1e-3)
            && near(ta.back().y,  expectAt(arc, 90.0).y, 1e-3);
        check(endsMatch, "the tessellated arc spans exactly its own range");
        // A quarter arc must not close on itself the way a full ellipse does.
        const double span = ta.size() >= 2
            ? std::hypot(ta.front().x - ta.back().x, ta.front().y - ta.back().y)
            : 0.0;
        check(span > 1.0, "the tessellated arc does not come back to its start");
    }

    // ---- length is the arc's length, not the circumference -------------
    {
        const double lf = entityLength(full);
        const double la = entityLength(arc);
        check(lf > 20.0 && lf < 24.0, "the full ellipse's circumference is about 22.1");
        check(la < lf * 0.5, "a quarter arc is far shorter than the whole circumference");
        check(la > 4.0 && la < 7.0, "the quarter arc's length is about 5.5");
    }

    // ---- hit tests answer about the arc, not the ellipse it came from --
    {
        // A point on the far side of the ellipse, at parameter 225, which the
        // 0..90 arc does NOT cover.
        const Point2D away = expectAt(full, 225.0);
        const Point2D cp = arc.closestPoint(away);
        const Point2D armStart = expectAt(arc, 0.0);
        const Point2D armEnd = expectAt(arc, 90.0);
        const bool atAnEnd =
            (near(cp.x, armStart.x, 1e-6) && near(cp.y, armStart.y, 1e-6)) ||
            (near(cp.x, armEnd.x, 1e-6) && near(cp.y, armEnd.y, 1e-6));
        check(atAnEnd, "closestPoint on an arc returns one of the arc's ends, "
                       "not a point on the part that was cut away");
        check(arc.distanceTo(away) > 1.0,
              "distanceTo an arc is large for a query off the end (it used to be ~0)");
        check(full.distanceTo(away) < 1e-6,
              "the same query is ON the full ellipse, so the two must differ");
    }

    // ---- snap points follow the rotation and the range -----------------
    {
        const std::vector<SnapPoint> sf = collectSnapPoints(full);
        int quads = 0; bool quadsOn = true;
        for (const SnapPoint& s : sf) {
            if (s.type == SnapType::Quadrant) {
                ++quads;
                if (!onEllipse(full, s.position)) quadsOn = false;
            }
        }
        check(quads == 4, "a full ellipse offers four quadrant snaps");
        check(quadsOn, "every quadrant snap lies ON the rotated ellipse");

        const std::vector<SnapPoint> sa = collectSnapPoints(arc);
        int aq = 0, ends = 0;
        for (const SnapPoint& s : sa) {
            if (s.type == SnapType::Quadrant) ++aq;
            if (s.type == SnapType::Endpoint) ++ends;
        }
        check(aq == 2, "a 0..90 arc offers only the two quadrants it reaches");
        check(ends == 2, "an elliptical arc offers its two endpoints as snaps");
    }

    // ---- a partial ellipse is an OPEN curve ----------------------------
    {
        const std::vector<Profile> pf = detectProfiles({full});
        check(pf.size() == 1, "a full ellipse is a closed profile on its own");

        const std::vector<Profile> pa = detectProfiles({arc});
        check(pa.empty(), "an elliptical arc alone is NOT a closed profile "
                          "(it closes a loop only with its neighbors)");
    }

    // ---- the shared helpers the tools, snaps and solver read ------------
    {
        // ellipsePointAtParamDeg places the rotated point (checked against the
        // independent formula above) and ellipseParamDeg reads it back.
        bool formula = true, inverse = true;
        for (double p : {0.0, 37.0, 90.0, 200.0, 315.5}) {
            const Point2D at = ellipsePointAtParamDeg(full, p);
            const Point2D want = expectAt(full, p);
            formula = formula && near(at.x, want.x) && near(at.y, want.y);
            inverse = inverse && near(ellipseParamDeg(full, at), p);
        }
        check(formula, "ellipsePointAtParamDeg places the point on the rotated ellipse");
        check(inverse, "and ellipseParamDeg reads the same parameter back");

        Entity e = full;
        e.ellipseSweep = 359.9995;
        const bool hairShort = isFullEllipse(e);
        e.ellipseSweep = -360.0;
        const bool clockwise = isFullEllipse(e);
        e.ellipseSweep = 359.9;
        const bool almost = isFullEllipse(e);
        check(isFullEllipse(full) && hairShort && clockwise,
              "isFullEllipse: a whole turn, one a hair short, or one clockwise");
        check(!almost && !isFullEllipse(arc), "isFullEllipse: not a 359.9 sweep, not an arc");

        // The circumference against a fine polyline of the same ellipse.
        double walked = 0.0;
        const int steps = 200000;
        Point2D prev = expectAt(full, 0.0);
        for (int i = 1; i <= steps; ++i) {
            const Point2D cur = expectAt(full, 360.0 * i / steps);
            walked += std::hypot(cur.x - prev.x, cur.y - prev.y);
            prev = cur;
        }
        const double c = geometry::ellipseCircumference(full.majorRadius, full.minorRadius);
        check(near(c, walked, walked * 1e-5),
              "ellipseCircumference matches the measured perimeter");
        check(near(geometry::ellipseCircumference(3.0, 3.0), 6.0 * M_PI, 1e-9)
                  && geometry::ellipseCircumference(0.0, 0.0) == 0.0,
              "ellipseCircumference: a circle is 2 pi r, a degenerate one is 0");
    }
    {
        // A Distance naming an ellipse's minor axis point drives that point
        // only; other points, other entities and other types are not driven.
        Constraint d;
        d.type = ConstraintType::Distance;
        d.entityIds = {1, 1};
        d.pointIndices = {0, 2};
        Constraint h = d;
        h.type = ConstraintType::Horizontal;
        check(dimensionDrivesPoint(d, 1, 2) && dimensionDrivesPoint(d, 1, 0),
              "dimensionDrivesPoint: the dimensioned points are driven");
        check(!dimensionDrivesPoint(d, 1, 1) && !dimensionDrivesPoint(d, 7, 2)
                  && !dimensionDrivesPoint(h, 1, 2),
              "dimensionDrivesPoint: not another point, entity or constraint type");
    }

    std::printf("\n%s (%d failure(s))\n", failures ? "FAILURES" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
