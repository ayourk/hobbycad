// =====================================================================
//  tests/project/conic_rho.cpp — the conic arc by rho, stored as a rational cubic
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  Fusion, Onshape and SolidWorks share this curve: start, end, the apex
//  where the end tangents meet, and rho. HobbyCAD stores it as ONE rational
//  cubic Bezier by exact degree elevation. Pinned here, each by a property
//  that does not reuse the construction:
//    - the ends are the ends and the end tangents point at the apex;
//    - the shoulder (t = 0.5, evaluated here as a rational cubic by hand)
//      sits at midpoint + rho * (apex - midpoint), for three rhos;
//    - rho = 0.5 is a parabola, so the weights are all 1 (plain cubic);
//    - a right-angle apex with rho = sqrt(2) - 1 is an exact quarter CIRCLE:
//      every tessellated point is at the radius, which also proves the
//      library tessellates the WEIGHTS and not just the control polygon;
//    - rho from a cursor is its projection onto the midpoint-apex segment,
//      clamped; degenerate placements are refused;
//    - rho is a STORED property (the Fusion route, 2026-09-16): the apex is
//      recovered from the control polygon, setConicRho re-authors the curve
//      keeping ends and tangents, a Bezier handle edit clears the mark, and
//      a project save/load keeps conic_rho.
// =====================================================================
#include <hobbycad/sketch/entity.h>
#include <hobbycad/sketch/queries.h>
#include <hobbycad/sketch/bezier.h>
#include <hobbycad/project.h>
#include <filesystem>
#include <string>
#include <cstdio>
#include <cmath>
#include <vector>
using namespace hobbycad;
using namespace hobbycad::sketch;

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what); if (!ok) ++failures;
}
static bool near(double a, double b, double e = 1e-9) { return std::fabs(a - b) < e; }

/// Rational cubic Bezier at t, by hand.
static Point2D evalRational(const Entity& e, double t)
{
    const double u = 1.0 - t;
    const double B[4] = {u * u * u, 3 * u * u * t, 3 * u * t * t, t * t * t};
    double x = 0, y = 0, w = 0;
    for (int i = 0; i < 4; ++i) {
        const double wi = e.weights.empty() ? 1.0 : e.weights[i];
        x += B[i] * wi * e.points[i].x; y += B[i] * wi * e.points[i].y; w += B[i] * wi;
    }
    return Point2D(x / w, y / w);
}
static bool parallel(double ax, double ay, double bx, double by)
{
    return std::fabs(ax * by - ay * bx)
           < 1e-9 * std::max(1.0, std::hypot(ax, ay) * std::hypot(bx, by));
}
/// Four control points and four weights: what a conic is built as, and what
/// the checks below index. A refused construction leaves the entity empty,
/// and indexing it would be undefined behavior, so every such check is
/// guarded by this and still fails rather than crashing.
static bool rationalCubic(const Entity& e)
{
    return e.points.size() == 4 && e.weights.size() == 4;
}

