// =====================================================================
//  tests/project/bezier_curvature.cpp — bezier splines and the G2-family
//  constraints survive a JSON save/load. SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  ConstraintType is serialized by its integer value, so this also guards the
//  enum order: if a member is inserted mid-enum, an older file (or these newer
//  types) would decode to the wrong constraint. New members must be appended.
#include <hobbycad/project.h>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;
using namespace hobbycad;
static int failures = 0;
static void check(bool ok, const char* w) { std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", w); if (!ok) ++failures; }
static bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

int main(int argc, char** argv) {
    const std::string base = argc > 1 ? argv[1] : "/tmp/hobbycad_bezier_curvature";
    fs::remove_all(base); fs::create_directories(base);
    std::printf("bezier + G2-family constraints round trip\n");
    const std::string dir = base + "/proj";
    {
        Project p; SketchData s; s.name = "g2";
        SketchEntityData bez;  bez.type  = sketch::EntityType::Spline; bez.splineBezier  = true;
        bez.points  = { {0,0}, {1,0}, {2,0}, {3,0} };
        SketchEntityData bez2; bez2.type = sketch::EntityType::Spline; bez2.splineBezier = true;
        bez2.points = { {3,0}, {4,0}, {5,0}, {6,0} };
        s.entities.push_back(bez);
        s.entities.push_back(bez2);
        // a RATIONAL bezier spline with non-unit weights
        SketchEntityData rat; rat.type = sketch::EntityType::Spline;
        rat.splineBezier = true; rat.splineRational = true;
        rat.points = { {0,0}, {0,2}, {2,2}, {2,0} };
        rat.weights = { 1.0, 2.0, 2.0, 1.0 };
        s.entities.push_back(rat);

        ConstraintData c1; c1.id = 1; c1.type = ConstraintType::Curvature;
        c1.entityIds = {1, 2}; c1.pointIndices = {1, 0};
        ConstraintData c2; c2.id = 2; c2.type = ConstraintType::PointOnSpline;
        c2.entityIds = {3, 1}; c2.pointIndices = {0, 0};
        ConstraintData c3; c3.id = 3; c3.type = ConstraintType::CurvatureDimension;
        c3.entityIds = {1}; c3.pointIndices = {1}; c3.value = 4.0;
        s.constraints = { c1, c2, c3 };

        p.addSketch(s);
        std::string err; check(p.save(dir, &err), "save succeeds");
    }
    {
        Project p; std::string err;
        check(p.load(dir, &err), "load succeeds");
        check(p.sketches().size() == 1, "the sketch loads");
        if (!p.sketches().empty()) {
            const auto& es = p.sketches()[0].entities;
            check(es.size() == 3 && es[0].type == sketch::EntityType::Spline && es[0].splineBezier,
                  "the splineBezier flag survives");
            check(es.size() == 3 && es[2].splineRational &&
                  es[2].weights.size() == 4 && std::fabs(es[2].weights[1]-2.0) < 1e-12,
                  "a rational spline's flag and weights survive the round trip");
            check(es.size() == 3 && es[0].points.size() == 4, "bezier control points survive");
            const auto& cs = p.sketches()[0].constraints;
            check(cs.size() == 3, "all three constraints load");
            bool cv = false, pos = false, cd = false;
            for (const auto& c : cs) {
                if (c.type == ConstraintType::Curvature)
                    cv = (c.entityIds == std::vector<int>{1, 2} && c.pointIndices == std::vector<int>{1, 0});
                if (c.type == ConstraintType::PointOnSpline)
                    pos = (c.entityIds == std::vector<int>{3, 1});
                if (c.type == ConstraintType::CurvatureDimension)
                    cd = (near(c.value, 4.0) && c.pointIndices == std::vector<int>{1});
            }
            check(cv, "Curvature type + ids + ends survive");
            check(pos, "PointOnSpline type + ids survive");
            check(cd, "CurvatureDimension type + value + end survive");
        }
    }
    std::printf("\n%s (%d failure(s))\n", failures ? "FAILURES" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
