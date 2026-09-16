// =====================================================================
//  tests/project/constraint_labels.cpp
//  Constraint display fields survive a project round trip.
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  ConstraintData is a single type unified with sketch::Constraint. The
//  solver ignores the display fields (labelPosition, labelAngle,
//  labelVisible) and the parametric expression, but a saved project must
//  keep them: they are what places a dimension on screen and what ties a
//  value to a parameter. This guards every one of those fields across BOTH
//  serializer backends: if a future edit drops one from a writer or
//  reader (the "add a field in six places" failure this unification is
//  meant to make rarer), this test fails loudly.
#include <hobbycad/project.h>
#include <cmath>
#include <cstdio>
#include <filesystem>

using namespace hobbycad;
namespace fs = std::filesystem;

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}
static bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

int main(int argc, char** argv) {
    const std::string base = argc > 1 ? argv[1] : "/tmp/hobbycad_constraint_labels";
    fs::remove_all(base);
    fs::create_directories(base);
    std::printf("constraint display fields round trip\n");

    const std::string dir = base + "/proj";
    {
        Project p;
        SketchData s; s.name = "dims";

        SketchEntityData line; line.type = sketch::EntityType::Line;
        line.points = { {0.0, 0.0}, {50.0, 0.0} };
        s.entities.push_back(line);

        // A reference (non-driving) distance whose value came from an
        // expression, with a deliberately non-default label placement.
        ConstraintData c;
        c.id = 7;
        c.type = ConstraintType::Distance;
        c.entityIds = { 1 };
        c.value = 25.0;
        c.expression = "width/2";
        c.isDriving = false;
        c.labelPosition = Point2D(3.5, 7.25);
        c.labelVisible = false;
        c.labelAngle = 0.7853981633974483;   // 45 degrees in radians
        s.constraints.push_back(c);

        p.addSketch(s);
        std::string err;
        check(p.save(dir, &err), "save succeeds");
    }

    {
        Project p; std::string err;
        check(p.load(dir, &err), "load succeeds");
        check(p.sketches().size() == 1, "the sketch loads");
        if (!p.sketches().empty()) {
            const auto& cs = p.sketches()[0].constraints;
            check(cs.size() == 1, "the constraint loads");
            if (cs.size() == 1) {
                const auto& c = cs[0];
                check(c.type == ConstraintType::Distance, "type survives");
                check(near(c.value, 25.0), "value survives");
                check(c.expression == "width/2", "expression survives");
                check(c.isDriving == false, "driving/reference flag survives");
                check(near(c.labelPosition.x, 3.5) && near(c.labelPosition.y, 7.25),
                      "label position survives");
                check(c.labelVisible == false, "label visibility survives");
                check(!std::isnan(c.labelAngle) && near(c.labelAngle, 0.7853981633974483),
                      "label angle survives");
            }
        }
    }

    std::printf("\n%s (%d failure(s))\n", failures ? "FAILURES" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