int main(int argc, char** argv)
{
    std::printf("conic arc by rho\n");
    const Point2D s(0, 0), e(10, 0), a(3, 6);   // an unsymmetric apex

    for (double rho : {0.25, 0.5, 0.75}) {
        Entity c;
        char msg[96];
        std::snprintf(msg, sizeof msg,
                      "rho %.2f: accepted as a single rational cubic segment", rho);
        const bool built = conicFromRho(7, s, e, a, rho, c) && rationalCubic(c);
        check(built && c.id == 7, msg);
        check(built && near(c.points[0].x, 0) && near(c.points[0].y, 0)
              && near(c.points[3].x, 10) && near(c.points[3].y, 0),
              "  the ends are the ends");
        check(built
              && parallel(c.points[1].x - c.points[0].x, c.points[1].y - c.points[0].y,
                          a.x - s.x, a.y - s.y)
              && parallel(c.points[3].x - c.points[2].x, c.points[3].y - c.points[2].y,
                          e.x - a.x, e.y - a.y),
              "  the end tangents point at the apex");
        const Point2D sh = conicShoulder(s, e, a, rho);
        const Point2D at = built ? evalRational(c, 0.5) : Point2D();
        std::snprintf(msg, sizeof msg,
                      "  the shoulder at t=0.5 is midpoint + %.2f * (apex - midpoint)", rho);
        check(built && near(at.x, sh.x, 1e-9) && near(at.y, sh.y, 1e-9), msg);
        check(built && near(c.weights[0], 1) && near(c.weights[3], 1)
              && near(c.weights[1], c.weights[2]),
              "  end weights 1, inner weights equal");
    }
    {
        Entity p;
        const bool built = conicFromRho(1, s, e, a, 0.5, p) && rationalCubic(p);
        check(built && near(p.weights[1], 1.0) && near(p.weights[2], 1.0),
              "rho 0.5 is a parabola: all weights 1 (a plain cubic)");
    }
    {
        // Quarter circle: ends (10,0) and (0,10), apex (10,10), rho = sqrt(2)-1.
        Entity q;
        const double rho = std::sqrt(2.0) - 1.0;
        const bool built = conicFromRho(2, Point2D(10, 0), Point2D(0, 10), Point2D(10, 10),
                                        rho, q);
        check(built, "right-angle apex with rho = sqrt(2)-1 is accepted");
        const bool sized = built && rationalCubic(q);
        double worst = 0.0;
        for (const Point2D& pt : tessellate(q, 64)) {
            worst = std::max(worst, std::fabs(std::hypot(pt.x, pt.y) - 10.0));
        }
        std::printf("    quarter circle: worst radius error over the tessellation = %.2e\n",
                    worst);
        check(sized && worst < 1e-9,
              "and every tessellated point is at radius 10: an exact circle, "
              "rationally tessellated");
        check(sized && near(std::hypot(evalRational(q, 0.3).x, evalRational(q, 0.3).y), 10.0),
              "so is the hand-evaluated point at t=0.3");
    }
    {
        check(near(conicRhoFromPoint(s, e, a, conicShoulder(s, e, a, 0.3)), 0.3),
              "rho from a point ON the midpoint-apex segment reads back exactly");
        check(near(conicRhoFromPoint(s, e, a, Point2D(a.x + 3, a.y + 6)), 0.98),
              "beyond the apex it clamps to 0.98");
        check(near(conicRhoFromPoint(s, e, a, Point2D(5, -4)), 0.02),
              "behind the chord it clamps to 0.02");
        // off the segment sideways: projection decides
        const Point2D mid(5, 0), off(5 + 3, 0 - 1);
        const Point2D beside(mid.x + 0.5 * (a.x - mid.x) + 2 * (a.y - mid.y) * 0.1,
                             mid.y + 0.5 * (a.y - mid.y) - 2 * (a.x - mid.x) * 0.1);
        check(near(conicRhoFromPoint(s, e, a, beside), 0.5, 1e-9),
              "a point beside the segment projects onto it (sideways offset ignored)");
        (void)off;
    }
    {
        Entity bad;
        check(!conicFromRho(3, s, s, a, 0.5, bad), "a zero chord is refused");
        check(!conicFromRho(4, s, e, Point2D(4, 0), 0.5, bad),
              "an apex on the chord's line is refused");
    }

    {
        std::printf("rho as a stored property\n");
        Entity c;
        // Guarded: the checks below index c's control polygon, and copies of
        // it, which a refused construction leaves empty.
        const bool built = conicFromRho(9, s, e, a, 0.3, c) && rationalCubic(c);
        check(built && near(c.conicRho, 0.3), "conicFromRho stores the rho on the entity");
        Point2D apex;
        check(conicApex(c, apex) && near(apex.x, a.x) && near(apex.y, a.y),
              "the apex is recovered from the control polygon "
              "(where the end tangents meet)");
        check(std::string(conicKindName(0.3)) == "elliptical"
              && std::string(conicKindName(0.5)) == "parabolic"
              && std::string(conicKindName(0.7)) == "hyperbolic"
              && std::string(conicKindName(1.5)).empty(),
              "kind names: elliptical below 0.5, parabolic at 0.5, hyperbolic above, "
              "none outside (0,1)");

        Entity edited = c;
        edited.isConstruction = true;   // a field setConicRho must not touch
        check(setConicRho(edited, 0.6), "setConicRho re-authors a conic");
        check(near(edited.conicRho, 0.6) && edited.isConstruction && edited.id == 9,
              "  the new rho is stored; other fields untouched");
        const bool editedSized = rationalCubic(edited);
        check(editedSized && near(edited.points[0].x, 0) && near(edited.points[0].y, 0)
              && near(edited.points[3].x, 10) && near(edited.points[3].y, 0),
              "  the ends stay");
        Point2D apex2;
        check(conicApex(edited, apex2) && near(apex2.x, a.x) && near(apex2.y, a.y),
              "  so does the apex (tangent directions kept)");
        const Point2D sh6 = conicShoulder(s, e, a, 0.6);
        const Point2D at6 = editedSized ? evalRational(edited, 0.5) : Point2D();
        check(editedSized && near(at6.x, sh6.x) && near(at6.y, sh6.y),
              "  the shoulder moved to rho 0.6");
        Entity direct;
        const bool directBuilt = conicFromRho(9, s, e, a, 0.6, direct) && rationalCubic(direct);
        bool same = directBuilt && editedSized;
        for (int i = 0; i < 4; ++i) {
            same = same && near(direct.points[i].x, edited.points[i].x)
                   && near(direct.points[i].y, edited.points[i].y)
                   && near(direct.weights[i], edited.weights[i]);
        }
        check(same,
              "  and the result is the conic authored at 0.6 directly "
              "(idempotent re-authoring)");
        Entity again = edited;
        check(setConicRho(again, 0.6) && editedSized && rationalCubic(again)
              && near(again.points[1].x, edited.points[1].x)
              && near(again.points[2].y, edited.points[2].y),
              "  re-authoring at the same rho changes nothing "
              "(the post-solve resync is stable)");

        Entity plain = createRationalBezierSpline(5, {s, Point2D(1, 3), Point2D(7, 3), e},
                                                  {1, 1, 1, 1});
        check(!(plain.conicRho > 0.0) && !setConicRho(plain, 0.4),
              "a bezier not authored as a conic has no rho and setConicRho refuses it");
        Entity hand = c;
        check(setBezierAnchorHandleLength(hand, 0, true, 2.0) && !(hand.conicRho > 0.0),
              "a Bezier handle edit clears the mark: "
              "a hand-edited curve is no longer the conic as authored");
        Entity w = c;
        check(setBezierAnchorWeight(w, 1, 2.0) && !(w.conicRho > 0.0),
              "  so does a weight edit");
        Entity degenerate = c;
        if (built) {
            // The start tangent now lies along the chord: parallel to the end
            // tangent? No, but with both handles on the chord the tangents
            // are parallel: no apex.
            degenerate.points[1] = Point3(2.0, 0.0);
            degenerate.points[2] = Point3(8.0, 0.0);
        }
        Point2D none;
        check(built && !conicApex(degenerate, none) && !setConicRho(degenerate, 0.5),
              "parallel end tangents: no apex, re-authoring refused");

        // Save/load keeps the rho (both JSON halves write conic_rho).
        const std::string base = argc > 1 ? argv[1] : "/tmp/hobbycad_conic_rho";
        std::filesystem::remove_all(base); std::filesystem::create_directories(base);
        const std::string dir = base + "/proj";
        {
            Project p; SketchData sk; sk.name = "conic";
            sk.entities.push_back(c);
            sk.entities.push_back(plain);
            p.addSketch(sk);
            std::string err;
            check(p.save(dir, &err), "save a sketch holding a conic and a plain bezier");
        }
        {
            Project p; std::string err;
            check(p.load(dir, &err), "reload it");
            const bool ok = !p.sketches().empty() && p.sketches()[0].entities.size() == 2;
            check(ok && near(p.sketches()[0].entities[0].conicRho, 0.3)
                  && p.sketches()[0].entities[0].splineRational,
                  "the conic comes back with rho 0.3 and its weights");
            check(ok && !(p.sketches()[0].entities[1].conicRho > 0.0),
                  "the plain bezier comes back with no rho");
        }
    }

    std::printf("\n%s (%d failure(s))\n", failures ? "FAILURES" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
